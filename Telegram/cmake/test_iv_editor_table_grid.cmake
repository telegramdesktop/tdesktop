# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_iv_editor_table_grid)
init_non_host_target(test_iv_editor_table_grid "(tests)")

target_precompile_headers(test_iv_editor_table_grid PRIVATE ${src_loc}/iv/iv_pch.h)
nice_target_sources(test_iv_editor_table_grid ${src_loc}
PRIVATE
    iv/editor/iv_editor_page_table_grid.cpp
    iv/editor/iv_editor_page_table_grid.h
    tests/test_iv_editor_table_grid.cpp
)

target_include_directories(test_iv_editor_table_grid
PRIVATE
    ${src_loc}
    ${CMAKE_CURRENT_BINARY_DIR}/gen
)

add_dependencies(test_iv_editor_table_grid td_scheme_scheme)

target_link_libraries(test_iv_editor_table_grid
PRIVATE
    desktop-app::lib_webview
    desktop-app::lib_tl
)

set_target_properties(test_iv_editor_table_grid PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
