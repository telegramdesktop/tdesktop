# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

if (APPLE)
    # target_precompile_headers with COMPILE_LANGUAGE restriction.
    cmake_minimum_required(VERSION 3.23)
else()
    cmake_minimum_required(VERSION 3.16)
endif()
if (POLICY CMP0149)
    cmake_policy(SET CMP0149 NEW)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(cmake/validate_special_target.cmake)
include(cmake/version.cmake)
desktop_app_parse_version(Telegram/build/version)

set(project_langs C CXX)
if (APPLE)
    list(APPEND project_langs OBJC OBJCXX)
elseif (LINUX)
    list(APPEND project_langs ASM)
endif()

project(Telegram
    LANGUAGES ${project_langs}
    VERSION ${desktop_app_version_cmake}
    DESCRIPTION "Official Telegram Desktop messenger"
    HOMEPAGE_URL "https://desktop.telegram.org"
)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Telegram)

get_filename_component(third_party_loc "Telegram/ThirdParty" REALPATH)
get_filename_component(submodules_loc "Telegram" REALPATH)
get_filename_component(cmake_helpers_loc "cmake" REALPATH)

if (NOT DESKTOP_APP_USE_PACKAGED AND WIN32)
    set(Python3_EXECUTABLE ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/python/Scripts/python)
endif()

include(cmake/variables.cmake)
include(cmake/nice_target_sources.cmake)
include(cmake/target_compile_options_if_exists.cmake)
include(cmake/target_link_frameworks.cmake)
include(cmake/target_link_optional_libraries.cmake)
include(cmake/target_link_options_if_exists.cmake)
include(cmake/target_link_static_libraries.cmake)
include(cmake/init_target.cmake)
include(cmake/generate_target.cmake)
include(cmake/nuget.cmake)
include(cmake/validate_d3d_compiler.cmake)
include(cmake/target_prepare_qrc.cmake)

include(cmake/options.cmake)

if (NOT DESKTOP_APP_USE_PACKAGED)
    if (WIN32)
        set(qt_version 5.15.11)
    elseif (APPLE)
        set(qt_version 6.2.6)
    endif()
endif()
include(cmake/external/qt/package.cmake)

set(desktop_app_skip_libs
    variant
)

add_subdirectory(cmake)
add_subdirectory(Telegram)
