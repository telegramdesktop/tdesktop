# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# QuickJS-ng: the JavaScript engine interactive HTML messages run their
# scripts on. It is the submodule of ThirdParty/twidget, so it stays at the
# exact revision the other clients use and their precompiled bytecode loads.
# Upstream's own CMakeLists always builds the CLI and test executables and
# sets project-wide flags, so only the engine sources are compiled here.
# There is no packaged branch: the API still changes between releases.

add_library(lib_quickjs STATIC)
init_target(lib_quickjs "(external)")

add_library(tdesktop::lib_quickjs ALIAS lib_quickjs)

set(quickjs_loc ${third_party_loc}/twidget/Sources/CQuickJSNG/quickjs)

if (NOT EXISTS ${quickjs_loc}/quickjs.c)
    message(FATAL_ERROR "QuickJS-ng sources are missing, run 'git submodule update --init --recursive'.")
endif()

# Silence third-party warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX.
if (MSVC)
    add_library(lib_quickjs_warnings_off INTERFACE)
    target_compile_options(lib_quickjs_warnings_off
    INTERFACE
        /W0
        /WX-
    )
endif()

nice_target_sources(lib_quickjs ${quickjs_loc}
PRIVATE
    cutils.h
    dtoa.c
    dtoa.h
    libregexp.c
    libregexp.h
    libunicode.c
    libunicode.h
    list.h
    quickjs-atom.h
    quickjs-c-atomics.h
    quickjs-opcode.h
    quickjs.c
    quickjs.h
)

set_target_properties(lib_quickjs PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)

target_include_directories(lib_quickjs
PUBLIC
    ${quickjs_loc}
)

target_compile_definitions(lib_quickjs
PRIVATE
    _GNU_SOURCE
    QUICKJS_NG_BUILD
)

if (WIN32)
    target_compile_definitions(lib_quickjs
    PRIVATE
        _WIN32_WINNT=0x0601
    )
endif()

if (MSVC)
    # Without it MSVC has no <stdatomic.h> and the engine drops Atomics.
    target_compile_options(lib_quickjs
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>
    )
    target_link_libraries(lib_quickjs PRIVATE lib_quickjs_warnings_off)
else()
    # Upstream builds and tests every platform with unsigned char.
    target_compile_options(lib_quickjs
    PRIVATE
        -funsigned-char
        -w
    )
endif()

if (LINUX)
    find_package(Threads REQUIRED)
    target_link_libraries(lib_quickjs
    PUBLIC
        m
        Threads::Threads
    )
endif()
