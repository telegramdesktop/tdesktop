function(init_target target_name) # init_target(my_target folder_name)
    set(folder_name "${ARGV1}")
    set_property(TARGET ${target_name} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if (NOT "${folder_name}" STREQUAL "")
        set_target_properties(${target_name} PROPERTIES FOLDER ${folder_name})
    endif()
endfunction()

