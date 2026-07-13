# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Bake 3D meshes (.obj -> .binobj) at build time, so the repository stores the
# human-readable source meshes instead of binary blobs. Every *.obj found under
# Resources/art is baked and exposed under the same :/gui/<relative>.binobj path,
# so new models just need to be dropped in next to the existing ones.
#
# Usage: include(cmake/generate_models.cmake)
# Requires: target "Telegram" and function "nice_target_sources" to exist.

set(_models_art_dir "${CMAKE_CURRENT_SOURCE_DIR}/Resources/art")
file(GLOB_RECURSE _model_sources "${_models_art_dir}/*.obj")

if (_model_sources STREQUAL "")
    return()
endif()

find_package(Python3 REQUIRED)

set(_models_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/obj2binobj.py")
set(_models_out_dir "${CMAKE_CURRENT_BINARY_DIR}/models")
set(_model_outputs)
set(_qrc_entries)
foreach(_src ${_model_sources})
    file(RELATIVE_PATH _rel "${_models_art_dir}" "${_src}")
    string(REGEX REPLACE "\\.obj$" ".binobj" _rel_bin "${_rel}")
    set(_out "${_models_out_dir}/${_rel_bin}")
    get_filename_component(_out_subdir "${_out}" DIRECTORY)
    file(MAKE_DIRECTORY ${_out_subdir})

    add_custom_command(
        OUTPUT ${_out}
        COMMAND ${Python3_EXECUTABLE} ${_models_script} ${_src} ${_out}
        DEPENDS ${_models_script} ${_src}
        COMMENT "Baking model: ${_rel} -> ${_rel_bin}"
        VERBATIM)
    list(APPEND _model_outputs ${_out})
    list(APPEND _qrc_entries "        <file alias=\"art/${_rel_bin}\">${_rel_bin}</file>")
endforeach()

# Write models.qrc at configure time (so AUTORCC can scan its inputs), only
# touching it when content changes — matching cmake/qrhi_shaders.cmake.
list(SORT _qrc_entries)
list(JOIN _qrc_entries "\n" _qrc_body)
set(_qrc_path "${_models_out_dir}/models.qrc")
set(_qrc_new "<RCC>\n    <qresource prefix=\"/gui\">\n${_qrc_body}\n    </qresource>\n</RCC>\n")
set(_qrc_old "")
if (EXISTS "${_qrc_path}")
    file(READ "${_qrc_path}" _qrc_old)
endif()
if (NOT "${_qrc_new}" STREQUAL "${_qrc_old}")
    file(WRITE "${_qrc_path}" "${_qrc_new}")
endif()

add_custom_target(bake_models DEPENDS ${_model_outputs})
nice_target_sources(Telegram ${_models_out_dir}
PRIVATE
    models.qrc
)
add_dependencies(Telegram bake_models)
message(STATUS "Models: will bake ${_models_art_dir}/**/*.obj -> .binobj")
