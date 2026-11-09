# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# The shared widget sandbox from ThirdParty/widget-sandbox (packages/desktop):
# the Qt-free C++ host that runs untrusted widget scripts in QuickJS, checks
# every DOM operation with the shared validator and talks the shared host
# protocol to a renderer. Its JavaScript bundles live next to it in
# packages/desktop/WidgetJS. Only the library sources are compiled here; the
# package's own CMakeLists always builds its harness and tests as well.
# On Windows IcuIntl (src/icu_intl.cpp) is built too, linking icu; Telegram delay-loads icu.dll.
# There is no packaged branch: no distribution ships this library.

add_library(lib_widget_sandbox STATIC)
init_target(lib_widget_sandbox "(external)")

add_library(tdesktop::lib_widget_sandbox ALIAS lib_widget_sandbox)

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

nice_target_sources(lib_widget_sandbox ${widget_sandbox_loc}
PRIVATE
    include/twidget/bundle_stamp.h
    include/twidget/cpu_budget.h
    include/twidget/gesture_policy.h
    include/twidget/intl.h
    include/twidget/js_thread.h
    include/twidget/json.h
    include/twidget/quickjs_context.h
    include/twidget/renderer.h
    include/twidget/resources.h
    include/twidget/sandbox_engine.h
    include/twidget/sdk_policy.h
    include/twidget/trusted_validator.h
    include/twidget/version.h
    include/twidget/widget_host.h
    src/bundle_stamp.cpp
    src/cpu_budget.cpp
    src/gesture_policy.cpp
    src/intl.cpp
    src/js_thread.cpp
    src/quickjs_context.cpp
    src/resources.cpp
    src/sandbox_engine.cpp
    src/sdk_policy.cpp
    src/trusted_validator.cpp
    src/widget_host.cpp
)

target_include_directories(lib_widget_sandbox
PUBLIC
    ${widget_sandbox_loc}/include
)

target_link_libraries(lib_widget_sandbox
PUBLIC
    tdesktop::lib_quickjs
)

if (WIN32)
    nice_target_sources(lib_widget_sandbox ${widget_sandbox_loc}
    PRIVATE
        include/twidget/icu_intl.h
        src/icu_intl.cpp
    )
    target_link_libraries(lib_widget_sandbox PUBLIC bcrypt icu)
elseif (LINUX)
    find_package(Threads REQUIRED)
    target_link_libraries(lib_widget_sandbox PUBLIC Threads::Threads)
endif()

if (MSVC)
    target_link_libraries(lib_widget_sandbox PRIVATE lib_widget_sandbox_warnings_off)
else()
    target_compile_options(lib_widget_sandbox PRIVATE -w)
endif()
