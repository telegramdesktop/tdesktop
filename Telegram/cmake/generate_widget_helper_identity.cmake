# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_widget_helper_identity target_name helper_name kind)
    cmake_parse_arguments(PARSE_ARGV 3 arg "" "IDENTIFIER;ENTITLEMENTS" "")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen/widget_helper_identity/$<CONFIG>)
    set(gen_file ${gen_dst}/widget_helper_identity.h)
    set(gen_script ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/widget_helper_identity.py)

    set(identifier_arguments)
    if (DEFINED arg_IDENTIFIER)
        set(identifier_arguments --identifier ${arg_IDENTIFIER})
    endif()

    set(entitlements_arguments)
    if (DEFINED arg_ENTITLEMENTS)
        set(entitlements_arguments --entitlements ${arg_ENTITLEMENTS})
    endif()

    add_custom_command(
    OUTPUT
        ${gen_file}
    COMMAND
        ${Python3_EXECUTABLE}
        ${gen_script}
        --kind
        ${kind}
        ${identifier_arguments}
        ${entitlements_arguments}
        --helper
        $<TARGET_FILE:${helper_name}>
        --output
        ${gen_file}
    COMMENT "Generating widget helper identity (${target_name})"
    DEPENDS
        ${helper_name}
        ${gen_script}
        ${arg_ENTITLEMENTS}
    VERBATIM
    )
    generate_target(${target_name} widget_helper_identity ${gen_file} "" ${gen_dst})
endfunction()
