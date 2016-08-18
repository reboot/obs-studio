project(text-pango)

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
        pkg_check_modules(PANGO pango)
        pkg_check_modules(CAIRO cairo)
        pkg_check_modules(PANGOCAIRO pangocairo)
endif()

include_directories(
	${PANGO_INCLUDE_DIRS}
	${CAIRO_INCLUDE_DIRS}
	${PANGOCAIRO_INCLUDE_DIRS})

set(text-pango_SOURCES
	text-pango.c
	text-pango.h
	utils.c
	utils.h)

add_library(text-pango MODULE
	${text-pango_SOURCES})

target_link_libraries(text-pango
	libobs
	${PANGO_LIBRARIES}
	${CAIRO_LIBRARIES}
	${PANGOCAIRO_LIBRARIES})

install_obs_plugin_with_data(text-pango data)
