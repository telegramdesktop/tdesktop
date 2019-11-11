add_library(external_auto_updates INTERFACE IMPORTED GLOBAL)

if (WIN32)
    add_subdirectory(lzma)
    target_link_libraries(external_auto_updates
    INTERFACE
        external_lzma
    )
else()
    add_subdirectory(xz)
    target_link_libraries(external_auto_updates
    INTERFACE
        external_xz
    )
endif()
