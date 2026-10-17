# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_update_unpack)
init_target(test_update_unpack "(tests)")

target_include_directories(test_update_unpack PRIVATE ${src_loc})

if (DESKTOP_APP_USE_PACKAGED)
    target_compile_definitions(test_update_unpack PRIVATE TDESKTOP_USE_PACKAGED)
endif()

nice_target_sources(test_update_unpack ${src_loc}
PRIVATE
    core/update_unpack.cpp
    core/update_unpack.h
    tests/test_update_unpack.cpp
)

target_link_libraries(test_update_unpack
PRIVATE
    desktop-app::external_qt
    desktop-app::external_auto_updates
)

set_target_properties(test_update_unpack PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
