# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

if (APPLE AND NOT DESKTOP_APP_USE_PACKAGED
    AND TARGET Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin
    AND TARGET Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin_init)
    add_executable(test_iv_editor_state)
    init_non_host_target(test_iv_editor_state "(tests)")

    target_precompile_headers(test_iv_editor_state
    PRIVATE
        ${src_loc}/iv/iv_pch.h
        ${src_loc}/tests/test_iv_editor_state_pch.h
    )
    nice_target_sources(test_iv_editor_state ${src_loc}
    PRIVATE
        data/data_location.cpp
        data/data_location.h
        data/data_types.h
        history/history_item_reply_markup.cpp
        history/history_item_reply_markup.h
        history/history_location_manager.cpp
        history/history_location_manager.h
        iv/iv_rich_page.cpp
        iv/iv_rich_page.h
        iv/iv_rich_page_quote.cpp
        iv/iv_rich_page_quote.h
        settings.h
        tests/test_iv_editor_state.cpp
        tests/test_iv_editor_state_pch.h
        ui/image/image_location.h
    )

    if (DESKTOP_APP_RELEASE_QT)
        get_target_property(test_iv_editor_state_offscreen_archive
            Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin
            IMPORTED_LOCATION_RELWITHDEBINFO)
    else()
        set(test_iv_editor_state_offscreen_archive
            $<TARGET_FILE:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin>)
    endif()

    target_link_libraries(test_iv_editor_state
    PRIVATE
        tdesktop::td_iv
        tdesktop::td_lang
        tdesktop::td_ui
        desktop-app::lib_spellcheck
        desktop-app::lib_storage
        desktop-app::lib_webview
        $<TARGET_OBJECTS:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin_init>
        ${test_iv_editor_state_offscreen_archive}
    )

    target_link_options(test_iv_editor_state PRIVATE -Wl,-dead_strip)

    set_target_properties(test_iv_editor_state PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_iv_editor_state
    )

    add_custom_command(TARGET test_iv_editor_state POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:test_iv_editor_state>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/lib_ui.rcc"
            "$<TARGET_FILE_DIR:test_iv_editor_state>/Contents/Resources/"
        VERBATIM
    )
endif()
