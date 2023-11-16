/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_widget.h"

#include "api/api_editing.h"
#include "api/api_bot.h"
#include "api/api_chat_participants.h"
#include "api/api_report.h"
#include "api/api_sending.h"
#include "api/api_send_progress.h"
#include "api/api_unread_things.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/delete_messages_box.h"
#include "boxes/send_files_box.h"
#include "boxes/share_box.h"
#include "boxes/edit_caption_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/peers/edit_peer_permissions_box.h" // ShowAboutGigagroup.
#include "boxes/peers/edit_peer_requests_box.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "ui/emoji_config.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/choose_theme_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/text/format_values.h"
#include "ui/chat/message_bar.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/chat/choose_send_as.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/controls/send_as_button.h"
#include "ui/controls/silent_toggle.h"
#include "inline_bots/inline_bot_result.h"
#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "data/data_chat_filters.h"
#include "data/data_scheduled_messages.h"
#include "data/data_sponsored_messages.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_group_call.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h" // GetErrorTextForSending.
#include "history/history_drag_area.h"
#include "history/history_inner_widget.h"
#include "history/history_item_components.h"
#include "history/history_unread_things.h"
#include "history/view/controls/history_view_compose_search.h"
#include "history/view/controls/history_view_forward_panel.h"
#include "history/view/controls/history_view_draft_options.h"
#include "history/view/controls/history_view_voice_record_bar.h"
#include "history/view/controls/history_view_ttl_button.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_scheduled_section.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/history_view_webpage_preview.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_pinned_bar.h"
#include "history/view/history_view_group_call_bar.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_requests_bar.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/media/history_view_media.h"
#include "profile/profile_block_group_members.h"
#include "core/click_handler_types.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/bot_keyboard.h"
#include "chat_helpers/message_field.h"
#include "menu/menu_send.h"
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
#include "ui/boxes/report_box.h"
#include "ui/chat/pinned_bar.h"
#include "ui/chat/group_call_bar.h"
#include "ui/chat/requests_bar.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/continuous_scroll.h"
#include "ui/widgets/popup_menu.h"
#include "ui/item_text_options.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/session/send_as_peers.h"
#include "window/notifications_manager.h"
#include "window/window_adaptive.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_peer_menu.h"
#include "inline_bots/inline_results_widget.h"
#include "inline_bots/bot_attach_web_view.h"
#include "info/profile/info_profile_values.h" // SharedMediaCountValue.
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/shortcuts.h"
#include "core/ui_integration.h"
#include "support/support_common.h"
#include "support/support_autocomplete.h"
#include "support/support_preload.h"
#include "dialogs/dialogs_key.h"
#include "calls/calls_instance.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

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
constexpr auto kSaveDraftTimeout = crl::time(1000);
constexpr auto kSaveDraftAnywayTimeout = 5 * crl::time(1000);
constexpr auto kSaveCloudDraftIdleTimeout = 14 * crl::time(1000);
constexpr auto kRefreshSlowmodeLabelTimeout = crl::time(200);
constexpr auto kCommonModifiers = 0
	| Qt::ShiftModifier
	| Qt::MetaModifier
	| Qt::ControlModifier;
const auto kPsaAboutPrefix = "cloud_lng_about_psa_";

[[nodiscard]] rpl::producer<PeerData*> ActivePeerValue(
		not_null<Window::SessionController*> controller) {
	return controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		const auto history = key.history();
		return history ? history->peer.get() : nullptr;
	});
}

} // namespace

HistoryWidget::HistoryWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::AbstractSectionWidget(
	parent,
	controller,
	ActivePeerValue(controller))
, _api(&controller->session().mtp())
, _updateEditTimeLeftDisplay([=] { updateField(); })
, _fieldBarCancel(this, st::historyReplyCancel)
, _topBar(this, controller)
, _scroll(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll),
	false)
, _updateHistoryItems([=] { updateHistoryItemsByTimer(); })
, _cornerButtons(
	_scroll.data(),
	controller->chatStyle(),
	static_cast<HistoryView::CornerButtonsDelegate*>(this))
, _fieldAutocomplete(this, controller->uiShow())
, _supportAutocomplete(session().supportMode()
	? object_ptr<Support::Autocomplete>(this, &session())
	: nullptr)
, _send(std::make_shared<Ui::SendButton>(this, st::historySend))
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
, _reportMessages(this, QString(), st::historyComposeButton)
, _attachToggle(this, st::historyAttach)
, _tabbedSelectorToggle(this, st::historyAttachEmoji)
, _botKeyboardShow(this, st::historyBotKeyboardShow)
, _botKeyboardHide(this, st::historyBotKeyboardHide)
, _botCommandStart(this, st::historyBotCommandStart)
, _voiceRecordBar(std::make_unique<VoiceRecordBar>(
	this,
	controller->uiShow(),
	_send,
	st::historySendSize.height()))
, _forwardPanel(std::make_unique<ForwardPanel>([=] { updateField(); }))
, _field(
	this,
	st::historyComposeField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_message_ph())
, _kbScroll(this, st::botKbScroll)
, _keyboard(_kbScroll->setOwnedWidget(object_ptr<BotKeyboard>(
	controller,
	this)))
, _membersDropdownShowTimer([=] { showMembersDropdown(); })
, _highlighter(
	&session().data(),
	[=](const HistoryItem *item) { return item->mainView(); },
	[=](const HistoryView::Element *view) {
		session().data().requestViewRepaint(view);
	})
, _saveDraftTimer([=] { saveDraft(); })
, _saveCloudDraftTimer([=] { saveCloudDraft(); })
, _topShadow(this) {
	setAcceptDrops(true);

	session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		handleScroll();
	}, lifetime());
	_scroll->geometryChanged(
	) | rpl::start_with_next(crl::guard(_list, [=] {
		_list->onParentGeometryChanged();
	}), lifetime());
	_scroll->addContentRequests(
	) | rpl::start_with_next([=] {
		if (_history
			&& _history->loadedAtBottom()
			&& session().data().sponsoredMessages().append(_history)) {
			if (_list) {
				_list->setCanHaveFromUserpicsSponsored(true);
			}
			_scroll->contentAdded();
		}
	}, lifetime());

	_fieldBarCancel->addClickHandler([=] { cancelFieldAreaState(); });
	_send->addClickHandler([=] { sendButtonClicked(); });

	SendMenu::SetupMenuAndShortcuts(
		_send.get(),
		[=] { return sendButtonMenuType(); },
		[=] { sendSilent(); },
		[=] { sendScheduled(); },
		[=] { sendWhenOnline(); });

	_unblock->addClickHandler([=] { unblockUser(); });
	_botStart->addClickHandler([=] { sendBotStartCommand(); });
	_joinChannel->addClickHandler([=] { joinChannel(); });
	_muteUnmute->addClickHandler([=] { toggleMuteUnmute(); });
	_reportMessages->addClickHandler([=] { reportSelectedMessages(); });
	_field->submits(
	) | rpl::start_with_next([=](Qt::KeyboardModifiers modifiers) {
		sendWithModifiers(modifiers);
	}, _field->lifetime());
	_field->cancelled(
	) | rpl::start_with_next([=] {
		escape();
	}, _field->lifetime());
	_field->tabbed(
	) | rpl::start_with_next([=] {
		fieldTabbed();
	}, _field->lifetime());
	_field->heightChanges(
	) | rpl::start_with_next([=] {
		fieldResized();
	}, _field->lifetime());
	_field->focusedChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::start_with_next([=] {
		fieldFocused();
	}, _field->lifetime());
	_field->changes(
	) | rpl::start_with_next([=] {
		fieldChanged();
	}, _field->lifetime());
	connect(
		controller->widget()->windowHandle(),
		&QWindow::visibleChanged,
		this,
		[=] { windowIsVisibleChanged(); });

	initTabbedSelector();

	_attachToggle->setClickedCallback([=] {
		base::call_delayed(st::historyAttach.ripple.hideDuration, this, [=] {
			chooseAttach();
		});
	});

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
	_kbScroll->hide();

	controller->chatStyle()->paletteChanged(
	) | rpl::start_with_next([=] {
		_scroll->updateBars();
	}, lifetime());

	_forwardPanel->itemsUpdated(
	) | rpl::start_with_next([=] {
		updateControlsVisibility();
		updateControlsGeometry();
	}, lifetime());

	InitMessageField(controller, _field, [=](
			not_null<DocumentData*> document) {
		if (_peer && Data::AllowEmojiWithoutPremium(_peer)) {
			return true;
		}
		showPremiumToast(document);
		return false;
	});

	_keyboard->sendCommandRequests(
	) | rpl::start_with_next([=](Bot::SendCommandRequest r) {
		sendBotCommand(r);
	}, lifetime());

	_fieldAutocomplete->mentionChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::MentionChosen data) {
		auto replacement = QString();
		auto entityTag = QString();
		if (data.mention.isEmpty()) {
			replacement = data.user->firstName;
			if (replacement.isEmpty()) {
				replacement = data.user->name();
			}
			entityTag = PrepareMentionTag(data.user);
		} else {
			replacement = '@' + data.mention;
		}
		_field->insertTag(replacement, entityTag);
	}, lifetime());

	_fieldAutocomplete->hashtagChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::HashtagChosen data) {
		insertHashtagOrBotCommand(data.hashtag, data.method);
	}, lifetime());

	_fieldAutocomplete->botCommandChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::BotCommandChosen data) {
		insertHashtagOrBotCommand(data.command, data.method);
	}, lifetime());

	_fieldAutocomplete->setModerateKeyActivateCallback([=](int key) {
		const auto context = [=](FullMsgId itemId) {
			return _list->prepareClickContext(Qt::LeftButton, itemId);
		};
		return !_keyboard->isHidden() && _keyboard->moderateKeyActivate(
			key,
			context);
	});

	_fieldAutocomplete->choosingProcesses(
	) | rpl::start_with_next([=](FieldAutocomplete::Type type) {
		if (!_history) {
			return;
		}
		if (type == FieldAutocomplete::Type::Stickers) {
			session().sendProgressManager().update(
				_history,
				Api::SendProgressType::ChooseSticker);
		}
	}, lifetime());

	_fieldAutocomplete->setSendMenuType([=] { return sendMenuType(); });

	if (_supportAutocomplete) {
		supportInitAutocomplete();
	}
	_field->rawTextEdit()->installEventFilter(this);
	_field->rawTextEdit()->installEventFilter(_fieldAutocomplete);
	_field->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return canSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(
				data,
				std::nullopt,
				Core::ReadMimeText(data));
		}
		Unexpected("action in MimeData hook.");
	});

	const auto allow = [=](const auto&) {
		return _peer && _peer->isSelf();
	};
	const auto suggestions = Ui::Emoji::SuggestionsController::Init(
		this,
		_field,
		&controller->session(),
		{ .suggestCustomEmoji = true, .allowCustomWithoutPremium = allow });
	_raiseEmojiSuggestions = [=] { suggestions->raise(); };
	updateFieldSubmitSettings();

	_field->hide();
	_send->hide();
	_unblock->hide();
	_botStart->hide();
	_joinChannel->hide();
	_muteUnmute->hide();
	_reportMessages->hide();

	initVoiceRecordBar();

	_attachToggle->hide();
	_tabbedSelectorToggle->hide();
	_botKeyboardShow->hide();
	_botKeyboardHide->hide();
	_botCommandStart->hide();

	session().attachWebView().requestBots();
	rpl::merge(
		session().attachWebView().attachBotsUpdates(),
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::Rights
		) | rpl::filter([=](const Data::PeerUpdate &update) {
			return update.peer == _peer;
		}) | rpl::to_empty
	) | rpl::start_with_next([=] {
		refreshAttachBotsMenu();
	}, lifetime());

	_botKeyboardShow->addClickHandler([=] { toggleKeyboard(); });
	_botKeyboardHide->addClickHandler([=] { toggleKeyboard(); });
	_botCommandStart->addClickHandler([=] { startBotCommand(); });

	_topShadow->hide();

	_attachDragAreas = DragArea::SetupDragAreaToContainer(
		this,
		crl::guard(this, [=](not_null<const QMimeData*> d) {
			if (!_peer || isRecording()) {
				return false;
			}
			const auto replyTo = (_replyTo && !_editMsgId)
				? _replyEditMsg
				: 0;
			const auto topic = replyTo ? replyTo->topic() : nullptr;
			return topic
				? Data::CanSendAnyOf(topic, Data::FilesSendRestrictions())
				: Data::CanSendAnyOf(_peer, Data::FilesSendRestrictions());
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

	session().data().newItemAdded(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		newItemAdded(item);
	}, lifetime());

	session().data().historyChanged(
	) | rpl::start_with_next([=](not_null<History*> history) {
		handleHistoryChange(history);
	}, lifetime());

	session().data().viewResizeRequest(
	) | rpl::start_with_next([=](not_null<HistoryView::Element*> view) {
		const auto item = view->data();
		const auto history = item->history();
		if (item->mainView() == view
			&& (history == _history || history == _migrated)) {
			updateHistoryGeometry();
		}
	}, lifetime());

	session().data().itemDataChanges(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return !_list && (item->mainView() != nullptr);
	}) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		item->mainView()->itemDataChanged();
	}, lifetime());

	Core::App().settings().largeEmojiChanges(
	) | rpl::start_with_next([=] {
		crl::on_main(this, [=] {
			updateHistoryGeometry();
		});
	}, lifetime());

	session().data().channelDifferenceTooLong(
	) | rpl::filter([=](not_null<ChannelData*> channel) {
		return _peer == channel.get();
	}) | rpl::start_with_next([=] {
		_cornerButtons.updateJumpDownVisibility();
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
	) | rpl::filter([=](not_null<PeerData*> peer) {
		return _peer && (_peer == peer);
	}) | rpl::start_with_next([=] {
		if (updateCmdStartShown()) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		if (_fieldAutocomplete->clearFilteredBotCommands()) {
			checkFieldAutocomplete();
		}
	}, lifetime());

	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	session().changes().entryUpdates(
		EntryUpdateFlag::HasPinnedMessages
		| EntryUpdateFlag::ForwardDraft
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		if (_pinnedTracker
			&& (update.flags & EntryUpdateFlag::HasPinnedMessages)
			&& ((update.entry.get() == _history)
				|| (update.entry.get() == _migrated))) {
			checkPinnedBarState();
		}
		if (update.flags & EntryUpdateFlag::ForwardDraft) {
			updateForwarding();
		}
	}, lifetime());

	using HistoryUpdateFlag = Data::HistoryUpdate::Flag;
	session().changes().historyUpdates(
		HistoryUpdateFlag::MessageSent
		| HistoryUpdateFlag::BotKeyboard
		| HistoryUpdateFlag::CloudDraft
		| HistoryUpdateFlag::UnreadMentions
		| HistoryUpdateFlag::UnreadReactions
		| HistoryUpdateFlag::UnreadView
		| HistoryUpdateFlag::TopPromoted
		| HistoryUpdateFlag::ClientSideMessages
	) | rpl::filter([=](const Data::HistoryUpdate &update) {
		return (_history == update.history.get());
	}) | rpl::start_with_next([=](const Data::HistoryUpdate &update) {
		const auto flags = update.flags;
		if (flags & HistoryUpdateFlag::MessageSent) {
			synteticScrollToY(_scroll->scrollTopMax());
		}
		if (flags & HistoryUpdateFlag::BotKeyboard) {
			updateBotKeyboard(update.history);
		}
		if (flags & HistoryUpdateFlag::CloudDraft) {
			applyCloudDraft(update.history);
		}
		if (flags & HistoryUpdateFlag::ClientSideMessages) {
			updateSendButtonType();
		}
		if ((flags & HistoryUpdateFlag::UnreadMentions)
			|| (flags & HistoryUpdateFlag::UnreadReactions)) {
			_cornerButtons.updateUnreadThingsVisibility();
		}
		if (flags & HistoryUpdateFlag::UnreadView) {
			unreadCountUpdated();
		}
		if (flags & HistoryUpdateFlag::TopPromoted) {
			updateHistoryGeometry();
			updateControlsVisibility();
			updateControlsGeometry();
			this->update();
		}
	}, lifetime());

	using MessageUpdateFlag = Data::MessageUpdate::Flag;
	session().changes().messageUpdates(
		MessageUpdateFlag::Destroyed
		| MessageUpdateFlag::Edited
		| MessageUpdateFlag::ReplyMarkup
		| MessageUpdateFlag::BotCallbackSent
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		const auto flags = update.flags;
		if (flags & MessageUpdateFlag::Destroyed) {
			itemRemoved(update.item);
			return;
		}
		if (flags & MessageUpdateFlag::Edited) {
			itemEdited(update.item);
		}
		if (flags & MessageUpdateFlag::ReplyMarkup) {
			if (_keyboard->forMsgId() == update.item->fullId()) {
				updateBotKeyboard(update.item->history(), true);
			}
		}
		if (flags & MessageUpdateFlag::BotCallbackSent) {
			botCallbackSent(update.item);
		}
	}, lifetime());

	session().changes().realtimeMessageUpdates(
		MessageUpdateFlag::NewUnreadReaction
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		maybeMarkReactionsRead(update.item);
	}, lifetime());

	using MediaSwitch = Media::Player::Instance::Switch;
	Media::Player::instance()->switchToNextEvents(
	) | rpl::filter([=](const MediaSwitch &pair) {
		return (pair.from.type() == AudioMsgId::Type::Voice);
	}) | rpl::start_with_next([=](const MediaSwitch &pair) {
		scrollToCurrentVoiceMessage(pair.from.contextId(), pair.to);
	}, lifetime());

	using PeerUpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		PeerUpdateFlag::Rights
		| PeerUpdateFlag::Migration
		| PeerUpdateFlag::UnavailableReason
		| PeerUpdateFlag::IsBlocked
		| PeerUpdateFlag::Admins
		| PeerUpdateFlag::Members
		| PeerUpdateFlag::OnlineStatus
		| PeerUpdateFlag::Notifications
		| PeerUpdateFlag::ChannelAmIn
		| PeerUpdateFlag::ChannelLinkedChat
		| PeerUpdateFlag::Slowmode
		| PeerUpdateFlag::BotStartToken
		| PeerUpdateFlag::MessagesTTL
		| PeerUpdateFlag::ChatThemeEmoji
		| PeerUpdateFlag::FullInfo
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer.get() == _peer);
	}) | rpl::map([](const Data::PeerUpdate &update) {
		return update.flags;
	}) | rpl::start_with_next([=](Data::PeerUpdate::Flags flags) {
		if (flags & PeerUpdateFlag::Rights) {
			updateStickersByEmoji();
			updateFieldPlaceholder();
			_preview->checkNow(false);
		}
		if (flags & PeerUpdateFlag::Migration) {
			handlePeerMigration();
		}
		if (flags & PeerUpdateFlag::Notifications) {
			updateNotifyControls();
		}
		if (flags & PeerUpdateFlag::UnavailableReason) {
			const auto unavailable = _peer->computeUnavailableReason();
			if (!unavailable.isEmpty()) {
				const auto account = &_peer->account();
				closeCurrent();
				if (const auto primary = Core::App().windowFor(account)) {
					controller->showToast(unavailable);
				}
				return;
			}
		}
		if (flags & PeerUpdateFlag::BotStartToken) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		if (flags & PeerUpdateFlag::Slowmode) {
			updateSendButtonType();
		}
		if (flags & (PeerUpdateFlag::IsBlocked
			| PeerUpdateFlag::Admins
			| PeerUpdateFlag::Members
			| PeerUpdateFlag::OnlineStatus
			| PeerUpdateFlag::Rights
			| PeerUpdateFlag::ChannelAmIn
			| PeerUpdateFlag::ChannelLinkedChat)) {
			handlePeerUpdate();
		}
		if (flags & PeerUpdateFlag::MessagesTTL) {
			checkMessagesTTL();
		}
		if ((flags & PeerUpdateFlag::ChatThemeEmoji) && _list) {
			const auto emoji = _peer->themeEmoji();
			if (Data::CloudThemes::TestingColors() && !emoji.isEmpty()) {
				_peer->owner().cloudThemes().themeForEmojiValue(
					emoji
				) | rpl::filter_optional(
				) | rpl::take(
					1
				) | rpl::start_with_next([=](const Data::CloudTheme &theme) {
					const auto &themes = _peer->owner().cloudThemes();
					const auto text = themes.prepareTestingLink(theme);
					if (!text.isEmpty()) {
						_field->setText(text);
					}
				}, _list->lifetime());
			}
		}
		if (flags & PeerUpdateFlag::FullInfo) {
			fullInfoUpdated();
		}
	}, lifetime());

	using Type = Data::DefaultNotify;
	rpl::merge(
		session().data().notifySettings().defaultUpdates(Type::User),
		session().data().notifySettings().defaultUpdates(Type::Group),
		session().data().notifySettings().defaultUpdates(Type::Broadcast)
	) | rpl::start_with_next([=] {
		updateNotifyControls();
	}, lifetime());

	session().data().itemVisibilityQueries(
	) | rpl::filter([=](
			const Data::Session::ItemVisibilityQuery &query) {
		return !_showAnimation
			&& (_history == query.item->history())
			&& (query.item->mainView() != nullptr)
			&& isVisible();
	}) | rpl::start_with_next([=](
			const Data::Session::ItemVisibilityQuery &query) {
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
	}, lifetime());

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
	_topBar->cancelChooseForReportRequest(
	) | rpl::start_with_next([=] {
		setChooseReportMessagesDetails({}, nullptr);
	}, _topBar->lifetime());
	_topBar->searchRequest(
	) | rpl::start_with_next([=] {
		searchInChat();
	}, _topBar->lifetime());

	session().api().sendActions(
	) | rpl::filter([=](const Api::SendAction &action) {
		return (action.history == _history);
	}) | rpl::start_with_next([=](const Api::SendAction &action) {
		const auto lastKeyboardUsed = lastForceReplyReplied(
			action.replyTo.messageId);
		if (action.replaceMediaOf) {
		} else if (action.options.scheduled) {
			cancelReply(lastKeyboardUsed);
			crl::on_main(this, [=, history = action.history] {
				controller->showSection(
					std::make_shared<HistoryView::ScheduledMemento>(history));
			});
		} else {
			fastShowAtEnd(action.history);
			if (!_justMarkingAsRead
				&& cancelReply(lastKeyboardUsed)
				&& !action.clearDraft) {
				saveCloudDraft();
			}
		}
		if (action.options.handleSupportSwitch) {
			handleSupportSwitch(action.history);
		}
	}, lifetime());

	if (session().supportMode()) {
		session().data().chatListEntryRefreshes(
		) | rpl::start_with_next([=] {
			crl::on_main(this, [=] { checkSupportPreload(true); });
		}, lifetime());
	}

	Core::App().materializeLocalDraftsRequests(
	) | rpl::start_with_next([=] {
		saveFieldToHistoryLocalDraft();
	}, lifetime());

	setupScheduledToggle();
	setupSendAsToggle();
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
		.currentReplyTo = replyTo(),
	};
}

void HistoryWidget::refreshJoinChannelText() {
	if (const auto channel = _peer ? _peer->asChannel() : nullptr) {
		_joinChannel->setText((channel->isBroadcast()
			? tr::lng_profile_join_channel(tr::now)
			: (channel->requestToJoin() && !channel->amCreator())
			? tr::lng_profile_apply_to_join_group(tr::now)
			: tr::lng_profile_join_group(tr::now)).toUpper());
	}
}

void HistoryWidget::refreshTopBarActiveChat() {
	const auto state = computeDialogsEntryState();
	_topBar->setActiveChat(state, _history->sendActionPainter());
	if (state.key) {
		controller()->setCurrentDialogsEntryState(state);
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
	_voiceRecordBar->setStartRecordingFilter([=] {
		const auto error = [&]() -> std::optional<QString> {
			if (_peer) {
				if (const auto error = Data::RestrictionError(
						_peer,
						ChatRestriction::SendVoiceMessages)) {
					return error;
				}
			}
			return std::nullopt;
		}();
		if (error) {
			controller()->showToast(*error);
			return true;
		} else if (showSlowmodeError()) {
			return true;
		}
		return false;
	});

	const auto applyLocalDraft = [=] {
		if (_history && _history->localDraft({})) {
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

		auto action = prepareSendAction(data.options);
		session().api().sendVoiceMessage(
			data.bytes,
			data.waveform,
			data.duration,
			action);
		_voiceRecordBar->clearListenState();
	}, lifetime());

	_voiceRecordBar->cancelRequests(
	) | rpl::start_with_next(applyLocalDraft, lifetime());

	_voiceRecordBar->lockShowStarts(
	) | rpl::start_with_next([=] {
		_cornerButtons.updateJumpDownVisibility();
		_cornerButtons.updateUnreadThingsVisibility();
	}, lifetime());

	_voiceRecordBar->updateSendButtonTypeRequests(
	) | rpl::start_with_next([=] {
		updateSendButtonType();
	}, lifetime());

	_voiceRecordBar->lockViewportEvents(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());

	_voiceRecordBar->recordingTipRequests(
	) | rpl::start_with_next([=] {
		controller()->showToast(tr::lng_record_hold_tip(tr::now));
	}, lifetime());

	_voiceRecordBar->recordingStateChanges(
	) | rpl::start_with_next([=](bool active) {
		controller()->widget()->setInnerFocus();
	}, lifetime());

	_voiceRecordBar->hideFast();
}

void HistoryWidget::initTabbedSelector() {
	refreshTabbedPanel();

	_tabbedSelectorToggle->addClickHandler([=] {
		if (_tabbedPanel && _tabbedPanel->isHidden()) {
			_tabbedPanel->showAnimated();
		} else {
			toggleTabbedSelectorMode();
		}
	});

	const auto selector = controller()->tabbedSelector();

	base::install_event_filter(this, selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return base::EventFilterResult::Continue;
	});

	auto filter = rpl::filter([=] {
		return !isHidden();
	});
	using Selector = TabbedSelector;

	selector->emojiChosen(
	) | rpl::filter([=] {
		return !isHidden() && !_field->isHidden();
	}) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), data.emoji);
	}, lifetime());

	rpl::merge(
		selector->fileChosen() | filter,
		_fieldAutocomplete->stickerChosen(),
		selector->customEmojiChosen() | filter,
		controller()->stickerOrEmojiChosen() | filter
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		controller()->hideLayer(anim::type::normal);
		if (const auto info = data.document->sticker()
			; info && info->setType == Data::StickersType::Emoji) {
			if (data.document->isPremiumEmoji()
				&& !session().premium()
				&& (!_peer || !Data::AllowEmojiWithoutPremium(_peer))) {
				showPremiumToast(data.document);
			} else if (!_field->isHidden()) {
				Data::InsertCustomEmoji(_field.data(), data.document);
			}
		} else {
			controller()->sendingAnimation().appendSending(
				data.messageSendingFrom);
			const auto localId = data.messageSendingFrom.localId;
			sendExistingDocument(data.document, data.options, localId);
		}
	}, lifetime());

	selector->photoChosen(
	) | filter | rpl::start_with_next([=](ChatHelpers::PhotoChosen data) {
		sendExistingPhoto(data.photo, data.options);
	}, lifetime());

	selector->inlineResultChosen(
	) | filter | rpl::filter([=](const ChatHelpers::InlineChosen &data) {
		if (!data.recipientOverride) {
			return true;
		} else if (data.recipientOverride != _peer) {
			showHistory(data.recipientOverride->id, ShowAtTheEndMsgId, {});
		}
		return (data.recipientOverride == _peer);
	}) | rpl::start_with_next([=](ChatHelpers::InlineChosen data) {
		sendInlineResult(data);
	}, lifetime());

	selector->contextMenuRequested(
	) | filter | rpl::start_with_next([=] {
		selector->showMenuWithType(sendMenuType());
	}, lifetime());

	selector->choosingStickerUpdated(
	) | rpl::start_with_next([=](const Selector::Action &data) {
		if (!_history) {
			return;
		}
		const auto type = Api::SendProgressType::ChooseSticker;
		if (data != Selector::Action::Cancel) {
			session().sendProgressManager().update(_history, type);
		} else {
			session().sendProgressManager().cancel(_history, type);
		}
	}, lifetime());
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
		auto options = Api::SendOptions{
			.sendAs = prepareSendAction({}).options.sendAs,
		};
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
	const auto box = controller()->show(Box<Support::ConfirmContactBox>(
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

	auto to = session().data().message(_history->peer, msgId);
	if (_list->itemTop(to) < 0) {
		return;
	}

	auto scrollTo = std::clamp(
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
		not_null<HistoryView::Element*> view,
		const TextWithEntities &part) {
	_highlighter.enqueue(view, part);
}

Ui::ChatPaintHighlight HistoryWidget::itemHighlight(
		not_null<const HistoryItem*> item) const {
	return _highlighter.state(item);
}

int HistoryWidget::itemTopForHighlight(
		not_null<HistoryView::Element*> view) const {
	if (const auto group = session().data().groups().find(view->data())) {
		if (const auto leader = group->items.front()->mainView()) {
			view = leader;
		}
	}
	const auto itemTop = _list->itemTop(view);
	Assert(itemTop >= 0);

	const auto reactionCenter = view->data()->hasUnreadReaction()
		? view->reactionButtonParameters({}, {}).center.y()
		: -1;

	const auto visibleAreaHeight = _scroll->height();
	const auto viewHeight = view->height();
	const auto heightLeft = (visibleAreaHeight - viewHeight);
	if (heightLeft >= 0) {
		return std::max(itemTop - (heightLeft / 2), 0);
	} else if (reactionCenter >= 0) {
		const auto maxSize = st::reactionInfoImage;

		// Show message right till the bottom.
		const auto forBottom = itemTop + viewHeight - visibleAreaHeight;

		// Show message bottom and some space below for the effect.
		const auto bottomResult = forBottom + maxSize;

		// Show the reaction button center in the middle.
		const auto byReactionResult = itemTop
			+ reactionCenter
			- visibleAreaHeight / 2;

		// Show the reaction center and some space above it for the effect.
		const auto maxAllowed = itemTop + reactionCenter - 2 * maxSize;
		return std::max(
			std::min(maxAllowed, std::max(bottomResult, byReactionResult)),
			0);
	}
	return itemTop;
}

void HistoryWidget::start() {
	session().data().stickers().updated(
		Data::StickersType::Stickers
	) | rpl::start_with_next([=] {
		updateStickersByEmoji();
	}, lifetime());
	session().data().stickers().notifySavedGifsUpdated();
}

void HistoryWidget::insertHashtagOrBotCommand(
		QString str,
		FieldAutocomplete::ChooseMethod method) {
	if (!_peer) {
		return;
	}

	// Send bot command at once, if it was not inserted by pressing Tab.
	if (str.at(0) == '/' && method != FieldAutocomplete::ChooseMethod::ByTab) {
		sendBotCommand({ _peer, str, FullMsgId(), replyTo() });
		session().api().finishForwarding(prepareSendAction({}));
		setFieldText(_field->getTextWithTagsPart(_field->textCursor().position()));
	} else {
		_field->insertTag(str);
	}
}


InlineBotQuery HistoryWidget::parseInlineBotQuery() const {
	return (isChoosingTheme() || _editMsgId)
		? InlineBotQuery()
		: ParseInlineBotQuery(&session(), _field);
}

AutocompleteQuery HistoryWidget::parseMentionHashtagBotCommandQuery() const {
	const auto result = (isChoosingTheme()
		|| (_inlineBot && !_inlineLookingUpBot))
		? AutocompleteQuery()
		: ParseMentionHashtagBotCommandQuery(_field, {});
	if (result.query.isEmpty()) {
		return result;
	} else if (result.query[0] == '#'
		&& cRecentWriteHashtags().isEmpty()
		&& cRecentSearchHashtags().isEmpty()) {
		session().local().readRecentHashtagsAndBots();
	} else if (result.query[0] == '@'
		&& cRecentInlineBots().isEmpty()) {
		session().local().readRecentHashtagsAndBots();
	} else if (result.query[0] == '/'
		&& ((_peer->isUser() && !_peer->asUser()->isBot()) || _editMsgId)) {
		return AutocompleteQuery();
	}
	return result;
}

void HistoryWidget::updateInlineBotQuery() {
	if (!_history) {
		return;
	}
	const auto query = parseInlineBotQuery();
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
				const auto &data = result.data();
				const auto resolvedBot = [&]() -> UserData* {
					if (const auto user = session().data().processUsers(
							data.vusers())) {
						if (user->isBot()
							&& !user->botInfo->inlinePlaceholder.isEmpty()) {
							return user;
						}
					}
					return nullptr;
				}();
				session().data().processChats(data.vchats());

				_inlineBotResolveRequestId = 0;
				const auto query = parseInlineBotQuery();
				if (_inlineBotUsername == query.username) {
					applyInlineBotQuery(
						query.lookingUpBot ? resolvedBot : query.bot,
						query.query);
				} else {
					clearInlineBot();
				}
			}).fail([=](const MTP::Error &error) {
				_inlineBotResolveRequestId = 0;
				if (username == _inlineBotUsername) {
					clearInlineBot();
				}
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
				if (result.open) {
					const auto request = result.result->openRequest();
					if (const auto photo = request.photo()) {
						controller()->openPhoto(photo, {});
					} else if (const auto document = request.document()) {
						controller()->openDocument(document, false, {});
					}
				} else {
					sendInlineResult(result);
				}
			});
			_inlineResults->setSendMenuType([=] { return sendMenuType(); });
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
	_voiceRecordBar->raise();
	_send->raise();
	if (_contactStatus) {
		_contactStatus->bar().raise();
	}
	if (_translateBar) {
		_translateBar->raise();
	}
	if (_pinnedBar) {
		_pinnedBar->raise();
	}
	if (_requestsBar) {
		_requestsBar->raise();
	}
	if (_groupCallBar) {
		_groupCallBar->raise();
	}
	if (_chooseTheme) {
		_chooseTheme->raise();
	}
	_topShadow->raise();
	_fieldAutocomplete->raise();
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

bool HistoryWidget::updateStickersByEmoji() {
	if (!_peer) {
		return false;
	}
	const auto emoji = [&] {
		const auto errorForStickers = Data::RestrictionError(
			_peer,
			ChatRestriction::SendStickers);
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
	return (emoji != nullptr);
}

void HistoryWidget::toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show) {
	const auto update = [=] {
		updateInlineBotQuery();
		updateControlsGeometry();
		updateControlsVisibility();
	};
	if (peer.get() != _peer) {
		return;
	} else if (_chooseTheme) {
		if (isChoosingTheme() && !show.value_or(false)) {
			const auto was = base::take(_chooseTheme);
			if (Ui::InFocusChain(this)) {
				setInnerFocus();
			}
			update();
		}
		return;
	} else if (!show.value_or(true)) {
		return;
	} else if (_voiceRecordBar->isActive()) {
		controller()->showToast(tr::lng_chat_theme_cant_voice(tr::now));
		return;
	}
	_chooseTheme = std::make_unique<Ui::ChooseThemeController>(
		this,
		controller(),
		peer);
	_chooseTheme->shouldBeShownValue(
	) | rpl::start_with_next(update, _chooseTheme->lifetime());
}

Ui::ChatTheme *HistoryWidget::customChatTheme() const {
	return _list ? _list->theme().get() : nullptr;
}

void HistoryWidget::fieldChanged() {
	const auto updateTyping = (_textUpdateEvents & TextUpdateEvent::SendTyping);

	InvokeQueued(this, [=] {
		updateInlineBotQuery();
		const auto choosingSticker = updateStickersByEmoji();
		if (_history
			&& !_inlineBot
			&& !_editMsgId
			&& !choosingSticker
			&& updateTyping) {
			session().sendProgressManager().update(
				_history,
				Api::SendProgressType::Typing);
		}
	});

	updateSendButtonType();
	if (!HasSendText(_field)) {
		_fieldIsEmpty = true;
	} else if (_fieldIsEmpty) {
		_fieldIsEmpty = false;
		if (_kbShown) {
			toggleKeyboard();
		}
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
	if (!_history) {
		return;
	}

	const auto topicRootId = MsgId();
	if (_editMsgId) {
		_history->setLocalEditDraft(std::make_unique<Data::Draft>(
			_field,
			FullReplyTo{
				.messageId = FullMsgId(_history->peer->id, _editMsgId),
				.topicRootId = topicRootId,
			},
			_preview->draft(),
			_saveEditMsgRequestId));
	} else {
		if (_replyTo || !_field->empty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				_field,
				_replyTo,
				_preview->draft()));
		} else {
			_history->clearLocalDraft(topicRootId);
		}
		_history->clearLocalEditDraft(topicRootId);
	}
}

void HistoryWidget::saveCloudDraft() {
	controller()->session().api().saveCurrentDraftToCloud();
}

void HistoryWidget::writeDraftTexts() {
	Expects(_history != nullptr);

	session().local().writeDrafts(_history);
	if (_migrated) {
		_migrated->clearDrafts();
		session().local().writeDrafts(_migrated);
	}
}

void HistoryWidget::writeDraftCursors() {
	Expects(_history != nullptr);

	session().local().writeDraftCursors(_history);
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
	if (_list) {
		if (isSearching()) {
			_composeSearch->setInnerFocus();
		} else if (_chooseTheme && _chooseTheme->shouldBeShown()) {
			_chooseTheme->setFocus();
		} else if (_nonEmptySelection
			|| (_list && _list->wasSelectedText())
			|| isRecording()
			|| isBotStart()
			|| isBlocked()
			|| (!_canSendTexts && !_editMsgId)) {
			if (_scroll->isHidden()) {
				setFocus();
			} else {
				_list->setFocus();
			}
		} else {
			_field->setFocus();
		}
	} else if (_scroll->isHidden()) {
		setFocus();
	}
}

bool HistoryWidget::notify_switchInlineBotButtonReceived(
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	if (samePeerBot) {
		if (_history) {
			const auto textWithTags = TextWithTags{
				'@' + samePeerBot->username() + ' ' + query,
				TextWithTags::Tags(),
			};
			MessageCursor cursor = {
				int(textWithTags.text.size()),
				int(textWithTags.text.size()),
				Ui::kQFixedMax,
			};
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				textWithTags,
				FullReplyTo(),
				cursor,
				Data::WebPageDraft()));
			applyDraft();
			return true;
		}
	} else if (const auto bot = _peer ? _peer->asUser() : nullptr) {
		const auto to = bot->isBot()
			? bot->botInfo->inlineReturnTo
			: Dialogs::EntryState();
		if (!to.key.owningHistory()) {
			return false;
		}
		bot->botInfo->inlineReturnTo = Dialogs::EntryState();
		controller()->switchInlineQuery(to, bot, query);
		return true;
	}
	return false;
}

void HistoryWidget::tryProcessKeyInput(not_null<QKeyEvent*> e) {
	e->accept();
	keyPressEvent(e);
	if (!e->isAccepted()
		&& _canSendTexts
		&& _field->isVisible()
		&& !e->text().isEmpty()) {
		_field->setFocusFast();
		QCoreApplication::sendEvent(_field->rawTextEdit(), e);
	}
}

void HistoryWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return _history
			&& Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !controller()->isLayerShown()
			&& window()->isActiveWindow();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 1) && request->handle([=] {
			searchInChat();
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
	}, lifetime());
}

void HistoryWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	if (item->history() != _history && item->history() != _migrated) {
		return;
	}
	_cornerButtons.pushReplyReturn(item);
	updateControlsVisibility();
}

QVector<FullMsgId> HistoryWidget::replyReturns() const {
	return _cornerButtons.replyReturns();
}

void HistoryWidget::setReplyReturns(
		PeerId peer,
		QVector<FullMsgId> replyReturns) {
	if (!_peer || _peer->id != peer) {
		return;
	}
	_cornerButtons.setReplyReturns(std::move(replyReturns));
}

void HistoryWidget::fastShowAtEnd(not_null<History*> history) {
	if (_history != history) {
		return;
	}

	clearAllLoadRequests();
	setMsgId(ShowAtUnreadMsgId);
	_pinnedClickedId = FullMsgId();
	_minPinnedId = std::nullopt;
	if (_history->isReadyFor(_showAtMsgId)) {
		historyLoaded();
	} else {
		firstLoadMessages();
		doneShow();
	}
}

bool HistoryWidget::applyDraft(FieldHistoryAction fieldHistoryAction) {
	InvokeQueued(this, [=] { updateStickersByEmoji(); });

	const auto editDraft = _history ? _history->localEditDraft({}) : nullptr;
	const auto draft = editDraft
		? editDraft
		: _history
		? _history->localDraft({})
		: nullptr;
	auto fieldAvailable = canWriteMessage();
	const auto editMsgId = editDraft ? editDraft->reply.messageId.msg : 0;
	if (_voiceRecordBar->isActive() || (!_canSendTexts && !editMsgId)) {
		if (!_canSendTexts) {
			clearFieldText(0, fieldHistoryAction);
		}
		return false;
	}

	if (!draft || (!editDraft && !fieldAvailable)) {
		auto fieldWillBeHiddenAfterEdit = (!fieldAvailable && _editMsgId != 0);
		clearFieldText(0, fieldHistoryAction);
		setInnerFocus();
		_processingReplyItem = _replyEditMsg = nullptr;
		_processingReplyTo = _replyTo = FullReplyTo();
		setEditMsgId(0);
		if (_preview) {
			_preview->apply({ .removed = true });
		}
		if (fieldWillBeHiddenAfterEdit) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		refreshTopBarActiveChat();
		return true;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags, 0, fieldHistoryAction);
	setInnerFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft
		| TextUpdateEvent::SendTyping;

	_processingReplyItem = _replyEditMsg = nullptr;
	_processingReplyTo = _replyTo = FullReplyTo();
	setEditMsgId(editMsgId);
	updateCmdStartShown();
	updateControlsVisibility();
	updateControlsGeometry();
	refreshTopBarActiveChat();
	if (_editMsgId) {
		updateReplyEditTexts();
		if (!_replyEditMsg) {
			requestMessageData(_editMsgId);
		}
	} else if (!readyToForward()) {
		const auto draft = _history->localDraft({});
		_processingReplyTo = draft ? draft->reply : FullReplyTo();
		processReply();
	}

	if (_preview) {
		_preview->setDisabled(_editMsgId
			&& _replyEditMsg
			&& _replyEditMsg->media()
			&& !_replyEditMsg->media()->webpage());
		if (!_editMsgId) {
			_preview->apply(draft->webpage, true);
		} else if (!_replyEditMsg
			|| !_replyEditMsg->media()
			|| _replyEditMsg->media()->webpage()) {
			_preview->apply(draft->webpage, false);
		}
	}
	return true;
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
	Expects(_history != nullptr);

	if (session().supportMode() || !_history->trackUnreadMessages()) {
		return true;
	} else if (!_historyInited) {
		return false;
	}
	_history->calculateFirstUnreadMessage();
	const auto unread = _history->firstUnreadMessage();
	const auto visibleBottom = _scroll->scrollTop() + _scroll->height();
	DEBUG_LOG(("JumpToEnd(%1, %2, %3): "
		"unread: %4, top: %5, visibleBottom: %6."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(unread ? unread->data()->id.bare : 0
		).arg(unread ? _list->itemTop(unread) : -1
		).arg(visibleBottom));
	return unread && _list->itemTop(unread) <= visibleBottom;
}

void HistoryWidget::showHistory(
		const PeerId &peerId,
		MsgId showAtMsgId,
		const TextWithEntities &highlightPart) {
	_pinnedClickedId = FullMsgId();
	_minPinnedId = std::nullopt;
	_showAtMsgHighlightPart = {};

	const auto wasDialogsEntryState = computeDialogsEntryState();
	const auto startBot = (showAtMsgId == ShowAndStartBotMsgId);
	if (startBot) {
		showAtMsgId = ShowAtTheEndMsgId;
	}

	_highlighter.clear();
	controller()->sendingAnimation().clear();
	_topToast.hide(anim::type::instant);
	if (_history) {
		if (_peer->id == peerId) {
			updateForwarding();

			if (showAtMsgId == ShowAtUnreadMsgId
				&& insideJumpToEndInsteadOfToUnread()) {
				DEBUG_LOG(("JumpToEnd(%1, %2, %3): "
					"Jumping to end instead of unread."
					).arg(_history->peer->name()
					).arg(_history->inboxReadTillId().bare
					).arg(Logs::b(_history->loadedAtBottom())));
				showAtMsgId = ShowAtTheEndMsgId;
			} else if (showAtMsgId == ShowForChooseMessagesMsgId) {
				if (_chooseForReport) {
					clearSelected();
					_chooseForReport->active = true;
					_list->setChooseReportReason(_chooseForReport->reason);
					updateControlsVisibility();
					updateControlsGeometry();
					updateTopBarChooseForReport();
				}
				return;
			}
			if (!IsServerMsgId(showAtMsgId)
				&& !IsClientMsgId(showAtMsgId)
				&& !IsServerMsgId(-showAtMsgId)) {
				// To end or to unread.
				destroyUnreadBar();
			}
			const auto canShowNow = _history->isReadyFor(showAtMsgId);
			if (!canShowNow) {
				if (!_firstLoadRequest) {
					DEBUG_LOG(("JumpToEnd(%1, %2, %3): Showing delayed at %4."
						).arg(_history->peer->name()
						).arg(_history->inboxReadTillId().bare
						).arg(Logs::b(_history->loadedAtBottom())
						).arg(showAtMsgId.bare));
					delayedShowAt(showAtMsgId, highlightPart);
				} else if (_showAtMsgId != showAtMsgId) {
					clearAllLoadRequests();
					setMsgId(showAtMsgId, highlightPart);
					firstLoadMessages();
					doneShow();
				}
			} else {
				_history->forgetScrollState();
				if (_migrated) {
					_migrated->forgetScrollState();
				}

				clearDelayedShowAt();
				const auto skipId = (_migrated && showAtMsgId < 0)
					? FullMsgId(_migrated->peer->id, -showAtMsgId)
					: (showAtMsgId > 0)
					? FullMsgId(_history->peer->id, showAtMsgId)
					: FullMsgId();
				if (skipId) {
					_cornerButtons.skipReplyReturn(skipId);
				}

				setMsgId(showAtMsgId, highlightPart);
				if (_historyInited) {
					DEBUG_LOG(("JumpToEnd(%1, %2, %3): "
						"Showing instant at %4."
						).arg(_history->peer->name()
						).arg(_history->inboxReadTillId().bare
						).arg(Logs::b(_history->loadedAtBottom())
						).arg(showAtMsgId.bare));

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
						_history->clearLocalDraft({});
						applyDraft();
						_send->finishAnimating();
					}
				}
			}
			return;
		} else {
			_sponsoredMessagesStateKnown = false;
			session().data().sponsoredMessages().clearItems(_history);
			session().data().hideShownSpoilers();
			_composeSearch = nullptr;
		}
		session().sendProgressManager().update(
			_history,
			Api::SendProgressType::Typing,
			-1);
		session().data().histories().sendPendingReadInbox(_history);
		session().sendProgressManager().cancelTyping(_history);
	}

	_cornerButtons.clearReplyReturns();
	if (_history) {
		if (Ui::InFocusChain(this)) {
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
		_translateBar = nullptr;
		_pinnedTracker = nullptr;
		_groupCallBar = nullptr;
		_requestsBar = nullptr;
		_chooseTheme = nullptr;
		_membersDropdown.destroy();
		_scrollToAnimation.stop();

		setHistory(nullptr);
		_list = nullptr;
		_peer = nullptr;
		_canSendMessages = false;
		_canSendTexts = false;
		_fieldDisabled = nullptr;
		_silent.destroy();
		updateBotKeyboard();
	} else {
		Assert(_list == nullptr);
	}

	HistoryView::Element::ClearGlobal();

	_saveEditMsgRequestId = 0;
	_processingReplyItem = _replyEditMsg = nullptr;
	_processingReplyTo = _replyTo = FullReplyTo();
	_editMsgId = MsgId();
	_canReplaceMedia = false;
	_photoEditMedia = nullptr;
	updateReplaceMediaButton();
	_fieldBarCancel->hide();

	_membersDropdownShowTimer.cancel();
	_scroll->takeWidget<HistoryInner>().destroy();

	clearInlineBot();

	_showAtMsgId = showAtMsgId;
	_showAtMsgHighlightPart = highlightPart;
	_historyInited = false;
	_contactStatus = nullptr;

	if (peerId) {
		_peer = session().data().peer(peerId);
		_contactStatus = std::make_unique<HistoryView::ContactStatus>(
			controller(),
			this,
			_peer,
			false);
		_contactStatus->bar().heightValue() | rpl::start_with_next([=] {
			updateControlsGeometry();
		}, _contactStatus->bar().lifetime());
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
	}

	_nonEmptySelection = false;
	_itemRevealPending.clear();
	_itemRevealAnimations.clear();
	_itemsRevealHeight = 0;

	if (_peer) {
		setHistory(_peer->owner().history(_peer));
		if (_migrated
			&& !_migrated->isEmpty()
			&& (!_history->loadedAtTop() || !_migrated->loadedAtBottom())) {
			_migrated->clear(History::ClearType::Unload);
		}
		_history->setFakeUnreadWhileOpened(true);

		if (_showAtMsgId == ShowForChooseMessagesMsgId) {
			_showAtMsgId = ShowAtUnreadMsgId;
			if (_chooseForReport) {
				_chooseForReport->active = true;
			}
		} else {
			_chooseForReport = nullptr;
		}
		if (_showAtMsgId == ShowAtUnreadMsgId
			&& !_history->trackUnreadMessages()
			&& !hasSavedScroll()) {
			_showAtMsgId = ShowAtTheEndMsgId;
		}
		refreshTopBarActiveChat();
		updateTopBarSelection();

		if (_peer->isChannel()) {
			updateNotifyControls();
			session().data().notifySettings().request(_peer);
			refreshSilentToggle();
		} else if (_peer->isRepliesChat()) {
			updateNotifyControls();
		}
		refreshScheduledToggle();
		refreshSendAsToggle();

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

		if (const auto channel = _peer->asChannel()) {
			channel->updateFull();
			if (!channel->isBroadcast()) {
				channel->flagsValue(
				) | rpl::start_with_next([=] {
					refreshJoinChannelText();
				}, _list->lifetime());
			} else {
				refreshJoinChannelText();
			}
		}

		controller()->adaptive().changes(
		) | rpl::start_with_next([=] {
			_history->forceFullResize();
			if (_migrated) {
				_migrated->forceFullResize();
			}
			updateHistoryGeometry();
			update();
		}, _list->lifetime());

		if (_chooseForReport && _chooseForReport->active) {
			_list->setChooseReportReason(_chooseForReport->reason);
		}
		updateTopBarChooseForReport();

		_updateHistoryItems.cancel();

		setupTranslateBar();
		setupPinnedTracker();
		setupGroupCallBar();
		setupRequestsBar();
		checkMessagesTTL();
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
		if (!applyDraft()) {
			clearFieldText();
		}
		_send->finishAnimating();

		updateControlsGeometry();

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

		// Must be done before unreadCountUpdated(), or we auto-close.
		if (_history->unreadMark()) {
			session().data().histories().changeDialogUnreadMark(
				_history,
				false);
		}
		if (_migrated && _migrated->unreadMark()) {
			session().data().histories().changeDialogUnreadMark(
				_migrated,
				false);
		}
		unreadCountUpdated(); // set _historyDown badge.
		showAboutTopPromotion();

		{
			_scroll->setTrackingContent(false);
			const auto checkState = crl::guard(this, [=, history = _history] {
				if (history != _history) {
					return;
				}
				auto &sponsored = session().data().sponsoredMessages();
				using State = Data::SponsoredMessages::State;
				const auto state = sponsored.state(_history);
				_sponsoredMessagesStateKnown = (state != State::None);
				if (state == State::AppendToEnd) {
					_scroll->setTrackingContent(
						sponsored.canHaveFor(_history));
				} else if (state == State::InjectToMiddle) {
					if (_list) {
						_list->setCanHaveFromUserpicsSponsored(true);
					}
					injectSponsoredMessages();
				}
			});
			session().data().sponsoredMessages().request(
				_history,
				checkState);
			checkState();
		}
	} else {
		_chooseForReport = nullptr;
		refreshTopBarActiveChat();
		updateTopBarSelection();
		checkMessagesTTL();
		clearFieldText();
		doneShow();
	}
	updateForwarding();
	updateOverStates(mapFromGlobal(QCursor::pos()));

	if (_history) {
		const auto msgId = (_showAtMsgId == ShowAtTheEndMsgId)
			? ShowAtUnreadMsgId
			: _showAtMsgId;
		controller()->setActiveChatEntry({
			_history,
			FullMsgId(_history->peer->id, msgId) });
	}
	update();
	controller()->floatPlayerAreaUpdated();
	session().data().itemVisibilitiesUpdated();

	crl::on_main(this, [=] { controller()->widget()->setInnerFocus(); });
}

void HistoryWidget::setHistory(History *history) {
	if (_history == history) {
		return;
	}

	const auto was = _attachBotsMenu && _history && _history->peer->isUser();
	const auto now = _attachBotsMenu && history && history->peer->isUser();
	if (was && !now) {
		_attachToggle->removeEventFilter(_attachBotsMenu.get());
		_attachBotsMenu->hideFast();
	} else if (now && !was) {
		_attachToggle->installEventFilter(_attachBotsMenu.get());
	}

	const auto unloadHeavyViewParts = [](History *history) {
		if (history) {
			history->owner().unloadHeavyViewParts(
				history->delegateMixin()->delegate());
			history->forceFullResize();
		}
	};

	if (_history) {
		unregisterDraftSources();
		clearAllLoadRequests();
		clearSupportPreloadRequest();
		const auto wasHistory = base::take(_history);
		const auto wasMigrated = base::take(_migrated);
		unloadHeavyViewParts(wasHistory);
		unloadHeavyViewParts(wasMigrated);
	}
	if (history) {
		_history = history;
		_migrated = _history ? _history->migrateFrom() : nullptr;
		registerDraftSource();
		if (_history) {
			setupPreview();
		} else {
			_previewDrawPreview = nullptr;
			_preview = nullptr;
		}
	}
	refreshAttachBotsMenu();
}

void HistoryWidget::setupPreview() {
	Expects(_history != nullptr);

	using namespace HistoryView::Controls;
	_preview = std::make_unique<WebpageProcessor>(_history, _field);
	_preview->repaintRequests() | rpl::start_with_next([=] {
		updateField();
	}, _preview->lifetime());

	_preview->parsedValue(
	) | rpl::start_with_next([=](WebpageParsed value) {
		_previewTitle.setText(
			st::msgNameStyle,
			value.title,
			Ui::NameTextOptions());
		_previewDescription.setText(
			st::defaultTextStyle,
			value.description,
			Ui::DialogTextOptions());
		const auto changed = (!_previewDrawPreview != !value.drawPreview);
		_previewDrawPreview = value.drawPreview;
		if (changed) {
			updateControlsGeometry();
			updateControlsVisibility();
		}
		updateField();
	}, _preview->lifetime());
}

void HistoryWidget::injectSponsoredMessages() const {
	session().data().sponsoredMessages().inject(
		_history,
		_showAtMsgId,
		_scroll->height() * 2,
		_scroll->width());
}

void HistoryWidget::refreshAttachBotsMenu() {
	_attachBotsMenu = nullptr;
	if (!_history) {
		return;
	}
	_attachBotsMenu = InlineBots::MakeAttachBotsMenu(
		this,
		controller(),
		_history->peer,
		[=] { return prepareSendAction({}); },
		[=](bool compress) { chooseAttach(compress); });
	if (!_attachBotsMenu) {
		return;
	}
	_attachBotsMenu->setOrigin(
		Ui::PanelAnimation::Origin::BottomLeft);
	_attachToggle->installEventFilter(_attachBotsMenu.get());
	_attachBotsMenu->heightValue(
	) | rpl::start_with_next([=] {
		moveFieldControls();
	}, _attachBotsMenu->lifetime());
}

void HistoryWidget::unregisterDraftSources() {
	if (!_history) {
		return;
	}
	session().local().unregisterDraftSource(
		_history,
		Data::DraftKey::Local({}));
	session().local().unregisterDraftSource(
		_history,
		Data::DraftKey::LocalEdit({}));
}

void HistoryWidget::registerDraftSource() {
	if (!_history) {
		return;
	}
	const auto peerId = _history->peer->id;
	const auto editMsgId = _editMsgId;
	const auto draft = [=] {
		return Storage::MessageDraft{
			(editMsgId
				? FullReplyTo{ FullMsgId(peerId, editMsgId) }
				: _replyTo),
			_field->getTextWithTags(),
			_preview->draft(),
		};
	};
	auto draftSource = Storage::MessageDraftSource{
		.draft = draft,
		.cursor = [=] { return MessageCursor(_field); },
	};
	session().local().registerDraftSource(
		_history,
		(editMsgId
			? Data::DraftKey::LocalEdit({})
			: Data::DraftKey::Local({})),
		std::move(draftSource));
}

void HistoryWidget::setEditMsgId(MsgId msgId) {
	unregisterDraftSources();
	_editMsgId = msgId;
	if (!msgId) {
		_canReplaceMedia = false;
		if (_preview) {
			_preview->setDisabled(false);
		}
	}
	if (_history) {
		refreshSendAsToggle();
		orderWidgets();
	}
	registerDraftSource();
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

void HistoryWidget::clearSupportPreloadRequest() {
	Expects(_history != nullptr);

	if (_supportPreloadRequest) {
		auto &histories = _history->owner().histories();
		histories.cancelRequest(_supportPreloadRequest);
		_supportPreloadRequest = 0;
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

bool HistoryWidget::updateReplaceMediaButton() {
	if (!_canReplaceMedia) {
		const auto result = (_replaceMedia != nullptr);
		_replaceMedia.destroy();
		return result;
	} else if (_replaceMedia) {
		return false;
	}
	_replaceMedia.create(this, st::historyReplaceMedia);
	_replaceMedia->setClickedCallback([=] {
		EditCaptionBox::StartMediaReplace(
			controller(),
			{ _history->peer->id, _editMsgId },
			_field->getTextWithTags(),
			crl::guard(_list, [=] { cancelEdit(); }));
	});
	return true;
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

	_muteUnmute->setText((_history->muted()
		? tr::lng_channel_unmute(tr::now)
		: tr::lng_channel_mute(tr::now)).toUpper());
	if (!session().data().notifySettings().silentPostsUnknown(_peer)) {
		if (_silent) {
			_silent->setChecked(
				session().data().notifySettings().silentPosts(_peer));
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
	) | rpl::map([=](Dialogs::Key key) -> rpl::producer<> {
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
		&& _canSendMessages
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

void HistoryWidget::setupSendAsToggle() {
	session().sendAsPeers().updated(
	) | rpl::filter([=](not_null<PeerData*> peer) {
		return (peer == _peer);
	}) | rpl::start_with_next([=] {
		refreshSendAsToggle();
		updateControlsVisibility();
		updateControlsGeometry();
		orderWidgets();
	}, lifetime());
}

void HistoryWidget::refreshSendAsToggle() {
	Expects(_peer != nullptr);

	if (_editMsgId || !session().sendAsPeers().shouldChoose(_peer)) {
		_sendAs.destroy();
		return;
	} else if (_sendAs) {
		return;
	}
	_sendAs.create(this, st::sendAsButton);
	Ui::SetupSendAsButton(_sendAs.data(), controller());
}

bool HistoryWidget::contentOverlapped(const QRect &globalRect) {
	return (_attachDragAreas.document->overlaps(globalRect)
			|| _attachDragAreas.photo->overlaps(globalRect)
			|| _fieldAutocomplete->overlaps(globalRect)
			|| (_tabbedPanel && _tabbedPanel->overlaps(globalRect))
			|| (_inlineResults && _inlineResults->overlaps(globalRect)));
}

bool HistoryWidget::canWriteMessage() const {
	if (!_history || !_canSendMessages) {
		return false;
	}
	if (isBlocked() || isJoinChannel() || isMuteUnmute() || isBotStart()) {
		return false;
	}
	if (isSearching()) {
		return false;
	}
	return true;
}

std::optional<QString> HistoryWidget::writeRestriction() const {
	const auto allWithoutPolls = Data::AllSendRestrictions()
		& ~ChatRestriction::SendPolls;
	auto result = (_peer && !Data::CanSendAnyOf(_peer, allWithoutPolls))
		? Data::RestrictionError(_peer, ChatRestriction::SendOther)
		: std::nullopt;
	if (result) {
		return result;
	} else if (_peer && _peer->isForum()) {
		return tr::lng_forum_replies_only(tr::now);
	}
	return std::nullopt;
}

void HistoryWidget::updateControlsVisibility() {
	auto fieldDisabledRemoved = (_fieldDisabled != nullptr);
	const auto guard = gsl::finally([&] {
		if (fieldDisabledRemoved) {
			_fieldDisabled = nullptr;
		}
	});

	if (!_showAnimation) {
		_topShadow->setVisible(_peer != nullptr);
		_topBar->setVisible(_peer != nullptr);
	}
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
	if (!_history || _showAnimation) {
		hideChildWidgets();
		return;
	}

	if (_firstLoadRequest && !_scroll->isHidden()) {
		if (Ui::InFocusChain(_scroll.data())) {
			// Don't loose focus back to chats list.
			setFocus();
		}
		_scroll->hide();
	} else if (!_firstLoadRequest && _scroll->isHidden()) {
		_scroll->show();
	}
	if (_pinnedBar) {
		_pinnedBar->show();
	}
	if (_translateBar) {
		_translateBar->show();
	}
	if (_groupCallBar) {
		_groupCallBar->show();
	}
	if (_requestsBar) {
		_requestsBar->show();
	}
	if (_contactStatus) {
		_contactStatus->show();
	}
	if (isChoosingTheme()
		|| (!editingMessage()
			&& (isSearching()
				|| isBlocked()
				|| isJoinChannel()
				|| isMuteUnmute()
				|| isBotStart()
				|| isReportMessages()))) {
		const auto toggle = [&](Ui::FlatButton *shown) {
			const auto toggleOne = [&](not_null<Ui::FlatButton*> button) {
				if (button.get() != shown) {
					button->hide();
				} else if (button->isHidden()) {
					button->clearState();
					button->show();
				}
			};
			toggleOne(_reportMessages);
			toggleOne(_joinChannel);
			toggleOne(_muteUnmute);
			toggleOne(_botStart);
			toggleOne(_unblock);
		};
		if (isChoosingTheme()) {
			_chooseTheme->show();
			setInnerFocus();
			toggle(nullptr);
		} else if (isReportMessages()) {
			toggle(_reportMessages);
		} else if (isBlocked()) {
			toggle(_unblock);
		} else if (isJoinChannel()) {
			toggle(_joinChannel);
		} else if (isMuteUnmute()) {
			toggle(_muteUnmute);
		} else if (isBotStart()) {
			toggle(_botStart);
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
		if (_ttlInfo) {
			_ttlInfo->hide();
		}
		if (_sendAs) {
			_sendAs->hide();
		}
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		if (_replaceMedia) {
			_replaceMedia->hide();
		}
		_tabbedSelectorToggle->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		if (_botMenuButton) {
			_botMenuButton->hide();
		}
		if (_tabbedPanel) {
			_tabbedPanel->hide();
		}
		if (_voiceRecordBar) {
			_voiceRecordBar->hideFast();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		hideFieldIfVisible();
	} else if (editingMessage() || _canSendMessages) {
		checkFieldAutocomplete();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_reportMessages->hide();
		_send->show();
		updateSendButtonType();

		if (_canSendTexts || _editMsgId) {
			_field->show();
		} else {
			fieldDisabledRemoved = false;
			if (!_fieldDisabled) {
				_fieldDisabled = CreateDisabledFieldView(this, _peer);
				orderWidgets();
				updateControlsGeometry();
				update();
			}
			_fieldDisabled->show();
			hideFieldIfVisible();
		}
		if (_kbShown) {
			_kbScroll->show();
			_tabbedSelectorToggle->hide();
			showKeyboardHideButton();
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
		if (_replaceMedia) {
			_replaceMedia->show();
			_attachToggle->hide();
		} else {
			_attachToggle->show();
		}
		if (_botMenuButton) {
			_botMenuButton->show();
		}
		{
			auto rightButtonsChanged = false;
			if (_silent) {
				const auto was = _silent->isVisible();
				const auto now = (!_editMsgId);
				if (was != now) {
					_silent->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (_scheduled) {
				const auto was = _scheduled->isVisible();
				const auto now = (!_editMsgId);
				if (was != now) {
					_scheduled->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (_ttlInfo) {
				const auto was = _ttlInfo->isVisible();
				const auto now = (!_editMsgId);
				if (was != now) {
					_ttlInfo->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (rightButtonsChanged) {
				updateFieldSize();
			}
		}
		if (_sendAs) {
			_sendAs->show();
		}
		updateFieldPlaceholder();

		if (_editMsgId
			|| _replyTo
			|| readyToForward()
			|| _previewDrawPreview
			|| _kbReplyTo) {
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
		_reportMessages->hide();
		_attachToggle->hide();
		if (_silent) {
			_silent->hide();
		}
		if (_scheduled) {
			_scheduled->hide();
		}
		if (_ttlInfo) {
			_ttlInfo->hide();
		}
		if (_sendAs) {
			_sendAs->hide();
		}
		if (_botMenuButton) {
			_botMenuButton->hide();
		}
		_kbScroll->hide();
		if (_replyTo || readyToForward() || _kbReplyTo) {
			if (_fieldBarCancel->isHidden()) {
				_fieldBarCancel->show();
				updateControlsGeometry();
				update();
			}
		} else {
			_fieldBarCancel->hide();
		}
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
		if (_composeSearch) {
			_composeSearch->hideAnimated();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		_kbScroll->hide();
		hideFieldIfVisible();
	}
	//checkTabbedSelectorToggleTooltip();
	updateMouseTracking();
}

void HistoryWidget::hideFieldIfVisible() {
	if (_field->isHidden()) {
		return;
	} else if (InFocusChain(_field)) {
		setFocus();
	}
	_field->hide();
	updateControlsGeometry();
	update();
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

void HistoryWidget::newItemAdded(not_null<HistoryItem*> item) {
	if (_history != item->history()
		|| !_historyInited
		|| item->isScheduled()) {
		return;
	}
	if (item->isSponsored()) {
		if (const auto view = item->mainView()) {
			view->resizeGetHeight(width());
			updateHistoryGeometry(
				false,
				true,
				{ ScrollChangeNoJumpToBottom, 0 });
		}
		return;
	}

	// If we get here in non-resized state we can't rely on results of
	// markingMessagesRead() and mark chat as read.
	// If we receive N messages being not at bottom:
	// - on first message we set unreadcount += 1, firstUnreadMessage.
	// - on second we get wrong markingMessagesRead() and read both.
	session().data().sendHistoryChangeNotifications();

	if (item->isSending()) {
		synteticScrollToY(_scroll->scrollTopMax());
	} else if (_scroll->scrollTop() < _scroll->scrollTopMax()) {
		return;
	}
	if (item->showNotification()) {
		destroyUnreadBar();
		if (markingMessagesRead()) {
			if (item->isUnreadMention() && !item->isUnreadMedia()) {
				session().api().markContentsRead(item);
			}
			session().data().histories().readInboxOnNewMessage(item);

			// Also clear possible scheduled messages notifications.
			// Side-effect: Also clears all notifications from forum topics.
			Core::App().notifications().clearFromHistory(_history);
		}
	}
	const auto view = item->mainView();
	if (!view) {
		return;
	} else if (anim::Disabled()) {
		if (!On(PowerSaving::kChatBackground)) {
			// Strange case of disabled animations, but enabled bg rotation.
			if (item->out() || _history->peer->isSelf()) {
				_list->theme()->rotateComplexGradientBackground();
			}
		}
		return;
	}
	_itemRevealPending.emplace(item);
}

void HistoryWidget::maybeMarkReactionsRead(not_null<HistoryItem*> item) {
	if (!_historyInited || !_list) {
		return;
	}
	const auto view = item->mainView();
	const auto itemTop = _list->itemTop(view);
	if (itemTop <= 0 || !markingContentsRead()) {
		return;
	}
	const auto reactionCenter
		= view->reactionButtonParameters({}, {}).center.y();
	const auto visibleTop = _scroll->scrollTop();
	const auto visibleBottom = visibleTop + _scroll->height();
	if (itemTop + reactionCenter < visibleTop
		|| itemTop + view->height() > visibleBottom) {
		return;
	}
	session().api().markContentsRead(item);
}

void HistoryWidget::unreadCountUpdated() {
	if (_history->unreadMark() || (_migrated && _migrated->unreadMark())) {
		crl::on_main(this, [=, history = _history] {
			if (history == _history) {
				closeCurrent();
			}
		});
	} else {
		const auto hideCounter = _history->isForum()
			|| !_history->trackUnreadMessages();
		_cornerButtons.updateJumpDownVisibility(hideCounter
			? 0
			: _history->chatListBadgesState().unreadCounter);
	}
}

void HistoryWidget::closeCurrent() {
	if (controller()->isPrimary()) {
		controller()->showBackFromStack();
	} else {
		controller()->window().close();
	}
}

void HistoryWidget::messagesFailed(const MTP::Error &error, int requestId) {
	if (error.type() == u"CHANNEL_PRIVATE"_q
		&& _peer->isChannel()
		&& _peer->asChannel()->invitePeekExpires()) {
		_peer->asChannel()->privateErrorReceived();
	} else if (error.type() == u"CHANNEL_PRIVATE"_q
		|| error.type() == u"CHANNEL_PUBLIC_GROUP_NA"_q
		|| error.type() == u"USER_BANNED_IN_CHANNEL"_q) {
		auto was = _peer;
		closeCurrent();
		if (const auto primary = Core::App().windowFor(&was->account())) {
			controller()->showToast((was && was->isMegagroup())
				? tr::lng_group_not_accessible(tr::now)
				: tr::lng_channel_not_accessible(tr::now));
		}
		return;
	}

	LOG(("RPC Error: %1 %2: %3").arg(
		QString::number(error.code()),
		error.type(),
		error.description()));

	if (_preloadRequest == requestId) {
		_preloadRequest = 0;
	} else if (_preloadDownRequest == requestId) {
		_preloadDownRequest = 0;
	} else if (_firstLoadRequest == requestId) {
		_firstLoadRequest = 0;
		closeCurrent();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::messagesReceived(
		not_null<PeerData*> peer,
		const MTPmessages_Messages &messages,
		int requestId) {
	Expects(_history != nullptr);

	const auto toMigrated = (peer == _peer->migrateFrom());
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
		if (const auto channel = peer->asChannel()) {
			channel->ptsReceived(d.vpts().v);
			channel->processTopics(d.vtopics());
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

	if (_preloadRequest == requestId) {
		addMessagesToFront(peer, *histList);
		_preloadRequest = 0;
		preloadHistoryIfNeeded();
	} else if (_preloadDownRequest == requestId) {
		addMessagesToBack(peer, *histList);
		_preloadDownRequest = 0;
		preloadHistoryIfNeeded();
		if (_history->loadedAtBottom()) {
			checkActivation();
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
		injectSponsoredMessages();
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
		const auto skipId = (_migrated && _delayedShowAtMsgId < 0)
			? FullMsgId(_migrated->peer->id, -_delayedShowAtMsgId)
			: (_delayedShowAtMsgId > 0)
			? FullMsgId(_history->peer->id, _delayedShowAtMsgId)
			: FullMsgId();
		if (skipId) {
			_cornerButtons.skipReplyReturn(skipId);
		}

		_delayedShowAtRequest = 0;
		setMsgId(_delayedShowAtMsgId, _delayedShowAtMsgHighlightPart);
		historyLoaded();
	}
	if (session().supportMode()) {
		crl::on_main(this, [=] { checkSupportPreload(); });
	}
}

void HistoryWidget::historyLoaded() {
	_historyInited = false;
	doneShow();
}

void HistoryWidget::windowShown() {
	updateControlsGeometry();
}

bool HistoryWidget::markingMessagesRead() const {
	return markingContentsRead() && !session().supportMode();
}

bool HistoryWidget::markingContentsRead() const {
	return _history
		&& _list
		&& _historyInited
		&& !_firstLoadRequest
		&& !_delayedShowAtRequest
		&& !_showAnimation
		&& controller()->widget()->markingAsRead();
}

void HistoryWidget::checkActivation() {
	if (_list) {
		_list->checkActivation();
	}
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) {
		return;
	}

	auto from = _history;
	auto offsetId = MsgId();
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
	} else if (_showAtMsgId < 0 && _history->peer->isChannel()) {
		if (_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId && _migrated) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = -_showAtMsgId;
		} else if (_showAtMsgId == SwitchAtTopMsgId) {
			_history->getReadyFor(_showAtMsgId);
		}
	}

	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

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
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _firstLoadRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
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
	const auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtTop()) {
		return;
	}

	const auto offsetId = from->minMsgId();
	const auto addOffset = 0;
	const auto loadCount = offsetId
		? kMessagesPerPage
		: kMessagesPerPageFirst;
	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

	DEBUG_LOG(("JumpToEnd(%1, %2, %3): Loading up before %4."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(offsetId.bare));

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
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _preloadRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
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
		if (_sponsoredMessagesStateKnown) {
			session().data().sponsoredMessages().request(_history, nullptr);
		}
		return;
	}

	const auto loadCount = kMessagesPerPage;
	auto addOffset = -loadCount;
	auto offsetId = from->maxMsgId();
	if (!offsetId) {
		if (loadMigrated || !_migrated) return;
		++offsetId;
		++addOffset;
	}
	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

	DEBUG_LOG(("JumpToEnd(%1, %2, %3): Loading down after %4."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(offsetId.bare));

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
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _preloadDownRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
			messagesFailed(error, _preloadDownRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::delayedShowAt(
		MsgId showAtMsgId,
		const TextWithEntities &highlightPart) {
	if (!_history) {
		return;
	}
	if (_delayedShowAtMsgHighlightPart != highlightPart) {
		_delayedShowAtMsgHighlightPart = highlightPart;
	}
	if (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId) {
		return;
	}

	clearAllLoadRequests();
	_delayedShowAtMsgId = showAtMsgId;

	DEBUG_LOG(("JumpToEnd(%1, %2, %3): Loading delayed around %4."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(showAtMsgId.bare));

	auto from = _history;
	auto offsetId = MsgId();
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
	} else if (_delayedShowAtMsgId < 0 && _history->peer->isChannel()) {
		if (_delayedShowAtMsgId < 0 && -_delayedShowAtMsgId < ServerMaxMsgId && _migrated) {
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = -_delayedShowAtMsgId;
		}
	}
	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

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
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _delayedShowAtRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
			messagesFailed(error, _delayedShowAtRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::handleScroll() {
	if (!_itemsRevealHeight) {
		preloadHistoryIfNeeded();
	}
	visibleAreaUpdated();
	if (!_itemsRevealHeight) {
		updatePinnedViewer();
	}
	const auto now = crl::now();
	if (!_synteticScrollEvent) {
		_lastUserScrolled = now;
	}
	const auto scrollTop = _scroll->scrollTop();
	if (scrollTop != _lastScrollTop) {
		if (!_synteticScrollEvent) {
			checkLastPinnedClickedIdReset(_lastScrollTop, scrollTop);
		}
		_lastScrolled = now;
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
		session().data().itemVisibilitiesUpdated();
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

	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
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
	if (session().supportMode()) {
		crl::on_main(this, [=] { checkSupportPreload(); });
	}
}

void HistoryWidget::checkSupportPreload(bool force) {
	if (!_history
		|| _firstLoadRequest
		|| _preloadRequest
		|| _preloadDownRequest
		|| (_supportPreloadRequest && !force)
		|| controller()->activeChatEntryCurrent().key.history() != _history) {
		return;
	}

	const auto setting = session().settings().supportSwitch();
	const auto command = Support::GetSwitchCommand(setting);
	const auto descriptor = !command
		? Dialogs::RowDescriptor()
		: (*command == Shortcuts::Command::ChatNext)
		? controller()->resolveChatNext()
		: controller()->resolveChatPrevious();
	auto history = descriptor.key.history();
	if (!history || _supportPreloadHistory == history) {
		return;
	}
	clearSupportPreloadRequest();
	_supportPreloadHistory = history;
	_supportPreloadRequest = Support::SendPreloadRequest(history, [=] {
		_supportPreloadRequest = 0;
		_supportPreloadHistory = nullptr;
		crl::on_main(this, [=] { checkSupportPreload(); });
	});
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
	while (const auto replyReturn = _cornerButtons.replyReturn()) {
		auto below = !replyReturn->mainView()
			&& (replyReturn->history() == _history)
			&& !_history->isEmpty()
			&& (replyReturn->id
				< _history->blocks.back()->messages.back()->data()->id);
		if (!below) {
			below = !replyReturn->mainView()
				&& (replyReturn->history() == _migrated)
				&& !_history->isEmpty();
		}
		if (!below) {
			below = !replyReturn->mainView()
				&& _migrated
				&& (replyReturn->history() == _migrated)
				&& !_migrated->isEmpty()
				&& (replyReturn->id
					< _migrated->blocks.back()->messages.back()->data()->id);
		}
		if (!below && replyReturn->mainView()) {
			below = (scrollTop >= scrollTopMax)
				|| (_list->itemTop(replyReturn)
					< scrollTop + scrollHeight / 2);
		}
		if (below) {
			_cornerButtons.calculateNextReplyReturn();
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

void HistoryWidget::saveEditMsg() {
	Expects(_history != nullptr);

	if (_saveEditMsgRequestId) {
		return;
	}

	const auto item = session().data().message(_history->peer, _editMsgId);
	if (!item) {
		cancelEdit();
		return;
	}
	const auto webPageDraft = _preview->draft();
	const auto textWithTags = _field->getTextWithAppliedMarkdown();
	const auto prepareFlags = Ui::ItemTextOptions(
		_history,
		session().user()).flags;
	auto sending = TextWithEntities();
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);

	const auto media = item->media();
	if (!TextUtilities::CutPart(sending, left, MaxMessageSize)
		&& (webPageDraft.removed
			|| webPageDraft.url.isEmpty()
			|| !webPageDraft.manual)
		&& (!media || !media->allowsEditCaption())) {
		const auto suggestModerateActions = false;
		controller()->show(
			Box<DeleteMessagesBox>(item, suggestModerateActions));
		return;
	} else if (!left.text.isEmpty()) {
		const auto remove = left.text.size();
		controller()->showToast(
			tr::lng_edit_limit_reached(tr::now, lt_count, remove));
		return;
	}

	const auto weak = Ui::MakeWeak(this);
	const auto history = _history;

	const auto done = [=](mtpRequestId requestId) {
		crl::guard(weak, [=] {
			if (requestId == _saveEditMsgRequestId) {
				_saveEditMsgRequestId = 0;
				cancelEdit();
			}
		})();
		if (const auto editDraft = history->localEditDraft({})) {
			if (editDraft->saveRequestId == requestId) {
				history->clearLocalEditDraft({});
				history->session().local().writeDrafts(history);
			}
		}
	};

	const auto fail = [=](const QString &error, mtpRequestId requestId) {
		if (const auto editDraft = history->localEditDraft({})) {
			if (editDraft->saveRequestId == requestId) {
				editDraft->saveRequestId = 0;
			}
		}
		crl::guard(weak, [=] {
			if (requestId == _saveEditMsgRequestId) {
				_saveEditMsgRequestId = 0;
			}
			if (ranges::contains(Api::kDefaultEditMessagesErrors, error)) {
				controller()->showToast(tr::lng_edit_error(tr::now));
			} else if (error == u"MESSAGE_NOT_MODIFIED"_q) {
				cancelEdit();
			} else if (error == u"MESSAGE_EMPTY"_q) {
				_field->selectAll();
				setInnerFocus();
			} else {
				controller()->showToast(tr::lng_edit_error(tr::now));
			}
			update();
		})();
	};

	auto options = Api::SendOptions();
	_saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		webPageDraft,
		options,
		done,
		fail);
}

void HistoryWidget::hideChildWidgets() {
	if (Ui::InFocusChain(this)) {
		// Removing focus from list clears selected and updates top bar.
		setFocus();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
	}
	if (_pinnedBar) {
		_pinnedBar->hide();
	}
	if (_translateBar) {
		_translateBar->hide();
	}
	if (_groupCallBar) {
		_groupCallBar->hide();
	}
	if (_requestsBar) {
		_requestsBar->hide();
	}
	if (_voiceRecordBar) {
		_voiceRecordBar->hideFast();
	}
	if (_composeSearch) {
		_composeSearch->hideAnimated();
	}
	if (_chooseTheme) {
		_chooseTheme->hide();
	}
	if (_contactStatus) {
		_contactStatus->hide();
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

Api::SendAction HistoryWidget::prepareSendAction(
		Api::SendOptions options) const {
	auto result = Api::SendAction(_history, options);
	result.replyTo = replyTo();
	result.options.sendAs = _sendAs
		? _history->session().sendAsPeers().resolveChosen(
			_history->peer).get()
		: nullptr;
	return result;
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

	if (!options.scheduled) {
		_cornerButtons.clearReplyReturns();
	}

	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = _field->getTextWithAppliedMarkdown();
	message.webPage = _preview->draft();

	const auto ignoreSlowmodeCountdown = (options.scheduled != 0);
	if (showSendMessageError(
			message.textWithTags,
			ignoreSlowmodeCountdown)) {
		return;
	}

	// Just a flag not to drop reply info if we're not sending anything.
	_justMarkingAsRead = !HasSendText(_field)
		&& message.webPage.url.isEmpty();
	session().api().sendMessage(std::move(message));
	_justMarkingAsRead = false;

	clearFieldText();
	if (_preview) {
		_preview->apply({ .removed = true });
	}
	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	hideSelectorControlsAnimated();

	setInnerFocus();

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
	send({ .handleSupportSwitch = Support::HandleSwitch(modifiers) });
}

void HistoryWidget::sendSilent() {
	send({ .silent = true });
}

void HistoryWidget::sendScheduled() {
	if (!_list) {
		return;
	}
	const auto ignoreSlowmodeCountdown = true;
	if (showSendMessageError(
			_field->getTextWithAppliedMarkdown(),
			ignoreSlowmodeCountdown)) {
		return;
	}
	const auto callback = [=](Api::SendOptions options) { send(options); };
	controller()->show(
		HistoryView::PrepareScheduleBox(_list, sendMenuType(), callback));
}

void HistoryWidget::sendWhenOnline() {
	send(Api::DefaultSendWhenOnlineOptions());
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
	const auto wasMuted = _history->muted();
	const auto muteForSeconds = Data::MuteValue{
		.unmute = wasMuted,
		.forever = !wasMuted,
	};
	session().data().notifySettings().update(_peer, muteForSeconds);
}

void HistoryWidget::reportSelectedMessages() {
	if (!_list || !_chooseForReport || !_list->getSelectionState().count) {
		return;
	}
	const auto ids = _list->getSelectedItems();
	const auto peer = _peer;
	const auto reason = _chooseForReport->reason;
	const auto weak = Ui::MakeWeak(_list.data());
	controller()->window().show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto &st = st::defaultReportBox;
		Ui::ReportDetailsBox(box, st, [=](const QString &text) {
			if (weak) {
				clearSelected();
				controller()->clearChooseReportMessages();
			}
			const auto show = controller()->uiShow();
			Api::SendReport(show, peer, reason, text, ids);
			box->closeBox();
		});
	}));
}

History *HistoryWidget::history() const {
	return _history;
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

// Sometimes _showAtMsgId is set directly.
void HistoryWidget::setMsgId(
		MsgId showAtMsgId,
		const TextWithEntities &highlightPart) {
	if (_showAtMsgHighlightPart != highlightPart) {
		_showAtMsgHighlightPart = highlightPart;
	}
	if (_showAtMsgId != showAtMsgId) {
		_showAtMsgId = showAtMsgId;
		if (_history) {
			controller()->setActiveChatEntry({
				_history,
				FullMsgId(_history->peer->id, _showAtMsgId) });
		}
	}
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

void HistoryWidget::showAnimated(
		Window::SlideDirection direction,
		const Window::SectionSlideParams &params) {
	_showAnimation = nullptr;

	// If we show pinned bar here, we don't want it to change the
	// calculated and prepared scrollTop of the messages history.
	_preserveScrollTop = true;
	show();
	_topBar->finishAnimating();
	_cornerButtons.finishAnimations();
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	_topShadow->setVisible(params.withTopBarShadow ? false : true);
	_preserveScrollTop = false;
	_stickerToast = nullptr;

	auto newContentCache = Ui::GrabWidget(this);

	hideChildWidgets();
	if (params.withTopBarShadow) _topShadow->show();

	if (_history) {
		_topBar->show();
		_topBar->setAnimatingMode(true);
	}

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] { showFinished(); });
	_showAnimation->setPixmaps(params.oldContentCache, newContentCache);
	_showAnimation->start();

	activate();
}

void HistoryWidget::showFinished() {
	_cornerButtons.finishAnimations();
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	_showAnimation = nullptr;
	doneShow();
	synteticScrollToY(_scroll->scrollTop());
}

void HistoryWidget::doneShow() {
	_topBar->setAnimatingMode(false);
	updateCanSendMessage();
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
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	checkActivation();
	controller()->widget()->setInnerFocus();
	_preserveScrollTop = false;
	checkSuggestToGigagroup();
}

void HistoryWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	if (position == Data::UnreadMessagePosition) {
		DEBUG_LOG(("JumpToEnd(%1, %2, %3): Show at unread requested."
			).arg(_history->peer->name()
			).arg(_history->inboxReadTillId().bare
			).arg(Logs::b(_history->loadedAtBottom())));
		showHistory(_peer->id, ShowAtUnreadMsgId, {});
	} else if (_peer && position.fullId.peer == _peer->id) {
		showHistory(_peer->id, position.fullId.msg, {});
	} else if (_migrated && position.fullId.peer == _migrated->peer->id) {
		showHistory(_peer->id, -position.fullId.msg, {});
	}
}

Data::Thread *HistoryWidget::cornerButtonsThread() {
	return _history;
}

FullMsgId HistoryWidget::cornerButtonsCurrentId() {
	return (_migrated && _showAtMsgId < 0)
		? FullMsgId(_migrated->peer->id, -_showAtMsgId)
		: (_history && _showAtMsgId > 0)
		? FullMsgId(_history->peer->id, _showAtMsgId)
		: FullMsgId();
}

void HistoryWidget::checkSuggestToGigagroup() {
	const auto group = _peer ? _peer->asMegagroup() : nullptr;
	if (!group || !group->owner().suggestToGigagroup(group)) {
		return;
	}
	InvokeQueued(_list, [=] {
		if (!controller()->isLayerShown()) {
			group->owner().setSuggestToGigagroup(group, false);
			group->session().api().request(MTPhelp_DismissSuggestion(
				group->input,
				MTP_string("convert_to_gigagroup")
			)).send();
			controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::lng_gigagroup_suggest_title());
				box->addRow(
					object_ptr<Ui::FlatLabel>(
						box,
						tr::lng_gigagroup_suggest_text(
						) | Ui::Text::ToRichLangValue(),
						st::infoAboutGigagroup));
				box->addButton(
					tr::lng_gigagroup_suggest_more(),
					AboutGigagroupCallback(group, controller()));
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		}
	});
}

void HistoryWidget::finishAnimating() {
	if (!_showAnimation) {
		return;
	}
	_showAnimation = nullptr;
	_topShadow->setVisible(_peer != nullptr);
	_topBar->setVisible(_peer != nullptr);
	_cornerButtons.finishAnimations();
}

void HistoryWidget::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	if (_editMsgId) {
		controller()->showToast(tr::lng_edit_caption_attach(tr::now));
		return;
	}

	if (!_peer || !_canSendMessages) {
		return;
	} else if (const auto error = Data::AnyFileRestrictionError(_peer)) {
		controller()->showToast(*error);
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	const auto filter = (overrideSendImagesAsPhotos == true)
		? FileDialog::ImagesOrAllFilter()
		: FileDialog::AllOrImagesFilter();

	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), filter, crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto read = Images::Read({
				.content = result.remoteContent,
			});
			if (!read.image.isNull() && !read.animated) {
				confirmSendingFiles(
					std::move(read.image),
					std::move(result.remoteContent),
					overrideSendImagesAsPhotos);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			const auto premium = controller()->session().user()->isPremium();
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize,
				premium);
			list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
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
	const auto isReadyToForward = readyToForward();
	const auto detailsRect = QRect(
		0,
		_field->y() - st::historySendPadding - st::historyReplyHeight,
		width() - _fieldBarCancel->width(),
		st::historyReplyHeight);
	const auto hasWebPage = !!_previewDrawPreview;
	const auto inDetails = detailsRect.contains(pos)
		&& (_editMsgId || replyTo() || isReadyToForward || hasWebPage);
	const auto inPhotoEdit = inDetails
		&& _photoEditMedia
		&& QRect(
			detailsRect.x() + st::historyReplySkip,
			(detailsRect.y()
				+ (detailsRect.height() - st::historyReplyPreview) / 2),
			st::historyReplyPreview,
			st::historyReplyPreview).contains(pos);
	const auto inClickable = inDetails;
	if (_inPhotoEdit != inPhotoEdit) {
		_inPhotoEdit = inPhotoEdit;
		if (_photoEditMedia) {
			_inPhotoEditOver.start(
				[=] { updateField(); },
				_inPhotoEdit ? 0. : 1.,
				_inPhotoEdit ? 1. : 0.,
				st::defaultMessageBar.duration);
		} else {
			_inPhotoEditOver.stop();
		}
	}
	_inDetails = inDetails && !inPhotoEdit;
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
}

void HistoryWidget::sendBotCommand(const Bot::SendCommandRequest &request) {
// replyTo != 0 from ReplyKeyboardMarkup, == 0 from command links
	if (_peer != request.peer.get()) {
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	const auto forMsgId = _keyboard->forMsgId();
	const auto lastKeyboardUsed = (forMsgId == request.replyTo.messageId)
		&& (forMsgId == FullMsgId(_peer->id, _history->lastKeyboardId));

	// 'bot' may be nullptr in case of sending from FieldAutocomplete.
	const auto toSend = (request.replyTo/* || !bot*/)
		? request.command
		: Bot::WrapCommandInChat(_peer, request.command, request.context);

	auto message = Api::MessageToSend(prepareSendAction({}));
	message.textWithTags = { toSend, TextWithTags::Tags() };
	message.action.replyTo = request.replyTo
		? ((!_peer->isUser()/* && (botStatus == 0 || botStatus == 2)*/)
			? request.replyTo
			: replyTo())
		: FullReplyTo();
	session().api().sendMessage(std::move(message));
	if (request.replyTo) {
		if (_replyTo == request.replyTo) {
			cancelReply();
			saveCloudDraft();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) toggleKeyboard(false);
			_history->lastKeyboardUsed = true;
		}
	}

	setInnerFocus();
}

void HistoryWidget::hideSingleUseKeyboard(FullMsgId replyToId) {
	if (!_peer || _peer->id != replyToId.peer) {
		return;
	}

	bool lastKeyboardUsed = (_keyboard->forMsgId() == replyToId)
		&& (_keyboard->forMsgId()
			== FullMsgId(_peer->id, _history->lastKeyboardId));
	if (replyToId) {
		if (_replyTo.messageId == replyToId) {
			cancelReply();
			saveCloudDraft();
		}
		if (_keyboard->singleUse()
			&& _keyboard->hasMarkup()
			&& lastKeyboardUsed) {
			if (_kbShown) {
				toggleKeyboard(false);
			}
			_history->lastKeyboardUsed = true;
		}
	}
}

bool HistoryWidget::insertBotCommand(const QString &cmd) {
	if (!_canSendTexts) {
		return false;
	}

	auto insertingInlineBot = !cmd.isEmpty() && (cmd.at(0) == '@');
	auto toInsert = cmd;
	if (!toInsert.isEmpty() && !insertingInlineBot) {
		auto bot = _peer->isUser()
			? _peer
			: (HistoryView::Element::HoveredLink()
				? HistoryView::Element::HoveredLink()->data()->fromOriginal().get()
				: nullptr);
		if (bot && (!bot->isUser() || !bot->asUser()->isBot())) {
			bot = nullptr;
		}
		auto username = bot ? bot->asUser()->username() : QString();
		auto botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
		if (toInsert.indexOf('@') < 0 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
			toInsert += '@' + username;
		}
	}
	toInsert += ' ';

	if (!insertingInlineBot) {
		auto &textWithTags = _field->getTextWithTags();
		TextWithTags textWithTagsToSet;
		const auto m = QRegularExpression(u"^/[A-Za-z_0-9]{0,64}(@[A-Za-z_0-9]{0,32})?(\\s|$)"_q).match(textWithTags.text);
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
		setInnerFocus();
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
	return TWidget::eventFilter(obj, e);
}

bool HistoryWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _peer ? _scroll->viewportEvent(e) : false;
}

QRect HistoryWidget::floatPlayerAvailableRect() {
	return _peer ? mapToGlobal(_scroll->geometry()) : mapToGlobal(rect());
}

bool HistoryWidget::readyToForward() const {
	return _canSendMessages && !_forwardPanel->empty();
}

bool HistoryWidget::hasSilentToggle() const {
	return _peer
		&& _peer->isBroadcast()
		&& Data::CanSendAnything(_peer)
		&& !session().data().notifySettings().silentPostsUnknown(_peer);
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

bool HistoryWidget::isReportMessages() const {
	return _peer && _chooseForReport && _chooseForReport->active;
}

bool HistoryWidget::isBlocked() const {
	return _peer && _peer->isUser() && _peer->asUser()->isBlocked();
}

bool HistoryWidget::isJoinChannel() const {
	return _peer && _peer->isChannel() && !_peer->asChannel()->amIn();
}

bool HistoryWidget::isChoosingTheme() const {
	return _chooseTheme && _chooseTheme->shouldBeShown();
}

bool HistoryWidget::isMuteUnmute() const {
	return _peer
		&& ((_peer->isBroadcast() && !_peer->asChannel()->canPostMessages())
			|| (_peer->isGigagroup() && !Data::CanSendAnything(_peer))
			|| _peer->isRepliesChat());
}

bool HistoryWidget::isSearching() const {
	return _composeSearch != nullptr;
}

bool HistoryWidget::showRecordButton() const {
	return Media::Capture::instance()->available()
		&& !_voiceRecordBar->isListenState()
		&& !_voiceRecordBar->isRecordingByAnotherBar()
		&& !HasSendText(_field)
		&& !_previewDrawPreview
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
	const auto bot = (_peer && _peer->isUser() && _peer->asUser()->isBot())
		? _peer->asUser()
		: nullptr;
	bool cmdStartShown = false;
	if (_history && _peer && ((_peer->isChat() && _peer->asChat()->botStatus > 0) || (_peer->isMegagroup() && _peer->asChannel()->mgInfo->botStatus > 0))) {
		if (!isBotStart() && !isBlocked() && !_keyboard->hasMarkup() && !_keyboard->forceReply() && !_editMsgId) {
			if (!HasSendText(_field)) {
				cmdStartShown = true;
			}
		}
	}
	const auto commandsChanged = (_cmdStartShown != cmdStartShown);
	auto buttonChanged = false;
	if (!bot
		|| (bot->botInfo->botMenuButtonUrl.isEmpty()
			&& bot->botInfo->commands.empty())) {
		buttonChanged = (_botMenuButton != nullptr);
		_botMenuButton.destroy();
	} else if (!_botMenuButton) {
		buttonChanged = true;
		_botMenuButtonText = bot->botInfo->botMenuButtonText;
		_botMenuButton.create(
			this,
			(_botMenuButtonText.isEmpty()
				? tr::lng_bot_menu_button()
				: rpl::single(_botMenuButtonText)),
			st::historyBotMenuButton);
		orderWidgets();

		_botMenuButton->setTextTransform(
			Ui::RoundButton::TextTransform::NoTransform);
		_botMenuButton->setFullRadius(true);
		_botMenuButton->setClickedCallback([=] {
			const auto user = _peer ? _peer->asUser() : nullptr;
			const auto bot = (user && user->isBot()) ? user : nullptr;
			if (bot && !bot->botInfo->botMenuButtonUrl.isEmpty()) {
				session().attachWebView().requestMenu(controller(), bot);
			} else if (!_fieldAutocomplete->isHidden()) {
				_fieldAutocomplete->hideAnimated();
			} else {
				_fieldAutocomplete->showFiltered(_peer, "/", true);
			}
		});
		_botMenuButton->widthValue(
		) | rpl::start_with_next([=](int width) {
			if (width > st::historyBotMenuMaxWidth) {
				_botMenuButton->setFullWidth(st::historyBotMenuMaxWidth);
			} else {
				updateFieldSize();
			}
		}, _botMenuButton->lifetime());
	}
	const auto textChanged = _botMenuButton
		&& (_botMenuButtonText != bot->botInfo->botMenuButtonText);
	if (textChanged) {
		_botMenuButtonText = bot->botInfo->botMenuButtonText;
		_botMenuButton->setText(_botMenuButtonText.isEmpty()
			? tr::lng_bot_menu_button()
			: rpl::single(_botMenuButtonText));
	}
	_cmdStartShown = cmdStartShown;
	return commandsChanged || buttonChanged || textChanged;
}

void HistoryWidget::searchInChat() {
	if (_history) {
		controller()->content()->searchInChat(_history);
	}
}

void HistoryWidget::searchInChatEmbedded(std::optional<QString> query) {
	if (!_history) {
		return;
	} else if (_composeSearch) {
		if (query) {
			_composeSearch->setQuery(*query);
		}
		_composeSearch->setInnerFocus();
		return;
	}
	const auto search = crl::guard(_list, [=] {
		if (!_history) {
			return;
		}
		const auto update = [=] {
			updateControlsVisibility();
			updateBotKeyboard();
			updateFieldPlaceholder();

			updateControlsGeometry();
		};
		_composeSearch = std::make_unique<HistoryView::ComposeSearch>(
			this,
			controller(),
			_history,
			query.value_or(QString()));

		update();
		setInnerFocus();
		_composeSearch->destroyRequests(
		) | rpl::take(
			1
		) | rpl::start_with_next([=] {
			_composeSearch = nullptr;

		update();
		setInnerFocus();
		}, _composeSearch->lifetime());
	});
	if (!preventsClose(search)) {
		search();
	}
}

bool HistoryWidget::kbWasHidden() const {
	return _history
		&& (_keyboard->forMsgId()
			== FullMsgId(
				_history->peer->id,
				_history->lastKeyboardHiddenId));
}

void HistoryWidget::showKeyboardHideButton() {
	_botKeyboardHide->setVisible(!_peer->isUser()
		|| !_keyboard->persistent());
}

void HistoryWidget::toggleKeyboard(bool manual) {
	auto fieldEnabled = canWriteMessage() && !_showAnimation;
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
			if (!readyToForward()
				&& !_previewDrawPreview
				&& !_editMsgId
				&& !_replyTo) {
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
		if (_kbReplyTo && !_editMsgId && !_replyTo && fieldEnabled) {
			updateReplyToName();
			updateReplyEditText(_kbReplyTo);
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	} else if (fieldEnabled) {
		showKeyboardHideButton();
		_botKeyboardShow->hide();
		_kbScroll->show();
		_kbShown = true;

		const auto maxheight = computeMaxFieldHeight();
		const auto kbheight = qMin(_keyboard->height(), maxheight - (maxheight / 2));
		_field->setMaxHeight(maxheight - kbheight);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply())
			? session().data().message(_keyboard->forMsgId())
			: nullptr;
		if (_kbReplyTo && !_editMsgId && !_replyTo) {
			updateReplyToName();
			updateReplyEditText(_kbReplyTo);
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	}
	updateControlsGeometry();
	updateFieldPlaceholder();
	if (_botKeyboardHide->isHidden() && canWriteMessage() && !_showAnimation) {
		_tabbedSelectorToggle->show();
	} else {
		_tabbedSelectorToggle->hide();
	}
	updateField();
}

void HistoryWidget::startBotCommand() {
	setFieldText(
		{ u"/"_q, TextWithTags::Tags() },
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
		_membersDropdown->setOwnedWidget(
			object_ptr<Profile::GroupMembersWidget>(
				this,
				controller(),
				_peer,
				st::membersInnerItem));
		_membersDropdown->resizeToWidth(st::membersInnerWidth);

		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
		_membersDropdown->moveToLeft(0, _topBar->height());
		_membersDropdown->setHiddenCallback([this] { _membersDropdown.destroyDelayed(); });
	}
	_membersDropdown->otherEnter();
}

bool HistoryWidget::pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) {
	if (!_tabbedPanel) {
		return true;
	} else if (!Data::CanSendAnyOf(
			thread,
			Data::TabbedPanelSendRestrictions())) {
		Core::App().settings().setTabbedReplacedWithInfo(true);
		controller()->showPeerInfo(thread, params.withThirdColumn());
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
	if (!_history) {
		return;
	}
	if (_tabbedPanel) {
		if (controller()->canShowThirdSection()
			&& !controller()->adaptive().isOneColumn()) {
			Core::App().settings().setTabbedSelectorSectionEnabled(true);
			Core::App().saveSettingsDelayed();
			pushTabbedSelectorToThirdSection(
				_history,
				Window::SectionShow::Way::ClearStack);
		} else {
			_tabbedPanel->toggleAnimated();
		}
	} else {
		controller()->closeThirdSection();
	}
}

void HistoryWidget::recountChatWidth() {
	const auto layout = (width() < st::adaptiveChatWideWidth)
		? Window::Adaptive::ChatLayout::Normal
		: Window::Adaptive::ChatLayout::Wide;
	controller()->adaptive().setChatLayout(layout);
}

int HistoryWidget::fieldHeight() const {
	return (_canSendTexts || _editMsgId)
		? _field->height()
		: (st::historySendSize.height() - 2 * st::historySendPadding);
}

bool HistoryWidget::fieldOrDisabledShown() const {
	return !_field->isHidden() || _fieldDisabled;
}

void HistoryWidget::moveFieldControls() {
	auto keyboardHeight = 0;
	auto bottom = height();
	auto maxKeyboardHeight = computeMaxFieldHeight() - fieldHeight();
	_keyboard->resizeToWidth(width(), maxKeyboardHeight);
	if (_kbShown) {
		keyboardHeight = qMin(_keyboard->height(), maxKeyboardHeight);
		bottom -= keyboardHeight;
		_kbScroll->setGeometryToLeft(0, bottom, width(), keyboardHeight);
	}

// (_botMenuButton) (_attachToggle|_replaceMedia) (_sendAs) ---- _inlineResults ------------------------------ _tabbedPanel ------ _fieldBarCancel
// (_attachDocument|_attachPhoto) _field (_ttlInfo) (_scheduled) (_silent|_cmdStart|_kbShow) (_kbHide|_tabbedSelectorToggle) _send
// (_botStart|_unblock|_joinChannel|_muteUnmute|_reportMessages)

	auto buttonsBottom = bottom - _attachToggle->height();
	auto left = st::historySendRight;
	if (_botMenuButton) {
		const auto skip = st::historyBotMenuSkip;
		_botMenuButton->moveToLeft(left + skip, buttonsBottom + skip); left += skip + _botMenuButton->width();
	}
	if (_replaceMedia) {
		_replaceMedia->moveToLeft(left, buttonsBottom);
	}
	_attachToggle->moveToLeft(left, buttonsBottom); left += _attachToggle->width();
	if (_sendAs) {
		_sendAs->moveToLeft(left, buttonsBottom); left += _sendAs->width();
	}
	_field->moveToLeft(left, bottom - _field->height() - st::historySendPadding);
	if (_fieldDisabled) {
		_fieldDisabled->moveToLeft(
			left,
			bottom - fieldHeight() - st::historySendPadding);
	}
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
		right += _scheduled->width();
	}
	if (_ttlInfo) {
		_ttlInfo->move(width() - right - _ttlInfo->width(), buttonsBottom);
	}

	_fieldBarCancel->moveToRight(0, _field->y() - st::historySendPadding - _fieldBarCancel->height());
	if (_inlineResults) {
		_inlineResults->moveBottom(_field->y() - st::historySendPadding);
	}
	if (_tabbedPanel) {
		_tabbedPanel->moveBottomRight(buttonsBottom, width());
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->moveToLeft(
			0,
			buttonsBottom - _attachBotsMenu->height());
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
	_reportMessages->setGeometry(fullWidthButtonRect);
}

void HistoryWidget::updateFieldSize() {
	const auto kbShowShown = _history && !_kbShown && _keyboard->hasMarkup();
	auto fieldWidth = width()
		- _attachToggle->width()
		- st::historySendRight
		- _send->width()
		- _tabbedSelectorToggle->width();
	if (_botMenuButton) {
		fieldWidth -= st::historyBotMenuSkip + _botMenuButton->width();
	}
	if (_sendAs) {
		fieldWidth -= _sendAs->width();
	}
	if (kbShowShown) {
		fieldWidth -= _botKeyboardShow->width();
	}
	if (_cmdStartShown) {
		fieldWidth -= _botCommandStart->width();
	}
	if (_silent && _silent->isVisible()) {
		fieldWidth -= _silent->width();
	}
	if (_scheduled && _scheduled->isVisible()) {
		fieldWidth -= _scheduled->width();
	}
	if (_ttlInfo && _ttlInfo->isVisible()) {
		fieldWidth -= _ttlInfo->width();
	}

	if (_fieldDisabled) {
		_fieldDisabled->resize(fieldWidth, fieldHeight());
	}
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
	if (!_history || _showAnimation) {
		return;
	}

	const auto autocomplete = parseMentionHashtagBotCommandQuery();
	_fieldAutocomplete->showFiltered(
		_peer,
		autocomplete.query,
		autocomplete.fromStart);
}

void HistoryWidget::updateFieldPlaceholder() {
	if (!_editMsgId && _inlineBot && !_inlineLookingUpBot) {
		_field->setPlaceholder(
			rpl::single(_inlineBot->botInfo->inlinePlaceholder.mid(1)),
			_inlineBotUsername.size() + 2);
		return;
	}

	_field->setPlaceholder([&]() -> rpl::producer<QString> {
		if (_editMsgId) {
			return tr::lng_edit_message_text();
		} else if (!_history) {
			return tr::lng_message_ph();
		} else if ((_kbShown || _keyboard->forceReply())
			&& !_keyboard->placeholder().isEmpty()) {
			return rpl::single(_keyboard->placeholder());
		} else if (const auto channel = _history->peer->asChannel()) {
			if (channel->isBroadcast()) {
				return session().data().notifySettings().silentPosts(channel)
					? tr::lng_broadcast_silent_ph()
					: tr::lng_broadcast_ph();
			} else if (channel->adminRights() & ChatAdminRight::Anonymous) {
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
	return showSendingFilesError(list, std::nullopt);
}

bool HistoryWidget::showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const {
	const auto text = [&] {
		const auto error = _peer
			? Data::FileRestrictionError(_peer, list, compress)
			: std::nullopt;
		if (error) {
			return *error;
		} else if (const auto left = _peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWordsSlowmode(left));
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
		case Error::TooLargeFile: return u"(toolarge)"_q;
		}
		return tr::lng_forward_send_files_cant(tr::now);
	}();
	if (text.isEmpty()) {
		return false;
	} else if (text == u"(toolarge)"_q) {
		const auto fileSize = list.files.back().size;
		controller()->show(
			Box(FileSizeLimitBox, &session(), fileSize, nullptr));
		return true;
	}
	controller()->showToast(text);
	return true;
}

bool HistoryWidget::showSendMessageError(
		const TextWithTags &textWithTags,
		bool ignoreSlowmodeCountdown) const {
	if (!_canSendMessages) {
		return false;
	}
	const auto topicRootId = _replyEditMsg
		? _replyEditMsg->topicRootId()
		: 0;
	const auto error = GetErrorTextForSending(
		_peer,
		{
			.topicRootId = topicRootId,
			.forward = &_forwardPanel->items(),
			.text = &textWithTags,
			.ignoreSlowmodeCountdown = ignoreSlowmodeCountdown,
		});
	if (error.isEmpty()) {
		return false;
	}
	controller()->showToast(error);
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
	const auto premium = controller()->session().user()->isPremium();
	return confirmSendingFiles(
		Storage::PrepareMediaList(files, st::sendMediaPreviewSize, premium),
		insertTextOnCancel);
}

bool HistoryWidget::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (_editMsgId) {
		if (_canReplaceMedia) {
			EditCaptionBox::StartMediaReplace(
				controller(),
				{ _history->peer->id, _editMsgId },
				std::move(list),
				_field->getTextWithTags(),
				crl::guard(_list, [=] { cancelEdit(); }));
			return true;
		}
		controller()->showToast(tr::lng_edit_caption_attach(tr::now));
		return false;
	} else if (showSendingFilesError(list)) {
		return false;
	}

	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	const auto text = _field->getTextWithTags();
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		text,
		DefaultLimitsForPeer(_peer),
		DefaultCheckForPeer(controller(), _peer),
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
	controller()->show(std::move(box));

	return true;
}

void HistoryWidget::sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	Expects(list.filesToProcess.empty());

	const auto compress = way.sendImagesAsPhotos();
	if (showSendingFilesError(list, compress)) {
		return;
	}
	auto groups = DivideByGroups(
		std::move(list),
		way,
		_peer->slowmodeApplied());
	const auto type = compress ? SendMediaType::Photo : SendMediaType::File;
	auto action = prepareSendAction(options);
	action.clearDraft = false;
	if ((groups.size() != 1 || !groups.front().sentWithCaption())
		&& !caption.text.isEmpty()) {
		auto message = Api::MessageToSend(action);
		message.textWithTags = base::take(caption);
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
	} else if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
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
	const auto premium = controller()->session().user()->isPremium();

	if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
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

	if (auto read = Core::ReadMimeImage(data)) {
		confirmSendingFiles(
			std::move(read.image),
			std::move(read.content),
			overrideSendImagesAsPhotos,
			insertTextOnCancel);
		return true;
	}
	return false;
}

void HistoryWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	if (!canWriteMessage()) return;

	session().api().sendFile(fileContent, type, prepareSendAction({}));
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
			const auto reportMessages = isReportMessages();
			const auto update = false
				|| (_reportMessages->isHidden() == reportMessages)
				|| (!reportMessages && _unblock->isHidden() == unblock)
				|| (!reportMessages
					&& !unblock
					&& _botStart->isHidden() == botStart)
				|| (!reportMessages
					&& !unblock
					&& !botStart
					&& _joinChannel->isHidden() == joinChannel)
				|| (!reportMessages
					&& !unblock
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
	const auto requestsTop = groupCallTop + (_groupCallBar ? _groupCallBar->height() : 0);
	if (_requestsBar) {
		_requestsBar->move(0, requestsTop);
		_requestsBar->resizeToWidth(width());
	}
	const auto pinnedBarTop = requestsTop + (_requestsBar ? _requestsBar->height() : 0);
	if (_pinnedBar) {
		_pinnedBar->move(0, pinnedBarTop);
		_pinnedBar->resizeToWidth(width());
	}
	const auto translateTop = pinnedBarTop + (_pinnedBar ? _pinnedBar->height() : 0);
	if (_translateBar) {
		_translateBar->move(0, translateTop);
		_translateBar->resizeToWidth(width());
	}
	const auto contactStatusTop = translateTop + (_translateBar ? _translateBar->height() : 0);
	if (_contactStatus) {
		_contactStatus->bar().move(0, contactStatusTop);
	}
	const auto scrollAreaTop = contactStatusTop + (_contactStatus ? _contactStatus->bar().height() : 0);
	if (_scroll->y() != scrollAreaTop) {
		_scroll->moveToLeft(0, scrollAreaTop);
		_fieldAutocomplete->setBoundings(_scroll->geometry());
		if (_supportAutocomplete) {
			_supportAutocomplete->setBoundings(_scroll->geometry());
		}
	}

	updateHistoryGeometry(false, false, { ScrollChangeAdd, _topDelta });

	updateFieldSize();

	_cornerButtons.updatePositions();

	if (_membersDropdown) {
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}

	const auto isOneColumn = controller()->adaptive().isOneColumn();
	const auto isThreeColumn = controller()->adaptive().isThreeColumn();
	const auto topShadowLeft = (isOneColumn || _inGrab)
		? 0
		: st::lineWidth;
	const auto topShadowRight = (isThreeColumn && !_inGrab && _peer)
		? st::lineWidth
		: 0;
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
	if (item == _replyEditMsg && _replyTo) {
		cancelReply();
	}
	if (item == _processingReplyItem) {
		_processingReplyTo = {};
		_processingReplyItem = nullptr;
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		toggleKeyboard();
		_kbReplyTo = nullptr;
	}
	const auto i = _itemRevealAnimations.find(item);
	if (i != end(_itemRevealAnimations)) {
		_itemRevealAnimations.erase(i);
		revealItemsCallback();
	}
	const auto j = _itemRevealPending.find(item);
	if (j != _itemRevealPending.end()) {
		_itemRevealPending.erase(j);
	}
}

void HistoryWidget::itemEdited(not_null<HistoryItem*> item) {
	if (item.get() == _replyEditMsg) {
		updateReplyEditTexts(true);
	}
}

FullReplyTo HistoryWidget::replyTo() const {
	return _replyTo
		? _replyTo
		: _kbReplyTo
		? FullReplyTo{ _kbReplyTo->fullId() }
		: FullReplyTo();
}

bool HistoryWidget::hasSavedScroll() const {
	Expects(_history != nullptr);

	return _history->scrollTopItem
		|| (_migrated && _migrated->scrollTopItem);
}

int HistoryWidget::countInitialScrollTop() {
	if (hasSavedScroll()) {
		return _list->historyScrollTop();
	} else if (_showAtMsgId
		&& (IsServerMsgId(_showAtMsgId)
			|| IsClientMsgId(_showAtMsgId)
			|| IsServerMsgId(-_showAtMsgId))) {
		const auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
		const auto itemTop = _list->itemTop(item);
		if (itemTop < 0) {
			setMsgId(ShowAtUnreadMsgId);
			controller()->showToast(tr::lng_message_not_found(tr::now));
			return countInitialScrollTop();
		} else {
			const auto view = item->mainView();
			Assert(view != nullptr);

			enqueueMessageHighlight(
				view,
				base::take(_showAtMsgHighlightPart));
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
	const auto guard = gsl::finally([&] {
		_itemRevealPending.clear();
	});
	if (!_history || (initial && _historyInited) || (!initial && !_historyInited)) {
		return;
	}
	if (_firstLoadRequest || _showAnimation) {
		_updateHistoryGeometryRequired = true;
		return; // scrollTopMax etc are not working after recountHistoryGeometry()
	}

	auto newScrollHeight = height() - _topBar->height();
	if (_translateBar) {
		newScrollHeight -= _translateBar->height();
	}
	if (_pinnedBar) {
		newScrollHeight -= _pinnedBar->height();
	}
	if (_groupCallBar) {
		newScrollHeight -= _groupCallBar->height();
	}
	if (_requestsBar) {
		newScrollHeight -= _requestsBar->height();
	}
	if (_contactStatus) {
		newScrollHeight -= _contactStatus->bar().height();
	}
	if (isChoosingTheme()) {
		newScrollHeight -= _chooseTheme->height();
	} else if (!editingMessage()
		&& (isSearching()
			|| isBlocked()
			|| isBotStart()
			|| isJoinChannel()
			|| isMuteUnmute()
			|| isReportMessages())) {
		newScrollHeight -= _unblock->height();
	} else {
		if (editingMessage() || _canSendMessages) {
			newScrollHeight -= (fieldHeight() + 2 * st::historySendPadding);
		} else if (writeRestriction().has_value()) {
			newScrollHeight -= _unblock->height();
		}
		if (_editMsgId
			|| replyTo()
			|| readyToForward()
			|| _previewDrawPreview) {
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
		_cornerButtons.updatePositions();
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
		}
	}
	const auto toY = std::clamp(newScrollTop, 0, _scroll->scrollTopMax());
	synteticScrollToY(toY);
}

void HistoryWidget::revealItemsCallback() {
	auto height = 0;
	if (!_historyInited) {
		_itemRevealAnimations.clear();
	}
	for (auto i = begin(_itemRevealAnimations)
		; i != end(_itemRevealAnimations);) {
		if (!i->second.animation.animating()) {
			i = _itemRevealAnimations.erase(i);
		} else {
			height += anim::interpolate(
				i->second.startHeight,
				0,
				i->second.animation.value(1.));
			++i;
		}
	}
	if (_itemsRevealHeight != height) {
		const auto wasScrollTop = _scroll->scrollTop();
		const auto wasAtBottom = (wasScrollTop == _scroll->scrollTopMax());
		if (!wasAtBottom) {
			height = 0;
			_itemRevealAnimations.clear();
		}

		_itemsRevealHeight = height;
		_list->changeItemsRevealHeight(_itemsRevealHeight);

		const auto newScrollTop = (wasAtBottom && !_history->unreadBar())
			? countAutomaticScrollTop()
			: _list->historyScrollTop();
		const auto toY = std::clamp(newScrollTop, 0, _scroll->scrollTopMax());
		synteticScrollToY(toY);
	}
}

void HistoryWidget::startItemRevealAnimations() {
	for (const auto &item : base::take(_itemRevealPending)) {
		if (const auto view = item->mainView()) {
			if (const auto top = _list->itemTop(view); top >= 0) {
				if (const auto height = view->height()) {
					startMessageSendingAnimation(item);
					if (!_itemRevealAnimations.contains(item)) {
						auto &animation = _itemRevealAnimations[item];
						animation.startHeight = height;
						_itemsRevealHeight += height;
						animation.animation.start(
							[=] { revealItemsCallback(); },
							0.,
							1.,
							HistoryView::ListWidget::kItemRevealDuration,
							anim::easeOutCirc);
						if (item->out() || _history->peer->isSelf()) {
							_list->theme()->rotateComplexGradientBackground();
						}
					}
				}
			}
		}
	}
}

void HistoryWidget::startMessageSendingAnimation(
		not_null<HistoryItem*> item) {
	auto &sendingAnimation = controller()->sendingAnimation();
	if (!sendingAnimation.hasLocalMessage(item->fullId().msg)
		|| !sendingAnimation.checkExpectedType(item)) {
		return;
	}
	Assert(item->mainView() != nullptr);
	Assert(item->mainView()->media() != nullptr);

	auto globalEndTopLeft = rpl::merge(
		_scroll->innerResizes() | rpl::to_empty,
		session().data().newItemAdded() | rpl::to_empty,
		geometryValue() | rpl::to_empty,
		_scroll->geometryValue() | rpl::to_empty,
		_list->geometryValue() | rpl::to_empty
	) | rpl::map([=] {
		const auto view = item->mainView();
		const auto additional = (_list->height() == _scroll->height())
			? view->height()
			: 0;
		return _list->mapToGlobal(QPoint(
			0,
			_list->itemTop(view) - additional));
	});

	sendingAnimation.startAnimation({
		.globalEndTopLeft = std::move(globalEndTopLeft),
		.view = [=] { return item->mainView(); },
		.paintContext = [=] { return _list->preparePaintContext({}); },
	});
}

void HistoryWidget::updateListSize() {
	Expects(_list != nullptr);

	_list->recountHistoryGeometry();
	auto washidden = _scroll->isHidden();
	if (washidden) {
		_scroll->show();
	}
	startItemRevealAnimations();
	_list->setItemsRevealHeight(_itemsRevealHeight);
	_list->updateSize();
	if (washidden) {
		_scroll->hide();
	}
	_updateHistoryGeometryRequired = true;
}

bool HistoryWidget::hasPendingResizedItems() const {
	if (!_list) {
		// Based on the crash reports there is a codepath (at least on macOS)
		// that leads from _list = _scroll->setOwnedWidget(...) right into
		// the HistoryWidget::paintEvent (by sending fake mouse move events
		// inside scroll area -> hiding tooltip window -> exposing the main
		// window -> syncing it backing store synchronously).
		//
		// So really we could get here with !_list && (_history != nullptr).
		return false;
	}
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

void HistoryWidget::addMessagesToFront(
		not_null<PeerData*> peer,
		const QVector<MTPMessage> &messages) {
	_list->messagesReceived(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry();
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(
		not_null<PeerData*> peer,
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
	injectSponsoredMessages();
}

void HistoryWidget::updateBotKeyboard(History *h, bool force) {
	if (h && h != _history && h != _migrated) {
		return;
	}

	const auto wasVisible = _kbShown || _kbReplyTo;
	const auto wasMsgId = _keyboard->forMsgId();
	auto changed = false;
	if ((_replyTo && !_replyEditMsg) || _editMsgId || !_history) {
		changed = _keyboard->updateMarkup(nullptr, force);
	} else if (_replyTo && _replyEditMsg) {
		changed = _keyboard->updateMarkup(_replyEditMsg, force);
	} else {
		const auto keyboardItem = _history->lastKeyboardId
			? session().data().message(
				_history->peer,
				_history->lastKeyboardId)
			: nullptr;
		changed = _keyboard->updateMarkup(keyboardItem, force);
	}
	updateCmdStartShown();
	if (!changed) {
		return;
	} else if (_keyboard->forMsgId() != wasMsgId) {
		_kbScroll->scrollTo({ 0, 0 });
	}

	bool hasMarkup = _keyboard->hasMarkup(), forceReply = _keyboard->forceReply() && (!_replyTo || !_replyEditMsg);
	if (hasMarkup || forceReply) {
		if (_keyboard->singleUse()
			&& _keyboard->hasMarkup()
			&& (_keyboard->forMsgId()
				== FullMsgId(_history->peer->id, _history->lastKeyboardId))
			&& _history->lastKeyboardUsed) {
			_history->lastKeyboardHiddenId = _history->lastKeyboardId;
		}
		if (!isSearching() && !isBotStart() && !isBlocked() && _canSendMessages && (wasVisible || (_replyTo && _replyEditMsg) || (!HasSendText(_field) && !kbWasHidden()))) {
			if (!_showAnimation) {
				if (hasMarkup) {
					_kbScroll->show();
					_tabbedSelectorToggle->hide();
					showKeyboardHideButton();
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
			if (_kbReplyTo && !_replyTo) {
				updateReplyToName();
				updateReplyEditText(_kbReplyTo);
			}
		} else {
			if (!_showAnimation) {
				_kbScroll->hide();
				_tabbedSelectorToggle->show();
				_botKeyboardHide->hide();
				_botKeyboardShow->show();
				_botCommandStart->hide();
			}
			_field->setMaxHeight(computeMaxFieldHeight());
			_kbShown = false;
			_kbReplyTo = nullptr;
			if (!readyToForward()
				&& !_previewDrawPreview
				&& !_replyTo) {
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
		if (!readyToForward()
			&& !_previewDrawPreview
			&& !_replyTo
			&& !_editMsgId) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
	}
	refreshTopBarActiveChat();
	updateFieldPlaceholder();
	updateControlsGeometry();
	update();
}

void HistoryWidget::botCallbackSent(not_null<HistoryItem*> item) {
	if (!item->isRegular() || _peer != item->history()->peer) {
		return;
	}

	const auto keyId = _keyboard->forMsgId();
	const auto lastKeyboardUsed = (keyId == FullMsgId(_peer->id, item->id))
		&& (keyId == FullMsgId(_peer->id, _history->lastKeyboardId));

	session().data().requestItemRepaint(item);

	if (_replyTo.messageId == item->fullId()) {
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
}

int HistoryWidget::computeMaxFieldHeight() const {
	const auto available = height()
		- _topBar->height()
		- (_contactStatus ? _contactStatus->bar().height() : 0)
		- (_pinnedBar ? _pinnedBar->height() : 0)
		- (_groupCallBar ? _groupCallBar->height() : 0)
		- (_requestsBar ? _requestsBar->height() : 0)
		- ((_editMsgId
			|| replyTo()
			|| readyToForward()
			|| _previewDrawPreview)
			? st::historyReplyHeight
			: 0)
		- (2 * st::historySendPadding)
		- st::historyReplyHeight; // at least this height for history.
	return std::min(st::historyComposeFieldMaxHeight, available);
}

bool HistoryWidget::cornerButtonsIgnoreVisibility() {
	return _showAnimation != nullptr;
}

std::optional<bool> HistoryWidget::cornerButtonsDownShown() {
	if (!_list || _firstLoadRequest) {
		return false;
	}
	if (_voiceRecordBar->isLockPresent()) {
		return false;
	}
	if (!_history->loadedAtBottom() || _cornerButtons.replyReturn()) {
		return true;
	}
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax()) {
		return true;
	}

	const auto haveUnreadBelowBottom = [&](History *history) {
		if (!_list
			|| !history
			|| history->unreadCount() <= 0
			|| !history->trackUnreadMessages()) {
			return false;
		}
		const auto unread = history->firstUnreadMessage();
		if (!unread) {
			return false;
		}
		const auto top = _list->itemTop(unread);
		return (top >= _scroll->scrollTop() + _scroll->height());
	};
	if (haveUnreadBelowBottom(_history)
		|| haveUnreadBelowBottom(_migrated)) {
		return true;
	}
	return false;
}

bool HistoryWidget::cornerButtonsUnreadMayBeShown() {
	return !_firstLoadRequest && !_voiceRecordBar->isLockPresent();
}

bool HistoryWidget::cornerButtonsHas(HistoryView::CornerButtonType type) {
	return true;
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	const auto isReadyToForward = readyToForward();
	if (_inPhotoEdit && _photoEditMedia) {
		EditCaptionBox::StartPhotoEdit(
			controller(),
			_photoEditMedia,
			{ _history->peer->id, _editMsgId },
			_field->getTextWithTags(),
			crl::guard(_list, [=] { cancelEdit(); }));
	} else if (!_inDetails) {
		return;
	} else if (_previewDrawPreview) {
		editDraftOptions();
	} else if (_editMsgId) {
		controller()->showPeerHistory(
			_peer,
			Window::SectionShow::Way::Forward,
			_editMsgId);
	} else if (isReadyToForward) {
		if (e->button() != Qt::LeftButton) {
			_forwardPanel->editToNextOption();
		} else {
			_forwardPanel->editOptions(controller()->uiShow());
		}
	} else if (_replyTo && (e->modifiers() & Qt::ControlModifier)) {
		jumpToReply(_replyTo);
	} else if (_replyTo) {
		editDraftOptions();
	} else if (_kbReplyTo) {
		controller()->showPeerHistory(
			_kbReplyTo->history()->peer->id,
			Window::SectionShow::Way::Forward,
			_kbReplyTo->id);
	}
}

void HistoryWidget::editDraftOptions() {
	Expects(_history != nullptr);

	const auto history = _history;
	const auto reply = _replyTo;
	const auto webpage = _preview->draft();

	const auto done = [=](
			FullReplyTo replyTo,
			Data::WebPageDraft webpage) {
		if (replyTo) {
			replyToMessage(replyTo);
		} else {
			cancelReply();
		}
		_preview->apply(webpage);
	};
	const auto replyToId = reply.messageId;
	const auto highlight = crl::guard(this, [=](FullReplyTo to) {
		jumpToReply(to);
	});

	using namespace HistoryView::Controls;
	EditDraftOptions({
		.show = controller()->uiShow(),
		.history = history,
		.draft = Data::Draft(_field, reply, _preview->draft()),
		.usedLink = _preview->link(),
		.links = _preview->links(),
		.resolver = _preview->resolver(),
		.done = done,
		.highlight = highlight,
		.clearOldDraft = [=] { ClearDraftReplyTo(history, 0, replyToId); },
	});
}

void HistoryWidget::jumpToReply(FullReplyTo to) {
	if (const auto item = session().data().message(to.messageId)) {
		JumpToMessageClickHandler(item, {}, to.quote)->onClick({});
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	const auto commonModifiers = e->modifiers() & kCommonModifiers;
	if (e->key() == Qt::Key_Escape) {
		if (hasFocus()) {
			escape();
		} else {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Back) {
		_cancelRequests.fire({});
	} else if (e->key() == Qt::Key_PageDown) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_PageUp) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Down && !commonModifiers) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Up && !commonModifiers) {
		const auto item = _history
			? _history->lastEditableMessage()
			: nullptr;
		if (item
			&& _field->empty()
			&& !_editMsgId
			&& !_replyTo) {
			editMessage(item);
			return;
		}
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Up
		&& commonModifiers == Qt::ControlModifier) {
		if (!replyToPreviousMessage()) {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Down
		&& commonModifiers == Qt::ControlModifier) {
		if (!replyToNextMessage()) {
			e->ignore();
		}
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
			(_showAtMsgId > 0) ? (-_showAtMsgId) : _showAtMsgId,
			{});
		channel->session().api().chatParticipants().requestCountDelayed(
			channel);
	} else {
		_migrated = _history->migrateFrom();
		_list->notifyMigrateUpdated();
		setupPinnedTracker();
		setupGroupCallBar();
		setupRequestsBar();
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
	if (!_history
		|| _editMsgId
		|| _history->isForum()
		|| (_replyTo && _replyTo.messageId.peer != _history->peer->id)) {
		return false;
	}
	const auto fullId = FullMsgId(
		_history->peer->id,
		(_field->isVisible()
			? _replyTo.messageId.msg
			: _highlighter.latestSingleHighlightedMsgId()));
	if (const auto item = session().data().message(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto previousView = view->previousDisplayedInBlocks()) {
				const auto previous = previousView->data();
				controller()->showMessage(previous);
				if (_field->isVisible()) {
					replyToMessage(previous);
				}
				return true;
			}
		}
	} else if (const auto previousView = _history->findLastDisplayed()) {
		const auto previous = previousView->data();
		controller()->showMessage(previous);
		if (_field->isVisible()) {
			replyToMessage(previous);
		}
		return true;
	}
	return false;
}

bool HistoryWidget::replyToNextMessage() {
	if (!_history
		|| _editMsgId
		|| _history->isForum()
		|| (_replyTo && _replyTo.messageId.peer != _history->peer->id)) {
		return false;
	}
	const auto fullId = FullMsgId(
		_history->peer->id,
		(_field->isVisible()
			? _replyTo.messageId.msg
			: _highlighter.latestSingleHighlightedMsgId()));
	if (const auto item = session().data().message(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto nextView = view->nextDisplayedInBlocks()) {
				const auto next = nextView->data();
				controller()->showMessage(next);
				if (_field->isVisible()) {
					replyToMessage(next);
				}
			} else {
				_highlighter.clear();
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
				Ui::FormatDurationWordsSlowmode(left));
		} else if (_peer->slowmodeApplied()) {
			if (const auto item = _history->latestSendingMessage()) {
				if (const auto view = item->mainView()) {
					animatedScrollToItem(item->id);
					enqueueMessageHighlight(view, {});
				}
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
		return QString();
	}();
	if (text.isEmpty()) {
		return false;
	}
	controller()->showToast(text);
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
	if (!_peer || !_canSendMessages) {
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	auto errorText = result.result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		controller()->showToast(errorText);
		return;
	}

	controller()->sendingAnimation().appendSending(
		result.messageSendingFrom);

	auto action = prepareSendAction(result.options);
	action.generateLocal = true;
	session().api().sendInlineResult(
		result.bot,
		result.result,
		action,
		result.messageSendingFrom.localId);

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

	setInnerFocus();
}

void HistoryWidget::updatePinnedViewer() {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_history
		|| !_historyInited
		|| !_pinnedTracker) {
		return;
	}
	const auto visibleBottom = _scroll->scrollTop() + _scroll->height();
	auto [view, offset] = _list->findViewForPinnedTracking(visibleBottom);
	const auto lessThanId = !view
		? (ServerMaxMsgId - 1)
		: (view->history() != _history)
		? (view->data()->id + (offset > 0 ? 1 : 0) - ServerMaxMsgId)
		: (view->data()->id + (offset > 0 ? 1 : 0));
	const auto lastClickedId = !_pinnedClickedId
		? (ServerMaxMsgId - 1)
		: (!_migrated || peerIsChannel(_pinnedClickedId.peer))
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
			MsgId(0), // topicRootId
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

void HistoryWidget::setupTranslateBar() {
	Expects(_history != nullptr);

	_translateBar = std::make_unique<HistoryView::TranslateBar>(
		this,
		controller(),
		_history);

	controller()->adaptive().oneColumnValue(
	) | rpl::start_with_next([=, raw = _translateBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _translateBar->lifetime());

	_translateBarHeight = 0;
	_translateBar->heightValue(
	) | rpl::start_with_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _translateBarHeight);
		_translateBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _translateBar->lifetime());

	orderWidgets();

	if (_showAnimation) {
		_translateBar->hide();
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
	Expects(_list != nullptr);

	const auto hiddenId = _peer->canPinMessages()
		? MsgId(0)
		: session().settings().hiddenPinnedMessageId(_peer->id);
	const auto currentPinnedId = Data::ResolveTopPinnedId(
		_peer,
		MsgId(0), // topicRootId
		_migrated ? _migrated->peer.get() : nullptr);
	const auto universalPinnedId = !currentPinnedId
		? int32(0)
		: (_migrated && !peerIsChannel(currentPinnedId.peer))
		? (currentPinnedId.msg - ServerMaxMsgId)
		: currentPinnedId.msg;
	if (universalPinnedId == hiddenId) {
		if (_pinnedBar) {
			_pinnedBar->setContent(rpl::single(Ui::MessageBarContent()));
			_pinnedTracker->reset();
			_list->setShownPinned(nullptr);
			_hidingPinnedBar = base::take(_pinnedBar);
			const auto raw = _hidingPinnedBar.get();
			base::call_delayed(st::defaultMessageBar.duration, this, [=] {
				if (_hidingPinnedBar.get() == raw) {
					clearHidingPinnedBar();
				}
			});
		}
		return;
	}
	if (_pinnedBar || !universalPinnedId) {
		return;
	}

	clearHidingPinnedBar();
	_pinnedBar = std::make_unique<Ui::PinnedBar>(this, [=] {
		return controller()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, controller()->gifPauseLevelChanged());
	auto pinnedRefreshed = Info::Profile::SharedMediaCountValue(
		_peer,
		MsgId(0), // topicRootId
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
	}) | rpl::distinct_until_changed();
	auto markupRefreshed = HistoryView::PinnedBarItemWithReplyMarkup(
		&session(),
		_pinnedTracker->shownMessageId());
	rpl::combine(
		rpl::duplicate(pinnedRefreshed),
		rpl::duplicate(markupRefreshed)
	) | rpl::start_with_next([=](bool many, HistoryItem *item) {
		refreshPinnedBarButton(many, item);
	}, _pinnedBar->lifetime());

	_pinnedBar->setContent(rpl::combine(
		HistoryView::PinnedBarContent(
			&session(),
			_pinnedTracker->shownMessageId(),
			[bar = _pinnedBar.get()] { bar->customEmojiRepaint(); }),
		std::move(pinnedRefreshed),
		std::move(markupRefreshed)
	) | rpl::map([=](Ui::MessageBarContent &&content, bool, HistoryItem*) {
		const auto id = (!content.title.isEmpty() || !content.text.empty())
			? _pinnedTracker->currentMessageId().message
			: FullMsgId();
		if (const auto list = _list.data()) {
			// Sometimes we get here with non-empty content and id of
			// message that is being deleted right now. We get here in
			// the moment when _itemRemoved was already fired (so in
			// the _list the _pinnedItem is already cleared) and the
			// MessageUpdate::Flag::Destroyed being fired right now,
			// so the message is still in Data::Session. So we need to
			// call data().message() async, otherwise we get a nearly-
			// destroyed message from it and save the pointer in _list.
			crl::on_main(list, [=] {
				list->setShownPinned(session().data().message(id));
			});
		}
		return std::move(content);
	}));

	controller()->adaptive().oneColumnValue(
	) | rpl::start_with_next([=, raw = _pinnedBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
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
			controller()->showPeerHistory(
				item->history()->peer,
				Window::SectionShow::Way::Forward,
				item->id);
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

	if (_showAnimation) {
		_pinnedBar->hide();
	}
}

void HistoryWidget::clearHidingPinnedBar() {
	if (!_hidingPinnedBar) {
		return;
	}
	if (const auto delta = -_pinnedBarHeight) {
		_pinnedBarHeight = 0;
		setGeometryWithTopMoved(geometry(), delta);
	}
	_hidingPinnedBar = nullptr;
}

void HistoryWidget::checkMessagesTTL() {
	if (!_peer || !_peer->messagesTTL()) {
		if (_ttlInfo) {
			_ttlInfo = nullptr;
			updateControlsGeometry();
			updateControlsVisibility();
		}
	} else if (!_ttlInfo || _ttlInfo->peer() != _peer) {
		_ttlInfo = std::make_unique<HistoryView::Controls::TTLButton>(
			this,
			controller()->uiShow(),
			_peer);
		orderWidgets();
		updateControlsGeometry();
		updateControlsVisibility();
	}
}

void HistoryWidget::setChooseReportMessagesDetails(
		Ui::ReportReason reason,
		Fn<void(MessageIdsList)> callback) {
	if (!callback) {
		const auto refresh = _chooseForReport && _chooseForReport->active;
		_chooseForReport = nullptr;
		if (_list) {
			_list->clearChooseReportReason();
		}
		if (refresh) {
			clearSelected();
			updateControlsVisibility();
			updateControlsGeometry();
			updateTopBarChooseForReport();
		}
	} else {
		_chooseForReport = std::make_unique<ChooseMessagesForReport>(
			ChooseMessagesForReport{
				.reason = reason,
				.callback = std::move(callback) });
	}
}

void HistoryWidget::refreshPinnedBarButton(bool many, HistoryItem *item) {
	if (!_pinnedBar) {
		return; // It can be in process of hiding.
	}
	const auto openSection = [=] {
		const auto id = _pinnedTracker
			? _pinnedTracker->currentMessageId()
			: HistoryView::PinnedId();
		if (!id.message) {
			return;
		}
		controller()->showSection(
			std::make_shared<HistoryView::PinnedMemento>(
				_history,
				((!_migrated || peerIsChannel(id.message.peer))
					? id.message.msg
					: (id.message.msg - ServerMaxMsgId))));
	};
	if (const auto replyMarkup = item ? item->inlineReplyMarkup() : nullptr) {
		const auto &rows = replyMarkup->data.rows;
		if ((rows.size() == 1) && (rows.front().size() == 1)) {
			const auto text = rows.front().front().text;
			if (!text.isEmpty()) {
				auto button = object_ptr<Ui::RoundButton>(
					this,
					rpl::single(text),
					st::historyPinnedBotButton);
				button->setTextTransform(
					Ui::RoundButton::TextTransform::NoTransform);
				button->setFullRadius(true);
				button->setClickedCallback([=] {
					Api::ActivateBotCommand(
						_list->prepareClickHandlerContext(item->fullId()),
						0,
						0);
				});
				if (button->width() > st::historyPinnedBotButtonMaxWidth) {
					button->setFullWidth(st::historyPinnedBotButtonMaxWidth);
				}
				struct State {
					base::unique_qptr<Ui::PopupMenu> menu;
				};
				const auto state = button->lifetime().make_state<State>();
				_pinnedBar->contextMenuRequested(
				) | rpl::start_with_next([=, raw = button.data()] {
					state->menu = base::make_unique_q<Ui::PopupMenu>(raw);
					state->menu->addAction(
						tr::lng_settings_events_pinned(tr::now),
						openSection);
					state->menu->popup(QCursor::pos());
				}, button->lifetime());
				_pinnedBar->setRightButton(std::move(button));
				return;
			}
		}
	}
	const auto close = !many;
	auto button = object_ptr<Ui::IconButton>(
		this,
		close ? st::historyReplyCancel : st::historyPinnedShowAll);
	button->clicks(
	) | rpl::start_with_next([=] {
		if (close) {
			hidePinnedMessage();
		} else {
			openSection();
		}
	}, button->lifetime());
	_pinnedBar->setRightButton(std::move(button));
}

void HistoryWidget::setupGroupCallBar() {
	Expects(_history != nullptr);

	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		_groupCallBar = nullptr;
		return;
	}
	_groupCallBar = std::make_unique<Ui::GroupCallBar>(
		this,
		HistoryView::GroupCallBarContentByPeer(
			peer,
			st::historyGroupCallUserpics.size,
			false),
		Core::App().appDeactivatedValue());

	controller()->adaptive().oneColumnValue(
	) | rpl::start_with_next([=](bool one) {
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
		if (peer->groupCall()) {
			controller()->startOrJoinGroupCall(peer, {});
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

	if (_showAnimation) {
		_groupCallBar->hide();
	}
}

void HistoryWidget::setupRequestsBar() {
	Expects(_history != nullptr);

	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		_requestsBar = nullptr;
		return;
	}
	_requestsBar = std::make_unique<Ui::RequestsBar>(
		this,
		HistoryView::RequestsBarContentByPeer(
			peer,
			st::historyRequestsUserpics.size,
			false));

	controller()->adaptive().oneColumnValue(
	) | rpl::start_with_next([=](bool one) {
		_requestsBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _requestsBar->lifetime());

	_requestsBar->barClicks(
	) | rpl::start_with_next([=] {
		RequestsBoxController::Start(controller(), _peer);
	}, _requestsBar->lifetime());

	_requestsBarHeight = 0;
	_requestsBar->heightValue(
	) | rpl::start_with_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _requestsBarHeight);
		_requestsBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _requestsBar->lifetime());

	orderWidgets();

	if (_showAnimation) {
		_requestsBar->hide();
	}
}

void HistoryWidget::requestMessageData(MsgId msgId) {
	if (!_peer) {
		return;
	}
	const auto peer = _peer;
	const auto callback = crl::guard(this, [=] {
		messageDataReceived(peer, msgId);
	});
	session().api().requestMessageData(_peer, msgId, callback);
}

bool HistoryWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		std::optional<MsgId> localId) {
	const auto error = _peer
		? Data::RestrictionError(_peer, ChatRestriction::SendStickers)
		: std::nullopt;
	if (error) {
		controller()->showToast(*error);
		return false;
	} else if (!_peer
		|| !_canSendMessages
		|| showSlowmodeError()
		|| ShowSendPremiumError(controller(), document)) {
		return false;
	}

	Api::SendExistingDocument(
		Api::MessageToSend(prepareSendAction(options)),
		document,
		localId);

	if (_fieldAutocomplete->stickersShown()) {
		clearFieldText();
		//_saveDraftText = true;
		//_saveDraftStart = crl::now();
		//saveDraft();
		saveCloudDraft(); // won't be needed if SendInlineBotResult will clear the cloud draft
	}

	hideSelectorControlsAnimated();

	setInnerFocus();
	return true;
}

bool HistoryWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto error = _peer
		? Data::RestrictionError(_peer, ChatRestriction::SendPhotos)
		: std::nullopt;
	if (error) {
		controller()->showToast(*error);
		return false;
	} else if (!_peer || !_canSendMessages) {
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	Api::SendExistingPhoto(
		Api::MessageToSend(prepareSendAction(options)),
		photo);

	hideSelectorControlsAnimated();

	setInnerFocus();
	return true;
}

void HistoryWidget::showInfoTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	_topToast.show(
		_scroll.data(),
		&session(),
		text,
		std::move(hiddenCallback));
}

void HistoryWidget::showPremiumStickerTooltip(
		not_null<const HistoryView::Element*> view) {
	if (const auto media = view->data()->media()) {
		if (const auto document = media->document()) {
			showPremiumToast(document);
		}
	}
}

void HistoryWidget::showPremiumToast(not_null<DocumentData*> document) {
	if (!_stickerToast) {
		_stickerToast = std::make_unique<HistoryView::StickerToast>(
			controller(),
			this,
			[=] { _stickerToast = nullptr; });
	}
	_stickerToast->showFor(document);
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

	if (_preview) {
		_preview->checkNow(false);
	}
}

void HistoryWidget::clearFieldText(
		TextUpdateEvents events,
		FieldHistoryAction fieldHistoryAction) {
	setFieldText(TextWithTags(), events, fieldHistoryAction);
}

void HistoryWidget::replyToMessage(FullReplyTo id) {
	if (const auto item = session().data().message(id.messageId)) {
		replyToMessage(item, id.quote);
	}
}

void HistoryWidget::replyToMessage(
		not_null<HistoryItem*> item,
		TextWithEntities quote) {
	if (isJoinChannel()) {
		return;
	}
	_processingReplyTo = { .messageId = item->fullId(), .quote = quote};
	_processingReplyItem = item;
	processReply();
}

void HistoryWidget::processReply() {
	const auto processContinue = [=] {
		return crl::guard(_list, [=] {
			if (!_peer || !_processingReplyTo) {
				return;
			} else if (!_processingReplyItem) {
				_processingReplyItem = _peer->owner().message(
					_processingReplyTo.messageId);
				if (!_processingReplyItem) {
					_processingReplyTo = {};
				} else {
					processReply();
				}
			}
		});
	};
	const auto processCancel = [=] {
		_processingReplyTo = {};
		_processingReplyItem = nullptr;
	};

	if (!_peer || !_processingReplyTo) {
		return processCancel();
	} else if (!_processingReplyItem) {
		session().api().requestMessageData(
			session().data().peer(_processingReplyTo.messageId.peer),
			_processingReplyTo.messageId.msg,
			processContinue());
		return;
#if 0 // Now we can "reply" to old legacy group messages.
	} else if (_processingReplyItem->history() == _migrated) {
		if (_processingReplyItem->isService()) {
			controller()->showToast(tr::lng_reply_cant(tr::now));
		} else {
			const auto itemId = _processingReplyItem->fullId();
			controller()->show(
				Ui::MakeConfirmBox({
					.text = tr::lng_reply_cant_forward(),
					.confirmed = crl::guard(this, [=] {
						controller()->content()->setForwardDraft(
							_history,
							{ .ids = { 1, itemId } });
					}),
					.confirmText = tr::lng_selected_forward(),
					}));
		}
		return processCancel();
#endif
	} else if (!_processingReplyItem->isRegular()) {
		return processCancel();
	} else if (const auto forum = _peer->forum()
		; forum && _processingReplyItem->history() == _history) {
		const auto topicRootId = _processingReplyItem->topicRootId();
		if (forum->topicDeleted(topicRootId)) {
			return processCancel();
		} else if (const auto topic = forum->topicFor(topicRootId)) {
			if (!Data::CanSendAnything(topic)) {
				return processCancel();
			}
		} else {
			forum->requestTopic(topicRootId, processContinue());
		}
	} else if (!Data::CanSendAnything(_peer)) {
		return processCancel();
	}
	setReplyFieldsFromProcessing();
}

void HistoryWidget::setReplyFieldsFromProcessing() {
	if (!_processingReplyTo || !_processingReplyItem) {
		return;
	}

	_history->setForwardDraft(MsgId(), {});
	if (_composeSearch) {
		_composeSearch->hideAnimated();
	}

	const auto id = base::take(_processingReplyTo);
	const auto item = base::take(_processingReplyItem);
	if (_editMsgId) {
		if (const auto localDraft = _history->localDraft({})) {
			localDraft->reply = id;
		} else {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				TextWithTags(),
				id,
				MessageCursor(),
				Data::WebPageDraft()));
		}
	} else {
		_replyEditMsg = item;
		_replyTo = id;
		updateReplyEditText(_replyEditMsg);
		updateCanSendMessage();
		updateBotKeyboard();
		updateReplyToName();
		updateControlsVisibility();
		updateControlsGeometry();
		updateField();
		refreshTopBarActiveChat();
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	setInnerFocus();
}

void HistoryWidget::editMessage(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		editMessage(item);
	}
}

void HistoryWidget::editMessage(not_null<HistoryItem*> item) {
	if (_chooseTheme) {
		toggleChooseChatTheme(_peer);
	} else if (_voiceRecordBar->isActive()) {
		controller()->showToast(tr::lng_edit_caption_voice(tr::now));
		return;
	} else if (_composeSearch) {
		_composeSearch->hideAnimated();
	}

	if (isRecording()) {
		// Just fix some strange inconsistency.
		_send->clearState();
	}
	if (!_editMsgId) {
		if (_replyTo || !_field->empty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				_field,
				_replyTo,
				_preview->draft()));
		} else {
			_history->clearLocalDraft({});
		}
	}

	const auto editData = PrepareEditText(item);
	const auto cursor = MessageCursor {
		int(editData.text.size()),
		int(editData.text.size()),
		Ui::kQFixedMax
	};
	const auto previewDraft = Data::WebPageDraft::FromItem(item);
	_history->setLocalEditDraft(std::make_unique<Data::Draft>(
		editData,
		FullReplyTo{ item->fullId() },
		cursor,
		previewDraft));
	applyDraft();

	updateBotKeyboard();

	if (fieldOrDisabledShown()) {
		_fieldBarCancel->show();
	}
	updateFieldPlaceholder();
	updateMouseTracking();
	updateReplyToName();
	updateControlsGeometry();
	updateField();

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	setInnerFocus();
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
			MsgId(0), // topicRootId
			crl::guard(this, callback));
	}
}

bool HistoryWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	return _peer
		&& (replyTo.peer == _peer->id)
		&& _keyboard->forceReply()
		&& _keyboard->forMsgId() == FullMsgId(_peer->id, _history->lastKeyboardId)
		&& _keyboard->forMsgId().msg == replyTo.msg;
}

bool HistoryWidget::lastForceReplyReplied() const {
	return _peer
		&& _keyboard->forceReply()
		&& _keyboard->forMsgId() == replyTo().messageId
		&& (_keyboard->forMsgId()
			== FullMsgId(_peer->id, _history->lastKeyboardId));
}

bool HistoryWidget::cancelReply(bool lastKeyboardUsed) {
	bool wasReply = false;
	if (_replyTo) {
		wasReply = true;

		_processingReplyItem = _replyEditMsg = nullptr;
		_processingReplyTo = _replyTo = FullReplyTo();
		mouseMoveEvent(0);
		if (!readyToForward()
			&& !_previewDrawPreview
			&& !_kbReplyTo) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
		updateBotKeyboard();
		refreshTopBarActiveChat();
		updateCanSendMessage();
		updateControlsVisibility();
		updateControlsGeometry();
		update();
	} else if (const auto localDraft = (_history ? _history->localDraft({}) : nullptr)) {
		if (localDraft->reply) {
			if (localDraft->textWithTags.text.isEmpty()) {
				_history->clearLocalDraft({});
			} else {
				localDraft->reply = {};
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

	_canReplaceMedia = false;
	_photoEditMedia = nullptr;
	updateReplaceMediaButton();
	_replyEditMsg = nullptr;
	setEditMsgId(0);
	_history->clearLocalEditDraft({});
	applyDraft();

	if (_saveEditMsgRequestId) {
		_history->session().api().request(_saveEditMsgRequestId).cancel();
		_saveEditMsgRequestId = 0;
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();

	mouseMoveEvent(nullptr);
	if (!readyToForward()
		&& !_previewDrawPreview
		&& !replyTo()) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}

	auto old = _textUpdateEvents;
	_textUpdateEvents = 0;
	fieldChanged();
	_textUpdateEvents = old;

	updateControlsVisibility();
	updateBotKeyboard();
	updateFieldPlaceholder();

	updateControlsGeometry();
	update();
}

void HistoryWidget::cancelFieldAreaState() {
	controller()->hideLayer();
	if (_previewDrawPreview) {
		_preview->apply({ .removed = true });
	} else if (_editMsgId) {
		cancelEdit();
	} else if (readyToForward()) {
		_history->setForwardDraft(MsgId(), {});
	} else if (_replyTo) {
		cancelReply();
	} else if (_kbReplyTo) {
		toggleKeyboard();
	}
}

void HistoryWidget::fullInfoUpdated() {
	auto refresh = false;
	if (_list) {
		if (updateCanSendMessage()) {
			refresh = true;
		}
		checkFieldAutocomplete();
		_list->updateBotInfo();

		handlePeerUpdate();
		checkSuggestToGigagroup();
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
			session().api().chatParticipants().requestBots(channel);
		}
		if (!channel->mgInfo->adminsLoaded) {
			session().api().chatParticipants().requestAdmins(channel);
		}
	}
	if (!_showAnimation) {
		if (_unblock->isHidden() == isBlocked()
			|| (!isBlocked() && _joinChannel->isHidden() == isJoinChannel())) {
			resize = true;
		}
		if (updateCanSendMessage()) {
			resize = true;
		}
		updateControlsVisibility();
		if (resize) {
			updateControlsGeometry();
		}
	}
}

bool HistoryWidget::updateCanSendMessage() {
	if (!_peer) {
		return false;
	}
	const auto checkTopicFromReplyTo = _replyTo
		&& !_editMsgId
		&& (_replyTo.messageId.peer == _peer->id);
	const auto replyTo = checkTopicFromReplyTo ? _replyEditMsg : 0;
	const auto topic = replyTo ? replyTo->topic() : nullptr;
	const auto allWithoutPolls = Data::AllSendRestrictions()
		& ~ChatRestriction::SendPolls;
	const auto newCanSendMessages = topic
		? Data::CanSendAnyOf(topic, allWithoutPolls)
		: Data::CanSendAnyOf(_peer, allWithoutPolls);
	const auto newCanSendTexts = topic
		? Data::CanSend(topic, ChatRestriction::SendOther)
		: Data::CanSend(_peer, ChatRestriction::SendOther);
	if (_canSendMessages == newCanSendMessages
		&& _canSendTexts == newCanSendTexts) {
		return false;
	}
	_canSendMessages = newCanSendMessages;
	_canSendTexts = newCanSendTexts;
	if (!_canSendMessages) {
		cancelReply();
	}
	refreshScheduledToggle();
	refreshSilentToggle();
	return true;
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
	auto box = Box<DeleteMessagesBox>(&session(), std::move(items));
	box->setDeleteConfirmedCallback(crl::guard(this, [=] {
		clearSelected();
	}));
	controller()->show(std::move(box));
}

void HistoryWidget::escape() {
	if (_composeSearch) {
		_composeSearch->hideAnimated();
	} else if (_chooseForReport) {
		controller()->clearChooseReportMessages();
	} else if (_nonEmptySelection && _list) {
		clearSelected();
	} else if (_isInlineBot) {
		cancelInlineBot();
	} else if (_editMsgId) {
		if (_replyEditMsg
			&& EditTextChanged(_replyEditMsg, _field->getTextWithTags())) {
			controller()->show(Ui::MakeConfirmBox({
				.text = tr::lng_cancel_edit_post_sure(),
				.confirmed = crl::guard(this, [this](Fn<void()> &&close) {
					if (_editMsgId) {
						cancelEdit();
						close();
					}
				}),
				.confirmText = tr::lng_cancel_edit_post_yes(),
				.cancelText = tr::lng_cancel_edit_post_no(),
			}));
		} else {
			cancelEdit();
		}
	} else if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->hideAnimated();
	} else if (_replyTo && _field->getTextWithTags().text.isEmpty()) {
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
	return (genericMsgId < 0 && -genericMsgId < ServerMaxMsgId && _migrated)
		? session().data().message(_migrated->peer, -genericMsgId)
		: _peer
		? session().data().message(_peer, genericMsgId)
		: nullptr;
}

MessageIdsList HistoryWidget::getSelectedItems() const {
	return _list ? _list->getSelectedItems() : MessageIdsList();
}

void HistoryWidget::updateTopBarChooseForReport() {
	if (_chooseForReport && _chooseForReport->active) {
		_topBar->showChooseMessagesForReport(
			_chooseForReport->reason);
	} else {
		_topBar->clearChooseMessagesForReport();
	}
	updateTopBarSelection();
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::updateTopBarSelection() {
	if (!_list) {
		_topBar->showSelected(HistoryView::TopBarWidget::SelectedState {});
		return;
	}

	auto selectedState = _list->getSelectionState();
	_nonEmptySelection = (selectedState.count > 0)
		|| selectedState.textSelected;
	_topBar->showSelected(selectedState);

	if ((selectedState.count > 0) && _composeSearch) {
		_composeSearch->hideAnimated();
	}

	const auto transparent = Qt::WA_TransparentForMouseEvents;
	if (selectedState.count == 0) {
		_reportMessages->clearState();
		_reportMessages->setAttribute(transparent);
		_reportMessages->setColorOverride(st::windowSubTextFg->c);
	} else if (_reportMessages->testAttribute(transparent)) {
		_reportMessages->setAttribute(transparent, false);
		_reportMessages->setColorOverride(std::nullopt);
	}
	_reportMessages->setText(Ui::Text::Upper(selectedState.count
		? tr::lng_report_messages_count(
			tr::now,
			lt_count,
			selectedState.count)
		: tr::lng_report_messages_none(tr::now)));
	updateControlsVisibility();
	updateHistoryGeometry();
	if (!controller()->isLayerShown()
		&& !Core::App().passcodeLocked()) {
		if (isSearching()) {
			_composeSearch->setInnerFocus();
		} else if (_nonEmptySelection
			|| (_list && _list->wasSelectedText())
			|| isRecording()
			|| isBotStart()
			|| isBlocked()
			|| (!_canSendTexts && !_editMsgId)) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
	_topBar->update();
	update();
}

void HistoryWidget::messageDataReceived(
		not_null<PeerData*> peer,
		MsgId msgId) {
	if (!_peer || _peer != peer || !msgId) {
		return;
	} else if (_editMsgId == msgId
		|| (_replyTo.messageId == FullMsgId(peer->id, msgId))) {
		updateReplyEditTexts(true);
		if (_editMsgId == msgId) {
			_preview->setDisabled(_editMsgId
				&& _replyEditMsg
				&& _replyEditMsg->media()
				&& !_replyEditMsg->media()->webpage());
		}
	}
}

void HistoryWidget::updateReplyEditText(not_null<HistoryItem*> item) {
	const auto context = Core::MarkedTextContext{
		.session = &session(),
		.customEmojiRepaint = [=] { updateField(); },
	};
	_replyEditMsgText.setMarkedText(
		st::defaultTextStyle,
		((_editMsgId || _replyTo.quote.empty())
			? item->inReplyText()
			: _replyTo.quote),
		Ui::DialogTextOptions(),
		context);
	if (fieldOrDisabledShown() || isRecording()) {
		_fieldBarCancel->show();
		updateMouseTracking();
	}
}

void HistoryWidget::updateReplyEditTexts(bool force) {
	if (!force) {
		if (_replyEditMsg || (!_editMsgId && !_replyTo)) {
			return;
		}
	}
	if (!_replyEditMsg && _peer) {
		_replyEditMsg = session().data().message(
			_editMsgId ? _peer->id : _replyTo.messageId.peer,
			_editMsgId ? _editMsgId : _replyTo.messageId.msg);
	}
	if (_replyEditMsg) {
		const auto media = _replyEditMsg->media();
		_canReplaceMedia = media && media->allowsEditMedia();
		_photoEditMedia = (_canReplaceMedia
			&& media->photo()
			&& !media->photo()->isNull())
			? media->photo()->createMediaView()
			: nullptr;
		if (_photoEditMedia) {
			_photoEditMedia->wanted(
				Data::PhotoSize::Large,
				_replyEditMsg->fullId());
		}
		if (updateReplaceMediaButton()) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
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
	_forwardPanel->update(_history, _history
		? _history->resolveForwardDraft(MsgId())
		: Data::ResolvedForwardDraft());
	if (readyToForward()) {
		cancelReply();
	}
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::updateReplyToName() {
	if (!_history || _editMsgId) {
		return;
	} else if (!_replyEditMsg && (_replyTo || !_kbReplyTo)) {
		return;
	}
	const auto context = Core::MarkedTextContext{
		.session = &_history->session(),
		.customEmojiRepaint = [] {},
		.customEmojiLoopLimit = 1,
	};
	const auto to = _replyEditMsg ? _replyEditMsg : _kbReplyTo;
	const auto replyToQuote = _replyTo && !_replyTo.quote.empty();
	_replyToName.setMarkedText(
		st::fwdTextStyle,
		HistoryView::Reply::ComposePreviewName(_history, to, replyToQuote),
		Ui::NameTextOptions(),
		context);
}

void HistoryWidget::updateField() {
	if (_repaintFieldScheduled) {
		return;
	}
	_repaintFieldScheduled = true;
	const auto fieldAreaTop = _scroll->y() + _scroll->height();
	rtlupdate(0, fieldAreaTop, width(), height() - fieldAreaTop);
}

void HistoryWidget::drawField(Painter &p, const QRect &rect) {
	_repaintFieldScheduled = false;

	auto backy = _field->y() - st::historySendPadding;
	auto backh = fieldHeight() + 2 * st::historySendPadding;
	auto hasForward = readyToForward();
	auto drawMsgText = (_editMsgId || _replyTo) ? _replyEditMsg : _kbReplyTo;
	if (_editMsgId || _replyTo || (!hasForward && _kbReplyTo)) {
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (hasForward) {
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (_previewDrawPreview) {
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	}
	p.setInactive(
		controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any));
	p.fillRect(myrtlrect(0, backy, width(), backh), st::historyReplyBg);

	const auto media = (!_previewDrawPreview && drawMsgText)
		? drawMsgText->media()
		: nullptr;
	const auto hasPreview = media && media->hasReplyPreview();
	const auto preview = hasPreview ? media->replyPreview() : nullptr;
	const auto spoilered = preview && media->hasSpoiler();
	if (!spoilered) {
		_replySpoiler = nullptr;
	} else if (!_replySpoiler) {
		_replySpoiler = std::make_unique<Ui::SpoilerAnimation>([=] {
			updateField();
		});
	}

	if (_previewDrawPreview) {
		st::historyLinkIcon.paint(
			p,
			st::historyReplyIconPosition + QPoint(0, backy),
			width());
		const auto textTop = backy + st::msgReplyPadding.top();
		auto previewLeft = st::historyReplySkip;

		const auto to = QRect(
			previewLeft,
			backy + (st::historyReplyHeight - st::historyReplyPreview) / 2,
			st::historyReplyPreview,
			st::historyReplyPreview);
		if (_previewDrawPreview(p, to)) {
			previewLeft += st::historyReplyPreview + st::msgReplyBarSkip;
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
	} else if (_editMsgId || _replyTo || (!hasForward && _kbReplyTo)) {
		const auto now = crl::now();
		const auto paused = p.inactive();
		const auto pausedSpoiler = paused || On(PowerSaving::kChatSpoiler);
		auto replyLeft = st::historyReplySkip;
		(_editMsgId
			? st::historyEditIcon
			: (_replyTo && !_replyTo.quote.empty())
			? st::historyQuoteIcon
			: st::historyReplyIcon).paint(
				p,
				st::historyReplyIconPosition + QPoint(0, backy),
				width());
		if (drawMsgText) {
			if (hasPreview) {
				if (preview) {
					const auto overEdit = _photoEditMedia
						? _inPhotoEditOver.value(_inPhotoEdit ? 1. : 0.)
						: 0.;
					auto to = QRect(
						replyLeft,
						backy + (st::historyReplyHeight - st::historyReplyPreview) / 2,
						st::historyReplyPreview,
						st::historyReplyPreview);
					p.drawPixmap(to.x(), to.y(), preview->pixSingle(
						preview->size() / style::DevicePixelRatio(),
						{
							.options = Images::Option::RoundSmall,
							.outer = to.size(),
						}));
					if (_replySpoiler) {
						if (overEdit > 0.) {
							p.setOpacity(1. - overEdit);
						}
						Ui::FillSpoilerRect(
							p,
							to,
							Ui::DefaultImageSpoiler().frame(
								_replySpoiler->index(now, pausedSpoiler)));
					}
					if (overEdit > 0.) {
						p.setOpacity(overEdit);
						p.fillRect(to, st::historyEditMediaBg);
						st::historyEditMedia.paintInCenter(p, to);
						p.setOpacity(1.);
					}
				}
				replyLeft += st::historyReplyPreview + st::msgReplyBarSkip;
			}
			p.setPen(st::historyReplyNameFg);
			if (_editMsgId) {
				paintEditHeader(p, rect, replyLeft, backy);
			} else {
				_replyToName.drawElided(p, replyLeft, backy + st::msgReplyPadding.top(), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			}
			p.setPen(st::historyComposeAreaFg);
			_replyEditMsgText.draw(p, {
				.position = QPoint(
					replyLeft,
					backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height),
				.availableWidth = width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right(),
				.palette = &st::historyComposeAreaPalette,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.now = now,
				.pausedEmoji = paused || On(PowerSaving::kEmojiChat),
				.pausedSpoiler = pausedSpoiler,
				.elisionLines = 1,
			});
		} else {
			p.setFont(st::msgDateFont);
			p.setPen(st::historyComposeAreaFgService);
			p.drawText(replyLeft, backy + (st::historyReplyHeight - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(tr::lng_profile_loading(tr::now), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right()));
		}
	} else if (hasForward) {
		st::historyForwardIcon.paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		const auto x = st::historyReplySkip;
		const auto available = width()
			- x
			- _fieldBarCancel->width()
			- st::msgReplyPadding.right();
		_forwardPanel->paint(p, x, backy, available, width());
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
		editTimeLeftText = u"0:00"_q;
	} else if (editTimeLeft > kDisplayEditTimeWarningMs) {
		updateIn = static_cast<int>(qMin(editTimeLeft - kDisplayEditTimeWarningMs, qint64(kFullDayInMs)));
	} else {
		updateIn = static_cast<int>(editTimeLeft % 1000);
		if (!updateIn) {
			updateIn = 1000;
		}
		++updateIn;

		editTimeLeft = (editTimeLeft - 1) / 1000; // seconds
		editTimeLeftText = u"%1:%2"_q.arg(editTimeLeft / 60).arg(editTimeLeft % 60, 2, 10, QChar('0'));
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

bool HistoryWidget::paintShowAnimationFrame() {
	if (_showAnimation) {
		QPainter p(this);
		_showAnimation->paintContents(p);
		return true;
	}
	return false;
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	if (paintShowAnimationFrame()
		|| controller()->contentOverlapped(this, e)) {
		return;
	}
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	Window::SectionWidget::PaintBackground(
		controller(),
		controller()->currentChatTheme(),
		this,
		e->rect());

	Painter p(this);
	const auto clip = e->rect();
	if (_list) {
		const auto restrictionHidden = fieldOrDisabledShown()
			|| isRecording();
		if (restrictionHidden
			|| replyTo()
			|| readyToForward()
			|| _kbShown) {
			drawField(p, clip);
		}
		const auto error = restrictionHidden
			? std::nullopt
			: writeRestriction();
		if (error) {
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
				- fieldHeight()
				- 2 * st::historySendPadding
				- h
				- st::msgServiceMargin.top()
				- st::msgServiceMargin.bottom()) / 2,
			w,
			h);
		const auto st = controller()->chatStyle();
		HistoryView::ServiceMessagePainter::PaintBubble(p, st, tr);

		p.setPen(st->msgServiceFg());
		p.setFont(st::msgServiceFont->f);
		p.drawTextLeft(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top(), width(), tr::lng_willbe_history(tr::now));

		//AssertIsDebug();
		//Ui::EmptyUserpic::PaintRepliesMessages(p, width() / 4, width() / 4, width(), width() / 2);
	}
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

bool HistoryWidget::touchScroll(const QPoint &delta) {
	const auto scTop = _scroll->scrollTop();
	const auto scMax = _scroll->scrollTopMax();
	const auto scNew = std::clamp(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) {
		return false;
	}
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
		// Saving a draft on account switching.
		saveFieldToHistoryLocalDraft();
		session().api().saveDraftToCloudDelayed(_history);
		setHistory(nullptr);

		session().data().itemVisibilitiesUpdated();
	}
	setTabbedPanel(nullptr);
}
