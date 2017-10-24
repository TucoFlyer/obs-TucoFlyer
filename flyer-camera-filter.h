#pragma once
#include <obs-module.h>
#include <vector>
#include <string>
#include "bot-connector.h"
#include "image-grabber.h"
#include "flyer-vision.h"
#include "overlay-drawing.h"

class FlyerCameraFilter {
public:
    FlyerCameraFilter(obs_source_t* source);

    static void module_load();

    void video_tick(float seconds);
    void video_render(gs_effect* effect);
    void update(obs_data_t* settings);
    obs_properties_t* get_properties();

private:
    obs_source_t        *source;
    ImageGrabber        grabber;
    FlyerVision         vision;
    OverlayDrawing      overlay;
    BotConnector        bot;
    std::string         connection_file_path;
    std::string         overlay_texture_path;
};
