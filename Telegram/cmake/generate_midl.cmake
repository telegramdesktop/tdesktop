# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_midl target_name src_loc)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    if (build_winarm)
        set(env arm64)
    elseif (build_win64)
        set(env x64)
    else()
        set(env win32)
    endif()

    set(gen_timestamp ${gen_dst}/${target_name}_midl.timestamp)
    set(gen_files "")
    set(full_generation_sources "")
    set(full_dependencies_list "")
    foreach (file ${ARGN})
        list(APPEND full_generation_sources ${src_loc}/${file})
        get_filename_component(file_name ${file} NAME_WLE)
        list(APPEND gen_files
            ${gen_dst}/${file_name}_i.c
            ${gen_dst}/${file_name}_h.h
        )
        list(APPEND gen_commands
        COMMAND
            midl
            /out ${gen_dst}
            /h ${file_name}_h.h
            /env ${env}
            /notlb
            ${src_loc}/${file}
        )
    endforeach()

    add_custom_command(
    OUTPUT
        ${gen_timestamp}
    BYPRODUCTS
        ${gen_files}
    ${gen_commands}
    COMMAND
        echo 1> ${gen_timestamp}
    COMMENT "Generating headers from IDLs (${target_name})"
    DEPENDS
        ${full_generation_sources}
    )
    generate_target(${target_name} midl ${gen_timestamp} "${gen_files}" ${gen_dst})
endfunction()
