project(obs-TucoFlyer)

set(obs-TucoFlyer_SOURCES
	obs-tucoflyer.cpp
	flyer-camera-filter.cpp
	flyer-camera-filter.h
	bot-connector.cpp
	bot-connector.h
	dlib/dlib/all/source.cpp)

add_library(obs-TucoFlyer MODULE
	${obs-TucoFlyer_SOURCES})

include_directories(${PROJECT_SOURCE_DIR}/dlib)
include_directories(${PROJECT_SOURCE_DIR}/asio/include)
include_directories(${PROJECT_SOURCE_DIR}/websocketpp)

target_link_libraries(obs-TucoFlyer
	libobs
	${PROJECT_SOURCE_DIR}/yolo/yolo_cpp_dll.lib)

if (MSVC)
	add_definitions (-D_WEBSOCKETPP_CPP11_FUNCTIONAL_)
	add_definitions (-D_WEBSOCKETPP_CPP11_SYSTEM_ERROR_)
	add_definitions (-D_WEBSOCKETPP_CPP11_RANDOM_DEVICE_)
	add_definitions (-D_WEBSOCKETPP_CPP11_MEMORY_)
else()
	add_definitions (-D_WEBSOCKETPP_CPP11_STL_)
endif()

install_obs_plugin_with_data(obs-TucoFlyer data)
