add_library(lib_lz4 OBJECT)
init_target(lib_lz4)

set(lz4_loc ${third_party_loc}/lz4/lib)

target_sources(lib_lz4
PRIVATE
    ${lz4_loc}/lz4.c
    ${lz4_loc}/lz4.h
    ${lz4_loc}/lz4frame.c
    ${lz4_loc}/lz4frame.h
    ${lz4_loc}/lz4frame_static.h
    ${lz4_loc}/lz4hc.c
    ${lz4_loc}/lz4hc.h
    ${lz4_loc}/xxhash.c
    ${lz4_loc}/xxhash.h
)

target_include_directories(lib_lz4
PUBLIC
    ${lz4_loc}
)
