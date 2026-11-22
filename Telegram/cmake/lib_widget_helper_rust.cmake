# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# The widget helper's serving in Rust (ThirdParty/twidget, rust/crates/helper_ffi) with Nova (ThirdParty/nova-safe)
# inside, built by cargo: TelegramWidgetHelper's one Rust library. It is not a part of tdesktop_rust: Telegram runs no
# widget script.

set(widget_helper_rust_loc ${third_party_loc}/twidget/rust)
if (NOT EXISTS ${widget_helper_rust_loc}/crates/helper_ffi/Cargo.toml)
    message(FATAL_ERROR "twidget sources are missing, run 'git submodule update --init --recursive'.")
endif()
if (NOT EXISTS ${third_party_loc}/nova-safe/nova_meter/Cargo.toml)
    message(FATAL_ERROR "nova-safe sources are missing, run 'git submodule update --init --recursive'.")
endif()

# The toolchain prepare.py installs next to Libraries, else whatever cargo the system has.
if (WIN32)
    set(widget_helper_rust_toolchain_loc ${CMAKE_SOURCE_DIR}/../ThirdParty/rust)
else()
    set(widget_helper_rust_toolchain_loc ${libs_loc}/../ThirdParty/rust)
endif()
find_program(TDESKTOP_CARGO cargo PATHS ${widget_helper_rust_toolchain_loc}/cargo/bin NO_DEFAULT_PATH)
find_program(TDESKTOP_CARGO cargo)
if (NOT TDESKTOP_CARGO)
    message(FATAL_ERROR "cargo was not found: TelegramWidgetHelper needs the Rust toolchain of Telegram/build/prepare/prepare.py, or set TDESKTOP_CARGO.")
endif()

set(widget_helper_rust_out ${CMAKE_BINARY_DIR}/widget_helper_rust)
if (WIN32)
    # The win7 targets' std imports what the helper's delay loads and lockdown already answer.
    if (build_winarm)
        set(widget_helper_rust_target aarch64-pc-windows-msvc)
    elseif (build_win64)
        set(widget_helper_rust_target x86_64-win7-windows-msvc)
    else()
        set(widget_helper_rust_target i686-win7-windows-msvc)
    endif()
    set(widget_helper_rust_library ${widget_helper_rust_out}/twidget_helper_ffi.lib)
    set(widget_helper_rust_platform -DHELPER_TARGET=${widget_helper_rust_target})
elseif (APPLE)
    set(widget_helper_rust_library ${widget_helper_rust_out}/libtwidget_helper_ffi.a)
    string(REPLACE ";" "," widget_helper_rust_archs "${CMAKE_OSX_ARCHITECTURES}")
    set(widget_helper_rust_platform
        -DHELPER_ARCHS=${widget_helper_rust_archs}
        -DHELPER_DEPLOYMENT=${CMAKE_OSX_DEPLOYMENT_TARGET})
else()
    set(widget_helper_rust_library ${widget_helper_rust_out}/libtwidget_helper_ffi.a)
    set(widget_helper_rust_platform)
endif()

set(widget_helper_rust_toolchain)
get_filename_component(widget_helper_cargo_bin ${TDESKTOP_CARGO} REALPATH)
get_filename_component(widget_helper_cargo_bin ${widget_helper_cargo_bin} DIRECTORY)
get_filename_component(widget_helper_prepared_bin ${widget_helper_rust_toolchain_loc}/cargo/bin REALPATH)
if (widget_helper_cargo_bin STREQUAL widget_helper_prepared_bin)
    get_filename_component(widget_helper_rustup_home ${widget_helper_rust_toolchain_loc}/rustup REALPATH)
    get_filename_component(widget_helper_cargo_home ${widget_helper_rust_toolchain_loc}/cargo REALPATH)
    set(widget_helper_rust_toolchain
        -DHELPER_RUSTUP_HOME=${widget_helper_rustup_home}
        -DHELPER_CARGO_HOME=${widget_helper_cargo_home})
elseif (NOT APPLE AND NOT WIN32)
    # The Docker image's toolchain is read-only for the user who builds, so crates are fetched into the build tree.
    set(widget_helper_rust_toolchain -DHELPER_CARGO_HOME=${widget_helper_rust_out}/cargo)
endif()

# Always runs: cargo decides what is stale, and the library is replaced only when its bytes change.
add_custom_target(widget_helper_rust_build
    COMMAND
        ${CMAKE_COMMAND}
        -DHELPER_ROOT=${widget_helper_rust_loc}
        -DHELPER_OUT=${widget_helper_rust_out}
        -DHELPER_CARGO=${TDESKTOP_CARGO}
        ${widget_helper_rust_platform}
        ${widget_helper_rust_toolchain}
        -P ${CMAKE_CURRENT_LIST_DIR}/build_widget_helper_rust.cmake
    BYPRODUCTS
        ${widget_helper_rust_library}
    COMMENT "Building the widget helper's Rust serving"
    VERBATIM
)
set_target_properties(widget_helper_rust_build PROPERTIES FOLDER "(external)")

add_library(lib_widget_helper_rust_static STATIC IMPORTED GLOBAL)
set_target_properties(lib_widget_helper_rust_static PROPERTIES IMPORTED_LOCATION ${widget_helper_rust_library})
add_library(lib_widget_helper_rust INTERFACE)
add_library(tdesktop::lib_widget_helper_rust ALIAS lib_widget_helper_rust)
target_link_libraries(lib_widget_helper_rust INTERFACE lib_widget_helper_rust_static)
target_include_directories(lib_widget_helper_rust INTERFACE ${widget_helper_rust_loc}/crates/helper_ffi/include)
add_dependencies(lib_widget_helper_rust widget_helper_rust_build)

if (WIN32)
    # What cargo rustc -- --print native-static-libs lists, its advisory /defaultlib:libcmt aside: the helper's CRT serves the archive.
    target_link_libraries(lib_widget_helper_rust
    INTERFACE
        kernel32
        ntdll
        userenv
        ws2_32
        dbghelp
    )
    if (widget_helper_rust_target MATCHES "-win7-")
        target_link_libraries(lib_widget_helper_rust INTERFACE advapi32)
    endif()
elseif (NOT APPLE)
    target_link_libraries(lib_widget_helper_rust INTERFACE ${CMAKE_DL_LIBS} m)
endif()
