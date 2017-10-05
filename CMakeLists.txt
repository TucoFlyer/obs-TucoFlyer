project(obs-TucoFlyer)

set(obs-TucoFlyer_SOURCES
	obs-tucoflyer.c
	flyer-camera-filter.c)

add_library(obs-TucoFlyer MODULE
	${obs-TucoFlyer_SOURCES})
target_link_libraries(obs-TucoFlyer
	libobs)

install_obs_plugin_with_data(obs-TucoFlyer data)
