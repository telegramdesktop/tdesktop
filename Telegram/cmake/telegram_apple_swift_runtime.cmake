# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(telegram_add_apple_swift_runtime target_name)
    if (NOT APPLE)
        return()
    endif()

    set(swift_compiler "")
    if (CMAKE_Swift_COMPILER)
        set(swift_compiler "${CMAKE_Swift_COMPILER}")
    endif()
    get_filename_component(swift_bin_dir "${swift_compiler}" DIRECTORY)
    if (NOT swift_compiler OR NOT EXISTS "${swift_bin_dir}/swift-stdlib-tool")
        execute_process(
            COMMAND xcrun --find swiftc
            OUTPUT_VARIABLE swift_compiler
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    get_filename_component(swift_bin_dir "${swift_compiler}" DIRECTORY)

    execute_process(
        COMMAND "${swift_compiler}" -print-target-info
        OUTPUT_VARIABLE swift_target_info
        COMMAND_ERROR_IS_FATAL ANY
    )
    string(JSON swift_runtime_paths GET "${swift_target_info}" paths runtimeLibraryPaths)
    string(JSON swift_runtime_paths_length LENGTH "${swift_runtime_paths}")

    target_link_options(${target_name}
    PRIVATE
        "-Wl,-rpath,/usr/lib/swift"
        "-Wl,-rpath,@executable_path/../Frameworks"
    )
    if (swift_runtime_paths_length GREATER 0)
        math(EXPR swift_runtime_paths_last "${swift_runtime_paths_length} - 1")
        foreach (index RANGE ${swift_runtime_paths_last})
            string(JSON swift_runtime_path GET "${swift_runtime_paths}" ${index})
            target_link_options(${target_name} PRIVATE "-L${swift_runtime_path}")
        endforeach()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND mkdir -p $<TARGET_FILE_DIR:${target_name}>/../Frameworks
        COMMAND "${swift_bin_dir}/swift-stdlib-tool"
            --copy
            --platform macosx
            --scan-executable $<TARGET_FILE:${target_name}>
            --destination $<TARGET_FILE_DIR:${target_name}>/../Frameworks
        VERBATIM
    )
endfunction()
