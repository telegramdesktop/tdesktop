add_library(external_gsl INTERFACE IMPORTED GLOBAL)

target_include_directories(external_gsl SYSTEM
INTERFACE
    ${third_party_loc}/GSL/include
)
