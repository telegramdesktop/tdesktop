# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

include(CheckCompilerFlag)
include(CheckSourceCompiles)
include(CMakePushCheckState)

set(widget_helper_hardened_targets
    TelegramWidgetHelper
    lib_widget_sandbox_platform
)

get_property(widget_helper_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (widget_helper_multi_config)
    set(widget_helper_configs ${CMAKE_CONFIGURATION_TYPES})
elseif (CMAKE_BUILD_TYPE)
    set(widget_helper_configs ${CMAKE_BUILD_TYPE})
else()
    set(widget_helper_configs None)
endif()

function(widget_helper_archs target_name result)
    get_target_property(archs ${target_name} OSX_ARCHITECTURES)
    if (NOT archs)
        set(archs "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if (NOT archs AND DEFINED CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        set(archs "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
    endif()
    set(${result} "${archs}" PARENT_SCOPE)
endfunction()

function(widget_helper_values result)
    set(values)
    foreach (variable IN LISTS ARGN)
        if (DEFINED ${variable})
            list(APPEND values "${variable}=${${variable}}")
        endif()
    endforeach()
    set(${result} "${values}" PARENT_SCOPE)
endfunction()

function(widget_helper_flags result)
    set(variables)
    foreach (flags IN LISTS ARGN)
        list(APPEND variables CMAKE_${flags})
        foreach (config IN LISTS widget_helper_configs)
            string(TOUPPER ${config} config)
            list(APPEND variables CMAKE_${flags}_${config})
        endforeach()
    endforeach()
    widget_helper_values(values ${variables})
    set(${result} "${values}" PARENT_SCOPE)
endfunction()

set(widget_helper_sdk "${CMAKE_OSX_SYSROOT}")
if (NOT IS_DIRECTORY "${widget_helper_sdk}")
    execute_process(COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE widget_helper_sdk
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if (IS_SYMLINK "${widget_helper_sdk}")
    set(widget_helper_sdk_stamp ${CMAKE_CURRENT_BINARY_DIR}/gen/widget_helper_sdk.txt)
    file(CONFIGURE OUTPUT ${widget_helper_sdk_stamp}.cmake CONTENT [[
file(REAL_PATH "${sdk}" sdk)
file(CONFIGURE OUTPUT "${stamp}" CONTENT "${sdk}" @ONLY)
]] @ONLY)
    set(widget_helper_sdk_check ${CMAKE_COMMAND} -Dsdk=${widget_helper_sdk} -Dstamp=${widget_helper_sdk_stamp}
        -P ${widget_helper_sdk_stamp}.cmake)
    execute_process(COMMAND ${widget_helper_sdk_check})
    add_custom_target(widget_helper_sdk COMMAND ${widget_helper_sdk_check} BYPRODUCTS ${widget_helper_sdk_stamp} VERBATIM)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${widget_helper_sdk_stamp})
    file(REAL_PATH "${widget_helper_sdk}" widget_helper_sdk)
endif()
file(TIMESTAMP "${widget_helper_sdk}/SDKSettings.json" widget_helper_sdk_time)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" widget_helper_module)
widget_helper_flags(widget_helper_inputs C_FLAGS CXX_FLAGS EXE_LINKER_FLAGS)
separate_arguments(widget_helper_sdk_flags UNIX_COMMAND "${widget_helper_inputs}")
string(REGEX MATCH "(^|[;=,])(-[iIFL]|--sysroot|-syslibroot)" widget_helper_sdk_flags "${widget_helper_sdk_flags}")
widget_helper_values(widget_helper_toolchain
    CMAKE_OSX_SYSROOT
    CMAKE_OSX_DEPLOYMENT_TARGET
    CMAKE_OSX_ARCHITECTURES
    CMAKE_C_COMPILER_TARGET
    CMAKE_CXX_COMPILER_TARGET
)
list(APPEND widget_helper_inputs
    ${widget_helper_toolchain}
    "${widget_helper_sdk}"
    "${widget_helper_sdk_time}"
    "${widget_helper_module}"
    "${widget_helper_configs}"
)
foreach (language C CXX)
    execute_process(COMMAND "${CMAKE_${language}_COMPILER}" --version
        OUTPUT_VARIABLE widget_helper_compiler
        ERROR_QUIET)
    list(APPEND widget_helper_inputs "${widget_helper_compiler}")
endforeach()
string(SHA256 widget_helper_inputs "${widget_helper_inputs}")
if (NOT TDESKTOP_WIDGET_HELPER_CHECKED_FOR STREQUAL widget_helper_inputs)
    get_cmake_property(widget_helper_checks CACHE_VARIABLES)
    list(FILTER widget_helper_checks INCLUDE REGEX "^TDESKTOP_WIDGET_HELPER_")
    foreach (check IN LISTS widget_helper_checks)
        unset(${check} CACHE)
    endforeach()
    set(TDESKTOP_WIDGET_HELPER_CHECKED_FOR "${widget_helper_inputs}" CACHE INTERNAL "")
endif()

cmake_push_check_state(RESET)
foreach (language C CXX)
    check_compiler_flag(${language} -fstack-protector-strong TDESKTOP_WIDGET_HELPER_${language}_STACK_PROTECTOR)
    check_compiler_flag(${language} -ftrivial-auto-var-init=zero TDESKTOP_WIDGET_HELPER_${language}_ZERO_INIT)
    check_compiler_flag(${language} -fno-typed-memory-operations TDESKTOP_WIDGET_HELPER_${language}_TYPED_MALLOC_FLAG)
endforeach()
check_compiler_flag(CXX "-fno-typed-cxx-new-delete -fno-typed-cxx-delete" TDESKTOP_WIDGET_HELPER_TYPED_NEW_DELETE_FLAG)
cmake_pop_check_state()

widget_helper_archs(lib_widget_sandbox_platform widget_helper_library_archs)
set(widget_helper_typed_cxx_archs)
foreach (arch IN LISTS widget_helper_library_archs)
    # The Intel object of libc++polyfills, which typed new and delete link below macOS 15, needs macOS 10.15.
    if (arch STREQUAL arm64 OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER_EQUAL 10.15)
        list(APPEND widget_helper_typed_cxx_archs ${arch})
    endif()
endforeach()

set(widget_helper_typed_malloc_guard ${CMAKE_CURRENT_BINARY_DIR}/gen/widget_helper_typed_malloc.h)
file(CONFIGURE OUTPUT ${widget_helper_typed_malloc_guard} CONTENT [[
#if __has_feature(typed_memory_operations) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 140000
#include <malloc/_malloc_type.h>
#if !(defined(_MALLOC_TYPE_MALLOC_IS_BACKDEPLOYING) && _MALLOC_TYPE_MALLOC_IS_BACKDEPLOYING)
#error "TelegramWidgetHelper was configured with other SDK headers, these call malloc_type_* unguarded below macOS 14: rerun CMake with -U 'TDESKTOP_WIDGET_HELPER_*'."
#endif
#endif
]] @ONLY)
set(widget_helper_typed_malloc -ftyped-memory-operations -include${widget_helper_typed_malloc_guard})

string(CONFIGURE [[
#include "@widget_helper_typed_malloc_guard@"
#include <stdlib.h>
#if !__has_feature(typed_memory_operations)
#error typed memory operations are not enabled
#endif
int main(void) {
    void *volatile memory = malloc(16);
    free(memory);
    return 0;
}
]] widget_helper_typed_malloc_source @ONLY)

function(widget_helper_probe language config flags source result)
    string(TOUPPER ${config} suffix)
    if (widget_helper_sdk_flags)
        unset(${result}_${suffix} CACHE)
    endif()
    list(JOIN flags " " options)
    set(CMAKE_TRY_COMPILE_CONFIGURATION ${config})
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    string(APPEND CMAKE_${language}_FLAGS_${suffix} " ${options}")
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LINK_OPTIONS ${flags})
    check_source_compiles(${language} "${source}" ${result}_${suffix})
    cmake_pop_check_state()
endfunction()

function(widget_helper_check config)
    widget_helper_probe(CXX ${config} "" [[
#undef _LIBCPP_HARDENING_MODE
#define _LIBCPP_HARDENING_MODE _LIBCPP_HARDENING_MODE_FAST
#include <cstddef>
#ifndef _LIBCPP_HARDENING_MODE_FAST
#error libc++ hardening modes are not supported
#endif
int main() {
    return 0;
}
]] TDESKTOP_WIDGET_HELPER_LIBCPP_HARDENING)
    foreach (language C CXX)
        if (TDESKTOP_WIDGET_HELPER_${language}_TYPED_MALLOC_FLAG)
            widget_helper_probe(${language} ${config} -ftyped-memory-operations "${widget_helper_typed_malloc_source}"
                TDESKTOP_WIDGET_HELPER_${language}_TYPED_MALLOC)
        endif()
    endforeach()
    if (TDESKTOP_WIDGET_HELPER_TYPED_NEW_DELETE_FLAG)
        foreach (arch IN LISTS widget_helper_typed_cxx_archs)
            set(CMAKE_OSX_ARCHITECTURES ${arch})
            widget_helper_probe(CXX ${config} "-ftyped-cxx-new-delete;-ftyped-cxx-delete" [[
#if !__has_feature(typed_cxx_new_delete)
#error typed new and delete are not enabled
#endif
#if defined(__x86_64__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
#error the Intel object of libc++polyfills needs macOS 10.15
#endif
struct Node {
    Node *next;
    int value;
};
Node *volatile node;
int main() {
    node = new Node{ nullptr, 1 };
    const int result = node->value;
    delete node;
    return result;
}
]] TDESKTOP_WIDGET_HELPER_TYPED_NEW_DELETE_${arch})
        endforeach()
    endif()
endfunction()

foreach (config IN LISTS widget_helper_configs)
    widget_helper_check(${config})
endforeach()

function(widget_helper_condition check result)
    set(passing)
    set(failing)
    foreach (config IN LISTS widget_helper_configs)
        string(TOUPPER ${config} suffix)
        if (${check} OR ${check}_${suffix})
            list(APPEND passing ${config})
        else()
            list(APPEND failing ${config})
        endif()
    endforeach()
    set(${result}_failing "" PARENT_SCOPE)
    if (NOT failing)
        set(${result} 1 PARENT_SCOPE)
    elseif (NOT passing)
        set(${result} 0 PARENT_SCOPE)
    else()
        list(JOIN passing , passing)
        set(${result} $<CONFIG:${passing}> PARENT_SCOPE)
        set(${result}_failing " in ${failing}" PARENT_SCOPE)
    endif()
endfunction()

set(widget_helper_unsupported)
macro(widget_helper_require condition what)
    if (NOT ${condition} STREQUAL 1)
        list(APPEND widget_helper_unsupported "${what}${${condition}_failing}")
    endif()
endmacro()

set(widget_helper_C_languages C,OBJC)
set(widget_helper_CXX_languages CXX,OBJCXX)
set(widget_helper_measures STACK_PROTECTOR ZERO_INIT TYPED_MALLOC)
set(widget_helper_measure_flags -fstack-protector-strong -ftrivial-auto-var-init=zero -ftyped-memory-operations)
foreach (language C CXX)
    foreach (measure flag IN ZIP_LISTS widget_helper_measures widget_helper_measure_flags)
        widget_helper_condition(TDESKTOP_WIDGET_HELPER_${language}_${measure} widget_helper_${language}_${measure})
        widget_helper_require(widget_helper_${language}_${measure} "${flag} for ${language}")
    endforeach()
endforeach()
widget_helper_condition(TDESKTOP_WIDGET_HELPER_LIBCPP_HARDENING widget_helper_libcpp)
widget_helper_require(widget_helper_libcpp _LIBCPP_HARDENING_MODE)
foreach (arch IN LISTS widget_helper_typed_cxx_archs)
    widget_helper_condition(TDESKTOP_WIDGET_HELPER_TYPED_NEW_DELETE_${arch} widget_helper_typed_cxx_${arch})
    widget_helper_require(widget_helper_typed_cxx_${arch} "-ftyped-cxx-new-delete -ftyped-cxx-delete for ${arch}")
endforeach()
if (widget_helper_unsupported)
    list(JOIN widget_helper_unsupported ", " widget_helper_unsupported)
    if (DESKTOP_APP_SPECIAL_TARGET)
        message(FATAL_ERROR "TelegramWidgetHelper cannot be hardened with this compiler and SDK, unsupported: ${widget_helper_unsupported}.")
    endif()
    message(WARNING "TelegramWidgetHelper is built without unsupported hardening: ${widget_helper_unsupported}.")
endif()

widget_helper_flags(widget_helper_caller_flags CXX_FLAGS OBJCXX_FLAGS)
widget_helper_flags(widget_helper_caller_link_flags EXE_LINKER_FLAGS)

function(widget_helper_switch target languages condition enable disable)
    set(when ${condition})
    set(unless $<NOT:${condition}>)
    if (languages)
        set(when $<AND:$<COMPILE_LANGUAGE:${languages}>,${when}>)
        set(unless $<AND:$<COMPILE_LANGUAGE:${languages}>,${unless}>)
    endif()
    set(options)
    if (NOT condition STREQUAL 0)
        list(APPEND options "$<${when}:${enable}>")
    endif()
    if (disable AND NOT condition STREQUAL 1)
        list(APPEND options "$<${unless}:${disable}>")
    endif()
    if (languages)
        target_compile_options(${target} PRIVATE ${options})
    else()
        target_link_options(${target} PRIVATE ${options})
    endif()
endfunction()

function(widget_helper_harden target)
    foreach (language C CXX)
        set(languages ${widget_helper_${language}_languages})
        widget_helper_switch(${target} ${languages} ${widget_helper_${language}_STACK_PROTECTOR} -fstack-protector-strong "")
        widget_helper_switch(${target} ${languages} ${widget_helper_${language}_ZERO_INIT} -ftrivial-auto-var-init=zero "")
        if (TDESKTOP_WIDGET_HELPER_${language}_TYPED_MALLOC_FLAG)
            widget_helper_switch(${target} ${languages} ${widget_helper_${language}_TYPED_MALLOC} "${widget_helper_typed_malloc}" -fno-typed-memory-operations)
        endif()
    endforeach()
    widget_helper_switch(${target} CXX,OBJCXX ${widget_helper_libcpp}
        "-U_LIBCPP_HARDENING_MODE;-D_LIBCPP_HARDENING_MODE=$<IF:$<CONFIG:Debug>,_LIBCPP_HARDENING_MODE_DEBUG,_LIBCPP_HARDENING_MODE_FAST>" "")

    widget_helper_archs(${target} archs)
    set(typed_cxx)
    foreach (arch IN LISTS archs)
        if (arch IN_LIST widget_helper_typed_cxx_archs)
            list(APPEND typed_cxx ${widget_helper_typed_cxx_${arch}})
        else()
            list(APPEND typed_cxx 0)
        endif()
    endforeach()
    set(typed_cxx_conditions ${typed_cxx})
    list(REMOVE_DUPLICATES typed_cxx_conditions)
    list(LENGTH typed_cxx_conditions typed_cxx_count)
    get_target_property(type ${target} TYPE)
    set(typed_cxx_off)
    if (TDESKTOP_WIDGET_HELPER_TYPED_NEW_DELETE_FLAG)
        set(typed_cxx_off -fno-typed-cxx-new-delete -fno-typed-cxx-delete)
    endif()
    if (typed_cxx_count LESS_EQUAL 1)
        if (NOT typed_cxx_conditions)
            set(typed_cxx_conditions 0)
        endif()
        foreach (languages CXX,OBJCXX "")
            if (languages OR type STREQUAL "EXECUTABLE")
                widget_helper_switch(${target} "${languages}" ${typed_cxx_conditions}
                    "-ftyped-cxx-new-delete;-ftyped-cxx-delete" "${typed_cxx_off}")
            endif()
        endforeach()
    elseif (NOT CMAKE_GENERATOR STREQUAL Xcode)
        foreach (arch condition IN ZIP_LISTS archs typed_cxx)
            # SHELL: keeps each repeated -Xarch_<arch> with its option through CMake's option de-duplication.
            set(enable "SHELL:-Xarch_${arch} -ftyped-cxx-new-delete;SHELL:-Xarch_${arch} -ftyped-cxx-delete")
            set(disable "SHELL:-Xarch_${arch} -fno-typed-cxx-new-delete;SHELL:-Xarch_${arch} -fno-typed-cxx-delete")
            foreach (languages CXX,OBJCXX "")
                if (languages OR type STREQUAL "EXECUTABLE")
                    widget_helper_switch(${target} "${languages}" ${condition} "${enable}" "${disable}")
                endif()
            endforeach()
        endforeach()
    elseif ("${widget_helper_caller_flags} ${widget_helper_caller_link_flags}" MATCHES "typed-cxx")
        message(FATAL_ERROR "TelegramWidgetHelper keeps typed new and delete out of some of its Xcode architectures, which the typed allocation flags given to CMake would override.")
    endif()

    if (CMAKE_GENERATOR STREQUAL Xcode)
        if (widget_helper_caller_flags MATCHES "_LIBCPP_HARDENING_MODE")
            set(libcpp "")
        else()
            set(libcpp $<${widget_helper_libcpp}:$<IF:$<CONFIG:Debug>,debug,fast>>)
        endif()
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CLANG_CXX_STANDARD_LIBRARY_HARDENING "${libcpp}"
            XCODE_ATTRIBUTE_CLANG_ENABLE_STACK_ZERO_INIT
                $<IF:$<AND:${widget_helper_C_ZERO_INIT},${widget_helper_CXX_ZERO_INIT}>,YES,NO>
        )
        if (TDESKTOP_WIDGET_HELPER_C_TYPED_MALLOC_FLAG AND TDESKTOP_WIDGET_HELPER_CXX_TYPED_MALLOC_FLAG)
            set_target_properties(${target} PROPERTIES
                XCODE_ATTRIBUTE_CLANG_ENABLE_C_TYPED_ALLOCATOR_SUPPORT
                    $<IF:$<AND:${widget_helper_C_TYPED_MALLOC},${widget_helper_CXX_TYPED_MALLOC}>,YES,NO>
            )
        endif()
        if (TDESKTOP_WIDGET_HELPER_TYPED_NEW_DELETE_FLAG AND typed_cxx_count EQUAL 1)
            set_target_properties(${target} PROPERTIES
                XCODE_ATTRIBUTE_CLANG_ENABLE_CPLUSPLUS_TYPED_ALLOCATOR_SUPPORT $<IF:${typed_cxx_conditions},YES,NO>
            )
        elseif (TDESKTOP_WIDGET_HELPER_TYPED_NEW_DELETE_FLAG)
            foreach (arch condition IN ZIP_LISTS archs typed_cxx)
                set_target_properties(${target} PROPERTIES
                    "XCODE_ATTRIBUTE_CLANG_ENABLE_CPLUSPLUS_TYPED_ALLOCATOR_SUPPORT[arch=${arch}]" $<IF:${condition},YES,NO>
                )
            endforeach()
        endif()
    endif()
endfunction()

foreach (target IN LISTS widget_helper_hardened_targets)
    widget_helper_harden(${target})
endforeach()
