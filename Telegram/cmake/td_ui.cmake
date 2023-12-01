# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_ui OBJECT)
init_non_host_target(td_ui)
add_library(tdesktop::td_ui ALIAS td_ui)

include(lib_ui/cmake/generate_styles.cmake)
include(cmake/generate_numbers.cmake)

set(style_files
    ui/td_common.style
    ui/filter_icons.style
    ui/menu_icons.style
    ui/chat/chat.style
    ui/effects/premium.style
    boxes/boxes.style
    dialogs/dialogs.style
    chat_helpers/chat_helpers.style
    calls/calls.style
    export/view/export.style
    info/info.style
    info/boosts/giveaway/giveaway.style
    info/userpic/info_userpic_builder.style
    intro/intro.style
    media/player/media_player.style
    passport/passport.style
    payments/ui/payments.style
    profile/profile.style
    settings/settings.style
    media/view/media_view.style
    overview/overview.style
    window/window.style
    editor/editor.style
    statistics/statistics.style
)

set(dependent_style_files
    ${submodules_loc}/lib_ui/ui/colors.palette
    ${submodules_loc}/lib_ui/ui/basic.style
    ${submodules_loc}/lib_ui/ui/layers/layers.style
    ${submodules_loc}/lib_ui/ui/widgets/widgets.style
)

generate_styles(td_ui ${src_loc} "${style_files}" "${dependent_style_files}")

target_precompile_headers(td_ui PRIVATE ${src_loc}/ui/ui_pch.h)
nice_target_sources(td_ui ${src_loc}
PRIVATE
    ${style_files}

    boxes/peers/edit_peer_history_visibility_box.cpp
    boxes/peers/edit_peer_history_visibility_box.h

    calls/group/ui/calls_group_recording_box.cpp
    calls/group/ui/calls_group_recording_box.h
    calls/group/ui/calls_group_scheduled_labels.cpp
    calls/group/ui/calls_group_scheduled_labels.h
    calls/group/ui/desktop_capture_choose_source.cpp
    calls/group/ui/desktop_capture_choose_source.h

    core/file_location.cpp
    core/file_location.h
    core/mime_type.cpp
    core/mime_type.h

    countries/countries_instance.cpp
    countries/countries_instance.h

    data/data_statistics_chart.cpp
    data/data_statistics_chart.h
    data/data_subscription_option.h

    dialogs/dialogs_three_state_icon.h
    dialogs/ui/dialogs_stories_list.cpp
    dialogs/ui/dialogs_stories_list.h

    editor/controllers/undo_controller.cpp
    editor/controllers/undo_controller.h
    editor/editor_crop.cpp
    editor/editor_crop.h
    editor/editor_layer_widget.cpp
    editor/editor_layer_widget.h
    editor/photo_editor_common.cpp
    editor/photo_editor_common.h
    editor/photo_editor_inner_common.h
    editor/scene/scene.cpp
    editor/scene/scene.h
    editor/scene/scene_item_base.cpp
    editor/scene/scene_item_base.h
    editor/scene/scene_item_canvas.cpp
    editor/scene/scene_item_canvas.h
    editor/scene/scene_item_image.cpp
    editor/scene/scene_item_image.h
    editor/scene/scene_item_line.cpp
    editor/scene/scene_item_line.h

    history/history_view_top_toast.cpp
    history/history_view_top_toast.h

    info/profile/info_profile_icon.cpp
    info/profile/info_profile_icon.h
    info/userpic/info_userpic_bubble_wrap.cpp
    info/userpic/info_userpic_bubble_wrap.h
    info/userpic/info_userpic_color_circle_button.cpp
    info/userpic/info_userpic_color_circle_button.h
    info/userpic/info_userpic_emoji_builder_layer.cpp
    info/userpic/info_userpic_emoji_builder_layer.h

    info/boosts/giveaway/boost_badge.cpp
    info/boosts/giveaway/boost_badge.h
    info/boosts/giveaway/giveaway_type_row.cpp
    info/boosts/giveaway/giveaway_type_row.h
    info/boosts/giveaway/select_countries_box.cpp
    info/boosts/giveaway/select_countries_box.h

    intro/intro_code_input.cpp
    intro/intro_code_input.h

    layout/abstract_layout_item.cpp
    layout/abstract_layout_item.h
    layout/layout_mosaic.cpp
    layout/layout_mosaic.h
    layout/layout_position.cpp
    layout/layout_position.h
    layout/layout_selection.cpp
    layout/layout_selection.h

    media/clip/media_clip_check_streaming.cpp
    media/clip/media_clip_check_streaming.h
    media/clip/media_clip_ffmpeg.cpp
    media/clip/media_clip_ffmpeg.h
    media/clip/media_clip_implementation.cpp
    media/clip/media_clip_implementation.h
    media/clip/media_clip_reader.cpp
    media/clip/media_clip_reader.h

    media/player/media_player_button.cpp
    media/player/media_player_button.h
    media/player/media_player_dropdown.cpp
    media/player/media_player_dropdown.h

    media/media_common.h

    menu/menu_check_item.cpp
    menu/menu_check_item.h
    menu/menu_ttl.cpp
    menu/menu_ttl.h

    passport/ui/passport_details_row.cpp
    passport/ui/passport_details_row.h
    passport/ui/passport_form_row.cpp
    passport/ui/passport_form_row.h

    payments/ui/payments_edit_card.cpp
    payments/ui/payments_edit_card.h
    payments/ui/payments_edit_information.cpp
    payments/ui/payments_edit_information.h
    payments/ui/payments_form_summary.cpp
    payments/ui/payments_form_summary.h
    payments/ui/payments_field.cpp
    payments/ui/payments_field.h
    payments/ui/payments_panel.cpp
    payments/ui/payments_panel.h
    payments/ui/payments_panel_data.h
    payments/ui/payments_panel_delegate.h

    platform/mac/file_bookmark_mac.h
    platform/mac/file_bookmark_mac.mm
    platform/platform_file_bookmark.h

    settings/settings_common.cpp
    settings/settings_common.h

    statistics/chart_lines_filter_controller.cpp
    statistics/chart_lines_filter_controller.h
    statistics/chart_rulers_data.cpp
    statistics/chart_rulers_data.h
    statistics/chart_widget.cpp
    statistics/chart_widget.h
    statistics/segment_tree.cpp
    statistics/segment_tree.h
    statistics/statistics_common.h
    statistics/statistics_data_deserialize.cpp
    statistics/statistics_data_deserialize.h
    statistics/statistics_format_values.cpp
    statistics/statistics_format_values.h
    statistics/view/abstract_chart_view.cpp
    statistics/view/abstract_chart_view.h
    statistics/view/bar_chart_view.cpp
    statistics/view/bar_chart_view.h
    statistics/view/chart_rulers_view.cpp
    statistics/view/chart_rulers_view.h
    statistics/view/chart_view_factory.cpp
    statistics/view/chart_view_factory.h
    statistics/view/linear_chart_view.cpp
    statistics/view/linear_chart_view.h
    statistics/view/stack_chart_common.cpp
    statistics/view/stack_chart_common.h
    statistics/view/stack_linear_chart_common.cpp
    statistics/view/stack_linear_chart_common.h
    statistics/view/stack_linear_chart_view.cpp
    statistics/view/stack_linear_chart_view.h
    statistics/widgets/chart_header_widget.cpp
    statistics/widgets/chart_header_widget.h
    statistics/widgets/chart_lines_filter_widget.cpp
    statistics/widgets/chart_lines_filter_widget.h
    statistics/widgets/point_details_widget.cpp
    statistics/widgets/point_details_widget.h

    ui/boxes/auto_delete_settings.cpp
    ui/boxes/auto_delete_settings.h
    ui/boxes/boost_box.cpp
    ui/boxes/boost_box.h
    ui/boxes/calendar_box.cpp
    ui/boxes/calendar_box.h
    ui/boxes/choose_date_time.cpp
    ui/boxes/choose_date_time.h
    ui/boxes/choose_language_box.cpp
    ui/boxes/choose_language_box.h
    ui/boxes/choose_time.cpp
    ui/boxes/choose_time.h
    ui/boxes/confirm_box.cpp
    ui/boxes/confirm_box.h
    ui/boxes/confirm_phone_box.cpp
    ui/boxes/confirm_phone_box.h
    ui/boxes/country_select_box.cpp
    ui/boxes/country_select_box.h
    ui/boxes/edit_invite_link.cpp
    ui/boxes/edit_invite_link.h
    ui/boxes/rate_call_box.cpp
    ui/boxes/rate_call_box.h
    ui/boxes/report_box.cpp
    ui/boxes/report_box.h
    ui/boxes/single_choice_box.cpp
    ui/boxes/single_choice_box.h
    ui/boxes/time_picker_box.cpp
    ui/boxes/time_picker_box.h

    ui/chat/attach/attach_abstract_single_file_preview.cpp
    ui/chat/attach/attach_abstract_single_file_preview.h
    ui/chat/attach/attach_abstract_single_media_preview.cpp
    ui/chat/attach/attach_abstract_single_media_preview.h
    ui/chat/attach/attach_abstract_single_preview.h
    ui/chat/attach/attach_album_preview.cpp
    ui/chat/attach/attach_album_preview.h
    ui/chat/attach/attach_album_thumbnail.cpp
    ui/chat/attach/attach_album_thumbnail.h
    ui/chat/attach/attach_bot_webview.cpp
    ui/chat/attach/attach_bot_webview.h
    ui/chat/attach/attach_controls.cpp
    ui/chat/attach/attach_controls.h
    ui/chat/attach/attach_extensions.cpp
    ui/chat/attach/attach_extensions.h
    ui/chat/attach/attach_prepare.cpp
    ui/chat/attach/attach_prepare.h
    ui/chat/attach/attach_send_files_way.cpp
    ui/chat/attach/attach_send_files_way.h
    ui/chat/attach/attach_single_file_preview.cpp
    ui/chat/attach/attach_single_file_preview.h
    ui/chat/attach/attach_single_media_preview.cpp
    ui/chat/attach/attach_single_media_preview.h
    ui/chat/chat_style.cpp
    ui/chat/chat_style.h
    ui/chat/chat_style_radius.cpp
    ui/chat/chat_style_radius.h
    ui/chat/chat_theme.cpp
    ui/chat/chat_theme.h
    ui/chat/continuous_scroll.cpp
    ui/chat/continuous_scroll.h
    ui/chat/forward_options_box.cpp
    ui/chat/forward_options_box.h
    ui/chat/group_call_bar.cpp
    ui/chat/group_call_bar.h
    ui/chat/group_call_userpics.cpp
    ui/chat/group_call_userpics.h
    ui/chat/message_bar.cpp
    ui/chat/message_bar.h
    ui/chat/message_bubble.cpp
    ui/chat/message_bubble.h
    ui/chat/more_chats_bar.cpp
    ui/chat/more_chats_bar.h
    ui/chat/pinned_bar.cpp
    ui/chat/pinned_bar.h
    ui/chat/requests_bar.cpp
    ui/chat/requests_bar.h
    ui/controls/call_mute_button.cpp
    ui/controls/call_mute_button.h
    ui/controls/chat_service_checkbox.cpp
    ui/controls/chat_service_checkbox.h
    ui/controls/delete_message_context_action.cpp
    ui/controls/delete_message_context_action.h
    ui/controls/download_bar.cpp
    ui/controls/download_bar.h
    ui/controls/emoji_button.cpp
    ui/controls/emoji_button.h
    ui/controls/filter_link_header.cpp
    ui/controls/filter_link_header.h
    ui/controls/jump_down_button.cpp
    ui/controls/jump_down_button.h
    ui/controls/invite_link_buttons.cpp
    ui/controls/invite_link_buttons.h
    ui/controls/invite_link_label.cpp
    ui/controls/invite_link_label.h
    ui/controls/peer_list_dummy.cpp
    ui/controls/peer_list_dummy.h
    ui/controls/send_as_button.cpp
    ui/controls/send_as_button.h
    ui/controls/send_button.cpp
    ui/controls/send_button.h
    ui/controls/tabbed_search.cpp
    ui/controls/tabbed_search.h
    ui/controls/who_reacted_context_action.cpp
    ui/controls/who_reacted_context_action.h
    ui/controls/window_outdated_bar.cpp
    ui/controls/window_outdated_bar_dummy.cpp
    ui/controls/window_outdated_bar.h
    ui/effects/fireworks_animation.cpp
    ui/effects/fireworks_animation.h
    ui/effects/glare.cpp
    ui/effects/glare.h
    ui/effects/loading_element.cpp
    ui/effects/loading_element.h
    ui/effects/outline_segments.cpp
    ui/effects/outline_segments.h
    ui/effects/premium_graphics.cpp
    ui/effects/premium_graphics.h
    ui/effects/premium_stars.cpp
    ui/effects/premium_stars.h
    ui/effects/premium_stars_colored.cpp
    ui/effects/premium_stars_colored.h
    ui/effects/premium_top_bar.cpp
    ui/effects/premium_top_bar.h
    ui/effects/round_checkbox.cpp
    ui/effects/round_checkbox.h
    ui/effects/scroll_content_shadow.cpp
    ui/effects/scroll_content_shadow.h
    ui/effects/shake_animation.cpp
    ui/effects/shake_animation.h
    ui/effects/snowflakes.cpp
    ui/effects/snowflakes.h
    ui/effects/toggle_arrow.cpp
    ui/effects/toggle_arrow.h
    ui/text/format_song_name.cpp
    ui/text/format_song_name.h
    ui/text/format_values.cpp
    ui/text/format_values.h
    ui/text/text_options.cpp
    ui/text/text_options.h

    ui/widgets/color_editor.cpp
    ui/widgets/color_editor.h
    ui/widgets/continuous_sliders.cpp
    ui/widgets/continuous_sliders.h
    ui/widgets/discrete_sliders.cpp
    ui/widgets/discrete_sliders.h
    ui/widgets/gradient_round_button.cpp
    ui/widgets/gradient_round_button.h
    ui/widgets/multi_select.cpp
    ui/widgets/multi_select.h
    ui/widgets/sent_code_field.cpp
    ui/widgets/sent_code_field.h
    ui/widgets/vertical_drum_picker.cpp
    ui/widgets/vertical_drum_picker.h

    ui/arc_angles.h
    ui/cached_round_corners.cpp
    ui/cached_round_corners.h
    ui/color_contrast.cpp
    ui/color_contrast.h
    ui/color_int_conversion.cpp
    ui/color_int_conversion.h
    ui/empty_userpic.cpp
    ui/empty_userpic.h
    ui/grouped_layout.cpp
    ui/grouped_layout.h
    ui/power_saving.cpp
    ui/power_saving.h
    ui/vertical_list.cpp
    ui/vertical_list.h
    ui/unread_badge_paint.cpp
    ui/unread_badge_paint.h
    ui/userpic_view.cpp
    ui/userpic_view.h
    ui/widgets/fields/special_fields.cpp
    ui/widgets/fields/special_fields.h
    ui/widgets/fields/time_part_input_with_placeholder.cpp
    ui/widgets/fields/time_part_input_with_placeholder.h

    ui/ui_pch.h
)

if (DESKTOP_APP_SPECIAL_TARGET)
    remove_target_sources(td_ui ${src_loc}
        ui/controls/window_outdated_bar_dummy.cpp
    )
else()
    remove_target_sources(td_ui ${src_loc}
        ui/controls/window_outdated_bar.cpp
    )
endif()

target_include_directories(td_ui
PUBLIC
    ${src_loc}
)

target_link_libraries(td_ui
PUBLIC
    tdesktop::td_lang
    desktop-app::lib_ui
    desktop-app::lib_lottie
PRIVATE
    tdesktop::lib_tgcalls
    desktop-app::lib_ffmpeg
    desktop-app::lib_webview
    desktop-app::lib_webrtc
    desktop-app::lib_spellcheck
    desktop-app::lib_stripe
    desktop-app::external_kcoreaddons
)
