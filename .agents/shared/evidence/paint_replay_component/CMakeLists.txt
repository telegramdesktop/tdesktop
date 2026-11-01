# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

cmake_minimum_required(VERSION 3.24)
project(paint_replay LANGUAGES CXX)

if (NOT APPLE)
    message(FATAL_ERROR
        "The paint replay component supports macOS hosts only; "
        "see paint-replay.md.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

get_filename_component(source_root
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
    ABSOLUTE)

if (NOT CMAKE_PREFIX_PATH AND "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
    if (NOT EXISTS "${source_root}/out/CMakeCache.txt")
        message(FATAL_ERROR
            "No Qt prefix: pass -DCMAKE_PREFIX_PATH=<Qt prefix>;<libraries "
            "prefix>, or configure Telegram into ${source_root}/out first.")
    endif()
    load_cache("${source_root}/out" READ_WITH_PREFIX telegram_ qt_requested)
    if (NOT telegram_qt_requested)
        message(FATAL_ERROR
            "${source_root}/out/CMakeCache.txt has no qt_requested; "
            "pass -DCMAKE_PREFIX_PATH=<Qt prefix>;<libraries prefix>.")
    endif()
    get_filename_component(libs_loc "${source_root}/../Libraries" REALPATH)
    set(CMAKE_PREFIX_PATH
        "${libs_loc}/local/Qt-${telegram_qt_requested};${libs_loc}/local")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets)

function(add_paint_component name source)
    add_executable(${name} ${source})
    target_compile_definitions(${name}
    PRIVATE
        QT_NO_KEYWORDS
        QT_NO_CAST_FROM_BYTEARRAY
        QT_DEPRECATED_WARNINGS_SINCE=0x051500
    )
    target_compile_options(${name}
    PRIVATE
        -Wall
        -Wextra
    )
    target_include_directories(${name}
    PRIVATE
        ${source_root}/Telegram/SourceFiles
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_link_libraries(${name}
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::GuiPrivate
        Qt6::Widgets
    )
    qt_import_plugins(${name}
        INCLUDE
            Qt6::QCocoaIntegrationPlugin
            Qt6::QOffscreenIntegrationPlugin
    )
endfunction()

add_paint_component(paint_replay replay.cpp)
add_paint_component(paint_replay_fixture fixture.cpp)
