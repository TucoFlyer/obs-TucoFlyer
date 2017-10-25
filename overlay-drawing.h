#pragma once

#include <obs-module.h>
#include <jansson.h>
#include <string>

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
    void update_scene(json_t *obj);

    void render(obs_source_t *source);

private:
    std::string texture_path;
    gs_image_file_t texture_img;
    gs_effect_t *effect;
    gs_vertbuffer_t *vb;
    unsigned vb_size;
    unsigned vb_draw_len;
    gs_eparam_t *image_param;
    gs_eparam_t *image_size_param;
    gs_eparam_t *source_size_param;

    gs_vertbuffer_t *create_vb(unsigned new_size);
};
