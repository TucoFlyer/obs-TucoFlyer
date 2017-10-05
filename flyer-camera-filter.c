#include <obs-module.h>

struct flyer_camera_filter_data {
    obs_source_t        *context;
};

static const char *flyer_camera_filter_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("FlyerCameraFilter");
}


static void flyer_camera_filter_update(void *data, obs_data_t *settings)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);    
}

static void flyer_camera_filter_destroy(void *data)
{
    bfree(data);
}

static void *flyer_camera_filter_create(obs_data_t *settings, obs_source_t *context)
{
    struct flyer_camera_filter_data *filter = bzalloc(sizeof(struct flyer_camera_filter_data));
    filter->context = context;

    return filter;
}

static void flyer_camera_filter_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(effect);
}

static obs_properties_t *flyer_camera_filter_properties(void *data)
{
    obs_properties_t *props = obs_properties_create();
    UNUSED_PARAMETER(data);
    return props;
}

static void flyer_camera_filter_defaults(obs_data_t *settings)
{
}

struct obs_source_info flyer_camera_filter = {
    .id                     = "flyer_camera_filter",
    .type                   = OBS_SOURCE_TYPE_FILTER,
    .output_flags           = OBS_SOURCE_VIDEO,
    .get_name               = flyer_camera_filter_name,
    .create                 = flyer_camera_filter_create,
    .destroy                = flyer_camera_filter_destroy,
    .video_render           = flyer_camera_filter_render,
    .update                 = flyer_camera_filter_update,
    .get_properties         = flyer_camera_filter_properties,
    .get_defaults           = flyer_camera_filter_defaults
};
