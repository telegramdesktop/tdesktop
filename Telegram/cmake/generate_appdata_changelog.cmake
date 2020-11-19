# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_appdata_changelog target_name changelog_path appdata_path)
    find_package(Python3 REQUIRED)

    add_custom_target(${target_name}_appdata_changelog
        ${Python3_EXECUTABLE}
        ${submodules_loc}/build/changelog2appdata.py
        -c "${changelog_path}"
        -a "${appdata_path}"
        -n 10
    COMMENT "Generating AppData changelog (${target_name})"
    DEPENDS
        ${submodules_loc}/build/changelog2appdata.py
        ${changelog_path}
        ${appdata_path}
    VERBATIM
    )
    add_dependencies(${target_name} ${target_name}_appdata_changelog)
endfunction()
