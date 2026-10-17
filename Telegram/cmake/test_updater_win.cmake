# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_updater_win)
init_target(test_updater_win "(tests)")

get_filename_component(updater_base_loc lib_base REALPATH)
target_include_directories(test_updater_win PRIVATE
    ${src_loc}
    ${updater_base_loc}
)

nice_target_sources(test_updater_win ${src_loc}
PRIVATE
    _other/updater.h
    _other/updater_win.cpp
    tests/test_updater_win.cpp
)
nice_target_sources(test_updater_win ${updater_base_loc}
PRIVATE
    base/platform/win/base_windows_safe_library.cpp
    base/platform/win/base_windows_safe_library.h
)

if (MINGW)
    target_link_options(test_updater_win PRIVATE -municode)
endif()

set_target_properties(test_updater_win PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
