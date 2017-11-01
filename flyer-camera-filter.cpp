#include "flyer-camera-filter.h"
#include <algorithm>
#include <functional>

#define S_CONNECTION_FILE_PATH      "connection_file_path"
#define S_OVERLAY_TEXTURE_PATH      "overlay_texture_path"

#define T_CONNECTION_FILE_PATH          obs_module_text("Controller \"connection.txt\" file")
#define T_CONNECTION_FILE_PATH_FILTER   "Connection info (*.txt);;All files (*.*)"
#define T_OVERLAY_TEXTURE_PATH          obs_module_text("Controller \"overlay.png\" file")
#define T_OVERLAY_TEXTURE_PATH_FILTER   "Texture (*.png);;All files (*.*)"

FlyerCameraFilter::FlyerCameraFilter(obs_source_t* source)
    : source(source),
      grabber_detector(fmt_detector),
      grabber_tracker(fmt_tracker),
      vision_detector(&grabber_detector, &bot),
      vision_tracker(&grabber_tracker, &bot)
{
    bot.on_camera_overlay_scene = std::bind(&OverlayDrawing::update_scene, &overlay, std::placeholders::_1);
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
