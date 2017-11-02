#include "image-grabber.h"
#include <dlib/image_processing.h>

ImageGrabber::ImageGrabber(ImageFormatter &fmt, uint32_t frames)
    : fmt(fmt),
      num_frames(frames),
      latest_counter(0),
      frame_fifo(new Frame[num_frames]),
      tick_flag(false),
      readback_flag(false),
      texrender(0),
      stagesurface(0)
{
    for (uint32_t i = 0; i < num_frames; i++) {
        frame_fifo[i].source_width = 0;
        frame_fifo[i].source_height = 0;
        frame_fifo[i].width = fmt.get_width();
        frame_fifo[i].height = fmt.get_height();
        frame_fifo[i].image = fmt.new_image();
    }
}

ImageGrabber::~ImageGrabber()
{
    for (uint32_t i = 0; i < num_frames; i++) {
        fmt.delete_image(frame_fifo[i].image);
    }
    delete frame_fifo;
    if (texrender) {
        gs_texrender_destroy(texrender);
    }
    if (stagesurface) {
        gs_stagesurface_destroy(stagesurface);
    }
}

void ImageGrabber::tick()
{
    tick_flag = true;
}

void ImageGrabber::render(obs_source_t *source)
{
    // At most once per tick
    if (tick_flag == false) {
        return;
    }
    tick_flag = false;

    obs_source_t *target = obs_filter_get_target(source);
    obs_source_t *parent = obs_filter_get_parent(source);
    if (!target || !parent) {
        return;
    }

    Frame *frame = next_writable_frame();
    uint32_t frame_width = frame->width;
    uint32_t frame_height = frame->height;

    int target_width = obs_source_get_base_width(target);
    int target_height = obs_source_get_base_height(target);
    if (!target_width || !target_height) {
        return;
    }

    // Save our source's size, for coordinate transformation after running computer vision
    frame->source_width = obs_source_get_base_width(source);
    frame->source_height = obs_source_get_base_height(source);

    // Resource allocation
    if (texrender) {
        gs_texrender_reset(texrender);
    } else {
        texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    }
    if (!stagesurface) {
        stagesurface = gs_stagesurface_create(frame_width, frame_height, GS_RGBA);
    }

    // Render next target into a temporary texture
    if (gs_texrender_begin(texrender, frame_width, frame_height)) {
        struct vec4 clear_color;
        vec4_zero(&clear_color);
        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
        gs_ortho(0.0f, (float)target_width, 0.0f, (float)target_height, -100.0, 100.0);

        obs_source_video_render(target);

        gs_texrender_end(texrender);
    }

    // Must copy texture into a staging buffer to read it back
    gs_stage_texture(stagesurface, gs_texrender_get_texture(texrender));
    readback_flag = true;
}

void ImageGrabber::post_render()
{
    // Read back from the GPU some time after render(), for less stalling.

    if (readback_flag && stagesurface) {
        readback_flag = false;

        Frame *frame = next_writable_frame();
        uint8_t *ptr = 0;
        uint32_t linesize = 0;

        if (gs_stagesurface_map(stagesurface, &ptr, &linesize)) {
            fmt.rgba_to_image(frame->image, ptr, linesize);
            gs_stagesurface_unmap(stagesurface);
        }

        finish_writing_frame(frame);
    }
}

void ImageGrabber::wait_for_frame(unsigned prev_counter)
{
    std::unique_lock<std::mutex> lock(latest_counter_mutex);
    latest_counter_cond.wait(lock, [=] { return latest_counter != prev_counter; });
}

ImageGrabber::Frame ImageGrabber::get_latest_frame()
{
    std::unique_lock<std::mutex> lock(latest_counter_mutex);
    return frame_fifo[latest_counter % num_frames];    
}

ImageGrabber::Frame *ImageGrabber::next_writable_frame()
{
    uint32_t next_counter = latest_counter + 1;
    Frame *f = &frame_fifo[next_counter % num_frames];
    f->counter = next_counter;
    return f;
}

void ImageGrabber::finish_writing_frame(Frame *frame)
{
    std::unique_lock<std::mutex> lock(latest_counter_mutex);
    latest_counter = frame->counter;
    lock.unlock();
    latest_counter_cond.notify_all();
}
