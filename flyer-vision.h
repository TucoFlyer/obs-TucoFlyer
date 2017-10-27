#pragma once
#include "image-grabber.h"
#include "bot-connector.h"
#include <thread>
#include <vector>
#include <string>
#include <atomic>

class FlyerVision {
public:
    FlyerVision(ImageGrabber *source, BotConnector *bot);
    ~FlyerVision();

private:
    std::atomic<bool> request_exit;
    ImageGrabber *source;
    BotConnector *bot;
    std::thread yolo_thread;

    static std::vector<std::string> FlyerVision::load_names(const char* filename);

    void start_yolo();
};
