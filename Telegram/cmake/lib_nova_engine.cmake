# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Nova (ThirdParty/nova-safe), the JavaScript engine widgets run in, built from the submodule by cargo.
# It belongs to TelegramWidgetHelper alone and is not a part of tdesktop_rust: Telegram runs no widget script.

set(nova_loc ${third_party_loc}/nova-safe)
if (NOT EXISTS ${nova_loc}/nova_embed_ffi/Cargo.toml)
    message(FATAL_ERROR "nova-safe sources are missing, run 'git submodule update --init --recursive'.")
endif()

if (NOT APPLE)
    message(FATAL_ERROR "The Nova engine is built for macOS only so far: Windows and Linux get their recipes with their helpers.")
endif()

# The toolchain prepare.py installs next to Libraries, else whatever cargo the system has.
set(nova_rust_loc ${libs_loc}/../ThirdParty/rust)
find_program(TDESKTOP_CARGO cargo PATHS ${nova_rust_loc}/cargo/bin NO_DEFAULT_PATH)
find_program(TDESKTOP_CARGO cargo)
if (NOT TDESKTOP_CARGO)
    message(FATAL_ERROR "cargo was not found: the Nova engine needs the Rust toolchain of Telegram/build/prepare/prepare.py, or set TDESKTOP_CARGO.")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(nova_out ${CMAKE_BINARY_DIR}/nova_engine)
set(nova_library ${nova_out}/libnova_embed_ffi.a)
set(nova_private_library ${nova_out}/libnova_embed_ffi_private.a)
string(REPLACE ";" "," nova_archs "${CMAKE_OSX_ARCHITECTURES}")

set(nova_toolchain_arguments)
get_filename_component(nova_cargo_bin ${TDESKTOP_CARGO} REALPATH)
get_filename_component(nova_cargo_bin ${nova_cargo_bin} DIRECTORY)
get_filename_component(nova_prepared_bin ${nova_rust_loc}/cargo/bin REALPATH)
if (nova_cargo_bin STREQUAL nova_prepared_bin)
    get_filename_component(nova_rustup_home ${nova_rust_loc}/rustup REALPATH)
    get_filename_component(nova_cargo_home ${nova_rust_loc}/cargo REALPATH)
    set(nova_toolchain_arguments -DNOVA_RUSTUP_HOME=${nova_rustup_home} -DNOVA_CARGO_HOME=${nova_cargo_home})
endif()

# Always runs: cargo decides what is stale, and the libraries are replaced only when their bytes change.
add_custom_target(nova_engine_build
    COMMAND
        ${CMAKE_COMMAND}
        -DNOVA_ROOT=${nova_loc}
        -DNOVA_OUT=${nova_out}
        -DNOVA_ARCHS=${nova_archs}
        -DNOVA_CARGO=${TDESKTOP_CARGO}
        -DNOVA_DEPLOYMENT=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -DNOVA_PYTHON=${Python3_EXECUTABLE}
        -DNOVA_PRIVATE=ON
        ${nova_toolchain_arguments}
        -P ${CMAKE_CURRENT_LIST_DIR}/build_nova_engine.cmake
    BYPRODUCTS
        ${nova_library}
        ${nova_private_library}
    COMMENT "Building the Nova engine"
    VERBATIM
)
set_target_properties(nova_engine_build PROPERTIES FOLDER "(external)")

add_library(lib_nova_engine_static STATIC IMPORTED GLOBAL)
set_target_properties(lib_nova_engine_static PROPERTIES IMPORTED_LOCATION ${nova_library})
add_library(lib_nova_engine INTERFACE)
add_library(tdesktop::lib_nova_engine ALIAS lib_nova_engine)
target_link_libraries(lib_nova_engine INTERFACE lib_nova_engine_static)
add_dependencies(lib_nova_engine nova_engine_build)

# Telegram still compiles the in-process engines and the validator (its Debug switch, and the engine-only arrangement),
# so it links the engine itself, in a form whose Rust runtime stays apart from tdesktop_rust's.
add_library(lib_nova_engine_private_static STATIC IMPORTED GLOBAL)
set_target_properties(lib_nova_engine_private_static PROPERTIES IMPORTED_LOCATION ${nova_private_library})
add_library(lib_nova_engine_private INTERFACE)
add_library(tdesktop::lib_nova_engine_private ALIAS lib_nova_engine_private)
target_link_libraries(lib_nova_engine_private INTERFACE lib_nova_engine_private_static)
add_dependencies(lib_nova_engine_private nova_engine_build)
