# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_scheme target_name script scheme_files)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(generated_files
        ${gen_dst}/scheme.cpp
        ${gen_dst}/scheme.h
        ${gen_dst}/scheme.timestamp
    )
    add_custom_command(
    OUTPUT
        ${gen_dst}/scheme.timestamp
    BYPRODUCTS
        ${gen_dst}/scheme.cpp
        ${gen_dst}/scheme.h
    COMMAND
        python
        ${script}
        -o${gen_dst}/scheme
        ${scheme_files}
    COMMENT "Generating scheme (${target_name})"
    DEPENDS
        ${script}
        ${submodules_loc}/lib_tl/tl/generate_tl.py
        ${scheme_files}
    )
    generate_target(${target_name} scheme "${generated_files}" ${gen_dst})
endfunction()
