# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

if ((APPLE OR WIN32) AND NOT DESKTOP_APP_USE_PACKAGED
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
        tests/test_iv_editor_state.cpp
    )

    set(test_iv_editor_state_code_sources
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
        tests/test_iv_editor_state_pch.h
        ui/image/image_location.h
    )
    set(test_iv_editor_state_code_libraries
        tdesktop::td_lang
        tdesktop::td_ui
        desktop-app::lib_spellcheck
        desktop-app::lib_storage
        desktop-app::lib_webview
    )

    # link.exe resolves every object it is given, unlike ld64 with -dead_strip.
    if (WIN32)
        add_library(test_iv_editor_state_iv STATIC)
        init_non_host_target(test_iv_editor_state_iv "(tests)")

        target_link_libraries(test_iv_editor_state_iv
        PUBLIC
            tdesktop::td_iv
        )

        add_library(test_iv_editor_state_code STATIC)
        init_non_host_target(test_iv_editor_state_code "(tests)")

        target_precompile_headers(test_iv_editor_state_code
        PRIVATE
            ${src_loc}/iv/iv_pch.h
            ${src_loc}/tests/test_iv_editor_state_pch.h
        )
        nice_target_sources(test_iv_editor_state_code ${src_loc}
        PRIVATE
            ${test_iv_editor_state_code_sources}
        )
        target_link_libraries(test_iv_editor_state_code
        PUBLIC
            ${test_iv_editor_state_code_libraries}
            tdesktop::td_scheme
            desktop-app::lib_tl
            ${libs_loc}/dav1d/builddir-$<IF:$<CONFIG:Debug>,debug,release>/src/libdav1d.a
        )
        set_target_properties(test_iv_editor_state_iv test_iv_editor_state_code
        PROPERTIES
            EXCLUDE_FROM_ALL TRUE
            EXCLUDE_FROM_DEFAULT_BUILD TRUE
        )

        nice_target_sources(test_iv_editor_state ${src_loc}
        PRIVATE
            core/credits_amount.h
            core/file_utilities.h
            data/stickers/data_custom_emoji.h
            data/data_chat_participant_status.h
            data/data_document.h
            data/data_peer.h
            data/data_peer_id.h
            data/data_photo.h
            data/data_premium_limits.h
            data/data_session.h
            history/history_item_components.h
            main/main_app_config.h
            main/main_session.h
            platform/platform_specific.h
            tests/test_iv_editor_state_app_symbols.cpp
        )
        target_link_libraries(test_iv_editor_state
        PRIVATE
            test_iv_editor_state_iv
            test_iv_editor_state_code
        )
        target_link_options(test_iv_editor_state PRIVATE /OPT:REF)
    else()
        nice_target_sources(test_iv_editor_state ${src_loc}
        PRIVATE
            ${test_iv_editor_state_code_sources}
        )
        target_link_libraries(test_iv_editor_state
        PRIVATE
            tdesktop::td_iv
            ${test_iv_editor_state_code_libraries}
        )
        target_link_options(test_iv_editor_state PRIVATE -Wl,-dead_strip)
    endif()

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
        $<TARGET_OBJECTS:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin_init>
        ${test_iv_editor_state_offscreen_archive}
    )

    set_target_properties(test_iv_editor_state PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_iv_editor_state
    )

    if (APPLE)
        add_custom_target(test_iv_editor_state_resources
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:test_iv_editor_state>/Contents/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/lib_ui.rcc"
                "${CMAKE_BINARY_DIR}/external_microtex_bundled.rcc"
                "$<TARGET_FILE_DIR:test_iv_editor_state>/Contents/Resources/"
            VERBATIM
        )
        add_dependencies(test_iv_editor_state_resources
            lib_ui
            external_microtex_bundled
        )
        set_target_properties(test_iv_editor_state_resources PROPERTIES
            EXCLUDE_FROM_ALL TRUE
            EXCLUDE_FROM_DEFAULT_BUILD TRUE
        )
        add_dependencies(test_iv_editor_state test_iv_editor_state_resources)
    endif()
endif()
