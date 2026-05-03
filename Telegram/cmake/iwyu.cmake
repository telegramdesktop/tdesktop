# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

option(DESKTOP_APP_USE_IWYU "Enable include-what-you-use checks for the Telegram target." OFF)

if (DESKTOP_APP_USE_IWYU)
    find_program(IWYU_EXECUTABLE
        NAMES include-what-you-use iwyu
        DOC "Path to the include-what-you-use binary.")

    if (NOT IWYU_EXECUTABLE)
        message(FATAL_ERROR
            " \n"
            " DESKTOP_APP_USE_IWYU is ON but include-what-you-use was not found.\n"
            " Install it (brew install include-what-you-use, apt install iwyu, ...)\n"
            " or override IWYU_EXECUTABLE on the CMake command line.\n"
            " ")
    endif()

    get_filename_component(_iwyu_scripts_dir "${CMAKE_SOURCE_DIR}/scripts/iwyu" REALPATH)

    set(_iwyu_args "${IWYU_EXECUTABLE}")
    list(APPEND _iwyu_args "-Xiwyu" "--no_fwd_decls")
    list(APPEND _iwyu_args "-Xiwyu" "--cxx17ns")
    list(APPEND _iwyu_args "-Xiwyu" "--quoted_includes_first")
    list(APPEND _iwyu_args "-Xiwyu" "--max_line_length=120")

    # IWYU is shipped against its own clang build, which does not know about
    # Apple Clang's bundled libc++/libc headers. Pin the resource dir to the
    # actual compiler's so <climits>, <type_traits>, etc. resolve. gcc has
    # no -print-resource-dir; ERROR_QUIET hides its complaint when this
    # configures on a non-clang toolchain (e.g. gcc-toolset on centos_env).
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE _compiler_resource_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _compiler_resource_dir_rc)
    if (_compiler_resource_dir_rc EQUAL 0 AND _compiler_resource_dir)
        list(APPEND _iwyu_args "-resource-dir=${_compiler_resource_dir}")
    endif()

    # Apple Clang infers its sysroot from the toolchain; IWYU does not.
    # Without -isysroot, <cmath>/<type_traits> are not found on macOS.
    if (APPLE)
        execute_process(
            COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE _macos_sdk_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _macos_sdk_rc)
        if (_macos_sdk_rc EQUAL 0 AND _macos_sdk_path)
            list(APPEND _iwyu_args "-isysroot" "${_macos_sdk_path}")
        endif()
    endif()

    if (EXISTS "${_iwyu_scripts_dir}/iwyu.imp")
        list(APPEND _iwyu_args "-Xiwyu" "--mapping_file=${_iwyu_scripts_dir}/iwyu.imp")
    endif()

    if (DEFINED DESKTOP_APP_IWYU_EXTRA_ARGS)
        list(APPEND _iwyu_args ${DESKTOP_APP_IWYU_EXTRA_ARGS})
    endif()

    set(DESKTOP_APP_IWYU_COMMAND "${_iwyu_args}" CACHE INTERNAL "Cached IWYU command line.")

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

    message(STATUS "IWYU enabled: ${IWYU_EXECUTABLE}")
endif()

# Apply IWYU to a single CMake target. No-op when DESKTOP_APP_USE_IWYU is OFF,
# so call sites stay clean and the function is safe to invoke unconditionally.
function(desktop_app_apply_iwyu target_name)
    if (NOT DESKTOP_APP_USE_IWYU)
        return()
    endif()
    # NOTE: this hooks IWYU directly into the build via CXX_INCLUDE_WHAT_YOU_USE.
    # It only works if the target also has DISABLE_PRECOMPILE_HEADERS=ON because
    # IWYU rejects -include-pch. The default flow (run_iwyu.py + iwyu_tool.py)
    # does not set the property; it instead strips -include-pch from a copy of
    # compile_commands.json, so PCH stays enabled for the regular build.
    set_property(TARGET ${target_name}
        PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${DESKTOP_APP_IWYU_COMMAND})
    set_property(TARGET ${target_name} PROPERTY DISABLE_PRECOMPILE_HEADERS ON)
endfunction()
