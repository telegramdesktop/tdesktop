# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_export_formats)
init_non_host_target(test_export_formats "(tests)")

target_precompile_headers(test_export_formats PRIVATE ${src_loc}/stdafx.h)
nice_target_sources(test_export_formats ${src_loc}
PRIVATE
    core/credits_amount.cpp
    core/credits_amount.h
    core/mime_type.cpp
    core/mime_type.h
    core/utils.cpp
    core/utils.h
    countries/countries_instance.cpp
    countries/countries_instance.h
    data/data_birthday.cpp
    data/data_birthday.h
    data/data_location.cpp
    data/data_location.h
    data/data_peer_id.cpp
    data/data_peer_id.h
    export/data/export_data_types.cpp
    export/data/export_data_types.h
    export/output/export_output_abstract.cpp
    export/output/export_output_abstract.h
    export/output/export_output_file.cpp
    export/output/export_output_file.h
    export/output/export_output_html.cpp
    export/output/export_output_html.h
    export/output/export_output_html_and_json.cpp
    export/output/export_output_html_and_json.h
    export/output/export_output_html_gift_offer.cpp
    export/output/export_output_json.cpp
    export/output/export_output_json.h
    export/output/export_output_result.h
    export/output/export_output_stats.cpp
    export/output/export_output_stats.h
    export/export_settings.h
    mtproto/details/mtproto_dump_to_text.cpp
    mtproto/details/mtproto_dump_to_text.h
    platform/platform_specific.h
    tests/test_export_formats.cc
    tests/test_export_formats.hpp
    tests/test_export_formats_actions.cc
    tests/test_export_formats_rich.cc
    ui/text/format_values.cpp
    ui/text/format_values.h
    ui/grouped_layout.cpp
    ui/grouped_layout.h
    ui/grouped_layout_geometry.cpp
    ui/grouped_layout_geometry.h
)

nice_target_sources(test_export_formats ${res_loc}
PRIVATE
    qrc/telegram/export.qrc
)

target_include_directories(test_export_formats
PRIVATE
    ${src_loc}
)

target_include_directories(test_export_formats SYSTEM
PRIVATE
    $<TARGET_PROPERTY:external_ffmpeg,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_definitions(test_export_formats
PRIVATE
    TDESKTOP_API_ID=0
    TDESKTOP_API_HASH=0
)

target_link_libraries(test_export_formats
PRIVATE
    tdesktop::td_lang
    tdesktop::td_scheme
    desktop-app::lib_ui
    desktop-app::lib_tl
    desktop-app::lib_base
    desktop-app::external_kcoreaddons
    desktop-app::external_zlib
)

set_target_properties(test_export_formats PROPERTIES
    AUTORCC ON
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_export_formats
)
