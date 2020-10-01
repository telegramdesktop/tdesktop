# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_export OBJECT)
init_target(td_export)
add_library(tdesktop::td_export ALIAS td_export)

target_precompile_headers(td_export PRIVATE ${src_loc}/export/export_pch.h)
nice_target_sources(td_export ${src_loc}
PRIVATE
    export/export_api_wrap.cpp
    export/export_api_wrap.h
    export/export_controller.cpp
    export/export_controller.h
    export/export_pch.h
    export/export_settings.cpp
    export/export_settings.h
    export/data/export_data_types.cpp
    export/data/export_data_types.h
    export/output/export_output_abstract.cpp
    export/output/export_output_abstract.h
    export/output/export_output_file.cpp
    export/output/export_output_file.h
    export/output/export_output_html.cpp
    export/output/export_output_html.h
    export/output/export_output_json.cpp
    export/output/export_output_json.h
    export/output/export_output_result.h
    export/output/export_output_stats.cpp
    export/output/export_output_stats.h
)

target_include_directories(td_export
PUBLIC
    ${src_loc}
)

target_link_libraries(td_export
PUBLIC
    desktop-app::lib_base
    tdesktop::td_scheme
)
