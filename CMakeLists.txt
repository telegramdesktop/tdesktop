# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

cmake_minimum_required(VERSION 3.25...3.31)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(cmake/validate_special_target.cmake)
include(cmake/version.cmake)
desktop_app_parse_version(Telegram/build/version)

if (NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(configuration_types_init 1)
endif()

project(Telegram
    LANGUAGES C CXX
    VERSION ${desktop_app_version_cmake}
    DESCRIPTION "Official Telegram Desktop messenger"
    HOMEPAGE_URL "https://desktop.telegram.org"
)

if (APPLE)
    enable_language(OBJC OBJCXX)
endif()

if (configuration_types_init
    AND CMAKE_CONFIGURATION_TYPES
    AND NOT MinSizeRel IN_LIST CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES};MinSizeRel" CACHE STRING "" FORCE)
endif()

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
include(cmake/target_link_options_if_exists.cmake)
include(cmake/init_target.cmake)
include(cmake/generate_target.cmake)
include(cmake/nuget.cmake)
include(cmake/validate_d3d_compiler.cmake)
include(cmake/target_prepare_qrc.cmake)

include(cmake/options.cmake)

# Nested: `if' expands all arguments, QT must be read only when set.
if (WIN32 AND NOT build_winarm AND NOT build_winstore AND DEFINED ENV{QT})
    if (NOT "$ENV{QT}" VERSION_LESS 6)
        # Windows 7 and 8 need YY-Thunks and ANGLE, Qt is already built with EGL.
        set(DESKTOP_APP_WIN7_BASELINE TRUE)

        set(angle_loc ${libs_loc}/tg_angle)
        set(angle_lib ${angle_loc}/out/Release/tg_angle.lib)
        set(HAVE_EGL ON CACHE BOOL "ANGLE provides EGL")
        set(EGL_INCLUDE_DIR ${angle_loc}/include CACHE PATH "ANGLE headers")
        set(EGL_LIBRARY ${angle_lib} CACHE FILEPATH "ANGLE library")
        set(HAVE_GLESv2 ON CACHE BOOL "ANGLE provides OpenGL ES 2")
        set(GLESv2_INCLUDE_DIR ${angle_loc}/include CACHE PATH "ANGLE headers")
        set(GLESv2_LIBRARY ${angle_lib} CACHE FILEPATH "ANGLE library")

        # Qt plugin links EGL::EGL, per-config target keeps Debug on Debug ANGLE.
        add_library(EGL::EGL STATIC IMPORTED)
        set_target_properties(EGL::EGL PROPERTIES
            IMPORTED_LOCATION ${angle_lib}
            IMPORTED_LOCATION_DEBUG ${angle_loc}/out/Debug/tg_angle.lib
            INTERFACE_INCLUDE_DIRECTORIES ${angle_loc}/include
        )
    endif()
endif()

include(cmake/external/qt/package.cmake)

set(desktop_app_skip_libs
    glibmm
    variant
)

add_subdirectory(cmake)
add_subdirectory(Telegram)
