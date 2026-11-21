# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# The widget helper's serving in Rust (ThirdParty/twidget, rust/crates/helper_ffi) with Nova inside, built by cargo:
# TelegramWidgetHelper's one Rust library when TDESKTOP_WIDGET_HELPER_RUST is ON.

if (NOT WIN32)
    message(FATAL_ERROR "TDESKTOP_WIDGET_HELPER_RUST is supported on Windows only so far.")
endif()

set(widget_helper_rust_loc ${third_party_loc}/twidget/rust)
if (NOT EXISTS ${widget_helper_rust_loc}/crates/helper_ffi/Cargo.toml)
    message(FATAL_ERROR "twidget sources are missing, run 'git submodule update --init --recursive'.")
endif()

set(widget_helper_rust_out ${CMAKE_BINARY_DIR}/widget_helper_rust)
set(widget_helper_rust_library ${widget_helper_rust_out}/twidget_helper_ffi.lib)

set(widget_helper_rust_toolchain)
if (DEFINED nova_rustup_home AND DEFINED nova_cargo_home)
    set(widget_helper_rust_toolchain
        -DHELPER_RUSTUP_HOME=${nova_rustup_home}
        -DHELPER_CARGO_HOME=${nova_cargo_home})
endif()

# Always runs: cargo decides what is stale, and the library is replaced only when its bytes change.
add_custom_target(widget_helper_rust_build
    COMMAND
        ${CMAKE_COMMAND}
        -DHELPER_ROOT=${widget_helper_rust_loc}
        -DHELPER_OUT=${widget_helper_rust_out}
        -DHELPER_CARGO=${TDESKTOP_CARGO}
        # Nova's target, whose std imports the helper's delay loads and lockdown already answer.
        -DHELPER_TARGET=${nova_target}
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

target_link_libraries(lib_widget_helper_rust
INTERFACE
    kernel32
    ntdll
    userenv
    ws2_32
    dbghelp
)
if (nova_target MATCHES "-win7-")
    target_link_libraries(lib_widget_helper_rust INTERFACE advapi32)
endif()
