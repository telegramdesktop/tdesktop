add_library(external_ranges INTERFACE IMPORTED GLOBAL)

target_include_directories(external_ranges SYSTEM
INTERFACE
    ${libs_loc}/range-v3/include
)

if (WIN32)
    target_compile_options(external_ranges
    INTERFACE
        /experimental:preprocessor # need for range-v3 see https://github.com/ericniebler/range-v3#supported-compilers
        /wd5105 # needed for `/experimental:preprocessor`, suppressing C5105 "macro expansion producing 'defined' has undefined behavior"
    )
endif()