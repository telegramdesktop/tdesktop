# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_text WIN32)
init_target(test_text "(tests)")

target_include_directories(test_text PRIVATE ${src_loc})

nice_target_sources(test_text ${src_loc}
PRIVATE
    tests/test_main.cpp
    tests/test_main.h
    tests/test_text.cpp
)

nice_target_sources(test_text ${res_loc}
PRIVATE
    qrc/emoji_1.qrc
    qrc/emoji_2.qrc
    qrc/emoji_3.qrc
    qrc/emoji_4.qrc
    qrc/emoji_5.qrc
    qrc/emoji_6.qrc
    qrc/emoji_7.qrc
    qrc/emoji_8.qrc
)

target_link_libraries(test_text
PRIVATE
    desktop-app::lib_base
    desktop-app::lib_crl
    desktop-app::lib_ui
    desktop-app::external_qt
    desktop-app::external_qt_static_plugins
)

set_target_properties(test_text PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_dependencies(Telegram test_text)

target_prepare_qrc(test_text)

add_executable(test_export_output_topics)
init_target(test_export_output_topics "(tests)")
target_include_directories(test_export_output_topics PRIVATE ${src_loc})

target_precompile_headers(test_export_output_topics PRIVATE ${src_loc}/export/export_pch.h <QtCore/QRect>)
target_include_directories(test_export_output_topics PRIVATE ${CMAKE_SOURCE_DIR}/Telegram/lib_ui)
nice_target_sources(test_export_output_topics ${src_loc}
PRIVATE
    tests/test_export_output_topics.cpp
    data/data_birthday.cpp
    export/data/export_data_types.cpp
    export/output/export_output_abstract.cpp
    export/output/export_output_file.cpp
    export/output/export_output_html.cpp
    export/output/export_output_html_and_json.cpp
    export/output/export_output_json.cpp
    export/output/export_output_stats.cpp
    ui/grouped_layout_geometry.cpp
    ui/grouped_layout.cpp
    lang/lang_tag.cpp
    ui/text/format_values.cpp
    countries/countries_instance.cpp
    core/utils.cpp
)
nice_target_sources(test_export_output_topics ${res_loc}
PRIVATE
    qrc/telegram/export.qrc
)
target_include_directories(test_export_output_topics PRIVATE
    ${CMAKE_SOURCE_DIR}/Telegram/ThirdParty/range-v3/include)
target_link_libraries(test_export_output_topics
PRIVATE
    desktop-app::lib_base
    tdesktop::td_scheme
    desktop-app::external_qt
    desktop-app::external_openssl
    desktop-app::external_ffmpeg
)
if (MSVC)
    target_compile_options(test_export_output_topics PRIVATE /Gy)
    target_link_options(test_export_output_topics PRIVATE /OPT:REF)
elseif (APPLE)
    target_compile_options(test_export_output_topics PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(test_export_output_topics PRIVATE -Wl,-dead_strip)
else()
    target_compile_options(test_export_output_topics PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(test_export_output_topics PRIVATE -Wl,--gc-sections)
endif()
target_prepare_qrc(test_export_output_topics)

if (APPLE)
    add_custom_command(TARGET test_text POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:test_text>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/test_text.rcc"
            "${CMAKE_BINARY_DIR}/lib_ui.rcc"
            "$<TARGET_FILE_DIR:test_text>/Contents/Resources/"
    )
endif()
