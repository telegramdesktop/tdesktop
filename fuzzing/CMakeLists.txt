# Telegram Desktop Protocol Fuzzers
#
# Standalone fuzzer build system for OSS-Fuzz integration.
# These fuzzers don't require the full tdesktop build.
#
# Usage:
#   cmake -B build .
#   cmake --build build -j$(nproc)
#
# Requirements:
#   - Clang compiler with libFuzzer support
#   - AddressSanitizer, UndefinedBehaviorSanitizer

cmake_minimum_required(VERSION 3.16)
project(telegram_fuzzers CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Verify Clang compiler (required for libFuzzer)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "Fuzzing requires Clang compiler for libFuzzer support")
    message(WARNING "Set CMAKE_CXX_COMPILER=clang++ to use fuzzers")
    message(WARNING "Skipping fuzzer build")
    return()
endif()

# Fuzzing flags for standalone build
set(FUZZER_FLAGS
    -fsanitize=fuzzer,address,undefined
    -g
    -O1
)

# List of all fuzzers
set(FUZZERS
    # MTProto protocol layers
    mtproto_v0_fuzzer
    mtproto_v1_obfuscated_fuzzer
    mtproto_vd_padded_fuzzer
    tl_serialization_fuzzer
    aes_ctr_obfuscation_fuzzer
    # Private message encryption
    aes_ige_encryption_fuzzer
    message_key_derivation_fuzzer
    auth_key_management_fuzzer
)

# Build each fuzzer as standalone binary
foreach(FUZZER ${FUZZERS})
    add_executable(${FUZZER} ${FUZZER}.cpp)
    target_compile_options(${FUZZER} PRIVATE ${FUZZER_FLAGS})
    target_link_options(${FUZZER} PRIVATE ${FUZZER_FLAGS})
    set_target_properties(${FUZZER}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fuzzers"
    )
endforeach()

message(STATUS "Telegram Protocol Fuzzers configured")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Fuzzers: ${FUZZERS}")
message(STATUS "  Output: ${CMAKE_BINARY_DIR}/fuzzers/")

# Note: These fuzzers are standalone and don't link against tdesktop
# For OSS-Fuzz integration, see oss-fuzz/build.sh
