# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# The shared litehtml renderer core from ThirdParty/widget-sandbox
# (apps/renderer-litehtml): it applies validated widget operations to a
# document laid out by twidget's litehtml fork, answers layout queries, runs
# animations, canvas and input, and records a display list the platform
# paints. Built in fork mode (TW_LITEHTML_TWIDGET) against lib_litehtml; the
# per-platform backends are not compiled, our Qt backend replaces them.
# Consumers include its headers as "renderer-litehtml/src/<name>.h".
# There is no packaged branch: no distribution ships this library.

add_library(lib_widget_renderer STATIC)
init_target(lib_widget_renderer "(external)")

add_library(tdesktop::lib_widget_renderer ALIAS lib_widget_renderer)

set(widget_sandbox_root ${third_party_loc}/widget-sandbox)
set(widget_renderer_loc ${widget_sandbox_root}/apps/renderer-litehtml/src)

if (NOT EXISTS ${widget_renderer_loc}/renderer.cpp)
    message(FATAL_ERROR "widget-sandbox sources are missing, run 'git submodule update --init --recursive'.")
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

nice_target_sources(lib_widget_renderer ${widget_renderer_loc}
PRIVATE
    animations.cpp
    animations.h
    backend.h
    canvas.cpp
    canvas.h
    canvas_ops.h
    compositor.cpp
    compositor.h
    css.cpp
    css.h
    display_list.h
    renderer.cpp
    renderer.h
)

target_include_directories(lib_widget_renderer
PUBLIC
    ${widget_sandbox_root}/apps
    ${widget_sandbox_root}/packages/desktop/sandbox/include
PRIVATE
    ${widget_renderer_loc}
)

target_compile_definitions(lib_widget_renderer
PUBLIC
    TW_LITEHTML_TWIDGET
)

target_link_libraries(lib_widget_renderer
PUBLIC
    tdesktop::lib_litehtml
)

if (MSVC)
    target_link_libraries(lib_widget_renderer PRIVATE lib_widget_renderer_warnings_off)
else()
    target_compile_options(lib_widget_renderer PRIVATE -w)
endif()
