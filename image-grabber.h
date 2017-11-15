#pragma once
#include <obs-module.h>
#include <stdint.h>
#include <mutex>
#include <condition_variable>

class ImageFormatter {
public:
    virtual uint32_t get_width() = 0;
    virtual uint32_t get_height() = 0;
    virtual void* new_image() = 0;
    virtual void delete_image(void* frame) = 0;
    virtual void rgba_to_image(void* frame, const uint8_t* rgba, uint32_t linesize) = 0;
};

class ImageGrabber {
public:
    ImageGrabber(ImageFormatter &fmt, uint32_t frames = 16);
    ~ImageGrabber();

    void tick();
    void render(obs_source_t* source);
    void post_render();

    struct Frame {
        uint32_t width, height;
        uint32_t source_width, source_height;
        unsigned counter;
        void *image;
    };

    void wait_for_frame(unsigned prev_counter);
    Frame get_latest_frame();

private:
    ImageFormatter &fmt;
    std::mutex latest_counter_mutex;
    std::condition_variable latest_counter_cond;
    uint32_t latest_counter;
    uint32_t num_frames;
    Frame *frame_fifo;
    bool tick_flag;
    bool readback_flag;

    gs_texrender_t *texrender_4x;
    gs_texrender_t *texrender_final;
    gs_stagesurf_t *stagesurface;

    gs_effect_t *effect;
    gs_eparam_t *image_param;
    gs_eparam_t *image_size_param;

    Frame *next_writable_frame();
    void finish_writing_frame(Frame *frame);
};
