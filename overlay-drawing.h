#pragma once

#include <obs-module.h>
#include <string>
#include <atomic>
#include <mutex>
#include <rapidjson/document.h>

extern "C" {
#include <graphics/graphics.h>
#include <graphics/effect.h>
#include <graphics/image-file.h>
}

class OverlayDrawing {
public:
    OverlayDrawing();
    ~OverlayDrawing();

    void set_texture_file_path(const char *path);
    void update_scene(rapidjson::Value const &scene);

    void render(obs_source_t *source);

private:
    std::string texture_path;

    gs_image_file_t texture_img;
    gs_effect_t *effect;
    gs_eparam_t *image_param;
    gs_eparam_t *image_size_param;
    gs_eparam_t *source_size_param;
    
    static constexpr uint32_t num_buffers = 2;
    std::atomic<uint32_t> draw_buffer;
    struct {
        gs_vertbuffer_t *vb;
        gs_vb_data *vbd;
        uint32_t draw_len;
    } buffers[num_buffers];

    gs_vb_data *create_vbdata(unsigned new_size);
};
