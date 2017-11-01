#pragma once
#include "image-grabber.h"
#include "bot-connector.h"
#include <thread>
#include <vector>
#include <string>
#include <atomic>

class FlyerVisionTracker {
public:
    FlyerVisionTracker(ImageGrabber *source, BotConnector *bot);
    ~FlyerVisionTracker();

private:
    std::atomic<bool> request_exit;
    ImageGrabber *source;
    BotConnector *bot;
    std::thread thread;

    void start();
};

class TrackerImageFormatter : public ImageFormatter {
public:
    uint32_t get_width();
    uint32_t get_height();
    void* new_image();
    void delete_image(void* frame);
    void rgba_to_image(void* frame, const uint8_t* rgba, uint32_t linesize);
};
