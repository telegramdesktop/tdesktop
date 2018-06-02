##
## Modified for Telegram Desktop project by Telegram Desktop authors.
##
# Function for setting up precompiled headers. Usage:
#
#   add_library/executable(target
#       pchheader.c pchheader.cpp pchheader.h)
#
#   add_precompiled_header(target pchheader.h
#       [FORCEINCLUDE]
#       [SOURCE_C pchheader.c]
#       [SOURCE_CXX pchheader.cpp])
#
# Options:
#
#   FORCEINCLUDE: Add compiler flags to automatically include the
#   pchheader.h from every source file. Works with both GCC and
#   MSVC. This is recommended.
#
#   SOURCE_C/CXX: Specifies the .c/.cpp source file that includes
#   pchheader.h for generating the pre-compiled header
#   output. Defaults to pchheader.c. Only required for MSVC.
#
# Caveats:
#
#   * Its not currently possible to use the same precompiled-header in
#     more than a single target in the same directory (No way to set
#     the source file properties differently for each target).
#
#   * MSVC: A source file with the same name as the header must exist
#     and be included in the target (E.g. header.cpp). Name of file
#     can be changed using the SOURCE_CXX/SOURCE_C options.
#
# License:
#
# Copyright (C) 2009-2013 Lars Christensen <larsch@belunktum.dk>
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the 'Software') deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

include(CMakeParseArguments)

macro(combine_arguments _variable)
  set(_result "")
  foreach(_element ${${_variable}})
    set(_result "${_result} \"${_element}\"")
  endforeach()
  string(STRIP "${_result}" _result)
  set(${_variable} "${_result}")
endmacro()

function(export_all_flags _filename _source_name_for_flags)
  set(_include_directories "$<TARGET_PROPERTY:${_target},INCLUDE_DIRECTORIES>")
  set(_compile_definitions "$<TARGET_PROPERTY:${_target},COMPILE_DEFINITIONS>")
  get_source_file_property(_compile_file_flags "${_source_name_for_flags}" COMPILE_FLAGS)
  set(_compile_flags "$<TARGET_PROPERTY:${_target},COMPILE_FLAGS>")
  set(_compile_options "$<TARGET_PROPERTY:${_target},COMPILE_OPTIONS>")
  set(_include_directories "$<$<BOOL:${_include_directories}>:-I$<JOIN:${_include_directories},\n-I>\n>")
  set(_compile_definitions "$<$<BOOL:${_compile_definitions}>:-D$<JOIN:${_compile_definitions},\n-D>\n>")
  set(_compile_file_flags "$<$<BOOL:${_compile_file_flags}>:$<JOIN:${_compile_file_flags},\n>\n>")
  set(_compile_flags "$<$<BOOL:${_compile_flags}>:$<JOIN:${_compile_flags},\n>\n>")
  set(_compile_options "$<$<BOOL:${_compile_options}>:$<JOIN:${_compile_options},\n>\n>")
  file(GENERATE OUTPUT "${_filename}" CONTENT "${_compile_definitions}${_include_directories}${_compile_file_flags}${_compile_flags}${_compile_options}\n")
endfunction()

function(add_precompiled_header _target _input)
  if(CMAKE_COMPILER_IS_GNUCXX)
    get_filename_component(_name ${_input} NAME)
    set(_pch_header "${CMAKE_CURRENT_SOURCE_DIR}/${_input}")
    set(_pch_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/${_target}_pch")
    set(_pchfile "${_pch_binary_dir}/${_name}")
    set(_outdir "${_pch_binary_dir}/${_name}.gch")
    make_directory(${_outdir})
    set(_output_cxx "${_outdir}/.c++")
    set(_output_c "${_outdir}/.c")

    get_property(_sources TARGET ${_target} PROPERTY SOURCES)
    foreach(_source ${_sources})
      if(_source MATCHES \\.\(c\)$ AND NOT _source_for_c_flags)
        set(_source_for_c_flags "${_source}")
      elseif(_source MATCHES \\.\(cc|cxx|cpp\)$ AND NOT _source_for_cpp_flags)
        set(_source_for_cpp_flags "${_source}")
      endif()
    endforeach()

    add_custom_command(
      OUTPUT "${_pchfile}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${_pch_header}" "${_pchfile}"
      DEPENDS "${_pch_header}"
      IMPLICIT_DEPENDS CXX "${_pch_header}"
      IMPLICIT_DEPENDS C "${_pch_header}"
      COMMENT "Updating ${_name}")

    if(_source_for_c_flags)
      set(_pch_c_flags_file "${_pch_binary_dir}/compile_flags_c.rsp")
      export_all_flags("${_pch_c_flags_file}" "${_source_for_c_flags}")
      set(_compiler_FLAGS "@${_pch_c_flags_file}")
      add_custom_command(
        OUTPUT "${_output_c}"
        COMMAND "${CMAKE_C_COMPILER}" ${_compiler_FLAGS} -x c-header -o "${_output_c}" -c "${_pchfile}"
        DEPENDS "${_pchfile}" "${_pch_c_flags_file}"
        IMPLICIT_DEPENDS C "${_pch_header}"
        COMMENT "Precompiling ${_name} for ${_target} (C)")
    endif()
    if(_source_for_cpp_flags)
      set(_pch_cpp_flags_file "${_pch_binary_dir}/compile_flags_cpp.rsp")
      export_all_flags("${_pch_cpp_flags_file}" "${_source_for_cpp_flags}")
      set(_compiler_FLAGS "@${_pch_cpp_flags_file}")
      add_custom_command(
        OUTPUT "${_output_cxx}"
        COMMAND "${CMAKE_CXX_COMPILER}" ${_compiler_FLAGS} -x c++-header -o "${_output_cxx}" -c "${_pchfile}"
        DEPENDS "${_pchfile}" "${_pch_cpp_flags_file}"
        IMPLICIT_DEPENDS CXX "${_pch_header}"
        COMMENT "Precompiling header ${_name} for ${_target} (C++)")
    endif()

    foreach(_source ${_sources})
      set(_pch_compile_flags "")

      if(_source MATCHES \\.\(cc|cxx|cpp|c\)$)
        get_source_file_property(_pch_compile_flags "${_source}" COMPILE_FLAGS)
        if(NOT _pch_compile_flags)
          set(_pch_compile_flags)
        endif()
        separate_arguments(_pch_compile_flags)
        if(_source MATCHES \\.\(cc|cxx|cpp\)$)
          list(APPEND _pch_compile_flags -include "${_pchfile}")
        else()
          list(APPEND _pch_compile_flags "-I${_pch_binary_dir}")
        endif()

        get_source_file_property(_object_depends "${_source}" OBJECT_DEPENDS)
        if(NOT _object_depends)
          set(_object_depends)
        endif()
        list(APPEND _object_depends "${_pchfile}")
        if(_source MATCHES \\.\(cc|cxx|cpp\)$)
          list(APPEND _object_depends "${_output_cxx}")
        else()
          list(APPEND _object_depends "${_output_c}")
        endif()

        combine_arguments(_pch_compile_flags)
        set_source_files_properties(${_source} PROPERTIES
          COMPILE_FLAGS "${_pch_compile_flags}"
          OBJECT_DEPENDS "${_object_depends}")
      endif()
    endforeach()
  endif(CMAKE_COMPILER_IS_GNUCXX)
endfunction()
