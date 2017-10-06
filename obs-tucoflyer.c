#include <obs-module.h>
#include "flyer-camera-filter.h"

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("obs-TucoFlyer", "en-US")

struct obs_source_info flyer_camera_filter = {
	.id = "flyer_camera_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC,
	.get_name = flyer_camera_filter_name,
	.create = flyer_camera_filter_create,
	.destroy = flyer_camera_filter_destroy,
	.filter_video = flyer_camera_filter_video,
	.update = flyer_camera_filter_update,
	.get_properties = flyer_camera_filter_properties,
	.get_defaults = flyer_camera_filter_defaults
};

bool obs_module_load(void)
{
	obs_register_source(&flyer_camera_filter);
	return true;
}
