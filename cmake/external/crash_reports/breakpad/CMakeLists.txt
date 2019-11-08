add_library(external_breakpad INTERFACE IMPORTED GLOBAL)

target_include_directories(external_breakpad SYSTEM
INTERFACE
    ${libs_loc}/breakpad/src
)

target_link_libraries(external_breakpad
INTERFACE
    windows/common
    windows/handler/exception_handler
    windows/crash_generation/crash_generation_client
)

target_link_directories(external_breakpad
INTERFACE
    ${libs_loc}/breakpad/src/out/$<IF:$<CONFIG:Debug>,Debug,Release>/obj/client
)
