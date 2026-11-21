# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# The shared widget sandbox from ThirdParty/widget-sandbox (packages/desktop):
# the Qt-free C++ host that runs untrusted widget scripts in the Nova engine,
# checks every DOM operation with the shared validator and talks the shared
# host protocol to a renderer. Its JavaScript bundles live next to it in
# packages/desktop/WidgetJS. Only the library sources are compiled here; the
# package's own CMakeLists always builds its harness and tests as well.
# Nova (ThirdParty/nova-safe, engine crates without unsafe code) is linked by
# whoever runs engines or validators from this library (cmake/lib_nova_engine.cmake); nova_engine.h is its C interface.
# On Windows IcuIntl (src/icu_intl.cpp) is built too, linking icu; Telegram delay-loads icu.dll.
# There is no packaged branch: no distribution ships this library.

set(widget_sandbox_loc ${third_party_loc}/widget-sandbox/packages/desktop/sandbox)

if (NOT EXISTS ${widget_sandbox_loc}/src/sandbox_engine.cpp)
    message(FATAL_ERROR "widget-sandbox sources are missing, run 'git submodule update --init --recursive'.")
endif()

# Silence third-party warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX.
if (MSVC)
    add_library(lib_widget_sandbox_warnings_off INTERFACE)
    target_compile_options(lib_widget_sandbox_warnings_off
    INTERFACE
        /W0
        /WX-
    )
endif()

function(add_widget_sandbox_host target_name)
    add_library(${target_name} STATIC)
    if (APPLE)
        init_standalone_target(${target_name} "(external)")
    else()
        init_target(${target_name} "(external)")
    endif()

    add_library(tdesktop::${target_name} ALIAS ${target_name})

    nice_target_sources(${target_name} ${widget_sandbox_loc}
    PRIVATE
        include/twidget/gesture_policy.h
        include/twidget/intl.h
        include/twidget/js_thread.h
        include/twidget/json.h
        include/twidget/remote_stream.h
        include/twidget/renderer.h
        include/twidget/resources.h
        include/twidget/sandbox_engine.h
        include/twidget/sdk_policy.h
        include/twidget/version.h
        include/twidget/widget_host.h
        src/gesture_policy.cpp
        src/intl.cpp
        src/js_thread.cpp
        src/remote_coding.cpp
        src/remote_internal.h
        src/remote_stream.cpp
        src/remote_wire.h
        src/resources.cpp
        src/sandbox_engine_client.cpp
        src/sandbox_engine_shared.cpp
        src/sdk_policy.cpp
        src/steady_clock.h
        src/widget_host.cpp
        src/widget_host_internal.h
    )

    target_include_directories(${target_name}
    PUBLIC
        ${widget_sandbox_loc}/include
    )

    if (WIN32)
        nice_target_sources(${target_name} ${widget_sandbox_loc}
        PRIVATE
            include/twidget/icu_intl.h
            src/icu_intl.cpp
        )
        target_link_libraries(${target_name} PUBLIC bcrypt icu)
    endif()
endfunction()

add_widget_sandbox_host(lib_widget_sandbox_host)
add_widget_sandbox_host(lib_widget_sandbox_host_hardened)
set(widget_sandbox_targets lib_widget_sandbox_host lib_widget_sandbox_host_hardened lib_widget_sandbox)

add_library(lib_widget_sandbox STATIC)
if (APPLE)
    init_standalone_target(lib_widget_sandbox "(external)")
else()
    init_target(lib_widget_sandbox "(external)")
endif()

add_library(tdesktop::lib_widget_sandbox ALIAS lib_widget_sandbox)

nice_target_sources(lib_widget_sandbox ${widget_sandbox_loc}
PRIVATE
    include/twidget/bundle_stamp.h
    include/twidget/cpu_budget.h
    include/twidget/nova_context.h
    include/twidget/remote_engine.h
    include/twidget/remote_hosted.h
    include/twidget/trusted_validator.h
    src/bundle_stamp.cpp
    src/cpu_budget.cpp
    src/nova_context.cpp
    src/remote_engine.cpp
    src/remote_hosted.cpp
    src/remote_stream_engine.cpp
    src/sandbox_engine.cpp
    src/trusted_validator.cpp
    src/widget_host_local.cpp
)

target_link_libraries(lib_widget_sandbox
PUBLIC
    lib_widget_sandbox_host_hardened
)

# Not SYSTEM, so that an engine update rebuilds the object compiled against nova_engine.h.
target_include_directories(lib_widget_sandbox
PRIVATE
    ${nova_loc}/nova_embed_ffi/include
)

foreach (target ${widget_sandbox_targets})
    if (MSVC)
        target_link_libraries(${target} PRIVATE lib_widget_sandbox_warnings_off)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()
