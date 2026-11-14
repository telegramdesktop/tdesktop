# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_wallet_phrase_shares)
init_non_host_target(test_wallet_phrase_shares "(tests)")

target_precompile_headers(test_wallet_phrase_shares PRIVATE ${src_loc}/tests/test_wallet_phrase_shares_pch.h)
nice_target_sources(test_wallet_phrase_shares ${src_loc}
PRIVATE
    tests/test_wallet_phrase_shares.cpp
    tests/test_wallet_phrase_shares_pch.h
    wallet/wallet_phrase_shares.cpp
    wallet/wallet_phrase_shares.h
)

target_include_directories(test_wallet_phrase_shares
PRIVATE
    ${src_loc}
)

target_link_libraries(test_wallet_phrase_shares
PRIVATE
    tdesktop::td_tde2e
    desktop-app::lib_tl
    desktop-app::lib_base
)

set_target_properties(test_wallet_phrase_shares PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_wallet_phrase_shares
)
