function(init_target_folder target_name folder_name)
    if (NOT "${folder_name}" STREQUAL "")
        set_target_properties(${target_name} PROPERTIES FOLDER ${folder_name})
    endif()
endfunction()

function(init_target_no_ranges target_name) # init_target(my_target folder_name)
    init_target_folder(${target_name} "${ARGV1}")
    set_property(TARGET ${target_name} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(${target_name} PUBLIC common_no_ranges)
endfunction()

function(init_target target_name) # init_target(my_target folder_name)
    init_target_folder(${target_name} "${ARGV1}")
    init_target_no_ranges(${target_name})
    target_link_libraries(${target_name} PUBLIC common)
endfunction()

