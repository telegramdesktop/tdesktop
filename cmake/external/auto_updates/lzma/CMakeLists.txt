add_library(external_lzma INTERFACE IMPORTED GLOBAL)

target_include_directories(external_lzma SYSTEM
INTERFACE
    ${libs_loc}/lzma/C
)

target_link_directories(external_lzma
INTERFACE
    ${libs_loc}/lzma/C/Util/LzmaLib/$<IF:$<CONFIG:Debug>,Debug,Release>
)

target_link_libraries(external_lzma
INTERFACE
    LzmaLib
)
