#include "flyer-vision.h"
#include "yolo/yolo_v2_class.hpp"
#include "util/platform.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace rapidjson;

FlyerVision::FlyerVision(ImageGrabber *source, BotConnector *bot)
    : request_exit(false), source(source), bot(bot)
{
    start_yolo();
}

FlyerVision::~FlyerVision()
{
    request_exit.store(true);
    yolo_thread.join();
}

std::vector<std::string> FlyerVision::load_names(const char* filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Can't open detector labels in FlyerCameraFilter");
        abort();
    }

    char line_buf[160];
    std::vector<std::string> names;
    names.clear();
    while (fgets(line_buf, sizeof line_buf, f)) {
        char *trim = strrchr(line_buf, '\n');
        if (trim) *trim = '\0';
        names.push_back(line_buf);
    }
    fclose(f);
    return names;
}

void FlyerVision::start_yolo()
{
    yolo_thread = std::thread([=] () {

        std::vector<bbox_t> boxes;
        ImageGrabber::Frame frame;
        frame.counter = 0;

        blog(LOG_INFO, "YOLO detector starting up...");

        std::vector<std::string> names = load_names(obs_module_file("coco.names"));
        Detector yolo(obs_module_file("tiny-yolo.cfg"),
                      obs_module_file("tiny-yolo.weights"));

        blog(LOG_INFO, "YOLO detector running");
        
        while (!request_exit.load()) {
        
            source->wait_for_frame(frame.counter);
            frame = source->get_latest_frame();

            image_t yolo_img = {};
            yolo_img.w = frame.width;
            yolo_img.h = frame.height;
            yolo_img.c = 3;
            yolo_img.data = frame.planar_float;

            boxes = yolo.detect(yolo_img);

            Document d;
            d.SetObject();
            d.AddMember("Fluff", "yep", d.GetAllocator());

            StringBuffer *buffer = new StringBuffer();
            Writer<StringBuffer> writer(*buffer);
            d.Accept(writer);
            bot->send(buffer);

            for (int n = 0; n < boxes.size(); n++) {
                bbox_t &box = boxes[n];
                if (box.obj_id < names.size()) {
                    const char* name = names[box.obj_id].c_str();
                       blog(LOG_INFO, "[%d] box (%d,%d,%d,%d) prob %f obj %s id %d\n",
                       n, box.x, box.y, box.w, box.h, box.prob, name, box.track_id);
                }
            }
        }

        blog(LOG_INFO, "YOLO detector exiting");
    });
}
