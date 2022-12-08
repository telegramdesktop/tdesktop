# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_ffmpeg OBJECT)
add_library(desktop-app::lib_ffmpeg ALIAS lib_ffmpeg)
init_target(lib_ffmpeg ltcg)

nice_target_sources(lib_ffmpeg ${src_loc}
PRIVATE
    ffmpeg/ffmpeg_frame_generator.cpp
    ffmpeg/ffmpeg_frame_generator.h
    ffmpeg/ffmpeg_utility.cpp
    ffmpeg/ffmpeg_utility.h
)

target_include_directories(lib_ffmpeg
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_ffmpeg
PUBLIC
    desktop-app::lib_base
    desktop-app::lib_ui
    desktop-app::external_ffmpeg
)

if (DESKTOP_APP_SPECIAL_TARGET)
    target_compile_definitions(lib_ffmpeg PRIVATE LIB_FFMPEG_USE_QT_PRIVATE_API)
endif()
