#include <obs-module.h>
#include "flyer-camera-filter.h"

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("obs-TucoFlyer", "en-US")

bool obs_module_load(void)
{
	FlyerCameraFilter::module_load();
	return true;
}
