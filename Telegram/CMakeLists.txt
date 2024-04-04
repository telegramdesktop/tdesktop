# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(Telegram WIN32 MACOSX_BUNDLE)
init_non_host_target(Telegram)

add_subdirectory(lib_rpl)
add_subdirectory(lib_crl)
add_subdirectory(lib_base)
add_subdirectory(lib_ui)
add_subdirectory(lib_tl)
add_subdirectory(lib_spellcheck)
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
include(cmake/lib_prisma.cmake)
include(cmake/td_export.cmake)
include(cmake/td_iv.cmake)
include(cmake/td_lang.cmake)
include(cmake/td_mtproto.cmake)
include(cmake/td_scheme.cmake)
include(cmake/td_ui.cmake)
include(cmake/generate_appdata_changelog.cmake)

if (WIN32)
    include(cmake/generate_midl.cmake)
    generate_midl(Telegram ${src_loc}
        platform/win/windows_quiethours.idl
        platform/win/windows_toastactivator.idl
    )

    nuget_add_winrt(Telegram)
endif()

set_target_properties(Telegram PROPERTIES AUTOMOC ON)

target_link_libraries(Telegram
PRIVATE
    tdesktop::lib_tgcalls_legacy
    tdesktop::lib_tgcalls
    tdesktop::lib_tgvoip

    # Order in this list defines the order of include paths in command line.
    # We need to place desktop-app::external_minizip this early to have its
    # include paths (usually ${PREFIX}/include/minizip) before any depend that
    # would add ${PREFIX}/include. This path may have a different <zip.h>,
    # for example installed by libzip (https://libzip.org).
    desktop-app::external_minizip

    tdesktop::td_export
    tdesktop::td_iv
    tdesktop::td_lang
    tdesktop::td_mtproto
    tdesktop::td_scheme
    tdesktop::td_ui
    desktop-app::lib_webrtc
    desktop-app::lib_base
    desktop-app::lib_crl
    desktop-app::lib_ui
    desktop-app::lib_tl
    desktop-app::lib_spellcheck
    desktop-app::lib_storage
    desktop-app::lib_lottie
    desktop-app::lib_qr
    desktop-app::lib_webview
    desktop-app::lib_ffmpeg
    desktop-app::lib_stripe
    desktop-app::external_rlottie
    desktop-app::external_zlib
    desktop-app::external_kcoreaddons
    desktop-app::external_qt_static_plugins
    desktop-app::external_qt
    desktop-app::external_qr_code_generator
    desktop-app::external_crash_reports
    desktop-app::external_auto_updates
    desktop-app::external_openssl
    desktop-app::external_openal
    desktop-app::external_xxhash
)

target_precompile_headers(Telegram PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${src_loc}/stdafx.h>)
nice_target_sources(Telegram ${src_loc}
PRIVATE
    ${style_files}

    api/api_attached_stickers.cpp
    api/api_attached_stickers.h
    api/api_authorizations.cpp
    api/api_authorizations.h
    api/api_blocked_peers.cpp
    api/api_blocked_peers.h
    api/api_bot.cpp
    api/api_bot.h
    api/api_chat_filters.cpp
    api/api_chat_filters.h
    api/api_chat_invite.cpp
    api/api_chat_invite.h
    api/api_chat_links.cpp
    api/api_chat_links.h
    api/api_chat_participants.cpp
    api/api_chat_participants.h
    api/api_cloud_password.cpp
    api/api_cloud_password.h
    api/api_common.cpp
    api/api_common.h
    api/api_confirm_phone.cpp
    api/api_confirm_phone.h
    api/api_earn.cpp
    api/api_earn.h
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
    api/api_messages_search.cpp
    api/api_messages_search.h
    api/api_messages_search_merged.cpp
    api/api_messages_search_merged.h
    api/api_peer_colors.cpp
    api/api_peer_colors.h
    api/api_peer_photo.cpp
    api/api_peer_photo.h
    api/api_polls.cpp
    api/api_polls.h
    api/api_premium.cpp
    api/api_premium.h
    api/api_premium_option.cpp
    api/api_premium_option.h
    api/api_report.cpp
    api/api_report.h
    api/api_ringtones.cpp
    api/api_ringtones.h
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
    api/api_statistics.cpp
    api/api_statistics.h
    api/api_text_entities.cpp
    api/api_text_entities.h
    api/api_toggling_media.cpp
    api/api_toggling_media.h
    api/api_transcribes.cpp
    api/api_transcribes.h
    api/api_unread_things.cpp
    api/api_unread_things.h
    api/api_updates.cpp
    api/api_updates.h
    api/api_user_names.cpp
    api/api_user_names.h
    api/api_user_privacy.cpp
    api/api_user_privacy.h
    api/api_views.cpp
    api/api_views.h
    api/api_websites.cpp
    api/api_websites.h
    api/api_who_reacted.cpp
    api/api_who_reacted.h
    boxes/filters/edit_filter_box.cpp
    boxes/filters/edit_filter_box.h
    boxes/filters/edit_filter_chats_list.cpp
    boxes/filters/edit_filter_chats_list.h
    boxes/filters/edit_filter_chats_preview.cpp
    boxes/filters/edit_filter_chats_preview.h
    boxes/filters/edit_filter_links.cpp
    boxes/filters/edit_filter_links.h
    boxes/peers/add_bot_to_chat_box.cpp
    boxes/peers/add_bot_to_chat_box.h
    boxes/peers/add_participants_box.cpp
    boxes/peers/add_participants_box.h
    boxes/peers/choose_peer_box.cpp
    boxes/peers/choose_peer_box.h
    boxes/peers/edit_contact_box.cpp
    boxes/peers/edit_contact_box.h
    boxes/peers/edit_forum_topic_box.cpp
    boxes/peers/edit_forum_topic_box.h
    boxes/peers/edit_linked_chat_box.cpp
    boxes/peers/edit_linked_chat_box.h
    boxes/peers/edit_members_visible.cpp
    boxes/peers/edit_members_visible.h
    boxes/peers/edit_participant_box.cpp
    boxes/peers/edit_participant_box.h
    boxes/peers/edit_participants_box.cpp
    boxes/peers/edit_participants_box.h
    boxes/peers/edit_peer_color_box.cpp
    boxes/peers/edit_peer_color_box.h
    boxes/peers/edit_peer_common.h
    boxes/peers/edit_peer_info_box.cpp
    boxes/peers/edit_peer_info_box.h
    boxes/peers/edit_peer_invite_link.cpp
    boxes/peers/edit_peer_invite_link.h
    boxes/peers/edit_peer_invite_links.cpp
    boxes/peers/edit_peer_invite_links.h
    boxes/peers/edit_peer_permissions_box.cpp
    boxes/peers/edit_peer_permissions_box.h
    boxes/peers/edit_peer_reactions.cpp
    boxes/peers/edit_peer_reactions.h
    boxes/peers/edit_peer_requests_box.cpp
    boxes/peers/edit_peer_requests_box.h
    boxes/peers/edit_peer_type_box.cpp
    boxes/peers/edit_peer_type_box.h
    boxes/peers/edit_peer_usernames_list.cpp
    boxes/peers/edit_peer_usernames_list.h
    boxes/peers/peer_short_info_box.cpp
    boxes/peers/peer_short_info_box.h
    boxes/peers/prepare_short_info_box.cpp
    boxes/peers/prepare_short_info_box.h
    boxes/peers/replace_boost_box.cpp
    boxes/peers/replace_boost_box.h
    boxes/about_box.cpp
    boxes/about_box.h
    boxes/about_sponsored_box.cpp
    boxes/about_sponsored_box.h
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
    boxes/choose_filter_box.cpp
    boxes/choose_filter_box.h
    boxes/connection_box.cpp
    boxes/connection_box.h
    boxes/create_poll_box.cpp
    boxes/create_poll_box.h
    boxes/delete_messages_box.cpp
    boxes/delete_messages_box.h
    boxes/dictionaries_manager.cpp
    boxes/dictionaries_manager.h
    boxes/download_path_box.cpp
    boxes/download_path_box.h
    boxes/edit_caption_box.cpp
    boxes/edit_caption_box.h
    boxes/edit_privacy_box.cpp
    boxes/edit_privacy_box.h
    boxes/gift_premium_box.cpp
    boxes/gift_premium_box.h
    boxes/language_box.cpp
    boxes/language_box.h
    boxes/local_storage_box.cpp
    boxes/local_storage_box.h
    boxes/max_invite_box.cpp
    boxes/max_invite_box.h
    boxes/peer_list_box.cpp
    boxes/peer_list_box.h
    boxes/peer_list_controllers.cpp
    boxes/peer_list_controllers.h
    boxes/peer_lists_box.cpp
    boxes/peer_lists_box.h
    boxes/passcode_box.cpp
    boxes/passcode_box.h
    boxes/phone_banned_box.cpp
    boxes/phone_banned_box.h
    boxes/pin_messages_box.cpp
    boxes/pin_messages_box.h
    boxes/premium_limits_box.cpp
    boxes/premium_limits_box.h
    boxes/premium_preview_box.cpp
    boxes/premium_preview_box.h
    boxes/reactions_settings_box.cpp
    boxes/reactions_settings_box.h
    boxes/report_messages_box.cpp
    boxes/report_messages_box.h
    boxes/ringtones_box.cpp
    boxes/ringtones_box.h
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
    boxes/translate_box.cpp
    boxes/translate_box.h
    boxes/url_auth_box.cpp
    boxes/url_auth_box.h
    boxes/username_box.cpp
    boxes/username_box.h
    calls/group/calls_choose_join_as.cpp
    calls/group/calls_choose_join_as.h
    calls/group/calls_cover_item.cpp
    calls/group/calls_cover_item.h
    calls/group/calls_group_call.cpp
    calls/group/calls_group_call.h
    calls/group/calls_group_common.cpp
    calls/group/calls_group_common.h
    calls/group/calls_group_invite_controller.cpp
    calls/group/calls_group_invite_controller.h
    calls/group/calls_group_members.cpp
    calls/group/calls_group_members.h
    calls/group/calls_group_members_row.cpp
    calls/group/calls_group_members_row.h
    calls/group/calls_group_menu.cpp
    calls/group/calls_group_menu.h
    calls/group/calls_group_panel.cpp
    calls/group/calls_group_panel.h
    calls/group/calls_group_rtmp.cpp
    calls/group/calls_group_rtmp.h
    calls/group/calls_group_settings.cpp
    calls/group/calls_group_settings.h
    calls/group/calls_group_toasts.cpp
    calls/group/calls_group_toasts.h
    calls/group/calls_group_viewport.cpp
    calls/group/calls_group_viewport.h
    calls/group/calls_group_viewport_opengl.cpp
    calls/group/calls_group_viewport_opengl.h
    calls/group/calls_group_viewport_raster.cpp
    calls/group/calls_group_viewport_raster.h
    calls/group/calls_group_viewport_tile.cpp
    calls/group/calls_group_viewport_tile.h
    calls/group/calls_volume_item.cpp
    calls/group/calls_volume_item.h
    calls/calls_box_controller.cpp
    calls/calls_box_controller.h
    calls/calls_call.cpp
    calls/calls_call.h
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
    calls/calls_video_incoming.cpp
    calls/calls_video_incoming.h
    chat_helpers/compose/compose_features.h
    chat_helpers/compose/compose_show.cpp
    chat_helpers/compose/compose_show.h
    chat_helpers/bot_command.cpp
    chat_helpers/bot_command.h
    chat_helpers/bot_keyboard.cpp
    chat_helpers/bot_keyboard.h
    chat_helpers/emoji_interactions.cpp
    chat_helpers/emoji_interactions.h
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
    chat_helpers/share_message_phrase_factory.cpp
    chat_helpers/share_message_phrase_factory.h
    chat_helpers/spellchecker_common.cpp
    chat_helpers/spellchecker_common.h
    chat_helpers/stickers_dice_pack.cpp
    chat_helpers/stickers_dice_pack.h
    chat_helpers/stickers_emoji_pack.cpp
    chat_helpers/stickers_emoji_pack.h
    chat_helpers/stickers_gift_box_pack.cpp
    chat_helpers/stickers_gift_box_pack.h
    chat_helpers/stickers_list_footer.cpp
    chat_helpers/stickers_list_footer.h
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
    chat_helpers/ttl_media_layer_widget.cpp
    chat_helpers/ttl_media_layer_widget.h
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
    core/core_settings_proxy.cpp
    core/core_settings_proxy.h
    core/crash_report_window.cpp
    core/crash_report_window.h
    core/crash_reports.cpp
    core/crash_reports.h
    core/deadlock_detector.h
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
    countries/countries_manager.cpp
    countries/countries_manager.h
    data/business/data_business_chatbots.cpp
    data/business/data_business_chatbots.h
    data/business/data_business_common.cpp
    data/business/data_business_common.h
    data/business/data_business_info.cpp
    data/business/data_business_info.h
    data/business/data_shortcut_messages.cpp
    data/business/data_shortcut_messages.h
    data/notify/data_notify_settings.cpp
    data/notify/data_notify_settings.h
    data/notify/data_peer_notify_settings.cpp
    data/notify/data_peer_notify_settings.h
    data/stickers/data_custom_emoji.cpp
    data/stickers/data_custom_emoji.h
    data/stickers/data_stickers_set.cpp
    data/stickers/data_stickers_set.h
    data/stickers/data_stickers.cpp
    data/stickers/data_stickers.h
    data/data_abstract_sparse_ids.h
    data/data_abstract_structure.cpp
    data/data_abstract_structure.h
    data/data_audio_msg_id.cpp
    data/data_audio_msg_id.h
    data/data_auto_download.cpp
    data/data_auto_download.h
    data/data_boosts.h
    data/data_bot_app.cpp
    data/data_bot_app.h
    data/data_chat.cpp
    data/data_chat.h
    data/data_chat_filters.cpp
    data/data_chat_filters.h
    data/data_chat_participant_status.cpp
    data/data_chat_participant_status.h
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
    data/data_document_resolver.cpp
    data/data_document_resolver.h
    data/data_download_manager.cpp
    data/data_download_manager.h
    data/data_drafts.cpp
    data/data_drafts.h
    data/data_emoji_statuses.cpp
    data/data_emoji_statuses.h
    data/data_folder.cpp
    data/data_folder.h
    data/data_forum.cpp
    data/data_forum.h
    data/data_forum_icons.cpp
    data/data_forum_icons.h
    data/data_forum_topic.cpp
    data/data_forum_topic.h
    data/data_file_click_handler.cpp
    data/data_file_click_handler.h
    data/data_file_origin.cpp
    data/data_file_origin.h
    data/data_flags.h
    data/data_game.cpp
    data/data_game.h
    data/data_group_call.cpp
    data/data_group_call.h
    data/data_groups.cpp
    data/data_groups.h
    data/data_histories.cpp
    data/data_histories.h
    data/data_lastseen_status.h
    data/data_location.cpp
    data/data_location.h
    data/data_media_rotation.cpp
    data/data_media_rotation.h
    data/data_media_types.cpp
    data/data_media_types.h
    # data/data_messages.cpp
    # data/data_messages.h
    data/data_message_reaction_id.cpp
    data/data_message_reaction_id.h
    data/data_message_reactions.cpp
    data/data_message_reactions.h
    data/data_msg_id.h
    data/data_peer.cpp
    data/data_peer.h
    data/data_peer_bot_command.cpp
    data/data_peer_bot_command.h
    data/data_peer_bot_commands.cpp
    data/data_peer_bot_commands.h
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
    data/data_premium_limits.cpp
    data/data_premium_limits.h
    data/data_pts_waiter.cpp
    data/data_pts_waiter.h
    data/data_replies_list.cpp
    data/data_replies_list.h
    data/data_reply_preview.cpp
    data/data_reply_preview.h
    data/data_saved_messages.cpp
    data/data_saved_messages.h
    data/data_saved_sublist.cpp
    data/data_saved_sublist.h
    data/data_search_controller.cpp
    data/data_search_controller.h
    data/data_send_action.cpp
    data/data_send_action.h
    data/data_session.cpp
    data/data_session.h
    data/data_scheduled_messages.cpp
    data/data_scheduled_messages.h
    data/data_shared_media.cpp
    data/data_shared_media.h
    data/data_sparse_ids.cpp
    data/data_sparse_ids.h
    data/data_sponsored_messages.cpp
    data/data_sponsored_messages.h
    data/data_statistics.h
    data/data_stories.cpp
    data/data_stories.h
    data/data_stories_ids.cpp
    data/data_stories_ids.h
    data/data_story.cpp
    data/data_story.h
    data/data_streaming.cpp
    data/data_streaming.h
    data/data_thread.cpp
    data/data_thread.h
    data/data_types.cpp
    data/data_types.h
    data/data_user.cpp
    data/data_user.h
    data/data_user_photos.cpp
    data/data_user_photos.h
    data/data_user_names.cpp
    data/data_user_names.h
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
    dialogs/dialogs_search_tags.cpp
    dialogs/dialogs_search_tags.h
    dialogs/dialogs_widget.cpp
    dialogs/dialogs_widget.h
    dialogs/ui/dialogs_layout.cpp
    dialogs/ui/dialogs_layout.h
    dialogs/ui/dialogs_message_view.cpp
    dialogs/ui/dialogs_message_view.h
    dialogs/ui/dialogs_stories_content.cpp
    dialogs/ui/dialogs_stories_content.h
    dialogs/ui/dialogs_topics_view.cpp
    dialogs/ui/dialogs_topics_view.h
    dialogs/ui/dialogs_video_userpic.cpp
    dialogs/ui/dialogs_video_userpic.h
    editor/color_picker.cpp
    editor/color_picker.h
    editor/controllers/controllers.h
    editor/controllers/stickers_panel_controller.cpp
    editor/controllers/stickers_panel_controller.h
    editor/editor_paint.cpp
    editor/editor_paint.h
    editor/photo_editor.cpp
    editor/photo_editor.h
    editor/photo_editor_content.cpp
    editor/photo_editor_content.h
    editor/photo_editor_controls.cpp
    editor/photo_editor_controls.h
    editor/photo_editor_layer_widget.cpp
    editor/photo_editor_layer_widget.h
    editor/scene/scene_item_sticker.cpp
    editor/scene/scene_item_sticker.h
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
    history/view/controls/history_view_compose_search.cpp
    history/view/controls/history_view_compose_search.h
    history/view/controls/history_view_draft_options.cpp
    history/view/controls/history_view_draft_options.h
    history/view/controls/history_view_forward_panel.cpp
    history/view/controls/history_view_forward_panel.h
    history/view/controls/history_view_ttl_button.cpp
    history/view/controls/history_view_ttl_button.h
    history/view/controls/history_view_voice_record_bar.cpp
    history/view/controls/history_view_voice_record_bar.h
    history/view/controls/history_view_webpage_processor.cpp
    history/view/controls/history_view_webpage_processor.h
    history/view/media/history_view_call.cpp
    history/view/media/history_view_call.h
    history/view/media/history_view_contact.cpp
    history/view/media/history_view_contact.h
    history/view/media/history_view_custom_emoji.cpp
    history/view/media/history_view_custom_emoji.h
    history/view/media/history_view_dice.cpp
    history/view/media/history_view_dice.h
    history/view/media/history_view_document.cpp
    history/view/media/history_view_document.h
    history/view/media/history_view_extended_preview.cpp
    history/view/media/history_view_extended_preview.h
    history/view/media/history_view_file.cpp
    history/view/media/history_view_file.h
    history/view/media/history_view_game.cpp
    history/view/media/history_view_game.h
    history/view/media/history_view_gif.cpp
    history/view/media/history_view_gif.h
    history/view/media/history_view_giveaway.cpp
    history/view/media/history_view_giveaway.h
    history/view/media/history_view_invoice.cpp
    history/view/media/history_view_invoice.h
    history/view/media/history_view_large_emoji.cpp
    history/view/media/history_view_large_emoji.h
    history/view/media/history_view_location.cpp
    history/view/media/history_view_location.h
    history/view/media/history_view_media.cpp
    history/view/media/history_view_media.h
    history/view/media/history_view_media_common.cpp
    history/view/media/history_view_media_common.h
    history/view/media/history_view_media_generic.cpp
    history/view/media/history_view_media_generic.h
    history/view/media/history_view_media_grouped.cpp
    history/view/media/history_view_media_grouped.h
    history/view/media/history_view_media_spoiler.cpp
    history/view/media/history_view_media_spoiler.h
    history/view/media/history_view_media_unwrapped.cpp
    history/view/media/history_view_media_unwrapped.h
    history/view/media/history_view_photo.cpp
    history/view/media/history_view_photo.h
    history/view/media/history_view_poll.cpp
    history/view/media/history_view_poll.h
    history/view/media/history_view_premium_gift.cpp
    history/view/media/history_view_premium_gift.h
    history/view/media/history_view_service_box.cpp
    history/view/media/history_view_service_box.h
    history/view/media/history_view_similar_channels.cpp
    history/view/media/history_view_similar_channels.h
    history/view/media/history_view_slot_machine.cpp
    history/view/media/history_view_slot_machine.h
    history/view/media/history_view_sticker.cpp
    history/view/media/history_view_sticker.h
    history/view/media/history_view_sticker_player.cpp
    history/view/media/history_view_sticker_player.h
    history/view/media/history_view_sticker_player_abstract.h
    history/view/media/history_view_story_mention.cpp
    history/view/media/history_view_story_mention.h
    history/view/media/history_view_theme_document.cpp
    history/view/media/history_view_theme_document.h
    history/view/media/history_view_userpic_suggestion.cpp
    history/view/media/history_view_userpic_suggestion.h
    history/view/media/history_view_web_page.cpp
    history/view/media/history_view_web_page.h
    history/view/reactions/history_view_reactions.cpp
    history/view/reactions/history_view_reactions.h
    history/view/reactions/history_view_reactions_button.cpp
    history/view/reactions/history_view_reactions_button.h
    history/view/reactions/history_view_reactions_list.cpp
    history/view/reactions/history_view_reactions_list.h
    history/view/reactions/history_view_reactions_selector.cpp
    history/view/reactions/history_view_reactions_selector.h
    history/view/reactions/history_view_reactions_strip.cpp
    history/view/reactions/history_view_reactions_strip.h
    history/view/reactions/history_view_reactions_tabs.cpp
    history/view/reactions/history_view_reactions_tabs.h
    history/view/history_view_about_view.cpp
    history/view/history_view_about_view.h
    history/view/history_view_bottom_info.cpp
    history/view/history_view_bottom_info.h
    history/view/history_view_contact_status.cpp
    history/view/history_view_contact_status.h
    history/view/history_view_context_menu.cpp
    history/view/history_view_context_menu.h
    history/view/history_view_corner_buttons.cpp
    history/view/history_view_corner_buttons.h
    history/view/history_view_cursor_state.cpp
    history/view/history_view_cursor_state.h
    history/view/history_view_element.cpp
    history/view/history_view_element.h
    history/view/history_view_emoji_interactions.cpp
    history/view/history_view_emoji_interactions.h
    history/view/history_view_empty_list_bubble.cpp
    history/view/history_view_empty_list_bubble.h
    history/view/history_view_group_call_bar.cpp
    history/view/history_view_group_call_bar.h
    history/view/history_view_item_preview.h
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
    history/view/history_view_quick_action.cpp
    history/view/history_view_quick_action.h
    history/view/history_view_replies_section.cpp
    history/view/history_view_replies_section.h
    history/view/history_view_reply.cpp
    history/view/history_view_reply.h
    history/view/history_view_requests_bar.cpp
    history/view/history_view_requests_bar.h
    history/view/history_view_schedule_box.cpp
    history/view/history_view_schedule_box.h
    history/view/history_view_scheduled_section.cpp
    history/view/history_view_scheduled_section.h
    history/view/history_view_send_action.cpp
    history/view/history_view_send_action.h
    history/view/history_view_service_message.cpp
    history/view/history_view_service_message.h
    history/view/history_view_spoiler_click_handler.cpp
    history/view/history_view_spoiler_click_handler.h
    history/view/history_view_sponsored_click_handler.cpp
    history/view/history_view_sponsored_click_handler.h
    history/view/history_view_sticker_toast.cpp
    history/view/history_view_sticker_toast.h
    history/view/history_view_sublist_section.cpp
    history/view/history_view_sublist_section.h
    history/view/history_view_transcribe_button.cpp
    history/view/history_view_transcribe_button.h
    history/view/history_view_translate_bar.cpp
    history/view/history_view_translate_bar.h
    history/view/history_view_translate_tracker.cpp
    history/view/history_view_translate_tracker.h
    history/view/history_view_top_bar_widget.cpp
    history/view/history_view_top_bar_widget.h
    history/view/history_view_view_button.cpp
    history/view/history_view_view_button.h
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
    history/history_item_edition.cpp
    history/history_item_edition.h
    history/history_item_helpers.cpp
    history/history_item_helpers.h
    history/history_item_reply_markup.cpp
    history/history_item_reply_markup.h
    history/history_item_text.cpp
    history/history_item_text.h
    history/history_inner_widget.cpp
    history/history_inner_widget.h
    history/history_location_manager.cpp
    history/history_location_manager.h
    history/history_translation.cpp
    history/history_translation.h
    history/history_unread_things.cpp
    history/history_unread_things.h
    history/history_view_highlight_manager.cpp
    history/history_view_highlight_manager.h
    history/history_widget.cpp
    history/history_widget.h
    info/channel_statistics/boosts/create_giveaway_box.cpp
    info/channel_statistics/boosts/create_giveaway_box.h
    info/channel_statistics/boosts/giveaway/giveaway_list_controllers.cpp
    info/channel_statistics/boosts/giveaway/giveaway_list_controllers.h
    info/channel_statistics/boosts/info_boosts_inner_widget.cpp
    info/channel_statistics/boosts/info_boosts_inner_widget.h
    info/channel_statistics/boosts/info_boosts_widget.cpp
    info/channel_statistics/boosts/info_boosts_widget.h
    info/channel_statistics/earn/info_earn_inner_widget.cpp
    info/channel_statistics/earn/info_earn_inner_widget.h
    info/channel_statistics/earn/info_earn_widget.cpp
    info/channel_statistics/earn/info_earn_widget.h
    info/common_groups/info_common_groups_inner_widget.cpp
    info/common_groups/info_common_groups_inner_widget.h
    info/common_groups/info_common_groups_widget.cpp
    info/common_groups/info_common_groups_widget.h
    info/downloads/info_downloads_inner_widget.cpp
    info/downloads/info_downloads_inner_widget.h
    info/downloads/info_downloads_provider.cpp
    info/downloads/info_downloads_provider.h
    info/downloads/info_downloads_widget.cpp
    info/downloads/info_downloads_widget.h
    info/media/info_media_buttons.h
    info/media/info_media_common.cpp
    info/media/info_media_common.h
    info/media/info_media_empty_widget.cpp
    info/media/info_media_empty_widget.h
    info/media/info_media_inner_widget.cpp
    info/media/info_media_inner_widget.h
    info/media/info_media_list_section.cpp
    info/media/info_media_list_section.h
    info/media/info_media_list_widget.cpp
    info/media/info_media_list_widget.h
    info/media/info_media_provider.cpp
    info/media/info_media_provider.h
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
    info/profile/info_profile_badge.cpp
    info/profile/info_profile_badge.h
    info/profile/info_profile_cover.cpp
    info/profile/info_profile_cover.h
    info/profile/info_profile_emoji_status_panel.cpp
    info/profile/info_profile_emoji_status_panel.h
    info/profile/info_profile_inner_widget.cpp
    info/profile/info_profile_inner_widget.h
    info/profile/info_profile_members.cpp
    info/profile/info_profile_members.h
    info/profile/info_profile_members_controllers.cpp
    info/profile/info_profile_members_controllers.h
    info/profile/info_profile_phone_menu.cpp
    info/profile/info_profile_phone_menu.h
    info/profile/info_profile_text.cpp
    info/profile/info_profile_text.h
    info/profile/info_profile_values.cpp
    info/profile/info_profile_values.h
    info/profile/info_profile_widget.cpp
    info/profile/info_profile_widget.h
    info/saved/info_saved_sublists_widget.cpp
    info/saved/info_saved_sublists_widget.h
    info/settings/info_settings_widget.cpp
    info/settings/info_settings_widget.h
    info/similar_channels/info_similar_channels_widget.cpp
    info/similar_channels/info_similar_channels_widget.h
    info/statistics/info_statistics_common.h
    info/statistics/info_statistics_inner_widget.cpp
    info/statistics/info_statistics_inner_widget.h
    info/statistics/info_statistics_list_controllers.cpp
    info/statistics/info_statistics_list_controllers.h
    info/statistics/info_statistics_recent_message.cpp
    info/statistics/info_statistics_recent_message.h
    info/statistics/info_statistics_widget.cpp
    info/statistics/info_statistics_widget.h
    info/stories/info_stories_inner_widget.cpp
    info/stories/info_stories_inner_widget.h
    info/stories/info_stories_provider.cpp
    info/stories/info_stories_provider.h
    info/stories/info_stories_widget.cpp
    info/stories/info_stories_widget.h
    info/userpic/info_userpic_colors_editor.cpp
    info/userpic/info_userpic_colors_editor.h
    info/userpic/info_userpic_emoji_builder.cpp
    info/userpic/info_userpic_emoji_builder.h
    info/userpic/info_userpic_emoji_builder_common.cpp
    info/userpic/info_userpic_emoji_builder_common.h
    info/userpic/info_userpic_emoji_builder_menu_item.cpp
    info/userpic/info_userpic_emoji_builder_menu_item.h
    info/userpic/info_userpic_emoji_builder_preview.cpp
    info/userpic/info_userpic_emoji_builder_preview.h
    info/userpic/info_userpic_emoji_builder_widget.cpp
    info/userpic/info_userpic_emoji_builder_widget.h
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
    inline_bots/bot_attach_web_view.cpp
    inline_bots/bot_attach_web_view.h
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
    iv/iv_delegate_impl.cpp
    iv/iv_delegate_impl.h
    iv/iv_instance.cpp
    iv/iv_instance.h
    lang/lang_cloud_manager.cpp
    lang/lang_cloud_manager.h
    lang/lang_instance.cpp
    lang/lang_instance.h
    lang/lang_numbers_animation.cpp
    lang/lang_numbers_animation.h
    lang/lang_translator.cpp
    lang/lang_translator.h
    layout/layout_document_generic_preview.cpp
    layout/layout_document_generic_preview.h
    layout/layout_item_base.cpp
    layout/layout_item_base.h
    main/main_account.cpp
    main/main_account.h
    main/main_app_config.cpp
    main/main_app_config.h
    main/main_app_config_values.cpp
    main/main_app_config_values.h
    main/main_domain.cpp
    main/main_domain.h
    main/main_session.cpp
    main/main_session.h
    main/main_session_settings.cpp
    main/main_session_settings.h
    main/session/send_as_peers.cpp
    main/session/send_as_peers.h
    main/session/session_show.cpp
    main/session/session_show.h
    media/audio/media_audio.cpp
    media/audio/media_audio.h
    media/audio/media_audio_capture.cpp
    media/audio/media_audio_capture.h
    media/audio/media_audio_capture_common.h
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
    media/stories/media_stories_caption_full_view.cpp
    media/stories/media_stories_caption_full_view.h
    media/stories/media_stories_controller.cpp
    media/stories/media_stories_controller.h
    media/stories/media_stories_delegate.cpp
    media/stories/media_stories_delegate.h
    media/stories/media_stories_header.cpp
    media/stories/media_stories_header.h
    media/stories/media_stories_reactions.cpp
    media/stories/media_stories_reactions.h
    media/stories/media_stories_recent_views.cpp
    media/stories/media_stories_recent_views.h
    media/stories/media_stories_reply.cpp
    media/stories/media_stories_reply.h
    media/stories/media_stories_repost_view.cpp
    media/stories/media_stories_repost_view.h
    media/stories/media_stories_share.cpp
    media/stories/media_stories_share.h
    media/stories/media_stories_sibling.cpp
    media/stories/media_stories_sibling.h
    media/stories/media_stories_slider.cpp
    media/stories/media_stories_slider.h
    media/stories/media_stories_stealth.cpp
    media/stories/media_stories_stealth.h
    media/stories/media_stories_view.cpp
    media/stories/media_stories_view.h
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
    media/view/media_view_overlay_opengl.cpp
    media/view/media_view_overlay_opengl.h
    media/view/media_view_overlay_raster.cpp
    media/view/media_view_overlay_raster.h
    media/view/media_view_overlay_renderer.h
    media/view/media_view_overlay_widget.cpp
    media/view/media_view_overlay_widget.h
    media/view/media_view_pip.cpp
    media/view/media_view_pip.h
    media/view/media_view_pip_opengl.cpp
    media/view/media_view_pip_opengl.h
    media/view/media_view_pip_raster.cpp
    media/view/media_view_pip_raster.h
    media/view/media_view_pip_renderer.h
    media/view/media_view_playback_controls.cpp
    media/view/media_view_playback_controls.h
    media/view/media_view_playback_progress.cpp
    media/view/media_view_playback_progress.h
    media/view/media_view_open_common.h
    media/system_media_controls_manager.h
    media/system_media_controls_manager.cpp
    menu/menu_antispam_validator.cpp
    menu/menu_antispam_validator.h
    menu/menu_item_download_files.cpp
    menu/menu_item_download_files.h
    menu/menu_mute.cpp
    menu/menu_mute.h
    menu/menu_send.cpp
    menu/menu_send.h
    menu/menu_sponsored.cpp
    menu/menu_sponsored.h
    menu/menu_ttl_validator.cpp
    menu/menu_ttl_validator.h
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
    platform/linux/linux_wayland_integration_dummy.cpp
    platform/linux/linux_wayland_integration.cpp
    platform/linux/linux_wayland_integration.h
    platform/linux/linux_xdp_open_with_dialog.cpp
    platform/linux/linux_xdp_open_with_dialog.h
    platform/linux/file_utilities_linux.cpp
    platform/linux/file_utilities_linux.h
    platform/linux/launcher_linux.cpp
    platform/linux/launcher_linux.h
    platform/linux/integration_linux.cpp
    platform/linux/integration_linux.h
    platform/linux/main_window_linux.cpp
    platform/linux/main_window_linux.h
    platform/linux/notifications_manager_linux.cpp
    platform/linux/notifications_manager_linux.h
    platform/linux/overlay_widget_linux.h
    platform/linux/specific_linux.cpp
    platform/linux/specific_linux.h
    platform/linux/tray_linux.cpp
    platform/linux/tray_linux.h
    platform/mac/file_utilities_mac.mm
    platform/mac/file_utilities_mac.h
    platform/mac/launcher_mac.mm
    platform/mac/launcher_mac.h
    platform/mac/integration_mac.mm
    platform/mac/integration_mac.h
    platform/mac/mac_iconv_helper.c
    platform/mac/main_window_mac.mm
    platform/mac/main_window_mac.h
    platform/mac/notifications_manager_mac.mm
    platform/mac/notifications_manager_mac.h
    platform/mac/overlay_widget_mac.h
    platform/mac/overlay_widget_mac.mm
    platform/mac/specific_mac.mm
    platform/mac/specific_mac.h
    platform/mac/specific_mac_p.mm
    platform/mac/specific_mac_p.h
    platform/mac/tray_mac.mm
    platform/mac/tray_mac.h
    platform/mac/window_title_mac.mm
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
    platform/win/file_utilities_win.cpp
    platform/win/file_utilities_win.h
    platform/win/launcher_win.cpp
    platform/win/launcher_win.h
    platform/win/integration_win.cpp
    platform/win/integration_win.h
    platform/win/main_window_win.cpp
    platform/win/main_window_win.h
    platform/win/notifications_manager_win.cpp
    platform/win/notifications_manager_win.h
    platform/win/overlay_widget_win.h
    platform/win/specific_win.cpp
    platform/win/specific_win.h
    platform/win/tray_win.cpp
    platform/win/tray_win.h
    platform/win/windows_app_user_model_id.cpp
    platform/win/windows_app_user_model_id.h
    platform/win/windows_dlls.cpp
    platform/win/windows_dlls.h
    platform/win/windows_autostart_task.cpp
    platform/win/windows_autostart_task.h
    platform/win/windows_toast_activator.cpp
    platform/win/windows_toast_activator.h
    platform/platform_file_utilities.h
    platform/platform_launcher.h
    platform/platform_integration.cpp
    platform/platform_integration.h
    platform/platform_main_window.h
    platform/platform_notifications_manager.h
    platform/platform_overlay_widget.cpp
    platform/platform_overlay_widget.h
    platform/platform_specific.h
    platform/platform_tray.h
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
    settings/business/settings_away_message.cpp
    settings/business/settings_away_message.h
    settings/business/settings_shortcut_messages.cpp
    settings/business/settings_shortcut_messages.h
    settings/business/settings_chat_intro.cpp
    settings/business/settings_chat_intro.h
    settings/business/settings_chat_links.cpp
    settings/business/settings_chat_links.h
    settings/business/settings_chatbots.cpp
    settings/business/settings_chatbots.h
    settings/business/settings_greeting.cpp
    settings/business/settings_greeting.h
    settings/business/settings_location.cpp
    settings/business/settings_location.h
    settings/business/settings_quick_replies.cpp
    settings/business/settings_quick_replies.h
    settings/business/settings_recipients_helper.cpp
    settings/business/settings_recipients_helper.h
    settings/business/settings_working_hours.cpp
    settings/business/settings_working_hours.h
    settings/cloud_password/settings_cloud_password_common.cpp
    settings/cloud_password/settings_cloud_password_common.h
    settings/cloud_password/settings_cloud_password_email.cpp
    settings/cloud_password/settings_cloud_password_email.h
    settings/cloud_password/settings_cloud_password_email_confirm.cpp
    settings/cloud_password/settings_cloud_password_email_confirm.h
    settings/cloud_password/settings_cloud_password_hint.cpp
    settings/cloud_password/settings_cloud_password_hint.h
    settings/cloud_password/settings_cloud_password_input.cpp
    settings/cloud_password/settings_cloud_password_input.h
    settings/cloud_password/settings_cloud_password_manage.cpp
    settings/cloud_password/settings_cloud_password_manage.h
    settings/cloud_password/settings_cloud_password_start.cpp
    settings/cloud_password/settings_cloud_password_start.h
    settings/settings_advanced.cpp
    settings/settings_advanced.h
    settings/settings_blocked_peers.cpp
    settings/settings_blocked_peers.h
    settings/settings_business.cpp
    settings/settings_business.h
    settings/settings_chat.cpp
    settings/settings_chat.h
    settings/settings_calls.cpp
    settings/settings_calls.h
    settings/settings_codes.cpp
    settings/settings_codes.h
    settings/settings_common_session.cpp
    settings/settings_common_session.h
    settings/settings_experimental.cpp
    settings/settings_experimental.h
    settings/settings_folders.cpp
    settings/settings_folders.h
    settings/settings_global_ttl.cpp
    settings/settings_global_ttl.h
    settings/settings_information.cpp
    settings/settings_information.h
    settings/settings_intro.cpp
    settings/settings_intro.h
    settings/settings_local_passcode.cpp
    settings/settings_local_passcode.h
    settings/settings_main.cpp
    settings/settings_main.h
    settings/settings_notifications.cpp
    settings/settings_notifications.h
    settings/settings_notifications_type.cpp
    settings/settings_notifications_type.h
    settings/settings_power_saving.cpp
    settings/settings_power_saving.h
    settings/settings_premium.cpp
    settings/settings_premium.h
    settings/settings_privacy_controllers.cpp
    settings/settings_privacy_controllers.h
    settings/settings_privacy_security.cpp
    settings/settings_privacy_security.h
    settings/settings_scale_preview.cpp
    settings/settings_scale_preview.h
    settings/settings_type.h
    settings/settings_websites.cpp
    settings/settings_websites.h
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
    support/support_preload.cpp
    support/support_preload.h
    support/support_templates.cpp
    support/support_templates.h
    ui/chat/attach/attach_item_single_file_preview.cpp
    ui/chat/attach/attach_item_single_file_preview.h
    ui/chat/attach/attach_item_single_media_preview.cpp
    ui/chat/attach/attach_item_single_media_preview.h
    ui/chat/choose_send_as.cpp
    ui/chat/choose_send_as.h
    ui/chat/choose_theme_controller.cpp
    ui/chat/choose_theme_controller.h
    ui/controls/silent_toggle.cpp
    ui/controls/silent_toggle.h
    ui/controls/userpic_button.cpp
    ui/controls/userpic_button.h
    ui/effects/emoji_fly_animation.cpp
    ui/effects/emoji_fly_animation.h
    ui/effects/message_sending_animation_common.h
    ui/effects/message_sending_animation_controller.cpp
    ui/effects/message_sending_animation_controller.h
    ui/effects/reaction_fly_animation.cpp
    ui/effects/reaction_fly_animation.h
    ui/effects/send_action_animations.cpp
    ui/effects/send_action_animations.h
    ui/image/image.cpp
    ui/image/image.h
    ui/image/image_location.cpp
    ui/image/image_location.h
    ui/image/image_location_factory.cpp
    ui/image/image_location_factory.h
    ui/widgets/level_meter.cpp
    ui/widgets/level_meter.h
    ui/countryinput.cpp
    ui/countryinput.h
    ui/dynamic_thumbnails.cpp
    ui/dynamic_thumbnails.h
    ui/filter_icons.cpp
    ui/filter_icons.h
    ui/filter_icon_panel.cpp
    ui/filter_icon_panel.h
    ui/item_text_options.cpp
    ui/item_text_options.h
    ui/resize_area.h
    ui/search_field_controller.cpp
    ui/search_field_controller.h
    ui/text/format_song_document_name.cpp
    ui/text/format_song_document_name.h
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
    window/window_adaptive.cpp
    window/window_adaptive.h
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
    window/window_peer_menu.cpp
    window/window_peer_menu.h
    window/window_section_common.h
    window/window_session_controller.cpp
    window/window_session_controller.h
    window/window_session_controller_link_info.h
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
    config.h
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
    tray.cpp
    tray.h
)

if (NOT build_winstore)
    remove_target_sources(Telegram ${src_loc}
        platform/win/windows_start_task.cpp
        platform/win/windows_start_task.h
    )
endif()

if (DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION)
    remove_target_sources(Telegram ${src_loc}
        platform/linux/linux_wayland_integration.cpp
    )
else()
    remove_target_sources(Telegram ${src_loc}
        platform/linux/linux_wayland_integration_dummy.cpp
    )
endif()

if (DESKTOP_APP_USE_PACKAGED)
    remove_target_sources(Telegram ${src_loc}
        platform/mac/mac_iconv_helper.c
    )
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
    qrc/emoji_8.qrc
    qrc/emoji_preview.qrc
    qrc/telegram/animations.qrc
    qrc/telegram/export.qrc
    qrc/telegram/iv.qrc
    qrc/telegram/telegram.qrc
    qrc/telegram/sounds.qrc
    winrc/Telegram.rc
    winrc/Telegram.manifest
    langs/lang.strings
    langs/cloud_lang.strings
    numbers.txt
)

if (APPLE AND NOT build_macstore)
    nice_target_sources(Telegram ${res_loc}
    PRIVATE
        qrc/telegram/mac_icons.qrc
    )
endif()

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
    if (NOT DESKTOP_APP_USE_PACKAGED)
        target_link_libraries(Telegram PRIVATE desktop-app::external_iconv)
    endif()

    set(icons_path ${CMAKE_CURRENT_SOURCE_DIR}/Telegram/Images.xcassets)
    if (CMAKE_GENERATOR STREQUAL Xcode)
        target_add_resource(Telegram ${icons_path})
    else()
        set(icon_path ${icons_path}/Icon.iconset)
        find_program(ICONUTIL iconutil)
        find_program(PNG2ICNS png2icns)
        if (ICONUTIL)
            add_custom_command(
                OUTPUT Icon.icns
                COMMAND ${ICONUTIL}
                ARGS
                    --convert icns
                    --output Icon.icns
                    ${icon_path}
            )
        elseif (PNG2ICNS)
            add_custom_command(
                OUTPUT Icon.icns
                COMMAND ${PNG2ICNS}
                ARGS
                    Icon.icns
                    ${icon_path}/icon_16x16.png
                    ${icon_path}/icon_32x32.png
                    ${icon_path}/icon_128x128.png
                    ${icon_path}/icon_256x256.png
                    ${icon_path}/icon_512x512.png
            )
        endif()
        if (ICONUTIL OR PNG2ICNS)
            set_source_files_properties(Icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
            target_add_resource(Telegram Icon.icns)
        endif()
    endif()

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

    add_custom_command(TARGET Telegram
    PRE_LINK
        COMMAND mkdir -p $<TARGET_FILE_DIR:Telegram>/../Resources
        COMMAND cp ${CMAKE_BINARY_DIR}/lib_ui.rcc $<TARGET_FILE_DIR:Telegram>/../Resources
        COMMAND cp ${CMAKE_BINARY_DIR}/lib_spellcheck.rcc $<TARGET_FILE_DIR:Telegram>/../Resources
    )
    if (NOT build_macstore AND NOT DESKTOP_APP_DISABLE_CRASH_REPORTS)
        if (DESKTOP_APP_MAC_ARCH STREQUAL "x86_64" OR DESKTOP_APP_MAC_ARCH STREQUAL "arm64")
            set(crashpad_dir_part ".${DESKTOP_APP_MAC_ARCH}")
        else()
            set(crashpad_dir_part "")
        endif()
        add_custom_command(TARGET Telegram
        PRE_LINK
            COMMAND mkdir -p $<TARGET_FILE_DIR:Telegram>/../Helpers
            COMMAND cp ${libs_loc}/crashpad/out/$<IF:$<CONFIG:Debug>,Debug,Release>${crashpad_dir_part}/crashpad_handler $<TARGET_FILE_DIR:Telegram>/../Helpers/
        )
    endif()
else()
    include(${cmake_helpers_loc}/external/glib/generate_dbus.cmake)
    generate_dbus(Telegram org.freedesktop.portal. XdpBackground ${third_party_loc}/xdg-desktop-portal/data/org.freedesktop.portal.Background.xml)
    generate_dbus(Telegram org.freedesktop. XdgNotifications ${src_loc}/platform/linux/org.freedesktop.Notifications.xml)

    if (NOT DESKTOP_APP_DISABLE_X11_INTEGRATION)
        target_link_libraries(Telegram
        PRIVATE
            desktop-app::external_xcb
        )
    endif()

    if (NOT DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION)
        qt_generate_wayland_protocol_client_sources(Telegram
        FILES
            ${third_party_loc}/wayland/protocol/wayland.xml
            ${third_party_loc}/plasma-wayland-protocols/src/protocols/plasma-shell.xml
        )

        target_link_libraries(Telegram
        PRIVATE
            desktop-app::external_wayland_client
        )
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
        COMMAND rm -rf $<TARGET_FILE_DIR:Telegram>/../Frameworks/Breakpad.framework/Resources/crash_report_sender.app
        COMMAND rm -rf $<TARGET_FILE_DIR:Telegram>/../Frameworks/Breakpad.framework/Resources/Inspector
    )
else()
    if (CMAKE_GENERATOR STREQUAL Xcode)
        set(bundle_identifier "com.tdesktop.Telegram$<$<CONFIG:Debug>:Debug>")
    else()
        set(bundle_identifier "com.tdesktop.Telegram")
    endif()
    set(bundle_entitlements "Telegram.entitlements")
    if (LINUX AND DESKTOP_APP_USE_PACKAGED)
        set(output_name "telegram-desktop")
    else()
        set(output_name "Telegram")
    endif()
endif()

if (CMAKE_GENERATOR STREQUAL Xcode)
    set(bundle_identifier_plist "$(PRODUCT_BUNDLE_IDENTIFIER)")
else()
    set(bundle_identifier_plist ${bundle_identifier})
endif()

set_target_properties(Telegram PROPERTIES
    OUTPUT_NAME ${output_name}
    MACOSX_BUNDLE_GUI_IDENTIFIER ${bundle_identifier}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Telegram.plist
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/Telegram/${bundle_entitlements}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${bundle_identifier}
    XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION ${desktop_app_version_string}
    XCODE_ATTRIBUTE_PRODUCT_NAME ${output_name}
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
    XCODE_ATTRIBUTE_COMBINE_HIDPI_IMAGES YES
    XCODE_ATTRIBUTE_COPY_PHASE_STRIP NO
    XCODE_ATTRIBUTE_ALWAYS_SEARCH_USER_PATHS NO
    XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY libc++
    XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS --deep
    XCODE_ATTRIBUTE_CLANG_DEBUG_INFORMATION_LEVEL $<IF:$<CONFIG:Debug>,default,line-tables-only>
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

if (APPLE
    OR "${CMAKE_GENERATOR}" STREQUAL "Ninja Multi-Config"
    OR NOT CMAKE_EXECUTABLE_SUFFIX STREQUAL ""
    OR NOT "${output_name}" STREQUAL "Telegram")
    set(output_folder ${CMAKE_BINARY_DIR})
else()
    set(output_folder ${CMAKE_BINARY_DIR}/bin)
endif()

set_target_properties(Telegram PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${output_folder})

if (WIN32)
    target_link_libraries(Telegram
    PRIVATE
        delayimp
    )
    target_link_options(Telegram
    PRIVATE
        /DELAYLOAD:secur32.dll
        /DELAYLOAD:winmm.dll
        /DELAYLOAD:ws2_32.dll
        /DELAYLOAD:user32.dll
        /DELAYLOAD:gdi32.dll
        /DELAYLOAD:advapi32.dll
        /DELAYLOAD:shell32.dll
        /DELAYLOAD:ole32.dll
        /DELAYLOAD:oleaut32.dll
        /DELAYLOAD:shlwapi.dll
        /DELAYLOAD:iphlpapi.dll
        /DELAYLOAD:gdiplus.dll
        /DELAYLOAD:version.dll
        /DELAYLOAD:dwmapi.dll
        /DELAYLOAD:uxtheme.dll
        /DELAYLOAD:crypt32.dll
        /DELAYLOAD:bcrypt.dll
        /DELAYLOAD:imm32.dll
        /DELAYLOAD:netapi32.dll
        /DELAYLOAD:userenv.dll
        /DELAYLOAD:wtsapi32.dll
        /DELAYLOAD:propsys.dll
    )
endif()

target_prepare_qrc(Telegram)

if (NOT DESKTOP_APP_DISABLE_AUTOUPDATE AND NOT build_macstore AND NOT build_winstore)
    add_executable(Updater WIN32)
    init_non_host_target(Updater)

    add_dependencies(Telegram Updater)

    nice_target_sources(Updater ${src_loc}
    PRIVATE
        _other/updater_win.cpp
        _other/updater_linux.cpp
        _other/updater_osx.m
        _other/updater.h
    )

    set_target_properties(Updater PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${output_folder}
    )

    if (WIN32)
        get_filename_component(lib_base_loc lib_base REALPATH)
        nice_target_sources(Updater ${lib_base_loc}
        PRIVATE
            base/platform/win/base_windows_safe_library.cpp
            base/platform/win/base_windows_safe_library.h
        )
        target_include_directories(Updater PRIVATE ${lib_base_loc})
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_link_libraries(Updater
            PRIVATE
                delayimp
            )
            target_link_options(Updater
            PRIVATE
                /DELAYLOAD:user32.dll
                /DELAYLOAD:advapi32.dll
                /DELAYLOAD:shell32.dll
                /DELAYLOAD:ole32.dll
                /DELAYLOAD:shlwapi.dll
            )
        else()
            target_link_options(Updater PRIVATE -municode)
        endif()
    elseif (APPLE)
        add_custom_command(TARGET Updater
        PRE_LINK
            COMMAND mkdir -p $<TARGET_FILE_DIR:Telegram>/../Frameworks
            COMMAND cp $<TARGET_FILE:Updater> $<TARGET_FILE_DIR:Telegram>/../Frameworks/
        )
    endif()

    if (DESKTOP_APP_SPECIAL_TARGET)
        add_executable(Packer)
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
elseif (build_winstore)
    add_executable(StartupTask WIN32)
    init_non_host_target(StartupTask)

    add_dependencies(Telegram StartupTask)

    nice_target_sources(StartupTask ${src_loc}
    PRIVATE
        _other/startup_task_win.cpp
    )

    set_target_properties(StartupTask PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${output_folder}
    )
endif()

if (LINUX AND DESKTOP_APP_USE_PACKAGED)
    include(GNUInstallDirs)
    configure_file("../lib/xdg/org.telegram.desktop.service" "${CMAKE_CURRENT_BINARY_DIR}/org.telegram.desktop.service" @ONLY)
    configure_file("../lib/xdg/org.telegram.desktop.metainfo.xml" "${CMAKE_CURRENT_BINARY_DIR}/org.telegram.desktop.metainfo.xml" @ONLY)
    generate_appdata_changelog(Telegram "${CMAKE_SOURCE_DIR}/changelog.txt" "${CMAKE_CURRENT_BINARY_DIR}/org.telegram.desktop.metainfo.xml")
    install(TARGETS Telegram RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "Resources/art/icon16.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/16x16/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon32.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/32x32/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon48.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/48x48/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon64.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/64x64/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon128.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon256.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps" RENAME "telegram.png")
    install(FILES "Resources/art/icon512.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/512x512/apps" RENAME "telegram.png")
    install(FILES "Resources/icons/tray_monochrome.svg" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/symbolic/apps" RENAME "telegram-symbolic.svg")
    install(FILES "../lib/xdg/org.telegram.desktop.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.telegram.desktop.service" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/services")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.telegram.desktop.metainfo.xml" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
endif()
