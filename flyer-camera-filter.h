const char *flyer_camera_filter_name(void *unused);
void flyer_camera_filter_update(void *data, obs_data_t *settings);
void flyer_camera_filter_destroy(void *data);
void *flyer_camera_filter_create(obs_data_t *settings, obs_source_t *context);
struct obs_source_frame* flyer_camera_filter_video(void *data, struct obs_source_frame *frame);
obs_properties_t *flyer_camera_filter_properties(void *data);
void flyer_camera_filter_defaults(obs_data_t *settings);
