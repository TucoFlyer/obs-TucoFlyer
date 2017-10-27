#include "overlay-drawing.h"
#include "json-util.h"
#include <algorithm>
#include <assert.h>

#define LOG_PREFIX      "OverlayDrawing: "

OverlayDrawing::OverlayDrawing()
    : effect(0),
      draw_buffer(0)
{
    memset(&texture_img, 0, sizeof texture_img);
    memset(&buffers, 0, sizeof buffers);

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

    for (uint32_t i = 0; i < num_buffers; i++) {
        if (buffers[i].vb) {
            gs_vertexbuffer_destroy(buffers[i].vb);
        }
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

void OverlayDrawing::update_scene(rapidjson::Value const &scene)
{
    const unsigned max_vertex_limit = 1024 * 1024 * 4;
    const unsigned verts_per_scene_item = 6;
    unsigned scene_size = scene.Size();
    if (scene_size > max_vertex_limit / verts_per_scene_item) {
        return;
    }
    unsigned num_vertices_needed = scene_size * verts_per_scene_item;

    // If we need to reallocate vertex buffers, take the (contentious, slow) graphics lock
    unsigned next_buffer = (draw_buffer.load() + 1) % num_buffers;
    if (!buffers[next_buffer].vb || num_vertices_needed > buffers[next_buffer].vbd->num) {

        obs_enter_graphics();

        // Round sizes up some to reallocate less frequently
        uint32_t padded_size = (num_vertices_needed + 1024) & ~511;
        blog(LOG_INFO, LOG_PREFIX "Resizing VB#%d to %d", next_buffer, padded_size);

        if (buffers[next_buffer].vb) {
            gs_vertexbuffer_destroy(buffers[next_buffer].vb);
        }
        buffers[next_buffer].vbd = create_vbdata(padded_size);
        buffers[next_buffer].vb = gs_vertexbuffer_create(buffers[next_buffer].vbd, GS_DYNAMIC);
        buffers[next_buffer].draw_len = 0;

        obs_leave_graphics();
    }

    gs_vb_data *vbd = buffers[next_buffer].vbd;
    unsigned vert_i = 0;

    for (unsigned scene_i = 0; scene_i < scene_size; scene_i++) {
        rapidjson::Value const &item = scene[scene_i];

        double src[4], dest[4], rgba[4];

        json_vec4(item, "src", src);
        json_vec4(item, "dest", dest);
        json_vec4(item, "rgba", rgba);

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

    assert(vert_i == num_vertices_needed);
    buffers[next_buffer].draw_len = num_vertices_needed;
    assert(next_scene_draw_len <= next_scene->num);
    draw_buffer.store(next_buffer);
}

void OverlayDrawing::render(obs_source_t *source)
{
    if (!texture_img.texture) {
        return;
    }

    uint32_t buffer_index = draw_buffer.load();
    gs_vertbuffer_t *vb = buffers[buffer_index].vb;
    uint32_t draw_len = buffers[buffer_index].draw_len;
    if (!vb || !draw_len) {
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

    gs_draw(GS_TRIS, 0, draw_len);

    gs_technique_end_pass(tech);
    gs_technique_end(tech);

    gs_blend_state_pop();
}

gs_vb_data *OverlayDrawing::create_vbdata(unsigned new_size)
{
    gs_vb_data *vbd = gs_vbdata_create();
    vbd->num = new_size;
    vbd->num_tex = 1;
    vbd->points = (vec3*) bzalloc(sizeof(vec3) * new_size);
    vbd->colors = (uint32_t*) bzalloc(sizeof(uint32_t) * new_size);
    vbd->tvarray = (gs_tvertarray*) bzalloc(sizeof(gs_tvertarray) * vbd->num_tex);
    vbd->tvarray[0].width = 2;
    vbd->tvarray[0].array = bzalloc(sizeof(vec2) * new_size);
    return vbd;
}
