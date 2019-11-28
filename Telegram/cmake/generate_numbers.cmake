# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_numbers target_name numbers_file)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(generated_files
        ${gen_dst}/numbers.cpp
        ${gen_dst}/numbers.h
        ${gen_dst}/numbers.timestamp
    )
    add_custom_command(
    OUTPUT
        ${gen_dst}/numbers.timestamp
    BYPRODUCTS
        ${gen_dst}/numbers.cpp
        ${gen_dst}/numbers.h
    COMMAND
        codegen_numbers
        -o${gen_dst}
        ${numbers_file}
    COMMENT "Generating numbers (${target_name})"
    DEPENDS
        codegen_numbers
        ${numbers_file}
    )
    generate_target(${target_name} numbers "${generated_files}" ${gen_dst})
endfunction()
