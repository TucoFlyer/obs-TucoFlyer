#include "flyer-camera-filter.h"
#include <algorithm>

#define S_CONNECTION_FILE_PATH      "connection_file_path"
#define S_IMAGES_PATH               "images_path"

#define T_CONNECTION_FILE_PATH          obs_module_text("Bot-Controller \"connection.txt\" file")
#define T_CONNECTION_FILE_PATH_FILTER   "Connection info (*.txt);;All files (*.*)"
#define T_IMAGES_PATH                   obs_module_text("Bot-Controller \"images\" directory")

FlyerCameraFilter::FlyerCameraFilter(obs_source_t* source)
    : source(source),
      yolo(obs_module_file("tiny-yolo.cfg"),
           obs_module_file("tiny-yolo.weights")),
      image_captured_this_tick(false),
      overlay_texture(0),
      capture_texrender(0),
      capture_staging(0)
{
    load_names(obs_module_file("coco.names"));
    memset(&reduced_image, 0, sizeof reduced_image);
}

FlyerCameraFilter::~FlyerCameraFilter()
{
    if (capture_texrender) {
        gs_texrender_destroy(capture_texrender);
    }
    if (capture_staging) {
        gs_stagesurface_destroy(capture_staging);
    }
    if (reduced_image.data) {
        delete reduced_image.data;
    }
    if (overlay_texture) {
        gs_texture_destroy(overlay_texture);
    }
}

void FlyerCameraFilter::get_defaults(obs_data_t* settings)
{
    // We only have path configuration currently, and paths don't seem to get defaults this way
}

obs_properties_t* FlyerCameraFilter::get_properties()
{
    obs_properties_t *props = obs_properties_create();
    
    obs_properties_add_path(props, S_CONNECTION_FILE_PATH, T_CONNECTION_FILE_PATH, OBS_PATH_FILE,
        T_CONNECTION_FILE_PATH_FILTER, connection_file_path.c_str());
	 
    obs_properties_add_path(props, S_IMAGES_PATH, T_IMAGES_PATH, OBS_PATH_DIRECTORY,
        NULL, images_path.c_str());

    return props;
}

void FlyerCameraFilter::update(obs_data_t* settings)
{
    connection_file_path = obs_data_get_string(settings, S_CONNECTION_FILE_PATH);
    images_path = obs_data_get_string(settings, S_IMAGES_PATH);

    blog(LOG_INFO, "Update! connection path: '%s' images path: '%s'\n",
        connection_file_path.c_str(),
        images_path.c_str());
}

void FlyerCameraFilter::load_names(const char* filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Can't open detector labels in FlyerCameraFilter");
        abort();
    }

    char line_buf[160];
    names.clear();
    while (fgets(line_buf, sizeof line_buf, f)) {
        char *trim = strrchr(line_buf, '\n');
        if (trim) *trim = '\0';
        names.push_back(line_buf);
    }
    fclose(f);
}

void FlyerCameraFilter::video_tick(float seconds)
{
    image_captured_this_tick = false;
}

void FlyerCameraFilter::video_render(gs_effect* effect)
{
    if (!image_captured_this_tick) {
        image_captured_this_tick = capture_reduced_image();
        if (image_captured_this_tick) {
            detect_objects();
        }
    }

    obs_source_t *target = obs_filter_get_target(source);
    if (target) {
        obs_source_video_render(target);
    } else {
        obs_source_skip_video_filter(source);
    }

    draw_overlay();
}

bool FlyerCameraFilter::capture_reduced_image()
{
    obs_source_t *target = obs_filter_get_target(source);
    obs_source_t *parent = obs_filter_get_parent(source);

    if (!target || !parent) {
        return false;
    }

    int net_width = yolo.get_net_width();
    int net_height = yolo.get_net_height();
    int target_width = obs_source_get_base_width(target);
    int target_height = obs_source_get_base_height(target);
    if (!target_width || !target_height) {
        return false;
    }

    // Resource allocation
    if (capture_texrender) {
        gs_texrender_reset(capture_texrender);
    } else {
        capture_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    }
    if (!capture_staging) {
        capture_staging = gs_stagesurface_create(net_width, net_height, GS_RGBA);
    }
    if (!reduced_image.data) {
        reduced_image.w = net_width;
        reduced_image.h = net_height;
        reduced_image.c = 3;
        reduced_image.data = new float[net_width * net_height * 3];
    }

    // Render next target into a temporary texture
    if (gs_texrender_begin(capture_texrender, net_width, net_height)) {
        struct vec4 clear_color;
        vec4_zero(&clear_color);
        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
        gs_ortho(0.0f, (float)target_width, 0.0f, (float)target_height, -100.0, 100.0);

        obs_source_video_render(target);

        gs_texrender_end(capture_texrender);
    }

    // Must copy texture into a staging buffer to read it back
    gs_stage_texture(capture_staging, gs_texrender_get_texture(capture_texrender));

    uint8_t *ptr;
    uint32_t linesize;
    if (gs_stagesurface_map(capture_staging, &ptr, &linesize)) {

        // Copy low-res RGBA image to planar RGB floating point buffer
        for (int y = 0; y < net_height; y++) {
            for (int x = 0; x < net_width; x++) {
                for (int channel = 0; channel < 3; channel++) {
                    float pixel = ptr[channel + x*4 + y*linesize] / 255.0;
                    reduced_image.data[ x + y*net_width + channel*net_width*net_height ] = pixel;
                }
            }
        }

        gs_stagesurface_unmap(capture_staging);
    }

    return true;
}

void FlyerCameraFilter::draw_overlay()
{
    obs_source_t *target = obs_filter_get_target(source);
    if (!target) {
        return;
    }

    if (!overlay_texture) {
        // Placeholder

        uint8_t pixels[] = {
            0x80, 0x80, 0x80, 0x80,
            0x80, 0x80, 0x80, 0x80,
            0x80, 0x80, 0x80, 0x80,
            0x80, 0x80, 0x80, 0x80,
        };
        const uint8_t* tex_data = pixels;
        overlay_texture = gs_texture_create(2, 2, GS_BGRA, 1, &tex_data, GS_DYNAMIC);
    }

    // int net_width = yolo.get_net_width();
    // int net_height = yolo.get_net_height();
    // int target_width = obs_source_get_base_width(target);
    // int target_height = obs_source_get_base_height(target);

    // for (int n = 0; n < boxes.size(); n++) {
    //  bbox_t &box = boxes[n];
    //  float x = (float)box.x * target_width / net_width;
    //  float y = (float)box.y * target_width / net_width;
    //  float w = (float)box.w * target_height / net_height;
    //  float h = (float)box.h * target_height / net_height;

//      gs_matrix_push();
//      gs_matrix_translate3f(x, y, 0);
        // gs_draw_sprite(overlay_texture, 0, w, 2);
        // gs_draw_sprite(overlay_texture, 0, 2, h);
        // gs_matrix_pop();
    // }

//      gs_matrix_push();
//      gs_matrix_translate3f(x, y, 0);
        gs_draw_sprite(overlay_texture, 0, 100, 100);
        // gs_matrix_pop();

}

void FlyerCameraFilter::detect_objects()
{
    boxes = yolo.detect(reduced_image);

    for (int n = 0; n < boxes.size(); n++) {
        bbox_t &box = boxes[n];
        if (box.obj_id < names.size()) {
            const char* name = names[box.obj_id].c_str();
            printf("[%d] box (%d,%d,%d,%d) prob %f obj %s id %d\n",
                n, box.x, box.y, box.w, box.h, box.prob, name, box.track_id);
        }
    }
}

void FlyerCameraFilter::module_load() {

    obs_source_info info = {};

    info.id = "flyer_camera_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO;

    info.get_defaults = FlyerCameraFilter::get_defaults;

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
