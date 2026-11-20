# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Run by lib_nova_engine.cmake at build time: cargo builds nova_embed_ffi for the Windows target, for the Linux host, or for every macOS architecture, which lipo joins.
# Cargo runs outside the checkout, so the toolchain is the one the caller chose and not nova-safe's own pin.

set(required NOVA_ROOT NOVA_OUT NOVA_CARGO)
if (DEFINED NOVA_ARCHS)
    list(APPEND required NOVA_DEPLOYMENT)
endif()
foreach (name IN LISTS required)
    if (NOT DEFINED ${name})
        message(FATAL_ERROR "build_nova_engine.cmake needs -D${name}.")
    endif()
endforeach()

file(MAKE_DIRECTORY "${NOVA_OUT}")

function(run)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${NOVA_OUT}" RESULT_VARIABLE result)
    if (NOT result EQUAL 0)
        list(JOIN ARGN " " text)
        message(FATAL_ERROR "The Nova engine build failed (${result}): ${text}")
    endif()
endfunction()

# Replaces `to` only when its content changes, so nothing relinks after a build that changed nothing.
function(publish from to)
    run("${CMAKE_COMMAND}" -E copy_if_different "${from}" "${to}")
endfunction()

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${NOVA_ROOT}"
    OUTPUT_VARIABLE commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if (commit STREQUAL "")
    set(commit unknown)
else()
    execute_process(
        COMMAND git status --porcelain
        WORKING_DIRECTORY "${NOVA_ROOT}"
        OUTPUT_VARIABLE changes
        ERROR_QUIET)
    if (NOT changes STREQUAL "")
        string(APPEND commit "-dirty")
    endif()
endif()

set(environment "NOVA_EMBED_COMMIT=${commit}")
if (DEFINED NOVA_TARGET)
    list(APPEND environment "RUSTC_BOOTSTRAP=1")
elseif (DEFINED NOVA_ARCHS)
    list(APPEND environment "MACOSX_DEPLOYMENT_TARGET=${NOVA_DEPLOYMENT}")
endif()
if (DEFINED NOVA_RUSTUP_HOME AND DEFINED NOVA_CARGO_HOME)
    list(APPEND environment "RUSTUP_HOME=${NOVA_RUSTUP_HOME}" "CARGO_HOME=${NOVA_CARGO_HOME}")
elseif (DEFINED NOVA_CARGO_HOME)
    # rustup's proxies find rustc in CARGO_HOME, here one only for crates.
    get_filename_component(cargo_bin "${NOVA_CARGO}" DIRECTORY)
    list(APPEND environment "CARGO_HOME=${NOVA_CARGO_HOME}" "PATH=${cargo_bin}:$ENV{PATH}")
endif()

function(build)
    run("${CMAKE_COMMAND}" -E env ${environment}
        # As a static library alone: with the crate's rlib type beside it cargo leaves out the link-time optimisation its profile asks for.
        "${NOVA_CARGO}" rustc --release --locked
        --package nova_embed_ffi --lib --crate-type staticlib
        ${ARGN}
        # Stripped as nova-safe's profile asks, a proc-macro linked for macOS 12 or later is one rustc cannot load.
        --config profile.release.build-override.strip=false
        --manifest-path "${NOVA_ROOT}/Cargo.toml"
        --target-dir "${NOVA_OUT}/target")
endfunction()

if (DEFINED NOVA_TARGET)
    if (NOVA_TARGET MATCHES "^i686-")
        # Measured: i686 with the profile's fat LTO and overflow checks corrupts the heap while parsing a class.
        set(overflow_checks)
    else()
        set(overflow_checks --config profile.release.overflow-checks=true)
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
        set(target_rustflags --config "target.${NOVA_TARGET}.rustflags=['${quoted_rustflags}']")
    endif()
    # The prepared toolchain has the host's prebuilt std alone, so std is built from its rust-src, as tdesktop_rust's is.
    build(--target ${NOVA_TARGET}
        -Z build-std=std,panic_abort
        ${target_rustflags}
        ${overflow_checks})
    publish("${NOVA_OUT}/target/${NOVA_TARGET}/release/nova_embed_ffi.lib" "${NOVA_OUT}/nova_embed_ffi.lib")
    return()
elseif (NOT DEFINED NOVA_ARCHS)
    build()
    publish("${NOVA_OUT}/target/release/libnova_embed_ffi.a" "${NOVA_OUT}/libnova_embed_ffi.a")
    return()
endif()

string(REPLACE "," ";" archs "${NOVA_ARCHS}")
set(built)
foreach (arch IN LISTS archs)
    if (arch STREQUAL "arm64")
        set(target aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        set(target x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "The Nova engine build does not know the architecture '${arch}'.")
    endif()
    build(--target ${target})
    set(library "${NOVA_OUT}/target/${target}/release/libnova_embed_ffi.a")
    list(APPEND built "${library}")
endforeach()

function(join output)
    set(newer FALSE)
    foreach (input IN LISTS ARGN)
        if ("${input}" IS_NEWER_THAN "${output}")
            set(newer TRUE)
        endif()
    endforeach()
    if (newer)
        run(lipo -create ${ARGN} -output "${output}.new")
        publish("${output}.new" "${output}")
        file(REMOVE "${output}.new")
    endif()
endfunction()

join("${NOVA_OUT}/libnova_embed_ffi.a" ${built})
