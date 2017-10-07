project(obs-TucoFlyer)

set(obs-TucoFlyer_SOURCES
	obs-tucoflyer.cpp
	flyer-camera-filter.cpp
	flyer-camera-filter.h)

add_library(obs-TucoFlyer MODULE
	${obs-TucoFlyer_SOURCES})
target_link_libraries(obs-TucoFlyer
	libobs
	${PROJECT_SOURCE_DIR}/yolo/yolo_cpp_dll.lib)

install_obs_plugin_with_data(obs-TucoFlyer data)
