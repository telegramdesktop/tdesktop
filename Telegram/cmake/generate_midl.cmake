# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_midl target_name idl_file)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    get_filename_component(idl_file_name ${idl_file} NAME_WLE)

    set(gen_timestamp ${gen_dst}/midl_${idl_file_name}.timestamp)
    set(gen_files
        ${gen_dst}/${idl_file_name}_i.c
        ${gen_dst}/${idl_file_name}_h.h
    )

    if (build_win64)
        set(env x64)
    else()
        set(env win32)
    endif()
    add_custom_command(
    OUTPUT
        ${gen_timestamp}
    BYPRODUCTS
        ${gen_files}
    COMMAND
        midl
        /out ${gen_dst}
        /h ${idl_file_name}_h.h
        /env ${env}
        /notlb
        ${idl_file}
    COMMAND
        echo 1> ${gen_timestamp}
    COMMENT "Generating header from IDL (${target_name})"
    DEPENDS
        ${idl_file}
    )
    generate_target(${target_name} midl ${gen_timestamp} "${gen_files}" ${gen_dst})
endfunction()
