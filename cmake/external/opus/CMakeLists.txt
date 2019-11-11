add_library(external_opus INTERFACE IMPORTED GLOBAL)

target_include_directories(external_opus SYSTEM
INTERFACE
    ${libs_loc}/opus/include
)

target_link_directories(external_opus
INTERFACE
    ${libs_loc}/opus/win32/VS2015/Win32/$<IF:$<CONFIG:Debug>,Debug,Release>
)

target_link_libraries(external_opus
INTERFACE
    opus
)
