function(force_include target_name file_path)
    if (MSVC)
        target_compile_options(${target_name}
        PRIVATE
            /FI${file_path}
        )
    else()
        target_compile_options(${target_name}
        PRIVATE
            -include ${file_path}
        )
    endif()
endfunction()
