add_library(external_zlib INTERFACE IMPORTED GLOBAL)

target_include_directories(external_zlib SYSTEM
INTERFACE
    ${libs_loc}/zlib
    ${libs_loc}/zlib/contrib/minizip
)

if (WIN32)
    target_compile_definitions(external_zlib
    INTERFACE
        ZLIB_WINAPI
    )
endif()

target_link_directories(external_zlib
INTERFACE
    ${libs_loc}/zlib/contrib/vstudio/vc14/x86/ZlibStat$<IF:$<CONFIG:Debug>,Debug,ReleaseWithoutAsm>
)

target_link_libraries(external_zlib
INTERFACE
    zlibstat
)
