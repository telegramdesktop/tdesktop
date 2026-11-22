# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# The shared widget sandbox from ThirdParty/widget-sandbox (packages/desktop),
# Qt-free C++ in two libraries. lib_widget_sandbox_host is the widget host
# Telegram links, the client of the engines TelegramWidgetHelper runs in Rust
# (cmake/lib_widget_helper_rust.cmake) over the shared host protocol.
# lib_widget_sandbox_platform is what that Rust serving asks the OS through the
# helper (engine_platform.h). Only the library sources are compiled here; the
# package's own CMakeLists always builds its harness and tests as well.
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

add_library(lib_widget_sandbox_host STATIC)
if (APPLE)
    init_standalone_target(lib_widget_sandbox_host "(external)")
else()
    init_target(lib_widget_sandbox_host "(external)")
endif()

add_library(tdesktop::lib_widget_sandbox_host ALIAS lib_widget_sandbox_host)

nice_target_sources(lib_widget_sandbox_host ${widget_sandbox_loc}
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

target_include_directories(lib_widget_sandbox_host
PUBLIC
    ${widget_sandbox_loc}/include
)

if (WIN32)
    nice_target_sources(lib_widget_sandbox_host ${widget_sandbox_loc}
    PRIVATE
        include/twidget/icu_intl.h
        src/icu_intl.cpp
    )
    target_link_libraries(lib_widget_sandbox_host PUBLIC bcrypt icu)
endif()

add_library(lib_widget_sandbox_platform STATIC)
if (APPLE)
    init_standalone_target(lib_widget_sandbox_platform "(external)")
else()
    init_target(lib_widget_sandbox_platform "(external)")
endif()

add_library(tdesktop::lib_widget_sandbox_platform ALIAS lib_widget_sandbox_platform)

nice_target_sources(lib_widget_sandbox_platform ${widget_sandbox_loc}
PRIVATE
    include/twidget/engine_platform.h
    src/engine_platform.cpp
    src/steady_clock.h
)

target_include_directories(lib_widget_sandbox_platform
PUBLIC
    ${widget_sandbox_loc}/include
)

if (WIN32)
    target_link_libraries(lib_widget_sandbox_platform PUBLIC bcrypt)
endif()

set(widget_sandbox_targets
    lib_widget_sandbox_host
    lib_widget_sandbox_platform)

foreach (target ${widget_sandbox_targets})
    if (MSVC)
        target_link_libraries(${target} PRIVATE lib_widget_sandbox_warnings_off)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()
