# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# The focused v2 update verification test is built alongside the Packer
# for special targets as well as with the development test apps: it runs
# the exact translation unit that verifies updates in the client and it
# checks the committed trust files against the pinned root.

add_executable(test_update_verify)
init_target(test_update_verify "(tests)")

target_include_directories(test_update_verify PRIVATE ${src_loc})

nice_target_sources(test_update_verify ${src_loc}
PRIVATE
    core/update_keys.cpp
    core/update_keys.h
    core/update_verify.cpp
    core/update_verify.h
    tests/test_update_verify.cpp
)

target_include_directories(test_update_verify PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
add_dependencies(test_update_verify Telegram_update_keys)

target_link_libraries(test_update_verify
PRIVATE
    desktop-app::external_qt
    desktop-app::external_openssl
)

set_target_properties(test_update_verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_dependencies(Telegram test_update_verify)
