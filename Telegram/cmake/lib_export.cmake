add_library(lib_export OBJECT)
init_target(lib_export)

set(lib_export_sources
PRIVATE
    export/export_api_wrap.cpp
    export/export_api_wrap.h
    export/export_controller.cpp
    export/export_controller.h
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
    export/output/export_output_text.cpp
    export/output/export_output_text.h
)
nice_target_sources(lib_export ${src_loc} "${lib_export_sources}")

target_precompile_headers(lib_export PRIVATE ${src_loc}/export/export_pch.h)

target_include_directories(lib_export
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_export
PUBLIC
    lib_base
    lib_scheme
)
