add_library(external_openssl INTERFACE IMPORTED GLOBAL)

target_include_directories(external_openssl SYSTEM
INTERFACE
    ${libs_loc}/openssl_1_1_1/include
)
