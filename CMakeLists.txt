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

target_include_directories(obs-TucoFlyer PUBLIC
	${OBS_JANSSON_INCLUDE_DIRS})

find_package(Libcurl REQUIRED)
include_directories(${LIBCURL_INCLUDE_DIRS})

include_directories(${PROJECT_SOURCE_DIR}/dlib)

include_directories(${PROJECT_SOURCE_DIR}/asio/asio/include)
add_definitions (-DASIO_STANDALONE)
add_definitions (-DASIO_HAS_STD_TYPE_TRAITS)

include_directories(${PROJECT_SOURCE_DIR}/websocketpp)
add_definitions (-D_WEBSOCKETPP_CPP11_FUNCTIONAL_)
add_definitions (-D_WEBSOCKETPP_CPP11_SYSTEM_ERROR_)
add_definitions (-D_WEBSOCKETPP_CPP11_RANDOM_DEVICE_)
add_definitions (-D_WEBSOCKETPP_CPP11_MEMORY_)
add_definitions (-D_WEBSOCKETPP_CPP11_TYPE_TRAITS_)

target_link_libraries(obs-TucoFlyer
	libobs
	${LIBCURL_LIBRARIES}
	${OBS_JANSSON_IMPORT}
	${PROJECT_SOURCE_DIR}/yolo/yolo_cpp_dll.lib)

install_obs_plugin_with_data(obs-TucoFlyer data)
