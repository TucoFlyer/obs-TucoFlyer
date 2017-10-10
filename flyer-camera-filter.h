#pragma once
#include <obs-module.h>
#include <vector>
#include <string>
#include "bot-connector.h"
#include "yolo/yolo_v2_class.hpp"

class FlyerCameraFilter {
public:
    FlyerCameraFilter(obs_source_t* source);
    ~FlyerCameraFilter();

    static void module_load();
    static void get_defaults(obs_data_t* settings);

    void video_tick(float seconds);
    void video_render(gs_effect* effect);
    void update(obs_data_t* settings);
    obs_properties_t* get_properties();

private:
    obs_source_t        *source;
    Detector            yolo;
    bool                image_captured_this_tick;
    gs_texture_t        *overlay_texture;
    gs_texrender_t      *capture_texrender;
    gs_stagesurf_t      *capture_staging;
    image_t             reduced_image;
    std::vector<bbox_t> boxes;
    std::vector<std::string> names;
    std::string         connection_file_path;
    std::string         images_path;
    BotConnector        bot;

    void load_names(const char* filename);
    bool capture_reduced_image();
    void update_overlay_texture();
    void draw_overlay();
    void detect_objects();
};
