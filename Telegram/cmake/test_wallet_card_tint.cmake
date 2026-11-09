# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_wallet_card_tint)
init_non_host_target(test_wallet_card_tint "(tests)")

target_precompile_headers(test_wallet_card_tint PRIVATE ${src_loc}/tests/test_wallet_card_tint_pch.h)
nice_target_sources(test_wallet_card_tint ${src_loc}
PRIVATE
    tests/test_wallet_card_tint.cpp
    tests/test_wallet_card_tint_pch.h
    wallet/wallet_card_tint.cpp
    wallet/wallet_card_tint.h
)

target_include_directories(test_wallet_card_tint
PRIVATE
    ${src_loc}
)

target_link_libraries(test_wallet_card_tint
PRIVATE
    desktop-app::lib_base
)

set_target_properties(test_wallet_card_tint PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_wallet_card_tint
)
