# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_lang target_name lang_file src_loc)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(gen_timestamp ${gen_dst}/lang_auto.timestamp)
    set(gen_keys ${gen_dst}/lang_auto_keys.h)
    set(gen_files
        ${gen_dst}/lang_auto.cpp
        ${gen_dst}/lang_auto.h
        ${gen_dst}/lang_auto_counts.h
        ${gen_keys}
    )

    add_custom_command(
    OUTPUT
        ${gen_timestamp}
    BYPRODUCTS
        ${gen_files}
    COMMAND
        codegen_lang
        -o${gen_dst}
        ${lang_file}
    COMMENT "Generating lang (${target_name})"
    DEPENDS
        codegen_lang
        ${lang_file}
    )
    generate_target(${target_name} lang ${gen_timestamp} "${gen_files}" ${gen_dst})

    if (NOT CMAKE_GENERATOR MATCHES "Visual Studio|Xcode")
        file(GLOB_RECURSE lang_sources CONFIGURE_DEPENDS
            ${src_loc}/*.cpp
            ${src_loc}/*.h
            ${src_loc}/*.mm
        )

        set(subset_headers "")
        foreach (entry ${lang_sources})
            if (entry MATCHES "\\.(cpp|mm)$")
                file(RELATIVE_PATH relative ${src_loc} ${entry})
                list(APPEND subset_headers ${gen_dst}/lang_subsets/${relative}.h)
                set_property(SOURCE ${entry} APPEND PROPERTY COMPILE_DEFINITIONS
                    "LANG_KEYS_SUBSET=\"lang_subsets/${relative}.h\"")
            endif()
        endforeach()

        set(subsets_timestamp ${gen_dst}/lang_subsets.timestamp)
        add_custom_command(
        OUTPUT
            ${subsets_timestamp}
            ${subset_headers}
        COMMAND
            codegen_lang
            --subsets-only
            -o${gen_dst}
            -s${src_loc}
            ${lang_file}
        COMMAND
            ${CMAKE_COMMAND} -E touch ${subsets_timestamp}
        COMMENT "Generating lang subsets (${target_name})"
        DEPENDS
            codegen_lang
            ${gen_keys}
            ${lang_sources}
        )
        add_custom_target(${target_name}_lang_subsets DEPENDS ${subsets_timestamp})
        init_target_folder(${target_name}_lang_subsets "(gen)")
        add_dependencies(${target_name}_lang_subsets ${target_name}_lang)
        add_dependencies(${target_name} ${target_name}_lang_subsets)
    endif()
endfunction()
