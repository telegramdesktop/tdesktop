add_library(lib_ffmpeg OBJECT)
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

target_link_libraries(lib_ffmpeg
PUBLIC
    lib_base
)
