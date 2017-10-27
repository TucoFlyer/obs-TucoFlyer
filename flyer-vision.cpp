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
        Detector yolo(obs_module_file("yolo.cfg"),
                      obs_module_file("yolo.weights"));

        blog(LOG_INFO, "YOLO detector running");
        
        while (!request_exit.load()) {
        
            source->wait_for_frame(frame.counter);
            frame = source->get_latest_frame();

            image_t yolo_img = {};
            yolo_img.w = frame.width;
            yolo_img.h = frame.height;
            yolo_img.c = 3;
            yolo_img.data = frame.planar_float;

            // Input coordinate system is relative to (squished) image provided to neural net;
            // output coordinate system should match the overlay rendering, with [0,0] in the
            // center, aspect correct, and horizontal extents from [-1,+1].

            double x_scale = 2.0 / frame.width;
            double aspect = frame.source_width ? frame.source_height / (double) frame.source_width : 0.0;
            double y_scale = x_scale * aspect;

            double center_x = frame.width / 2.0;
            double center_y = frame.height / 2.0;

            boxes = yolo.detect(yolo_img, 0.1);
            if (bot->is_authenticated()) {
                Document d;
                d.SetObject();
                Value arr;
                arr.SetArray();

                for (int n = 0; n < boxes.size(); n++) {
                    bbox_t &box = boxes[n];

                    const char *label = "";
                    if (box.obj_id < names.size()) {
                        label = names[box.obj_id].c_str();
                    }

                    Value rect;
                    rect.SetArray();
                    rect.PushBack(Value(x_scale * (box.x - center_x)), d.GetAllocator());
                    rect.PushBack(Value(y_scale * (box.y - center_y)), d.GetAllocator());
                    rect.PushBack(Value(x_scale * box.w), d.GetAllocator());
                    rect.PushBack(Value(y_scale * box.h), d.GetAllocator());

                    Value obj;
                    obj.SetObject();
                    obj.AddMember("rect", rect, d.GetAllocator());
                    obj.AddMember("prob", Value(box.prob), d.GetAllocator());
                    obj.AddMember("label", StringRef(label), d.GetAllocator());
                    arr.PushBack(obj, d.GetAllocator());
                }

                Value cmd;
                cmd.SetObject();
                cmd.AddMember("CameraObjectDetection", arr, d.GetAllocator());
                d.AddMember("Command", cmd, d.GetAllocator());

                StringBuffer *buffer = new StringBuffer();
                Writer<StringBuffer> writer(*buffer);
                d.Accept(writer);
                bot->send(buffer);
            }
        }

        blog(LOG_INFO, "YOLO detector exiting");
    });
}
