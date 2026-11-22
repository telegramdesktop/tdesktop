# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# TwidgetCore's platform end from ThirdParty/twidget (core/src), the wire and
# remote messages, PNG and filter code: what Telegram's painter and hosted
# client link, neither litehtml nor the renderer, as twidget's own
# twidget_wire and twidget_remote targets. The renderer runs in Rust, in
# TelegramWidgetHelper (cmake/lib_widget_helper_rust.cmake).
# Consumers include its headers as "core/src/<name>.h".
# There is no packaged branch: no distribution ships this library.

set(widget_sandbox_root ${third_party_loc}/widget-sandbox)
set(widget_renderer_root ${third_party_loc}/twidget)
set(widget_renderer_loc ${widget_renderer_root}/core/src)

if (NOT EXISTS ${widget_renderer_loc}/platform_end.cpp)
    message(FATAL_ERROR "twidget sources are missing, run 'git submodule update --init --recursive'.")
endif()

# Silence third-party warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX.
if (MSVC)
    add_library(lib_widget_renderer_warnings_off INTERFACE)
    target_compile_options(lib_widget_renderer_warnings_off
    INTERFACE
        /W0
        /WX-
    )
endif()

add_library(lib_widget_renderer_platform STATIC)
if (APPLE)
    init_standalone_target(lib_widget_renderer_platform "(external)")
else()
    init_target(lib_widget_renderer_platform "(external)")
endif()

add_library(tdesktop::lib_widget_renderer_platform ALIAS lib_widget_renderer_platform)

nice_target_sources(lib_widget_renderer_platform ${widget_renderer_loc}
PRIVATE
    backend.h
    canvas_ops.h
    display_list.h
    filters.cpp
    filters.h
    platform_end.cpp
    platform_end.h
    png.cpp
    png.h
    remote.cpp
    remote.h
    wire.cpp
    wire.h
)

target_include_directories(lib_widget_renderer_platform
PUBLIC
    ${widget_renderer_root}
    ${widget_sandbox_root}/packages/desktop/sandbox/include
PRIVATE
    ${widget_renderer_loc}
)

target_link_libraries(lib_widget_renderer_platform
PUBLIC
    tdesktop::lib_widget_sandbox_host
)

if (MSVC)
    target_compile_definitions(lib_widget_renderer_platform PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(lib_widget_renderer_platform PRIVATE lib_widget_renderer_warnings_off)
else()
    # No fused multiply-adds, which MSVC does not form either: the same float results on every compiler.
    target_compile_options(lib_widget_renderer_platform PRIVATE -w -ffp-contract=off)
endif()
