# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(Telegram WIN32 MACOSX_BUNDLE)
init_target(Telegram)

add_subdirectory(lib_rpl)
add_subdirectory(lib_crl)
add_subdirectory(lib_base)
add_subdirectory(lib_ui)
add_subdirectory(lib_tl)
if (NOT DESKTOP_APP_DISABLE_SPELLCHECK)
    add_subdirectory(lib_spellcheck)
endif()
add_subdirectory(lib_storage)
add_subdirectory(lib_lottie)
add_subdirectory(lib_qr)
add_subdirectory(lib_webrtc)
add_subdirectory(lib_webview)
add_subdirectory(codegen)

get_filename_component(src_loc SourceFiles REALPATH)
get_filename_component(res_loc Resources REALPATH)

include(cmake/telegram_options.cmake)
include(cmake/lib_ffmpeg.cmake)
include(cmake/lib_stripe.cmake)
include(cmake/lib_tgvoip.cmake)
include(cmake/lib_tgcalls.cmake)
include(cmake/td_export.cmake)
include(cmake/td_mtproto.cmake)
include(cmake/td_lang.cmake)
include(cmake/td_scheme.cmake)
include(cmake/td_ui.cmake)
include(cmake/generate_appdata_changelog.cmake)

set_target_properties(Telegram PROPERTIES AUTOMOC ON AUTORCC ON)

target_link_libraries(Telegram
PRIVATE
    tdesktop::lib_tgcalls_legacy
    tdesktop::lib_tgcalls
    tdesktop::lib_tgvoip
    tdesktop::td_export
    tdesktop::td_mtproto
    tdesktop::td_lang
    tdesktop::td_scheme
    tdesktop::td_ui
    desktop-app::lib_webrtc
    desktop-app::lib_base
    desktop-app::lib_crl
    desktop-app::lib_ui
    desktop-app::lib_tl
    desktop-app::lib_storage
    desktop-app::lib_lottie
    desktop-app::lib_qr
    desktop-app::lib_webview
    desktop-app::lib_ffmpeg
    desktop-app::lib_stripe
    desktop-app::external_lz4
    desktop-app::external_rlottie
    desktop-app::external_zlib
    desktop-app::external_minizip
    desktop-app::external_qt_static_plugins
    desktop-app::external_qt
    desktop-app::external_qr_code_generator
    desktop-app::external_crash_reports
    desktop-app::external_auto_updates
    desktop-app::external_openssl
    desktop-app::external_openal
    desktop-app::external_xxhash
)

if (WIN32)
    target_link_libraries(Telegram
    PRIVATE
        desktop-app::lib_webview_winrt
    )
elseif (LINUX)
    target_link_libraries(Telegram
    PRIVATE
        desktop-app::external_glibmm
        desktop-app::external_glib
    )

    if (NOT DESKTOP_APP_DISABLE_DBUS_INTEGRATION)
        target_link_libraries(Telegram
        PRIVATE
            desktop-app::external_statusnotifieritem
            desktop-app::external_dbusmenu_qt
        )
    endif()

    if (NOT DESKTOP_APP_DISABLE_X11_INTEGRATION)
        target_link_libraries(Telegram
        PRIVATE
            desktop-app::external_xcb
        )
    endif()

    if (NOT DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION)
        target_link_libraries(Telegram
        PRIVATE
            desktop-app::external_kwayland
        )
    endif()

    if (NOT DESKTOP_APP_DISABLE_GTK_INTEGRATION)
        find_package(PkgConfig REQUIRED)

        if (DESKTOP_APP_USE_PACKAGED AND NOT DESKTOP_APP_USE_PACKAGED_LAZY)
            pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
            target_link_libraries(Telegram PRIVATE PkgConfig::GTK3)

            if (NOT DESKTOP_APP_DISABLE_X11_INTEGRATION)
                pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
                target_link_libraries(Telegram PRIVATE PkgConfig::X11)
            endif()
        else()
            pkg_search_module(GTK REQUIRED gtk+-3.0 gtk+-2.0)
            target_include_directories(Telegram PRIVATE ${GTK_INCLUDE_DIRS})

            if (NOT DESKTOP_APP_DISABLE_X11_INTEGRATION)
                target_link_libraries(Telegram PRIVATE X11)
            endif()
        endif()
    endif()
endif()

target_precompile_headers(Telegram PRIVATE ${src_loc}/stdafx.h)
nice_target_sources(Telegram ${src_loc}
PRIVATE
    ${style_files}

    api/api_attached_stickers.cpp
    api/api_attached_stickers.h
    api/api_authorizations.cpp
    api/api_authorizations.h
    api/api_bot.cpp
    api/api_bot.h
    api/api_chat_filters.cpp
    api/api_chat_filters.h
    api/api_chat_invite.cpp
    api/api_chat_invite.h
    api/api_common.h
    api/api_editing.cpp
    api/api_editing.h
    api/api_global_privacy.cpp
    api/api_global_privacy.h
    api/api_hash.cpp
    api/api_hash.h
    api/api_invite_links.cpp
    api/api_invite_links.h
    api/api_media.cpp
    api/api_media.h
    api/api_self_destruct.cpp
    api/api_self_destruct.h
    api/api_send_progress.cpp
    api/api_send_progress.h
    api/api_sending.cpp
    api/api_sending.h
    api/api_sensitive_content.cpp
    api/api_sensitive_content.h
    api/api_single_message_search.cpp
    api/api_single_message_search.h
    api/api_text_entities.cpp
    api/api_text_entities.h
    api/api_toggling_media.cpp
    api/api_toggling_media.h
    api/api_updates.cpp
    api/api_updates.h
    boxes/filters/edit_filter_box.cpp
    boxes/filters/edit_filter_box.h
    boxes/filters/edit_filter_chats_list.cpp
    boxes/filters/edit_filter_chats_list.h
    boxes/peers/add_participants_box.cpp
    boxes/peers/add_participants_box.h
    boxes/peers/edit_contact_box.cpp
    boxes/peers/edit_contact_box.h
    boxes/peers/edit_linked_chat_box.cpp
    boxes/peers/edit_linked_chat_box.h
    boxes/peers/edit_participant_box.cpp
    boxes/peers/edit_participant_box.h
    boxes/peers/edit_participants_box.cpp
    boxes/peers/edit_participants_box.h
    boxes/peers/edit_peer_info_box.cpp
    boxes/peers/edit_peer_info_box.h
    boxes/peers/edit_peer_invite_link.cpp
    boxes/peers/edit_peer_invite_link.h
    boxes/peers/edit_peer_invite_links.cpp
    boxes/peers/edit_peer_invite_links.h
    boxes/peers/edit_peer_type_box.cpp
    boxes/peers/edit_peer_type_box.h
    boxes/peers/edit_peer_history_visibility_box.cpp
    boxes/peers/edit_peer_history_visibility_box.h
    boxes/peers/edit_peer_permissions_box.cpp
    boxes/peers/edit_peer_permissions_box.h
    boxes/about_box.cpp
    boxes/about_box.h
    boxes/abstract_box.cpp
    boxes/abstract_box.h
    boxes/add_contact_box.cpp
    boxes/add_contact_box.h
    boxes/auto_lock_box.cpp
    boxes/auto_lock_box.h
    boxes/auto_download_box.cpp
    boxes/auto_download_box.h
    boxes/background_box.cpp
    boxes/background_box.h
    boxes/background_preview_box.cpp
    boxes/background_preview_box.h
    boxes/change_phone_box.cpp
    boxes/change_phone_box.h
    boxes/confirm_box.cpp
    boxes/confirm_box.h
    boxes/confirm_phone_box.cpp
    boxes/confirm_phone_box.h
    boxes/connection_box.cpp
    boxes/connection_box.h
    boxes/create_poll_box.cpp
    boxes/create_poll_box.h
    boxes/dictionaries_manager.cpp
    boxes/dictionaries_manager.h
    boxes/download_path_box.cpp
    boxes/download_path_box.h
    boxes/edit_caption_box.cpp
    boxes/edit_caption_box.h
    boxes/edit_color_box.cpp
    boxes/edit_color_box.h
    boxes/edit_privacy_box.cpp
    boxes/edit_privacy_box.h
    boxes/language_box.cpp
    boxes/language_box.h
    boxes/local_storage_box.cpp
    boxes/local_storage_box.h
    boxes/mute_settings_box.cpp
    boxes/mute_settings_box.h
    boxes/peer_list_box.cpp
    boxes/peer_list_box.h
    boxes/peer_list_controllers.cpp
    boxes/peer_list_controllers.h
    boxes/peer_lists_box.cpp
    boxes/peer_lists_box.h
    boxes/passcode_box.cpp
    boxes/passcode_box.h
    boxes/photo_crop_box.cpp
    boxes/photo_crop_box.h
    boxes/rate_call_box.cpp
    boxes/rate_call_box.h
    boxes/self_destruction_box.cpp
    boxes/self_destruction_box.h
    boxes/send_files_box.cpp
    boxes/send_files_box.h
    boxes/sessions_box.cpp
    boxes/sessions_box.h
    boxes/share_box.cpp
    boxes/share_box.h
    boxes/sticker_set_box.cpp
    boxes/sticker_set_box.h
    boxes/stickers_box.cpp
    boxes/stickers_box.h
    boxes/url_auth_box.cpp
    boxes/url_auth_box.h
    boxes/username_box.cpp
    boxes/username_box.h
    calls/calls_box_controller.cpp
    calls/calls_box_controller.h
    calls/calls_call.cpp
    calls/calls_call.h
    calls/calls_choose_join_as.cpp
    calls/calls_choose_join_as.h
    calls/calls_group_call.cpp
    calls/calls_group_call.h
    calls/calls_group_common.h
    calls/calls_group_members.cpp
    calls/calls_group_members.h
    calls/calls_group_menu.cpp
    calls/calls_group_menu.h
    calls/calls_group_panel.cpp
    calls/calls_group_panel.h
    calls/calls_group_settings.cpp
    calls/calls_group_settings.h
    calls/calls_emoji_fingerprint.cpp
    calls/calls_emoji_fingerprint.h
    calls/calls_instance.cpp
    calls/calls_instance.h
    calls/calls_panel.cpp
    calls/calls_panel.h
    calls/calls_signal_bars.cpp
    calls/calls_signal_bars.h
    calls/calls_top_bar.cpp
    calls/calls_top_bar.h
    calls/calls_userpic.cpp
    calls/calls_userpic.h
    calls/calls_video_bubble.cpp
    calls/calls_video_bubble.h
    calls/calls_volume_item.cpp
    calls/calls_volume_item.h
    chat_helpers/bot_keyboard.cpp
    chat_helpers/bot_keyboard.h
    chat_helpers/emoji_keywords.cpp
    chat_helpers/emoji_keywords.h
    chat_helpers/emoji_list_widget.cpp
    chat_helpers/emoji_list_widget.h
    chat_helpers/emoji_sets_manager.cpp
    chat_helpers/emoji_sets_manager.h
    chat_helpers/emoji_suggestions_widget.cpp
    chat_helpers/emoji_suggestions_widget.h
    chat_helpers/field_autocomplete.cpp
    chat_helpers/field_autocomplete.h
    chat_helpers/gifs_list_widget.cpp
    chat_helpers/gifs_list_widget.h
    chat_helpers/message_field.cpp
    chat_helpers/message_field.h
    chat_helpers/send_context_menu.cpp
    chat_helpers/send_context_menu.h
    chat_helpers/spellchecker_common.cpp
    chat_helpers/spellchecker_common.h
    chat_helpers/stickers_emoji_image_loader.cpp
    chat_helpers/stickers_emoji_image_loader.h
    chat_helpers/stickers_emoji_pack.cpp
    chat_helpers/stickers_emoji_pack.h
    chat_helpers/stickers_dice_pack.cpp
    chat_helpers/stickers_dice_pack.h
    chat_helpers/stickers_list_widget.cpp
    chat_helpers/stickers_list_widget.h
    chat_helpers/stickers_lottie.cpp
    chat_helpers/stickers_lottie.h
    chat_helpers/tabbed_panel.cpp
    chat_helpers/tabbed_panel.h
    chat_helpers/tabbed_section.cpp
    chat_helpers/tabbed_section.h
    chat_helpers/tabbed_selector.cpp
    chat_helpers/tabbed_selector.h
    core/application.cpp
    core/application.h
    core/base_integration.cpp
    core/base_integration.h
    core/changelogs.cpp
    core/changelogs.h
    core/click_handler_types.cpp
    core/click_handler_types.h
    core/core_cloud_password.cpp
    core/core_cloud_password.h
    core/core_settings.cpp
    core/core_settings.h
    core/crash_report_window.cpp
    core/crash_report_window.h
    core/crash_reports.cpp
    core/crash_reports.h
    core/file_utilities.cpp
    core/file_utilities.h
    core/launcher.cpp
    core/launcher.h
    core/local_url_handlers.cpp
    core/local_url_handlers.h
    core/sandbox.cpp
    core/sandbox.h
    core/shortcuts.cpp
    core/shortcuts.h
    core/ui_integration.cpp
    core/ui_integration.h
    core/update_checker.cpp
    core/update_checker.h
    core/utils.cpp
    core/utils.h
    core/version.h
    data/stickers/data_stickers_set.cpp
    data/stickers/data_stickers_set.h
    data/stickers/data_stickers.cpp
    data/stickers/data_stickers.h
    data/data_abstract_structure.cpp
    data/data_abstract_structure.h
    data/data_auto_download.cpp
    data/data_auto_download.h
    data/data_chat.cpp
    data/data_chat.h
    data/data_chat_filters.cpp
    data/data_chat_filters.h
    data/data_changes.cpp
    data/data_changes.h
    data/data_channel.cpp
    data/data_channel.h
    data/data_channel_admins.cpp
    data/data_channel_admins.h
    data/data_cloud_file.cpp
    data/data_cloud_file.h
    data/data_cloud_themes.cpp
    data/data_cloud_themes.h
    data/data_document.cpp
    data/data_document.h
    data/data_document_media.cpp
    data/data_document_media.h
    data/data_drafts.cpp
    data/data_drafts.h
    data/data_folder.cpp
    data/data_folder.h
    data/data_file_origin.cpp
    data/data_file_origin.h
    data/data_flags.h
    data/data_game.h
    data/data_group_call.cpp
    data/data_group_call.h
    data/data_groups.cpp
    data/data_groups.h
    data/data_histories.cpp
    data/data_histories.h
    data/data_location.cpp
    data/data_location.h
    data/data_media_rotation.cpp
    data/data_media_rotation.h
    data/data_media_types.cpp
    data/data_media_types.h
    data/data_messages.cpp
    data/data_messages.h
    data/data_notify_settings.cpp
    data/data_notify_settings.h
    data/data_peer.cpp
    data/data_peer.h
    data/data_peer_id.cpp
    data/data_peer_id.h
    data/data_peer_values.cpp
    data/data_peer_values.h
    data/data_photo.cpp
    data/data_photo.h
    data/data_photo_media.cpp
    data/data_photo_media.h
    data/data_poll.cpp
    data/data_poll.h
    data/data_pts_waiter.cpp
    data/data_pts_waiter.h
    data/data_replies_list.cpp
    data/data_replies_list.h
    data/data_reply_preview.cpp
    data/data_reply_preview.h
    data/data_search_controller.cpp
    data/data_search_controller.h
    data/data_session.cpp
    data/data_session.h
    data/data_scheduled_messages.cpp
    data/data_scheduled_messages.h
    data/data_shared_media.cpp
    data/data_shared_media.h
    data/data_sparse_ids.cpp
    data/data_sparse_ids.h
    data/data_streaming.cpp
    data/data_streaming.h
    data/data_types.cpp
    data/data_types.h
    data/data_user.cpp
    data/data_user.h
    data/data_user_photos.cpp
    data/data_user_photos.h
    data/data_wall_paper.cpp
    data/data_wall_paper.h
    data/data_web_page.cpp
    data/data_web_page.h
    dialogs/dialogs_entry.cpp
    dialogs/dialogs_entry.h
    dialogs/dialogs_indexed_list.cpp
    dialogs/dialogs_indexed_list.h
    dialogs/dialogs_inner_widget.cpp
    dialogs/dialogs_inner_widget.h
    dialogs/dialogs_key.cpp
    dialogs/dialogs_key.h
    dialogs/dialogs_layout.cpp
    dialogs/dialogs_layout.h
    dialogs/dialogs_list.cpp
    dialogs/dialogs_list.h
    dialogs/dialogs_main_list.cpp
    dialogs/dialogs_main_list.h
    dialogs/dialogs_pinned_list.cpp
    dialogs/dialogs_pinned_list.h
    dialogs/dialogs_row.cpp
    dialogs/dialogs_row.h
    dialogs/dialogs_search_from_controllers.cpp
    dialogs/dialogs_search_from_controllers.h
    dialogs/dialogs_widget.cpp
    dialogs/dialogs_widget.h
    export/export_manager.cpp
    export/export_manager.h
    export/view/export_view_content.cpp
    export/view/export_view_content.h
    export/view/export_view_panel_controller.cpp
    export/view/export_view_panel_controller.h
    export/view/export_view_progress.cpp
    export/view/export_view_progress.h
    export/view/export_view_settings.cpp
    export/view/export_view_settings.h
    export/view/export_view_top_bar.cpp
    export/view/export_view_top_bar.h
    history/admin_log/history_admin_log_filter.cpp
    history/admin_log/history_admin_log_filter.h
    history/admin_log/history_admin_log_inner.cpp
    history/admin_log/history_admin_log_inner.h
    history/admin_log/history_admin_log_item.cpp
    history/admin_log/history_admin_log_item.h
    history/admin_log/history_admin_log_section.cpp
    history/admin_log/history_admin_log_section.h
    history/view/controls/compose_controls_common.h
    history/view/controls/history_view_compose_controls.cpp
    history/view/controls/history_view_compose_controls.h
    history/view/controls/history_view_ttl_button.cpp
    history/view/controls/history_view_ttl_button.h
    history/view/controls/history_view_voice_record_bar.cpp
    history/view/controls/history_view_voice_record_bar.h
    history/view/controls/history_view_voice_record_button.cpp
    history/view/controls/history_view_voice_record_button.h
    history/view/media/history_view_call.h
    history/view/media/history_view_call.cpp
    history/view/media/history_view_contact.h
    history/view/media/history_view_contact.cpp
    history/view/media/history_view_dice.h
    history/view/media/history_view_dice.cpp
    history/view/media/history_view_document.h
    history/view/media/history_view_document.cpp
    history/view/media/history_view_file.h
    history/view/media/history_view_file.cpp
    history/view/media/history_view_game.h
    history/view/media/history_view_game.cpp
    history/view/media/history_view_gif.h
    history/view/media/history_view_gif.cpp
    history/view/media/history_view_invoice.h
    history/view/media/history_view_invoice.cpp
    history/view/media/history_view_large_emoji.h
    history/view/media/history_view_large_emoji.cpp
    history/view/media/history_view_location.h
    history/view/media/history_view_location.cpp
    history/view/media/history_view_media.h
    history/view/media/history_view_media.cpp
    history/view/media/history_view_media_common.h
    history/view/media/history_view_media_common.cpp
    history/view/media/history_view_media_grouped.h
    history/view/media/history_view_media_grouped.cpp
    history/view/media/history_view_media_unwrapped.h
    history/view/media/history_view_media_unwrapped.cpp
    history/view/media/history_view_photo.h
    history/view/media/history_view_photo.cpp
    history/view/media/history_view_poll.h
    history/view/media/history_view_poll.cpp
    history/view/media/history_view_slot_machine.h
    history/view/media/history_view_slot_machine.cpp
    history/view/media/history_view_sticker.h
    history/view/media/history_view_sticker.cpp
    history/view/media/history_view_theme_document.h
    history/view/media/history_view_theme_document.cpp
    history/view/media/history_view_web_page.h
    history/view/media/history_view_web_page.cpp
    history/view/history_view_contact_status.cpp
    history/view/history_view_contact_status.h
    history/view/history_view_context_menu.cpp
    history/view/history_view_context_menu.h
    history/view/history_view_cursor_state.cpp
    history/view/history_view_cursor_state.h
    history/view/history_view_element.cpp
    history/view/history_view_element.h
    history/view/history_view_group_call_tracker.cpp
    history/view/history_view_group_call_tracker.h
    history/view/history_view_list_widget.cpp
    history/view/history_view_list_widget.h
    history/view/history_view_message.cpp
    history/view/history_view_message.h
    history/view/history_view_object.h
    history/view/history_view_pinned_bar.cpp
    history/view/history_view_pinned_bar.h
    history/view/history_view_pinned_section.cpp
    history/view/history_view_pinned_section.h
    history/view/history_view_pinned_tracker.cpp
    history/view/history_view_pinned_tracker.h
    history/view/history_view_replies_section.cpp
    history/view/history_view_replies_section.h
    history/view/history_view_schedule_box.cpp
    history/view/history_view_schedule_box.h
    history/view/history_view_scheduled_section.cpp
    history/view/history_view_scheduled_section.h
    history/view/history_view_send_action.cpp
    history/view/history_view_send_action.h
    history/view/history_view_service_message.cpp
    history/view/history_view_service_message.h
    history/view/history_view_top_bar_widget.cpp
    history/view/history_view_top_bar_widget.h
    history/view/history_view_webpage_preview.cpp
    history/view/history_view_webpage_preview.h
    history/history.cpp
    history/history.h
    history/history_drag_area.cpp
    history/history_drag_area.h
    history/history_item.cpp
    history/history_item.h
    history/history_item_components.cpp
    history/history_item_components.h
    history/history_item_text.cpp
    history/history_item_text.h
    history/history_inner_widget.cpp
    history/history_inner_widget.h
    history/history_location_manager.cpp
    history/history_location_manager.h
    history/history_message.cpp
    history/history_message.h
    history/history_service.cpp
    history/history_service.h
    history/history_widget.cpp
    history/history_widget.h
    info/info_content_widget.cpp
    info/info_content_widget.h
    info/info_controller.cpp
    info/info_controller.h
    info/info_layer_widget.cpp
    info/info_layer_widget.h
    info/info_memento.cpp
    info/info_memento.h
    info/info_section_widget.cpp
    info/info_section_widget.h
    info/info_top_bar.cpp
    info/info_top_bar.h
    info/info_wrap_widget.cpp
    info/info_wrap_widget.h
    info/common_groups/info_common_groups_inner_widget.cpp
    info/common_groups/info_common_groups_inner_widget.h
    info/common_groups/info_common_groups_widget.cpp
    info/common_groups/info_common_groups_widget.h
    info/media/info_media_buttons.h
    info/media/info_media_empty_widget.cpp
    info/media/info_media_empty_widget.h
    info/media/info_media_inner_widget.cpp
    info/media/info_media_inner_widget.h
    info/media/info_media_list_widget.cpp
    info/media/info_media_list_widget.h
    info/media/info_media_widget.cpp
    info/media/info_media_widget.h
    info/members/info_members_widget.cpp
    info/members/info_members_widget.h
    info/polls/info_polls_results_inner_widget.cpp
    info/polls/info_polls_results_inner_widget.h
    info/polls/info_polls_results_widget.cpp
    info/polls/info_polls_results_widget.h
    info/profile/info_profile_actions.cpp
    info/profile/info_profile_actions.h
    info/profile/info_profile_cover.cpp
    info/profile/info_profile_cover.h
    info/profile/info_profile_icon.cpp
    info/profile/info_profile_icon.h
    info/profile/info_profile_inner_widget.cpp
    info/profile/info_profile_inner_widget.h
    info/profile/info_profile_members.cpp
    info/profile/info_profile_members.h
    info/profile/info_profile_members_controllers.cpp
    info/profile/info_profile_members_controllers.h
    info/profile/info_profile_text.cpp
    info/profile/info_profile_text.h
    info/profile/info_profile_values.cpp
    info/profile/info_profile_values.h
    info/profile/info_profile_widget.cpp
    info/profile/info_profile_widget.h
    info/settings/info_settings_widget.cpp
    info/settings/info_settings_widget.h
    inline_bots/inline_bot_layout_internal.cpp
    inline_bots/inline_bot_layout_internal.h
    inline_bots/inline_bot_layout_item.cpp
    inline_bots/inline_bot_layout_item.h
    inline_bots/inline_bot_result.cpp
    inline_bots/inline_bot_result.h
    inline_bots/inline_bot_send_data.cpp
    inline_bots/inline_bot_send_data.h
    inline_bots/inline_results_inner.cpp
    inline_bots/inline_results_inner.h
    inline_bots/inline_results_widget.cpp
    inline_bots/inline_results_widget.h
    intro/intro_code.cpp
    intro/intro_code.h
    intro/intro_password_check.cpp
    intro/intro_password_check.h
    intro/intro_phone.cpp
    intro/intro_phone.h
    intro/intro_qr.cpp
    intro/intro_qr.h
    intro/intro_signup.cpp
    intro/intro_signup.h
    intro/intro_start.cpp
    intro/intro_start.h
    intro/intro_step.cpp
    intro/intro_step.h
    intro/intro_widget.cpp
    intro/intro_widget.h
    lang/lang_cloud_manager.cpp
    lang/lang_cloud_manager.h
    lang/lang_instance.cpp
    lang/lang_instance.h
    lang/lang_numbers_animation.cpp
    lang/lang_numbers_animation.h
    lang/lang_translator.cpp
    lang/lang_translator.h
    main/main_account.cpp
    main/main_account.h
    main/main_app_config.cpp
    main/main_app_config.h
    main/main_domain.cpp
    main/main_domain.h
    main/main_session.cpp
    main/main_session.h
    main/main_session_settings.cpp
    main/main_session_settings.h
    media/audio/media_audio.cpp
    media/audio/media_audio.h
    media/audio/media_audio_capture.cpp
    media/audio/media_audio_capture.h
    media/audio/media_audio_ffmpeg_loader.cpp
    media/audio/media_audio_ffmpeg_loader.h
    media/audio/media_audio_loader.cpp
    media/audio/media_audio_loader.h
    media/audio/media_audio_loaders.cpp
    media/audio/media_audio_loaders.h
    media/audio/media_audio_track.cpp
    media/audio/media_audio_track.h
    media/audio/media_child_ffmpeg_loader.cpp
    media/audio/media_child_ffmpeg_loader.h
    media/audio/media_openal_functions.cpp
    media/audio/media_openal_functions.h
    media/player/media_player_button.cpp
    media/player/media_player_button.h
    media/player/media_player_float.cpp
    media/player/media_player_float.h
    media/player/media_player_instance.cpp
    media/player/media_player_instance.h
    media/player/media_player_panel.cpp
    media/player/media_player_panel.h
    media/player/media_player_volume_controller.cpp
    media/player/media_player_volume_controller.h
    media/player/media_player_widget.cpp
    media/player/media_player_widget.h
    media/streaming/media_streaming_audio_track.cpp
    media/streaming/media_streaming_audio_track.h
    media/streaming/media_streaming_common.h
    media/streaming/media_streaming_document.cpp
    media/streaming/media_streaming_document.h
    media/streaming/media_streaming_file.cpp
    media/streaming/media_streaming_file.h
    media/streaming/media_streaming_file_delegate.h
    media/streaming/media_streaming_instance.cpp
    media/streaming/media_streaming_instance.h
    media/streaming/media_streaming_loader.cpp
    media/streaming/media_streaming_loader.h
    media/streaming/media_streaming_loader_local.cpp
    media/streaming/media_streaming_loader_local.h
    media/streaming/media_streaming_loader_mtproto.cpp
    media/streaming/media_streaming_loader_mtproto.h
    media/streaming/media_streaming_player.cpp
    media/streaming/media_streaming_player.h
    media/streaming/media_streaming_reader.cpp
    media/streaming/media_streaming_reader.h
    media/streaming/media_streaming_utility.cpp
    media/streaming/media_streaming_utility.h
    media/streaming/media_streaming_video_track.cpp
    media/streaming/media_streaming_video_track.h
    media/view/media_view_group_thumbs.cpp
    media/view/media_view_group_thumbs.h
    media/view/media_view_overlay_widget.cpp
    media/view/media_view_overlay_widget.h
    media/view/media_view_pip.cpp
    media/view/media_view_pip.h
    media/view/media_view_playback_controls.cpp
    media/view/media_view_playback_controls.h
    media/view/media_view_playback_progress.cpp
    media/view/media_view_playback_progress.h
    mtproto/config_loader.cpp
    mtproto/config_loader.h
    mtproto/connection_abstract.cpp
    mtproto/connection_abstract.h
    mtproto/connection_http.cpp
    mtproto/connection_http.h
    mtproto/connection_resolving.cpp
    mtproto/connection_resolving.h
    mtproto/connection_tcp.cpp
    mtproto/connection_tcp.h
    mtproto/core_types.h
    mtproto/dedicated_file_loader.cpp
    mtproto/dedicated_file_loader.h
    mtproto/facade.cpp
    mtproto/facade.h
    mtproto/mtp_instance.cpp
    mtproto/mtp_instance.h
    mtproto/sender.h
    mtproto/session.cpp
    mtproto/session.h
    mtproto/session_private.cpp
    mtproto/session_private.h
    mtproto/special_config_request.cpp
    mtproto/special_config_request.h
    mtproto/type_utils.h
    overview/overview_layout.cpp
    overview/overview_layout.h
    overview/overview_layout_delegate.h
    passport/passport_encryption.cpp
    passport/passport_encryption.h
    passport/passport_form_controller.cpp
    passport/passport_form_controller.h
    passport/passport_form_view_controller.cpp
    passport/passport_form_view_controller.h
    passport/passport_panel.cpp
    passport/passport_panel.h
    passport/passport_panel_controller.cpp
    passport/passport_panel_controller.h
    passport/passport_panel_edit_contact.cpp
    passport/passport_panel_edit_contact.h
    passport/passport_panel_edit_document.cpp
    passport/passport_panel_edit_document.h
    passport/passport_panel_edit_scans.cpp
    passport/passport_panel_edit_scans.h
    passport/passport_panel_form.cpp
    passport/passport_panel_form.h
    passport/passport_panel_password.cpp
    passport/passport_panel_password.h
    payments/payments_checkout_process.cpp
    payments/payments_checkout_process.h
    payments/payments_form.cpp
    payments/payments_form.h
    platform/linux/linux_desktop_environment.cpp
    platform/linux/linux_desktop_environment.h
    platform/linux/linux_gdk_helper.cpp
    platform/linux/linux_gdk_helper.h
    platform/linux/linux_gsd_media_keys.cpp
    platform/linux/linux_gsd_media_keys.h
    platform/linux/linux_gtk_file_dialog.cpp
    platform/linux/linux_gtk_file_dialog.h
    platform/linux/linux_gtk_integration_p.h
    platform/linux/linux_gtk_integration.cpp
    platform/linux/linux_gtk_integration.h
    platform/linux/linux_gtk_open_with_dialog.cpp
    platform/linux/linux_gtk_open_with_dialog.h
    platform/linux/linux_mpris_support.cpp
    platform/linux/linux_mpris_support.h
    platform/linux/linux_notification_service_watcher.cpp
    platform/linux/linux_notification_service_watcher.h
    platform/linux/linux_wayland_integration.cpp
    platform/linux/linux_wayland_integration.h
    platform/linux/linux_xdp_file_dialog.cpp
    platform/linux/linux_xdp_file_dialog.h
    platform/linux/linux_xdp_open_with_dialog.cpp
    platform/linux/linux_xdp_open_with_dialog.h
    platform/linux/file_utilities_linux.cpp
    platform/linux/file_utilities_linux.h
    platform/linux/launcher_linux.cpp
    platform/linux/launcher_linux.h
    platform/linux/main_window_linux.cpp
    platform/linux/main_window_linux.h
    platform/linux/notifications_manager_linux.cpp
    platform/linux/notifications_manager_linux.h
    platform/linux/specific_linux.cpp
    platform/linux/specific_linux.h
    platform/linux/window_title_linux.h
    platform/mac/file_utilities_mac.mm
    platform/mac/file_utilities_mac.h
    platform/mac/launcher_mac.mm
    platform/mac/launcher_mac.h
    platform/mac/main_window_mac.mm
    platform/mac/main_window_mac.h
    platform/mac/notifications_manager_mac.mm
    platform/mac/notifications_manager_mac.h
    platform/mac/specific_mac.mm
    platform/mac/specific_mac.h
    platform/mac/specific_mac_p.mm
    platform/mac/specific_mac_p.h
    platform/mac/window_title_mac.mm
    platform/mac/window_title_mac.h
    platform/mac/touchbar/items/mac_formatter_item.h
    platform/mac/touchbar/items/mac_formatter_item.mm
    platform/mac/touchbar/items/mac_pinned_chats_item.h
    platform/mac/touchbar/items/mac_pinned_chats_item.mm
    platform/mac/touchbar/items/mac_scrubber_item.h
    platform/mac/touchbar/items/mac_scrubber_item.mm
    platform/mac/touchbar/mac_touchbar_audio.h
    platform/mac/touchbar/mac_touchbar_audio.mm
    platform/mac/touchbar/mac_touchbar_common.h
    platform/mac/touchbar/mac_touchbar_common.mm
    platform/mac/touchbar/mac_touchbar_controls.h
    platform/mac/touchbar/mac_touchbar_controls.mm
    platform/mac/touchbar/mac_touchbar_main.h
    platform/mac/touchbar/mac_touchbar_main.mm
    platform/mac/touchbar/mac_touchbar_manager.h
    platform/mac/touchbar/mac_touchbar_manager.mm
    platform/mac/touchbar/mac_touchbar_media_view.h
    platform/mac/touchbar/mac_touchbar_media_view.mm
    platform/win/audio_win.cpp
    platform/win/audio_win.h
    platform/win/file_utilities_win.cpp
    platform/win/file_utilities_win.h
    platform/win/launcher_win.cpp
    platform/win/launcher_win.h
    platform/win/main_window_win.cpp
    platform/win/main_window_win.h
    platform/win/notifications_manager_win.cpp
    platform/win/notifications_manager_win.h
    platform/win/specific_win.cpp
    platform/win/specific_win.h
    platform/win/window_title_win.cpp
    platform/win/window_title_win.h
    platform/win/windows_app_user_model_id.cpp
    platform/win/windows_app_user_model_id.h
    platform/win/windows_dlls.cpp
    platform/win/windows_dlls.h
    platform/win/windows_event_filter.cpp
    platform/win/windows_event_filter.h
    platform/platform_audio.h
    platform/platform_file_utilities.h
    platform/platform_launcher.h
    platform/platform_main_window.h
    platform/platform_notifications_manager.h
    platform/platform_specific.h
    platform/platform_window_title.h
    profile/profile_back_button.cpp
    profile/profile_back_button.h
    profile/profile_block_group_members.cpp
    profile/profile_block_group_members.h
    profile/profile_block_peer_list.cpp
    profile/profile_block_peer_list.h
    profile/profile_block_widget.cpp
    profile/profile_block_widget.h
    profile/profile_cover_drop_area.cpp
    profile/profile_cover_drop_area.h
    settings/settings_advanced.cpp
    settings/settings_advanced.h
    settings/settings_chat.cpp
    settings/settings_chat.h
    settings/settings_calls.cpp
    settings/settings_calls.h
    settings/settings_codes.cpp
    settings/settings_codes.h
    settings/settings_common.cpp
    settings/settings_common.h
    settings/settings_folders.cpp
    settings/settings_folders.h
    settings/settings_information.cpp
    settings/settings_information.h
    settings/settings_intro.cpp
    settings/settings_intro.h
    settings/settings_main.cpp
    settings/settings_main.h
    settings/settings_notifications.cpp
    settings/settings_notifications.h
    settings/settings_privacy_controllers.cpp
    settings/settings_privacy_controllers.h
    settings/settings_privacy_security.cpp
    settings/settings_privacy_security.h
    storage/details/storage_file_utilities.cpp
    storage/details/storage_file_utilities.h
    storage/details/storage_settings_scheme.cpp
    storage/details/storage_settings_scheme.h
    storage/download_manager_mtproto.cpp
    storage/download_manager_mtproto.h
    storage/file_download.cpp
    storage/file_download.h
    storage/file_download_mtproto.cpp
    storage/file_download_mtproto.h
    storage/file_download_web.cpp
    storage/file_download_web.h
    storage/file_upload.cpp
    storage/file_upload.h
    storage/localimageloader.cpp
    storage/localimageloader.h
    storage/localstorage.cpp
    storage/localstorage.h
    storage/serialize_common.cpp
    storage/serialize_common.h
    storage/serialize_document.cpp
    storage/serialize_document.h
    storage/serialize_peer.cpp
    storage/serialize_peer.h
    storage/storage_account.cpp
    storage/storage_account.h
    storage/storage_cloud_blob.cpp
    storage/storage_cloud_blob.h
    storage/storage_cloud_song_cover.cpp
    storage/storage_cloud_song_cover.h
    storage/storage_domain.cpp
    storage/storage_domain.h
    storage/storage_facade.cpp
    storage/storage_facade.h
    storage/storage_media_prepare.cpp
    storage/storage_media_prepare.h
    storage/storage_shared_media.cpp
    storage/storage_shared_media.h
    storage/storage_sparse_ids_list.cpp
    storage/storage_sparse_ids_list.h
    storage/storage_user_photos.cpp
    storage/storage_user_photos.h
    storage/streamed_file_downloader.cpp
    storage/streamed_file_downloader.h
    support/support_autocomplete.cpp
    support/support_autocomplete.h
    support/support_common.cpp
    support/support_common.h
    support/support_helper.cpp
    support/support_helper.h
    support/support_templates.cpp
    support/support_templates.h
    ui/effects/fireworks_animation.cpp
    ui/effects/fireworks_animation.h
    ui/effects/round_checkbox.cpp
    ui/effects/round_checkbox.h
    ui/effects/send_action_animations.cpp
    ui/effects/send_action_animations.h
    ui/image/image.cpp
    ui/image/image.h
    ui/image/image_location.cpp
    ui/image/image_location.h
    ui/image/image_location_factory.cpp
    ui/image/image_location_factory.h
    ui/widgets/continuous_sliders.cpp
    ui/widgets/continuous_sliders.h
    ui/widgets/discrete_sliders.cpp
    ui/widgets/discrete_sliders.h
    ui/widgets/level_meter.cpp
    ui/widgets/level_meter.h
    ui/widgets/multi_select.cpp
    ui/widgets/multi_select.h
    ui/countryinput.cpp
    ui/countryinput.h
    ui/empty_userpic.cpp
    ui/empty_userpic.h
    ui/filter_icons.cpp
    ui/filter_icons.h
    ui/filter_icon_panel.cpp
    ui/filter_icon_panel.h
    ui/item_text_options.cpp
    ui/item_text_options.h
    ui/resize_area.h
    ui/search_field_controller.cpp
    ui/search_field_controller.h
    ui/special_buttons.cpp
    ui/special_buttons.h
    ui/unread_badge.cpp
    ui/unread_badge.h
    window/main_window.cpp
    window/main_window.h
    window/notifications_manager.cpp
    window/notifications_manager.h
    window/notifications_manager_default.cpp
    window/notifications_manager_default.h
    window/notifications_utilities.cpp
    window/notifications_utilities.h
    window/section_memento.h
    window/section_widget.cpp
    window/section_widget.h
    window/window_connecting_widget.cpp
    window/window_connecting_widget.h
    window/window_controller.cpp
    window/window_controller.h
    window/window_filters_menu.cpp
    window/window_filters_menu.h
    window/window_history_hider.cpp
    window/window_history_hider.h
    window/window_lock_widgets.cpp
    window/window_lock_widgets.h
    window/window_main_menu.cpp
    window/window_main_menu.h
    window/window_media_preview.cpp
    window/window_media_preview.h
    window/window_outdated_bar.cpp
    window/window_outdated_bar.h
    window/window_peer_menu.cpp
    window/window_peer_menu.h
    window/window_session_controller.cpp
    window/window_session_controller.h
    window/window_slide_animation.cpp
    window/window_slide_animation.h
    window/window_title_qt.cpp
    window/window_title_qt.h
    window/window_title.h
    window/window_top_bar_wrap.h
    window/themes/window_theme.cpp
    window/themes/window_theme.h
    window/themes/window_theme_editor.cpp
    window/themes/window_theme_editor.h
    window/themes/window_theme_editor_block.cpp
    window/themes/window_theme_editor_block.h
    window/themes/window_theme_editor_box.cpp
    window/themes/window_theme_editor_box.h
    window/themes/window_theme_preview.cpp
    window/themes/window_theme_preview.h
    window/themes/window_theme_warning.cpp
    window/themes/window_theme_warning.h
    window/themes/window_themes_cloud_list.cpp
    window/themes/window_themes_cloud_list.h
    window/themes/window_themes_embedded.cpp
    window/themes/window_themes_embedded.h
    window/themes/window_themes_generate_name.cpp
    window/themes/window_themes_generate_name.h
    apiwrap.cpp
    apiwrap.h
    app.cpp
    app.h
    config.h
    facades.cpp
    facades.h
    layout.cpp
    layout.h
    logs.cpp
    logs.h
    main.cpp
    mainwidget.cpp
    mainwidget.h
    mainwindow.cpp
    mainwindow.h
    settings.cpp
    settings.h
    stdafx.h
)

if (NOT LINUX)
    remove_target_sources(Telegram ${src_loc}
        window/window_title_qt.cpp
        window/window_title_qt.h
    )
endif()

if (DESKTOP_APP_DISABLE_DBUS_INTEGRATION)
    remove_target_sources(Telegram ${src_loc}
        platform/linux/linux_gsd_media_keys.cpp
        platform/linux/linux_gsd_media_keys.h
        platform/linux/linux_mpris_support.cpp
        platform/linux/linux_mpris_support.h
        platform/linux/linux_notification_service_watcher.cpp
        platform/linux/linux_notification_service_watcher.h
        platform/linux/linux_xdp_file_dialog.cpp
        platform/linux/linux_xdp_file_dialog.h
        platform/linux/linux_xdp_open_with_dialog.cpp
        platform/linux/linux_xdp_open_with_dialog.h
        platform/linux/notifications_manager_linux.cpp
    )

    nice_target_sources(Telegram ${src_loc}
    PRIVATE
        platform/linux/notifications_manager_linux_dummy.cpp
    )
endif()

if (DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION)
    remove_target_sources(Telegram ${src_loc} platform/linux/linux_wayland_integration.cpp)
    nice_target_sources(Telegram ${src_loc} PRIVATE platform/linux/linux_wayland_integration_dummy.cpp)
endif()

if (DESKTOP_APP_DISABLE_GTK_INTEGRATION)
    remove_target_sources(Telegram ${src_loc}
        platform/linux/linux_gdk_helper.cpp
        platform/linux/linux_gdk_helper.h
        platform/linux/linux_gtk_file_dialog.cpp
        platform/linux/linux_gtk_file_dialog.h
        platform/linux/linux_gtk_integration_p.h
        platform/linux/linux_gtk_integration.cpp
        platform/linux/linux_gtk_open_with_dialog.cpp
        platform/linux/linux_gtk_open_with_dialog.h
    )

    nice_target_sources(Telegram ${src_loc}
    PRIVATE
        platform/linux/linux_gtk_integration_dummy.cpp
    )
endif()

if (NOT DESKTOP_APP_USE_PACKAGED)
    nice_target_sources(Telegram ${src_loc} PRIVATE platform/mac/mac_iconv_helper.c)
endif()

nice_target_sources(Telegram ${res_loc}
PRIVATE
    qrc/emoji_1.qrc
    qrc/emoji_2.qrc
    qrc/emoji_3.qrc
    qrc/emoji_4.qrc
    qrc/emoji_5.qrc
    qrc/emoji_6.qrc
    qrc/emoji_7.qrc
    qrc/emoji_preview.qrc
    qrc/telegram/telegram.qrc
    qrc/telegram/sounds.qrc
    winrc/Telegram.rc
    winrc/Telegram.manifest
    langs/lang.strings
    langs/cloud_lang.strings
    numbers.txt
)

if (WIN32)
    # message(${CMAKE_GENERATOR})
    # mt.exe -manifest "${res_loc}/winrc/Telegram.manifest" "-inputresource:\"$<TARGET_FILE:Telegram>\";#1" "-outputresource:\"$<TARGET_FILE:Telegram>\";#1" >NUL
    # set(hash_symbol "#")
    # set(release $<CONFIG:Release>)
    # add_custom_command(
    # TARGET
    #     Telegram
    # POST_BUILD COMMAND
    #     $<IF:${release},mt.exe,echo.> $<${release}:-manifest> $<${release}:"${res_loc}/winrc/Telegram.manifest"> $<${release}:-inputresource:"$<TARGET_FILE:Telegram>"$<SEMICOLON>${hash_symbol}1> $<${release}:-outputresource:"$<TARGET_FILE:Telegram>"$<SEMICOLON>${hash_symbol}1> $<${release}:$<ANGLE-R>NUL>
    # COMMENT
    #     $<IF:${release},"Appending compatibility manifest.","Finalizing build.">
    # )
elseif (APPLE)
    target_link_libraries(Telegram PRIVATE desktop-app::external_sp_media_key_tap)

    if (NOT DESKTOP_APP_USE_PACKAGED)
        target_link_libraries(Telegram PRIVATE desktop-app::external_iconv)
    endif()

    set(icons_path ${CMAKE_CURRENT_SOURCE_DIR}/Telegram/Images.xcassets)
    set_target_properties(Telegram PROPERTIES RESOURCE ${icons_path})
    target_sources(Telegram PRIVATE ${icons_path})

    set(lang_packs
        en
        de
        es
        it
        nl
        ko
        pt-BR
    )
    foreach (lang ${lang_packs})
        set(strings_path ${res_loc}/langs/${lang}.lproj/Localizable.strings)
        set_source_files_properties(${strings_path} PROPERTIES
        MACOSX_PACKAGE_LOCATION
            Resources/${lang}.lproj
        )
        target_sources(Telegram PRIVATE ${strings_path})
        source_group(TREE ${res_loc} PREFIX Resources FILES ${strings_path})
    endforeach()

    if (NOT build_macstore)
        add_custom_command(TARGET Telegram
        PRE_LINK
            COMMAND mkdir -p $<TARGET_FILE_DIR:Telegram>/../Frameworks
            COMMAND cp $<TARGET_FILE:Updater> $<TARGET_FILE_DIR:Telegram>/../Frameworks/
        )
        if (NOT DESKTOP_APP_DISABLE_CRASH_REPORTS)
            add_custom_command(TARGET Telegram
            PRE_LINK
                COMMAND mkdir -p $<TARGET_FILE_DIR:Telegram>/../Helpers
                COMMAND cp ${libs_loc}/crashpad/out/$<IF:$<CONFIG:Debug>,Debug,Release>/crashpad_handler $<TARGET_FILE_DIR:Telegram>/../Helpers/
            )
        endif()
    endif()
endif()

if (build_macstore)
    set(bundle_identifier "org.telegram.desktop")
    set(bundle_entitlements "Telegram Lite.entitlements")
    set(output_name "Telegram Lite")
    set_target_properties(Telegram PROPERTIES
        XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS ${libs_loc}/breakpad/src/client/mac/build/Release
    )
    target_link_frameworks(Telegram PRIVATE Breakpad)
    add_custom_command(TARGET Telegram
    PRE_LINK
        COMMAND rm -rf $<TARGET_FILE_DIR:Telegram>/../Frameworks
        COMMAND mkdir -p $<TARGET_FILE_DIR:Telegram>/../Frameworks
        COMMAND cp -a ${libs_loc}/breakpad/src/client/mac/build/Release/Breakpad.framework $<TARGET_FILE_DIR:Telegram>/../Frameworks/Breakpad.framework
    )
elseif (build_osx)
    set(bundle_identifier "com.tdesktop.Telegram$<$<CONFIG:Debug>:DebugOsx>")
    set(bundle_entitlements "Telegram.entitlements")
    set(output_name "Telegram")
else()
    set(bundle_identifier "com.tdesktop.Telegram$<$<CONFIG:Debug>:Debug>")
    set(bundle_entitlements "Telegram.entitlements")
    if (LINUX AND DESKTOP_APP_USE_PACKAGED)
        set(output_name "telegram-desktop")
    else()
        set(output_name "Telegram")
    endif()
endif()

set_target_properties(Telegram PROPERTIES
    OUTPUT_NAME ${output_name}
    MACOSX_BUNDLE_GUI_IDENTIFIER ${bundle_identifier}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Telegram.plist
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/Telegram/${bundle_entitlements}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${bundle_identifier}
    XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION ${desktop_app_version_string}
    XCODE_ATTRIBUTE_PRODUCT_NAME ${output_name}
    XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT $<$<NOT:$<CONFIG:Debug>>:dwarf-with-dsym>
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
    XCODE_ATTRIBUTE_COMBINE_HIDPI_IMAGES YES
    XCODE_ATTRIBUTE_COPY_PHASE_STRIP NO
    XCODE_ATTRIBUTE_ALWAYS_SEARCH_USER_PATHS NO
    XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY libc++
    XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS --deep
)
set(entitlement_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/Telegram/Telegram.entitlements"
    "${CMAKE_CURRENT_SOURCE_DIR}/Telegram/Telegram Lite.entitlements"
)
target_sources(Telegram PRIVATE ${entitlement_sources})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Telegram PREFIX Resources FILES ${entitlement_sources})

target_include_directories(Telegram PRIVATE ${src_loc})

target_compile_definitions(Telegram
PRIVATE
    TDESKTOP_API_ID=${TDESKTOP_API_ID}
    TDESKTOP_API_HASH=${TDESKTOP_API_HASH}
    G_LOG_DOMAIN="Telegram"
)

if (APPLE OR NOT CMAKE_EXECUTABLE_SUFFIX STREQUAL "" OR NOT "${output_name}" STREQUAL "Telegram")
    set(output_folder ${CMAKE_BINARY_DIR})
elseif (DESKTOP_APP_SPECIAL_TARGET STREQUAL "")
    set(output_folder ${CMAKE_BINARY_DIR}/bin)
else()
    set(output_folder ${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>)
endif()

set_target_properties(Telegram PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${output_folder})

if ((NOT DESKTOP_APP_DISABLE_AUTOUPDATE OR APPLE) AND NOT build_macstore AND NOT build_winstore)
    add_executable(Updater WIN32)
    init_target(Updater)

    add_dependencies(Telegram Updater)

    nice_target_sources(Updater ${src_loc}
    PRIVATE
        _other/updater_win.cpp
        _other/updater_linux.cpp
        _other/updater_osx.m
        _other/updater.h
    )

    set_target_properties(Updater PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${output_folder})

    if (WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_link_options(Updater PRIVATE -municode)
    endif()

    if (LINUX)
        target_link_options(Updater PRIVATE -static-libstdc++)
    endif()

    if (DESKTOP_APP_SPECIAL_TARGET)
        add_executable(Packer WIN32)
        init_target(Packer)

        add_dependencies(Telegram Packer)

        nice_target_sources(Packer ${src_loc}
        PRIVATE
            _other/packer.cpp
            _other/packer.h
        )

        target_link_libraries(Packer
        PRIVATE
            desktop-app::external_qt
            desktop-app::external_zlib
            desktop-app::external_auto_updates
            desktop-app::external_openssl
        )

        set_target_properties(Packer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${output_folder})
    endif()
endif()

if (LINUX AND DESKTOP_APP_USE_PACKAGED)
    include(GNUInstallDirs)
    configure_file("../lib/xdg/telegramdesktop.appdata.xml.in" "${CMAKE_CURRENT_BINARY_DIR}/telegramdesktop.appdata.xml" @ONLY)
    generate_appdata_changelog(Telegram "${CMAKE_SOURCE_DIR}/changelog.txt" "${CMAKE_CURRENT_BINARY_DIR}/telegramdesktop.appdata.xml")
    install(TARGETS Telegram RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "Resources/art/icon16.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/16x16/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon32.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/32x32/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon48.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/48x48/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon64.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/64x64/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon128.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon256.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon512.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/512x512/apps" RENAME "telegram.png")
    install(FILES "../lib/xdg/telegramdesktop.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications" RENAME "${TDESKTOP_LAUNCHER_BASENAME}.desktop")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/telegramdesktop.appdata.xml" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo" RENAME "${TDESKTOP_LAUNCHER_BASENAME}.appdata.xml")
endif()
