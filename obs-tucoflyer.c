#include <obs-module.h>

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("obs-TucoFlyer", "en-US")

extern struct obs_source_info flyer_camera_filter;

bool obs_module_load(void)
{
	obs_register_source(&flyer_camera_filter);
	return true;
}
