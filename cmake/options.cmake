add_library(common_options INTERFACE)

target_compile_features(common_options
INTERFACE
    cxx_std_17
)

target_compile_definitions(common_options
INTERFACE
    UNICODE
)

if (WIN32)
    include(cmake/options_win.cmake)
else()
endif()
