# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

if (NOT LINUX)
    add_library(standalone_options INTERFACE)
    add_library(tdesktop::standalone_options ALIAS standalone_options)

    # common_options without its libraries: no framework reaches code that must start clean.
    foreach (property
        INTERFACE_COMPILE_DEFINITIONS
        INTERFACE_COMPILE_OPTIONS
        INTERFACE_INCLUDE_DIRECTORIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        INTERFACE_LINK_OPTIONS)
        get_property(property_set TARGET common_options PROPERTY ${property} SET)
        if (property_set)
            get_target_property(value common_options ${property})
            set_property(TARGET standalone_options PROPERTY ${property} "${value}")
        endif()
    endforeach()
endif()

function(init_standalone_target target_name)
    set(standard ${MAXIMUM_CXX_STANDARD})
    foreach (entry ${ARGN})
        if (${entry} STREQUAL cxx_std_14 OR ${entry} STREQUAL cxx_std_11 OR ${entry} STREQUAL cxx_std_17)
            set(standard ${entry})
        else()
            init_target_folder(${target_name} ${entry})
        endif()
    endforeach()
    target_compile_features(${target_name} PRIVATE ${standard})
    if (LINUX)
        target_link_libraries(${target_name} PRIVATE desktop-app::common_options)
    else()
        target_link_libraries(${target_name} PRIVATE tdesktop::standalone_options)
    endif()
    set_target_properties(${target_name} PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_WEAK YES
    )
    if (DESKTOP_APP_SPECIAL_TARGET AND DESKTOP_APP_ENABLE_LTO)
        if (CMAKE_GENERATOR STREQUAL Xcode)
            set_target_properties(${target_name} PROPERTIES
                XCODE_ATTRIBUTE_LLVM_LTO $<IF:$<CONFIG:Debug>,NO,YES>
            )
        elseif (APPLE)
            target_compile_options(${target_name} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-flto>)
            target_link_options(${target_name} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-flto>)
        else()
            set_target_properties(${target_name} PROPERTIES
                INTERPROCEDURAL_OPTIMIZATION_RELEASE True
                INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO True
                INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL True
            )
        endif()
    endif()
    get_target_property(target_type ${target_name} TYPE)
    if (target_type STREQUAL "EXECUTABLE")
        if (DESKTOP_APP_MAC_ARCH)
            set_target_properties(${target_name} PROPERTIES
                OSX_ARCHITECTURES "${DESKTOP_APP_MAC_ARCH}"
            )
        endif()
        if (DESKTOP_APP_SPECIAL_TARGET
            AND DESKTOP_APP_ENABLE_LTO
            AND NOT CMAKE_GENERATOR STREQUAL Xcode
            AND DESKTOP_APP_MAC_ARCH MATCHES "^(arm64|x86_64)$")
            target_link_options(${target_name} PRIVATE
                "$<$<NOT:$<CONFIG:Debug>>:LINKER:-object_path_lto,${CMAKE_CURRENT_BINARY_DIR}/${target_name}-$<CONFIG>.lto.o>"
            )
        endif()
    endif()
endfunction()
