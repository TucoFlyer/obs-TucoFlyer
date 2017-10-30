#include "flyer-vision.h"
#include "yolo/yolo_v2_class.hpp"
#include "util/platform.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <dlib/image_processing/scan_fhog_pyramid.h>
#include <dlib/image_processing/correlation_tracker.h>

using namespace rapidjson;

FlyerVision::FlyerVision(ImageGrabber *source, BotConnector *bot)
    : request_exit(false), source(source), bot(bot)
{
    start_yolo();
    start_tracker();
}

FlyerVision::~FlyerVision()
{
    request_exit.store(true);
    yolo_thread.join();
    tracker_thread.join();
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

                Value scene;
                scene.SetObject();
                scene.AddMember("objects", arr, d.GetAllocator());
                scene.AddMember("frame", Value(frame.counter), d.GetAllocator());

                Value cmd;
                cmd.SetObject();
                cmd.AddMember("CameraObjectDetection", scene, d.GetAllocator());
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


void FlyerVision::start_tracker()
{
    tracker_thread = std::thread([=] () {

        ImageGrabber::Frame frame;
        frame.counter = 0;

        unsigned age = 0;
        bool rect_is_empty = true;
        dlib::correlation_tracker tracker(5, 4);

        blog(LOG_INFO, "Object tracker thread running");    
        while (!request_exit.load()) {
        
            source->wait_for_frame(frame.counter);
            frame = source->get_latest_frame();

            double x_scale = 2.0 / frame.width;
            double aspect = frame.source_width ? frame.source_height / (double) frame.source_width : 0.0;
            double y_scale = x_scale * aspect;
            double center_x = frame.width / 2.0;
            double center_y = frame.height / 2.0;

            double init_rect[4];
            if (bot->poll_for_tracking_region_reset(init_rect)) {
                rect_is_empty = init_rect[2] <= 0.0 || init_rect[3] <= 0.0;
                if (!rect_is_empty) {
                    dlib::drectangle rect(center_x + init_rect[0]/x_scale,
                                          center_y + init_rect[1]/y_scale,
                                          center_x + (init_rect[0] + init_rect[2])/x_scale,
                                          center_y + (init_rect[1] + init_rect[3])/y_scale);
                    tracker.start_track(*frame.dlib_img, rect);
                    age = 0;
                }
            } else if (!rect_is_empty) {
                age++;
                double psr = tracker.update(*frame.dlib_img);
                dlib::drectangle rect = tracker.get_position();

                Document d;
                d.SetObject();

                Value arr;
                arr.SetArray();
                arr.PushBack(Value((rect.left() - center_x) * x_scale), d.GetAllocator());
                arr.PushBack(Value((rect.top() - center_y) * y_scale), d.GetAllocator());
                arr.PushBack(Value(rect.width() * x_scale), d.GetAllocator());
                arr.PushBack(Value(rect.height() * y_scale), d.GetAllocator());

                Value obj;
                obj.SetObject();
                obj.AddMember("rect", arr, d.GetAllocator());
                obj.AddMember("frame", Value(frame.counter), d.GetAllocator());
                obj.AddMember("age", Value(age), d.GetAllocator());
                obj.AddMember("psr", Value(psr), d.GetAllocator());

                Value cmd;
                cmd.SetObject();
                cmd.AddMember("CameraRegionTracking", obj, d.GetAllocator());
                d.AddMember("Command", cmd, d.GetAllocator());

                StringBuffer *buffer = new StringBuffer();
                Writer<StringBuffer> writer(*buffer);
                d.Accept(writer);
                bot->send(buffer);
            }
        }

        blog(LOG_INFO, "Object tracker thread exiting");
    });
}


