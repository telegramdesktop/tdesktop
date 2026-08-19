# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Pack Lottie animations (.json -> .tgs) at build time, so the repository
# stores the plain JSON sources instead of gzip blobs. A .tgs source is copied
# through unchanged, so animations taken from upstream work as they are.
#
# Resources/qrc/telegram/animations.qrc stays the place where an animation is
# registered: it lists the JSON sources with the resource aliases used from
# C++. It is not compiled directly, it is rewritten into the build tree with
# the same aliases pointing at the packed files.
#
# Usage: include(cmake/pack_animations.cmake)
# Requires: target "Telegram" and function "nice_target_sources" to exist.

set(_animations_qrc "${CMAKE_CURRENT_SOURCE_DIR}/Resources/qrc/telegram/animations.qrc")

if (NOT EXISTS "${_animations_qrc}")
    return()
endif()

find_package(Python3 REQUIRED)

set(_animations_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/json2tgs.py")
set(_animations_out_dir "${CMAKE_CURRENT_BINARY_DIR}/animations")
get_filename_component(_animations_qrc_dir "${_animations_qrc}" DIRECTORY)

file(READ "${_animations_qrc}" _animations_qrc_content)
string(REGEX MATCH "<qresource[^>]*prefix=\"([^\"]*)\"" _ "${_animations_qrc_content}")
set(_animations_prefix "${CMAKE_MATCH_1}")
string(REGEX MATCHALL "<file[^>]*>[^<]*</file>" _animations_entries "${_animations_qrc_content}")

set(_animations_sources)
set(_animations_outputs)
set(_qrc_entries)
foreach (_entry ${_animations_entries})
    string(REGEX REPLACE "^<file[^>]*>(.*)</file>$" "\\1" _relative "${_entry}")
    if ("${_entry}" MATCHES "alias=\"([^\"]*)\"")
        set(_alias "${CMAKE_MATCH_1}")
    else()
        set(_alias "${_relative}")
    endif()
    get_filename_component(_src "${_animations_qrc_dir}/${_relative}" ABSOLUTE)
    set(_out "${_animations_out_dir}/${_alias}")
    get_filename_component(_out_subdir "${_out}" DIRECTORY)
    file(MAKE_DIRECTORY ${_out_subdir})

    add_custom_command(
        OUTPUT ${_out}
        COMMAND ${Python3_EXECUTABLE} ${_animations_script} ${_src} ${_out}
        DEPENDS ${_animations_script} ${_src}
        COMMENT "Packing animation: ${_alias}"
        VERBATIM)
    list(APPEND _animations_sources ${_src})
    list(APPEND _animations_outputs ${_out})
    list(APPEND _qrc_entries "        <file>${_alias}</file>")
endforeach()

if (NOT _animations_outputs)
    return()
endif()

# Write animations.qrc at configure time (so AUTORCC can scan its inputs), only
# touching it when content changes — matching cmake/generate_models.cmake.
list(SORT _qrc_entries)
list(JOIN _qrc_entries "\n" _qrc_body)
set(_qrc_path "${_animations_out_dir}/animations.qrc")
set(_qrc_new "<RCC>\n    <qresource prefix=\"${_animations_prefix}\">\n${_qrc_body}\n    </qresource>\n</RCC>\n")
set(_qrc_old "")
if (EXISTS "${_qrc_path}")
    file(READ "${_qrc_path}" _qrc_old)
endif()
if (NOT "${_qrc_new}" STREQUAL "${_qrc_old}")
    file(WRITE "${_qrc_path}" "${_qrc_new}")
endif()

# target_prepare_qrc() skips build-tree paths, so declare the JSON sources.
set_source_files_properties("${_qrc_path}" PROPERTIES
    QRC_GENERATED_FROM "${_animations_script};${_animations_sources}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_animations_qrc})
add_custom_target(pack_animations DEPENDS ${_animations_outputs})
nice_target_sources(Telegram ${_animations_out_dir}
PRIVATE
    animations.qrc
)
add_dependencies(Telegram pack_animations)
message(STATUS "Animations: will pack ${_animations_qrc} sources -> .tgs")
