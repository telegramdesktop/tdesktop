add_library(external_crash_reports INTERFACE IMPORTED GLOBAL)

if (WIN32)
    add_subdirectory(breakpad)
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
