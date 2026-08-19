# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(telegram_add_apple_swift_runtime target_name)
    if (NOT APPLE)
        return()
    endif()

    execute_process(
        COMMAND xcode-select -p
        OUTPUT_VARIABLE DEVELOPER_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(SWIFT_LIB_DIR
        "${DEVELOPER_DIR}/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx")

    target_link_options(${target_name}
    PRIVATE
        "-L${SWIFT_LIB_DIR}"
        "-Wl,-rpath,/usr/lib/swift"
        "-Wl,-rpath,@executable_path/../Frameworks"
    )

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND mkdir -p $<TARGET_FILE_DIR:${target_name}>/../Frameworks
        COMMAND xcrun swift-stdlib-tool
            --copy
            --platform macosx
            --scan-executable $<TARGET_FILE:${target_name}>
            --destination $<TARGET_FILE_DIR:${target_name}>/../Frameworks
        VERBATIM
    )
endfunction()