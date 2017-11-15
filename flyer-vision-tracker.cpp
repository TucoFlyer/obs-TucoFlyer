#include "flyer-vision-tracker.h"
#include "util/platform.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <dlib/image_processing/scan_fhog_pyramid.h>
#include <dlib/image_processing/correlation_tracker.h>
#include <dlib/image_saver/save_png.h>

using namespace rapidjson;
using namespace dlib;

FlyerVisionTracker::FlyerVisionTracker(ImageGrabber *source, BotConnector *bot)
    : request_exit(false), source(source), bot(bot)
{
    start();
}

FlyerVisionTracker::~FlyerVisionTracker()
{
    request_exit.store(true);
    thread.join();
}

void FlyerVisionTracker::start()
{
    thread = std::thread([=] () { thread_func(); });
}

template <typename Allocator>
static Value drectangle_to_value(ImageGrabber::Frame &frame, drectangle &drect, Allocator &alloc) {
    double x_scale = 2.0 / frame.width;
    double aspect = frame.source_width ? frame.source_height / (double) frame.source_width : 0.0;
    double y_scale = x_scale * aspect;
    double center_x = frame.width / 2.0;
    double center_y = frame.height / 2.0;

    Value arr;
    arr.SetArray();
    arr.PushBack(Value((drect.left() - center_x) * x_scale), alloc);
    arr.PushBack(Value((drect.top() - center_y) * y_scale), alloc);
    arr.PushBack(Value(drect.width() * x_scale), alloc);
    arr.PushBack(Value(drect.height() * y_scale), alloc);
    return arr;
}

static drectangle drectangle_from_vec4(ImageGrabber::Frame &frame, double vec[4]) {
    double x_scale = 2.0 / frame.width;
    double aspect = frame.source_width ? frame.source_height / (double) frame.source_width : 0.0;
    double y_scale = x_scale * aspect;
    double center_x = frame.width / 2.0;
    double center_y = frame.height / 2.0;

    drectangle rect(center_x + vec[0]/x_scale,
                    center_y + vec[1]/y_scale,
                    center_x + (vec[0] + vec[2])/x_scale,
                    center_y + (vec[1] + vec[3])/y_scale);
    return rect;
}

void FlyerVisionTracker::thread_func()
{
    ImageGrabber::Frame frame;
    frame.counter = 0;

    drectangle previous_rect = {};
    unsigned age = 0;
    bool rect_is_empty = true;
    correlation_tracker tracker(6, 4);

    blog(LOG_INFO, "Object tracker thread running");
    while (!request_exit.load()) {

        source->wait_for_frame(frame.counter);
        frame = source->get_latest_frame();
        array2d<rgb_pixel> &array = *static_cast<array2d<rgb_pixel>*>(frame.image);

        if (!rect_is_empty) {
#if 0
            char name[200];
            snprintf(name, sizeof name, "fr-%08u-cont.png", (unsigned)frame.counter);
            save_png(array, name);
#endif

            age++;
            uint64_t timestamp_1 = os_gettime_ns();
            double psr = tracker.update_noscale(array);
            uint64_t timestamp_2 = os_gettime_ns();
            drectangle rect = tracker.get_position();

            // The tracker can fail and give us NaN sometimes, which makes JSON serialize fail
            if (!(psr >= 0.0)) psr = 0.0;

            Document d;
            d.SetObject();

            Value obj;
            obj.SetObject();
            obj.AddMember("rect", drectangle_to_value(frame, rect, d.GetAllocator()), d.GetAllocator());
            obj.AddMember("previous_rect", drectangle_to_value(frame, previous_rect, d.GetAllocator()), d.GetAllocator());
            obj.AddMember("frame", frame.counter, d.GetAllocator());
            obj.AddMember("age", age, d.GetAllocator());
            obj.AddMember("psr", psr, d.GetAllocator());
            obj.AddMember("tracker_nsec", Value(timestamp_2 - timestamp_1), d.GetAllocator());

            Value cmd;
            cmd.SetObject();
            cmd.AddMember("CameraRegionTracking", obj, d.GetAllocator());
            d.AddMember("Command", cmd, d.GetAllocator());

            StringBuffer *buffer = new StringBuffer();
            Writer<StringBuffer> writer(*buffer);
            d.Accept(writer);
            bot->send(buffer);

            previous_rect = rect;
        }

        double init_rect[4];
        if (bot->poll_for_tracking_region_reset(init_rect)) {
            rect_is_empty = init_rect[2] <= 0.0 || init_rect[3] <= 0.0;
            if (!rect_is_empty) {
#if 0
                char name[200];
                snprintf(name, sizeof name, "fr-%08u-init.png", (unsigned)frame.counter);
                save_png(array, name);
#endif
                drectangle rect = drectangle_from_vec4(frame, init_rect);
                tracker.start_track(array, rect);
                age = 0;
                previous_rect = rect;
            }
        }
    }

    blog(LOG_INFO, "Object tracker thread exiting");
}

uint32_t TrackerImageFormatter::get_width() {
    return 256;
}

uint32_t TrackerImageFormatter::get_height() {
    return 256;
}

void* TrackerImageFormatter::new_image() {
    return static_cast<void*>(new array2d<rgb_pixel>(get_width(), get_height()));
}

void TrackerImageFormatter::delete_image(void* frame) {
    delete static_cast<array2d<rgb_pixel>*>(frame);
}

void TrackerImageFormatter::rgba_to_image(void* frame, const uint8_t* rgba, uint32_t linesize) {
    array2d<rgb_pixel> &array = *static_cast<array2d<rgb_pixel>*>(frame);
    uint32_t frame_width = get_width();
    uint32_t frame_height = get_height();

    for (uint32_t y = 0; y < frame_height; y++) {
        for (uint32_t x = 0; x < frame_width; x++) {

            const uint8_t *pix = &rgba[x*4 + y*linesize];
            uint8_t r8 = pix[0];
            uint8_t g8 = pix[1];
            uint8_t b8 = pix[2];

            assign_pixel(array[y][x], rgb_pixel(r8, g8, b8));
        }
    }
}
