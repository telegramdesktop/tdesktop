# Source atlas

This atlas divides Telegram Desktop's sources into named areas, ordered from foundation to features, each with one responsibility; every source file belongs to exactly one area, and paths are relative to `Telegram/`. Find the area of the code you touch, put new code where an area's responsibility says it belongs, and read the area's own map first when its directory has an `AGENTS.md`. When no area fits, say so in the task instead of inventing a place.

## Libraries

### lib/crl — concurrency runtime
- Paths: `lib_crl/`
- `crl::async`, `crl::on_main` with guards, serial queues, objects living on queues or threads, and `crl::now()`. It must not own app scheduling policy.

### lib/rpl — reactive streams
- Paths: `lib_rpl/`
- `rpl::producer`, `variable`, `event_stream`, `lifetime` and the operators that every widget and data object uses to propagate changes. It must not hold app state.

### lib/base — foundation utilities and OS services
- Paths: `lib_base/`
- Shared C++ and Qt utilities for all Desktop App Toolkit apps, built on crl and rpl: containers, weak pointers, flags, timers, bytes, crypto and zlib helpers, logging, runtime options, plus OS services behind portable APIs (`base/platform/`). It must not own UI, the Telegram data model or app policy.

### lib/storage — encrypted cache primitives
- Paths: `lib_storage/`
- The binlog cache database for media and files, encrypted files and keys, and legacy cleanup. It must not own the account data schema or file transfer.

### lib/tl — TL serialization runtime
- Paths: `lib_tl/`
- Primitive TL types, boxed wrappers, readers and writers that the generated MTProto scheme builds on. It must not own transport or sessions.

## Core

### core — application lifetime and process-wide services
- Paths: `SourceFiles/core/`, `SourceFiles/main.cpp`, `SourceFiles/logs*`, `SourceFiles/settings.*`, `SourceFiles/config.h`, `SourceFiles/stdafx.h`, `SourceFiles/tray*`, `SourceFiles/data/raw/`
- `Core::Application` and startup (`Launcher`, `Sandbox`), app and proxy settings, crash reports, shortcuts, file and mime utilities, the lib_base and lib_ui integration, logging, the tray menu, geolocation and screenshot protection. It must not own link routing, self-update or any feature.

### core/links — links and clickable entities
- Paths: `SourceFiles/core/deep_links/`, `SourceFiles/core/local_url_handlers*`, `SourceFiles/core/click_handler_types*`, `SourceFiles/core/phone_click_handler*`, `SourceFiles/core/bank_card_click_handler*`
- Turning tg:// and t.me links, deep links and clickable text entities into in-app navigation and actions. It must not own the screens those links open. Today it also builds the personal-channel picker in `local_url_handlers.cpp`; that belongs in `settings/sections` beside the account-info page.

### core/updates — self-update
- Paths: `SourceFiles/core/update_*`, `SourceFiles/_other/updater*`, `SourceFiles/_other/packer*`, `SourceFiles/platform/linux/update_install_linux*`
- Checking, downloading, verifying, unpacking and installing new versions per channel, the standalone Updater, the Linux protected install and the release Packer. It must not own changelogs or settings UI.

### accounts — accounts and sessions
- Paths: `SourceFiles/main/`
- The multi-account `Main::Domain`, each `Main::Account` with its MTP instance and local storage, the logged-in `Main::Session` that owns data, API and storage, per-session settings and the server `AppConfig`. It must not own feature logic beyond holding components.

### lang — localization
- Paths: `SourceFiles/lang/`, `SourceFiles/countries/`
- The language pack, generated `tr::` phrases with tags and plurals, cloud language switching, hardcoded fallbacks, and the country list with phone formats and flags. It must not own message translation (`ai-translate`).

## Model

### data/session — the session data registry
- Paths: `SourceFiles/data/`
- `Data::Session`, which creates and owns every peer, history, message, photo and document by id, the `Data::Changes` bus, ids and cache keys, album grouping, pts gaps and small session-wide values. It must not own widgets or feature request flows. Today it also fetches the next upgradeable gift and toggles the forum view mode in `data_session.cpp`; those requests belong in `api`.

### data/peers — users, groups and channels
- Paths: `SourceFiles/data/data_peer*`, `SourceFiles/data/data_user*`, `SourceFiles/data/data_chat.*`, `SourceFiles/data/data_chat_participant_status*`, `SourceFiles/data/data_channel*`, `SourceFiles/data/data_birthday*`, `SourceFiles/data/data_lastseen_status.h`, `SourceFiles/data/data_community*`, `SourceFiles/data/data_bot_app*`, `SourceFiles/data/data_group_call*`, `SourceFiles/data/data_star_gift*`, `SourceFiles/data/data_credits*`, `SourceFiles/data/data_boosts.h`, `SourceFiles/data/data_premium_*`, `SourceFiles/data/data_subscriptions.h`, `SourceFiles/data/data_statistics*`, `SourceFiles/data/notify/`, `SourceFiles/data/business/`
- Who peers are and what they allow: fields, rights and restrictions, online status, birthdays, bot commands, communities, the active group call, notification and business settings, and the per-peer premium, Stars, gift, boost and statistics values. It must not own how they are fetched or shown. Today it also pages group-call participants in `data_group_call.cpp` and paints userpics in `PeerData::paintUserpic`; those belong in `api` and `ui/common`.

### data/threads — chat lists and conversation threads
- Paths: `SourceFiles/data/data_thread*`, `SourceFiles/data/data_folder*`, `SourceFiles/data/data_forum*`, `SourceFiles/data/data_chat_filters*`, `SourceFiles/data/data_saved_messages*`, `SourceFiles/data/data_saved_sublist*`, `SourceFiles/data/data_replies_list*`, `SourceFiles/data/data_histor*`, `SourceFiles/data/data_messages*`, `SourceFiles/data/data_sparse_ids*`, `SourceFiles/data/data_abstract_sparse_ids.h`, `SourceFiles/data/data_shared_media*`, `SourceFiles/data/data_search_*`, `SourceFiles/data/data_unread_value*`, `SourceFiles/data/data_poll_messages*`, `SourceFiles/data/data_drafts*`, `SourceFiles/data/data_compose_stash.h`, `SourceFiles/data/data_send_action*`, `SourceFiles/storage/storage_facade*`, `SourceFiles/storage/storage_shared_media*`, `SourceFiles/storage/storage_sparse_ids_list*`, `SourceFiles/storage/storage_user_photos*`
- Folders, chat filters, forums and topics, saved-messages sublists, replies lists, the per-history request queue, message slices, search and calendar, the in-memory shared-media and user-photo indexes, unread counters, drafts and typing. The chat-list base classes still live in `dialogs`.

### data/media — photos, documents, files and stories
- Paths: `SourceFiles/data/data_document*`, `SourceFiles/data/data_photo*`, `SourceFiles/data/data_cloud_file*`, `SourceFiles/data/data_file_*`, `SourceFiles/data/data_streaming*`, `SourceFiles/data/data_download_manager*`, `SourceFiles/data/data_auto_download*`, `SourceFiles/data/data_media_*`, `SourceFiles/data/data_web_page*`, `SourceFiles/data/data_game*`, `SourceFiles/data/data_poll.*`, `SourceFiles/data/data_todo_list*`, `SourceFiles/data/data_location*`, `SourceFiles/data/data_audio_msg_id*`, `SourceFiles/data/data_wall_paper*`, `SourceFiles/data/data_cloud_themes*`, `SourceFiles/data/data_reply_preview*`, `SourceFiles/data/data_saved_music*`, `SourceFiles/data/data_stor*`
- Cloud files and origins, loading and streaming, the download manager and auto-download rules, message media kinds (web pages, games, polls, to-do lists, locations, invoices), wallpapers, cloud themes, saved music, and stories with viewers and albums. It must not own decoding or playback.

### data/stickers — stickers, custom emoji and reactions
- Paths: `SourceFiles/data/stickers/`, `SourceFiles/data/data_message_reaction*`, `SourceFiles/data/data_emoji_statuses*`, `SourceFiles/chat_helpers/stickers_lottie*`, `SourceFiles/chat_helpers/stickers_emoji_*`, `SourceFiles/chat_helpers/stickers_dice_pack*`, `SourceFiles/chat_helpers/stickers_gift_box_pack*`
- Sticker sets, the custom emoji manager, the reactions catalog and per-message reactions, emoji statuses, the animated-emoji, dice and gift-box packs and the Lottie and WebM player factories. It must not own pickers or reaction strips.

### data/components — per-session lists
- Paths: `SourceFiles/data/components/`, `SourceFiles/main/session/send_as_peers*`, `SourceFiles/data/business/data_shortcut_messages*`, `SourceFiles/data/data_ai_compose_tones*`
- Per-session services owned by `Main::Session` or `Data::Session`, each loading and caching one server list: scheduled, sponsored, business, quick-reply and ephemeral messages, fact-checks, recent and top peers, the Stars balance, gift auctions, passkeys, promo suggestions, AI compose tones and send-as peers. It must not own the screens that show them.

### history/model — message histories and messages
- Paths: `SourceFiles/history/`
- `History` with its blocks, unread state and forum switches, and `HistoryItem` with its components, reply markup, edits, text for copy, translation and streamed bot drafts. It must not own widgets.

### iv/document — rich document model
- Paths: `SourceFiles/iv/iv_rich_page*`, `SourceFiles/iv/iv_rich_message_serializer*`, `SourceFiles/iv/markdown/iv_markdown_document*`, `SourceFiles/iv/markdown/iv_markdown_parse*`, `SourceFiles/iv/markdown/iv_markdown_prepare*`, `SourceFiles/iv/markdown/iv_markdown_math*`, `SourceFiles/iv/markdown/iv_markdown_microtex*`, `SourceFiles/iv/markdown/iv_markdown_common*`
- The `RichPage` model behind Instant View and rich messages with its MTProto serializer, Markdown parsing with math and MicroTeX measurement, and preparing Markdown or RichPage into a render document. It must not paint. Today it also rasterizes formulas in `RenderWithMicrotex` (`iv_markdown_microtex.cpp`); that belongs in `iv/article`, while measurement stays here.

### wallet/core — TON wallet state, keys and transport
- Paths: `SourceFiles/wallet/wallet_engine*`, `SourceFiles/wallet/wallet_session*`, `SourceFiles/wallet/wallet_api*`, `SourceFiles/wallet/wallet_stream*`, `SourceFiles/wallet/wallet_rates*`, `SourceFiles/wallet/wallet_fiat*`, `SourceFiles/wallet/wallet_onramp*`, `SourceFiles/wallet/wallet_transfer_*`, `SourceFiles/wallet/wallet_user_addresses*`, `SourceFiles/wallet/wallet_address*`, `SourceFiles/wallet/wallet_vault*`, `SourceFiles/wallet/wallet_custody*`, `SourceFiles/wallet/wallet_unlock*`, `SourceFiles/wallet/wallet_key_protection*`, `SourceFiles/wallet/wallet_phrase_shares*`, `SourceFiles/gram/`
- `Wallet::Session` with balance, history and sends, the engine host, toncenter transport and streaming, rates and on-ramp, the transfer store and its chat service messages, the key vault with unlock and protection, and the pure `td_gram` codecs. `Main::Session`, `Data::Session` and `api_updates` reach into it; it must not own panels or boxes. Today it also builds the passcode, unlock and key-protection boxes in `wallet_unlock` and `wallet_key_protection`; those belong in `wallet/ui`.

## Network and storage

### api — Telegram API requests
- Paths: `SourceFiles/api/`, `SourceFiles/apiwrap*`
- The updates stream, sending and editing, and the per-feature request managers hung off `ApiWrap` that turn MTP answers into `Data::` changes. It must not own boxes, list controllers or click handlers.

### mtproto — MTProto transport
- Paths: `SourceFiles/mtproto/`, `SourceFiles/tde2e/`
- DC options and config, auth keys, sessions and resending, TCP, HTTP and TLS connections, proxies and checks, special-config fallback, dedicated loaders, the TL schemes and the tde2e end-to-end binding. It knows nothing about messages, peers or UI.

### storage/local — encrypted local persistence
- Paths: `SourceFiles/storage/storage_account*`, `SourceFiles/storage/storage_domain*`, `SourceFiles/storage/localstorage*`, `SourceFiles/storage/details/`, `SourceFiles/storage/serialize_*`, `SourceFiles/storage/storage_file_lock*`
- The tdata of accounts and the domain, the settings scheme, serializers and file locks. It must not own file transfer.

### storage/transfer — downloads and uploads
- Paths: `SourceFiles/storage/`, `SourceFiles/ui/image/image_location*`
- Downloaders and the MTProto download manager, the upload queue, preparing local files and folder archives for sending, cloud blobs, and the `StorageFileLocation` and `ImageLocation` cloud-file addresses. It must not own the media model.

### media/engine — decoding, audio and encoding
- Paths: `SourceFiles/media/`, `SourceFiles/media/streaming/`, `SourceFiles/media/audio/`, `SourceFiles/ffmpeg/`
- FFmpeg wrappers, the streaming loader, decoder and track sync, GIF clip readers, the OpenAL mixer, voice capture and short sounds, and offline video and sticker encoding with frames and waveforms. It must not own players or viewers.

## UI kit

### ui-kit/core — widget base, drawing and style runtime
- Paths: `lib_ui/ui/`, `lib_qr/`
- `RpWidget`, buttons and click handlers, the `Ui::Integration` hooks, layout wraps, accessibility, painter and rect helpers, image preparation, OpenGL and RHI surfaces, the style runtime (colors, fonts, icons, scale) and QR images. It must not use phrases, app styles or product data.

### ui-kit/text — rich text and input fields
- Paths: `lib_ui/ui/text/`, `lib_ui/ui/widgets/fields/`, `lib_ui/ui/widgets/time_input*`, `lib_ui/ui/emoji_config*`, `lib_ui/emoji_suggestions/`
- Entities and `TextWithEntities`, `Ui::Text::String` layout, shaping and painting, custom emoji, clipboard HTML, emoji sets and shortcode suggestions, and the editable `InputField` with masked, number, password and time inputs. It must not own message-specific formatting.

### ui-kit/widgets — generic controls and menus
- Paths: `lib_ui/ui/widgets/`
- Buttons, checkboxes and toggles, labels, scroll areas, shadows, tooltips, tabs, and context and dropdown menus, styled by `widgets.style`. It must not hold app-specific widgets.

### ui-kit/windows — kit windows and native window behavior
- Paths: `lib_ui/ui/platform/`, `lib_ui/ui/widgets/rp_window*`, `lib_ui/ui/widgets/separate_panel*`
- `RpWindow`, `SeparatePanel`, per-OS frameless helpers, custom title bars, window shadows and platform UI utilities. It must not own the app's main window or OS integration.

### ui-kit/effects — animation engine and generic effects
- Paths: `lib_ui/ui/effects/`, `lib_ui/ui/paint/`, `lib_ui/ui/animated_icon*`
- `anim::value` and `Ui::Animations`, ripples, radial progress, gradients, panel and slide animations, number tickers, spoilers, blobs and animated icons. It must not own product effects.

### ui-kit/layers — boxes, layers and toasts
- Paths: `lib_ui/ui/layers/`, `lib_ui/ui/toast/`
- `BoxContent` and `GenericBox`, the box frame, the layer stack, `Ui::Show` and toasts. It must not own concrete boxes.

## UI libraries

### lib/lottie — Lottie playback
- Paths: `lib_lottie/`
- Animated stickers, emoji and icons on top of lib_ui and lib_ffmpeg: rlottie parsing, background frame rendering, the compressed frame cache and the single and multi players. It must not own sticker logic.

### lib/spellcheck — spelling, language and translation interfaces
- Paths: `lib_spellcheck/`, `lib_translate/`
- Platform and Hunspell spellcheckers, language detection, the misspelling highlighter for lib_ui fields, and in `lib_translate/` the message-translation provider interface with the macOS Swift bridge. It also holds code-block syntax highlighting, which is not about spelling; it must not own dictionary downloads or settings UI.

### lib/webview — embedded web views
- Paths: `lib_webview/`
- `Webview::Window` over WebKitGTK, WKWebView and WebView2 with data streams, the JS bridge, dialogs and storage profiles, used by mini apps, payments and IV. It must not own bot, payment or IV logic.

### lib/webrtc — call media devices
- Paths: `lib_webrtc/`
- Device enumeration and tracking, the OpenAL audio module, loopback capture for screen sharing with sound, video track frames through lib_ffmpeg and the microphone tester. It must not own call signaling or call UI.

## Shared UI

### ui/common — Telegram-specific visual helpers
- Paths: `SourceFiles/ui/`, `SourceFiles/layout/`, `SourceFiles/boxes/boxes.style`, `SourceFiles/dialogs/ui/dialogs_pill*`, `SourceFiles/info/channel_statistics/earn/earn_format*`, `SourceFiles/_other/zxcvbn_translit.cpp`
- Userpics, dynamic thumbnails, badges, round corners, colors, power saving, value and currency formatting, text options, album and mosaic geometry, item layout bases, passcode strength and the shared `td_common`, `boxes` and `menu_icons` styles. Reusable outside Telegram goes to lib_ui; needing phrases, app styles or product data goes here.

### ui/chat — the look of chat screens
- Paths: `SourceFiles/ui/chat/`
- `ChatTheme`, `ChatStyle` and the paint context, bubbles, readability fixes, the generic bars above a message list (pinned, group call, join requests, sponsored, more chats) and the send-as and theme choosers. It must not own message layout.

### ui/controls — reusable app controls and widgets
- Paths: `SourceFiles/ui/controls/`, `SourceFiles/ui/widgets/`, `SourceFiles/info/userpic/`, `SourceFiles/info/profile/info_levels.style`, `SourceFiles/menu/menu_check_item*`, `SourceFiles/menu/menu_checked_action*`
- Controls several features embed: the userpic button and emoji avatar builder, send and emoji buttons, checked menu items, who-reacted and delete menu items, sub-tabs, swipe-back, the location picker, invite-link labels, table rows, sliders, multi-select, the color editor, special fields and meters. A control only one feature uses belongs to that feature.

### ui/effects — product animations and decorative graphics
- Paths: `SourceFiles/ui/effects/`, `SourceFiles/info/channel_statistics/earn/earn_icons*`, `SourceFiles/info/channel_statistics/boosts/giveaway/boost_badge*`
- Thanos deletion, typing and sending animations, reaction and emoji fly, particles, skeletons, round checkboxes, and the Premium, Stars and TON visuals: limit bubbles, badges, 3D stars and coins, currency icons. It must not own the boxes or pages that show them. Today it also builds the Premium limits comparison box (`ShowListBox` in `premium_graphics.cpp`), whose only caller is `premium_preview_box`; that belongs in `premium`.

### ui/boxes — reusable box bodies
- Paths: `SourceFiles/ui/boxes/`, `SourceFiles/info/channel_statistics/boosts/giveaway/select_countries_box*`
- Session-free boxes that features fill with data and callbacks: confirm, single choice, calendar, date and time pickers, country selection, language choice, birthday, fact-check, peer QR, boosts, collectible info and Stars and TON inputs. It must not talk to the API. Today it also reaches the session in `peer_qr_box` (window controller, cloud themes) and in the USD estimate of `emoji_stake_box`; those parts belong in `info/profile` and `ui/controls`.

### window — window shell and navigation
- Paths: `SourceFiles/window/`, `SourceFiles/mainwidget*`, `SourceFiles/mainwindow*`, `SourceFiles/boxes/abstract_box*`, `SourceFiles/dialogs/ui/restore_windows_offer*`, `SourceFiles/ui/controls/window_*`, `SourceFiles/ui/controls/title_sub_widget*`
- OS windows and their controllers, `SessionController` showing chats, sections, layers and boxes, section mementos and slide animations, the main menu, folders sidebar, saved windows, chat switching and previews, the connecting widget and lock screens. It must not own chat action menus or notifications.

### notifications — notification system
- Paths: `SourceFiles/window/notifications_*`
- Scheduling and grouping notifications, the `Manager` and `NativeManager` interface that `platform` backends implement, and the custom-drawn default popups. It must not own the per-OS backends.

### themes — app themes and chat backgrounds
- Paths: `SourceFiles/window/themes/`
- Loading, applying and saving palettes and backgrounds, embedded schemes and accents, the cloud themes list, per-chat themes, previews and the palette editor. It must not own cloud-theme data or the page that hosts the pickers.

### settings — settings framework
- Paths: `SourceFiles/settings/`
- Section base and factory, the declarative builder with its search index, settings search, keyboard navigation and the row kit (`settings_common`, `settings.style`) that boxes also use. It must not own concrete pages.

### peer-list — searchable peer lists
- Paths: `SourceFiles/boxes/peer_list*`
- `PeerListRow`, `PeerListController`, `PeerListBox` and `PeerListsBox`, section headers and index, with stock controllers for contacts, chats, recipients, topics and sublists. It must not own any one feature's list.

### stickers — emoji, sticker and GIF UI
- Paths: `SourceFiles/chat_helpers/tabbed_*`, `SourceFiles/chat_helpers/emoji_list_widget*`, `SourceFiles/chat_helpers/emoji_picker_overlay*`, `SourceFiles/chat_helpers/emoji_sets_manager*`, `SourceFiles/chat_helpers/stickers_list_*`, `SourceFiles/chat_helpers/gifs_list_widget*`, `SourceFiles/chat_helpers/chat_helpers.style`, `SourceFiles/ui/controls/tabbed_search*`, `SourceFiles/boxes/sticker_*`, `SourceFiles/boxes/stickers_*`
- The tabbed selector with its lists, footer and search, the emoji picker overlay and emoji-set manager, the sticker-set box, the installed and archived sets manager and the sticker creator. It must not own sticker data.

### message-field — message input helpers
- Paths: `SourceFiles/chat_helpers/`
- Field setup and handlers, mention, hashtag, command and sticker autocomplete, emoji suggestions and keywords, spellchecker dictionaries, rich paste, the character counter, the bot keyboard and the `ChatHelpers::Show` interface. It must not own the chat compose area.

### statistics/charts — chart library
- Paths: `SourceFiles/statistics/`, `SourceFiles/data/data_statistics_chart*`
- The chart data with its measurement (`StatisticalChart`), linear, stacked, bar and pie charts with the zoom footer, filters and point details, chart JSON parsing and XLSX export. It must not own any screen, data loading or API code.

### iv/article — rich document rendering
- Paths: `SourceFiles/iv/markdown/`, `SourceFiles/iv/iv_cached_media*`
- Laying out, painting and interacting with rich documents as articles: selection, links, search highlights, media, embed and button blocks, slideshows, the media runtime and the article widget, used by IV windows, rich messages, AI boxes and the editor. It must not own windows, sending or editing state.

## Features: chats

### dialogs — chats list
- Paths: `SourceFiles/dialogs/`
- The chats column with search (tags, posts, communities), the list painting chats, topics and results, row painters, the stories strip, search suggestions and top peers, swipe actions and top-bar promo suggestions. It also holds the chat-list base classes (`Dialogs::Entry`, `MainList`, `Row`) that `data/` builds on, which change with this UI more than with data.

### folders — chat folders UI
- Paths: `SourceFiles/boxes/filters/`, `SourceFiles/boxes/choose_filter_box*`, `SourceFiles/api/api_chat_filters*`, `SourceFiles/ui/filter_icon_panel*`, `SourceFiles/ui/controls/filter_link_header*`, `SourceFiles/ui/widgets/chat_filters_tabs_*`
- The folder editor with included and excluded chats, shareable folder links and their import, the add-to-folder menu and drag-and-drop, the icon picker and the folder tab strip. It must not own folder data or the folders sidebar.

### history/screen — chat screens
- Paths: `SourceFiles/history/history_widget*`, `SourceFiles/history/history_drag_area*`, `SourceFiles/history/history_view_*`, `SourceFiles/history/view/history_view_chat_section*`, `SourceFiles/history/view/history_view_pinned_section*`, `SourceFiles/history/view/history_view_scheduled_section*`, `SourceFiles/history/view/history_view_welcome_messages_section*`, `SourceFiles/support/`
- The classic `HistoryWidget` and the `ChatWidget`-based replies, topic, sublist, pinned, scheduled and business-message sections, with drag-and-drop, highlights, pull-to-next-channel and swipe-back, plus the support-account tools. The two screen implementations mirror each other, so a change to one usually needs the other.

### history/list — message list widgets
- Paths: `SourceFiles/history/view/`, `SourceFiles/history/history_inner_widget*`, `SourceFiles/history/history_message_selection.h`, `SourceFiles/chat_helpers/emoji_interactions*`, `SourceFiles/ui/controls/popup_selector*`, `SourceFiles/ui/controls/dynamic_images_strip*`
- `HistoryInner` and `ListWidget` with selection and copy, the message context menu, corner buttons, chat preview, reply button, emoji interactions, in-list toasts and read metrics; the two lists mirror each other. It must not own message elements or the sections around the list.

### history/messages — message elements
- Paths: `SourceFiles/history/view/history_view_element.*`, `SourceFiles/history/view/history_view_message.*`, `SourceFiles/history/view/history_view_service_message.*`, `SourceFiles/history/view/history_view_bottom_info.*`, `SourceFiles/history/view/history_view_reply.*`, `SourceFiles/history/view/history_view_cursor_state.*`, `SourceFiles/history/view/history_view_object.h`, `SourceFiles/history/view/history_view_view_button.*`, `SourceFiles/history/view/history_view_transcribe_button.*`, `SourceFiles/history/view/history_view_summary_header.*`, `SourceFiles/history/view/history_view_text_helper.*`, `SourceFiles/history/view/history_view_about_view.*`, `SourceFiles/history/view/history_view_item_preview.h`, `SourceFiles/history/view/history_view_fake_items.*`
- The on-screen element of one message: bubble and service layout, painting and hit-testing, the reply header, bottom info, view and transcribe buttons, summary header, the bot-about card and item previews for dialogs and notifications. It must not own attachments or the list.

### history/top-bars — bars above a chat
- Paths: `SourceFiles/history/view/history_view_top_bar_widget*`, `SourceFiles/history/view/history_view_contact_status*`, `SourceFiles/history/view/history_view_group_call_bar*`, `SourceFiles/history/view/history_view_pinned_bar*`, `SourceFiles/history/view/history_view_pinned_tracker*`, `SourceFiles/history/view/history_view_requests_bar*`, `SourceFiles/history/view/history_view_translate_*`, `SourceFiles/history/view/history_view_send_action*`, `SourceFiles/history/view/history_view_subsection_tabs*`, `SourceFiles/history/view/controls/history_view_top_controls*`, `SourceFiles/ui/controls/subsection_tabs_slider*`
- The title bar with typing status, the pinned bar and tracker, contact, business-bot and paid-message bars, group-call and join-request bars, the translate bar and tracker, and topic and sublist tabs, reused by both chat screens and the chats list.

### history/media — message attachments
- Paths: `SourceFiles/history/view/media/`, `SourceFiles/history/view/history_view_add_poll_option*`, `SourceFiles/history/view/history_view_sponsored_click_handler*`, `SourceFiles/chat_helpers/ttl_media_layer_widget*`
- Everything inside a bubble beyond text: photos, videos, GIFs, round videos, documents, voice, stickers, albums, web pages, polls, to-do lists, locations, games, invoices, calls, and service boxes for gifts, giveaways, suggestions and TON transfers. It must not own playback, downloads or the media viewer.

### history/compose — chat compose area
- Paths: `SourceFiles/history/view/controls/`, `SourceFiles/history/view/history_view_webpage_preview*`, `SourceFiles/ui/controls/round_video_recorder*`, `SourceFiles/api/api_suggest_post*`
- The field with send, attach, emoji and bot-menu buttons, reply, edit and forward panels with draft options, link previews, voice and round recording, suggested-post options, the draft stash, media editing with covers, search in chat and the join, mute and unblock bars. It must not own reusable field helpers.

### history/reactions — reactions UI
- Paths: `SourceFiles/history/view/reactions/`
- In-bubble reaction counters, the hover button, the picker strip and expanded selector, and the who-reacted list with moderation, reused by stories, the media viewer, group calls and the send menu. It must not own reaction data or sending.

### history/admin-log — recent actions
- Paths: `SourceFiles/history/admin_log/`
- The Recent actions section of a group or channel: loading admin-log events, turning them into display messages and filtering by action and admin. It must not own shared message rendering.

### chat-actions — chat and message menus and confirmations
- Paths: `SourceFiles/menu/`, `SourceFiles/window/window_peer_menu*`, `SourceFiles/history/view/history_view_schedule_box*`, `SourceFiles/boxes/delete_messages_box*`, `SourceFiles/boxes/moderate_messages_box*`, `SourceFiles/boxes/report_messages_box*`, `SourceFiles/boxes/pin_messages_box*`, `SourceFiles/boxes/about_sponsored_box*`
- The peer and chat-entry menus with their handlers, send options and scheduling, mute, auto-delete, mark as read, sponsored and per-message menu items, and the delete, moderate, report and pin confirmations. It must not own generic menu widgets.

### sending — preparing outgoing content
- Paths: `SourceFiles/boxes/send_files_box*`, `SourceFiles/boxes/send_gif_with_caption_box*`, `SourceFiles/boxes/edit_caption_box*`, `SourceFiles/boxes/music_attach_box*`, `SourceFiles/boxes/share_box*`, `SourceFiles/ui/chat/attach/`
- The send-files box with album, spoiler and caption options and the prepared-file model with its previews, GIF with caption, the music picker, editing a sent caption or media, and the share and forward box. It must not own the send pipeline.

### polls — composing polls and checklists
- Paths: `SourceFiles/boxes/create_poll_box*`, `SourceFiles/boxes/edit_todo_list_box*`, `SourceFiles/boxes/polls.style`, `SourceFiles/poll/`
- Creating polls, quizzes and checklists, with media and link attachments on options and their uploads. It must not own poll data or poll rendering.

### ai-translate — AI text tools and translation
- Paths: `SourceFiles/boxes/compose_ai_box*`, `SourceFiles/boxes/create_ai_*`, `SourceFiles/boxes/preview_ai_tone_box*`, `SourceFiles/boxes/translate_box*`, `SourceFiles/lang/translate_*`
- Compose-with-AI rewriting with tones, create-with-AI for the rich editor, AI tone management, the translate box, the translate-to choice and the MTProto and URL translation providers. It must not own localization or the translate bar.

### bots — inline results and mini apps
- Paths: `SourceFiles/inline_bots/`, `SourceFiles/ui/chat/attach/attach_bot_*`, `SourceFiles/api/api_bot*`, `SourceFiles/ui/layers/standalone_layer_stack*`
- Inline-query results with their layouts and panel, and Mini Apps: attach-menu bots, launching, the webview panel, permissions, downloads, device storage and prepared messages, plus bot button activation. It must not own the webview engine.

### export — data export
- Paths: `SourceFiles/export/`
- Exporting Telegram data: the settings and progress UI, the controller that walks chats and media through its own API wrapper, the export data model and the HTML and JSON writers. Other code only starts it and shows its top bar.

## Features: peers and profiles

### boxes/peers — managing chats, channels and contacts
- Paths: `SourceFiles/boxes/peers/`, `SourceFiles/boxes/add_contact_box*`, `SourceFiles/boxes/max_invite_box*`, `SourceFiles/boxes/select_future_owner_box*`, `SourceFiles/boxes/username_box*`, `SourceFiles/ui/boxes/edit_invite_link*`, `SourceFiles/api/api_chat_invite*`
- Creating groups and channels, editing info, type, usernames, colors, permissions, reactions and topics, invite links with previews and join requests, members, admins and restrictions, communities, managed bots, verification, ownership transfer and boost reassignment. It must not own the generic peer list or the profile page.

### info — Info panel framework and small sections
- Paths: `SourceFiles/info/`, `SourceFiles/profile/`
- The right-column, layer and separate-window Info container with its navigation stack, top bar, search and selection, and the small sections inside it: common groups, similar channels, members, join requests, reaction lists, communities, poll lists, saved sublists and the settings host. It must not own the profile page or the media list.

### info/profile — profile page
- Paths: `SourceFiles/info/profile/`, `SourceFiles/boxes/peers/peer_short_info_box*`, `SourceFiles/boxes/peers/prepare_short_info_box*`, `SourceFiles/ui/controls/stars_rating*`
- The profile of users, bots, groups, channels and topics: the cover and action buttons, details and actions blocks, members, media tabs, the birthday effect, the short-info card, and the per-peer value producers and badges that other areas reuse. It must not own the media list or other Info sections.

### info/media — shared media lists
- Paths: `SourceFiles/info/media/`, `SourceFiles/info/downloads/`, `SourceFiles/info/global_media/`, `SourceFiles/info/stories/`, `SourceFiles/info/saved/info_saved_music*`, `SourceFiles/overview/`
- The media list engine with pluggable providers and its sections (shared media, downloads, global media search, profile stories, saved music), the media item layouts in `overview/` and the counted media buttons. It must not own the profile page.

### info/stats — statistics and revenue
- Paths: `SourceFiles/info/statistics/`, `SourceFiles/info/channel_statistics/`, `SourceFiles/info/bot/`, `SourceFiles/api/api_earn*`
- Channel, group, message and story statistics with public forwards and export, boosts and boost links, giveaways, channel TON and Stars revenue with the withdrawal flow, bot earnings and the Stars affiliate program. It must not own chart rendering.

## Features: Stars, gifts and payments

### gifts — gifts and Stars UI
- Paths: `SourceFiles/boxes/star_gift_*`, `SourceFiles/boxes/transfer_gift_box*`, `SourceFiles/boxes/gift_*`, `SourceFiles/info/peer_gifts/`, `SourceFiles/settings/settings_credits_graphics*`, `SourceFiles/menu/gift_resale_filter*`
- Choosing, sending, upgrading, crafting, auctioning, reselling, transferring and wearing star gifts, premium gift codes and giveaways, gifting Stars, received gifts and collections, gift tiles, Stars balance and top-up widgets and transaction details. It must not own gift data or the payment flow.

### premium — Premium upsell
- Paths: `SourceFiles/boxes/premium_*`, `SourceFiles/settings/sections/settings_premium*`
- Limit-reached boxes, the feature preview carousel, unlock buttons and the Premium promo page with `ShowPremium` and the subscribe button. It must not own premium graphics.

### payments — paying invoices
- Paths: `SourceFiles/payments/`, `SourceFiles/boxes/send_credits_box*`
- The checkout panel with forms, card entry, saved credentials and 3-D Secure, the Stripe and SmartGlocal tokenizers, Stars invoices and receipts, gift invoices and paid reactions. It must not own the Stars balance or gift UI.

### passport — Telegram Passport
- Paths: `SourceFiles/passport/`
- Answering a service's authorization form with encrypted identity data: details, documents, scans and translations, phone and email verification and the encrypted credentials. It must not own cloud-password setup, though it exports the secure-secret helpers.

## Features: settings and sign-in

### settings/sections — Settings pages
- Paths: `SourceFiles/settings/sections/`, `SourceFiles/settings/cloud_password/`, `SourceFiles/settings/settings_privacy_controllers*`, `SourceFiles/settings/settings_experimental*`, `SourceFiles/settings/settings_power_saving*`, `SourceFiles/settings/settings_scale_preview*`, `SourceFiles/settings/settings_notifications_common.h`, `SourceFiles/settings/settings_intro*`, `SourceFiles/settings/settings_codes*`, `SourceFiles/ui/boxes/choose_font_box*`
- Main, account info, privacy and security with per-rule pages, two-step verification steps, notifications, chat settings, folders, advanced, data and storage, local passcode, sessions, websites, passkeys, calls, shortcuts, experimental options and the Stars page. It must not own the framework or Business pages.

### settings/business — Telegram Business pages
- Paths: `SourceFiles/settings/business/`, `SourceFiles/settings/sections/settings_business*`
- The Business hub, away and greeting messages, quick replies and their editor, working hours, location, chat intro, chat links, chatbots and the recipients picker.

### settings/boxes — privacy, security and preference boxes
- Paths: `SourceFiles/boxes/edit_privacy_box*`, `SourceFiles/boxes/passcode_box*`, `SourceFiles/boxes/auto_lock_box*`, `SourceFiles/boxes/self_destruction_box*`, `SourceFiles/boxes/url_auth_box*`, `SourceFiles/boxes/unconfirmed_auth_denied_box*`, `SourceFiles/boxes/phone_banned_box*`, `SourceFiles/boxes/auto_download_box*`, `SourceFiles/boxes/connection_box*`, `SourceFiles/boxes/download_path_box*`, `SourceFiles/boxes/language_box*`, `SourceFiles/boxes/dictionaries_manager*`, `SourceFiles/boxes/ringtones_box*`, `SourceFiles/boxes/reactions_settings_box*`, `SourceFiles/boxes/about_box*`, `SourceFiles/boxes/background_*`, `SourceFiles/api/api_confirm_phone*`
- Privacy rules and message prices, passcode and cloud password set, change and recovery, auto-lock, self-destruction, phone confirmation from `confirmphone` links, proxy and connection, auto-download, download folder, language, dictionaries, sounds, quick reaction, wallpapers and About. Log in with Telegram, denied logins and banned numbers also sit here, though they are not settings.

### intro — sign-in and sign-up
- Paths: `SourceFiles/intro/`
- The steps before an account is logged in: welcome, phone, QR login, code, cloud password check, login email and signup. It must not own post-login locks or settings.

## Features: media and calls

### media/view — media viewer
- Paths: `SourceFiles/media/view/`
- The full-window viewer for photos, videos, documents and theme previews with its OpenGL, RHI, raster and Metal renderers, playback controls, album strip, sponsored messages, recognized-text selection and picture-in-picture. It hosts stories and live streams but must not own their logic.

### media/player — audio player
- Paths: `SourceFiles/media/player/`, `SourceFiles/media/system_media_controls_*`
- Music, voice and round-video playback: playlist and state, the top-bar widget, playlist panel, floating round player, speed, order and volume controls, listen tracking and OS media controls. It must not own decoding.

### media/stories — stories viewer
- Paths: `SourceFiles/media/stories/`
- Story playback and navigation inside the media viewer: header, caption, reply field, reactions and story areas, recent viewers, reposts, sharing and stealth mode. It must not own story data or story lists.

### editor — photo and video editor
- Paths: `SourceFiles/editor/`
- Editing before sending, for profile photos, stickers and draw-to-reply: canvas items, crop, paint, undo, trim, audio and video timelines and the output modifications. It must not own the send boxes or uploads.

### calls — calls service and one-to-one calls
- Paths: `SourceFiles/calls/`, `SourceFiles/calls/group/ui/desktop_capture_choose_source*`, `SourceFiles/ui/controls/call_button*`, `SourceFiles/ui/controls/call_mute_button*`, `SourceFiles/ui/boxes/rate_call_box*`
- `Calls::Instance` that starts and tracks every call, the tgcalls 1:1 call model and key fingerprint, the call window and panel, the active-call top bar, recent calls, device menus, the screen-source picker and call rating. It must not own group calls.

### calls/group — group calls and live streams
- Paths: `SourceFiles/calls/group/`, `SourceFiles/media/view/media_view_video_stream*`
- Group calls, video chats, RTMP streams and conferences: the `GroupCall` model with participants, video, screen sharing and encryption, the panel, members and viewport, in-call messages and paid reactions, invite, join-as, settings and recording boxes, and the live stream inside the media viewer.

## Features: Instant View and wallet

### iv — Instant View windows
- Paths: `SourceFiles/iv/`, `SourceFiles/iv/markdown/iv_markdown_controller*`
- `Iv::Instance` routing IV pages, rich messages and TON sites to their native or webview windows, search, zoom, and exporting rich messages to HTML. It must not own the document model or rendering.

### iv/editor — rich document editor
- Paths: `SourceFiles/iv/editor/`
- Editing state over blocks, lists, tables and media groups, commands, clipboard import and export, entity mapping, and the editing widget with insert suggestions, the structure menu and the button and formula dialogs. It must not own sending or chat integration.

### iv/compose — composing rich messages
- Paths: `SourceFiles/iv/editor/iv_editor_session*`, `SourceFiles/iv/editor/iv_editor_box*`, `SourceFiles/iv/editor/iv_editor_window*`, `SourceFiles/iv/editor/iv_editor_toolbar_pill*`
- The compose and edit box with its toolbar and standalone window, compose threads and draft autosave, attachment upload, premium and paid-message gates, and sending or editing rich messages through the API.

### wallet/ton-connect — TON Connect
- Paths: `SourceFiles/wallet/wallet_ton_connect*`
- dApp sessions over Telegram's API, link parsing, connect and request approval boxes, transfer emulation previews, sign-data and recovery of in-flight requests.

### wallet/ui — wallet panel
- Paths: `SourceFiles/wallet/`
- The card and balance, history, send and receive, collectible transfers, transaction details, collectibles, phrase backup, import and rotation, currency choice and the visuals reused by the chat transfer bubble. It must not own wallet state, keys or transport.

## Platform

### platform — OS integration
- Paths: `SourceFiles/platform/`, `SourceFiles/webauthn/`, `SourceFiles/data/data_passkey_deserialize*`, `SourceFiles/menu/menu_dock*`, `SourceFiles/_other/startup_task_win.cpp`
- Linux, macOS and Windows implementations behind `platform_*.h`: native window and title, notification backends, tray, launcher and autostart, file dialogs, overlay helpers, OCR, translation providers, wallet key protection, passkeys with caBLE and FIDO2, the Touch Bar and the Dock menu. It must not hold cross-platform feature logic. Today it also holds the shared caBLE passkey protocol, ceremony and QR box in `webauthn/`, which every OS backend calls; that belongs in a passkeys feature area, which does not exist yet.

## Tooling and tests

### test — test tooling
- Paths: `SourceFiles/test/`, `SourceFiles/tests/`
- The Debug-only `-testagent` harness (scenario runner, evidence log, session and RPC fixtures, widget probes and pixel captures, wallet fixtures and the secrecy scan) and the standalone unit tests for the text engine and the updater. Product code must not depend on it. Today it also exposes hooks that `core/application.cpp`, `data_document.cpp`, `mtp_instance.cpp` and `platform_file_utilities.cpp` call directly; those belong behind seams owned by the calling areas.
