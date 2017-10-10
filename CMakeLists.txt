project(obs-TucoFlyer)

set(obs-TucoFlyer_SOURCES
	obs-tucoflyer.cpp
	flyer-camera-filter.cpp
	flyer-camera-filter.h
	dlib/dlib/all/source.cpp)

add_library(obs-TucoFlyer MODULE
	${obs-TucoFlyer_SOURCES})

include_directories(${PROJECT_SOURCE_DIR}/dlib)
include_directories(${PROJECT_SOURCE_DIR}/websocketpp)

target_link_libraries(obs-TucoFlyer
	libobs
	${PROJECT_SOURCE_DIR}/yolo/yolo_cpp_dll.lib)

install_obs_plugin_with_data(obs-TucoFlyer data)
