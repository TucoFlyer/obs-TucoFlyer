#include "flyer-vision-detector.h"
#include "yolo/yolo_v2_class.hpp"
#include "util/platform.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace rapidjson;

FlyerVisionDetector::FlyerVisionDetector(ImageGrabber *source, BotConnector *bot)
    : request_exit(false), source(source), bot(bot)
{
    start();
}

FlyerVisionDetector::~FlyerVisionDetector()
{
    request_exit.store(true);
    thread.join();
}

std::vector<std::string> FlyerVisionDetector::load_names(const char* filename)
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

void FlyerVisionDetector::start()
{
    thread = std::thread([=] () {

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
            yolo_img.data = static_cast<float*>(frame.image);

            // Input coordinate system is relative to (squished) image provided to neural net;
            // output coordinate system should match the overlay rendering, with [0,0] in the
            // center, aspect correct, and horizontal extents from [-1,+1].

            double x_scale = 2.0 / frame.width;
            double aspect = frame.source_width ? frame.source_height / (double) frame.source_width : 0.0;
            double y_scale = x_scale * aspect;

            double center_x = frame.width / 2.0;
            double center_y = frame.height / 2.0;

            uint64_t timestamp_1 = os_gettime_ns();
            boxes = yolo.detect(yolo_img, 0.1);
            uint64_t timestamp_2 = os_gettime_ns();

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
                scene.AddMember("detector_nsec", Value(timestamp_2 - timestamp_1), d.GetAllocator());

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

uint32_t DetectorImageFormatter::get_width() {
    return 608;
}

uint32_t DetectorImageFormatter::get_height() {
    return 608;
}

void* DetectorImageFormatter::new_image() {
    return static_cast<void*>(new float[get_width() * get_height() * 3]);
}

void DetectorImageFormatter::delete_image(void* frame) {
    delete static_cast<float*>(frame);
}

void DetectorImageFormatter::rgba_to_image(void* frame, const uint8_t* rgba, uint32_t linesize) {
    float *planar = static_cast<float*>(frame);
    uint32_t frame_width = get_width();
    uint32_t frame_height = get_height();
    uint32_t frame_area = frame_width * frame_height;

    for (uint32_t y = 0; y < frame_height; y++) {
        for (uint32_t x = 0; x < frame_width; x++) {

            const uint8_t *pix = &rgba[x*4 + y*linesize];
            uint8_t r8 = pix[0];
            uint8_t g8 = pix[1];
            uint8_t b8 = pix[2];

            planar[x + y*frame_width + 0*frame_area] = r8 / 255.0f;
            planar[x + y*frame_width + 1*frame_area] = g8 / 255.0f;
            planar[x + y*frame_width + 2*frame_area] = b8 / 255.0f;
        }
    }
}
