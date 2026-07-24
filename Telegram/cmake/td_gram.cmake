# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_gram OBJECT)
init_non_host_target(td_gram)
add_library(tdesktop::td_gram ALIAS td_gram)

target_precompile_headers(td_gram PRIVATE ${src_loc}/gram/gram_pch.h)
nice_target_sources(td_gram ${src_loc}
PRIVATE
    gram/crypto/gram_ed25519.cpp
    gram/crypto/gram_ed25519.h
    gram/crypto/gram_hmac.cpp
    gram/crypto/gram_hmac.h
    gram/crypto/gram_mnemonic.cpp
    gram/crypto/gram_mnemonic.h
    gram/crypto/gram_slip10.cpp
    gram/crypto/gram_slip10.h
    gram/crypto/gram_wordlist.cpp
    gram/crypto/gram_wordlist.h
    gram/gram_pch.h
    gram/ton/gram_address.cpp
    gram/ton/gram_address.h
    gram/ton/gram_boc.cpp
    gram/ton/gram_boc.h
    gram/ton/gram_cell.cpp
    gram/ton/gram_cell.h
    gram/ton/gram_crc.cpp
    gram/ton/gram_crc.h
    gram/ton/gram_message.cpp
    gram/ton/gram_message.h
    gram/wallet/gram_wallet_v5.cpp
    gram/wallet/gram_wallet_v5.h
    gram/wallet/gram_wallet_v5_code.h
)

target_include_directories(td_gram
PUBLIC
    ${src_loc}
)

target_link_libraries(td_gram
PUBLIC
    desktop-app::lib_base
)

add_executable(td_gram_test)
init_non_host_target(td_gram_test "(gram)")

nice_target_sources(td_gram_test ${src_loc}
PRIVATE
    gram/tests/gram_crypto_tests.cpp
    gram/tests/gram_key_tests.cpp
    gram/tests/gram_mnemonic_tests.cpp
    gram/tests/gram_tests.h
    gram/tests/gram_tests_main.cpp
    gram/tests/gram_ton_tests.cpp
    gram/tests/gram_wallet_tests.cpp
)

target_include_directories(td_gram_test
PRIVATE
    ${src_loc}
)

target_link_libraries(td_gram_test
PRIVATE
    tdesktop::td_gram
    desktop-app::lib_base
)

set_target_properties(td_gram_test PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
