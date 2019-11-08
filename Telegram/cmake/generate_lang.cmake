function(generate_lang target_name lang_file)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    set(generated_files
        ${gen_dst}/lang_auto.cpp
        ${gen_dst}/lang_auto.h
    )
    add_custom_command(
    OUTPUT
        ${generated_files}
    COMMAND
        codegen_lang
        -o${gen_dst}
        ${lang_file}
    COMMENT "Generating lang (${target_name})"
    DEPENDS
        codegen_lang
        ${lang_file}
    )
    generate_target(${target_name} lang "${generated_files}" ${gen_dst})
endfunction()
