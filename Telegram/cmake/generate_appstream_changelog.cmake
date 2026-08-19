# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_appstream_changelog target_name changelog_path metadata_path)
    find_package(Python3 REQUIRED)

    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(gen_timestamp ${gen_dst}/${target_name}_appstream_changelog.timestamp)
    set(gen_files ${metadata_path})

    add_custom_command(
    OUTPUT
        ${gen_timestamp}
    COMMAND
        ${Python3_EXECUTABLE}
        ${submodules_loc}/build/changelog2appstream.py
        -c "${changelog_path}"
        -m "${metadata_path}"
        -n 10
    COMMAND
        echo 1> ${gen_timestamp}
    COMMENT "Generating AppStream changelog (${target_name})"
    DEPENDS
        ${submodules_loc}/build/changelog2appstream.py
        ${changelog_path}
        ${metadata_path}
    )
    generate_target(${target_name} appstream_changelog ${gen_timestamp} "${gen_files}" ${gen_dst})
endfunction()
