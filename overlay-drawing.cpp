#include "overlay-drawing.h"

OverlayDrawing::OverlayDrawing()
{
    memset(&texture_img, 0, sizeof texture_img);
}

OverlayDrawing::~OverlayDrawing()
{
    obs_enter_graphics();
    gs_image_file_free(&texture_img);
    obs_leave_graphics();
}

void OverlayDrawing::set_texture_file_path(const char *path)
{
    if (texture_path.compare(path)) {
        texture_path = path;

        obs_enter_graphics();

        gs_image_file_free(&texture_img);
        gs_image_file_init(&texture_img, texture_path.c_str());
        gs_image_file_init_texture(&texture_img);

        obs_leave_graphics();
    }
    texture_path = path;
}

void OverlayDrawing::update_scene(json_t *obj)
{

}

void OverlayDrawing::render()
{
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_OPAQUE);
    gs_technique_t *tech = gs_effect_get_technique(effect, "Draw");

    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture_img.texture);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);

    gs_draw_sprite(texture_img.texture, 0, texture_img.cx, texture_img.cy);

    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}
