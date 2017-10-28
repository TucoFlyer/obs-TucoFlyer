#pragma once
#include <obs-module.h>
#include <stdint.h>
#include <mutex>
#include <condition_variable>
#include <dlib/array2d.h>

class ImageGrabber {
public:
    ImageGrabber(uint32_t width = 608, uint32_t height = 608, uint32_t frames = 16);
    ~ImageGrabber();

    void tick();
    void render(obs_source_t* source);

    struct Frame {
        uint32_t width, height;
        uint32_t source_width, source_height;
        unsigned counter;

        float *planar_float;
        dlib::array2d<dlib::rgb_pixel> *dlib_img;
    };

    void wait_for_frame(unsigned prev_counter);
    Frame get_latest_frame();

private:
    std::mutex latest_counter_mutex;
    std::condition_variable latest_counter_cond;
    uint32_t latest_counter;
    uint32_t num_frames;
    Frame *frame_fifo;
    bool tick_flag;

    gs_texrender_t *texrender;
    gs_stagesurf_t *stagesurface;

    Frame *next_writable_frame();
    void finish_writing_frame(Frame *frame);
};
