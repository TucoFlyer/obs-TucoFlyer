#pragma once
#include "image-grabber.h"
#include "bot-connector.h"
#include <thread>
#include <vector>
#include <string>
#include <atomic>

class FlyerVisionDetector {
public:
    FlyerVisionDetector(ImageGrabber *source, BotConnector *bot);
    ~FlyerVisionDetector();

private:
    std::atomic<bool> request_exit;
    ImageGrabber *source;
    BotConnector *bot;
    std::thread thread;

    static std::vector<std::string> load_names(const char* filename);
    void start();
};

class DetectorImageFormatter : public ImageFormatter {
public:
    uint32_t get_width();
    uint32_t get_height();
    void* new_image();
    void delete_image(void* frame);
    void rgba_to_image(void* frame, const uint8_t* rgba, uint32_t linesize);
};
