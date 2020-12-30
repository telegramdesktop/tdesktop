/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_widget.h"

#include "api/api_editing.h"
#include "api/api_bot.h"
#include "api/api_sending.h"
#include "api/api_text_entities.h"
#include "api/api_send_progress.h"
#include "boxes/confirm_box.h"
#include "boxes/send_files_box.h"
#include "boxes/share_box.h"
#include "boxes/edit_caption_box.h"
#include "core/file_utilities.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "ui/special_buttons.h"
#include "ui/emoji_config.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/text/format_values.h"
#include "ui/chat/message_bar.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/image/image.h"
#include "ui/special_buttons.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "inline_bots/inline_bot_result.h"
#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_media_types.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_chat_filters.h"
#include "data/data_scheduled_messages.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_group_call.h"
#include "data/stickers/data_stickers.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h"
#include "history/history_drag_area.h"
#include "history/history_inner_widget.h"
#include "history/history_item_components.h"
//#include "history/feed/history_feed_section.h" // #feed
#include "history/view/controls/history_view_voice_record_bar.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_scheduled_section.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/history_view_webpage_preview.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_pinned_bar.h"
#include "history/view/history_view_group_call_tracker.h"
#include "history/view/media/history_view_media.h"
#include "profile/profile_block_group_members.h"
#include "info/info_memento.h"
#include "core/click_handler_types.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/bot_keyboard.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/send_context_menu.h"
#include "platform/platform_specific.h"
#include "mtproto/mtproto_config.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/file_upload.h"
#include "storage/storage_media_prepare.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "media/player/media_player_instance.h"
#include "core/application.h"
#include "apiwrap.h"
#include "base/qthelp_regex.h"
#include "ui/chat/pinned_bar.h"
#include "ui/chat/group_call_bar.h"
#include "ui/widgets/popup_menu.h"
#include "ui/item_text_options.h"
#include "ui/unread_badge.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_peer_menu.h"
#include "inline_bots/inline_results_widget.h"
#include "info/profile/info_profile_values.h" // SharedMediaCountValue.
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/crash_reports.h"
#include "core/shortcuts.h"
#include "support/support_common.h"
#include "support/support_autocomplete.h"
#include "dialogs/dialogs_key.h"
#include "calls/calls_instance.h"
#include "facades.h"
#include "app.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

#include <QGuiApplication> // keyboardModifiers()
#include <QtGui/QWindow>
#include <QtCore/QMimeData>

namespace {

constexpr auto kMessagesPerPageFirst = 30;
constexpr auto kMessagesPerPage = 50;
constexpr auto kPreloadHeightsCount = 3; // when 3 screens to scroll left make a preload request
constexpr auto kScrollToVoiceAfterScrolledMs = 1000;
constexpr auto kSkipRepaintWhileScrollMs = 100;
constexpr auto kShowMembersDropdownTimeoutMs = 300;
constexpr auto kDisplayEditTimeWarningMs = 300 * 1000;
constexpr auto kFullDayInMs = 86400 * 1000;
constexpr auto kSaveDraftTimeout = 1000;
constexpr auto kSaveDraftAnywayTimeout = 5000;
constexpr auto kSaveCloudDraftIdleTimeout = 14000;
constexpr auto kRecordingUpdateDelta = crl::time(100);
constexpr auto kRefreshSlowmodeLabelTimeout = crl::time(200);
constexpr auto kCommonModifiers = 0
	| Qt::ShiftModifier
	| Qt::MetaModifier
	| Qt::ControlModifier;
const auto kPsaAboutPrefix = "cloud_lng_about_psa_";

[[nodiscard]] crl::time CountToastDuration(const TextWithEntities &text) {
	return std::clamp(
		crl::time(1000) * text.text.size() / 14,
		crl::time(1000) * 5,
		crl::time(1000) * 8);
}

} // namespace

HistoryWidget::HistoryWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::AbstractSectionWidget(parent, controller)
, _api(&controller->session().mtp())
, _updateEditTimeLeftDisplay([=] { updateField(); })
, _fieldBarCancel(this, st::historyReplyCancel)
, _previewTimer([=] { requestPreview(); })
, _topBar(this, controller)
, _scroll(this, st::historyScroll, false)
, _updateHistoryItems([=] { updateHistoryItemsByTimer(); })
, _historyDown(_scroll, st::historyToDown)
, _unreadMentions(_scroll, st::historyUnreadMentions)
, _fieldAutocomplete(this, controller)
, _supportAutocomplete(session().supportMode()
	? object_ptr<Support::Autocomplete>(this, &session())
	: nullptr)
, _send(std::make_shared<Ui::SendButton>(this))
, _unblock(this, tr::lng_unblock_button(tr::now).toUpper(), st::historyUnblock)
, _botStart(this, tr::lng_bot_start(tr::now).toUpper(), st::historyComposeButton)
, _joinChannel(
	this,
	tr::lng_profile_join_channel(tr::now).toUpper(),
	st::historyComposeButton)
, _muteUnmute(
	this,
	tr::lng_channel_mute(tr::now).toUpper(),
	st::historyComposeButton)
, _attachToggle(this, st::historyAttach)
, _tabbedSelectorToggle(this, st::historyAttachEmoji)
, _botKeyboardShow(this, st::historyBotKeyboardShow)
, _botKeyboardHide(this, st::historyBotKeyboardHide)
, _botCommandStart(this, st::historyBotCommandStart)
, _voiceRecordBar(std::make_unique<HistoryWidget::VoiceRecordBar>(
		this,
		controller,
		_send,
		st::historySendSize.height()))
, _field(
	this,
	st::historyComposeField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_message_ph())
, _kbScroll(this, st::botKbScroll)
, _membersDropdownShowTimer([=] { showMembersDropdown(); })
, _scrollTimer([=] { scrollByTimer(); })
, _saveDraftTimer([=] { saveDraft(); })
, _saveCloudDraftTimer([=] { saveCloudDraft(); })
, _topShadow(this) {
	setAcceptDrops(true);

	session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	connect(_scroll, &Ui::ScrollArea::scrolled, [=] {
		handleScroll();
	});
	_historyDown->addClickHandler([=] { historyDownClicked(); });
	_unreadMentions->addClickHandler([=] { showNextUnreadMention(); });
	_fieldBarCancel->addClickHandler([=] { cancelFieldAreaState(); });
	_send->addClickHandler([=] { sendButtonClicked(); });

	SendMenu::SetupMenuAndShortcuts(
		_send.get(),
		[=] { return sendButtonMenuType(); },
		[=] { sendSilent(); },
		[=] { sendScheduled(); });

	_unblock->addClickHandler([=] { unblockUser(); });
	_botStart->addClickHandler([=] { sendBotStartCommand(); });
	_joinChannel->addClickHandler([=] { joinChannel(); });
	_muteUnmute->addClickHandler([=] { toggleMuteUnmute(); });
	connect(
		_field,
		&Ui::InputField::submitted,
		[=](Qt::KeyboardModifiers modifiers) { sendWithModifiers(modifiers); });
	connect(_field, &Ui::InputField::cancelled, [=] {
		escape();
	});
	connect(_field, &Ui::InputField::tabbed, [=] {
		fieldTabbed();
	});
	connect(_field, &Ui::InputField::resized, [=] {
		fieldResized();
	});
	connect(_field, &Ui::InputField::focused, [=] {
		fieldFocused();
	});
	connect(_field, &Ui::InputField::changed, [=] {
		fieldChanged();
	});
	connect(App::wnd()->windowHandle(), &QWindow::visibleChanged, this, [=] {
		windowIsVisibleChanged();
	});

	initTabbedSelector();

	_attachToggle->addClickHandler(App::LambdaDelayed(
		st::historyAttach.ripple.hideDuration,
		this,
		[=] { chooseAttach(); }));

	_highlightTimer.setCallback([this] { updateHighlightedMessage(); });

	const auto rawTextEdit = _field->rawTextEdit().get();
	rpl::merge(
		_field->scrollTop().changes() | rpl::to_empty,
		base::qt_signal_producer(
			rawTextEdit,
			&QTextEdit::cursorPositionChanged)
	) | rpl::start_with_next([=] {
		saveDraftDelayed();
	}, _field->lifetime());

	connect(rawTextEdit, &QTextEdit::cursorPositionChanged, this, [=] {
		checkFieldAutocomplete();
	}, Qt::QueuedConnection);

	_fieldBarCancel->hide();

	_topBar->hide();
	_scroll->hide();

	_keyboard = _kbScroll->setOwnedWidget(object_ptr<BotKeyboard>(
		&session(),
		this));
	_kbScroll->hide();

	updateScrollColors();

	_historyDown->installEventFilter(this);
	_unreadMentions->installEventFilter(this);

	InitMessageField(controller, _field);

	_fieldAutocomplete->mentionChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::MentionChosen data) {
		insertMention(data.user);
	}, lifetime());

	_fieldAutocomplete->hashtagChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::HashtagChosen data) {
		insertHashtagOrBotCommand(data.hashtag, data.method);
	}, lifetime());

	_fieldAutocomplete->botCommandChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::BotCommandChosen data) {
		insertHashtagOrBotCommand(data.command, data.method);
	}, lifetime());

	_fieldAutocomplete->stickerChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::StickerChosen data) {
		sendExistingDocument(data.sticker, data.options);
	}, lifetime());

	_fieldAutocomplete->setModerateKeyActivateCallback([=](int key) {
		return _keyboard->isHidden()
			? false
			: _keyboard->moderateKeyActivate(key);
	});

	if (_supportAutocomplete) {
		supportInitAutocomplete();
	}
	_fieldLinksParser = std::make_unique<MessageLinksParser>(_field);
	_fieldLinksParser->list().changes(
	) | rpl::start_with_next([=](QStringList &&parsed) {
		_parsedLinks = std::move(parsed);
		checkPreview();
	}, lifetime());
	_field->rawTextEdit()->installEventFilter(this);
	_field->rawTextEdit()->installEventFilter(_fieldAutocomplete);
	_field->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return canSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(data, std::nullopt, data->text());
		}
		Unexpected("action in MimeData hook.");
	});
	InitSpellchecker(controller, _field);

	const auto suggestions = Ui::Emoji::SuggestionsController::Init(
		this,
		_field,
		&controller->session());
	_raiseEmojiSuggestions = [=] { suggestions->raise(); };
	updateFieldSubmitSettings();

	_field->hide();
	_send->hide();
	_unblock->hide();
	_botStart->hide();
	_joinChannel->hide();
	_muteUnmute->hide();

	initVoiceRecordBar();

	_attachToggle->hide();
	_tabbedSelectorToggle->hide();
	_botKeyboardShow->hide();
	_botKeyboardHide->hide();
	_botCommandStart->hide();

	_botKeyboardShow->addClickHandler([=] { toggleKeyboard(); });
	_botKeyboardHide->addClickHandler([=] { toggleKeyboard(); });
	_botCommandStart->addClickHandler([=] { startBotCommand(); });

	_topShadow->hide();

	_attachDragAreas = DragArea::SetupDragAreaToContainer(
		this,
		crl::guard(this, [=](not_null<const QMimeData*> d) {
			return _history && _canSendMessages && !isRecording();
		}),
		crl::guard(this, [=](bool f) { _field->setAcceptDrops(f); }),
		crl::guard(this, [=] { updateControlsGeometry(); }));
	_attachDragAreas.document->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, false);
		Window::ActivateWindow(controller);
	});
	_attachDragAreas.photo->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, true);
		Window::ActivateWindow(controller);
	});

	subscribe(Adaptive::Changed(), [=] {
		if (_history) {
			_history->forceFullResize();
			if (_migrated) {
				_migrated->forceFullResize();
			}
			updateHistoryGeometry();
			update();
		}
	});

	session().data().unreadItemAdded(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		unreadMessageAdded(item);
	}, lifetime());

	session().data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		itemRemoved(item);
	}, lifetime());

	session().data().historyChanged(
	) | rpl::start_with_next([=](not_null<History*> history) {
		handleHistoryChange(history);
	}, lifetime());

	session().data().viewResizeRequest(
	) | rpl::start_with_next([=](not_null<HistoryView::Element*> view) {
		if (view->data()->mainView() == view) {
			updateHistoryGeometry();
		}
	}, lifetime());

	Core::App().settings().largeEmojiChanges(
	) | rpl::start_with_next([=] {
		crl::on_main(this, [=] {
			updateHistoryGeometry();
		});
	}, lifetime());

	session().data().animationPlayInlineRequest(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		if (const auto view = item->mainView()) {
			if (const auto media = view->media()) {
				media->playAnimation();
			}
		}
	}, lifetime());

	session().data().webPageUpdates(
	) | rpl::filter([=](not_null<WebPageData*> page) {
		return (_previewData == page.get());
	}) | rpl::start_with_next([=] {
		updatePreview();
	}, lifetime());

	session().data().channelDifferenceTooLong(
	) | rpl::filter([=](not_null<ChannelData*> channel) {
		return _peer == channel.get();
	}) | rpl::start_with_next([=] {
		updateHistoryDownVisibility();
		preloadHistoryIfNeeded();
	}, lifetime());

	session().data().userIsBotChanges(
	) | rpl::filter([=](not_null<UserData*> user) {
		return (_peer == user.get());
	}) | rpl::start_with_next([=](not_null<UserData*> user) {
		_list->notifyIsBotChanged();
		_list->updateBotInfo();
		updateControlsVisibility();
		updateControlsGeometry();
	}, lifetime());

	session().data().botCommandsChanges(
	) | rpl::filter([=](not_null<UserData*> user) {
		return _peer && (_peer == user || !_peer->isUser());
	}) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (_fieldAutocomplete->clearFilteredBotCommands()) {
			checkFieldAutocomplete();
		}
	}, lifetime());

	session().changes().historyUpdates(
		Data::HistoryUpdate::Flag::MessageSent
		| Data::HistoryUpdate::Flag::ForwardDraft
		| Data::HistoryUpdate::Flag::BotKeyboard
		| Data::HistoryUpdate::Flag::CloudDraft
		| Data::HistoryUpdate::Flag::UnreadMentions
		| Data::HistoryUpdate::Flag::UnreadView
		| Data::HistoryUpdate::Flag::TopPromoted
		| Data::HistoryUpdate::Flag::LocalMessages
	) | rpl::filter([=](const Data::HistoryUpdate &update) {
		return (_history == update.history.get());
	}) | rpl::start_with_next([=](const Data::HistoryUpdate &update) {
		if (update.flags & Data::HistoryUpdate::Flag::MessageSent) {
			synteticScrollToY(_scroll->scrollTopMax());
		}
		if (update.flags & Data::HistoryUpdate::Flag::ForwardDraft) {
			updateForwarding();
		}
		if (update.flags & Data::HistoryUpdate::Flag::BotKeyboard) {
			updateBotKeyboard(update.history);
		}
		if (update.flags & Data::HistoryUpdate::Flag::CloudDraft) {
			applyCloudDraft(update.history);
		}
		if (update.flags & Data::HistoryUpdate::Flag::LocalMessages) {
			updateSendButtonType();
		}
		if (update.flags & Data::HistoryUpdate::Flag::UnreadMentions) {
			updateUnreadMentionsVisibility();
		}
		if (update.flags & Data::HistoryUpdate::Flag::UnreadView) {
			unreadCountUpdated();
		}
		if (update.flags & Data::HistoryUpdate::Flag::TopPromoted) {
			updateHistoryGeometry();
			updateControlsVisibility();
			updateControlsGeometry();
			this->update();
		}
	}, lifetime());

	session().changes().messageUpdates(
		Data::MessageUpdate::Flag::Edited
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		itemEdited(update.item);
	}, lifetime());

	session().changes().messageUpdates(
		Data::MessageUpdate::Flag::ReplyMarkup
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		if (_keyboard->forMsgId() == update.item->fullId()) {
			updateBotKeyboard(update.item->history(), true);
		}
	}, lifetime());

	session().changes().messageUpdates(
		Data::MessageUpdate::Flag::BotCallbackSent
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		const auto item = update.item;
		if (item->id < 0 || _peer != item->history()->peer) {
			return;
		}

		const auto keyId = _keyboard->forMsgId();
		const auto lastKeyboardUsed = (keyId == FullMsgId(_channel, item->id))
			&& (keyId == FullMsgId(_channel, _history->lastKeyboardId));

		session().data().requestItemRepaint(item);

		if (_replyToId == item->id) {
			cancelReply();
		}
		if (_keyboard->singleUse()
			&& _keyboard->hasMarkup()
			&& lastKeyboardUsed) {
			if (_kbShown) {
				toggleKeyboard(false);
			}
			_history->lastKeyboardUsed = true;
		}
	}, lifetime());

	subscribe(Media::Player::instance()->switchToNextNotifier(), [this](const Media::Player::Instance::Switch &pair) {
		if (pair.from.type() == AudioMsgId::Type::Voice) {
			scrollToCurrentVoiceMessage(pair.from.contextId(), pair.to);
		}
	});

	using UpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		UpdateFlag::Rights
		| UpdateFlag::Migration
		| UpdateFlag::UnavailableReason
		| UpdateFlag::IsBlocked
		| UpdateFlag::Admins
		| UpdateFlag::Members
		| UpdateFlag::OnlineStatus
		| UpdateFlag::Notifications
		| UpdateFlag::ChannelAmIn
		| UpdateFlag::ChannelLinkedChat
		| UpdateFlag::Slowmode
		| UpdateFlag::BotStartToken
		| UpdateFlag::PinnedMessages
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		if (_migrated && update.peer.get() == _migrated->peer) {
			if (_pinnedTracker
				&& (update.flags & UpdateFlag::PinnedMessages)) {
				checkPinnedBarState();
			}
		}
		return (update.peer.get() == _peer);
	}) | rpl::map([](const Data::PeerUpdate &update) {
		return update.flags;
	}) | rpl::start_with_next([=](Data::PeerUpdate::Flags flags) {
		if (flags & UpdateFlag::Rights) {
			checkPreview();
			updateStickersByEmoji();
			updateFieldPlaceholder();
		}
		if (flags & UpdateFlag::Migration) {
			handlePeerMigration();
		}
		if (flags & UpdateFlag::Notifications) {
			updateNotifyControls();
		}
		if (flags & UpdateFlag::UnavailableReason) {
			const auto unavailable = _peer->computeUnavailableReason();
			if (!unavailable.isEmpty()) {
				controller->showBackFromStack();
				Ui::show(Box<InformBox>(unavailable));
				return;
			}
		}
		if (flags & UpdateFlag::BotStartToken) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		if (flags & UpdateFlag::Slowmode) {
			updateSendButtonType();
		}
		if (flags & (UpdateFlag::IsBlocked
			| UpdateFlag::Admins
			| UpdateFlag::Members
			| UpdateFlag::OnlineStatus
			| UpdateFlag::Rights
			| UpdateFlag::ChannelAmIn
			| UpdateFlag::ChannelLinkedChat)) {
			handlePeerUpdate();
		}
		if (_pinnedTracker && (flags & UpdateFlag::PinnedMessages)) {
			checkPinnedBarState();
		}
	}, lifetime());

	rpl::merge(
		session().data().defaultUserNotifyUpdates(),
		session().data().defaultChatNotifyUpdates(),
		session().data().defaultBroadcastNotifyUpdates()
	) | rpl::start_with_next([=] {
		updateNotifyControls();
	}, lifetime());

	subscribe(session().data().queryItemVisibility(), [=](
			const Data::Session::ItemVisibilityQuery &query) {
		if (_a_show.animating()
			|| _history != query.item->history()
			|| !query.item->mainView() || !isVisible()) {
			return;
		}
		if (const auto view = query.item->mainView()) {
			auto top = _list->itemTop(view);
			if (top >= 0) {
				auto scrollTop = _scroll->scrollTop();
				if (top + view->height() > scrollTop
					&& top < scrollTop + _scroll->height()) {
					*query.isVisible = true;
				}
			}
		}
	});
	_topBar->membersShowAreaActive(
	) | rpl::start_with_next([=](bool active) {
		setMembersShowAreaActive(active);
	}, _topBar->lifetime());
	_topBar->forwardSelectionRequest(
	) | rpl::start_with_next([=] {
		forwardSelected();
	}, _topBar->lifetime());
	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	session().api().sendActions(
	) | rpl::filter([=](const Api::SendAction &action) {
		return (action.history == _history);
	}) | rpl::start_with_next([=](const Api::SendAction &action) {
		const auto lastKeyboardUsed = lastForceReplyReplied(FullMsgId(
			action.history->channelId(),
			action.replyTo));
		if (action.options.scheduled) {
			cancelReply(lastKeyboardUsed);
			crl::on_main(this, [=, history = action.history]{
				controller->showSection(
					std::make_shared<HistoryView::ScheduledMemento>(history));
			});
		} else {
			fastShowAtEnd(action.history);
			if (cancelReply(lastKeyboardUsed) && !action.clearDraft) {
				saveCloudDraft();
			}
		}
		if (action.options.handleSupportSwitch) {
			handleSupportSwitch(action.history);
		}
	}, lifetime());

	setupScheduledToggle();
	orderWidgets();
	setupShortcuts();
}

void HistoryWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		auto weak = Ui::MakeWeak(this);
		setGeometry(newGeometry);
		if (!weak) {
			return;
		}
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

Dialogs::EntryState HistoryWidget::computeDialogsEntryState() const {
	return Dialogs::EntryState{
		.key = _history,
		.section = Dialogs::EntryState::Section::History,
		.currentReplyToId = replyToId(),
	};
}

void HistoryWidget::refreshTopBarActiveChat() {
	const auto state = computeDialogsEntryState();
	_topBar->setActiveChat(state, _history->sendActionPainter());
	if (_inlineResults) {
		_inlineResults->setCurrentDialogsEntryState(state);
	}
}

void HistoryWidget::refreshTabbedPanel() {
	if (_peer && controller()->hasTabbedSelectorOwnership()) {
		createTabbedPanel();
	} else {
		setTabbedPanel(nullptr);
	}
}

void HistoryWidget::initVoiceRecordBar() {
	{
		auto scrollHeight = rpl::combine(
			_scroll->topValue(),
			_scroll->heightValue()
		) | rpl::map([=](int top, int height) {
			return top + height - st::historyRecordLockPosition.y();
		});
		_voiceRecordBar->setLockBottom(std::move(scrollHeight));
	}

	_voiceRecordBar->setSendButtonGeometryValue(_send->geometryValue());

	_voiceRecordBar->setStartRecordingFilter([=] {
		const auto error = _peer
			? Data::RestrictionError(_peer, ChatRestriction::f_send_media)
			: std::nullopt;
		if (error) {
			Ui::show(Box<InformBox>(*error));
			return true;
		} else if (showSlowmodeError()) {
			return true;
		}
		return false;
	});

	const auto applyLocalDraft = [=] {
		if (_history && _history->localDraft()) {
			applyDraft();
		}
	};

	_voiceRecordBar->sendActionUpdates(
	) | rpl::start_with_next([=](const auto &data) {
		if (!_history) {
			return;
		}
		session().sendProgressManager().update(
			_history,
			data.type,
			data.progress);
	}, lifetime());

	_voiceRecordBar->sendVoiceRequests(
	) | rpl::start_with_next([=](const auto &data) {
		if (!canWriteMessage() || data.bytes.isEmpty() || !_history) {
			return;
		}

		auto action = Api::SendAction(_history);
		action.replyTo = replyToId();
		action.options = data.options;
		session().api().sendVoiceMessage(
			data.bytes,
			data.waveform,
			data.duration,
			action);
		_voiceRecordBar->clearListenState();
		applyLocalDraft();
	}, lifetime());

	_voiceRecordBar->cancelRequests(
	) | rpl::start_with_next(applyLocalDraft, lifetime());

	_voiceRecordBar->lockShowStarts(
	) | rpl::start_with_next([=] {
		updateHistoryDownVisibility();
		updateUnreadMentionsVisibility();
	}, lifetime());

	_voiceRecordBar->updateSendButtonTypeRequests(
	) | rpl::start_with_next([=] {
		updateSendButtonType();
	}, lifetime());

	_voiceRecordBar->lockViewportEvents(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());

	_voiceRecordBar->hideFast();
}

void HistoryWidget::initTabbedSelector() {
	refreshTabbedPanel();

	_tabbedSelectorToggle->addClickHandler([=] {
		toggleTabbedSelectorMode();
	});

	const auto selector = controller()->tabbedSelector();

	base::install_event_filter(this, selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return base::EventFilterResult::Continue;
	});

	selector->emojiChosen(
	) | rpl::filter([=] {
		return !isHidden() && !_field->isHidden();
	}) | rpl::start_with_next([=](EmojiPtr emoji) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), emoji);
	}, lifetime());

	selector->fileChosen(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=](TabbedSelector::FileChosen data) {
		sendExistingDocument(data.document, data.options);
	}, lifetime());

	selector->photoChosen(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=](TabbedSelector::PhotoChosen data) {
		sendExistingPhoto(data.photo, data.options);
	}, lifetime());

	selector->inlineResultChosen(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=](TabbedSelector::InlineChosen data) {
		sendInlineResult(data);
	}, lifetime());

	selector->setSendMenuType([=] { return sendMenuType(); });
}

void HistoryWidget::supportInitAutocomplete() {
	_supportAutocomplete->hide();

	_supportAutocomplete->insertRequests(
	) | rpl::start_with_next([=](const QString &text) {
		supportInsertText(text);
	}, _supportAutocomplete->lifetime());

	_supportAutocomplete->shareContactRequests(
	) | rpl::start_with_next([=](const Support::Contact &contact) {
		supportShareContact(contact);
	}, _supportAutocomplete->lifetime());
}

void HistoryWidget::supportInsertText(const QString &text) {
	_field->setFocus();
	_field->textCursor().insertText(text);
	_field->ensureCursorVisible();
}

void HistoryWidget::supportShareContact(Support::Contact contact) {
	if (!_history) {
		return;
	}
	supportInsertText(contact.comment);
	contact.comment = _field->getLastText();

	const auto submit = [=](Qt::KeyboardModifiers modifiers) {
		const auto history = _history;
		if (!history) {
			return;
		}
		auto options = Api::SendOptions();
		auto action = Api::SendAction(history);
		send(options);
		options.handleSupportSwitch = Support::HandleSwitch(modifiers);
		action.options = options;
		session().api().shareContact(
			contact.phone,
			contact.firstName,
			contact.lastName,
			action);
	};
	const auto box = Ui::show(Box<Support::ConfirmContactBox>(
		controller(),
		_history,
		contact,
		crl::guard(this, submit)));
	box->boxClosing(
	) | rpl::start_with_next([=] {
		_field->document()->undo();
	}, lifetime());
}

void HistoryWidget::scrollToCurrentVoiceMessage(FullMsgId fromId, FullMsgId toId) {
	if (crl::now() <= _lastUserScrolled + kScrollToVoiceAfterScrolledMs) {
		return;
	}
	if (!_list) {
		return;
	}

	auto from = session().data().message(fromId);
	auto to = session().data().message(toId);
	if (!from || !to) {
		return;
	}

	// If history has pending resize items, the scrollTopItem won't be updated.
	// And the scrollTop will be reset back to scrollTopItem + scrollTopOffset.
	handlePendingHistoryUpdate();

	if (const auto toView = to->mainView()) {
		auto toTop = _list->itemTop(toView);
		if (toTop >= 0 && !isItemCompletelyHidden(from)) {
			auto scrollTop = _scroll->scrollTop();
			auto scrollBottom = scrollTop + _scroll->height();
			auto toBottom = toTop + toView->height();
			if ((toTop < scrollTop && toBottom < scrollBottom) || (toTop > scrollTop && toBottom > scrollBottom)) {
				animatedScrollToItem(to->id);
			}
		}
	}
}

void HistoryWidget::animatedScrollToItem(MsgId msgId) {
	Expects(_history != nullptr);

	if (hasPendingResizedItems()) {
		updateListSize();
	}

	auto to = session().data().message(_channel, msgId);
	if (_list->itemTop(to) < 0) {
		return;
	}

	auto scrollTo = snap(
		itemTopForHighlight(to->mainView()),
		0,
		_scroll->scrollTopMax());
	animatedScrollToY(scrollTo, to);
}

void HistoryWidget::animatedScrollToY(int scrollTo, HistoryItem *attachTo) {
	Expects(_history != nullptr);

	if (hasPendingResizedItems()) {
		updateListSize();
	}

	// Attach our scroll animation to some item.
	auto itemTop = _list->itemTop(attachTo);
	auto scrollTop = _scroll->scrollTop();
	if (itemTop < 0 && !_history->isEmpty()) {
		attachTo = _history->blocks.back()->messages.back()->data();
		itemTop = _list->itemTop(attachTo);
	}
	if (itemTop < 0 || (scrollTop == scrollTo)) {
		synteticScrollToY(scrollTo);
		return;
	}

	_scrollToAnimation.stop();
	auto maxAnimatedDelta = _scroll->height();
	auto transition = anim::sineInOut;
	if (scrollTo > scrollTop + maxAnimatedDelta) {
		scrollTop = scrollTo - maxAnimatedDelta;
		synteticScrollToY(scrollTop);
		transition = anim::easeOutCubic;
	} else if (scrollTo + maxAnimatedDelta < scrollTop) {
		scrollTop = scrollTo + maxAnimatedDelta;
		synteticScrollToY(scrollTop);
		transition = anim::easeOutCubic;
	} else {
		// In local showHistory() we forget current scroll state,
		// so we need to restore it synchronously, otherwise we may
		// jump to the bottom of history in some updateHistoryGeometry() call.
		synteticScrollToY(scrollTop);
	}
	const auto itemId = attachTo->fullId();
	const auto relativeFrom = scrollTop - itemTop;
	const auto relativeTo = scrollTo - itemTop;
	_scrollToAnimation.start(
		[=] { scrollToAnimationCallback(itemId, relativeTo); },
		relativeFrom,
		relativeTo,
		st::slideDuration,
		anim::sineInOut);
}

void HistoryWidget::scrollToAnimationCallback(
		FullMsgId attachToId,
		int relativeTo) {
	auto itemTop = _list->itemTop(session().data().message(attachToId));
	if (itemTop < 0) {
		_scrollToAnimation.stop();
	} else {
		synteticScrollToY(qRound(_scrollToAnimation.value(relativeTo)) + itemTop);
	}
	if (!_scrollToAnimation.animating()) {
		preloadHistoryByScroll();
		checkReplyReturns();
	}
}

void HistoryWidget::enqueueMessageHighlight(
		not_null<HistoryView::Element*> view) {
	auto enqueueMessageId = [this](MsgId universalId) {
		if (_highlightQueue.empty() && !_highlightTimer.isActive()) {
			highlightMessage(universalId);
		} else if (_highlightedMessageId != universalId
			&& !base::contains(_highlightQueue, universalId)) {
			_highlightQueue.push_back(universalId);
			checkNextHighlight();
		}
	};
	const auto item = view->data();
	if (item->history() == _history) {
		enqueueMessageId(item->id);
	} else if (item->history() == _migrated) {
		enqueueMessageId(-item->id);
	}
}

void HistoryWidget::highlightMessage(MsgId universalMessageId) {
	_highlightStart = crl::now();
	_highlightedMessageId = universalMessageId;
	_highlightTimer.callEach(AnimationTimerDelta);
}

void HistoryWidget::checkNextHighlight() {
	if (_highlightTimer.isActive()) {
		return;
	}
	auto nextHighlight = [this] {
		while (!_highlightQueue.empty()) {
			auto msgId = _highlightQueue.front();
			_highlightQueue.pop_front();
			auto item = getItemFromHistoryOrMigrated(msgId);
			if (item && item->mainView()) {
				return msgId;
			}
		}
		return 0;
	}();
	if (!nextHighlight) {
		return;
	}
	highlightMessage(nextHighlight);
}

void HistoryWidget::updateHighlightedMessage() {
	const auto item = getItemFromHistoryOrMigrated(_highlightedMessageId);
	auto view = item ? item->mainView() : nullptr;
	if (!view) {
		return stopMessageHighlight();
	}
	auto duration = st::activeFadeInDuration + st::activeFadeOutDuration;
	if (crl::now() - _highlightStart > duration) {
		return stopMessageHighlight();
	}

	if (const auto group = session().data().groups().find(view->data())) {
		if (const auto leader = group->items.front()->mainView()) {
			view = leader;
		}
	}
	session().data().requestViewRepaint(view);
}

crl::time HistoryWidget::highlightStartTime(not_null<const HistoryItem*> item) const {
	auto isHighlighted = [this](not_null<const HistoryItem*> item) {
		if (item->id == _highlightedMessageId) {
			return (item->history() == _history);
		} else if (item->id == -_highlightedMessageId) {
			return (item->history() == _migrated);
		}
		return false;
	};
	return (isHighlighted(item) && _highlightTimer.isActive())
		? _highlightStart
		: 0;
}

void HistoryWidget::stopMessageHighlight() {
	_highlightTimer.cancel();
	_highlightedMessageId = 0;
	checkNextHighlight();
}

void HistoryWidget::clearHighlightMessages() {
	_highlightQueue.clear();
	stopMessageHighlight();
}

int HistoryWidget::itemTopForHighlight(
		not_null<HistoryView::Element*> view) const {
	if (const auto group = session().data().groups().find(view->data())) {
		if (const auto leader = group->items.front()->mainView()) {
			view = leader;
		}
	}
	auto itemTop = _list->itemTop(view);
	Assert(itemTop >= 0);

	auto heightLeft = (_scroll->height() - view->height());
	if (heightLeft <= 0) {
		return itemTop;
	}
	return qMax(itemTop - (heightLeft / 2), 0);
}

void HistoryWidget::start() {
	session().data().stickers().updated(
	) | rpl::start_with_next([=] {
		updateStickersByEmoji();
	}, lifetime());
	session().data().stickers().notifySavedGifsUpdated();
	subscribe(session().api().fullPeerUpdated(), [this](PeerData *peer) {
		fullPeerUpdated(peer);
	});
}

void HistoryWidget::insertMention(UserData *user) {
	QString replacement, entityTag;
	if (user->username.isEmpty()) {
		replacement = user->firstName;
		if (replacement.isEmpty()) {
			replacement = user->name;
		}
		entityTag = PrepareMentionTag(user);
	} else {
		replacement = '@' + user->username;
	}
	_field->insertTag(replacement, entityTag);
}

void HistoryWidget::insertHashtagOrBotCommand(
		QString str,
		FieldAutocomplete::ChooseMethod method) {
	if (!_peer) {
		return;
	}

	// Send bot command at once, if it was not inserted by pressing Tab.
	if (str.at(0) == '/' && method != FieldAutocomplete::ChooseMethod::ByTab) {
		App::sendBotCommand(_peer, nullptr, str, replyToId());
		session().api().finishForwarding(Api::SendAction(_history));
		setFieldText(_field->getTextWithTagsPart(_field->textCursor().position()));
	} else {
		_field->insertTag(str);
	}
}

void HistoryWidget::updateInlineBotQuery() {
	if (!_history) {
		return;
	}
	const auto query = ParseInlineBotQuery(&session(), _field);
	if (_inlineBotUsername != query.username) {
		_inlineBotUsername = query.username;
		if (_inlineBotResolveRequestId) {
			_api.request(_inlineBotResolveRequestId).cancel();
			_inlineBotResolveRequestId = 0;
		}
		if (query.lookingUpBot) {
			_inlineBot = nullptr;
			_inlineLookingUpBot = true;
			const auto username = _inlineBotUsername;
			_inlineBotResolveRequestId = _api.request(MTPcontacts_ResolveUsername(
				MTP_string(username)
			)).done([=](const MTPcontacts_ResolvedPeer &result) {
				inlineBotResolveDone(result);
			}).fail([=](const RPCError &error) {
				inlineBotResolveFail(error, username);
			}).send();
		} else {
			applyInlineBotQuery(query.bot, query.query);
		}
	} else if (query.lookingUpBot) {
		if (!_inlineLookingUpBot) {
			applyInlineBotQuery(_inlineBot, query.query);
		}
	} else {
		applyInlineBotQuery(query.bot, query.query);
	}
}

void HistoryWidget::applyInlineBotQuery(UserData *bot, const QString &query) {
	if (bot) {
		if (_inlineBot != bot) {
			_inlineBot = bot;
			_inlineLookingUpBot = false;
			inlineBotChanged();
		}
		if (!_inlineResults) {
			_inlineResults.create(this, controller());
			_inlineResults->setResultSelectedCallback([=](
					InlineBots::ResultSelected result) {
				sendInlineResult(result);
			});
			_inlineResults->setCurrentDialogsEntryState(
				computeDialogsEntryState());
			_inlineResults->requesting(
			) | rpl::start_with_next([=](bool requesting) {
				_tabbedSelectorToggle->setLoading(requesting);
			}, _inlineResults->lifetime());
			updateControlsGeometry();
			orderWidgets();
		}
		_inlineResults->queryInlineBot(_inlineBot, _peer, query);
		if (!_fieldAutocomplete->isHidden()) {
			_fieldAutocomplete->hideAnimated();
		}
	} else {
		clearInlineBot();
	}
}

void HistoryWidget::orderWidgets() {
	_send->raise();
	if (_contactStatus) {
		_contactStatus->raise();
	}
	if (_pinnedBar) {
		_pinnedBar->raise();
	}
	if (_groupCallBar) {
		_groupCallBar->raise();
	}
	_topShadow->raise();
	if (_fieldAutocomplete) {
		_fieldAutocomplete->raise();
	}
	if (_membersDropdown) {
		_membersDropdown->raise();
	}
	if (_inlineResults) {
		_inlineResults->raise();
	}
	if (_tabbedPanel) {
		_tabbedPanel->raise();
	}
	_raiseEmojiSuggestions();
	_attachDragAreas.document->raise();
	_attachDragAreas.photo->raise();
}

void HistoryWidget::updateStickersByEmoji() {
	if (!_peer) {
		return;
	}
	const auto emoji = [&] {
		const auto errorForStickers = Data::RestrictionError(
			_peer,
			ChatRestriction::f_send_stickers);
		if (!_editMsgId && !errorForStickers) {
			const auto &text = _field->getTextWithTags().text;
			auto length = 0;
			if (const auto emoji = Ui::Emoji::Find(text, &length)) {
				if (text.size() <= length) {
					return emoji;
				}
			}
		}
		return EmojiPtr(nullptr);
	}();
	_fieldAutocomplete->showStickers(emoji);
}

void HistoryWidget::fieldChanged() {
	InvokeQueued(this, [=] {
		updateInlineBotQuery();
		updateStickersByEmoji();
	});

	if (_history) {
		if (!_inlineBot
			&& !_editMsgId
			&& (_textUpdateEvents & TextUpdateEvent::SendTyping)) {
			session().sendProgressManager().update(
				_history,
				Api::SendProgressType::Typing);
		}
	}

	updateSendButtonType();
	if (showRecordButton()) {
		_previewCancelled = false;
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		updateControlsGeometry();
	}

	_saveCloudDraftTimer.cancel();
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}

	_saveDraftText = true;
	saveDraft(true);
}

void HistoryWidget::saveDraftDelayed() {
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}
	if (!_field->textCursor().position()
		&& !_field->textCursor().anchor()
		&& !_field->scrollTop().current()) {
		if (!session().local().hasDraftCursors(_peer->id)) {
			return;
		}
	}
	saveDraft(true);
}

void HistoryWidget::saveDraft(bool delayed) {
	if (!_peer) {
		return;
	} else if (delayed) {
		auto ms = crl::now();
		if (!_saveDraftStart) {
			_saveDraftStart = ms;
			return _saveDraftTimer.callOnce(kSaveDraftTimeout);
		} else if (ms - _saveDraftStart < kSaveDraftAnywayTimeout) {
			return _saveDraftTimer.callOnce(kSaveDraftTimeout);
		}
	}
	writeDrafts();
}

void HistoryWidget::saveFieldToHistoryLocalDraft() {
	if (!_history) return;

	if (_editMsgId) {
		_history->setLocalEditDraft(std::make_unique<Data::Draft>(_field, _editMsgId, _previewCancelled, _saveEditMsgRequestId));
	} else {
		if (_replyToId || !_field->empty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(_field, _replyToId, _previewCancelled));
		} else {
			_history->clearLocalDraft();
		}
		_history->clearLocalEditDraft();
	}
}

void HistoryWidget::saveCloudDraft() {
	controller()->session().api().saveCurrentDraftToCloud();
}

void HistoryWidget::writeDraftTexts() {
	Expects(_history != nullptr);

	session().local().writeDrafts(
		_history,
		_editMsgId ? Data::DraftKey::LocalEdit() : Data::DraftKey::Local(),
		Storage::MessageDraft{
			_editMsgId ? _editMsgId : _replyToId,
			_field->getTextWithTags(),
			_previewCancelled,
		});
	if (_migrated) {
		_migrated->clearDrafts();
		session().local().writeDrafts(_migrated);
	}
}

void HistoryWidget::writeDraftCursors() {
	Expects(_history != nullptr);

	session().local().writeDraftCursors(
		_history,
		_editMsgId ? Data::DraftKey::LocalEdit() : Data::DraftKey::Local(),
		MessageCursor(_field));
	if (_migrated) {
		_migrated->clearDrafts();
		session().local().writeDraftCursors(_migrated);
	}
}

void HistoryWidget::writeDrafts() {
	const auto save = (_history != nullptr) && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.cancel();
	if (save) {
		if (_saveDraftText) {
			writeDraftTexts();
		}
		writeDraftCursors();
	}
	_saveDraftText = false;

	if (!_editMsgId && !_inlineBot) {
		_saveCloudDraftTimer.callOnce(kSaveCloudDraftIdleTimeout);
	}
}

bool HistoryWidget::isRecording() const {
	return _voiceRecordBar->isRecording();
}

void HistoryWidget::activate() {
	if (_history) {
		if (!_historyInited) {
			updateHistoryGeometry(true);
		} else if (hasPendingResizedItems()) {
			updateHistoryGeometry();
		}
	}
	controller()->widget()->setInnerFocus();
}

void HistoryWidget::setInnerFocus() {
	if (_scroll->isHidden()) {
		setFocus();
	} else if (_list) {
		if (_nonEmptySelection
			|| (_list && _list->wasSelectedText())
			|| isRecording()
			|| isBotStart()
			|| isBlocked()
			|| !_canSendMessages) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
}

bool HistoryWidget::notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	if (samePeerBot) {
		if (_history) {
			TextWithTags textWithTags = { '@' + samePeerBot->username + ' ' + query, TextWithTags::Tags() };
			MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
			_history->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
			applyDraft();
			return true;
		}
	} else if (const auto bot = _peer ? _peer->asUser() : nullptr) {
		const auto to = bot->isBot()
			? bot->botInfo->inlineReturnTo
			: Dialogs::EntryState();
		const auto history = to.key.history();
		if (!history) {
			return false;
		}
		bot->botInfo->inlineReturnTo = Dialogs::EntryState();
		using Section = Dialogs::EntryState::Section;

		TextWithTags textWithTags = { '@' + bot->username + ' ' + query, TextWithTags::Tags() };
		MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
		auto draft = std::make_unique<Data::Draft>(
			textWithTags,
			to.currentReplyToId,
			cursor,
			false);

		if (to.section == Section::Replies) {
			history->setDraft(
				Data::DraftKey::Replies(to.rootId),
				std::move(draft));
			controller()->showRepliesForMessage(history, to.rootId);
		} else if (to.section == Section::Scheduled) {
			history->setDraft(Data::DraftKey::Scheduled(), std::move(draft));
			controller()->showSection(
				std::make_shared<HistoryView::ScheduledMemento>(history));
		} else {
			history->setLocalDraft(std::move(draft));
			if (history == _history) {
				applyDraft();
			} else {
				Ui::showPeerHistory(history->peer, ShowAtUnreadMsgId);
			}
		}
		return true;
	}
	return false;
}

void HistoryWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !Ui::isLayerShown();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		if (_history) {
			request->check(Command::Search, 1) && request->handle([=] {
				controller()->content()->searchInChat(_history);
				return true;
			});
			if (session().supportMode()) {
				request->check(
					Command::SupportToggleMuted
				) && request->handle([=] {
					toggleMuteUnmute();
					return true;
				});
			}
		}
	}, lifetime());
}

void HistoryWidget::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = nullptr;
}

void HistoryWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	if (item->history() == _history) {
		_replyReturns.push_back(item->id);
	} else if (item->history() == _migrated) {
		_replyReturns.push_back(-item->id);
	} else {
		return;
	}
	_replyReturn = item;
	updateControlsVisibility();
}

QList<MsgId> HistoryWidget::replyReturns() {
	return _replyReturns;
}

void HistoryWidget::setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns) {
	if (!_peer || _peer->id != peer) return;

	_replyReturns = replyReturns;
	if (_replyReturns.isEmpty()) {
		_replyReturn = nullptr;
	} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
		_replyReturn = session().data().message(0, -_replyReturns.back());
	} else {
		_replyReturn = session().data().message(_channel, _replyReturns.back());
	}
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = nullptr;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = session().data().message(0, -_replyReturns.back());
		} else {
			_replyReturn = session().data().message(_channel, _replyReturns.back());
		}
	}
}

void HistoryWidget::calcNextReplyReturn() {
	_replyReturn = nullptr;
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = nullptr;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = session().data().message(0, -_replyReturns.back());
		} else {
			_replyReturn = session().data().message(_channel, _replyReturns.back());
		}
	}
	if (!_replyReturn) {
		updateControlsVisibility();
	}
}

void HistoryWidget::fastShowAtEnd(not_null<History*> history) {
	if (_history != history) {
		return;
	}

	clearAllLoadRequests();
	setMsgId(ShowAtUnreadMsgId);
	if (_history->isReadyFor(_showAtMsgId)) {
		historyLoaded();
	} else {
		firstLoadMessages();
		doneShow();
	}
}

void HistoryWidget::applyDraft(FieldHistoryAction fieldHistoryAction) {
	InvokeQueued(this, [=] { updateStickersByEmoji(); });

	if (_voiceRecordBar->isActive()) {
		return;
	}

	auto draft = !_history
		? nullptr
		: _history->localEditDraft()
		? _history->localEditDraft()
		: _history->localDraft();
	auto fieldAvailable = canWriteMessage();
	if (!draft || (!_history->localEditDraft() && !fieldAvailable)) {
		auto fieldWillBeHiddenAfterEdit = (!fieldAvailable && _editMsgId != 0);
		clearFieldText(0, fieldHistoryAction);
		_field->setFocus();
		_replyEditMsg = nullptr;
		_editMsgId = _replyToId = 0;
		if (fieldWillBeHiddenAfterEdit) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		refreshTopBarActiveChat();
		return;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags, 0, fieldHistoryAction);
	_field->setFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;
	_previewCancelled = draft->previewCancelled;
	_replyEditMsg = nullptr;
	if (const auto editDraft = _history->localEditDraft()) {
		_editMsgId = editDraft->msgId;
		_replyToId = 0;
	} else {
		_editMsgId = 0;
		_replyToId = readyToForward() ? 0 : _history->localDraft()->msgId;
	}
	updateCmdStartShown();
	updateControlsVisibility();
	updateControlsGeometry();
	refreshTopBarActiveChat();
	if (_editMsgId || _replyToId) {
		updateReplyEditTexts();
		if (!_replyEditMsg) {
			requestMessageData(_editMsgId ? _editMsgId : _replyToId);
		}
	}
}

void HistoryWidget::applyCloudDraft(History *history) {
	Expects(!session().supportMode());

	if (_history == history && !_editMsgId) {
		applyDraft(Ui::InputField::HistoryAction::NewEntry);

		updateControlsVisibility();
		updateControlsGeometry();
	}
}

bool HistoryWidget::insideJumpToEndInsteadOfToUnread() const {
	if (session().supportMode()) {
		return true;
	} else if (!_historyInited) {
		return false;
	}
	_history->calculateFirstUnreadMessage();
	const auto unread = _history->firstUnreadMessage();
	const auto visibleBottom = _scroll->scrollTop() + _scroll->height();
	return unread && _list->itemTop(unread) <= visibleBottom;
}

void HistoryWidget::showHistory(
		const PeerId &peerId,
		MsgId showAtMsgId,
		bool reload) {
	_pinnedClickedId = FullMsgId();
	_minPinnedId = std::nullopt;

	const auto wasDialogsEntryState = computeDialogsEntryState();
	const auto startBot = (showAtMsgId == ShowAndStartBotMsgId);
	if (startBot) {
		showAtMsgId = ShowAtTheEndMsgId;
	}

	clearHighlightMessages();
	hideInfoTooltip(anim::type::instant);
	if (_history) {
		if (_peer->id == peerId && !reload) {
			updateForwarding();

			if (showAtMsgId == ShowAtUnreadMsgId
				&& insideJumpToEndInsteadOfToUnread()) {
				showAtMsgId = ShowAtTheEndMsgId;
			}
			if (!IsServerMsgId(showAtMsgId)
				&& !IsServerMsgId(-showAtMsgId)) {
				// To end or to unread.
				destroyUnreadBar();
			}
			const auto canShowNow = _history->isReadyFor(showAtMsgId);
			if (!canShowNow) {
				delayedShowAt(showAtMsgId);
			} else {
				_history->forgetScrollState();
				if (_migrated) {
					_migrated->forgetScrollState();
				}

				clearDelayedShowAt();
				while (_replyReturn) {
					if (_replyReturn->history() == _history && _replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					} else if (_replyReturn->history() == _migrated && -_replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					} else {
						break;
					}
				}

				setMsgId(showAtMsgId);
				if (_historyInited) {
					const auto to = countInitialScrollTop();
					const auto item = getItemFromHistoryOrMigrated(
						_showAtMsgId);
					animatedScrollToY(
						std::clamp(to, 0, _scroll->scrollTopMax()),
						item);
				} else {
					historyLoaded();
				}
			}

			_topBar->update();
			update();

			if (const auto user = _peer->asUser()) {
				if (const auto &info = user->botInfo) {
					if (startBot) {
						if (wasDialogsEntryState.key) {
							info->inlineReturnTo = wasDialogsEntryState;
						}
						sendBotStartCommand();
						_history->clearLocalDraft();
						applyDraft();
						_send->finishAnimating();
					}
				}
			}
			return;
		}
		session().sendProgressManager().update(
			_history,
			Api::SendProgressType::Typing,
			-1);
		session().data().histories().sendPendingReadInbox(_history);
		session().sendProgressManager().cancelTyping(_history);
	}

	clearReplyReturns();
	if (_history) {
		if (Ui::InFocusChain(_list)) {
			// Removing focus from list clears selected and updates top bar.
			setFocus();
		}
		controller()->session().api().saveCurrentDraftToCloud();
		if (_migrated) {
			_migrated->clearDrafts(); // use migrated draft only once
		}

		_history->showAtMsgId = _showAtMsgId;

		destroyUnreadBarOnClose();
		_pinnedBar = nullptr;
		_pinnedTracker = nullptr;
		_groupCallBar = nullptr;
		_groupCallTracker = nullptr;
		_membersDropdown.destroy();
		_scrollToAnimation.stop();

		clearAllLoadRequests();
		_history = _migrated = nullptr;
		_list = nullptr;
		_peer = nullptr;
		_channel = NoChannel;
		_canSendMessages = false;
		_silent.destroy();
		updateBotKeyboard();
	} else {
		Assert(_list == nullptr);
	}

	App::clearMousedItems();

	_addToScroll = 0;
	_saveEditMsgRequestId = 0;
	_replyEditMsg = nullptr;
	_editMsgId = _replyToId = 0;
	_previewData = nullptr;
	_previewCache.clear();
	_fieldBarCancel->hide();

	_membersDropdownShowTimer.cancel();
	_scroll->takeWidget<HistoryInner>().destroy();

	clearInlineBot();

	_showAtMsgId = showAtMsgId;
	_historyInited = false;
	_contactStatus = nullptr;

	// Unload lottie animations.
	session().data().unloadHeavyViewParts(HistoryInner::ElementDelegate());

	if (peerId) {
		_peer = session().data().peer(peerId);
		_channel = peerToChannel(_peer->id);
		_canSendMessages = _peer->canWrite();
		_contactStatus = std::make_unique<HistoryView::ContactStatus>(
			controller(),
			this,
			_peer);
		_contactStatus->heightValue() | rpl::start_with_next([=] {
			updateControlsGeometry();
		}, _contactStatus->lifetime());
		orderWidgets();
		controller()->tabbedSelector()->setCurrentPeer(_peer);
	}
	refreshTabbedPanel();

	if (_peer) {
		_unblock->setText(((_peer->isUser()
			&& _peer->asUser()->isBot()
			&& !_peer->asUser()->isSupport())
				? tr::lng_restart_button(tr::now)
				: tr::lng_unblock_button(tr::now)).toUpper());
		if (const auto channel = _peer->asChannel()) {
			channel->updateFull();
			_joinChannel->setText((channel->isMegagroup()
				? tr::lng_profile_join_group(tr::now)
				: tr::lng_profile_join_channel(tr::now)).toUpper());
		}
	}

	noSelectingScroll();
	_nonEmptySelection = false;

	if (_peer) {
		_history = _peer->owner().history(_peer);
		_migrated = _history->migrateFrom();
		if (_migrated
			&& !_migrated->isEmpty()
			&& (!_history->loadedAtTop() || !_migrated->loadedAtBottom())) {
			_migrated->clear(History::ClearType::Unload);
		}
		_history->setFakeUnreadWhileOpened(true);

		refreshTopBarActiveChat();
		updateTopBarSelection();

		if (_channel) {
			updateNotifyControls();
			session().data().requestNotifySettings(_peer);
			refreshSilentToggle();
		} else if (_peer->isRepliesChat()) {
			updateNotifyControls();
		}
		refreshScheduledToggle();

		if (_showAtMsgId == ShowAtUnreadMsgId) {
			if (_history->scrollTopItem) {
				_showAtMsgId = _history->showAtMsgId;
			}
		} else {
			_history->forgetScrollState();
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		}

		_scroll->hide();
		_list = _scroll->setOwnedWidget(
			object_ptr<HistoryInner>(this, _scroll, controller(), _history));
		_list->show();

		_updateHistoryItems.cancel();

		setupPinnedTracker();
		setupGroupCallTracker();
		if (_history->scrollTopItem
			|| (_migrated && _migrated->scrollTopItem)
			|| _history->isReadyFor(_showAtMsgId)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}

		handlePeerUpdate();

		session().local().readDraftsWithCursors(_history);
		applyDraft();
		_send->finishAnimating();

		updateControlsGeometry();

		connect(_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));

		if (const auto user = _peer->asUser()) {
			if (const auto &info = user->botInfo) {
				if (startBot) {
					if (wasDialogsEntryState.key) {
						info->inlineReturnTo = wasDialogsEntryState;
					}
					sendBotStartCommand();
				}
			}
		}
		if (!_history->folderKnown()) {
			session().data().histories().requestDialogEntry(_history);
		}
		if (_history->chatListUnreadMark()) {
			_history->owner().histories().changeDialogUnreadMark(
				_history,
				false);
			if (_migrated) {
				_migrated->owner().histories().changeDialogUnreadMark(
					_migrated,
					false);
			}

			// Must be done before unreadCountUpdated(), or we auto-close.
			_history->setUnreadMark(false);
			if (_migrated) {
				_migrated->setUnreadMark(false);
			}
		}
		unreadCountUpdated(); // set _historyDown badge.
		showAboutTopPromotion();
	} else {
		refreshTopBarActiveChat();
		updateTopBarSelection();

		clearFieldText();
		doneShow();
	}
	updateForwarding();
	updateOverStates(mapFromGlobal(QCursor::pos()));

	if (_history) {
		controller()->setActiveChatEntry({
			_history,
			FullMsgId(_history->channelId(), _showAtMsgId) });
	}
	update();
	controller()->floatPlayerAreaUpdated();

	crl::on_main(App::wnd(), [] { App::wnd()->setInnerFocus(); });
}

void HistoryWidget::clearDelayedShowAt() {
	_delayedShowAtMsgId = -1;
	clearDelayedShowAtRequest();
}

void HistoryWidget::clearDelayedShowAtRequest() {
	Expects(_history != nullptr);

	if (_delayedShowAtRequest) {
		_history->owner().histories().cancelRequest(_delayedShowAtRequest);
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::clearAllLoadRequests() {
	Expects(_history != nullptr);

	auto &histories = _history->owner().histories();
	clearDelayedShowAtRequest();
	if (_firstLoadRequest) {
		histories.cancelRequest(_firstLoadRequest);
		_firstLoadRequest = 0;
	}
	if (_preloadRequest) {
		histories.cancelRequest(_preloadRequest);
		_preloadRequest = 0;
	}
	if (_preloadDownRequest) {
		histories.cancelRequest(_preloadDownRequest);
		_preloadDownRequest = 0;
	}
}

void HistoryWidget::updateFieldSubmitSettings() {
	const auto settings = _isInlineBot
		? Ui::InputField::SubmitSettings::None
		: Core::App().settings().sendSubmitWay();
	_field->setSubmitSettings(settings);
}

void HistoryWidget::updateNotifyControls() {
	if (!_peer || (!_peer->isChannel() && !_peer->isRepliesChat())) {
		return;
	}

	_muteUnmute->setText((_history->mute()
		? tr::lng_channel_unmute(tr::now)
		: tr::lng_channel_mute(tr::now)).toUpper());
	if (!session().data().notifySilentPostsUnknown(_peer)) {
		if (_silent) {
			_silent->setChecked(session().data().notifySilentPosts(_peer));
			updateFieldPlaceholder();
		} else if (hasSilentToggle()) {
			refreshSilentToggle();
			updateControlsVisibility();
			updateControlsGeometry();
		}
	}
}

void HistoryWidget::refreshSilentToggle() {
	if (!_silent && hasSilentToggle()) {
		_silent.create(this, _peer->asChannel());
		orderWidgets();
	} else if (_silent && !hasSilentToggle()) {
		_silent.destroy();
	}
}

void HistoryWidget::setupScheduledToggle() {
	controller()->activeChatValue(
	) | rpl::map([=](const Dialogs::Key &key) -> rpl::producer<> {
		if (const auto history = key.history()) {
			return session().data().scheduledMessages().updates(history);
		}
		return rpl::never<rpl::empty_value>();
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=] {
		refreshScheduledToggle();
		updateControlsVisibility();
		updateControlsGeometry();
	}, lifetime());
}

void HistoryWidget::refreshScheduledToggle() {
	const auto has = _history
		&& _peer->canWrite()
		&& (session().data().scheduledMessages().count(_history) > 0);
	if (!_scheduled && has) {
		_scheduled.create(this, st::historyScheduledToggle);
		_scheduled->show();
		_scheduled->addClickHandler([=] {
			controller()->showSection(
				std::make_shared<HistoryView::ScheduledMemento>(_history));
		});
		orderWidgets(); // Raise drag areas to the top.
	} else if (_scheduled && !has) {
		_scheduled.destroy();
	}
}

bool HistoryWidget::contentOverlapped(const QRect &globalRect) {
	return (_attachDragAreas.document->overlaps(globalRect)
			|| _attachDragAreas.photo->overlaps(globalRect)
			|| _fieldAutocomplete->overlaps(globalRect)
			|| (_tabbedPanel && _tabbedPanel->overlaps(globalRect))
			|| (_inlineResults && _inlineResults->overlaps(globalRect)));
}

bool HistoryWidget::canWriteMessage() const {
	if (!_history || !_canSendMessages) return false;
	if (isBlocked() || isJoinChannel() || isMuteUnmute() || isBotStart()) return false;
	return true;
}

std::optional<QString> HistoryWidget::writeRestriction() const {
	return _peer
		? Data::RestrictionError(_peer, ChatRestriction::f_send_messages)
		: std::nullopt;
}

void HistoryWidget::updateControlsVisibility() {
	if (!_a_show.animating()) {
		_topShadow->setVisible(_peer != nullptr);
		_topBar->setVisible(_peer != nullptr);
	}
	updateHistoryDownVisibility();
	updateUnreadMentionsVisibility();
	if (!_history || _a_show.animating()) {
		hideChildWidgets();
		return;
	}

	if (_pinnedBar) {
		_pinnedBar->show();
	}
	if (_groupCallBar) {
		_groupCallBar->show();
	}
	if (_firstLoadRequest && !_scroll->isHidden()) {
		_scroll->hide();
	} else if (!_firstLoadRequest && _scroll->isHidden()) {
		_scroll->show();
	}
	if (_contactStatus) {
		_contactStatus->show();
	}
	if (!editingMessage() && (isBlocked() || isJoinChannel() || isMuteUnmute() || isBotStart())) {
		if (isBlocked()) {
			_joinChannel->hide();
			_muteUnmute->hide();
			_botStart->hide();
			if (_unblock->isHidden()) {
				_unblock->clearState();
				_unblock->show();
			}
		} else if (isJoinChannel()) {
			_unblock->hide();
			_muteUnmute->hide();
			_botStart->hide();
			if (_joinChannel->isHidden()) {
				_joinChannel->clearState();
				_joinChannel->show();
			}
		} else if (isMuteUnmute()) {
			_unblock->hide();
			_joinChannel->hide();
			_botStart->hide();
			if (_muteUnmute->isHidden()) {
				_muteUnmute->clearState();
				_muteUnmute->show();
			}
		} else if (isBotStart()) {
			_unblock->hide();
			_joinChannel->hide();
			_muteUnmute->hide();
			if (_botStart->isHidden()) {
				_botStart->clearState();
				_botStart->show();
			}
		}
		_kbShown = false;
		_fieldAutocomplete->hide();
		if (_supportAutocomplete) {
			_supportAutocomplete->hide();
		}
		_send->hide();
		if (_silent) {
			_silent->hide();
		}
		if (_scheduled) {
			_scheduled->hide();
		}
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		_tabbedSelectorToggle->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		if (_tabbedPanel) {
			_tabbedPanel->hide();
		}
		if (_voiceRecordBar) {
			_voiceRecordBar->hideFast();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		if (!_field->isHidden()) {
			_field->hide();
			updateControlsGeometry();
			update();
		}
	} else if (editingMessage() || _canSendMessages) {
		checkFieldAutocomplete();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_send->show();
		updateSendButtonType();

		_field->show();
		if (_kbShown) {
			_kbScroll->show();
			_tabbedSelectorToggle->hide();
			_botKeyboardHide->show();
			_botKeyboardShow->hide();
			_botCommandStart->hide();
		} else if (_kbReplyTo) {
			_kbScroll->hide();
			_tabbedSelectorToggle->show();
			_botKeyboardHide->hide();
			_botKeyboardShow->hide();
			_botCommandStart->hide();
		} else {
			_kbScroll->hide();
			_tabbedSelectorToggle->show();
			_botKeyboardHide->hide();
			if (_keyboard->hasMarkup()) {
				_botKeyboardShow->show();
				_botCommandStart->hide();
			} else {
				_botKeyboardShow->hide();
				_botCommandStart->setVisible(_cmdStartShown);
			}
		}
		_attachToggle->show();
		if (_silent) {
			_silent->show();
		}
		if (_scheduled) {
			_scheduled->show();
		}
		updateFieldPlaceholder();

		if (_editMsgId || _replyToId || readyToForward() || (_previewData && _previewData->pendingTill >= 0) || _kbReplyTo) {
			if (_fieldBarCancel->isHidden()) {
				_fieldBarCancel->show();
				updateControlsGeometry();
				update();
			}
		} else {
			_fieldBarCancel->hide();
		}
	} else {
		_fieldAutocomplete->hide();
		if (_supportAutocomplete) {
			_supportAutocomplete->hide();
		}
		_send->hide();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_attachToggle->hide();
		if (_silent) {
			_silent->hide();
		}
		if (_scheduled) {
			_scheduled->hide();
		}
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		_tabbedSelectorToggle->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		if (_tabbedPanel) {
			_tabbedPanel->hide();
		}
		if (_voiceRecordBar) {
			_voiceRecordBar->hideFast();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		_kbScroll->hide();
		if (!_field->isHidden()) {
			_field->hide();
			updateControlsGeometry();
			update();
		}
	}
	//checkTabbedSelectorToggleTooltip();
	updateMouseTracking();
}

void HistoryWidget::showAboutTopPromotion() {
	Expects(_history != nullptr);
	Expects(_list != nullptr);

	if (!_history->useTopPromotion() || _history->topPromotionAboutShown()) {
		return;
	}
	_history->markTopPromotionAboutShown();
	const auto type = _history->topPromotionType();
	const auto custom = type.isEmpty()
		? QString()
		: Lang::GetNonDefaultValue(kPsaAboutPrefix + type.toUtf8());
	const auto text = type.isEmpty()
		? tr::lng_proxy_sponsor_about(tr::now, Ui::Text::RichLangValue)
		: custom.isEmpty()
		? tr::lng_about_psa_default(tr::now, Ui::Text::RichLangValue)
		: Ui::Text::RichLangValue(custom);
	showInfoTooltip(text, nullptr);
}

void HistoryWidget::updateMouseTracking() {
	const auto trackMouse = !_fieldBarCancel->isHidden();
	setMouseTracking(trackMouse);
}

void HistoryWidget::destroyUnreadBar() {
	if (_history) _history->destroyUnreadBar();
	if (_migrated) _migrated->destroyUnreadBar();
}

void HistoryWidget::destroyUnreadBarOnClose() {
	if (!_history || !_historyInited) {
		return;
	} else if (_scroll->scrollTop() == _scroll->scrollTopMax()) {
		destroyUnreadBar();
		return;
	}
	const auto top = unreadBarTop();
	if (top && *top < _scroll->scrollTop()) {
		destroyUnreadBar();
		return;
	}
}

void HistoryWidget::unreadMessageAdded(not_null<HistoryItem*> item) {
	if (_history != item->history() || !_historyInited) {
		return;
	}

	// If we get here in non-resized state we can't rely on results of
	// doWeReadServerHistory() and mark chat as read.
	// If we receive N messages being not at bottom:
	// - on first message we set unreadcount += 1, firstUnreadMessage.
	// - on second we get wrong doWeReadServerHistory() and read both.
	session().data().sendHistoryChangeNotifications();

	const auto atBottom = (_scroll->scrollTop() >= _scroll->scrollTopMax());
	if (!atBottom) {
		return;
	}
	destroyUnreadBar();
	if (!doWeReadServerHistory()) {
		return;
	}
	if (item->isUnreadMention() && !item->isUnreadMedia()) {
		session().api().markMediaRead(item);
	}
	session().data().histories().readInboxOnNewMessage(item);

	// Also clear possible scheduled messages notifications.
	Core::App().notifications().clearFromHistory(_history);
}

void HistoryWidget::unreadCountUpdated() {
	if (_history->chatListUnreadMark()) {
		crl::on_main(this, [=, history = _history] {
			if (history == _history) {
				controller()->showBackFromStack();
				_cancelRequests.fire({});
			}
		});
	} else {
		updateHistoryDownVisibility();
		_historyDown->setUnreadCount(_history->chatListUnreadCount());
	}
}

void HistoryWidget::messagesFailed(const RPCError &error, int requestId) {
	if (error.type() == qstr("CHANNEL_PRIVATE")
		&& _peer->isChannel()
		&& _peer->asChannel()->invitePeekExpires()) {
		_peer->asChannel()->privateErrorReceived();
	} else if (error.type() == qstr("CHANNEL_PRIVATE")
		|| error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA")
		|| error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		auto was = _peer;
		controller()->showBackFromStack();
		Ui::ShowMultilineToast({
			.text = ((was && was->isMegagroup())
				? tr::lng_group_not_accessible(tr::now)
				: tr::lng_channel_not_accessible(tr::now)),
		});
		return;
	}

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_preloadRequest == requestId) {
		_preloadRequest = 0;
	} else if (_preloadDownRequest == requestId) {
		_preloadDownRequest = 0;
	} else if (_firstLoadRequest == requestId) {
		_firstLoadRequest = 0;
		controller()->showBackFromStack();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, int requestId) {
	Expects(_history != nullptr);

	bool toMigrated = (peer == _peer->migrateFrom());
	if (peer != _peer && !toMigrated) {
		if (_preloadRequest == requestId) {
			_preloadRequest = 0;
		} else if (_preloadDownRequest == requestId) {
			_preloadDownRequest = 0;
		} else if (_firstLoadRequest == requestId) {
			_firstLoadRequest = 0;
		} else if (_delayedShowAtRequest == requestId) {
			_delayedShowAtRequest = 0;
		}
		return;
	}

	auto count = 0;
	const QVector<MTPMessage> emptyList, *histList = &emptyList;
	switch (messages.type()) {
	case mtpc_messages_messages: {
		auto &d(messages.c_messages_messages());
		_history->owner().processUsers(d.vusers());
		_history->owner().processChats(d.vchats());
		histList = &d.vmessages().v;
		count = histList->size();
	} break;
	case mtpc_messages_messagesSlice: {
		auto &d(messages.c_messages_messagesSlice());
		_history->owner().processUsers(d.vusers());
		_history->owner().processChats(d.vchats());
		histList = &d.vmessages().v;
		count = d.vcount().v;
	} break;
	case mtpc_messages_channelMessages: {
		auto &d(messages.c_messages_channelMessages());
		if (peer && peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts().v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		_history->owner().processUsers(d.vusers());
		_history->owner().processChats(d.vchats());
		histList = &d.vmessages().v;
		count = d.vcount().v;
	} break;
	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (HistoryWidget::messagesReceived)"));
	} break;
	}

	const auto ExtractFirstId = [&] {
		return histList->empty() ? -1 : IdFromMessage(histList->front());
	};
	const auto ExtractLastId = [&] {
		return histList->empty() ? -1 : IdFromMessage(histList->back());
	};
	const auto PeerString = [](PeerId peerId) {
		if (peerIsUser(peerId)) {
			return QString("User-%1").arg(peerToUser(peerId));
		} else if (peerIsChat(peerId)) {
			return QString("Chat-%1").arg(peerToChat(peerId));
		} else if (peerIsChannel(peerId)) {
			return QString("Channel-%1").arg(peerToChannel(peerId));
		}
		return QString("Bad-%1").arg(peerId);
	};

	if (_preloadRequest == requestId) {
		auto to = toMigrated ? _migrated : _history;
		addMessagesToFront(peer, *histList);
		_preloadRequest = 0;
		preloadHistoryIfNeeded();
	} else if (_preloadDownRequest == requestId) {
		auto to = toMigrated ? _migrated : _history;
		addMessagesToBack(peer, *histList);
		_preloadDownRequest = 0;
		preloadHistoryIfNeeded();
		if (_history->loadedAtBottom()) {
			checkHistoryActivation();
		}
	} else if (_firstLoadRequest == requestId) {
		if (toMigrated) {
			_history->clear(History::ClearType::Unload);
		} else if (_migrated) {
			_migrated->clear(History::ClearType::Unload);
		}
		addMessagesToFront(peer, *histList);
		_firstLoadRequest = 0;
		if (_history->loadedAtTop() && _history->isEmpty() && count > 0) {
			firstLoadMessages();
			return;
		}

		historyLoaded();
	} else if (_delayedShowAtRequest == requestId) {
		if (toMigrated) {
			_history->clear(History::ClearType::Unload);
		} else if (_migrated) {
			_migrated->clear(History::ClearType::Unload);
		}

		clearAllLoadRequests();
		_firstLoadRequest = -1; // hack - don't updateListSize yet
		_history->getReadyFor(_delayedShowAtMsgId);
		if (_history->isEmpty()) {
			addMessagesToFront(peer, *histList);
		}
		_firstLoadRequest = 0;

		if (_history->loadedAtTop()
			&& _history->isEmpty()
			&& count > 0) {
			firstLoadMessages();
			return;
		}
		while (_replyReturn) {
			if (_replyReturn->history() == _history
				&& _replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			} else if (_replyReturn->history() == _migrated
				&& -_replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			} else {
				break;
			}
		}

		_delayedShowAtRequest = 0;
		setMsgId(_delayedShowAtMsgId);
		historyLoaded();
	}
}

void HistoryWidget::historyLoaded() {
	_historyInited = false;
	doneShow();
}

void HistoryWidget::windowShown() {
	updateControlsGeometry();
}

bool HistoryWidget::doWeReadServerHistory() const {
	return doWeReadMentions() && !session().supportMode();
}

bool HistoryWidget::doWeReadMentions() const {
	return _history
		&& _list
		&& _historyInited
		&& !_firstLoadRequest
		&& !_delayedShowAtRequest
		&& !_a_show.animating()
		&& controller()->widget()->doWeMarkAsRead();
}

void HistoryWidget::checkHistoryActivation() {
	if (_list) {
		_list->checkHistoryActivation();
	}
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) {
		return;
	}

	auto from = _history;
	auto offsetId = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = around;
		} else if (const auto around = _history->loadAroundId()) {
			_history->getReadyFor(_showAtMsgId);
			offset = -loadCount / 2;
			offsetId = around;
		} else {
			_history->getReadyFor(ShowAtTheEndMsgId);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		_history->getReadyFor(_showAtMsgId);
		loadCount = kMessagesPerPageFirst;
	} else if (_showAtMsgId > 0) {
		_history->getReadyFor(_showAtMsgId);
		offset = -loadCount / 2;
		offsetId = _showAtMsgId;
	} else if (_showAtMsgId < 0 && _history->isChannel()) {
		if (_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId && _migrated) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = -_showAtMsgId;
		} else if (_showAtMsgId == SwitchAtTopMsgId) {
			_history->getReadyFor(_showAtMsgId);
		}
	}

	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_firstLoadRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _firstLoadRequest);
			finish();
		}).fail([=](const RPCError &error) {
			messagesFailed(error, _firstLoadRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::loadMessages() {
	if (!_history || _preloadRequest) {
		return;
	}

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	auto loadMigrated = _migrated
		&& (_history->isEmpty()
			|| _history->loadedAtTop()
			|| (!_migrated->isEmpty() && !_migrated->loadedAtBottom()));
	auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtTop()) {
		return;
	}

	auto offsetId = from->minMsgId();
	auto addOffset = 0;
	auto loadCount = offsetId
		? kMessagesPerPage
		: kMessagesPerPageFirst;
	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_preloadRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _preloadRequest);
			finish();
		}).fail([=](const RPCError &error) {
			messagesFailed(error, _preloadRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _preloadDownRequest) {
		return;
	}

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	auto loadMigrated = _migrated && !(_migrated->isEmpty() || _migrated->loadedAtBottom() || (!_history->isEmpty() && !_history->loadedAtTop()));
	auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtBottom()) {
		return;
	}

	auto loadCount = kMessagesPerPage;
	auto addOffset = -loadCount;
	auto offsetId = from->maxMsgId();
	if (!offsetId) {
		if (loadMigrated || !_migrated) return;
		++offsetId;
		++addOffset;
	}
	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_preloadDownRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input,
			MTP_int(offsetId + 1),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _preloadDownRequest);
			finish();
		}).fail([=](const RPCError &error) {
			messagesFailed(error, _preloadDownRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::delayedShowAt(MsgId showAtMsgId) {
	if (!_history
		|| (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId)) {
		return;
	}

	clearAllLoadRequests();
	_delayedShowAtMsgId = showAtMsgId;

	auto from = _history;
	auto offsetId = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = around;
		} else if (const auto around = _history->loadAroundId()) {
			offset = -loadCount / 2;
			offsetId = around;
		} else {
			loadCount = kMessagesPerPageFirst;
		}
	} else if (_delayedShowAtMsgId == ShowAtTheEndMsgId) {
		loadCount = kMessagesPerPageFirst;
	} else if (_delayedShowAtMsgId > 0) {
		offset = -loadCount / 2;
		offsetId = _delayedShowAtMsgId;
	} else if (_delayedShowAtMsgId < 0 && _history->isChannel()) {
		if (_delayedShowAtMsgId < 0 && -_delayedShowAtMsgId < ServerMaxMsgId && _migrated) {
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = -_delayedShowAtMsgId;
		}
	}
	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_delayedShowAtRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _delayedShowAtRequest);
			finish();
		}).fail([=](const RPCError &error) {
			messagesFailed(error, _delayedShowAtRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::handleScroll() {
	preloadHistoryIfNeeded();
	visibleAreaUpdated();
	updatePinnedViewer();
	if (!_synteticScrollEvent) {
		_lastUserScrolled = crl::now();
	}
	const auto scrollTop = _scroll->scrollTop();
	if (scrollTop != _lastScrollTop) {
		if (!_synteticScrollEvent) {
			checkLastPinnedClickedIdReset(_lastScrollTop, scrollTop);
		}
		_lastScrolled = crl::now();
		_lastScrollTop = scrollTop;
	}
}

bool HistoryWidget::isItemCompletelyHidden(HistoryItem *item) const {
	const auto view = item ? item->mainView() : nullptr;
	if (!view) {
		return true;
	}
	auto top = _list ? _list->itemTop(item) : -2;
	if (top < 0) {
		return true;
	}

	auto bottom = top + view->height();
	auto scrollTop = _scroll->scrollTop();
	auto scrollBottom = scrollTop + _scroll->height();
	return (top >= scrollBottom || bottom <= scrollTop);
}

void HistoryWidget::visibleAreaUpdated() {
	if (_list && !_scroll->isHidden()) {
		const auto scrollTop = _scroll->scrollTop();
		const auto scrollBottom = scrollTop + _scroll->height();
		_list->visibleAreaUpdated(scrollTop, scrollBottom);
		controller()->floatPlayerAreaUpdated();
	}
}

void HistoryWidget::preloadHistoryIfNeeded() {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_peer
		|| !_historyInited) {
		return;
	}

	updateHistoryDownVisibility();
	updateUnreadMentionsVisibility();
	if (!_scrollToAnimation.animating()) {
		preloadHistoryByScroll();
		checkReplyReturns();
	}
}

void HistoryWidget::preloadHistoryByScroll() {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_peer
		|| !_historyInited) {
		return;
	}

	auto scrollTop = _scroll->scrollTop();
	auto scrollTopMax = _scroll->scrollTopMax();
	auto scrollHeight = _scroll->height();
	if (scrollTop + kPreloadHeightsCount * scrollHeight >= scrollTopMax) {
		loadMessagesDown();
	}
	if (scrollTop <= kPreloadHeightsCount * scrollHeight) {
		loadMessages();
	}
}

void HistoryWidget::checkReplyReturns() {
	if (_firstLoadRequest
		|| _scroll->isHidden()
		|| !_peer
		|| !_historyInited) {
		return;
	}
	auto scrollTop = _scroll->scrollTop();
	auto scrollTopMax = _scroll->scrollTopMax();
	auto scrollHeight = _scroll->height();
	while (_replyReturn) {
		auto below = (!_replyReturn->mainView() && _replyReturn->history() == _history && !_history->isEmpty() && _replyReturn->id < _history->blocks.back()->messages.back()->data()->id);
		if (!below) {
			below = (!_replyReturn->mainView() && _replyReturn->history() == _migrated && !_history->isEmpty());
		}
		if (!below) {
			below = (!_replyReturn->mainView() && _migrated && _replyReturn->history() == _migrated && !_migrated->isEmpty() && _replyReturn->id < _migrated->blocks.back()->messages.back()->data()->id);
		}
		if (!below && _replyReturn->mainView()) {
			below = (scrollTop >= scrollTopMax) || (_list->itemTop(_replyReturn) < scrollTop + scrollHeight / 2);
		}
		if (below) {
			calcNextReplyReturn();
		} else {
			break;
		}
	}
}

void HistoryWidget::cancelInlineBot() {
	auto &textWithTags = _field->getTextWithTags();
	if (textWithTags.text.size() > _inlineBotUsername.size() + 2) {
		setFieldText(
			{ '@' + _inlineBotUsername + ' ', TextWithTags::Tags() },
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
	} else {
		clearFieldText(
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
	}
}

void HistoryWidget::windowIsVisibleChanged() {
	InvokeQueued(this, [=] {
		preloadHistoryIfNeeded();
	});
}

void HistoryWidget::historyDownClicked() {
	if (QGuiApplication::keyboardModifiers() == Qt::ControlModifier) {
		showHistory(_peer->id, ShowAtUnreadMsgId);
	} else if (_replyReturn && _replyReturn->history() == _history) {
		showHistory(_peer->id, _replyReturn->id);
	} else if (_replyReturn && _replyReturn->history() == _migrated) {
		showHistory(_peer->id, -_replyReturn->id);
	} else if (_peer) {
		showHistory(_peer->id, ShowAtUnreadMsgId);
	}
}

void HistoryWidget::showNextUnreadMention() {
	const auto msgId = _history->getMinLoadedUnreadMention();
	const auto already = (_showAtMsgId == msgId);

	// Mark mention voice/video message as read.
	// See https://github.com/telegramdesktop/tdesktop/issues/5623
	if (msgId && already) {
		const auto item = _history->owner().message(
			_history->channelId(),
			msgId);
		if (const auto media = item ? item->media() : nullptr) {
			if (const auto document = media->document()) {
				if (!media->webpage()
					&& (document->isVoiceMessage()
						|| document->isVideoMessage())) {
					document->owner().markMediaRead(document);
				}
			}
		}
	}
	showHistory(_peer->id, msgId);
}

void HistoryWidget::saveEditMsg() {
	Expects(_history != nullptr);

	if (_saveEditMsgRequestId) {
		return;
	}

	const auto item = session().data().message(_channel, _editMsgId);
	if (!item) {
		cancelEdit();
		return;
	}
	const auto webPageId = _previewCancelled
		? CancelledWebPageId
		: ((_previewData && _previewData->pendingTill >= 0)
			? _previewData->id
			: WebPageId(0));

	const auto textWithTags = _field->getTextWithAppliedMarkdown();
	const auto prepareFlags = Ui::ItemTextOptions(
		_history,
		session().user()).flags;
	auto sending = TextWithEntities();
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);

	if (!TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		const auto suggestModerateActions = false;
		Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
		return;
	} else if (!left.text.isEmpty()) {
		Ui::show(Box<InformBox>(tr::lng_edit_too_long(tr::now)));
		return;
	}

	const auto weak = Ui::MakeWeak(this);
	const auto history = _history;

	const auto done = [=](const MTPUpdates &result, mtpRequestId requestId) {
		crl::guard(weak, [=] {
			if (requestId == _saveEditMsgRequestId) {
				_saveEditMsgRequestId = 0;
				cancelEdit();
			}
		})();
		if (const auto editDraft = history->localEditDraft()) {
			if (editDraft->saveRequestId == requestId) {
				history->clearLocalEditDraft();
				history->session().local().writeDrafts(history);
			}
		}
	};

	const auto fail = [=](const RPCError &error, mtpRequestId requestId) {
		if (const auto editDraft = history->localEditDraft()) {
			if (editDraft->saveRequestId == requestId) {
				editDraft->saveRequestId = 0;
			}
		}
		crl::guard(weak, [=] {
			if (requestId == _saveEditMsgRequestId) {
				_saveEditMsgRequestId = 0;
			}
			const auto &err = error.type();
			if (ranges::contains(Api::kDefaultEditMessagesErrors, err)) {
				Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
			} else if (err == u"MESSAGE_NOT_MODIFIED"_q) {
				cancelEdit();
			} else if (err == u"MESSAGE_EMPTY"_q) {
				_field->selectAll();
				_field->setFocus();
			} else {
				Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
			}
			update();
		})();
	};

	_saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		{ .removeWebPageId = (webPageId == CancelledWebPageId) },
		done,
		fail);
}

void HistoryWidget::hideChildWidgets() {
	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
	}
	if (_pinnedBar) {
		_pinnedBar->hide();
	}
	if (_groupCallBar) {
		_groupCallBar->hide();
	}
	if (_voiceRecordBar) {
		_voiceRecordBar->hideFast();
	}
	hideChildren();
}

void HistoryWidget::hideSelectorControlsAnimated() {
	_fieldAutocomplete->hideAnimated();
	if (_supportAutocomplete) {
		_supportAutocomplete->hide();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideAnimated();
	}
	if (_inlineResults) {
		_inlineResults->hideAnimated();
	}
}

void HistoryWidget::send(Api::SendOptions options) {
	if (!_history) {
		return;
	} else if (_editMsgId) {
		saveEditMsg();
		return;
	} else if (!options.scheduled && showSlowmodeError()) {
		return;
	}

	if (_voiceRecordBar->isListenState()) {
		_voiceRecordBar->requestToSendWithOptions(options);
		return;
	}

	const auto webPageId = _previewCancelled
		? CancelledWebPageId
		: ((_previewData && _previewData->pendingTill >= 0)
			? _previewData->id
			: WebPageId(0));

	auto message = ApiWrap::MessageToSend(_history);
	message.textWithTags = _field->getTextWithAppliedMarkdown();
	message.action.options = options;
	message.action.replyTo = replyToId();
	message.webPageId = webPageId;

	if (_canSendMessages) {
		const auto error = GetErrorTextForSending(
			_peer,
			_toForward,
			message.textWithTags,
			options.scheduled);
		if (!error.isEmpty()) {
			Ui::ShowMultilineToast({
				.text = { error },
			});
			return;
		}
	}

	session().api().sendMessage(std::move(message));

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	hideSelectorControlsAnimated();

	if (_previewData && _previewData->pendingTill) previewCancel();
	_field->setFocus();

	if (!_keyboard->hasMarkup() && _keyboard->forceReply() && !_kbReplyTo) {
		toggleKeyboard();
	}
	session().changes().historyUpdated(
		_history,
		(options.scheduled
			? Data::HistoryUpdate::Flag::ScheduledSent
			: Data::HistoryUpdate::Flag::MessageSent));
}

void HistoryWidget::sendWithModifiers(Qt::KeyboardModifiers modifiers) {
	auto options = Api::SendOptions();
	options.handleSupportSwitch = Support::HandleSwitch(modifiers);
	send(options);
}

void HistoryWidget::sendSilent() {
	auto options = Api::SendOptions();
	options.silent = true;
	send(options);
}

void HistoryWidget::sendScheduled() {
	if (!_list) {
		return;
	}
	const auto callback = [=](Api::SendOptions options) { send(options); };
	Ui::show(
		HistoryView::PrepareScheduleBox(_list, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

SendMenu::Type HistoryWidget::sendMenuType() const {
	return !_peer
		? SendMenu::Type::Disabled
		: _peer->isSelf()
		? SendMenu::Type::Reminder
		: HistoryView::CanScheduleUntilOnline(_peer)
		? SendMenu::Type::ScheduledToUser
		: SendMenu::Type::Scheduled;
}

auto HistoryWidget::computeSendButtonType() const {
	using Type = Ui::SendButton::Type;

	if (_editMsgId) {
		return Type::Save;
	} else if (_isInlineBot) {
		return Type::Cancel;
	} else if (showRecordButton()) {
		return Type::Record;
	}
	return Type::Send;
}

SendMenu::Type HistoryWidget::sendButtonMenuType() const {
	return (computeSendButtonType() == Ui::SendButton::Type::Send)
		? sendMenuType()
		: SendMenu::Type::Disabled;
}

void HistoryWidget::unblockUser() {
	if (const auto user = _peer ? _peer->asUser() : nullptr) {
		Window::PeerMenuUnblockUserWithBotRestart(user);
	} else {
		updateControlsVisibility();
	}
}

void HistoryWidget::sendBotStartCommand() {
	if (!_peer
		|| !_peer->isUser()
		|| !_peer->asUser()->isBot()
		|| !_canSendMessages) {
		updateControlsVisibility();
		return;
	}
	session().api().sendBotStart(_peer->asUser());
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::joinChannel() {
	if (!_peer || !_peer->isChannel() || !isJoinChannel()) {
		updateControlsVisibility();
		return;
	}
	session().api().joinChannel(_peer->asChannel());
}

void HistoryWidget::toggleMuteUnmute() {
	const auto muteForSeconds = _history->mute()
		? 0
		: Data::NotifySettings::kDefaultMutePeriod;
	session().data().updateNotifySettings(_peer, muteForSeconds);
}

History *HistoryWidget::history() const {
	return _history;
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

// Sometimes _showAtMsgId is set directly.
void HistoryWidget::setMsgId(MsgId showAtMsgId) {
	if (_showAtMsgId != showAtMsgId) {
		auto wasMsgId = _showAtMsgId;
		_showAtMsgId = showAtMsgId;
		if (_history) {
			controller()->setActiveChatEntry({
				_history,
				FullMsgId(_history->channelId(), _showAtMsgId) });
		}
	}
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

void HistoryWidget::showAnimated(
		Window::SlideDirection direction,
		const Window::SectionSlideParams &params) {
	_showDirection = direction;

	_a_show.stop();

	_cacheUnder = params.oldContentCache;

	// If we show pinned bar here, we don't want it to change the
	// calculated and prepared scrollTop of the messages history.
	_preserveScrollTop = true;
	show();
	_topBar->finishAnimating();
	historyDownAnimationFinish();
	unreadMentionsAnimationFinish();
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	_topShadow->setVisible(params.withTopBarShadow ? false : true);
	_preserveScrollTop = false;

	_cacheOver = controller()->content()->grabForShowAnimation(params);

	hideChildWidgets();
	if (params.withTopBarShadow) _topShadow->show();

	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([=] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());
	if (_history) {
		_topBar->show();
		_topBar->setAnimatingMode(true);
	}

	activate();
}

void HistoryWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		historyDownAnimationFinish();
		unreadMentionsAnimationFinish();
		if (_pinnedBar) {
			_pinnedBar->finishAnimating();
		}
		if (_groupCallBar) {
			_groupCallBar->finishAnimating();
		}
		_cacheUnder = _cacheOver = QPixmap();
		doneShow();
		synteticScrollToY(_scroll->scrollTop());
	}
}

void HistoryWidget::doneShow() {
	_topBar->setAnimatingMode(false);
	updateBotKeyboard();
	updateControlsVisibility();
	if (!_historyInited) {
		updateHistoryGeometry(true);
	} else {
		handlePendingHistoryUpdate();
	}
	// If we show pinned bar here, we don't want it to change the
	// calculated and prepared scrollTop of the messages history.
	_preserveScrollTop = true;
	preloadHistoryIfNeeded();
	updatePinnedViewer();
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	checkHistoryActivation();
	controller()->widget()->setInnerFocus();
	_preserveScrollTop = false;
}

void HistoryWidget::finishAnimating() {
	if (!_a_show.animating()) return;
	_a_show.stop();
	_topShadow->setVisible(_peer != nullptr);
	_topBar->setVisible(_peer != nullptr);
	historyDownAnimationFinish();
	unreadMentionsAnimationFinish();
}

void HistoryWidget::historyDownAnimationFinish() {
	_historyDownShown.stop();
	updateHistoryDownPosition();
}

void HistoryWidget::unreadMentionsAnimationFinish() {
	_unreadMentionsShown.stop();
	updateUnreadMentionsPosition();
}

void HistoryWidget::chooseAttach() {
	if (_editMsgId) {
		Ui::show(Box<InformBox>(tr::lng_edit_caption_attach(tr::now)));
		return;
	}

	if (!_peer || !_peer->canWrite()) {
		return;
	} else if (const auto error = Data::RestrictionError(
			_peer,
			ChatRestriction::f_send_media)) {
		Ui::ShowMultilineToast({
			.text = { *error },
		});
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	const auto filter = FileDialog::AllOrImagesFilter();

	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), filter, crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto animated = false;
			auto image = App::readImage(
				result.remoteContent,
				nullptr,
				false,
				&animated);
			if (!image.isNull() && !animated) {
				confirmSendingFiles(
					std::move(image),
					std::move(result.remoteContent));
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize);
			confirmSendingFiles(std::move(list));
		}
	}), nullptr);
}

void HistoryWidget::sendButtonClicked() {
	const auto type = _send->type();
	if (type == Ui::SendButton::Type::Cancel) {
		cancelInlineBot();
	} else if (type != Ui::SendButton::Type::Record) {
		send({});
	}
}

void HistoryWidget::leaveEventHook(QEvent *e) {
	if (hasMouseTracking()) {
		mouseMoveEvent(nullptr);
	}
}

void HistoryWidget::mouseMoveEvent(QMouseEvent *e) {
	auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
	updateOverStates(pos);
}

void HistoryWidget::updateOverStates(QPoint pos) {
	auto inReplyEditForward = QRect(st::historyReplySkip, _field->y() - st::historySendPadding - st::historyReplyHeight, width() - st::historyReplySkip - _fieldBarCancel->width(), st::historyReplyHeight).contains(pos) && (_editMsgId || replyToId() || readyToForward());
	auto inClickable = inReplyEditForward;
	_inReplyEditForward = inReplyEditForward;
	if (inClickable != _inClickable) {
		_inClickable = inClickable;
		setCursor(_inClickable ? style::cur_pointer : style::cur_default);
	}
}

void HistoryWidget::leaveToChildEvent(QEvent *e, QWidget *child) { // e -- from enterEvent() of child TWidget
	if (hasMouseTracking()) {
		updateOverStates(mapFromGlobal(QCursor::pos()));
	}
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_replyForwardPressed) {
		_replyForwardPressed = false;
		update(0, _field->y() - st::historySendPadding - st::historyReplyHeight, width(), st::historyReplyHeight);
	}
}

void HistoryWidget::sendBotCommand(
		not_null<PeerData*> peer,
		UserData *bot,
		const QString &cmd,
		MsgId replyTo) { // replyTo != 0 from ReplyKeyboardMarkup, == 0 from cmd links
	if (_peer != peer.get()) {
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, replyTo));

	// 'bot' may be nullptr in case of sending from FieldAutocomplete.
	const auto toSend = (replyTo || !bot)
		? cmd
		: HistoryView::WrapBotCommandInChat(_peer, cmd, bot);

	auto message = ApiWrap::MessageToSend(_history);
	message.textWithTags = { toSend, TextWithTags::Tags() };
	message.action.replyTo = replyTo
		? ((!_peer->isUser()/* && (botStatus == 0 || botStatus == 2)*/)
			? replyTo
			: replyToId())
		: 0;
	session().api().sendMessage(std::move(message));
	if (replyTo) {
		if (_replyToId == replyTo) {
			cancelReply();
			saveCloudDraft();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) toggleKeyboard(false);
			_history->lastKeyboardUsed = true;
		}
	}

	_field->setFocus();
}

void HistoryWidget::hideSingleUseKeyboard(PeerData *peer, MsgId replyTo) {
	if (!_peer || _peer != peer) return;

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, replyTo));
	if (replyTo) {
		if (_replyToId == replyTo) {
			cancelReply();
			saveCloudDraft();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) toggleKeyboard(false);
			_history->lastKeyboardUsed = true;
		}
	}
}

bool HistoryWidget::insertBotCommand(const QString &cmd) {
	if (!canWriteMessage()) return false;

	auto insertingInlineBot = !cmd.isEmpty() && (cmd.at(0) == '@');
	auto toInsert = cmd;
	if (!toInsert.isEmpty() && !insertingInlineBot) {
		auto bot = _peer->isUser()
			? _peer
			: (App::hoveredLinkItem()
				? App::hoveredLinkItem()->data()->fromOriginal().get()
				: nullptr);
		if (bot && (!bot->isUser() || !bot->asUser()->isBot())) {
			bot = nullptr;
		}
		auto username = bot ? bot->asUser()->username : QString();
		auto botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
		if (toInsert.indexOf('@') < 0 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
			toInsert += '@' + username;
		}
	}
	toInsert += ' ';

	if (!insertingInlineBot) {
		auto &textWithTags = _field->getTextWithTags();
		TextWithTags textWithTagsToSet;
		QRegularExpressionMatch m = QRegularExpression(qsl("^/[A-Za-z_0-9]{0,64}(@[A-Za-z_0-9]{0,32})?(\\s|$)")).match(textWithTags.text);
		if (m.hasMatch()) {
			textWithTagsToSet = _field->getTextWithTagsPart(m.capturedLength());
		} else {
			textWithTagsToSet = textWithTags;
		}
		textWithTagsToSet.text = toInsert + textWithTagsToSet.text;
		for (auto &tag : textWithTagsToSet.tags) {
			tag.offset += toInsert.size();
		}
		_field->setTextWithTags(textWithTagsToSet);

		QTextCursor cur(_field->textCursor());
		cur.movePosition(QTextCursor::End);
		_field->setTextCursor(cur);
	} else {
		setFieldText(
			{ toInsert, TextWithTags::Tags() },
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
		_field->setFocus();
		return true;
	}
	return false;
}

bool HistoryWidget::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::KeyPress) {
		const auto k = static_cast<QKeyEvent*>(e);
		if ((k->modifiers() & kCommonModifiers) == Qt::ControlModifier) {
			if (k->key() == Qt::Key_Up) {
#ifdef Q_OS_MAC
				// Cmd + Up is used instead of Home.
				if (!_field->textCursor().atStart()) {
					return false;
				}
#endif
				return replyToPreviousMessage();
			} else if (k->key() == Qt::Key_Down) {
#ifdef Q_OS_MAC
				// Cmd + Down is used instead of End.
				if (!_field->textCursor().atEnd()) {
					return false;
				}
#endif
				return replyToNextMessage();
			}
		}
	}
	if ((obj == _historyDown || obj == _unreadMentions) && e->type() == QEvent::Wheel) {
		return _scroll->viewportEvent(e);
	}
	return TWidget::eventFilter(obj, e);
}

bool HistoryWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _peer ? _scroll->viewportEvent(e) : false;
}

QRect HistoryWidget::floatPlayerAvailableRect() {
	return _peer ? mapToGlobal(_scroll->geometry()) : mapToGlobal(rect());
}

bool HistoryWidget::readyToForward() const {
	return _canSendMessages && !_toForward.empty();
}

bool HistoryWidget::hasSilentToggle() const {
	return _peer
		&& _peer->isChannel()
		&& !_peer->isMegagroup()
		&& _peer->canWrite()
		&& !session().data().notifySilentPostsUnknown(_peer);
}

void HistoryWidget::handleSupportSwitch(not_null<History*> updated) {
	if (_history != updated || !session().supportMode()) {
		return;
	}

	const auto setting = session().settings().supportSwitch();
	if (auto method = Support::GetSwitchMethod(setting)) {
		crl::on_main(this, std::move(method));
	}
}

void HistoryWidget::inlineBotResolveDone(
		const MTPcontacts_ResolvedPeer &result) {
	Expects(result.type() == mtpc_contacts_resolvedPeer);

	_inlineBotResolveRequestId = 0;
	const auto &data = result.c_contacts_resolvedPeer();
	const auto resolvedBot = [&]() -> UserData* {
		if (const auto result = session().data().processUsers(data.vusers())) {
			if (result->isBot()
				&& !result->botInfo->inlinePlaceholder.isEmpty()) {
				return result;
			}
		}
		return nullptr;
	}();
	session().data().processChats(data.vchats());

	const auto query = ParseInlineBotQuery(&session(), _field);
	if (_inlineBotUsername == query.username) {
		applyInlineBotQuery(
			query.lookingUpBot ? resolvedBot : query.bot,
			query.query);
	} else {
		clearInlineBot();
	}
}

void HistoryWidget::inlineBotResolveFail(
		const RPCError &error,
		const QString &username) {
	_inlineBotResolveRequestId = 0;
	if (username == _inlineBotUsername) {
		clearInlineBot();
	}
}

bool HistoryWidget::isBotStart() const {
	const auto user = _peer ? _peer->asUser() : nullptr;
	if (!user
		|| !user->isBot()
		|| !_canSendMessages) {
		return false;
	} else if (!user->botInfo->startToken.isEmpty()) {
		return true;
	} else if (_history->isEmpty() && !_history->lastMessage()) {
		return true;
	}
	return false;
}

bool HistoryWidget::isBlocked() const {
	return _peer && _peer->isUser() && _peer->asUser()->isBlocked();
}

bool HistoryWidget::isJoinChannel() const {
	return _peer && _peer->isChannel() && !_peer->asChannel()->amIn();
}

bool HistoryWidget::isMuteUnmute() const {
	return _peer
		&& ((_peer->isBroadcast()
			&& !_peer->asChannel()->canPublish())
			|| _peer->isRepliesChat());
}

bool HistoryWidget::showRecordButton() const {
	return Media::Capture::instance()->available()
		&& !_voiceRecordBar->isListenState()
		&& !HasSendText(_field)
		&& !readyToForward()
		&& !_editMsgId;
}

bool HistoryWidget::showInlineBotCancel() const {
	return _inlineBot && !_inlineLookingUpBot;
}

void HistoryWidget::updateSendButtonType() {
	using Type = Ui::SendButton::Type;

	const auto type = computeSendButtonType();
	_send->setType(type);

	// This logic is duplicated in RepliesWidget.
	const auto disabledBySlowmode = _peer
		&& _peer->slowmodeApplied()
		&& (_history->latestSendingMessage() != nullptr);

	const auto delay = [&] {
		return (type != Type::Cancel && type != Type::Save && _peer)
			? _peer->slowmodeSecondsLeft()
			: 0;
	}();
	_send->setSlowmodeDelay(delay);
	_send->setDisabled(disabledBySlowmode
		&& (type == Type::Send || type == Type::Record));

	if (delay != 0) {
		base::call_delayed(
			kRefreshSlowmodeLabelTimeout,
			this,
			[=] { updateSendButtonType(); });
	}
}

bool HistoryWidget::updateCmdStartShown() {
	bool cmdStartShown = false;
	if (_history && _peer && ((_peer->isChat() && _peer->asChat()->botStatus > 0) || (_peer->isMegagroup() && _peer->asChannel()->mgInfo->botStatus > 0) || (_peer->isUser() && _peer->asUser()->isBot()))) {
		if (!isBotStart() && !isBlocked() && !_keyboard->hasMarkup() && !_keyboard->forceReply() && !_editMsgId) {
			if (!HasSendText(_field)) {
				cmdStartShown = true;
			}
		}
	}
	if (_cmdStartShown != cmdStartShown) {
		_cmdStartShown = cmdStartShown;
		return true;
	}
	return false;
}

bool HistoryWidget::kbWasHidden() const {
	return _history && (_keyboard->forMsgId() == FullMsgId(_history->channelId(), _history->lastKeyboardHiddenId));
}

void HistoryWidget::toggleKeyboard(bool manual) {
	auto fieldEnabled = canWriteMessage() && !_a_show.animating();
	if (_kbShown || _kbReplyTo) {
		_botKeyboardHide->hide();
		if (_kbShown) {
			if (fieldEnabled) {
				_botKeyboardShow->show();
			}
			if (manual && _history) {
				_history->lastKeyboardHiddenId = _keyboard->forMsgId().msg;
			}

			_kbScroll->hide();
			_kbShown = false;

			_field->setMaxHeight(computeMaxFieldHeight());

			_kbReplyTo = nullptr;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_editMsgId && !_replyToId) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		} else {
			if (_history) {
				_history->clearLastKeyboard();
			} else {
				updateBotKeyboard();
			}
		}
	} else if (!_keyboard->hasMarkup() && _keyboard->forceReply()) {
		_botKeyboardHide->hide();
		_botKeyboardShow->hide();
		if (fieldEnabled) {
			_botCommandStart->show();
		}
		_kbScroll->hide();
		_kbShown = false;

		_field->setMaxHeight(computeMaxFieldHeight());

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply())
			? session().data().message(_keyboard->forMsgId())
			: nullptr;
		if (_kbReplyTo && !_editMsgId && !_replyToId && fieldEnabled) {
			updateReplyToName();
			updateReplyEditText(_kbReplyTo);
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	} else if (fieldEnabled) {
		_botKeyboardHide->show();
		_botKeyboardShow->hide();
		_kbScroll->show();
		_kbShown = true;

		const auto maxheight = computeMaxFieldHeight();
		const auto kbheight = qMin(_keyboard->height(), maxheight - (maxheight / 2));
		_field->setMaxHeight(maxheight - kbheight);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply())
			? session().data().message(_keyboard->forMsgId())
			: nullptr;
		if (_kbReplyTo && !_editMsgId && !_replyToId) {
			updateReplyToName();
			updateReplyEditText(_kbReplyTo);
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	}
	updateControlsGeometry();
	if (_botKeyboardHide->isHidden() && canWriteMessage() && !_a_show.animating()) {
		_tabbedSelectorToggle->show();
	} else {
		_tabbedSelectorToggle->hide();
	}
	updateField();
}

void HistoryWidget::startBotCommand() {
	setFieldText(
		{ qsl("/"), TextWithTags::Tags() },
		0,
		Ui::InputField::HistoryAction::NewEntry);
}

void HistoryWidget::setMembersShowAreaActive(bool active) {
	if (!active) {
		_membersDropdownShowTimer.cancel();
	}
	if (active && _peer && (_peer->isChat() || _peer->isMegagroup())) {
		if (_membersDropdown) {
			_membersDropdown->otherEnter();
		} else if (!_membersDropdownShowTimer.isActive()) {
			_membersDropdownShowTimer.callOnce(kShowMembersDropdownTimeoutMs);
		}
	} else if (_membersDropdown) {
		_membersDropdown->otherLeave();
	}
}

void HistoryWidget::showMembersDropdown() {
	if (!_peer) {
		return;
	}
	if (!_membersDropdown) {
		_membersDropdown.create(this, st::membersInnerDropdown);
		_membersDropdown->setOwnedWidget(object_ptr<Profile::GroupMembersWidget>(this, _peer, st::membersInnerItem));
		_membersDropdown->resizeToWidth(st::membersInnerWidth);

		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
		_membersDropdown->moveToLeft(0, _topBar->height());
		_membersDropdown->setHiddenCallback([this] { _membersDropdown.destroyDelayed(); });
	}
	_membersDropdown->otherEnter();
}

bool HistoryWidget::pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params) {
	if (!_tabbedPanel) {
		return true;
	} else if (!peer->canWrite()) {
		Core::App().settings().setTabbedReplacedWithInfo(true);
		controller()->showPeerInfo(peer, params.withThirdColumn());
		return false;
	}
	Core::App().settings().setTabbedReplacedWithInfo(false);
	controller()->resizeForThirdSection();
	controller()->showSection(
		std::make_shared<ChatHelpers::TabbedMemento>(),
		params.withThirdColumn());
	return true;
}

bool HistoryWidget::returnTabbedSelector() {
	createTabbedPanel();
	moveFieldControls();
	return true;
}

void HistoryWidget::createTabbedPanel() {
	setTabbedPanel(std::make_unique<TabbedPanel>(
		this,
		controller(),
		controller()->tabbedSelector()));
}

void HistoryWidget::setTabbedPanel(std::unique_ptr<TabbedPanel> panel) {
	_tabbedPanel = std::move(panel);
	if (const auto raw = _tabbedPanel.get()) {
		_tabbedSelectorToggle->installEventFilter(raw);
		_tabbedSelectorToggle->setColorOverrides(nullptr, nullptr, nullptr);
	} else {
		_tabbedSelectorToggle->setColorOverrides(
			&st::historyAttachEmojiActive,
			&st::historyRecordVoiceFgActive,
			&st::historyRecordVoiceRippleBgActive);
	}
}

bool HistoryWidget::preventsClose(Fn<void()> &&continueCallback) const {
	if (_voiceRecordBar->isActive()) {
		_voiceRecordBar->showDiscardBox(std::move(continueCallback));
		return true;
	}
	return false;
}

void HistoryWidget::toggleTabbedSelectorMode() {
	if (!_peer) {
		return;
	}
	if (_tabbedPanel) {
		if (controller()->canShowThirdSection() && !Adaptive::OneColumn()) {
			Core::App().settings().setTabbedSelectorSectionEnabled(true);
			Core::App().saveSettingsDelayed();
			pushTabbedSelectorToThirdSection(
				_peer,
				Window::SectionShow::Way::ClearStack);
		} else {
			_tabbedPanel->toggleAnimated();
		}
	} else {
		controller()->closeThirdSection();
	}
}

void HistoryWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Adaptive::ChatLayout::Normal
		: Adaptive::ChatLayout::Wide;
	if (layout != Global::AdaptiveChatLayout()) {
		Global::SetAdaptiveChatLayout(layout);
		Adaptive::Changed().notify(true);
	}
}

void HistoryWidget::moveFieldControls() {
	auto keyboardHeight = 0;
	auto bottom = height();
	auto maxKeyboardHeight = computeMaxFieldHeight() - _field->height();
	_keyboard->resizeToWidth(width(), maxKeyboardHeight);
	if (_kbShown) {
		keyboardHeight = qMin(_keyboard->height(), maxKeyboardHeight);
		bottom -= keyboardHeight;
		_kbScroll->setGeometryToLeft(0, bottom, width(), keyboardHeight);
	}

// _attachToggle --------- _inlineResults -------------------------------------- _tabbedPanel --------- _fieldBarCancel
// (_attachDocument|_attachPhoto) _field (_scheduled) (_silent|_cmdStart|_kbShow) (_kbHide|_tabbedSelectorToggle) _send
// (_botStart|_unblock|_joinChannel|_muteUnmute)

	auto buttonsBottom = bottom - _attachToggle->height();
	auto left = 0;
	_attachToggle->moveToLeft(left, buttonsBottom); left += _attachToggle->width();
	_field->moveToLeft(left, bottom - _field->height() - st::historySendPadding);
	auto right = st::historySendRight;
	_send->moveToRight(right, buttonsBottom); right += _send->width();
	_voiceRecordBar->moveToLeft(0, bottom - _voiceRecordBar->height());
	_tabbedSelectorToggle->moveToRight(right, buttonsBottom);
	_botKeyboardHide->moveToRight(right, buttonsBottom); right += _botKeyboardHide->width();
	_botKeyboardShow->moveToRight(right, buttonsBottom);
	_botCommandStart->moveToRight(right, buttonsBottom);
	if (_silent) {
		_silent->moveToRight(right, buttonsBottom);
	}
	const auto kbShowShown = _history && !_kbShown && _keyboard->hasMarkup();
	if (kbShowShown || _cmdStartShown || _silent) {
		right += _botCommandStart->width();
	}
	if (_scheduled) {
		_scheduled->moveToRight(right, buttonsBottom);
	}

	_fieldBarCancel->moveToRight(0, _field->y() - st::historySendPadding - _fieldBarCancel->height());
	if (_inlineResults) {
		_inlineResults->moveBottom(_field->y() - st::historySendPadding);
	}
	if (_tabbedPanel) {
		_tabbedPanel->moveBottomRight(buttonsBottom, width());
	}

	const auto fullWidthButtonRect = myrtlrect(
		0,
		bottom - _botStart->height(),
		width(),
		_botStart->height());
	_botStart->setGeometry(fullWidthButtonRect);
	_unblock->setGeometry(fullWidthButtonRect);
	_joinChannel->setGeometry(fullWidthButtonRect);

	_muteUnmute->setGeometry(fullWidthButtonRect);
}

void HistoryWidget::updateFieldSize() {
	auto kbShowShown = _history && !_kbShown && _keyboard->hasMarkup();
	auto fieldWidth = width() - _attachToggle->width() - st::historySendRight;
	fieldWidth -= _send->width();
	fieldWidth -= _tabbedSelectorToggle->width();
	if (kbShowShown) fieldWidth -= _botKeyboardShow->width();
	if (_cmdStartShown) fieldWidth -= _botCommandStart->width();
	if (_silent) fieldWidth -= _silent->width();
	if (_scheduled) fieldWidth -= _scheduled->width();

	if (_field->width() != fieldWidth) {
		_field->resize(fieldWidth, _field->height());
	} else {
		moveFieldControls();
	}
}

void HistoryWidget::clearInlineBot() {
	if (_inlineBot || _inlineLookingUpBot) {
		_inlineBot = nullptr;
		_inlineLookingUpBot = false;
		inlineBotChanged();
		_field->finishAnimating();
	}
	if (_inlineResults) {
		_inlineResults->clearInlineBot();
	}
	checkFieldAutocomplete();
}

void HistoryWidget::inlineBotChanged() {
	bool isInlineBot = showInlineBotCancel();
	if (_isInlineBot != isInlineBot) {
		_isInlineBot = isInlineBot;
		updateFieldPlaceholder();
		updateFieldSubmitSettings();
		updateControlsVisibility();
	}
}

void HistoryWidget::fieldResized() {
	moveFieldControls();
	updateHistoryGeometry();
	updateField();
}

void HistoryWidget::fieldFocused() {
	if (_list) {
		_list->clearSelected(true);
	}
}

void HistoryWidget::checkFieldAutocomplete() {
	if (!_history || _a_show.animating()) {
		return;
	}

	const auto isInlineBot = _inlineBot && !_inlineLookingUpBot;
	const auto autocomplete = isInlineBot
		? AutocompleteQuery()
		: ParseMentionHashtagBotCommandQuery(_field);
	if (!autocomplete.query.isEmpty()) {
		if (autocomplete.query[0] == '#'
			&& cRecentWriteHashtags().isEmpty()
			&& cRecentSearchHashtags().isEmpty()) {
			session().local().readRecentHashtagsAndBots();
		} else if (autocomplete.query[0] == '@'
			&& cRecentInlineBots().isEmpty()) {
			session().local().readRecentHashtagsAndBots();
		} else if (autocomplete.query[0] == '/'
			&& ((_peer->isUser() && !_peer->asUser()->isBot())
				|| _editMsgId)) {
			return;
		}
	}
	_fieldAutocomplete->showFiltered(
		_peer,
		autocomplete.query,
		autocomplete.fromStart);
}

void HistoryWidget::updateFieldPlaceholder() {
	if (!_editMsgId && _inlineBot && !_inlineLookingUpBot) {
		_field->setPlaceholder(
			rpl::single(_inlineBot->botInfo->inlinePlaceholder.mid(1)),
			_inlineBot->username.size() + 2);
		return;
	}

	_field->setPlaceholder([&] {
		if (_editMsgId) {
			return tr::lng_edit_message_text();
		} else if (!_history) {
			return tr::lng_message_ph();
		} else if (const auto channel = _history->peer->asChannel()) {
			if (channel->isBroadcast()) {
				return session().data().notifySilentPosts(channel)
					? tr::lng_broadcast_silent_ph()
					: tr::lng_broadcast_ph();
			} else if (channel->adminRights() & ChatAdminRight::f_anonymous) {
				return tr::lng_send_anonymous_ph();
			} else {
				return tr::lng_message_ph();
			}
		} else {
			return tr::lng_message_ph();
		}
	}());
	updateSendButtonType();
}

bool HistoryWidget::showSendingFilesError(
		const Ui::PreparedList &list) const {
	const auto text = [&] {
		const auto error = _peer
			? Data::RestrictionError(
				_peer,
				ChatRestriction::f_send_media)
			: std::nullopt;
		if (error) {
			return *error;
		} else if (!canWriteMessage()) {
			return tr::lng_forward_send_files_cant(tr::now);
		}
		if (_peer->slowmodeApplied() && !list.canBeSentInSlowmode()) {
			return tr::lng_slowmode_no_many(tr::now);
		} else if (const auto left = _peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWords(left));
		}
		using Error = Ui::PreparedList::Error;
		switch (list.error) {
		case Error::None: return QString();
		case Error::EmptyFile:
		case Error::Directory:
		case Error::NonLocalUrl: return tr::lng_send_image_empty(
			tr::now,
			lt_name,
			list.errorData);
		case Error::TooLargeFile: return tr::lng_send_image_too_large(
			tr::now,
			lt_name,
			list.errorData);
		}
		return tr::lng_forward_send_files_cant(tr::now);
	}();
	if (text.isEmpty()) {
		return false;
	}

	Ui::ShowMultilineToast({
		.text = { text },
	});
	return true;
}

bool HistoryWidget::confirmSendingFiles(const QStringList &files) {
	return confirmSendingFiles(files, QString());
}

bool HistoryWidget::confirmSendingFiles(not_null<const QMimeData*> data) {
	return confirmSendingFiles(data, std::nullopt);
}

bool HistoryWidget::confirmSendingFiles(
		const QStringList &files,
		const QString &insertTextOnCancel) {
	return confirmSendingFiles(
		Storage::PrepareMediaList(files, st::sendMediaPreviewSize),
		insertTextOnCancel);
}

bool HistoryWidget::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (showSendingFilesError(list)) {
		return false;
	}
	if (_editMsgId) {
		Ui::show(Box<InformBox>(tr::lng_edit_caption_attach(tr::now)));
		return false;
	}

	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	const auto text = _field->getTextWithTags();
	using SendLimit = SendFilesBox::SendLimit;
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		text,
		_peer->slowmodeApplied() ? SendLimit::One : SendLimit::Many,
		Api::SendType::Normal,
		sendMenuType());
	_field->setTextWithTags({});
	box->setConfirmedCallback(crl::guard(this, [=](
			Ui::PreparedList &&list,
			Ui::SendFilesWay way,
			TextWithTags &&caption,
			Api::SendOptions options,
			bool ctrlShiftEnter) {
		sendingFilesConfirmed(
			std::move(list),
			way,
			std::move(caption),
			options,
			ctrlShiftEnter);
	}));
	box->setCancelledCallback(crl::guard(this, [=] {
		_field->setTextWithTags(text);
		auto cursor = _field->textCursor();
		cursor.setPosition(anchor);
		if (position != anchor) {
			cursor.setPosition(position, QTextCursor::KeepAnchor);
		}
		_field->setTextCursor(cursor);
		if (!insertTextOnCancel.isEmpty()) {
			_field->textCursor().insertText(insertTextOnCancel);
		}
	}));

	Window::ActivateWindow(controller());
	const auto shown = Ui::show(std::move(box));
	shown->setCloseByOutsideClick(false);

	return true;
}

void HistoryWidget::sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	Expects(list.filesToProcess.empty());

	if (showSendingFilesError(list)) {
		return;
	}
	auto groups = DivideByGroups(
		std::move(list),
		way,
		_peer->slowmodeApplied());
	const auto type = way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	action.options = options;
	action.clearDraft = false;
	if ((groups.size() != 1 || !groups.front().sentWithCaption())
		&& !caption.text.isEmpty()) {
		auto message = Api::MessageToSend(_history);
		message.textWithTags = base::take(caption);
		message.action = action;
		session().api().sendMessage(std::move(message));
	}
	for (auto &group : groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		session().api().sendFiles(
			std::move(group.list),
			type,
			base::take(caption),
			album,
			action);
	}
}

bool HistoryWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
	return confirmSendingFiles(std::move(list), insertTextOnCancel);
}

bool HistoryWidget::canSendFiles(not_null<const QMimeData*> data) const {
	if (!canWriteMessage()) {
		return false;
	} else if (data->hasImage()) {
		return true;
	} else if (const auto urls = data->urls(); !urls.empty()) {
		if (ranges::all_of(urls, &QUrl::isLocalFile)) {
			return true;
		}
	}
	return false;
}

bool HistoryWidget::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (!canWriteMessage()) {
		return false;
	}

	const auto hasImage = data->hasImage();

	if (const auto urls = data->urls(); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			if (list.error == Ui::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
				confirmSendingFiles(std::move(list), emptyTextOnCancel);
				return true;
			}
		}
	}

	if (hasImage) {
		auto image = Platform::GetImageFromClipboard();
		if (image.isNull()) {
			image = qvariant_cast<QImage>(data->imageData());
		}
		if (!image.isNull()) {
			confirmSendingFiles(
				std::move(image),
				QByteArray(),
				overrideSendImagesAsPhotos,
				insertTextOnCancel);
			return true;
		}
	}
	return false;
}

void HistoryWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	if (!canWriteMessage()) return;

	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	session().api().sendFile(fileContent, type, action);
}

void HistoryWidget::handleHistoryChange(not_null<const History*> history) {
	if (_list && (_history == history || _migrated == history)) {
		handlePendingHistoryUpdate();
		updateBotKeyboard();
		if (!_scroll->isHidden()) {
			const auto unblock = isBlocked();
			const auto botStart = isBotStart();
			const auto joinChannel = isJoinChannel();
			const auto muteUnmute = isMuteUnmute();
			const auto update = false
				|| (_unblock->isHidden() == unblock)
				|| (!unblock && _botStart->isHidden() == botStart)
				|| (!unblock
					&& !botStart
					&& _joinChannel->isHidden() == joinChannel)
				|| (!unblock
					&& !botStart
					&& !joinChannel
					&& _muteUnmute->isHidden() == muteUnmute);
			if (update) {
				updateControlsVisibility();
				updateControlsGeometry();
			}
		}
	}
}

QPixmap HistoryWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_topShadow->hide();
	}
	_inGrab = true;
	updateControlsGeometry();
	auto result = Ui::GrabWidget(this);
	_inGrab = false;
	updateControlsGeometry();
	if (params.withTopBarShadow) {
		_topShadow->show();
	}
	return result;
}

bool HistoryWidget::skipItemRepaint() {
	auto ms = crl::now();
	if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
		return false;
	}
	_updateHistoryItems.callOnce(
		_lastScrolled + kSkipRepaintWhileScrollMs - ms);
	return true;
}

void HistoryWidget::updateHistoryItemsByTimer() {
	if (!_list) {
		return;
	}

	auto ms = crl::now();
	if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
		_list->update();
	} else {
		_updateHistoryItems.callOnce(
			_lastScrolled + kSkipRepaintWhileScrollMs - ms);
	}
}

PeerData *HistoryWidget::ui_getPeerForMouseAction() {
	return _peer;
}

void HistoryWidget::handlePendingHistoryUpdate() {
	if (hasPendingResizedItems() || _updateHistoryGeometryRequired) {
		updateHistoryGeometry();
		_list->update();
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	//updateTabbedSelectorSectionShown();
	recountChatWidth();
	updateControlsGeometry();
}

void HistoryWidget::updateControlsGeometry() {
	_topBar->resizeToWidth(width());
	_topBar->moveToLeft(0, 0);
	_voiceRecordBar->resizeToWidth(width());

	moveFieldControls();

	const auto groupCallTop = _topBar->bottomNoMargins();
	if (_groupCallBar) {
		_groupCallBar->move(0, groupCallTop);
		_groupCallBar->resizeToWidth(width());
	}
	const auto pinnedBarTop = groupCallTop + (_groupCallBar ? _groupCallBar->height() : 0);
	if (_pinnedBar) {
		_pinnedBar->move(0, pinnedBarTop);
		_pinnedBar->resizeToWidth(width());
	}
	const auto contactStatusTop = pinnedBarTop + (_pinnedBar ? _pinnedBar->height() : 0);
	if (_contactStatus) {
		_contactStatus->move(0, contactStatusTop);
	}
	const auto scrollAreaTop = contactStatusTop + (_contactStatus ? _contactStatus->height() : 0);
	if (_scroll->y() != scrollAreaTop) {
		_scroll->moveToLeft(0, scrollAreaTop);
		_fieldAutocomplete->setBoundings(_scroll->geometry());
		if (_supportAutocomplete) {
			_supportAutocomplete->setBoundings(_scroll->geometry());
		}
	}

	updateHistoryGeometry(false, false, { ScrollChangeAdd, _topDelta });

	updateFieldSize();

	updateHistoryDownPosition();

	if (_membersDropdown) {
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}

	auto topShadowLeft = (Adaptive::OneColumn() || _inGrab) ? 0 : st::lineWidth;
	auto topShadowRight = (Adaptive::ThreeColumn() && !_inGrab && _peer) ? st::lineWidth : 0;
	_topShadow->setGeometryToLeft(
		topShadowLeft,
		_topBar->bottomNoMargins(),
		width() - topShadowLeft - topShadowRight,
		st::lineWidth);
}

void HistoryWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (item == _replyEditMsg && _editMsgId) {
		cancelEdit();
	}
	if (item == _replyEditMsg && _replyToId) {
		cancelReply();
	}
	while (item == _replyReturn) {
		calcNextReplyReturn();
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		toggleKeyboard();
		_kbReplyTo = nullptr;
	}
	auto found = ranges::find(_toForward, item);
	if (found != _toForward.end()) {
		_toForward.erase(found);
		updateForwardingTexts();
		if (_toForward.empty()) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
	}
}

void HistoryWidget::itemEdited(not_null<HistoryItem*> item) {
	if (item.get() == _replyEditMsg) {
		updateReplyEditTexts(true);
	}
}

void HistoryWidget::updateScrollColors() {
	_scroll->updateBars();
}

MsgId HistoryWidget::replyToId() const {
	return _replyToId ? _replyToId : (_kbReplyTo ? _kbReplyTo->id : 0);
}

int HistoryWidget::countInitialScrollTop() {
	if (_history->scrollTopItem || (_migrated && _migrated->scrollTopItem)) {
		return _list->historyScrollTop();
	} else if (_showAtMsgId
		&& (IsServerMsgId(_showAtMsgId)
			|| IsServerMsgId(-_showAtMsgId))) {
		const auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
		const auto itemTop = _list->itemTop(item);
		if (itemTop < 0) {
			setMsgId(0);
			return countInitialScrollTop();
		} else {
			const auto view = item->mainView();
			Assert(view != nullptr);

			enqueueMessageHighlight(view);
			const auto result = itemTopForHighlight(view);
			createUnreadBarIfBelowVisibleArea(result);
			return result;
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		return ScrollMax;
	} else if (const auto top = unreadBarTop()) {
		return *top;
	} else {
		_history->calculateFirstUnreadMessage();
		return countAutomaticScrollTop();
	}
}

void HistoryWidget::createUnreadBarIfBelowVisibleArea(int withScrollTop) {
	Expects(_history != nullptr);

	if (_history->unreadBar()) {
		return;
	}
	_history->calculateFirstUnreadMessage();
	if (const auto unread = _history->firstUnreadMessage()) {
		if (_list->itemTop(unread) > withScrollTop) {
			createUnreadBarAndResize();
		}
	}
}

void HistoryWidget::createUnreadBarAndResize() {
	if (!_history->firstUnreadMessage()) {
		return;
	}
	const auto was = base::take(_historyInited);
	_history->addUnreadBar();
	if (hasPendingResizedItems()) {
		updateListSize();
	}
	_historyInited = was;
}

int HistoryWidget::countAutomaticScrollTop() {
	Expects(_history != nullptr);
	Expects(_list != nullptr);

	if (const auto unread = _history->firstUnreadMessage()) {
		const auto firstUnreadTop = _list->itemTop(unread);
		const auto possibleUnreadBarTop = _scroll->scrollTopMax()
			+ HistoryView::UnreadBar::height()
			- HistoryView::UnreadBar::marginTop();
		if (firstUnreadTop < possibleUnreadBarTop) {
			createUnreadBarAndResize();
			if (_history->unreadBar() != nullptr) {
				setMsgId(ShowAtUnreadMsgId);
				return countInitialScrollTop();
			}
		}
	}
	return ScrollMax;
}

void HistoryWidget::updateHistoryGeometry(
		bool initial,
		bool loadedDown,
		const ScrollChange &change) {
	if (!_history || (initial && _historyInited) || (!initial && !_historyInited)) {
		return;
	}
	if (_firstLoadRequest || _a_show.animating()) {
		_updateHistoryGeometryRequired = true;
		return; // scrollTopMax etc are not working after recountHistoryGeometry()
	}

	auto newScrollHeight = height() - _topBar->height();
	if (_pinnedBar) {
		newScrollHeight -= _pinnedBar->height();
	}
	if (_groupCallBar) {
		newScrollHeight -= _groupCallBar->height();
	}
	if (_contactStatus) {
		newScrollHeight -= _contactStatus->height();
	}
	if (!editingMessage() && (isBlocked() || isBotStart() || isJoinChannel() || isMuteUnmute())) {
		newScrollHeight -= _unblock->height();
	} else {
		if (editingMessage() || _canSendMessages) {
			newScrollHeight -= (_field->height() + 2 * st::historySendPadding);
		} else if (writeRestriction().has_value()) {
			newScrollHeight -= _unblock->height();
		}
		if (_editMsgId || replyToId() || readyToForward() || (_previewData && _previewData->pendingTill >= 0)) {
			newScrollHeight -= st::historyReplyHeight;
		}
		if (_kbShown) {
			newScrollHeight -= _kbScroll->height();
		}
	}
	if (newScrollHeight <= 0) {
		return;
	}
	const auto wasScrollTop = _scroll->scrollTop();
	const auto wasAtBottom = (wasScrollTop == _scroll->scrollTopMax());
	const auto needResize = (_scroll->width() != width())
		|| (_scroll->height() != newScrollHeight);
	if (needResize) {
		_scroll->resize(width(), newScrollHeight);
		// on initial updateListSize we didn't put the _scroll->scrollTop correctly yet
		// so visibleAreaUpdated() call will erase it with the new (undefined) value
		if (!initial) {
			visibleAreaUpdated();
		}

		_fieldAutocomplete->setBoundings(_scroll->geometry());
		if (_supportAutocomplete) {
			_supportAutocomplete->setBoundings(_scroll->geometry());
		}
		if (!_historyDownShown.animating()) {
			// _historyDown is a child widget of _scroll, not me.
			_historyDown->moveToRight(st::historyToDownPosition.x(), _scroll->height() - _historyDown->height() - st::historyToDownPosition.y());
			if (!_unreadMentionsShown.animating()) {
				// _unreadMentions is a child widget of _scroll, not me.
				auto additionalSkip = _historyDownIsShown ? (_historyDown->height() + st::historyUnreadMentionsSkip) : 0;
				_unreadMentions->moveToRight(st::historyToDownPosition.x(), _scroll->height() - _unreadMentions->height() - additionalSkip - st::historyToDownPosition.y());
			}
		}

		controller()->floatPlayerAreaUpdated();
	}

	updateListSize();
	_updateHistoryGeometryRequired = false;

	auto newScrollTop = 0;
	if (initial) {
		newScrollTop = countInitialScrollTop();
		_historyInited = true;
		_scrollToAnimation.stop();
	} else if (wasAtBottom && !loadedDown && !_history->unreadBar()) {
		newScrollTop = countAutomaticScrollTop();
	} else {
		newScrollTop = std::min(
			_list->historyScrollTop(),
			_scroll->scrollTopMax());
		if (change.type == ScrollChangeAdd) {
			newScrollTop += change.value;
		} else if (change.type == ScrollChangeNoJumpToBottom) {
			newScrollTop = wasScrollTop;
		} else if (const auto add = base::take(_addToScroll)) {
			newScrollTop += add;
		}
	}
	const auto toY = std::clamp(newScrollTop, 0, _scroll->scrollTopMax());
	synteticScrollToY(toY);
}

void HistoryWidget::updateListSize() {
	_list->recountHistoryGeometry();
	auto washidden = _scroll->isHidden();
	if (washidden) {
		_scroll->show();
	}
	_list->updateSize();
	if (washidden) {
		_scroll->hide();
	}
	_updateHistoryGeometryRequired = true;
}

bool HistoryWidget::hasPendingResizedItems() const {
	return (_history && _history->hasPendingResizedItems())
		|| (_migrated && _migrated->hasPendingResizedItems());
}

std::optional<int> HistoryWidget::unreadBarTop() const {
	const auto bar = [&]() -> HistoryView::Element* {
		if (const auto bar = _migrated ? _migrated->unreadBar() : nullptr) {
			return bar;
		}
		return _history->unreadBar();
	}();
	if (bar) {
		const auto result = _list->itemTop(bar)
			+ HistoryView::UnreadBar::marginTop();
		if (bar->Has<HistoryView::DateBadge>()) {
			return result + bar->Get<HistoryView::DateBadge>()->height();
		}
		return result;
	}
	return std::nullopt;
}

void HistoryWidget::addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceived(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry();
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(
		PeerData *peer,
		const QVector<MTPMessage> &messages) {
	const auto checkForUnreadStart = [&] {
		if (_history->unreadBar() || !_history->trackUnreadMessages()) {
			return false;
		}
		_history->calculateFirstUnreadMessage();
		return !_history->firstUnreadMessage();
	}();
	_list->messagesReceivedDown(peer, messages);
	if (checkForUnreadStart) {
		_history->calculateFirstUnreadMessage();
		createUnreadBarAndResize();
	}
	if (!_firstLoadRequest) {
		updateHistoryGeometry(false, true, { ScrollChangeNoJumpToBottom, 0 });
	}
}

void HistoryWidget::updateBotKeyboard(History *h, bool force) {
	if (h && h != _history && h != _migrated) {
		return;
	}

	bool changed = false;
	bool wasVisible = _kbShown || _kbReplyTo;
	if ((_replyToId && !_replyEditMsg) || _editMsgId || !_history) {
		changed = _keyboard->updateMarkup(nullptr, force);
	} else if (_replyToId && _replyEditMsg) {
		changed = _keyboard->updateMarkup(_replyEditMsg, force);
	} else {
		const auto keyboardItem = _history->lastKeyboardId
			? session().data().message(_channel, _history->lastKeyboardId)
			: nullptr;
		changed = _keyboard->updateMarkup(keyboardItem, force);
	}
	updateCmdStartShown();
	if (!changed) return;

	bool hasMarkup = _keyboard->hasMarkup(), forceReply = _keyboard->forceReply() && (!_replyToId || !_replyEditMsg);
	if (hasMarkup || forceReply) {
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _history->lastKeyboardUsed) {
			_history->lastKeyboardHiddenId = _history->lastKeyboardId;
		}
		if (!isBotStart() && !isBlocked() && _canSendMessages && (wasVisible || (_replyToId && _replyEditMsg) || (!HasSendText(_field) && !kbWasHidden()))) {
			if (!_a_show.animating()) {
				if (hasMarkup) {
					_kbScroll->show();
					_tabbedSelectorToggle->hide();
					_botKeyboardHide->show();
				} else {
					_kbScroll->hide();
					_tabbedSelectorToggle->show();
					_botKeyboardHide->hide();
				}
				_botKeyboardShow->hide();
				_botCommandStart->hide();
			}
			const auto maxheight = computeMaxFieldHeight();
			const auto kbheight = hasMarkup ? qMin(_keyboard->height(), maxheight - (maxheight / 2)) : 0;
			_field->setMaxHeight(maxheight - kbheight);
			_kbShown = hasMarkup;
			_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply())
				? session().data().message(_keyboard->forMsgId())
				: nullptr;
			if (_kbReplyTo && !_replyToId) {
				updateReplyToName();
				updateReplyEditText(_kbReplyTo);
			}
		} else {
			if (!_a_show.animating()) {
				_kbScroll->hide();
				_tabbedSelectorToggle->show();
				_botKeyboardHide->hide();
				_botKeyboardShow->show();
				_botCommandStart->hide();
			}
			_field->setMaxHeight(computeMaxFieldHeight());
			_kbShown = false;
			_kbReplyTo = nullptr;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		}
	} else {
		if (!_scroll->isHidden()) {
			_kbScroll->hide();
			_tabbedSelectorToggle->show();
			_botKeyboardHide->hide();
			_botKeyboardShow->hide();
			_botCommandStart->setVisible(!_editMsgId);
		}
		_field->setMaxHeight(computeMaxFieldHeight());
		_kbShown = false;
		_kbReplyTo = nullptr;
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId && !_editMsgId) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
	}
	refreshTopBarActiveChat();
	updateControlsGeometry();
	update();
}

int HistoryWidget::computeMaxFieldHeight() const {
	const auto available = height()
		- _topBar->height()
		- (_contactStatus ? _contactStatus->height() : 0)
		- (_pinnedBar ? _pinnedBar->height() : 0)
		- (_groupCallBar ? _groupCallBar->height() : 0)
		- ((_editMsgId
			|| replyToId()
			|| readyToForward()
			|| (_previewData && _previewData->pendingTill >= 0))
			? st::historyReplyHeight
			: 0)
		- (2 * st::historySendPadding)
		- st::historyReplyHeight; // at least this height for history.
	return std::min(st::historyComposeFieldMaxHeight, available);
}

void HistoryWidget::updateHistoryDownPosition() {
	// _historyDown is a child widget of _scroll, not me.
	auto top = anim::interpolate(0, _historyDown->height() + st::historyToDownPosition.y(), _historyDownShown.value(_historyDownIsShown ? 1. : 0.));
	_historyDown->moveToRight(st::historyToDownPosition.x(), _scroll->height() - top);
	auto shouldBeHidden = !_historyDownIsShown && !_historyDownShown.animating();
	if (shouldBeHidden != _historyDown->isHidden()) {
		_historyDown->setVisible(!shouldBeHidden);
	}
	updateUnreadMentionsPosition();
}

void HistoryWidget::updateHistoryDownVisibility() {
	if (_a_show.animating()) return;

	const auto haveUnreadBelowBottom = [&](History *history) {
		if (!_list || !history || history->unreadCount() <= 0) {
			return false;
		}
		const auto unread = history->firstUnreadMessage();
		if (!unread) {
			return false;
		}
		const auto top = _list->itemTop(unread);
		return (top >= _scroll->scrollTop() + _scroll->height());
	};
	const auto historyDownIsVisible = [&] {
		if (!_list || _firstLoadRequest) {
			return false;
		}
		if (_voiceRecordBar->isLockPresent()) {
			return false;
		}
		if (!_history->loadedAtBottom() || _replyReturn) {
			return true;
		}
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax()) {
			return true;
		}
		if (haveUnreadBelowBottom(_history)
			|| haveUnreadBelowBottom(_migrated)) {
			return true;
		}
		return false;
	};
	auto historyDownIsShown = historyDownIsVisible();
	if (_historyDownIsShown != historyDownIsShown) {
		_historyDownIsShown = historyDownIsShown;
		_historyDownShown.start([=] { updateHistoryDownPosition(); }, _historyDownIsShown ? 0. : 1., _historyDownIsShown ? 1. : 0., st::historyToDownDuration);
	}
}

void HistoryWidget::updateUnreadMentionsPosition() {
	// _unreadMentions is a child widget of _scroll, not me.
	auto right = anim::interpolate(-_unreadMentions->width(), st::historyToDownPosition.x(), _unreadMentionsShown.value(_unreadMentionsIsShown ? 1. : 0.));
	auto shift = anim::interpolate(0, _historyDown->height() + st::historyUnreadMentionsSkip, _historyDownShown.value(_historyDownIsShown ? 1. : 0.));
	auto top = _scroll->height() - _unreadMentions->height() - st::historyToDownPosition.y() - shift;
	_unreadMentions->moveToRight(right, top);
	auto shouldBeHidden = !_unreadMentionsIsShown && !_unreadMentionsShown.animating();
	if (shouldBeHidden != _unreadMentions->isHidden()) {
		_unreadMentions->setVisible(!shouldBeHidden);
	}
}

void HistoryWidget::updateUnreadMentionsVisibility() {
	if (_a_show.animating()) return;

	auto showUnreadMentions = _peer && (_peer->isChat() || _peer->isMegagroup());
	if (showUnreadMentions) {
		session().api().preloadEnoughUnreadMentions(_history);
	}
	const auto unreadMentionsIsShown = [&] {
		if (!showUnreadMentions || _firstLoadRequest) {
			return false;
		}
		if (_voiceRecordBar->isLockPresent()) {
			return false;
		}
		if (!_history->getUnreadMentionsLoadedCount()) {
			return false;
		}
		// If we have an unheard voice message with the mention
		// and our message is the last one, we can't see the status
		// (delivered/read) of this message.
		// (Except for MacBooks with the TouchPad.)
		if (_scroll->scrollTop() == _scroll->scrollTopMax()) {
			if (const auto lastMessage = _history->lastMessage()) {
				return !lastMessage->from()->isSelf();
			}
		}
		return true;
	}();
	if (unreadMentionsIsShown) {
		_unreadMentions->setUnreadCount(_history->getUnreadMentionsCount());
	}
	if (_unreadMentionsIsShown != unreadMentionsIsShown) {
		_unreadMentionsIsShown = unreadMentionsIsShown;
		_unreadMentionsShown.start([=] { updateUnreadMentionsPosition(); }, _unreadMentionsIsShown ? 0. : 1., _unreadMentionsIsShown ? 1. : 0., st::historyToDownDuration);
	}
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	const auto hasSecondLayer = (_editMsgId
		|| _replyToId
		|| readyToForward()
		|| _kbReplyTo);
	_replyForwardPressed = hasSecondLayer && QRect(
		0,
		_field->y() - st::historySendPadding - st::historyReplyHeight,
		st::historyReplySkip,
		st::historyReplyHeight).contains(e->pos());
	if (_replyForwardPressed && !_fieldBarCancel->isHidden()) {
		updateField();
	} else if (_inReplyEditForward) {
		if (readyToForward()) {
			const auto items = std::move(_toForward);
			session().data().cancelForwarding(_history);
			auto list = ranges::view::all(
				items
			) | ranges::view::transform(
				&HistoryItem::fullId
			) | ranges::to_vector;
			Window::ShowForwardMessagesBox(controller(), std::move(list));
		} else {
			Ui::showPeerHistory(_peer, _editMsgId ? _editMsgId : replyToId());
		}
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	const auto commonModifiers = e->modifiers() & kCommonModifiers;
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		controller()->showBackFromStack();
		_cancelRequests.fire({});
	} else if (e->key() == Qt::Key_PageDown) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_PageUp) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Down && !commonModifiers) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Up && !commonModifiers) {
		const auto item = _history
			? _history->lastSentMessage()
			: nullptr;
		if (item
			&& item->allowsEdit(base::unixtime::now())
			&& _field->empty()
			&& !_editMsgId
			&& !_replyToId) {
			editMessage(item);
			return;
		}
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!_botStart->isHidden()) {
			sendBotStartCommand();
		}
		if (!_canSendMessages) {
			const auto submitting = Ui::InputField::ShouldSubmit(
				Core::App().settings().sendSubmitWay(),
				e->modifiers());
			if (submitting) {
				sendWithModifiers(e->modifiers());
			}
		}
	} else if (e->key() == Qt::Key_O && e->modifiers() == Qt::ControlModifier) {
		chooseAttach();
	} else {
		e->ignore();
	}
}

void HistoryWidget::handlePeerMigration() {
	const auto current = _peer->migrateToOrMe();
	const auto chat = current->migrateFrom();
	if (!chat) {
		return;
	}
	const auto channel = current->asChannel();
	Assert(channel != nullptr);

	if (_peer != channel) {
		showHistory(
			channel->id,
			(_showAtMsgId > 0) ? (-_showAtMsgId) : _showAtMsgId);
		channel->session().api().requestParticipantsCountDelayed(channel);
	} else {
		_migrated = _history->migrateFrom();
		_list->notifyMigrateUpdated();
		setupPinnedTracker();
		setupGroupCallTracker();
		updateHistoryGeometry();
	}
	const auto from = chat->owner().historyLoaded(chat);
	const auto to = channel->owner().historyLoaded(channel);
	if (from
		&& to
		&& !from->isEmpty()
		&& (!from->loadedAtBottom() || !to->loadedAtTop())) {
		from->clear(History::ClearType::Unload);
	}
}

bool HistoryWidget::replyToPreviousMessage() {
	if (!_history || _editMsgId) {
		return false;
	}
	const auto fullId = FullMsgId(
		_history->channelId(),
		_replyToId);
	if (const auto item = session().data().message(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto previousView = view->previousDisplayedInBlocks()) {
				const auto previous = previousView->data();
				Ui::showPeerHistoryAtItem(previous);
				replyToMessage(previous);
				return true;
			}
		}
	} else if (const auto previousView = _history->findLastDisplayed()) {
		const auto previous = previousView->data();
		Ui::showPeerHistoryAtItem(previous);
		replyToMessage(previous);
		return true;
	}
	return false;
}

bool HistoryWidget::replyToNextMessage() {
	if (!_history || _editMsgId) {
		return false;
	}
	const auto fullId = FullMsgId(
		_history->channelId(),
		_replyToId);
	if (const auto item = session().data().message(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto nextView = view->nextDisplayedInBlocks()) {
				const auto next = nextView->data();
				Ui::showPeerHistoryAtItem(next);
				replyToMessage(next);
			} else {
				clearHighlightMessages();
				cancelReply(false);
			}
			return true;
		}
	}
	return false;
}

bool HistoryWidget::showSlowmodeError() {
	const auto text = [&] {
		if (const auto left = _peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWords(left));
		} else if (_peer->slowmodeApplied()) {
			if (const auto item = _history->latestSendingMessage()) {
				if (const auto view = item->mainView()) {
					animatedScrollToItem(item->id);
					enqueueMessageHighlight(view);
				}
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
		return QString();
	}();
	if (text.isEmpty()) {
		return false;
	}
	Ui::ShowMultilineToast({
		.text = { text },
	});
	return true;
}

void HistoryWidget::fieldTabbed() {
	if (_supportAutocomplete) {
		_supportAutocomplete->activate(_field.data());
	} else if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
	}
}

void HistoryWidget::sendInlineResult(InlineBots::ResultSelected result) {
	if (!_peer || !_peer->canWrite()) {
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	auto errorText = result.result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		Ui::show(Box<InformBox>(errorText));
		return;
	}

	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	action.options = std::move(result.options);
	action.generateLocal = true;
	session().api().sendInlineResult(result.bot, result.result, action);

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	auto &bots = cRefRecentInlineBots();
	const auto index = bots.indexOf(result.bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(result.bot);
		session().local().writeRecentHashtagsAndBots();
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
}

void HistoryWidget::updatePinnedViewer() {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_history
		|| !_historyInited) {
		return;
	}
	const auto visibleBottom = _scroll->scrollTop() + _scroll->height();
	auto [view, offset] = _list->findViewForPinnedTracking(visibleBottom);
	const auto lessThanId = !view
		? (ServerMaxMsgId - 1)
		: (view->data()->history() != _history)
		? (view->data()->id + (offset > 0 ? 1 : 0) - ServerMaxMsgId)
		: (view->data()->id + (offset > 0 ? 1 : 0));
	const auto lastClickedId = !_pinnedClickedId
		? (ServerMaxMsgId - 1)
		: (!_migrated || _pinnedClickedId.channel)
		? _pinnedClickedId.msg
		: (_pinnedClickedId.msg - ServerMaxMsgId);
	if (_pinnedClickedId
		&& lessThanId <= lastClickedId
		&& !_scrollToAnimation.animating()) {
		_pinnedClickedId = FullMsgId();
	}
	if (_pinnedClickedId && !_minPinnedId) {
		_minPinnedId = Data::ResolveMinPinnedId(
			_peer,
			_migrated ? _migrated->peer.get() : nullptr);
	}
	if (_pinnedClickedId && _minPinnedId && _minPinnedId >= _pinnedClickedId) {
		// After click on the last pinned message we should the top one.
		_pinnedTracker->trackAround(ServerMaxMsgId - 1);
	} else {
		_pinnedTracker->trackAround(std::min(lessThanId, lastClickedId));
	}
}

void HistoryWidget::checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop) {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_history
		|| !_historyInited) {
		return;
	}
	if (wasScrollTop < nowScrollTop && _pinnedClickedId) {
		// User scrolled down.
		_pinnedClickedId = FullMsgId();
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	}
}

void HistoryWidget::setupPinnedTracker() {
	Expects(_history != nullptr);

	_pinnedTracker = std::make_unique<HistoryView::PinnedTracker>(_history);
	_pinnedBar = nullptr;
	checkPinnedBarState();
}

void HistoryWidget::checkPinnedBarState() {
	Expects(_pinnedTracker != nullptr);

	const auto hiddenId = _peer->canPinMessages()
		? MsgId(0)
		: session().settings().hiddenPinnedMessageId(_peer->id);
	const auto currentPinnedId = Data::ResolveTopPinnedId(
		_peer,
		_migrated ? _migrated->peer.get() : nullptr);
	const auto universalPinnedId = !currentPinnedId
		? int32(0)
		: (_migrated && !currentPinnedId.channel)
		? (currentPinnedId.msg - ServerMaxMsgId)
		: currentPinnedId.msg;
	if (universalPinnedId == hiddenId) {
		if (_pinnedBar) {
			_pinnedTracker->reset();
			auto qobject = base::unique_qptr{
				Ui::WrapAsQObject(this, std::move(_pinnedBar)).get()
			};
			auto destroyer = [this, object = std::move(qobject)]() mutable {
				object = nullptr;
				updateHistoryGeometry();
				updateControlsGeometry();
			};
			base::call_delayed(
				st::defaultMessageBar.duration,
				this,
				std::move(destroyer));
		}
		return;
	}
	if (_pinnedBar || !universalPinnedId) {
		return;
	}

	auto barContent = HistoryView::PinnedBarContent(
		&session(),
		_pinnedTracker->shownMessageId());
	_pinnedBar = std::make_unique<Ui::PinnedBar>(
		this,
		std::move(barContent));
	Info::Profile::SharedMediaCountValue(
		_peer,
		nullptr,
		Storage::SharedMediaType::Pinned
	) | rpl::distinct_until_changed(
	) | rpl::map([=](int count) {
		if (_pinnedClickedId) {
			_pinnedClickedId = FullMsgId();
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		}
		return (count > 1);
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool many) {
		refreshPinnedBarButton(many);
	}, _pinnedBar->lifetime());

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		base::ObservableViewer(Adaptive::Changed())
	) | rpl::map([] {
		return Adaptive::OneColumn();
	}) | rpl::start_with_next([=](bool one) {
		_pinnedBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _pinnedBar->lifetime());

	_pinnedBar->barClicks(
	) | rpl::start_with_next([=] {
		const auto id = _pinnedTracker->currentMessageId();
		if (const auto item = session().data().message(id.message)) {
			Ui::showPeerHistory(item->history()->peer, item->id);
			if (const auto group = session().data().groups().find(item)) {
				// Hack for the case when a non-first item of an album
				// is pinned and we still want the 'show last after first'.
				_pinnedClickedId = group->items.front()->fullId();
			} else {
				_pinnedClickedId = id.message;
			}
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		}
	}, _pinnedBar->lifetime());

	_pinnedBarHeight = 0;
	_pinnedBar->heightValue(
	) | rpl::start_with_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _pinnedBarHeight);
		_pinnedBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _pinnedBar->lifetime());

	orderWidgets();

	if (_a_show.animating()) {
		_pinnedBar->hide();
	}
}

void HistoryWidget::refreshPinnedBarButton(bool many) {
	const auto close = !many;
	auto button = object_ptr<Ui::IconButton>(
		this,
		close ? st::historyReplyCancel : st::historyPinnedShowAll);
	button->clicks(
	) | rpl::start_with_next([=] {
		if (close) {
			hidePinnedMessage();
		} else {
			const auto id = _pinnedTracker->currentMessageId();
			if (id.message) {
				controller()->showSection(
					std::make_shared<HistoryView::PinnedMemento>(
						_history,
						((!_migrated || id.message.channel)
							? id.message.msg
							: (id.message.msg - ServerMaxMsgId))));
			}
		}
	}, button->lifetime());
	_pinnedBar->setRightButton(std::move(button));
}

void HistoryWidget::setupGroupCallTracker() {
	Expects(_history != nullptr);

	const auto peer = _history->peer;
	if (!peer->asMegagroup() && !peer->asChat()) {
		_groupCallTracker = nullptr;
		_groupCallBar = nullptr;
		return;
	}
	_groupCallTracker = std::make_unique<HistoryView::GroupCallTracker>(
		peer);
	_groupCallBar = std::make_unique<Ui::GroupCallBar>(
		this,
		_groupCallTracker->content(),
		Core::App().appDeactivatedValue());

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		base::ObservableViewer(Adaptive::Changed())
	) | rpl::map([] {
		return Adaptive::OneColumn();
	}) | rpl::start_with_next([=](bool one) {
		_groupCallBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _groupCallBar->lifetime());

	rpl::merge(
		_groupCallBar->barClicks(),
		_groupCallBar->joinClicks()
	) | rpl::start_with_next([=] {
		const auto peer = _history->peer;
		const auto channel = peer->asChannel();
		if (channel && channel->amAnonymous()) {
			Ui::ShowMultilineToast({
				.text = tr::lng_group_call_no_anonymous(tr::now),
				});
			return;
		} else if (peer->groupCall()) {
			controller()->startOrJoinGroupCall(peer);
		}
	}, _groupCallBar->lifetime());

	_groupCallBarHeight = 0;
	_groupCallBar->heightValue(
	) | rpl::start_with_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _groupCallBarHeight);
		_groupCallBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _groupCallBar->lifetime());

	orderWidgets();

	if (_a_show.animating()) {
		_groupCallBar->hide();
	}
}

void HistoryWidget::requestMessageData(MsgId msgId) {
	const auto callback = [=](ChannelData *channel, MsgId msgId) {
		messageDataReceived(channel, msgId);
	};
	session().api().requestMessageData(
		_peer->asChannel(),
		msgId,
		crl::guard(this, callback));
}

bool HistoryWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options) {
	const auto error = _peer
		? Data::RestrictionError(_peer, ChatRestriction::f_send_stickers)
		: std::nullopt;
	if (error) {
		Ui::show(Box<InformBox>(*error), Ui::LayerOption::KeepOther);
		return false;
	} else if (!_peer || !_peer->canWrite()) {
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	auto message = Api::MessageToSend(_history);
	message.action.options = std::move(options);
	message.action.replyTo = replyToId();
	Api::SendExistingDocument(std::move(message), document);

	if (_fieldAutocomplete->stickersShown()) {
		clearFieldText();
		//_saveDraftText = true;
		//_saveDraftStart = crl::now();
		//saveDraft();
		saveCloudDraft(); // won't be needed if SendInlineBotResult will clear the cloud draft
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
	return true;
}

bool HistoryWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto error = _peer
		? Data::RestrictionError(_peer, ChatRestriction::f_send_media)
		: std::nullopt;
	if (error) {
		Ui::show(Box<InformBox>(*error), Ui::LayerOption::KeepOther);
		return false;
	} else if (!_peer || !_peer->canWrite()) {
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	auto message = Api::MessageToSend(_history);
	message.action.replyTo = replyToId();
	message.action.options = std::move(options);
	Api::SendExistingPhoto(std::move(message), photo);

	hideSelectorControlsAnimated();

	_field->setFocus();
	return true;
}

void HistoryWidget::showInfoTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	hideInfoTooltip(anim::type::normal);
	_topToast = Ui::Toast::Show(_scroll, Ui::Toast::Config{
		.text = text,
		.st = &st::historyInfoToast,
		.durationMs = CountToastDuration(text),
		.multiline = true,
		.dark = true,
		.slideSide = RectPart::Top,
	});
	if (const auto strong = _topToast.get()) {
		if (hiddenCallback) {
			connect(strong->widget(), &QObject::destroyed, hiddenCallback);
		}
	} else if (hiddenCallback) {
		hiddenCallback();
	}
}

void HistoryWidget::hideInfoTooltip(anim::type animated) {
	if (const auto strong = _topToast.get()) {
		if (animated == anim::type::normal) {
			strong->hideAnimated();
		} else {
			strong->hide();
		}
	}
}

void HistoryWidget::setFieldText(
		const TextWithTags &textWithTags,
		TextUpdateEvents events,
		FieldHistoryAction fieldHistoryAction) {
	_textUpdateEvents = events;
	_field->setTextWithTags(textWithTags, fieldHistoryAction);
	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
	_textUpdateEvents = TextUpdateEvent::SaveDraft
		| TextUpdateEvent::SendTyping;

	previewCancel();
	_previewCancelled = false;
}

void HistoryWidget::clearFieldText(
		TextUpdateEvents events,
		FieldHistoryAction fieldHistoryAction) {
	setFieldText(TextWithTags(), events, fieldHistoryAction);
}

void HistoryWidget::replyToMessage(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		replyToMessage(item);
	}
}

void HistoryWidget::replyToMessage(not_null<HistoryItem*> item) {
	if (!IsServerMsgId(item->id) || !_canSendMessages) {
		return;
	}
	if (item->history() == _migrated) {
		if (item->serviceMsg()) {
			Ui::show(Box<InformBox>(tr::lng_reply_cant(tr::now)));
		} else {
			const auto itemId = item->fullId();
			Ui::show(Box<ConfirmBox>(tr::lng_reply_cant_forward(tr::now), tr::lng_selected_forward(tr::now), crl::guard(this, [=] {
				controller()->content()->setForwardDraft(
					_peer->id,
					{ 1, itemId });
			})));
		}
		return;
	}

	session().data().cancelForwarding(_history);

	if (_editMsgId) {
		if (auto localDraft = _history->localDraft()) {
			localDraft->msgId = item->id;
		} else {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				TextWithTags(),
				item->id,
				MessageCursor(),
				false));
		}
	} else {
		_replyEditMsg = item;
		_replyToId = item->id;
		updateReplyEditText(_replyEditMsg);
		updateBotKeyboard();
		updateReplyToName();
		updateControlsGeometry();
		updateField();
		refreshTopBarActiveChat();
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	_field->setFocus();
}

void HistoryWidget::editMessage(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		editMessage(item);
	}
}

void HistoryWidget::editMessage(not_null<HistoryItem*> item) {
	if (_voiceRecordBar->isActive()) {
		Ui::show(Box<InformBox>(tr::lng_edit_caption_voice(tr::now)));
		return;
	}
	if (const auto media = item->media()) {
		if (media->allowsEditCaption()) {
			Ui::show(Box<EditCaptionBox>(controller(), item));
			return;
		}
	}

	if (isRecording()) {
		// Just fix some strange inconsistency.
		_send->clearState();
	}
	if (!_editMsgId) {
		if (_replyToId || !_field->empty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				_field,
				_replyToId,
				_previewCancelled));
		} else {
			_history->clearLocalDraft();
		}
	}

	const auto editData = PrepareEditText(item);
	const auto cursor = MessageCursor {
		editData.text.size(),
		editData.text.size(),
		QFIXED_MAX
	};
	_history->setLocalEditDraft(std::make_unique<Data::Draft>(
		editData,
		item->id,
		cursor,
		false));
	applyDraft();

	_previewData = nullptr;
	if (const auto media = item->media()) {
		if (const auto page = media->webpage()) {
			_previewData = page;
			updatePreview();
		}
	}

	updateBotKeyboard();

	if (!_field->isHidden()) _fieldBarCancel->show();
	updateFieldPlaceholder();
	updateMouseTracking();
	updateReplyToName();
	updateControlsGeometry();
	updateField();

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	_field->setFocus();
}

void HistoryWidget::hidePinnedMessage() {
	Expects(_pinnedBar != nullptr);

	const auto id = _pinnedTracker->currentMessageId();
	if (!id.message) {
		return;
	}
	if (_peer->canPinMessages()) {
		Window::ToggleMessagePinned(controller(), id.message, false);
	} else {
		const auto callback = [=] {
			if (_pinnedTracker) {
				checkPinnedBarState();
			}
		};
		Window::HidePinnedBar(
			controller(),
			_peer,
			crl::guard(this, callback));
	}
}

bool HistoryWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	if (replyTo.channel != _channel) {
		return false;
	}
	return _keyboard->forceReply()
		&& _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)
		&& _keyboard->forMsgId().msg == replyTo.msg;
}

bool HistoryWidget::lastForceReplyReplied() const {
	return _keyboard->forceReply()
		&& _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)
		&& _keyboard->forMsgId().msg == replyToId();
}

bool HistoryWidget::cancelReply(bool lastKeyboardUsed) {
	bool wasReply = false;
	if (_replyToId) {
		wasReply = true;

		_replyEditMsg = nullptr;
		_replyToId = 0;
		mouseMoveEvent(0);
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_kbReplyTo) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}

		updateBotKeyboard();
		refreshTopBarActiveChat();
		updateControlsGeometry();
		update();
	} else if (auto localDraft = (_history ? _history->localDraft() : nullptr)) {
		if (localDraft->msgId) {
			if (localDraft->textWithTags.text.isEmpty()) {
				_history->clearLocalDraft();
			} else {
				localDraft->msgId = 0;
			}
		}
	}
	if (wasReply) {
		_saveDraftText = true;
		_saveDraftStart = crl::now();
		saveDraft();
	}
	if (!_editMsgId
		&& _keyboard->singleUse()
		&& _keyboard->forceReply()
		&& lastKeyboardUsed) {
		if (_kbReplyTo) {
			toggleKeyboard(false);
		}
	}
	return wasReply;
}

void HistoryWidget::cancelReplyAfterMediaSend(bool lastKeyboardUsed) {
	if (cancelReply(lastKeyboardUsed)) {
		saveCloudDraft();
	}
}

int HistoryWidget::countMembersDropdownHeightMax() const {
	int result = height() - st::membersInnerDropdown.padding.top() - st::membersInnerDropdown.padding.bottom();
	result -= _tabbedSelectorToggle->height();
	accumulate_min(result, st::membersInnerHeightMax);
	return result;
}

void HistoryWidget::cancelEdit() {
	if (!_editMsgId) {
		return;
	}

	_replyEditMsg = nullptr;
	_editMsgId = 0;
	_history->clearLocalEditDraft();
	applyDraft();

	if (_saveEditMsgRequestId) {
		_history->session().api().request(_saveEditMsgRequestId).cancel();
		_saveEditMsgRequestId = 0;
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	mouseMoveEvent(nullptr);
	if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !replyToId()) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}

	auto old = _textUpdateEvents;
	_textUpdateEvents = 0;
	fieldChanged();
	_textUpdateEvents = old;

	if (!canWriteMessage()) {
		updateControlsVisibility();
	}
	updateBotKeyboard();
	updateFieldPlaceholder();

	updateControlsGeometry();
	update();
}

void HistoryWidget::cancelFieldAreaState() {
	Ui::hideLayer();
	_replyForwardPressed = false;
	if (_previewData && _previewData->pendingTill >= 0) {
		_previewCancelled = true;
		previewCancel();

		_saveDraftText = true;
		_saveDraftStart = crl::now();
		saveDraft();
	} else if (_editMsgId) {
		cancelEdit();
	} else if (readyToForward()) {
		session().data().cancelForwarding(_history);
	} else if (_replyToId) {
		cancelReply();
	} else if (_kbReplyTo) {
		toggleKeyboard();
	}
}

void HistoryWidget::previewCancel() {
	_api.request(base::take(_previewRequest)).cancel();
	_previewData = nullptr;
	_previewLinks.clear();
	updatePreview();
}

void HistoryWidget::checkPreview() {
	const auto previewRestricted = [&] {
		return _peer && _peer->amRestricted(ChatRestriction::f_embed_links);
	}();
	if (_previewCancelled || previewRestricted) {
		previewCancel();
		return;
	}
	const auto links = _parsedLinks.join(' ');
	if (_previewLinks != links) {
		_api.request(base::take(_previewRequest)).cancel();
		_previewLinks = links;
		if (_previewLinks.isEmpty()) {
			if (_previewData && _previewData->pendingTill >= 0) {
				previewCancel();
			}
		} else {
			const auto i = _previewCache.constFind(links);
			if (i == _previewCache.cend()) {
				_previewRequest = _api.request(MTPmessages_GetWebPagePreview(
					MTP_flags(0),
					MTP_string(links),
					MTPVector<MTPMessageEntity>()
				)).done([=](const MTPMessageMedia &result, mtpRequestId requestId) {
					gotPreview(links, result, requestId);
				}).send();
			} else if (i.value()) {
				_previewData = session().data().webpage(i.value());
				updatePreview();
			} else if (_previewData && _previewData->pendingTill >= 0) {
				previewCancel();
			}
		}
	}
}

void HistoryWidget::requestPreview() {
	if (!_previewData
		|| (_previewData->pendingTill <= 0)
		|| _previewLinks.isEmpty()) {
		return;
	}
	const auto links = _previewLinks;
	_previewRequest = _api.request(MTPmessages_GetWebPagePreview(
		MTP_flags(0),
		MTP_string(links),
		MTPVector<MTPMessageEntity>()
	)).done([=](const MTPMessageMedia &result, mtpRequestId requestId) {
		gotPreview(links, result, requestId);
	}).send();
}

void HistoryWidget::gotPreview(QString links, const MTPMessageMedia &result, mtpRequestId req) {
	if (req == _previewRequest) {
		_previewRequest = 0;
	}
	if (result.type() == mtpc_messageMediaWebPage) {
		const auto &data = result.c_messageMediaWebPage().vwebpage();
		const auto page = session().data().processWebpage(data);
		_previewCache.insert(links, page->id);
		if (page->pendingTill > 0 && page->pendingTill <= base::unixtime::now()) {
			page->pendingTill = -1;
		}
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = (page->id && page->pendingTill >= 0)
				? page.get()
				: nullptr;
			updatePreview();
		}
		session().data().sendWebPageGamePollNotifications();
	} else if (result.type() == mtpc_messageMediaEmpty) {
		_previewCache.insert(links, 0);
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = nullptr;
			updatePreview();
		}
	}
}

void HistoryWidget::updatePreview() {
	_previewTimer.cancel();
	if (_previewData && _previewData->pendingTill >= 0) {
		_fieldBarCancel->show();
		updateMouseTracking();
		if (_previewData->pendingTill) {
			_previewTitle.setText(
				st::msgNameStyle,
				tr::lng_preview_loading(tr::now),
				Ui::NameTextOptions());
#ifndef OS_MAC_OLD
			auto linkText = _previewLinks.splitRef(' ').at(0).toString();
#else // OS_MAC_OLD
			auto linkText = _previewLinks.split(' ').at(0);
#endif // OS_MAC_OLD
			_previewDescription.setText(
				st::messageTextStyle,
				TextUtilities::Clean(linkText),
				Ui::DialogTextOptions());

			const auto timeout = (_previewData->pendingTill - base::unixtime::now());
			_previewTimer.callOnce(std::max(timeout, 0) * crl::time(1000));
		} else {
			auto preview =
				HistoryView::TitleAndDescriptionFromWebPage(_previewData);
			if (preview.title.isEmpty()) {
				if (_previewData->document) {
					preview.title = tr::lng_attach_file(tr::now);
				} else if (_previewData->photo) {
					preview.title = tr::lng_attach_photo(tr::now);
				}
			}
			_previewTitle.setText(
				st::msgNameStyle,
				preview.title,
				Ui::NameTextOptions());
			_previewDescription.setText(
				st::messageTextStyle,
				TextUtilities::Clean(preview.description),
				Ui::DialogTextOptions());
		}
	} else if (!readyToForward() && !replyToId() && !_editMsgId) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}
	updateControlsGeometry();
	update();
}

void HistoryWidget::fullPeerUpdated(PeerData *peer) {
	auto refresh = false;
	if (_list && peer == _peer) {
		auto newCanSendMessages = _peer->canWrite();
		if (newCanSendMessages != _canSendMessages) {
			_canSendMessages = newCanSendMessages;
			if (!_canSendMessages) {
				cancelReply();
			}
			refreshScheduledToggle();
			refreshSilentToggle();
			refresh = true;
		}
		checkFieldAutocomplete();
		_list->updateBotInfo();

		handlePeerUpdate();
	}
	if (updateCmdStartShown()) {
		refresh = true;
	} else if (!_scroll->isHidden() && _unblock->isHidden() == isBlocked()) {
		refresh = true;
	}
	if (refresh) {
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::handlePeerUpdate() {
	bool resize = false;
	updateHistoryGeometry();
	if (_peer->isChat() && _peer->asChat()->noParticipantInfo()) {
		session().api().requestFullPeer(_peer);
	} else if (_peer->isUser()
		&& (_peer->asUser()->blockStatus() == UserData::BlockStatus::Unknown
			|| _peer->asUser()->callsStatus() == UserData::CallsStatus::Unknown)) {
		session().api().requestFullPeer(_peer);
	} else if (auto channel = _peer->asMegagroup()) {
		if (!channel->mgInfo->botStatus) {
			session().api().requestBots(channel);
		}
		if (channel->mgInfo->admins.empty()) {
			session().api().requestAdmins(channel);
		}
	}
	if (!_a_show.animating()) {
		if (_unblock->isHidden() == isBlocked()
			|| (!isBlocked() && _joinChannel->isHidden() == isJoinChannel())) {
			resize = true;
		}
		bool newCanSendMessages = _peer->canWrite();
		if (newCanSendMessages != _canSendMessages) {
			_canSendMessages = newCanSendMessages;
			if (!_canSendMessages) {
				cancelReply();
			}
			refreshScheduledToggle();
			refreshSilentToggle();
			resize = true;
		}
		updateControlsVisibility();
		if (resize) {
			updateControlsGeometry();
		}
	}
}

void HistoryWidget::forwardSelected() {
	if (!_list) {
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	Window::ShowForwardMessagesBox(controller(), getSelectedItems(), [=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void HistoryWidget::confirmDeleteSelected() {
	if (!_list) return;

	auto items = _list->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	const auto box = Ui::show(Box<DeleteMessagesBox>(
		&session(),
		std::move(items)));
	box->setDeleteConfirmedCallback([=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void HistoryWidget::escape() {
	if (_nonEmptySelection && _list) {
		clearSelected();
	} else if (_isInlineBot) {
		cancelInlineBot();
	} else if (_editMsgId) {
		if (_replyEditMsg
			&& PrepareEditText(_replyEditMsg) != _field->getTextWithTags()) {
			Ui::show(Box<ConfirmBox>(
				tr::lng_cancel_edit_post_sure(tr::now),
				tr::lng_cancel_edit_post_yes(tr::now),
				tr::lng_cancel_edit_post_no(tr::now),
				crl::guard(this, [this] {
					if (_editMsgId) {
						cancelEdit();
						Ui::hideLayer();
					}
				})));
		} else {
			cancelEdit();
		}
	} else if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->hideAnimated();
	} else if (_replyToId && _field->getTextWithTags().text.isEmpty()) {
		cancelReply();
	} else if (auto &voice = _voiceRecordBar; voice->isActive()) {
		voice->showDiscardBox(nullptr, anim::type::normal);
	} else {
		_cancelRequests.fire({});
	}
}

void HistoryWidget::clearSelected() {
	if (_list) {
		_list->clearSelected();
	}
}

HistoryItem *HistoryWidget::getItemFromHistoryOrMigrated(MsgId genericMsgId) const {
	if (genericMsgId < 0 && -genericMsgId < ServerMaxMsgId && _migrated) {
		return session().data().message(_migrated->channelId(), -genericMsgId);
	}
	return session().data().message(_channel, genericMsgId);
}

MessageIdsList HistoryWidget::getSelectedItems() const {
	return _list ? _list->getSelectedItems() : MessageIdsList();
}

void HistoryWidget::updateTopBarSelection() {
	if (!_list) {
		_topBar->showSelected(HistoryView::TopBarWidget::SelectedState {});
		return;
	}

	auto selectedState = _list->getSelectionState();
	_nonEmptySelection = (selectedState.count > 0) || selectedState.textSelected;
	_topBar->showSelected(selectedState);
	updateControlsVisibility();
	updateHistoryGeometry();
	if (!Ui::isLayerShown() && !Core::App().passcodeLocked()) {
		if (_nonEmptySelection
			|| (_list && _list->wasSelectedText())
			|| isRecording()
			|| isBotStart()
			|| isBlocked()
			|| !_canSendMessages) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
	_topBar->update();
	update();
}

void HistoryWidget::messageDataReceived(ChannelData *channel, MsgId msgId) {
	if (!_peer || _peer->asChannel() != channel || !msgId) {
		return;
	}
	if (_editMsgId == msgId || _replyToId == msgId) {
		updateReplyEditTexts(true);
	}
}

void HistoryWidget::updateReplyEditText(not_null<HistoryItem*> item) {
	_replyEditMsgText.setText(
		st::messageTextStyle,
		item->inReplyText(),
		Ui::DialogTextOptions());
	if (!_field->isHidden() || isRecording()) {
		_fieldBarCancel->show();
		updateMouseTracking();
	}
}

void HistoryWidget::updateReplyEditTexts(bool force) {
	if (!force) {
		if (_replyEditMsg || (!_editMsgId && !_replyToId)) {
			return;
		}
	}
	if (!_replyEditMsg) {
		_replyEditMsg = session().data().message(_channel, _editMsgId ? _editMsgId : _replyToId);
	}
	if (_replyEditMsg) {
		updateReplyEditText(_replyEditMsg);
		updateBotKeyboard();
		updateReplyToName();
		updateField();
	} else if (force) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
}

void HistoryWidget::updateForwarding() {
	if (_history) {
		_toForward = _history->validateForwardDraft();
		updateForwardingTexts();
	} else {
		_toForward.clear();
	}
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::updateForwardingTexts() {
	int32 version = 0;
	QString from, text;
	if (const auto count = int(_toForward.size())) {
		auto insertedPeers = base::flat_set<not_null<PeerData*>>();
		auto insertedNames = base::flat_set<QString>();
		auto fullname = QString();
		auto names = std::vector<QString>();
		names.reserve(_toForward.size());
		for (const auto item : _toForward) {
			if (const auto from = item->senderOriginal()) {
				if (!insertedPeers.contains(from)) {
					insertedPeers.emplace(from);
					names.push_back(from->shortName());
					fullname = from->name;
				}
				version += from->nameVersion;
			} else if (const auto info = item->hiddenForwardedInfo()) {
				if (!insertedNames.contains(info->name)) {
					insertedNames.emplace(info->name);
					names.push_back(info->firstName);
					fullname = info->name;
				}
				++version;
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}
		if (names.size() > 2) {
			from = tr::lng_forwarding_from(tr::now, lt_count, names.size() - 1, lt_user, names[0]);
		} else if (names.size() < 2) {
			from = fullname;
		} else {
			from = tr::lng_forwarding_from_two(tr::now, lt_user, names[0], lt_second_user, names[1]);
		}

		if (count < 2) {
			text = _toForward.front()->inReplyText();
		} else {
			text = textcmdLink(1, tr::lng_forward_messages(tr::now, lt_count, count));
		}
	}
	_toForwardFrom.setText(st::msgNameStyle, from, Ui::NameTextOptions());
	_toForwardText.setText(
		st::messageTextStyle,
		text,
		Ui::DialogTextOptions());
	_toForwardNameVersion = version;
}

void HistoryWidget::checkForwardingInfo() {
	if (!_toForward.empty()) {
		auto version = 0;
		for (const auto item : _toForward) {
			if (const auto from = item->senderOriginal()) {
				version += from->nameVersion;
			} else if (const auto info = item->hiddenForwardedInfo()) {
				++version;
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}
		if (version != _toForwardNameVersion) {
			updateForwardingTexts();
		}
	}
}

void HistoryWidget::updateReplyToName() {
	if (_editMsgId) {
		return;
	} else if (!_replyEditMsg && (_replyToId || !_kbReplyTo)) {
		return;
	}
	const auto from = [&] {
		const auto item = _replyEditMsg ? _replyEditMsg : _kbReplyTo;
		if (const auto from = item->displayFrom()) {
			return from;
		}
		return item->author().get();
	}();
	_replyToName.setText(
		st::msgNameStyle,
		from->name,
		Ui::NameTextOptions());
	_replyToNameVersion = (_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()->nameVersion;
}

void HistoryWidget::updateField() {
	auto fieldAreaTop = _scroll->y() + _scroll->height();
	rtlupdate(0, fieldAreaTop, width(), height() - fieldAreaTop);
}

void HistoryWidget::drawField(Painter &p, const QRect &rect) {
	auto backy = _field->y() - st::historySendPadding;
	auto backh = _field->height() + 2 * st::historySendPadding;
	auto hasForward = readyToForward();
	auto drawMsgText = (_editMsgId || _replyToId) ? _replyEditMsg : _kbReplyTo;
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		if (!_editMsgId && drawMsgText && drawMsgText->author()->nameVersion > _replyToNameVersion) {
			updateReplyToName();
		}
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (hasForward) {
		checkForwardingInfo();
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (_previewData && _previewData->pendingTill >= 0) {
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	}
	auto drawWebPagePreview = (_previewData && _previewData->pendingTill >= 0) && !_replyForwardPressed;
	p.fillRect(myrtlrect(0, backy, width(), backh), st::historyReplyBg);
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		auto replyLeft = st::historyReplySkip;
		(_editMsgId ? st::historyEditIcon : st::historyReplyIcon).paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		if (!drawWebPagePreview) {
			if (drawMsgText) {
				if (drawMsgText->media() && drawMsgText->media()->hasReplyPreview()) {
					if (const auto image = drawMsgText->media()->replyPreview()) {
						auto to = QRect(replyLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
						p.drawPixmap(to.x(), to.y(), image->pixSingle(image->width() / cIntRetinaFactor(), image->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
					}
					replyLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
				}
				p.setPen(st::historyReplyNameFg);
				if (_editMsgId) {
					paintEditHeader(p, rect, replyLeft, backy);
				} else {
					_replyToName.drawElided(p, replyLeft, backy + st::msgReplyPadding.top(), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
				}
				p.setPen(st::historyComposeAreaFg);
				p.setTextPalette(st::historyComposeAreaPalette);
				_replyEditMsgText.drawElided(p, replyLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
				p.restoreTextPalette();
			} else {
				p.setFont(st::msgDateFont);
				p.setPen(st::historyComposeAreaFgService);
				p.drawText(replyLeft, backy + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(tr::lng_profile_loading(tr::now), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right()));
			}
		}
	} else if (hasForward) {
		auto forwardLeft = st::historyReplySkip;
		st::historyForwardIcon.paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		if (!drawWebPagePreview) {
			const auto firstItem = _toForward.front();
			const auto firstMedia = firstItem->media();
			const auto preview = (_toForward.size() < 2 && firstMedia && firstMedia->hasReplyPreview())
				? firstMedia->replyPreview()
				: nullptr;
			if (preview) {
				auto to = QRect(forwardLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (preview->width() == preview->height()) {
					p.drawPixmap(to.x(), to.y(), preview->pix());
				} else {
					auto from = (preview->width() > preview->height()) ? QRect((preview->width() - preview->height()) / 2, 0, preview->height(), preview->height()) : QRect(0, (preview->height() - preview->width()) / 2, preview->width(), preview->width());
					p.drawPixmap(to, preview->pix(), from);
				}
				forwardLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
			}
			p.setPen(st::historyReplyNameFg);
			_toForwardFrom.drawElided(p, forwardLeft, backy + st::msgReplyPadding.top(), width() - forwardLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			p.setPen(st::historyComposeAreaFg);
			p.setTextPalette(st::historyComposeAreaPalette);
			_toForwardText.drawElided(p, forwardLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - forwardLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			p.restoreTextPalette();
		}
	}
	if (drawWebPagePreview) {
		const auto textTop = backy + st::msgReplyPadding.top();
		auto previewLeft = st::historyReplySkip + st::webPageLeft;
		p.fillRect(
			st::historyReplySkip,
			textTop,
			st::webPageBar,
			st::msgReplyBarSize.height(),
			st::msgInReplyBarColor);

		const auto to = QRect(
			previewLeft,
			textTop,
			st::msgReplyBarSize.height(),
			st::msgReplyBarSize.height());
		if (HistoryView::DrawWebPageDataPreview(p, _previewData, to)) {
			previewLeft += st::msgReplyBarSize.height()
				+ st::msgReplyBarSkip
				- st::msgReplyBarSize.width()
				- st::msgReplyBarPos.x();
		}
		p.setPen(st::historyReplyNameFg);
		const auto elidedWidth = width()
			- previewLeft
			- _fieldBarCancel->width()
			- st::msgReplyPadding.right();

		_previewTitle.drawElided(
			p,
			previewLeft,
			textTop,
			elidedWidth);
		p.setPen(st::historyComposeAreaFg);
		_previewDescription.drawElided(
			p,
			previewLeft,
			textTop + st::msgServiceNameFont->height,
			elidedWidth);
	}
}

void HistoryWidget::drawRestrictedWrite(Painter &p, const QString &error) {
	auto rect = myrtlrect(0, height() - _unblock->height(), width(), _unblock->height());
	p.fillRect(rect, st::historyReplyBg);

	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(rect.marginsRemoved(QMargins(st::historySendPadding, 0, st::historySendPadding, 0)), error, style::al_center);
}

void HistoryWidget::paintEditHeader(Painter &p, const QRect &rect, int left, int top) const {
	if (!rect.intersects(myrtlrect(left, top, width() - left, st::normalFont->height))) {
		return;
	}

	p.setFont(st::msgServiceNameFont);
	p.drawTextLeft(left, top + st::msgReplyPadding.top(), width(), tr::lng_edit_message(tr::now));

	if (!_replyEditMsg
		|| _replyEditMsg->history()->peer->canEditMessagesIndefinitely()) {
		return;
	}

	QString editTimeLeftText;
	int updateIn = -1;
	auto timeSinceMessage = ItemDateTime(_replyEditMsg).msecsTo(QDateTime::currentDateTime());
	auto editTimeLeft = (session().serverConfig().editTimeLimit * 1000LL) - timeSinceMessage;
	if (editTimeLeft < 2) {
		editTimeLeftText = qsl("0:00");
	} else if (editTimeLeft > kDisplayEditTimeWarningMs) {
		updateIn = static_cast<int>(qMin(editTimeLeft - kDisplayEditTimeWarningMs, qint64(kFullDayInMs)));
	} else {
		updateIn = static_cast<int>(editTimeLeft % 1000);
		if (!updateIn) {
			updateIn = 1000;
		}
		++updateIn;

		editTimeLeft = (editTimeLeft - 1) / 1000; // seconds
		editTimeLeftText = qsl("%1:%2").arg(editTimeLeft / 60).arg(editTimeLeft % 60, 2, 10, QChar('0'));
	}

	// Restart timer only if we are sure that we've painted the whole timer.
	if (rect.contains(myrtlrect(left, top, width() - left, st::normalFont->height)) && updateIn > 0) {
		_updateEditTimeLeftDisplay.callOnce(updateIn);
	}

	if (!editTimeLeftText.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(left + st::msgServiceNameFont->width(tr::lng_edit_message(tr::now)) + st::normalFont->spacew, top + st::msgReplyPadding.top() + st::msgServiceNameFont->ascent, editTimeLeftText);
	}
}

//
//void HistoryWidget::drawPinnedBar(Painter &p) {
//	//if (_pinnedBar->msg) {
//	//	const auto media = _pinnedBar->msg->media();
//	//	if (media && media->hasReplyPreview()) {
//	//		if (const auto image = media->replyPreview()) {
//	//			QRect to(left, top, st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
//	//			p.drawPixmap(to.x(), to.y(), image->pixSingle(image->width() / cIntRetinaFactor(), image->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
//	//		}
//	//		left += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
//	//	}
//	//}
//}

bool HistoryWidget::paintShowAnimationFrame() {
	auto progress = _a_show.value(1.);
	if (!_a_show.animating()) {
		return false;
	}

	Painter p(this);
	auto animationWidth = width();
	auto retina = cIntRetinaFactor();
	auto fromLeft = (_showDirection == Window::SlideDirection::FromLeft);
	auto coordUnder = fromLeft ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
	auto coordOver = fromLeft ? anim::interpolate(0, animationWidth, progress) : anim::interpolate(animationWidth, 0, progress);
	auto shadow = fromLeft ? (1. - progress) : progress;
	if (coordOver > 0) {
		p.drawPixmap(QRect(0, 0, coordOver, height()), _cacheUnder, QRect(-coordUnder * retina, 0, coordOver * retina, height() * retina));
		p.setOpacity(shadow);
		p.fillRect(0, 0, coordOver, height(), st::slideFadeOutBg);
		p.setOpacity(1);
	}
	p.drawPixmap(QRect(coordOver, 0, _cacheOver.width() / retina, height()), _cacheOver, QRect(0, 0, _cacheOver.width(), height() * retina));
	p.setOpacity(shadow);
	st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), height()));
	return true;
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	if (paintShowAnimationFrame()) {
		return;
	}
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	Window::SectionWidget::PaintBackground(controller(), this, e->rect());

	Painter p(this);
	const auto clip = e->rect();
	if (_list) {
		if (!_field->isHidden() || isRecording()) {
			drawField(p, clip);
		} else if (const auto error = writeRestriction()) {
			drawRestrictedWrite(p, *error);
		}
	} else {
		const auto w = st::msgServiceFont->width(tr::lng_willbe_history(tr::now))
			+ st::msgPadding.left()
			+ st::msgPadding.right();
		const auto h = st::msgServiceFont->height
			+ st::msgServicePadding.top()
			+ st::msgServicePadding.bottom();
		const auto tr = QRect(
			(width() - w) / 2,
			st::msgServiceMargin.top() + (height()
				- _field->height()
				- 2 * st::historySendPadding
				- h
				- st::msgServiceMargin.top()
				- st::msgServiceMargin.bottom()) / 2,
			w,
			h);
		HistoryView::ServiceMessagePainter::paintBubble(p, tr.x(), tr.y(), tr.width(), tr.height());

		p.setPen(st::msgServiceFg);
		p.setFont(st::msgServiceFont->f);
		p.drawTextLeft(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top(), width(), tr::lng_willbe_history(tr::now));

		//AssertIsDebug();
		//Ui::EmptyUserpic::PaintRepliesMessages(p, width() / 4, width() / 4, width(), width() / 2);
	}
}

QRect HistoryWidget::historyRect() const {
	return _scroll->geometry();
}

QPoint HistoryWidget::clampMousePosition(QPoint point) {
	if (point.x() < 0) {
		point.setX(0);
	} else if (point.x() >= _scroll->width()) {
		point.setX(_scroll->width() - 1);
	}
	if (point.y() < _scroll->scrollTop()) {
		point.setY(_scroll->scrollTop());
	} else if (point.y() >= _scroll->scrollTop() + _scroll->height()) {
		point.setY(_scroll->scrollTop() + _scroll->height() - 1);
	}
	return point;
}

void HistoryWidget::scrollByTimer() {
	const auto d = (_scrollDelta > 0)
		? qMin(_scrollDelta * 3 / 20 + 1, int32(Ui::kMaxScrollSpeed))
		: qMax(_scrollDelta * 3 / 20 - 1, -int32(Ui::kMaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + d);
}

void HistoryWidget::checkSelectingScroll(QPoint point) {
	if (point.y() < _scroll->scrollTop()) {
		_scrollDelta = point.y() - _scroll->scrollTop();
	} else if (point.y() >= _scroll->scrollTop() + _scroll->height()) {
		_scrollDelta = point.y() - _scroll->scrollTop() - _scroll->height() + 1;
	} else {
		_scrollDelta = 0;
	}
	if (_scrollDelta) {
		_scrollTimer.callEach(15);
	} else {
		_scrollTimer.cancel();
	}
}

void HistoryWidget::noSelectingScroll() {
	_scrollTimer.cancel();
}

bool HistoryWidget::touchScroll(const QPoint &delta) {
	int32 scTop = _scroll->scrollTop(), scMax = _scroll->scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	_scroll->scrollToY(scNew);
	return true;
}

void HistoryWidget::synteticScrollToY(int y) {
	_synteticScrollEvent = true;
	if (_scroll->scrollTop() == y) {
		visibleAreaUpdated();
	} else {
		_scroll->scrollToY(y);
	}
	_synteticScrollEvent = false;
}

HistoryWidget::~HistoryWidget() {
	if (_history) {
		clearAllLoadRequests();
	}
	setTabbedPanel(nullptr);
}
