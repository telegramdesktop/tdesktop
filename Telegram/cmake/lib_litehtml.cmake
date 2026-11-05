# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# litehtml as forked in ThirdParty/twidget: the HTML and CSS layout engine
# for interactive HTML messages, with the fork's document patching, pixel
# snapping, SVG, grid and effects.
# Only the portable C++ core is built; twidget's Objective-C++ and Swift
# layers are Apple renderers, replaced on our side by a Qt one.
# There is no packaged branch: no distribution ships this fork.

add_library(lib_litehtml STATIC)
init_target(lib_litehtml cxx_std_17 "(external)")

add_library(tdesktop::lib_litehtml ALIAS lib_litehtml)

set(litehtml_loc ${third_party_loc}/twidget/litehtml)

if (NOT EXISTS ${litehtml_loc}/src/document.cpp)
    message(FATAL_ERROR "twidget sources are missing, run 'git submodule update --init --recursive'.")
endif()

# Silence third-party warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX.
if (MSVC)
    add_library(lib_litehtml_warnings_off INTERFACE)
    target_compile_options(lib_litehtml_warnings_off
    INTERFACE
        /W0
        /WX-
    )
endif()

# The fork adds and splits files as it grows; the directories hold nothing
# but the portable engine, so a glob follows it across submodule updates.
file(GLOB litehtml_sources CONFIGURE_DEPENDS
    ${litehtml_loc}/include/*.h
    ${litehtml_loc}/include/litehtml/*.h
    ${litehtml_loc}/src/*.cpp
    ${litehtml_loc}/src/*.h
    ${litehtml_loc}/gumbo-include/*.h
    ${litehtml_loc}/gumbo-include/gumbo/*.h
    ${litehtml_loc}/src/gumbo/*.c
    ${litehtml_loc}/src/gumbo/*.h
)

target_sources(lib_litehtml PRIVATE ${litehtml_sources})

target_include_directories(lib_litehtml
PUBLIC
    ${litehtml_loc}/include
    ${litehtml_loc}/include/litehtml
PRIVATE
    ${litehtml_loc}/gumbo-include
)

if (MSVC)
    # gumbo includes POSIX <strings.h>, the fork ships a stand-in for MSVC.
    target_include_directories(lib_litehtml
    PRIVATE
        ${litehtml_loc}/src/gumbo/visualc/include
    )
    target_link_libraries(lib_litehtml PRIVATE lib_litehtml_warnings_off)
else()
    target_compile_options(lib_litehtml PRIVATE -w)
endif()
