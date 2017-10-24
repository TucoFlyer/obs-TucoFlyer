#pragma once

#include <obs-module.h>
#include <jansson.h>
#include <string>

class OverlayDrawing {
public:
    OverlayDrawing();
    ~OverlayDrawing();

    void set_texture_file_path(const char *path);
    void update_scene(json_t *obj);

    void render();

private:
    std::string texture_path;
    gs_texture_t *texture;
};
