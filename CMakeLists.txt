project(obs-TucoFlyer)

set(obs-TucoFlyer_SOURCES
	obs-tucoflyer.cpp
	json-util.h
	flyer-camera-filter.cpp
	flyer-camera-filter.h
	flyer-vision-tracker.cpp
	flyer-vision-tracker.h
	flyer-vision-detector.cpp
	flyer-vision-detector.h
	image-grabber.cpp
	image-grabber.h
	overlay-drawing.cpp
	overlay-drawing.h
	bot-connector.cpp
	bot-connector.h)

add_library(obs-TucoFlyer MODULE
	${obs-TucoFlyer_SOURCES})

add_subdirectory(cryptopp)

find_package(Libcurl REQUIRED)
include_directories(${LIBCURL_INCLUDE_DIRS})

# Built-in FFT gives slow tracker performance, use Intel's Math Kernel Library
set(DLIB_USE_BLAS ON)
set(DLIB_USE_LAPACK ON)
set(DLIB_USE_MKL_FFT ON)

# For debugging the tracker code, writing PNG frames
set(DLIB_PNG_SUPPORT ON)

add_subdirectory(dlib)
# Local definition too, for the header files to see
add_definitions (-DDLIB_PNG_SUPPORT)

include_directories(${PROJECT_SOURCE_DIR}/rapidjson/include)

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
	cryptopp
	dlib
	${LIBCURL_LIBRARIES}
	${PROJECT_SOURCE_DIR}/yolo/yolo_cpp_dll.lib)

install_obs_plugin_with_data(obs-TucoFlyer data)
