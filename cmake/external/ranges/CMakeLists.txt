add_library(external_ranges INTERFACE IMPORTED GLOBAL)

target_include_directories(external_ranges SYSTEM
INTERFACE
    ${libs_loc}/range-v3/include
)
