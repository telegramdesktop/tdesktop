add_library(external_openal INTERFACE IMPORTED GLOBAL)

target_include_directories(external_openal SYSTEM
INTERFACE
    ${libs_loc}/openal-soft/include
)

target_link_directories(external_openal
INTERFACE
    ${libs_loc}/openal-soft/build/$<IF:$<CONFIG:Debug>,Debug,Release>
)

target_link_libraries(external_openal
INTERFACE
    OpenAL32
)
