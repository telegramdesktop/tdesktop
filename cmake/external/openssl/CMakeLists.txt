add_library(external_openssl INTERFACE IMPORTED GLOBAL)

target_include_directories(external_openssl SYSTEM
INTERFACE
    ${libs_loc}/openssl_1_1_1/include
)

target_link_directories(external_openssl
INTERFACE
    ${libs_loc}/openssl_1_1_1/out32$<$<CONFIG:Debug>:.dbg>
)

target_link_libraries(external_openssl
INTERFACE
    libcrypto
    libssl
)
