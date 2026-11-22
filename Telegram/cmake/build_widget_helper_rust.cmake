# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Run by lib_widget_helper_rust.cmake at build time: cargo builds twidget_helper_ffi for the Windows target, for each macOS architecture joined by lipo or for the Linux host, with link-time optimisation and overflow checks.

foreach (name HELPER_ROOT HELPER_OUT HELPER_CARGO)
    if (NOT DEFINED ${name})
        message(FATAL_ERROR "build_widget_helper_rust.cmake needs -D${name}.")
    endif()
endforeach()

file(MAKE_DIRECTORY "${HELPER_OUT}")

function(run)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${HELPER_OUT}" RESULT_VARIABLE result)
    if (NOT result EQUAL 0)
        list(JOIN ARGN " " text)
        message(FATAL_ERROR "The widget helper's Rust build failed (${result}): ${text}")
    endif()
endfunction()

# Replaces `to` only when its content changes, so nothing relinks after a build that changed nothing.
function(publish from to)
    run("${CMAKE_COMMAND}" -E copy_if_different "${from}" "${to}")
endfunction()

set(environment)
if (DEFINED HELPER_TARGET)
    list(APPEND environment "RUSTC_BOOTSTRAP=1")
elseif (DEFINED HELPER_ARCHS)
    list(APPEND environment "MACOSX_DEPLOYMENT_TARGET=${HELPER_DEPLOYMENT}")
endif()
if (DEFINED HELPER_RUSTUP_HOME AND DEFINED HELPER_CARGO_HOME)
    list(APPEND environment "RUSTUP_HOME=${HELPER_RUSTUP_HOME}" "CARGO_HOME=${HELPER_CARGO_HOME}")
elseif (DEFINED HELPER_CARGO_HOME)
    # rustup's proxies find rustc in CARGO_HOME, here one only for crates.
    get_filename_component(cargo_bin "${HELPER_CARGO}" DIRECTORY)
    list(APPEND environment "CARGO_HOME=${HELPER_CARGO_HOME}" "PATH=${cargo_bin}:$ENV{PATH}")
endif()

function(build)
    run("${CMAKE_COMMAND}" -E env ${environment}
        # As a static library alone: with the crate's rlib type beside it cargo leaves out link-time optimisation.
        "${HELPER_CARGO}" rustc --release --locked
        --package twidget_helper_ffi --lib --crate-type staticlib
        ${ARGN}
        --config profile.release.overflow-checks=true
        --config profile.release.lto=true
        --manifest-path "${HELPER_ROOT}/Cargo.toml"
        --target-dir "${HELPER_OUT}/target")
endfunction()

if (DEFINED HELPER_ARCHS)
    string(REPLACE "," ";" archs "${HELPER_ARCHS}")
    set(built)
    foreach (arch IN LISTS archs)
        if (arch STREQUAL "arm64")
            set(target aarch64-apple-darwin)
        elseif (arch STREQUAL "x86_64")
            set(target x86_64-apple-darwin)
        else()
            message(FATAL_ERROR "The widget helper's Rust build does not know the architecture '${arch}'.")
        endif()
        build(--target ${target} --config profile.release.build-override.strip=false)
        list(APPEND built "${HELPER_OUT}/target/${target}/release/libtwidget_helper_ffi.a")
    endforeach()
    run(lipo -create ${built} -output "${HELPER_OUT}/libtwidget_helper_ffi.a.new")
    publish("${HELPER_OUT}/libtwidget_helper_ffi.a.new" "${HELPER_OUT}/libtwidget_helper_ffi.a")
    file(REMOVE "${HELPER_OUT}/libtwidget_helper_ffi.a.new")
    return()
elseif (NOT DEFINED HELPER_TARGET)
    build()
    publish("${HELPER_OUT}/target/release/libtwidget_helper_ffi.a" "${HELPER_OUT}/libtwidget_helper_ffi.a")
    return()
endif()

set(rustflags -C target-feature=+crt-static -C control-flow-guard)
if (DEFINED ENV{CARGO_ENCODED_RUSTFLAGS})
    set(rustflags_variable CARGO_ENCODED_RUSTFLAGS)
    string(ASCII 31 rustflags_separator)
elseif (DEFINED ENV{RUSTFLAGS})
    set(rustflags_variable RUSTFLAGS)
    set(rustflags_separator " ")
endif()
if (DEFINED rustflags_variable)
    list(JOIN rustflags "${rustflags_separator}" appended_rustflags)
    set(inherited_rustflags "$ENV{${rustflags_variable}}")
    if (NOT inherited_rustflags STREQUAL "")
        set(appended_rustflags "${inherited_rustflags}${rustflags_separator}${appended_rustflags}")
    endif()
    set(ENV{${rustflags_variable}} "${appended_rustflags}")
    set(target_rustflags)
else()
    list(JOIN rustflags "','" quoted_rustflags)
    set(target_rustflags --config "target.${HELPER_TARGET}.rustflags=['${quoted_rustflags}']")
endif()

# The prepared toolchain has the host's prebuilt std alone, so std is built from its rust-src, as tdesktop_rust's is.
build(--target ${HELPER_TARGET}
    -Z build-std=std,panic_abort
    ${target_rustflags})
publish("${HELPER_OUT}/target/${HELPER_TARGET}/release/twidget_helper_ffi.lib" "${HELPER_OUT}/twidget_helper_ffi.lib")
