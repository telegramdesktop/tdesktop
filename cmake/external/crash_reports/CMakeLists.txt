add_library(external_crash_reports INTERFACE IMPORTED GLOBAL)

add_subdirectory(breakpad)

if (WIN32)
    target_link_libraries(external_crash_reports
    INTERFACE
        external_breakpad
    )
else()
    target_link_libraries(external_crash_reports
    INTERFACE
        external_crashpad
    )
endif()
