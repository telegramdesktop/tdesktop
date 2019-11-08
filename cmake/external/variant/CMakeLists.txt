add_library(external_variant INTERFACE IMPORTED GLOBAL)

target_include_directories(external_variant SYSTEM
INTERFACE
    ${third_party_loc}/variant/include
)
