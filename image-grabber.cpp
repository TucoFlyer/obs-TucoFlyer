#include "image-grabber.h"
#include <dlib/image_processing.h>

ImageGrabber::ImageGrabber(uint32_t width, uint32_t height, uint32_t frames)
    : latest_counter(0),
      num_frames(frames),
      frame_fifo(0),
      tick_flag(false),
      texrender(0),
      stagesurface(0)
{
    frame_fifo = new Frame[num_frames];
    for (uint32_t i = 0; i < num_frames; i++) {
        frame_fifo[i].source_width = 0;
        frame_fifo[i].source_height = 0;
        frame_fifo[i].width = width;
        frame_fifo[i].height = height;
        frame_fifo[i].dlib_img = new dlib::array2d<dlib::rgb_pixel>(height, width);
        frame_fifo[i].planar_float = new float[width * height * 3];
    }
}

ImageGrabber::~ImageGrabber()
{
    for (uint32_t i = 0; i < num_frames; i++) {
        delete frame_fifo[i].dlib_img;
        delete frame_fifo[i].planar_float;
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
    uint32_t frame_area = frame_width * frame_height;

    int target_width = obs_source_get_base_width(target);
    int target_height = obs_source_get_base_height(target);
    if (!target_width || !target_height) {
        return;
    }

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

    uint8_t *ptr;
    uint32_t linesize;
    if (gs_stagesurface_map(stagesurface, &ptr, &linesize)) {

        // Format conversions
        for (uint32_t y = 0; y < frame_height; y++) {
            for (uint32_t x = 0; x < frame_width; x++) {

                uint8_t *rgba = &ptr[x*4 + y*linesize];

                uint8_t r8 = rgba[0];
                uint8_t g8 = rgba[1];
                uint8_t b8 = rgba[2];

        		dlib::rgb_pixel pix(r8, g8, b8);
        		assign_pixel((*frame->dlib_img)[y][x], pix);

                frame->planar_float[x + y*frame_width + 0*frame_area] = r8 / 255.0f;
                frame->planar_float[x + y*frame_width + 1*frame_area] = g8 / 255.0f;
                frame->planar_float[x + y*frame_width + 2*frame_area] = b8 / 255.0f;
            }
        }

        gs_stagesurface_unmap(stagesurface);
    }

    // Save our source's size, for coordinate transformation after running computer vision
    frame->source_width = obs_source_get_base_width(source);
    frame->source_height = obs_source_get_base_height(source);

    finish_writing_frame(frame);
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
