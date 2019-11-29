# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_ffmpeg OBJECT)
add_library(desktop-app::lib_ffmpeg ALIAS lib_ffmpeg)
init_target(lib_ffmpeg)

set(lib_ffmpeg_sources
PRIVATE
    ffmpeg/ffmpeg_utility.cpp
    ffmpeg/ffmpeg_utility.h
)
nice_target_sources(lib_ffmpeg ${src_loc} "${lib_ffmpeg_sources}")

target_include_directories(lib_ffmpeg
PUBLIC
    ${src_loc}
    ${libs_loc}/ffmpeg
)

set(ffmpeg_lib_loc ${libs_loc}/ffmpeg)

target_link_libraries(lib_ffmpeg
PUBLIC
    desktop-app::lib_base
    ${ffmpeg_lib_loc}/libavformat/libavformat.a
    ${ffmpeg_lib_loc}/libavcodec/libavcodec.a
    ${ffmpeg_lib_loc}/libavutil/libavutil.a
    ${ffmpeg_lib_loc}/libswresample/libswresample.a
    ${ffmpeg_lib_loc}/libswscale/libswscale.a
)
