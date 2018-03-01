#include "image-grabber.h"
#include <dlib/image_processing.h>

using namespace std::chrono_literals;

ImageGrabber::ImageGrabber(ImageFormatter &fmt, uint32_t frames)
    : fmt(fmt),
      num_frames(frames),
      latest_counter(0),
      frame_fifo(new Frame[num_frames]),
      tick_flag(false),
      readback_flag(false),
      texrender_4x(0),
      texrender_final(0),
      stagesurface(0)
{
    for (uint32_t i = 0; i < num_frames; i++) {
        frame_fifo[i].source_width = 0;
        frame_fifo[i].source_height = 0;
        frame_fifo[i].width = fmt.get_width();
        frame_fifo[i].height = fmt.get_height();
        frame_fifo[i].image = fmt.new_image();
    }

    obs_enter_graphics();

    effect = gs_effect_create_from_file(obs_module_file("scale_4x.effect"), NULL);
    image_param = gs_effect_get_param_by_name(effect, "image");
    image_size_param = gs_effect_get_param_by_name(effect, "image_size");

    obs_leave_graphics();
}

ImageGrabber::~ImageGrabber()
{
    obs_enter_graphics();

    for (uint32_t i = 0; i < num_frames; i++) {
        fmt.delete_image(frame_fifo[i].image);
    }
    delete frame_fifo;
    gs_effect_destroy(effect);
    if (texrender_4x) {
        gs_texrender_destroy(texrender_4x);
    }
    if (texrender_final) {
        gs_texrender_destroy(texrender_final);
    }
    if (stagesurface) {
        gs_stagesurface_destroy(stagesurface);
    }

    obs_leave_graphics();
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
    if (texrender_final) {
        gs_texrender_reset(texrender_4x);
        gs_texrender_reset(texrender_final);
    } else {
        texrender_4x = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        texrender_final = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    }
    if (!stagesurface) {
        stagesurface = gs_stagesurface_create(frame_width, frame_height, GS_RGBA);
    }

    // Render source into one render target at 4x final size
    if (gs_texrender_begin(texrender_4x, frame_width * 4, frame_height * 4)) {
        struct vec4 clear_color;
        vec4_zero(&clear_color);
        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
        gs_ortho(0.0f, (float)target_width, 0.0f, (float)target_height, -100.0, 100.0);

        obs_source_video_render(target);

        gs_texrender_end(texrender_4x);
    }

    // Scale from the 4x texture to the final size with a shader
    if (gs_texrender_begin(texrender_final, frame_width, frame_height)) {

        gs_ortho(0.0f, (float)frame_width, 0.0f, (float)frame_height, -100.0, 100.0);

        gs_blend_state_push();
        gs_enable_blending(true);
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

        while (gs_effect_loop(effect, "Draw")) {
            gs_effect_set_texture(image_param, gs_texrender_get_texture(texrender_4x));

            vec2 image_size;
            vec2_set(&image_size, frame_width, frame_height);
            gs_effect_set_vec2(image_size_param, &image_size);

            gs_draw_sprite(gs_texrender_get_texture(texrender_4x), false, frame_width, frame_height);
        }

        gs_blend_state_pop();
        gs_texrender_end(texrender_final);
    }

    // Must copy texture into a staging buffer to read it back later
    gs_stage_texture(stagesurface, gs_texrender_get_texture(texrender_final));
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

bool ImageGrabber::wait_for_frame(unsigned prev_counter)
{
    std::unique_lock<std::mutex> lock(latest_counter_mutex);
    return latest_counter_cond.wait_for(lock, 40ms, [=] { return latest_counter != prev_counter; });
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
