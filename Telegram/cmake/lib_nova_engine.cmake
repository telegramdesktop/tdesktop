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

# The toolchain prepare.py installs next to Libraries, else whatever cargo the system has.
if (WIN32)
    set(nova_rust_loc ${CMAKE_SOURCE_DIR}/../ThirdParty/rust)
else()
    set(nova_rust_loc ${libs_loc}/../ThirdParty/rust)
endif()
find_program(TDESKTOP_CARGO cargo PATHS ${nova_rust_loc}/cargo/bin NO_DEFAULT_PATH)
find_program(TDESKTOP_CARGO cargo)
if (NOT TDESKTOP_CARGO)
    message(FATAL_ERROR "cargo was not found: the Nova engine needs the Rust toolchain of Telegram/build/prepare/prepare.py, or set TDESKTOP_CARGO.")
endif()

set(nova_out ${CMAKE_BINARY_DIR}/nova_engine)
if (WIN32)
    if (build_winarm)
        set(nova_target aarch64-pc-windows-msvc)
    elseif (build_win64)
        set(nova_target x86_64-win7-windows-msvc)
    else()
        set(nova_target i686-win7-windows-msvc)
    endif()
    set(nova_library ${nova_out}/nova_embed_ffi.lib)
    set(nova_libraries ${nova_library})
    set(nova_platform_arguments -DNOVA_TARGET=${nova_target})
else()
    set(nova_library ${nova_out}/libnova_embed_ffi.a)
    set(nova_libraries ${nova_library})
    set(nova_platform_arguments)
    if (APPLE)
        string(REPLACE ";" "," nova_archs "${CMAKE_OSX_ARCHITECTURES}")
        list(APPEND nova_platform_arguments
            -DNOVA_ARCHS=${nova_archs}
            -DNOVA_DEPLOYMENT=${CMAKE_OSX_DEPLOYMENT_TARGET}
        )
    endif()
endif()

set(nova_toolchain_arguments)
get_filename_component(nova_cargo_bin ${TDESKTOP_CARGO} REALPATH)
get_filename_component(nova_cargo_bin ${nova_cargo_bin} DIRECTORY)
get_filename_component(nova_prepared_bin ${nova_rust_loc}/cargo/bin REALPATH)
if (nova_cargo_bin STREQUAL nova_prepared_bin)
    get_filename_component(nova_rustup_home ${nova_rust_loc}/rustup REALPATH)
    get_filename_component(nova_cargo_home ${nova_rust_loc}/cargo REALPATH)
    set(nova_toolchain_arguments -DNOVA_RUSTUP_HOME=${nova_rustup_home} -DNOVA_CARGO_HOME=${nova_cargo_home})
elseif (NOT APPLE AND NOT WIN32)
    # The Docker image's toolchain is read-only for the user who builds, so crates are fetched into the build tree.
    set(nova_toolchain_arguments -DNOVA_CARGO_HOME=${nova_out}/cargo)
endif()

# Always runs: cargo decides what is stale, and the libraries are replaced only when their bytes change.
add_custom_target(nova_engine_build
    COMMAND
        ${CMAKE_COMMAND}
        -DNOVA_ROOT=${nova_loc}
        -DNOVA_OUT=${nova_out}
        -DNOVA_CARGO=${TDESKTOP_CARGO}
        ${nova_platform_arguments}
        ${nova_toolchain_arguments}
        -P ${CMAKE_CURRENT_LIST_DIR}/build_nova_engine.cmake
    BYPRODUCTS
        ${nova_libraries}
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

if (WIN32)
    # What cargo rustc -- --print native-static-libs lists, its advisory /defaultlib:libcmt aside: the helper's CRT serves the archive.
    target_link_libraries(lib_nova_engine
    INTERFACE
        kernel32
        ntdll
        userenv
        ws2_32
        dbghelp
    )
    if (nova_target MATCHES "-win7-")
        target_link_libraries(lib_nova_engine INTERFACE advapi32)
    endif()
elseif (NOT APPLE)
    target_link_libraries(lib_nova_engine INTERFACE ${CMAKE_DL_LIBS} m)
endif()
