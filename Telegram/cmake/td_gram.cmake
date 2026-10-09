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
    gram/api/gram_api_account.cpp
    gram/api/gram_api_account.h
    gram/api/gram_api_emulate.cpp
    gram/api/gram_api_emulate.h
    gram/api/gram_api_nft.cpp
    gram/api/gram_api_nft.h
    gram/api/gram_api_rates.cpp
    gram/api/gram_api_rates.h
    gram/api/gram_api_request.cpp
    gram/api/gram_api_request.h
    gram/api/gram_api_stream.cpp
    gram/api/gram_api_stream.h
    gram/gram_boc.cpp
    gram/gram_boc.h
    gram/gram_pch.h
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
    gram/tests/gram_api_tests.cpp
    gram/tests/gram_boc_tests.cpp
    gram/tests/gram_emulate_tests.cpp
    gram/tests/gram_nft_tests.cpp
    gram/tests/gram_rates_tests.cpp
    gram/tests/gram_stream_tests.cpp
    gram/tests/gram_tests.h
    gram/tests/gram_tests_main.cpp
)

target_include_directories(td_gram_test
PRIVATE
    ${src_loc}
)

target_compile_definitions(td_gram_test
PRIVATE
    GRAM_TEST_FIXTURES_PATH="${src_loc}/gram/tests/fixtures"
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
