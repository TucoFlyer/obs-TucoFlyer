#include "overlay-drawing.h"
#include <algorithm>

OverlayDrawing::OverlayDrawing()
    : effect(0),
      vb(0),
      vb_size(0)
{
    memset(&texture_img, 0, sizeof texture_img);

    obs_enter_graphics();
    effect = gs_effect_create_from_file(obs_module_file("overlay.effect"), NULL);
    image_param = gs_effect_get_param_by_name(effect, "image");
    image_size_param = gs_effect_get_param_by_name(effect, "image_size");
    source_size_param = gs_effect_get_param_by_name(effect, "source_size");
    obs_leave_graphics();
}

OverlayDrawing::~OverlayDrawing()
{
    obs_enter_graphics();
    gs_image_file_free(&texture_img);
    gs_effect_destroy(effect);
    if (vb) {
        gs_vertexbuffer_destroy(vb);
    }
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
    const unsigned max_vertex_limit = 65000;
    const unsigned verts_per_scene_item = 6;

    unsigned scene_size = json_array_size(obj);
    if (scene_size > max_vertex_limit / verts_per_scene_item) {
        return;
    }
    unsigned num_vertices_needed = scene_size * verts_per_scene_item;

    obs_enter_graphics();

    if (num_vertices_needed > vb_size) {
        if (vb) {
            gs_vertexbuffer_destroy(vb);
        }
        vb = create_vb(num_vertices_needed);
        vb_size = num_vertices_needed;
    }

    gs_vb_data *vbd = gs_vertexbuffer_get_data(vb);
    vb_draw_len = num_vertices_needed;
    unsigned vert_i = 0;

    for (unsigned scene_i = 0; scene_i < scene_size; scene_i++) {
        json_t *item = json_array_get(obj, scene_i);

        double src[4], dest[4], rgba[4];
        if (json_unpack(item, "{s[FFFF]s[FFFF]s[FFFF]}",
                "src", &src[0], &src[1], &src[2], &src[3],
                "dest", &dest[0], &dest[1], &dest[2], &dest[3],
                "rgba", &rgba[0], &rgba[1], &rgba[2], &rgba[3]) == 0) {

            uint8_t r8 = (uint8_t)std::max(0.0, std::min(255.0, round(rgba[0] * 255.0)));
            uint8_t g8 = (uint8_t)std::max(0.0, std::min(255.0, round(rgba[1] * 255.0)));
            uint8_t b8 = (uint8_t)std::max(0.0, std::min(255.0, round(rgba[2] * 255.0)));
            uint8_t a8 = (uint8_t)std::max(0.0, std::min(255.0, round(rgba[3] * 255.0)));
	    uint32_t color = r8 | (g8 << 8) | (b8 << 16) | (a8 << 24);

            vec2 tex_topleft, tex_topright, tex_botleft, tex_botright;
            vec3 dst_topleft, dst_topright, dst_botleft, dst_botright;

            vec2_set(&tex_topleft,  (float)src[0],          (float)src[1]);
            vec2_set(&tex_topright, (float)(src[0]+src[2]), (float)src[1]);
            vec2_set(&tex_botleft,  (float)src[0],          (float)(src[1]+src[3]));
            vec2_set(&tex_botright, (float)(src[0]+src[2]), (float)(src[1]+src[3]));

            vec3_set(&dst_topleft,  (float)dest[0],           (float)dest[1], 0.0f);
            vec3_set(&dst_topright, (float)(dest[0]+dest[2]), (float)dest[1], 0.0f);
            vec3_set(&dst_botleft,  (float)dest[0],           (float)(dest[1]+dest[3]), 0.0f);
            vec3_set(&dst_botright, (float)(dest[0]+dest[2]), (float)(dest[1]+dest[3]), 0.0f);

            vec3 *points = &vbd->points[vert_i];
            uint32_t *colors = &vbd->colors[vert_i];
            vec2 *texcoord = &((vec2*)vbd->tvarray[0].array)[vert_i];

            for (unsigned i = 0; i < verts_per_scene_item; i++) {
                colors[i] = color;
            }

            points[0] = dst_topleft;  texcoord[0] = tex_topleft;
            points[1] = dst_topright; texcoord[1] = tex_topright;
            points[2] = dst_botleft;  texcoord[2] = tex_botleft;

            points[3] = dst_topright; texcoord[3] = tex_topright;
            points[4] = dst_botright; texcoord[4] = tex_botright;
            points[5] = dst_botleft;  texcoord[5] = tex_botleft;

            vert_i += verts_per_scene_item;
        }
    }

    obs_leave_graphics();
}

void OverlayDrawing::render(obs_source_t *source)
{
    if (!texture_img.texture) {
        return;
    }
    if (!vb || !vb_draw_len) {
        return;
    }

    gs_technique_t *tech = gs_effect_get_technique(effect, "Draw");

    gs_vertexbuffer_flush(vb);
    gs_load_vertexbuffer(vb);
    gs_load_indexbuffer(NULL);

    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
    gs_enable_color(true, true, true, false);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);

    gs_effect_set_texture(image_param, texture_img.texture);

    vec2 image_size;
    vec2_set(&image_size, texture_img.cx, texture_img.cy);
    gs_effect_set_vec2(image_size_param, &image_size);

    vec2 source_size;
    vec2_set(&source_size,
        obs_source_get_width(source),
        obs_source_get_height(source));
    gs_effect_set_vec2(source_size_param, &source_size);

    gs_draw(GS_TRIS, 0, vb_draw_len);

    gs_technique_end_pass(tech);
    gs_technique_end(tech);

    gs_blend_state_pop();
}

gs_vertbuffer_t *OverlayDrawing::create_vb(unsigned new_size)
{
    gs_vb_data *vbd = gs_vbdata_create();

    vbd->num = new_size;
    vbd->num_tex = 1;

    vbd->points = (vec3*) bzalloc(sizeof(vec3) * new_size);
    vbd->colors = (uint32_t*) bzalloc(sizeof(uint32_t) * new_size);
    vbd->tvarray = (gs_tvertarray*) bzalloc(sizeof(gs_tvertarray) * vbd->num_tex);
    vbd->tvarray[0].width = 2;
    vbd->tvarray[0].array = bzalloc(sizeof(vec2) * new_size);

    return gs_vertexbuffer_create(vbd, GS_DYNAMIC);
}
