extern "C" {
	#include <obs-module.h>
    #include "flyer-camera-filter.h"
}

#include "yolo/yolo_v2_class.hpp"

struct FlyerCameraFilter {
	obs_source_t        *context;
	Detector			yolo;

	FlyerCameraFilter(obs_source_t* context)
		: context(context),
		  yolo(obs_module_file("yolo9000.cfg"),
			   obs_module_file("yolo9000.weights"))
	{}
};

const char *flyer_camera_filter_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("Flyer Camera Filter");
}

void flyer_camera_filter_update(void *data, obs_data_t *settings)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);    
}

void flyer_camera_filter_destroy(void *data)
{
	FlyerCameraFilter *filter = static_cast<FlyerCameraFilter*>(data);
    delete filter;
}

void *flyer_camera_filter_create(obs_data_t *settings, obs_source_t *context)
{
	return static_cast<void*>(new FlyerCameraFilter(context));
}

struct obs_source_frame* flyer_camera_filter_video(void *data, struct obs_source_frame *frame)
{
	FlyerCameraFilter *filter = static_cast<FlyerCameraFilter*>(data);
	printf("Frame!\n");
    return frame;
}

obs_properties_t *flyer_camera_filter_properties(void *data)
{
	FlyerCameraFilter *filter = static_cast<FlyerCameraFilter*>(data);
    obs_properties_t *props = obs_properties_create();
    return props;
}

void flyer_camera_filter_defaults(obs_data_t *settings)
{
}
