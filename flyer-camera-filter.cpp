#include "flyer-camera-filter.h"
#include <obs-frontend-api.h>
#include <algorithm>
#include <functional>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace rapidjson;

#define S_CONNECTION_FILE_PATH      "connection_file_path"
#define S_OVERLAY_TEXTURE_PATH      "overlay_texture_path"

#define T_CONNECTION_FILE_PATH          obs_module_text("Controller \"connection.txt\" file")
#define T_CONNECTION_FILE_PATH_FILTER   "Connection info (*.txt);;All files (*.*)"
#define T_OVERLAY_TEXTURE_PATH          obs_module_text("Controller \"overlay.png\" file")
#define T_OVERLAY_TEXTURE_PATH_FILTER   "Texture (*.png);;All files (*.*)"

#define S_LOCAL_RECORDING               "LocalRecording"
#define S_LIVE_STREAM                   "LiveStream"

#define CAMERA_OUTPUT_STATUS_INTERVAL   0.2

static void output_timer_tick(obs_output_t* output, float tick_seconds, double* pTimer)
{
    if (output && obs_output_active(output)) {
        *pTimer += tick_seconds;
    } else {
        *pTimer = 0.0;
    }
}

template <typename Allocator>
static Value output_status(obs_output_t* output, double timer, Allocator &alloc)
{
    Value id;
    id.SetString(output ? obs_output_get_id(output) : "", alloc);

    uint32_t width = 0, height = 0;
    bool active = false;

    if (output && obs_output_active(output)) {
        active = true;
        width = obs_output_get_width(output);
        height = obs_output_get_height(output);
    }

    Value obj;
    obj.SetObject();
    obj.AddMember("active", active, alloc);
    obj.AddMember("reconnecting", output ? obs_output_reconnecting(output) : false, alloc);
    obj.AddMember("id", id, alloc);
    obj.AddMember("width", width, alloc);
    obj.AddMember("height", height, alloc);
    obj.AddMember("congestion", output ? obs_output_get_congestion(output) : 0.0f, alloc);
    obj.AddMember("total_bytes", output ? obs_output_get_total_bytes(output) : 0, alloc);
    obj.AddMember("total_frames", output ? obs_output_get_total_frames(output) : 0, alloc);
    obj.AddMember("frames_dropped", output ? obs_output_get_frames_dropped(output) : 0, alloc);
    obj.AddMember("active_seconds", timer, alloc);
    return obj;
}

FlyerCameraFilter::FlyerCameraFilter(obs_source_t* source)
    : source(source),
      grabber_detector(fmt_detector),
      grabber_tracker(fmt_tracker),
      vision_detector(&grabber_detector, &bot),
      vision_tracker(&grabber_tracker, &bot),
      camera_output_status_timer(0.0f),
      streaming_active_timer(0.0),
      recording_active_timer(0.0)
{
    bot.on_camera_overlay_scene = std::bind(&OverlayDrawing::update_scene, &overlay, std::placeholders::_1);
    bot.on_camera_output_enable = std::bind(&FlyerCameraFilter::camera_output_enable, this, std::placeholders::_1);
}

obs_properties_t* FlyerCameraFilter::get_properties()
{
    obs_properties_t *props = obs_properties_create();

    obs_properties_add_path(props, S_CONNECTION_FILE_PATH, T_CONNECTION_FILE_PATH, OBS_PATH_FILE,
        T_CONNECTION_FILE_PATH_FILTER, connection_file_path.c_str());

    obs_properties_add_path(props, S_OVERLAY_TEXTURE_PATH, T_OVERLAY_TEXTURE_PATH, OBS_PATH_FILE,
        T_OVERLAY_TEXTURE_PATH_FILTER, overlay_texture_path.c_str());

    return props;
}

void FlyerCameraFilter::update(obs_data_t* settings)
{
    connection_file_path = obs_data_get_string(settings, S_CONNECTION_FILE_PATH);
    overlay_texture_path = obs_data_get_string(settings, S_OVERLAY_TEXTURE_PATH);

    bot.set_connection_file_path(connection_file_path.c_str());
    overlay.set_texture_file_path(overlay_texture_path.c_str());
}

void FlyerCameraFilter::video_tick(float seconds)
{
    grabber_tracker.tick();
    grabber_detector.tick();

    camera_output_status_timer += seconds;
    if (camera_output_status_timer > CAMERA_OUTPUT_STATUS_INTERVAL) {
        camera_output_status_timer -= CAMERA_OUTPUT_STATUS_INTERVAL;
        if (camera_output_status_timer > CAMERA_OUTPUT_STATUS_INTERVAL) {
            camera_output_status_timer = 0.0f;
        }
        send_camera_output_status();
    }

    output_timer_tick(obs_frontend_get_recording_output(), seconds, &recording_active_timer);
    output_timer_tick(obs_frontend_get_streaming_output(), seconds, &streaming_active_timer);
}

void FlyerCameraFilter::video_render(gs_effect* effect)
{
    grabber_tracker.render(source);
    grabber_detector.render(source);

    obs_source_t *target = obs_filter_get_target(source);
    if (target) {
        obs_source_video_render(target);
    } else {
        obs_source_skip_video_filter(source);
    }

    overlay.render(source);

    grabber_tracker.post_render();
    grabber_detector.post_render();
}

void FlyerCameraFilter::camera_output_enable(rapidjson::Value const &cmd)
{
    if (cmd.IsArray() && cmd.Size() == 2 && cmd[0].IsString() && cmd[1].IsBool()) {
        const char* output = cmd[0].GetString();
        const bool enable = cmd[1].GetBool();

        if (!strcmp(output, S_LIVE_STREAM)) {
            if (enable) {
                obs_frontend_streaming_start();
            } else {
                obs_frontend_streaming_stop();
            }
        } else if (!strcmp(output, S_LOCAL_RECORDING)) {
            if (enable) {
                obs_frontend_recording_start();
            } else {
                obs_frontend_recording_stop();
            }
        }
    }
}

void FlyerCameraFilter::send_camera_output_status()
{
    Document d;
    d.SetObject();

    obs_output_t* recording = obs_frontend_get_recording_output();
    obs_output_t* streaming = obs_frontend_get_streaming_output();

    Value obj;
    obj.SetObject();
    obj.AddMember(S_LOCAL_RECORDING, output_status(recording, recording_active_timer, d.GetAllocator()), d.GetAllocator());
    obj.AddMember(S_LIVE_STREAM, output_status(streaming, streaming_active_timer, d.GetAllocator()), d.GetAllocator());

    Value cmd;
    cmd.SetObject();
    cmd.AddMember("CameraOutputStatus", obj, d.GetAllocator());
    d.AddMember("Command", cmd, d.GetAllocator());

    StringBuffer *buffer = new StringBuffer();
    Writer<StringBuffer> writer(*buffer);
    d.Accept(writer);
    bot.send(buffer);
}

void FlyerCameraFilter::module_load() {
    obs_source_info info = {};

    info.id = "flyer_camera_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO;

    info.get_name = [] (void*) {
        return obs_module_text("Flyer Camera Filter");
    };

    info.create = [] (obs_data_t* settings, obs_source_t* source) {
        obs_source_update(source, settings);
        return static_cast<void*>(new FlyerCameraFilter(source));
    };

    info.destroy = [] (void* filter) {
        delete static_cast<FlyerCameraFilter*>(filter);
    };

    info.video_tick = [] (void* filter, float seconds) {
        static_cast<FlyerCameraFilter*>(filter)->video_tick(seconds);
    };

    info.video_render = [] (void* filter, gs_effect* effect) {
        static_cast<FlyerCameraFilter*>(filter)->video_render(effect);
    };

    info.update = [] (void* filter, obs_data_t* settings) {
        static_cast<FlyerCameraFilter*>(filter)->update(settings);
	};

    info.get_properties = [] (void* filter) {
        return static_cast<FlyerCameraFilter*>(filter)->get_properties();
	};

    obs_register_source(&info);
}
