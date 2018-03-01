#pragma once
#include <obs-module.h>
#include <vector>
#include <string>
#include "bot-connector.h"
#include "image-grabber.h"
#include "flyer-vision-tracker.h"
#include "flyer-vision-detector.h"
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

    DetectorImageFormatter  fmt_detector;
    TrackerImageFormatter   fmt_tracker;
    ImageGrabber            grabber_detector;
    ImageGrabber            grabber_tracker;
    FlyerVisionDetector     vision_detector;
    FlyerVisionTracker      vision_tracker;

    float                   camera_output_status_timer;
    double                  streaming_active_timer;
    double                  recording_active_timer;

    OverlayDrawing      overlay;
    BotConnector        bot;

    std::string         connection_file_path;
    std::string         overlay_texture_path;

    void camera_output_enable(rapidjson::Value const &scene);
    void send_camera_output_status();
};
