# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(telegram_add_apple_swift_runtime target_name)
    if (NOT APPLE)
        return()
    endif()

    if (CMAKE_Swift_COMPILER)
        set(swift_compiler "${CMAKE_Swift_COMPILER}")
    else()
        execute_process(
            COMMAND xcrun --find swiftc
            OUTPUT_VARIABLE swift_compiler
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    get_filename_component(swift_bin_dir "${swift_compiler}" DIRECTORY)
    set(SWIFT_LIB_DIR "${swift_bin_dir}/../lib/swift/macosx")

    target_link_options(${target_name}
    PRIVATE
        "-L${SWIFT_LIB_DIR}"
        "-Wl,-rpath,/usr/lib/swift"
        "-Wl,-rpath,@executable_path/../Frameworks"
    )

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
