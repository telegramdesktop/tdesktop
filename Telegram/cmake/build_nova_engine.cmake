# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Run by lib_nova_engine.cmake at build time: cargo builds nova_embed_ffi for every architecture, lipo joins them.
# Cargo runs outside the checkout, so the toolchain is the one the caller chose and not nova-safe's own pin.

foreach (name NOVA_ROOT NOVA_OUT NOVA_ARCHS NOVA_CARGO NOVA_DEPLOYMENT)
    if (NOT DEFINED ${name})
        message(FATAL_ERROR "build_nova_engine.cmake needs -D${name}.")
    endif()
endforeach()

string(REPLACE "," ";" archs "${NOVA_ARCHS}")
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

set(environment "NOVA_EMBED_COMMIT=${commit}" "MACOSX_DEPLOYMENT_TARGET=${NOVA_DEPLOYMENT}")
if (DEFINED NOVA_RUSTUP_HOME AND DEFINED NOVA_CARGO_HOME)
    list(APPEND environment "RUSTUP_HOME=${NOVA_RUSTUP_HOME}" "CARGO_HOME=${NOVA_CARGO_HOME}")
endif()

set(built)
set(prelinked)
foreach (arch IN LISTS archs)
    if (arch STREQUAL "arm64")
        set(target aarch64-apple-darwin)
    elseif (arch STREQUAL "x86_64")
        set(target x86_64-apple-darwin)
    else()
        message(FATAL_ERROR "The Nova engine build does not know the architecture '${arch}'.")
    endif()
    run("${CMAKE_COMMAND}" -E env ${environment}
        # As a static library alone: with the crate's rlib type beside it cargo leaves out the link-time optimisation its profile asks for.
        "${NOVA_CARGO}" rustc --release --locked
        --package nova_embed_ffi --lib --crate-type staticlib
        --target ${target}
        # Stripped as nova-safe's profile asks, a proc-macro linked for macOS 12 or later is one rustc cannot load.
        --config profile.release.build-override.strip=false
        --manifest-path "${NOVA_ROOT}/Cargo.toml"
        --target-dir "${NOVA_OUT}/target")
    set(library "${NOVA_OUT}/target/${target}/release/libnova_embed_ffi.a")
    list(APPEND built "${library}")

    if (NOVA_PRIVATE)
        # nova-safe's own recipe (safe/embed/build-xcframework.sh): one object that exports only nova_*, its Rust runtime private.
        set(object "${NOVA_OUT}/nova_embed_ffi-${arch}.o")
        set(archive "${NOVA_OUT}/libnova_embed_ffi_private-${arch}.a")
        if ("${library}" IS_NEWER_THAN "${archive}")
            set(minimum "${NOVA_DEPLOYMENT}")
            if (arch STREQUAL "arm64" AND minimum VERSION_LESS 11.0)
                set(minimum 11.0)
            endif()
            file(WRITE "${NOVA_OUT}/exported-symbols.txt" "_nova_*\n_rust_eh_personality\n")
            run(ld -r -arch ${arch} -platform_version macos ${minimum} ${minimum}
                -exported_symbols_list "${NOVA_OUT}/exported-symbols.txt"
                -all_load "${library}"
                -o "${object}")
            run(strip -S "${object}")
            # Weak, so that it merges with the one tdesktop_rust brings instead of being a fourth personality routine.
            run("${NOVA_PYTHON}" -I "${NOVA_ROOT}/safe/embed/weak_personality.py" "${object}")
            file(REMOVE "${archive}")
            run(libtool -static -o "${archive}" "${object}")
        endif()
        list(APPEND prelinked "${archive}")
    endif()
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
if (NOVA_PRIVATE)
    join("${NOVA_OUT}/libnova_embed_ffi_private.a" ${prelinked})
endif()
