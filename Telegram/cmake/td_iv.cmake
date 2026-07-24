# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_iv OBJECT)
init_non_host_target(td_iv)
add_library(tdesktop::td_iv ALIAS td_iv)

target_precompile_headers(td_iv PRIVATE ${src_loc}/iv/iv_pch.h)
nice_target_sources(td_iv ${src_loc}
PRIVATE
    iv/editor/iv_editor_box.cpp
    iv/editor/iv_editor_box.h
    iv/editor/iv_editor_clipboard.cpp
    iv/editor/iv_editor_clipboard.h
    iv/editor/iv_editor_state.cpp
    iv/editor/iv_editor_state.h
    iv/editor/iv_editor_text_entities.cpp
    iv/editor/iv_editor_text_entities.h
    iv/editor/iv_editor_toolbar_pill.cpp
    iv/editor/iv_editor_toolbar_pill.h
    iv/editor/iv_editor_widget.cpp
    iv/editor/iv_editor_widget.h
    iv/editor/iv_editor_window.cpp
    iv/editor/iv_editor_window.h

    iv/iv_controller.cpp
    iv/iv_controller.h
    iv/iv_data.cpp
    iv/iv_data.h
    iv/iv_delegate.h
    iv/iv_pch.h
    iv/iv_search_bar.cpp
    iv/iv_search_bar.h
    iv/iv_search_controller.cpp
    iv/iv_search_controller.h
    iv/iv_zoom_controls.cpp
    iv/iv_zoom_controls.h
)

nice_target_sources(td_iv ${src_loc}
PRIVATE
    iv/markdown/iv_markdown_common.cpp
    iv/markdown/iv_markdown_common.h
    iv/markdown/iv_markdown_article.cpp
    iv/markdown/iv_markdown_article.h
    iv/markdown/iv_markdown_article_layout_blocks.cpp
    iv/markdown/iv_markdown_article_layout_blocks.h
    iv/markdown/iv_markdown_article_layout_structure.cpp
    iv/markdown/iv_markdown_article_layout_structure.h
    iv/markdown/iv_markdown_article_paint.cpp
    iv/markdown/iv_markdown_article_paint.h
    iv/markdown/iv_markdown_article_scroll_forwarder.cpp
    iv/markdown/iv_markdown_article_scroll_forwarder.h
    iv/markdown/iv_markdown_article_selection.cpp
    iv/markdown/iv_markdown_article_selection.h
    iv/markdown/iv_markdown_article_text.cpp
    iv/markdown/iv_markdown_article_text.h
    iv/markdown/iv_markdown_controller.cpp
    iv/markdown/iv_markdown_controller.h
    iv/markdown/iv_markdown_document.cpp
    iv/markdown/iv_markdown_document.h
    iv/markdown/iv_markdown_embed_overlay.cpp
    iv/markdown/iv_markdown_embed_overlay.h
    iv/markdown/iv_markdown_history_view_media.cpp
    iv/markdown/iv_markdown_history_view_media.h
    iv/markdown/iv_markdown_math.cpp
    iv/markdown/iv_markdown_math.h
    iv/markdown/iv_markdown_math_renderer.cpp
    iv/markdown/iv_markdown_math_renderer.h
    iv/markdown/iv_markdown_microtex.cpp
    iv/markdown/iv_markdown_microtex.h
    iv/markdown/iv_markdown_parse.cpp
    iv/markdown/iv_markdown_parse.h
    iv/markdown/iv_markdown_parse_convert.cpp
    iv/markdown/iv_markdown_parse_convert.h
    iv/markdown/iv_markdown_parse_finalize.cpp
    iv/markdown/iv_markdown_parse_finalize.h
    iv/markdown/iv_markdown_parse_validate.cpp
    iv/markdown/iv_markdown_parse_validate.h
    iv/markdown/iv_markdown_media_block.cpp
    iv/markdown/iv_markdown_media_block.h
    iv/markdown/iv_markdown_media_reuse.cpp
    iv/markdown/iv_markdown_media_reuse.h
    iv/markdown/iv_markdown_prepare.cpp
    iv/markdown/iv_markdown_prepare.h
    iv/markdown/iv_markdown_prepare_blocks.cpp
    iv/markdown/iv_markdown_prepare_blocks.h
    iv/markdown/iv_markdown_prepare_formulas.cpp
    iv/markdown/iv_markdown_prepare_formulas.h
    iv/markdown/iv_markdown_prepare_inline.cpp
    iv/markdown/iv_markdown_prepare_inline.h
    iv/markdown/iv_markdown_prepare_links.cpp
    iv/markdown/iv_markdown_prepare_links.h
    iv/markdown/iv_markdown_prepare_native_blocks.cpp
    iv/markdown/iv_markdown_prepare_native_blocks.h
    iv/markdown/iv_markdown_prepare_native_richtext.cpp
    iv/markdown/iv_markdown_prepare_native_richtext.h
    iv/markdown/iv_markdown_prepare_serialize.cpp
    iv/markdown/iv_markdown_prepare_serialize.h
    iv/markdown/iv_markdown_prepare_state.cpp
    iv/markdown/iv_markdown_prepare_state.h
    iv/markdown/iv_markdown_slideshow_chrome.cpp
    iv/markdown/iv_markdown_slideshow_chrome.h
    iv/markdown/iv_markdown_view.cpp
    iv/markdown/iv_markdown_view.h
    iv/markdown/iv_markdown_view_widget.cpp
    iv/markdown/iv_markdown_view_widget.h
)

target_link_libraries(td_iv
PRIVATE
    desktop-app::external_cmark_gfm
    desktop-app::external_microtex
    desktop-app::lib_spellcheck
)

target_include_directories(td_iv
PUBLIC
    ${src_loc}
)

target_link_libraries(td_iv
PUBLIC
    desktop-app::lib_ui
    tdesktop::td_scheme
PRIVATE
    desktop-app::lib_webview
    desktop-app::lib_storage
    desktop-app::external_ada
    tdesktop::td_lang
    tdesktop::td_ui
)
