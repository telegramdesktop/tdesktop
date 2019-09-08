/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_widget.h"

#include "api/api_sending.h"
#include "boxes/confirm_box.h"
#include "boxes/send_files_box.h"
#include "boxes/share_box.h"
#include "boxes/edit_caption_box.h"
#include "core/file_utilities.h"
#include "core/event_filter.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "ui/emoji_config.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/image/image.h"
#include "ui/special_buttons.h"
#include "inline_bots/inline_bot_result.h"
#include "base/unixtime.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_media_types.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_scheduled_messages.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h"
#include "history/history_drag_area.h"
#include "history/history_inner_widget.h"
#include "history/history_item_components.h"
//#include "history/feed/history_feed_section.h" // #feed
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_scheduled_section.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/media/history_view_media.h"
#include "profile/profile_block_group_members.h"
#include "info/info_memento.h"
#include "core/click_handler_types.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/bot_keyboard.h"
#include "chat_helpers/message_field.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "storage/localimageloader.h"
#include "storage/localstorage.h"
#include "storage/file_upload.h"
#include "storage/storage_media_prepare.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "media/player/media_player_instance.h"
#include "core/application.h"
#include "apiwrap.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_contact_status.h"
#include "observer_peer.h"
#include "base/qthelp_regex.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text_options.h"
#include "ui/unread_badge.h"
#include "main/main_session.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_peer_menu.h"
#include "inline_bots/inline_results_widget.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/crash_reports.h"
#include "core/shortcuts.h"
#include "support/support_common.h"
#include "support/support_autocomplete.h"
#include "dialogs/dialogs_key.h"
#include "styles/style_history.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

namespace {

constexpr auto kMessagesPerPageFirst = 30;
constexpr auto kMessagesPerPage = 50;
constexpr auto kPreloadHeightsCount = 3; // when 3 screens to scroll left make a preload request
constexpr auto kTabbedSelectorToggleTooltipTimeoutMs = 3000;
constexpr auto kTabbedSelectorToggleTooltipCount = 3;
constexpr auto kScrollToVoiceAfterScrolledMs = 1000;
constexpr auto kSkipRepaintWhileScrollMs = 100;
constexpr auto kShowMembersDropdownTimeoutMs = 300;
constexpr auto kDisplayEditTimeWarningMs = 300 * 1000;
constexpr auto kFullDayInMs = 86400 * 1000;
constexpr auto kCancelTypingActionTimeout = crl::time(5000);
constexpr auto kSaveDraftTimeout = 1000;
constexpr auto kSaveDraftAnywayTimeout = 5000;
constexpr auto kSaveCloudDraftIdleTimeout = 14000;
constexpr auto kRecordingUpdateDelta = crl::time(100);
constexpr auto kRefreshSlowmodeLabelTimeout = crl::time(200);

ApiWrap::RequestMessageDataCallback replyEditMessageDataCallback() {
	return [](ChannelData *channel, MsgId msgId) {
		if (App::main()) {
			App::main()->messageDataReceived(channel, msgId);
		}
	};
}

void ActivateWindow(not_null<Window::SessionController*> controller) {
	const auto window = controller->window();
	window->activateWindow();
	Core::App().activateWindowDelayed(window);
}

bool ShowHistoryEndInsteadOfUnread(
		not_null<Data::Session*> session,
		PeerId peerId) {
	// Ignore unread messages in case of unread changelogs.
	// We must show this history at end for the changelog to be visible.
	if (peerId != PeerData::kServiceNotificationsId) {
		return false;
	}
	const auto history = session->history(peerId);
	if (!history->unreadCount()) {
		return false;
	}
	const auto last = history->lastMessage();
	return (last != nullptr) && !IsServerMsgId(last->id);
}

object_ptr<Ui::FlatButton> SetupDiscussButton(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) {
	auto result = object_ptr<Ui::FlatButton>(
		parent,
		QString(),
		st::historyComposeButton);
	const auto button = result.data();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		tr::lng_channel_discuss() | Ui::Text::ToUpper(),
		st::historyComposeButtonLabel);
	const auto badge = Ui::CreateChild<Ui::UnreadBadge>(button);
	label->show();

	controller->activeChatValue(
	) | rpl::map([=](Dialogs::Key chat) {
		return chat.history();
	}) | rpl::map([=](History *history) {
		return history ? history->peer->asChannel() : nullptr;
	}) | rpl::map([=](ChannelData *channel) -> rpl::producer<ChannelData*> {
		if (channel && channel->isBroadcast()) {
			return PeerUpdateValue(
				channel,
				Notify::PeerUpdate::Flag::ChannelLinkedChat
			) | rpl::map([=] {
				return channel->linkedChat();
			});
		}
		return rpl::single<ChannelData*>(nullptr);
	}) | rpl::flatten_latest(
	) | rpl::distinct_until_changed(
	) | rpl::map([=](ChannelData *chat)
	-> rpl::producer<std::tuple<int, bool>> {
		if (chat) {
			return PeerUpdateValue(
				chat,
				Notify::PeerUpdate::Flag::UnreadViewChanged
				| Notify::PeerUpdate::Flag::NotificationsEnabled
				| Notify::PeerUpdate::Flag::ChannelAmIn
			) | rpl::map([=] {
				const auto history = chat->amIn()
					? chat->owner().historyLoaded(chat)
					: nullptr;
				return history
					? std::make_tuple(
						history->unreadCountForBadge(),
						!history->mute())
					: std::make_tuple(0, false);
			});
		} else {
			return rpl::single(std::make_tuple(0, false));
		}
	}) | rpl::flatten_latest(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int count, bool active) {
		badge->setText(QString::number(count), active);
		badge->setVisible(count > 0);
	}, badge->lifetime());

	rpl::combine(
		badge->shownValue(),
		badge->widthValue(),
		label->widthValue(),
		button->widthValue()
	) | rpl::start_with_next([=](
			bool badgeShown,
			int badgeWidth,
			int labelWidth,
			int width) {
		const auto textTop = st::historyComposeButton.textTop;
		const auto badgeTop = textTop
			+ st::historyComposeButton.font->height
			- badge->textBaseline();
		const auto add = badgeShown
			? (textTop + badgeWidth)
			: 0;
		const auto total = labelWidth + add;
		label->moveToLeft((width - total) / 2, textTop, width);
		badge->moveToRight((width - total) / 2, textTop, width);
	}, button->lifetime());

	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	badge->setAttribute(Qt::WA_TransparentForMouseEvents);

	return result;
}

void ShowErrorToast(const QString &text) {
	auto config = Ui::Toast::Config();
	config.multiline = true;
	config.minWidth = st::msgMinWidth;
	config.text = text;
	Ui::Toast::Show(config);
}

} // namespace

HistoryWidget::HistoryWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::AbstractSectionWidget(parent, controller)
, _updateEditTimeLeftDisplay([=] { updateField(); })
, _fieldBarCancel(this, st::historyReplyCancel)
, _previewTimer([=] { requestPreview(); })
, _topBar(this, controller)
, _scroll(this, st::historyScroll, false)
, _historyDown(_scroll, st::historyToDown)
, _unreadMentions(_scroll, st::historyUnreadMentions)
, _fieldAutocomplete(this, &session())
, _supportAutocomplete(session().supportMode()
	? object_ptr<Support::Autocomplete>(this, &session())
	: nullptr)
, _send(this)
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
, _discuss(SetupDiscussButton(this, controller))
, _attachToggle(this, st::historyAttach)
, _tabbedSelectorToggle(this, st::historyAttachEmoji)
, _botKeyboardShow(this, st::historyBotKeyboardShow)
, _botKeyboardHide(this, st::historyBotKeyboardHide)
, _botCommandStart(this, st::historyBotCommandStart)
, _field(
	this,
	st::historyComposeField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_message_ph())
, _recordCancelWidth(st::historyRecordFont->width(tr::lng_record_cancel(tr::now)))
, _recordingAnimation([=](crl::time now) {
	return recordingAnimationCallback(now);
})
, _kbScroll(this, st::botKbScroll)
, _attachDragState(DragState::None)
, _attachDragDocument(this)
, _attachDragPhoto(this)
, _sendActionStopTimer([this] { cancelTypingAction(); })
, _topShadow(this) {
	setAcceptDrops(true);

	subscribe(session().downloaderTaskFinished(), [=] { update(); });
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	_historyDown->addClickHandler([=] { historyDownClicked(); });
	_unreadMentions->addClickHandler([=] { showNextUnreadMention(); });
	_fieldBarCancel->addClickHandler([=] { cancelFieldAreaState(); });
	_send->addClickHandler([=] { sendButtonClicked(); });

	SetupSendMenu(
		_send,
		[=] { return sendButtonMenuType(); },
		[=] { sendSilent(); },
		[=] { sendScheduled(); });

	_unblock->addClickHandler([=] { unblockUser(); });
	_botStart->addClickHandler([=] { sendBotStartCommand(); });
	_joinChannel->addClickHandler([=] { joinChannel(); });
	_muteUnmute->addClickHandler([=] { toggleMuteUnmute(); });
	_discuss->addClickHandler([=] { goToDiscussionGroup(); });
	connect(
		_field,
		&Ui::InputField::submitted,
		[=](Qt::KeyboardModifiers modifiers) { sendWithModifiers(modifiers); });
	connect(_field, &Ui::InputField::cancelled, [=] {
		escape();
	});
	connect(_field, SIGNAL(tabbed()), this, SLOT(onFieldTabbed()));
	connect(_field, SIGNAL(resized()), this, SLOT(onFieldResize()));
	connect(_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(_field, SIGNAL(changed()), this, SLOT(onTextChange()));
	connect(App::wnd()->windowHandle(), SIGNAL(visibleChanged(bool)), this, SLOT(onWindowVisibleChanged()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));

	initTabbedSelector();

	connect(Media::Capture::instance(), SIGNAL(error()), this, SLOT(onRecordError()));
	connect(Media::Capture::instance(), SIGNAL(updated(quint16,qint32)), this, SLOT(onRecordUpdate(quint16,qint32)));
	connect(Media::Capture::instance(), SIGNAL(done(QByteArray,VoiceWaveform,qint32)), this, SLOT(onRecordDone(QByteArray,VoiceWaveform,qint32)));

	_attachToggle->addClickHandler(App::LambdaDelayed(
		st::historyAttach.ripple.hideDuration,
		this,
		[=] { chooseAttach(); }));

	_updateHistoryItems.setSingleShot(true);
	connect(&_updateHistoryItems, SIGNAL(timeout()), this, SLOT(onUpdateHistoryItems()));

	_scrollTimer.setSingleShot(false);

	_highlightTimer.setCallback([this] { updateHighlightedMessage(); });

	_membersDropdownShowTimer.setSingleShot(true);
	connect(&_membersDropdownShowTimer, SIGNAL(timeout()), this, SLOT(onMembersDropdownShow()));

	_saveDraftTimer.setSingleShot(true);
	connect(&_saveDraftTimer, SIGNAL(timeout()), this, SLOT(onDraftSave()));
	_saveCloudDraftTimer.setSingleShot(true);
	connect(&_saveCloudDraftTimer, SIGNAL(timeout()), this, SLOT(onCloudDraftSave()));
	_field->scrollTop().changes(
	) | rpl::start_with_next([=] {
		onDraftSaveDelayed();
	}, _field->lifetime());
	connect(_field->rawTextEdit(), SIGNAL(cursorPositionChanged()), this, SLOT(onDraftSaveDelayed()));
	connect(_field->rawTextEdit(), SIGNAL(cursorPositionChanged()), this, SLOT(onCheckFieldAutocomplete()), Qt::QueuedConnection);

	_fieldBarCancel->hide();

	_topBar->hide();
	_scroll->hide();

	_keyboard = _kbScroll->setOwnedWidget(object_ptr<BotKeyboard>(this));
	_kbScroll->hide();

	updateScrollColors();

	_historyDown->installEventFilter(this);
	_unreadMentions->installEventFilter(this);

	InitMessageField(controller, _field);
	_fieldAutocomplete->hide();
	connect(_fieldAutocomplete, SIGNAL(mentionChosen(UserData*,FieldAutocomplete::ChooseMethod)), this, SLOT(onMentionInsert(UserData*)));
	connect(_fieldAutocomplete, SIGNAL(hashtagChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, SIGNAL(botCommandChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, &FieldAutocomplete::stickerChosen, this, [=](not_null<DocumentData*> document) {
		sendExistingDocument(document);
	});
	connect(_fieldAutocomplete, SIGNAL(moderateKeyActivate(int,bool*)), this, SLOT(onModerateKeyActivate(int,bool*)));
	if (_supportAutocomplete) {
		supportInitAutocomplete();
	}
	_fieldLinksParser = std::make_unique<MessageLinksParser>(_field);
	_fieldLinksParser->list().changes(
	) | rpl::start_with_next([=](QStringList &&parsed) {
		_parsedLinks = std::move(parsed);
		checkPreview();
	}, lifetime());
	_field->rawTextEdit()->installEventFilter(_fieldAutocomplete);
	_field->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return canSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(
				data,
				CompressConfirm::Auto,
				data->text());
		}
		Unexpected("action in MimeData hook.");
	});

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
	_discuss->hide();

	_send->setRecordStartCallback([this] { recordStartCallback(); });
	_send->setRecordStopCallback([this](bool active) { recordStopCallback(active); });
	_send->setRecordUpdateCallback([this](QPoint globalPos) { recordUpdateCallback(globalPos); });
	_send->setRecordAnimationCallback([this] { updateField(); });

	_attachToggle->hide();
	_tabbedSelectorToggle->hide();
	_botKeyboardShow->hide();
	_botKeyboardHide->hide();
	_botCommandStart->hide();

	_botKeyboardShow->addClickHandler([=] { toggleKeyboard(); });
	_botKeyboardHide->addClickHandler([=] { toggleKeyboard(); });
	_botCommandStart->addClickHandler([=] { startBotCommand(); });

	_attachDragDocument->hide();
	_attachDragPhoto->hide();

	_topShadow->hide();

	_attachDragDocument->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, CompressConfirm::No);
		ActivateWindow(controller);
	});
	_attachDragPhoto->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, CompressConfirm::Yes);
		ActivateWindow(controller);
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

	session().data().itemViewRefreshRequest(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		// While HistoryInner doesn't own item views we must refresh them
		// even if the list is not yet created / was destroyed.
		if (!_list) {
			item->refreshMainView();
		}
	}, lifetime());

	session().settings().largeEmojiChanges(
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

	subscribe(Media::Player::instance()->switchToNextNotifier(), [this](const Media::Player::Instance::Switch &pair) {
		if (pair.from.type() == AudioMsgId::Type::Voice) {
			scrollToCurrentVoiceMessage(pair.from.contextId(), pair.to);
		}
	});
	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto changes = UpdateFlag::RightsChanged
		| UpdateFlag::UnreadMentionsChanged
		| UpdateFlag::UnreadViewChanged
		| UpdateFlag::MigrationChanged
		| UpdateFlag::UnavailableReasonChanged
		| UpdateFlag::PinnedMessageChanged
		| UpdateFlag::UserIsBlocked
		| UpdateFlag::AdminsChanged
		| UpdateFlag::MembersChanged
		| UpdateFlag::UserOnlineChanged
		| UpdateFlag::NotificationsEnabled
		| UpdateFlag::ChannelAmIn
		| UpdateFlag::ChannelPromotedChanged
		| UpdateFlag::ChannelLinkedChat
		| UpdateFlag::ChannelSlowmode
		| UpdateFlag::ChannelLocalMessages;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(changes, [=](const Notify::PeerUpdate &update) {
		if (update.peer == _peer) {
			if (update.flags & UpdateFlag::RightsChanged) {
				checkPreview();
			}
			if (update.flags & UpdateFlag::UnreadMentionsChanged) {
				updateUnreadMentionsVisibility();
			}
			if (update.flags & UpdateFlag::UnreadViewChanged) {
				unreadCountUpdated();
			}
			if (update.flags & UpdateFlag::MigrationChanged) {
				handlePeerMigration();
			}
			if (update.flags & UpdateFlag::NotificationsEnabled) {
				updateNotifyControls();
			}
			if (update.flags & UpdateFlag::UnavailableReasonChanged) {
				const auto unavailable = _peer->unavailableReason();
				if (!unavailable.isEmpty()) {
					controller->showBackFromStack();
					Ui::show(Box<InformBox>(unavailable));
					return;
				}
			}
			if (update.flags & UpdateFlag::PinnedMessageChanged) {
				if (pinnedMsgVisibilityUpdated()) {
					updateHistoryGeometry();
					updateControlsVisibility();
					updateControlsGeometry();
					this->update();
				}
			}
			if (update.flags & UpdateFlag::ChannelPromotedChanged) {
				refreshAboutProxyPromotion();
				updateHistoryGeometry();
				updateControlsVisibility();
				updateControlsGeometry();
				this->update();
			}
			if (update.flags & (UpdateFlag::ChannelSlowmode
				| UpdateFlag::ChannelLocalMessages)) {
				updateSendButtonType();
			}
			if (update.flags & (UpdateFlag::UserIsBlocked
				| UpdateFlag::AdminsChanged
				| UpdateFlag::MembersChanged
				| UpdateFlag::UserOnlineChanged
				| UpdateFlag::RightsChanged
				| UpdateFlag::ChannelAmIn
				| UpdateFlag::ChannelLinkedChat)) {
				handlePeerUpdate();
			}
		}
	}));

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
		if (action.options.scheduled) {
			crl::on_main(this, [=, history = action.history]{
				controller->showSection(
					HistoryView::ScheduledMemento(history));
			});
		} else {
			fastShowAtEnd(action.history);
			const auto lastKeyboardUsed = lastForceReplyReplied(FullMsgId(
				action.history->channelId(),
				action.replyTo));
			if (cancelReply(lastKeyboardUsed) && !action.clearDraft) {
				onCloudDraftSave();
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

void HistoryWidget::refreshTabbedPanel() {
	if (_peer && controller()->hasTabbedSelectorOwnership()) {
		createTabbedPanel();
	} else {
		setTabbedPanel(nullptr);
	}
}

void HistoryWidget::initTabbedSelector() {
	refreshTabbedPanel();

	_tabbedSelectorToggle->addClickHandler([=] {
		toggleTabbedSelectorMode();
	});

	const auto selector = controller()->tabbedSelector();

	Core::InstallEventFilter(this, selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return false;
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
	}) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		sendExistingDocument(document);
	}, lifetime());

	selector->photoChosen(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=](not_null<PhotoData*> photo) {
		sendExistingPhoto(photo);
	}, lifetime());

	selector->inlineResultChosen(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=](TabbedSelector::InlineChosen data) {
		sendInlineResult(data.result, data.bot);
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
	if (const auto group = session().data().groups().find(view->data())) {
		if (const auto leader = group->items.back()->mainView()) {
			view = leader;
		}
	}
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
	const auto view = item ? item->mainView() : nullptr;
	if (!view) {
		return stopMessageHighlight();
	}
	auto duration = st::activeFadeInDuration + st::activeFadeOutDuration;
	if (crl::now() - _highlightStart > duration) {
		return stopMessageHighlight();
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
		if (const auto leader = group->items.back()->mainView()) {
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
	session().data().stickersUpdated(
	) | rpl::start_with_next([=] {
		updateStickersByEmoji();
	}, lifetime());
	session().data().notifySavedGifsUpdated();
	subscribe(session().api().fullPeerUpdated(), [this](PeerData *peer) {
		fullPeerUpdated(peer);
	});
}

void HistoryWidget::onMentionInsert(UserData *user) {
	QString replacement, entityTag;
	if (user->username.isEmpty()) {
		replacement = user->firstName;
		if (replacement.isEmpty()) {
			replacement = App::peerName(user);
		}
		entityTag = PrepareMentionTag(user);
	} else {
		replacement = '@' + user->username;
	}
	_field->insertTag(replacement, entityTag);
}

void HistoryWidget::onHashtagOrBotCommandInsert(
		QString str,
		FieldAutocomplete::ChooseMethod method) {
	if (!_peer) {
		return;
	}

	// Send bot command at once, if it was not inserted by pressing Tab.
	if (str.at(0) == '/' && method != FieldAutocomplete::ChooseMethod::ByTab) {
		App::sendBotCommand(_peer, nullptr, str, replyToId());
		App::main()->finishForwarding(Api::SendAction(_history));
		setFieldText(_field->getTextWithTagsPart(_field->textCursor().position()));
	} else {
		_field->insertTag(str);
	}
}

void HistoryWidget::updateInlineBotQuery() {
	if (!_history) {
		return;
	}
	const auto query = ParseInlineBotQuery(_field);
	if (_inlineBotUsername != query.username) {
		_inlineBotUsername = query.username;
		if (_inlineBotResolveRequestId) {
//			Notify::inlineBotRequesting(false);
			MTP::cancel(_inlineBotResolveRequestId);
			_inlineBotResolveRequestId = 0;
		}
		if (query.lookingUpBot) {
			_inlineBot = nullptr;
			_inlineLookingUpBot = true;
//			Notify::inlineBotRequesting(true);
			_inlineBotResolveRequestId = MTP::send(
				MTPcontacts_ResolveUsername(MTP_string(_inlineBotUsername)),
				rpcDone(&HistoryWidget::inlineBotResolveDone),
				rpcFail(
					&HistoryWidget::inlineBotResolveFail,
					_inlineBotUsername));
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
					InlineBots::Result *result,
					UserData *bot) {
				sendInlineResult(result, bot);
			});
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
	if (_contactStatus) {
		_contactStatus->raise();
	}
	if (_pinnedBar) {
		_pinnedBar->shadow->raise();
	}
	_topShadow->raise();
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
	_attachDragDocument->raise();
	_attachDragPhoto->raise();
}

void HistoryWidget::updateStickersByEmoji() {
	if (!_history) {
		return;
	}
	const auto emoji = [&] {
		if (!_editMsgId) {
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

void HistoryWidget::onTextChange() {
	InvokeQueued(this, [=] {
		updateInlineBotQuery();
		updateStickersByEmoji();
	});

	if (_history) {
		if (!_inlineBot
			&& !_editMsgId
			&& (_textUpdateEvents & TextUpdateEvent::SendTyping)) {
			updateSendAction(_history, SendAction::Type::Typing);
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

	_saveCloudDraftTimer.stop();
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}

	_saveDraftText = true;
	onDraftSave(true);
}

void HistoryWidget::onDraftSaveDelayed() {
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}
	if (!_field->textCursor().position()
		&& !_field->textCursor().anchor()
		&& !_field->scrollTop().current()) {
		if (!Local::hasDraftCursors(_peer->id)) {
			return;
		}
	}
	onDraftSave(true);
}

void HistoryWidget::onDraftSave(bool delayed) {
	if (!_peer) return;
	if (delayed) {
		auto ms = crl::now();
		if (!_saveDraftStart) {
			_saveDraftStart = ms;
			return _saveDraftTimer.start(kSaveDraftTimeout);
		} else if (ms - _saveDraftStart < kSaveDraftAnywayTimeout) {
			return _saveDraftTimer.start(kSaveDraftTimeout);
		}
	}
	writeDrafts(nullptr, nullptr);
}

void HistoryWidget::saveFieldToHistoryLocalDraft() {
	if (!_history) return;

	if (_editMsgId) {
		_history->setEditDraft(std::make_unique<Data::Draft>(_field, _editMsgId, _previewCancelled, _saveEditMsgRequestId));
	} else {
		if (_replyToId || !_field->empty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(_field, _replyToId, _previewCancelled));
		} else {
			_history->clearLocalDraft();
		}
		_history->clearEditDraft();
	}
}

void HistoryWidget::onCloudDraftSave() {
	if (App::main()) {
		App::main()->saveDraftToCloud();
	}
}

void HistoryWidget::writeDrafts(Data::Draft **localDraft, Data::Draft **editDraft) {
	Data::Draft *historyLocalDraft = _history ? _history->localDraft() : nullptr;
	if (!localDraft && _editMsgId) localDraft = &historyLocalDraft;

	bool save = _peer && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.stop();
	if (_saveDraftText) {
		if (save) {
			Local::MessageDraft storedLocalDraft, storedEditDraft;
			if (localDraft) {
				if (*localDraft) {
					storedLocalDraft = Local::MessageDraft((*localDraft)->msgId, (*localDraft)->textWithTags, (*localDraft)->previewCancelled);
				}
			} else {
				storedLocalDraft = Local::MessageDraft(_replyToId, _field->getTextWithTags(), _previewCancelled);
			}
			if (editDraft) {
				if (*editDraft) {
					storedEditDraft = Local::MessageDraft((*editDraft)->msgId, (*editDraft)->textWithTags, (*editDraft)->previewCancelled);
				}
			} else if (_editMsgId) {
				storedEditDraft = Local::MessageDraft(_editMsgId, _field->getTextWithTags(), _previewCancelled);
			}
			Local::writeDrafts(_peer->id, storedLocalDraft, storedEditDraft);
			if (_migrated) {
				Local::writeDrafts(_migrated->peer->id, Local::MessageDraft(), Local::MessageDraft());
			}
		}
		_saveDraftText = false;
	}
	if (save) {
		MessageCursor localCursor, editCursor;
		if (localDraft) {
			if (*localDraft) {
				localCursor = (*localDraft)->cursor;
			}
		} else {
			localCursor = MessageCursor(_field);
		}
		if (editDraft) {
			if (*editDraft) {
				editCursor = (*editDraft)->cursor;
			}
		} else if (_editMsgId) {
			editCursor = MessageCursor(_field);
		}
		Local::writeDraftCursors(_peer->id, localCursor, editCursor);
		if (_migrated) {
			Local::writeDraftCursors(_migrated->peer->id, MessageCursor(), MessageCursor());
		}
	}

	if (!_editMsgId && !_inlineBot) {
		_saveCloudDraftTimer.start(kSaveCloudDraftIdleTimeout);
	}
}

void HistoryWidget::cancelSendAction(
		not_null<History*> history,
		SendAction::Type type) {
	auto i = _sendActionRequests.find(qMakePair(history, type));
	if (i != _sendActionRequests.cend()) {
		MTP::cancel(i.value());
		_sendActionRequests.erase(i);
	}
}

void HistoryWidget::cancelTypingAction() {
	if (_history) {
		cancelSendAction(_history, SendAction::Type::Typing);
	}
	_sendActionStopTimer.cancel();
}

void HistoryWidget::updateSendAction(
		not_null<History*> history,
		SendAction::Type type,
		int32 progress) {
	const auto peer = history->peer;
	if (peer->isSelf() || (peer->isChannel() && !peer->isMegagroup())) {
		return;
	}

	const auto doing = (progress >= 0);
	if (history->mySendActionUpdated(type, doing)) {
		cancelSendAction(history, type);
		if (doing) {
			using Type = SendAction::Type;
			MTPsendMessageAction action;
			switch (type) {
			case Type::Typing: action = MTP_sendMessageTypingAction(); break;
			case Type::RecordVideo: action = MTP_sendMessageRecordVideoAction(); break;
			case Type::UploadVideo: action = MTP_sendMessageUploadVideoAction(MTP_int(progress)); break;
			case Type::RecordVoice: action = MTP_sendMessageRecordAudioAction(); break;
			case Type::UploadVoice: action = MTP_sendMessageUploadAudioAction(MTP_int(progress)); break;
			case Type::RecordRound: action = MTP_sendMessageRecordRoundAction(); break;
			case Type::UploadRound: action = MTP_sendMessageUploadRoundAction(MTP_int(progress)); break;
			case Type::UploadPhoto: action = MTP_sendMessageUploadPhotoAction(MTP_int(progress)); break;
			case Type::UploadFile: action = MTP_sendMessageUploadDocumentAction(MTP_int(progress)); break;
			case Type::ChooseLocation: action = MTP_sendMessageGeoLocationAction(); break;
			case Type::ChooseContact: action = MTP_sendMessageChooseContactAction(); break;
			case Type::PlayGame: action = MTP_sendMessageGamePlayAction(); break;
			}
			const auto key = qMakePair(history, type);
			const auto requestId = MTP::send(
				MTPmessages_SetTyping(
					peer->input,
					action),
				rpcDone(&HistoryWidget::sendActionDone));
			_sendActionRequests.insert(key, requestId);
			if (type == Type::Typing) {
				_sendActionStopTimer.callOnce(kCancelTypingActionTimeout);
			}
		}
	}
}

void HistoryWidget::sendActionDone(const MTPBool &result, mtpRequestId req) {
	for (auto i = _sendActionRequests.begin(), e = _sendActionRequests.end(); i != e; ++i) {
		if (i.value() == req) {
			_sendActionRequests.erase(i);
			break;
		}
	}
}

void HistoryWidget::activate() {
	if (_history) {
		if (!_historyInited) {
			updateHistoryGeometry(true);
		} else if (hasPendingResizedItems()) {
			updateHistoryGeometry();
		}
	}
	if (App::wnd()) App::wnd()->setInnerFocus();
}

void HistoryWidget::setInnerFocus() {
	if (_scroll->isHidden()) {
		setFocus();
	} else if (_list) {
		if (_nonEmptySelection || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked() || !_canSendMessages) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
}

void HistoryWidget::onRecordError() {
	stopRecording(false);
}

void HistoryWidget::onRecordDone(
		QByteArray result,
		VoiceWaveform waveform,
		qint32 samples) {
	if (!canWriteMessage() || result.isEmpty()) return;

	ActivateWindow(controller());
	const auto duration = samples / Media::Player::kDefaultFrequency;
	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	session().api().sendVoiceMessage(result, waveform, duration, action);
}

void HistoryWidget::onRecordUpdate(quint16 level, qint32 samples) {
	if (!_recording) {
		return;
	}

	_recordingLevel.start(level);
	_recordingAnimation.start();
	_recordingSamples = samples;
	if (samples < 0 || samples >= Media::Player::kDefaultFrequency * AudioVoiceMsgMaxLength) {
		stopRecording(_peer && samples > 0 && _inField);
	}
	Core::App().updateNonIdle();
	updateField();
	if (_history) {
		updateSendAction(_history, SendAction::Type::RecordVoice);
	}
}

void HistoryWidget::notify_botCommandsChanged(UserData *user) {
	if (_peer && (_peer == user || !_peer->isUser())) {
		if (_fieldAutocomplete->clearFilteredBotCommands()) {
			onCheckFieldAutocomplete();
		}
	}
}

void HistoryWidget::notify_inlineBotRequesting(bool requesting) {
	_tabbedSelectorToggle->setLoading(requesting);
}

void HistoryWidget::notify_replyMarkupUpdated(const HistoryItem *item) {
	if (_keyboard->forMsgId() == item->fullId()) {
		updateBotKeyboard(item->history(), true);
	}
}

void HistoryWidget::notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	if (_history == item->history() || _migrated == item->history()) {
		if (int move = _list->moveScrollFollowingInlineKeyboard(item, oldKeyboardTop, newKeyboardTop)) {
			_addToScroll = move;
		}
	}
}

bool HistoryWidget::notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	if (samePeerBot) {
		if (_history) {
			TextWithTags textWithTags = { '@' + samePeerBot->username + ' ' + query, TextWithTags::Tags() };
			MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
			auto replyTo = _history->peer->isUser() ? 0 : samePeerReplyTo;
			_history->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, replyTo, cursor, false));
			applyDraft();
			return true;
		}
	} else if (auto bot = _peer ? _peer->asUser() : nullptr) {
		const auto toPeerId = bot->isBot()
			? bot->botInfo->inlineReturnPeerId
			: PeerId(0);
		if (!toPeerId) {
			return false;
		}
		bot->botInfo->inlineReturnPeerId = 0;
		const auto h = bot->owner().history(toPeerId);
		TextWithTags textWithTags = { '@' + bot->username + ' ' + query, TextWithTags::Tags() };
		MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
		h->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
		if (h == _history) {
			applyDraft();
		} else {
			Ui::showPeerHistory(toPeerId, ShowAtUnreadMsgId);
		}
		return true;
	}
	return false;
}

void HistoryWidget::notify_userIsBotChanged(UserData *user) {
	if (_peer && _peer == user) {
		_list->notifyIsBotChanged();
		_list->updateBotInfo();
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return isActiveWindow() && !Ui::isLayerShown() && inFocusChain();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		if (_history) {
			request->check(Command::Search, 1) && request->handle([=] {
				App::main()->searchInChat(_history);
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
	_historyInited = false;

	if (_history->isReadyFor(_showAtMsgId)) {
		historyLoaded();
	} else {
		firstLoadMessages();
		doneShow();
	}
}

void HistoryWidget::applyDraft(FieldHistoryAction fieldHistoryAction) {
	InvokeQueued(this, [=] { updateStickersByEmoji(); });

	auto draft = _history ? _history->draft() : nullptr;
	auto fieldAvailable = canWriteMessage();
	if (!draft || (!_history->editDraft() && !fieldAvailable)) {
		auto fieldWillBeHiddenAfterEdit = (!fieldAvailable && _editMsgId != 0);
		clearFieldText(0, fieldHistoryAction);
		_field->setFocus();
		_replyEditMsg = nullptr;
		_editMsgId = _replyToId = 0;
		if (fieldWillBeHiddenAfterEdit) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		return;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags, 0, fieldHistoryAction);
	_field->setFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;
	_previewCancelled = draft->previewCancelled;
	_replyEditMsg = nullptr;
	if (auto editDraft = _history->editDraft()) {
		_editMsgId = editDraft->msgId;
		_replyToId = 0;
	} else {
		_editMsgId = 0;
		_replyToId = readyToForward() ? 0 : _history->localDraft()->msgId;
	}
	updateControlsVisibility();
	updateControlsGeometry();

	if (_editMsgId || _replyToId) {
		updateReplyEditTexts();
		if (!_replyEditMsg) {
			session().api().requestMessageData(
				_peer->asChannel(),
				_editMsgId ? _editMsgId : _replyToId,
				replyEditMessageDataCallback());
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

void HistoryWidget::showHistory(
		const PeerId &peerId,
		MsgId showAtMsgId,
		bool reload) {
	MsgId wasMsgId = _showAtMsgId;
	History *wasHistory = _history;

	const auto startBot = (showAtMsgId == ShowAndStartBotMsgId);
	if (startBot) {
		showAtMsgId = ShowAtTheEndMsgId;
	} else if ((showAtMsgId == ShowAtUnreadMsgId)
		&& ShowHistoryEndInsteadOfUnread(&session().data(), peerId)) {
		showAtMsgId = ShowAtTheEndMsgId;
	}

	clearHighlightMessages();
	if (_history) {
		if (_peer->id == peerId && !reload) {
			updateForwarding();

			bool canShowNow = _history->isReadyFor(showAtMsgId);
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
					countHistoryShowFrom();
					destroyUnreadBar();

					auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
					animatedScrollToY(countInitialScrollTop(), item);
				} else {
					historyLoaded();
				}
			}

			_topBar->update();
			update();

			if (const auto user = _peer->asUser()) {
				if (const auto &info = user->botInfo) {
					if (startBot) {
						if (wasHistory) {
							info->inlineReturnPeerId = wasHistory->peer->id;
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
		updateSendAction(_history, SendAction::Type::Typing, -1);
		cancelTypingAction();
	}

	if (!session().settings().autoplayGifs()) {
		session().data().stopAutoplayAnimations();
	}
	clearReplyReturns();
	clearAllLoadRequests();

	if (_history) {
		if (Ui::InFocusChain(_list)) {
			// Removing focus from list clears selected and updates top bar.
			setFocus();
		}
		if (App::main()) {
			App::main()->saveDraftToCloud();
		}
		if (_migrated) {
			_migrated->clearLocalDraft(); // use migrated draft only once
			_migrated->clearEditDraft();
		}

		_history->showAtMsgId = _showAtMsgId;

		destroyUnreadBar();
		destroyPinnedBar();
		_membersDropdown.destroy();
		_scrollToAnimation.stop();
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

	_membersDropdownShowTimer.stop();
	_scroll->takeWidget<HistoryInner>().destroy();

	clearInlineBot();

	_showAtMsgId = showAtMsgId;
	_historyInited = false;

	// Unload lottie animations.
	Auth().data().unloadHeavyViewParts(HistoryInner::ElementDelegate());

	if (peerId) {
		_peer = session().data().peer(peerId);
		_channel = peerToChannel(_peer->id);
		_canSendMessages = _peer->canWrite();
		_contactStatus = std::make_unique<HistoryView::ContactStatus>(
			&controller()->window()->controller(),
			this,
			_peer);
		_contactStatus->heightValue() | rpl::start_with_next([=] {
			updateControlsGeometry();
		}, _contactStatus->lifetime());
		orderWidgets();
		controller()->tabbedSelector()->setCurrentPeer(_peer);
	} else {
		_contactStatus = nullptr;
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
		session().downloader().clearPriorities();

		_history = _peer->owner().history(_peer);
		_migrated = _history->migrateFrom();
		if (_migrated
			&& !_migrated->isEmpty()
			&& (!_history->loadedAtTop() || !_migrated->loadedAtBottom())) {
			_migrated->clear(History::ClearType::Unload);
		}

		_topBar->setActiveChat(
			_history,
			HistoryView::TopBarWidget::Section::History);
		updateTopBarSelection();

		if (_channel) {
			updateNotifyControls();
			session().data().requestNotifySettings(_peer);
			refreshSilentToggle();
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
		_list = _scroll->setOwnedWidget(object_ptr<HistoryInner>(this, controller(), _scroll, _history));
		_list->show();

		_updateHistoryItems.stop();

		pinnedMsgVisibilityUpdated();
		if (_history->scrollTopItem || (_migrated && _migrated->scrollTopItem) || _history->isReadyFor(_showAtMsgId)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}

		handlePeerUpdate();

		Local::readDraftsWithCursors(_history);
		if (_migrated) {
			Local::readDraftsWithCursors(_migrated);
			_migrated->clearEditDraft();
			_history->takeLocalDraft(_migrated);
		}
		applyDraft();
		_send->finishAnimating();

		updateControlsGeometry();

		connect(_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));

		if (const auto user = _peer->asUser()) {
			if (const auto &info = user->botInfo) {
				if (startBot) {
					if (wasHistory) {
						info->inlineReturnPeerId = wasHistory->peer->id;
					}
					sendBotStartCommand();
				}
			}
		}
		if (_history->chatListUnreadMark()) {
			session().api().changeDialogUnreadMark(_history, false);
			if (_migrated) {
				session().api().changeDialogUnreadMark(_migrated, false);
			}

			// Must be done before unreadCountUpdated(), or we auto-close.
			_history->setUnreadMark(false);
			if (_migrated) {
				_migrated->setUnreadMark(false);
			}
		}
		unreadCountUpdated(); // set _historyDown badge.
	} else {
		_topBar->setActiveChat(
			Dialogs::Key(),
			HistoryView::TopBarWidget::Section::History);
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

	crl::on_main(App::wnd(), [] { App::wnd()->setInnerFocus(); });
}

void HistoryWidget::clearDelayedShowAt() {
	_delayedShowAtMsgId = -1;
	if (_delayedShowAtRequest) {
		MTP::cancel(_delayedShowAtRequest);
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::clearAllLoadRequests() {
	clearDelayedShowAt();
	if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
	if (_preloadRequest) MTP::cancel(_preloadRequest);
	if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
	_preloadRequest = _preloadDownRequest = _firstLoadRequest = 0;
}

void HistoryWidget::updateFieldSubmitSettings() {
	const auto settings = _isInlineBot
		? Ui::InputField::SubmitSettings::None
		: session().settings().sendSubmitWay();
	_field->setSubmitSettings(settings);
}

void HistoryWidget::updateNotifyControls() {
	if (!_peer || !_peer->isChannel()) return;

	_muteUnmute->setText((_history->mute()
		? tr::lng_channel_unmute(tr::now)
		: tr::lng_channel_mute(tr::now)).toUpper());
	if (!session().data().notifySilentPostsUnknown(_peer)) {
		if (_silent) {
			_silent->setChecked(session().data().notifySilentPosts(_peer));
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
				HistoryView::ScheduledMemento(_history));
		});
	} else if (_scheduled && !has) {
		_scheduled.destroy();
	}
}

bool HistoryWidget::contentOverlapped(const QRect &globalRect) {
	return (_attachDragDocument->overlaps(globalRect)
			|| _attachDragPhoto->overlaps(globalRect)
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
		if (_tabbedPanel) {
			_tabbedPanel->hideFast();
		}
		hideChildren();
		return;
	}

	if (_pinnedBar) {
		_pinnedBar->cancel->show();
		_pinnedBar->shadow->show();
	}
	if (_firstLoadRequest && !_scroll->isHidden()) {
		_scroll->hide();
	} else if (!_firstLoadRequest && _scroll->isHidden()) {
		_scroll->show();
	}
	if (_contactStatus) {
		_contactStatus->show();
	}
	refreshAboutProxyPromotion();
	if (!editingMessage() && (isBlocked() || isJoinChannel() || isMuteUnmute() || isBotStart())) {
		if (isBlocked()) {
			_joinChannel->hide();
			_muteUnmute->hide();
			_discuss->hide();
			_botStart->hide();
			if (_unblock->isHidden()) {
				_unblock->clearState();
				_unblock->show();
			}
		} else if (isJoinChannel()) {
			_unblock->hide();
			_muteUnmute->hide();
			_discuss->hide();
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
			if (hasDiscussionGroup()) {
				if (_discuss->isHidden()) {
					_discuss->clearState();
					_discuss->show();
				}
			} else {
				_discuss->hide();
			}
		} else if (isBotStart()) {
			_unblock->hide();
			_joinChannel->hide();
			_muteUnmute->hide();
			_discuss->hide();
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
		if (_inlineResults) {
			_inlineResults->hide();
		}
		if (!_field->isHidden()) {
			_field->hide();
			updateControlsGeometry();
			update();
		}
	} else if (editingMessage() || _canSendMessages) {
		onCheckFieldAutocomplete();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_discuss->hide();
		_send->show();
		updateSendButtonType();
		if (_recording) {
			_field->hide();
			_tabbedSelectorToggle->hide();
			_botKeyboardShow->hide();
			_botKeyboardHide->hide();
			_botCommandStart->hide();
			_attachToggle->hide();
			if (_silent) {
				_silent->hide();
			}
			if (_scheduled) {
				_scheduled->hide();
			}
			if (_kbShown) {
				_kbScroll->show();
			} else {
				_kbScroll->hide();
			}
		} else {
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
					if (_cmdStartShown) {
						_botCommandStart->show();
					} else {
						_botCommandStart->hide();
					}
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
		}
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
		_discuss->hide();
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

void HistoryWidget::refreshAboutProxyPromotion() {
	if (_history->useProxyPromotion()) {
		if (!_aboutProxyPromotion) {
			_aboutProxyPromotion = object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				this,
				object_ptr<Ui::FlatLabel>(
					this,
					tr::lng_proxy_sponsor_about(tr::now),
					st::historyAboutProxy),
				st::historyAboutProxyPadding);
		}
		_aboutProxyPromotion->show();
	} else {
		_aboutProxyPromotion.destroy();
	}
}

void HistoryWidget::updateMouseTracking() {
	bool trackMouse = !_fieldBarCancel->isHidden() || _pinnedBar;
	setMouseTracking(trackMouse);
}

void HistoryWidget::destroyUnreadBar() {
	if (_history) _history->destroyUnreadBar();
	if (_migrated) _migrated->destroyUnreadBar();
}

void HistoryWidget::unreadMessageAdded(not_null<HistoryItem*> item) {
	if (_history != item->history()) {
		return;
	}

	// If we get here in non-resized state we can't rely on results of
	// doWeReadServerHistory() and mark chat as read.
	// If we receive N messages being not at bottom:
	// - on first message we set unreadcount += 1, firstUnreadMessage.
	// - on second we get wrong doWeReadServerHistory() and read both.
	session().data().sendHistoryChangeNotifications();

	if (_scroll->scrollTop() + 1 > _scroll->scrollTopMax()) {
		destroyUnreadBar();
	}
	if (!App::wnd()->doWeReadServerHistory()) {
		return;
	}
	if (item->isUnreadMention() && !item->isUnreadMedia()) {
		session().api().markMediaRead(item);
	}
	session().api().readServerHistoryForce(_history);

	// Also clear possible scheduled messages notifications.
	session().notifications().clearFromHistory(_history);
}

void HistoryWidget::historyToDown(History *history) {
	history->forgetScrollState();
	if (auto migrated = history->owner().historyLoaded(history->peer->migrateFrom())) {
		migrated->forgetScrollState();
	}
	if (history == _history) {
		synteticScrollToY(_scroll->scrollTopMax());
	}
}

void HistoryWidget::unreadCountUpdated() {
	if (_history->chatListUnreadMark()) {
		crl::on_main(this, [=, history = _history] {
			if (history == _history) {
				controller()->showBackFromStack();
				emit cancelled();
			}
		});
	} else {
		updateHistoryDownVisibility();
		_historyDown->setUnreadCount(_history->chatListUnreadCount());
	}
}

bool HistoryWidget::messagesFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("CHANNEL_PRIVATE")
		|| error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA")
		|| error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		auto was = _peer;
		controller()->showBackFromStack();
		Ui::show(Box<InformBox>((was && was->isMegagroup()) ? tr::lng_group_not_accessible(tr::now) : tr::lng_channel_not_accessible(tr::now)));
		return true;
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
	return true;
}

void HistoryWidget::messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, mtpRequestId requestId) {
	if (!_history) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	bool toMigrated = (peer == _peer->migrateFrom());
	if (peer != _peer && !toMigrated) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
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
		if (_history->loadedAtBottom() && App::wnd()) App::wnd()->checkHistoryActivation();
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

		_delayedShowAtRequest = 0;
		_history->getReadyFor(_delayedShowAtMsgId);
		if (_history->isEmpty()) {
			if (_preloadRequest) MTP::cancel(_preloadRequest);
			if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
			if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
			_preloadRequest = _preloadDownRequest = 0;
			_firstLoadRequest = -1; // hack - don't updateListSize yet
			addMessagesToFront(peer, *histList);
			_firstLoadRequest = 0;
			if (_history->loadedAtTop()
				&& _history->isEmpty()
				&& count > 0) {
				firstLoadMessages();
				return;
			}
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

		setMsgId(_delayedShowAtMsgId);

		_historyInited = false;
		historyLoaded();
	}
}

void HistoryWidget::historyLoaded() {
	countHistoryShowFrom();
	destroyUnreadBar();
	doneShow();
}

void HistoryWidget::windowShown() {
	updateControlsGeometry();
}

bool HistoryWidget::doWeReadServerHistory() const {
	if (!_history || !_list) return true;
	if (_firstLoadRequest || _a_show.animating()) return false;
	if (_history->loadedAtBottom()) {
		int scrollTop = _scroll->scrollTop();
		if (scrollTop + 1 > _scroll->scrollTopMax()) return true;

		if (const auto unread = firstUnreadMessage()) {
			const auto scrollBottom = scrollTop + _scroll->height();
			if (scrollBottom > _list->itemTop(unread)) {
				return true;
			}
		}
	}
	if (_history->hasNotFreezedUnreadBar()
		|| (_migrated && _migrated->hasNotFreezedUnreadBar())) {
		return true;
	}
	return false;
}

bool HistoryWidget::doWeReadMentions() const {
	if (!_history || !_list) return true;
	if (_firstLoadRequest || _a_show.animating()) return false;
	return true;
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) return;

	auto from = _peer;
	auto offsetId = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated->peer;
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
			from = _migrated->peer;
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

	_firstLoadRequest = MTP::send(
		MTPmessages_GetHistory(
			from->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessages() {
	if (!_history || _preloadRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	auto loadMigrated = _migrated && (_history->isEmpty() || _history->loadedAtTop() || (!_migrated->isEmpty() && !_migrated->loadedAtBottom()));
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

	_preloadRequest = MTP::send(
		MTPmessages_GetHistory(
			from->peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from->peer.get()),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _preloadDownRequest) return;

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

	_preloadDownRequest = MTP::send(
		MTPmessages_GetHistory(
			from->peer->input,
			MTP_int(offsetId + 1),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from->peer.get()),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::delayedShowAt(MsgId showAtMsgId) {
	if (!_history || (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId)) return;

	clearDelayedShowAt();
	_delayedShowAtMsgId = showAtMsgId;

	auto from = _peer;
	auto offsetId = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			from = _migrated->peer;
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
			from = _migrated->peer;
			offset = -loadCount / 2;
			offsetId = -_delayedShowAtMsgId;
		}
	}
	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	_delayedShowAtRequest = MTP::send(
		MTPmessages_GetHistory(
			from->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::onScroll() {
	preloadHistoryIfNeeded();
	visibleAreaUpdated();
	if (!_synteticScrollEvent) {
		_lastUserScrolled = crl::now();
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
		controller()->floatPlayerAreaUpdated().notify(true);

		const auto atBottom = (scrollTop >= _scroll->scrollTopMax());
		if (_history->loadedAtBottom()
			&& atBottom
			&& App::wnd()->doWeReadServerHistory()) {
			// Clear possible scheduled messages notifications.
			session().api().readServerHistory(_history);
			session().notifications().clearFromHistory(_history);
		} else if (_history->loadedAtBottom()
			&& (_history->unreadCount() > 0
				|| (_migrated && _migrated->unreadCount() > 0))) {
			const auto unread = firstUnreadMessage();
			const auto unreadVisible = unread
				&& (scrollBottom > _list->itemTop(unread));
			if (unreadVisible && App::wnd()->doWeReadServerHistory()) {
				session().api().readServerHistory(_history);
			}
		}
	}
}

void HistoryWidget::preloadHistoryIfNeeded() {
	if (_firstLoadRequest || _scroll->isHidden() || !_peer) {
		return;
	}

	updateHistoryDownVisibility();
	if (!_scrollToAnimation.animating()) {
		preloadHistoryByScroll();
		checkReplyReturns();
	}

	auto scrollTop = _scroll->scrollTop();
	if (scrollTop != _lastScrollTop) {
		_lastScrolled = crl::now();
		_lastScrollTop = scrollTop;
	}
}

void HistoryWidget::preloadHistoryByScroll() {
	if (_firstLoadRequest || _scroll->isHidden() || !_peer) {
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
	if (_firstLoadRequest || _scroll->isHidden() || !_peer) {
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

void HistoryWidget::onInlineBotCancel() {
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

void HistoryWidget::onWindowVisibleChanged() {
	QTimer::singleShot(0, this, SLOT(preloadHistoryIfNeeded()));
}

void HistoryWidget::historyDownClicked() {
	if (_replyReturn && _replyReturn->history() == _history) {
		showHistory(_peer->id, _replyReturn->id);
	} else if (_replyReturn && _replyReturn->history() == _migrated) {
		showHistory(_peer->id, -_replyReturn->id);
	} else if (_peer) {
		showHistory(
			_peer->id,
			session().supportMode() ? ShowAtTheEndMsgId : ShowAtUnreadMsgId);
	}
}

void HistoryWidget::showNextUnreadMention() {
	showHistory(_peer->id, _history->getMinLoadedUnreadMention());
}

void HistoryWidget::saveEditMsg() {
	if (_saveEditMsgRequestId) return;

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
	auto left = TextWithEntities { textWithTags.text, ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);

	if (!TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		if (const auto item = session().data().message(_channel, _editMsgId)) {
			const auto suggestModerateActions = false;
			Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
		} else {
			_field->selectAll();
			_field->setFocus();
		}
		return;
	} else if (!left.text.isEmpty()) {
		Ui::show(Box<InformBox>(tr::lng_edit_too_long(tr::now)));
		return;
	}

	auto sendFlags = MTPmessages_EditMessage::Flag::f_message | 0;
	if (webPageId == CancelledWebPageId) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	auto localEntities = TextUtilities::EntitiesToMTP(sending.entities);
	auto sentEntities = TextUtilities::EntitiesToMTP(sending.entities, TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_entities;
	}

	_saveEditMsgRequestId = MTP::send(
		MTPmessages_EditMessage(
			MTP_flags(sendFlags),
			_history->peer->input,
			MTP_int(_editMsgId),
			MTP_string(sending.text),
			MTPInputMedia(),
			MTPReplyMarkup(),
			sentEntities,
			MTP_int(0)), // schedule_date
		rpcDone(&HistoryWidget::saveEditMsgDone, _history),
		rpcFail(&HistoryWidget::saveEditMsgFail, _history));
}

void HistoryWidget::saveEditMsgDone(History *history, const MTPUpdates &updates, mtpRequestId req) {
	session().api().applyUpdates(updates);
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
		cancelEdit();
	}
	if (auto editDraft = history->editDraft()) {
		if (editDraft->saveRequestId == req) {
			history->clearEditDraft();
			if (App::main()) App::main()->writeDrafts(history);
		}
	}
}

bool HistoryWidget::saveEditMsgFail(History *history, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
	}
	if (auto editDraft = history->editDraft()) {
		if (editDraft->saveRequestId == req) {
			editDraft->saveRequestId = 0;
		}
	}

	const auto &err = error.type();
	if (err == qstr("MESSAGE_ID_INVALID") || err == qstr("CHAT_ADMIN_REQUIRED") || err == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
	} else if (err == qstr("MESSAGE_NOT_MODIFIED")) {
		cancelEdit();
	} else if (err == qstr("MESSAGE_EMPTY")) {
		_field->selectAll();
		_field->setFocus();
	} else {
		Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
	}
	update();
	return true;
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
			ShowErrorToast(error);
			return;
		}
	}

	session().api().sendMessage(std::move(message));

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = crl::now();
	onDraftSave();

	hideSelectorControlsAnimated();

	if (_previewData && _previewData->pendingTill) previewCancel();
	_field->setFocus();

	if (!_keyboard->hasMarkup() && _keyboard->forceReply() && !_kbReplyTo) {
		toggleKeyboard();
	}
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
		LayerOption::KeepOther);
}

SendMenuType HistoryWidget::sendMenuType() const {
	return !_peer
		? SendMenuType::Disabled
		: _peer->isSelf()
		? SendMenuType::Reminder
		: SendMenuType::Scheduled;
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

SendMenuType HistoryWidget::sendButtonMenuType() const {
	return (computeSendButtonType() == Ui::SendButton::Type::Send)
		? sendMenuType()
		: SendMenuType::Disabled;
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

void HistoryWidget::goToDiscussionGroup() {
	const auto channel = _peer ? _peer->asChannel() : nullptr;
	const auto chat = channel ? channel->linkedChat() : nullptr;
	if (!chat) {
		return;
	}
	controller()->showPeerHistory(chat, Window::SectionShow::Way::Forward);
}

bool HistoryWidget::hasDiscussionGroup() const {
	const auto channel = _peer ? _peer->asChannel() : nullptr;
	return channel
		&& channel->isBroadcast()
		&& (channel->flags() & MTPDchannel::Flag::f_has_link);
}

void HistoryWidget::onBroadcastSilentChange() {
	updateFieldPlaceholder();
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
	show();
	_topBar->finishAnimating();
	historyDownAnimationFinish();
	unreadMentionsAnimationFinish();
	_topShadow->setVisible(params.withTopBarShadow ? false : true);
	_cacheOver = App::main()->grabForShowAnimation(params);

	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
	}
	hideChildren();
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
		_cacheUnder = _cacheOver = QPixmap();
		doneShow();
	}
}

void HistoryWidget::doneShow() {
	_topBar->setAnimatingMode(false);
	updateBotKeyboard();
	updateControlsVisibility();
	if (!_historyInited) {
		updateHistoryGeometry(true);
	} else if (hasPendingResizedItems()) {
		updateHistoryGeometry();
	}
	preloadHistoryIfNeeded();
	if (App::wnd()) {
		App::wnd()->checkHistoryActivation();
		App::wnd()->setInnerFocus();
	}
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

bool HistoryWidget::recordingAnimationCallback(crl::time now) {
	const auto dt = anim::Disabled()
		? 1.
		: ((now - _recordingAnimation.started())
			/ float64(kRecordingUpdateDelta));
	if (dt >= 1.) {
		_recordingLevel.finish();
	} else {
		_recordingLevel.update(dt, anim::linear);
	}
	if (!anim::Disabled()) {
		update(_attachToggle->geometry());
	}
	return (dt < 1.);
}

void HistoryWidget::chooseAttach() {
	if (!_peer || !_peer->canWrite()) {
		return;
	} else if (const auto error = Data::RestrictionError(
			_peer,
			ChatRestriction::f_send_media)) {
		Ui::Toast::Show(*error);
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	const auto filter = FileDialog::AllFilesFilter()
		+ qsl(";;Image files (*")
		+ cImgExtensions().join(qsl(" *"))
		+ qsl(")");

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
					std::move(result.remoteContent),
					CompressConfirm::Auto);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize);
			if (list.allFilesForCompress || list.albumIsPossible) {
				confirmSendingFiles(std::move(list), CompressConfirm::Auto);
			} else if (!showSendingFilesError(list)) {
				confirmSendingFiles(std::move(list), CompressConfirm::No);
			}
		}
	}), nullptr);
}

void HistoryWidget::sendButtonClicked() {
	const auto type = _send->type();
	if (type == Ui::SendButton::Type::Cancel) {
		onInlineBotCancel();
	} else if (type != Ui::SendButton::Type::Record) {
		send({});
	}
}

void HistoryWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!_history || !_canSendMessages) return;

	_attachDragState = Storage::ComputeMimeDataState(e->mimeData());
	updateDragAreas();

	if (_attachDragState != DragState::None) {
		e->setDropAction(Qt::IgnoreAction);
		e->accept();
	}
}

void HistoryWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_attachDragState != DragState::None || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDragState = DragState::None;
		updateDragAreas();
	}
}

void HistoryWidget::leaveEventHook(QEvent *e) {
	if (_attachDragState != DragState::None || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDragState = DragState::None;
		updateDragAreas();
	}
	if (hasMouseTracking()) {
		mouseMoveEvent(nullptr);
	}
}

void HistoryWidget::mouseMoveEvent(QMouseEvent *e) {
	auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
	updateOverStates(pos);
}

void HistoryWidget::updateOverStates(QPoint pos) {
	auto inField = pos.y() >= (_scroll->y() + _scroll->height()) && pos.y() < height() && pos.x() >= 0 && pos.x() < width();
	auto inReplyEditForward = QRect(st::historyReplySkip, _field->y() - st::historySendPadding - st::historyReplyHeight, width() - st::historyReplySkip - _fieldBarCancel->width(), st::historyReplyHeight).contains(pos) && (_editMsgId || replyToId() || readyToForward());
	auto inPinnedMsg = QRect(0, _topBar->bottomNoMargins(), width(), st::historyReplyHeight).contains(pos) && _pinnedBar;
	auto inClickable = inReplyEditForward || inPinnedMsg;
	if (inField != _inField && _recording) {
		_inField = inField;
		_send->setRecordActive(_inField);
	}
	_inReplyEditForward = inReplyEditForward;
	_inPinnedMsg = inPinnedMsg;
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

void HistoryWidget::recordStartCallback() {
	const auto error = _peer
		? Data::RestrictionError(_peer, ChatRestriction::f_send_media)
		: std::nullopt;
	if (error) {
		Ui::show(Box<InformBox>(*error));
		return;
	} else if (showSlowmodeError()) {
		return;
	} else if (!Media::Capture::instance()->available()) {
		return;
	}

	emit Media::Capture::instance()->start();

	_recording = _inField = true;
	updateControlsVisibility();
	activate();

	updateField();

	_send->setRecordActive(true);
}

void HistoryWidget::recordStopCallback(bool active) {
	stopRecording(_peer && active);
}

void HistoryWidget::recordUpdateCallback(QPoint globalPos) {
	updateOverStates(mapFromGlobal(globalPos));
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_replyForwardPressed) {
		_replyForwardPressed = false;
		update(0, _field->y() - st::historySendPadding - st::historyReplyHeight, width(), st::historyReplyHeight);
	}
	if (_attachDragState != DragState::None || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDragState = DragState::None;
		updateDragAreas();
	}
	if (_recording) {
		stopRecording(_peer && _inField);
	}
}

void HistoryWidget::stopRecording(bool send) {
	emit Media::Capture::instance()->stop(send);

	_recordingLevel = anim::value();
	_recordingAnimation.stop();

	_recording = false;
	_recordingSamples = 0;
	if (_history) {
		updateSendAction(_history, SendAction::Type::RecordVoice, -1);
	}

	updateControlsVisibility();
	activate();

	updateField();
	_send->setRecordActive(false);
}

void HistoryWidget::sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) { // replyTo != 0 from ReplyKeyboardMarkup, == 0 from cmd links
	if (!_peer || _peer != peer) {
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, replyTo));

	QString toSend = cmd;
	if (bot && (!bot->isUser() || !bot->asUser()->isBot())) {
		bot = nullptr;
	}
	QString username = bot ? bot->asUser()->username : QString();
	int32 botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
	if (!replyTo && toSend.indexOf('@') < 2 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
		toSend += '@' + username;
	}

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
			onCloudDraftSave();
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
			onCloudDraftSave();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) toggleKeyboard(false);
			_history->lastKeyboardUsed = true;
		}
	}
}

void HistoryWidget::app_sendBotCallback(
		not_null<const HistoryMessageMarkupButton*> button,
		not_null<const HistoryItem*> msg,
		int row,
		int column) {
	if (msg->id < 0 || _peer != msg->history()->peer) {
		return;
	}

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, msg->id));

	auto bot = msg->getMessageBot();

	using ButtonType = HistoryMessageMarkupButton::Type;
	BotCallbackInfo info = {
		bot,
		msg->fullId(),
		row,
		column,
		(button->type == ButtonType::Game)
	};
	auto flags = MTPmessages_GetBotCallbackAnswer::Flags(0);
	QByteArray sendData;
	if (info.game) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_game;
	} else if (button->type == ButtonType::Callback) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_data;
		sendData = button->data;
	}
	button->requestId = MTP::send(
		MTPmessages_GetBotCallbackAnswer(
			MTP_flags(flags),
			_peer->input,
			MTP_int(msg->id),
			MTP_bytes(sendData)),
		rpcDone(&HistoryWidget::botCallbackDone, info),
		rpcFail(&HistoryWidget::botCallbackFail, info));
	session().data().requestItemRepaint(msg);

	if (_replyToId == msg->id) {
		cancelReply();
	}
	if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
		if (_kbShown) toggleKeyboard(false);
		_history->lastKeyboardUsed = true;
	}
}

void HistoryWidget::botCallbackDone(
		BotCallbackInfo info,
		const MTPmessages_BotCallbackAnswer &answer,
		mtpRequestId req) {
	const auto item = session().data().message(info.msgId);
	if (const auto button = HistoryMessageMarkupButton::Get(info.msgId, info.row, info.col)) {
		if (button->requestId == req) {
			button->requestId = 0;
			session().data().requestItemRepaint(item);
		}
	}
	answer.match([&](const MTPDmessages_botCallbackAnswer &data) {
		if (const auto message = data.vmessage()) {
			if (data.is_alert()) {
				Ui::show(Box<InformBox>(qs(*message)));
			} else {
				Ui::Toast::Show(qs(*message));
			}
		} else if (const auto url = data.vurl()) {
			auto link = qs(*url);
			if (info.game) {
				link = AppendShareGameScoreUrl(&session(), link, info.msgId);
				BotGameUrlClickHandler(info.bot, link).onClick({});
				if (item) {
					updateSendAction(item->history(), SendAction::Type::PlayGame);
				}
			} else {
				UrlClickHandler(link).onClick({});
			}
		}
	});
}

bool HistoryWidget::botCallbackFail(
		BotCallbackInfo info,
		const RPCError &error,
		mtpRequestId req) {
	// show error?
	if (const auto button = HistoryMessageMarkupButton::Get(info.msgId, info.row, info.col)) {
		if (button->requestId == req) {
			button->requestId = 0;
			session().data().requestItemRepaint(session().data().message(info.msgId));
		}
	}
	return true;
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
	if ((obj == _historyDown || obj == _unreadMentions) && e->type() == QEvent::Wheel) {
		return _scroll->viewportEvent(e);
	}
	return TWidget::eventFilter(obj, e);
}

bool HistoryWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect HistoryWidget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

void HistoryWidget::updateDragAreas() {
	_field->setAcceptDrops(_attachDragState == DragState::None);
	updateControlsGeometry();

	switch (_attachDragState) {
	case DragState::None:
		_attachDragDocument->otherLeave();
		_attachDragPhoto->otherLeave();
	break;
	case DragState::Files:
		_attachDragDocument->setText(tr::lng_drag_files_here(tr::now), tr::lng_drag_to_send_files(tr::now));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->hideFast();
	break;
	case DragState::PhotoFiles:
		_attachDragDocument->setText(tr::lng_drag_images_here(tr::now), tr::lng_drag_to_send_no_compression(tr::now));
		_attachDragPhoto->setText(tr::lng_drag_photos_here(tr::now), tr::lng_drag_to_send_quick(tr::now));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->otherEnter();
	break;
	case DragState::Image:
		_attachDragPhoto->setText(tr::lng_drag_images_here(tr::now), tr::lng_drag_to_send_quick(tr::now));
		_attachDragDocument->hideFast();
		_attachDragPhoto->otherEnter();
	break;
	};
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
//	Notify::inlineBotRequesting(false);
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

	const auto query = ParseInlineBotQuery(_field);
	if (_inlineBotUsername == query.username) {
		applyInlineBotQuery(
			query.lookingUpBot ? resolvedBot : query.bot,
			query.query);
	} else {
		clearInlineBot();
	}
}

bool HistoryWidget::inlineBotResolveFail(QString name, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_inlineBotResolveRequestId = 0;
//	Notify::inlineBotRequesting(false);
	if (name == _inlineBotUsername) {
		clearInlineBot();
	}
	return true;
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
	return _peer && _peer->isChannel() && _peer->asChannel()->isBroadcast() && !_peer->asChannel()->canPublish();
}

bool HistoryWidget::showRecordButton() const {
	return Media::Capture::instance()->available() && !HasSendText(_field) && !readyToForward() && !_editMsgId;
}

bool HistoryWidget::showInlineBotCancel() const {
	return _inlineBot && !_inlineLookingUpBot;
}

void HistoryWidget::updateSendButtonType() {
	using Type = Ui::SendButton::Type;

	const auto type = computeSendButtonType();
	_send->setType(type);

	const auto delay = [&] {
		return (type != Type::Cancel && type != Type::Save && _peer)
			? _peer->slowmodeSecondsLeft()
			: 0;
	}();
	_send->setSlowmodeDelay(delay);
	_send->setDisabled(_peer
		&& _peer->slowmodeApplied()
		&& (_history->latestSendingMessage() != nullptr)
		&& (type == Type::Send || type == Type::Record));

	if (delay != 0) {
		App::CallDelayed(
			kRefreshSlowmodeLabelTimeout,
			this,
			[=] { updateSendButtonType(); });
	}
}

bool HistoryWidget::updateCmdStartShown() {
	bool cmdStartShown = false;
	if (_history && _peer && ((_peer->isChat() && _peer->asChat()->botStatus > 0) || (_peer->isMegagroup() && _peer->asChannel()->mgInfo->botStatus > 0) || (_peer->isUser() && _peer->asUser()->isBot()))) {
		if (!isBotStart() && !isBlocked() && !_keyboard->hasMarkup() && !_keyboard->forceReply()) {
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

void HistoryWidget::dropEvent(QDropEvent *e) {
	_attachDragState = DragState::None;
	updateDragAreas();
	e->acceptProposedAction();
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
		_membersDropdownShowTimer.stop();
	}
	if (active && _peer && (_peer->isChat() || _peer->isMegagroup())) {
		if (_membersDropdown) {
			_membersDropdown->otherEnter();
		} else if (!_membersDropdownShowTimer.isActive()) {
			_membersDropdownShowTimer.start(kShowMembersDropdownTimeoutMs);
		}
	} else if (_membersDropdown) {
		_membersDropdown->otherLeave();
	}
}

void HistoryWidget::onMembersDropdownShow() {
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

void HistoryWidget::onModerateKeyActivate(int index, bool *outHandled) {
	*outHandled = _keyboard->isHidden() ? false : _keyboard->moderateKeyActivate(index);
}

void HistoryWidget::pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params) {
	if (!_history || !_tabbedPanel) {
		return;
	} else if (!_canSendMessages) {
		session().settings().setTabbedReplacedWithInfo(true);
		controller()->showPeerInfo(_peer, params.withThirdColumn());
		return;
	}
	session().settings().setTabbedReplacedWithInfo(false);
	controller()->resizeForThirdSection();
	controller()->showSection(
		ChatHelpers::TabbedMemento(),
		params.withThirdColumn());
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

void HistoryWidget::toggleTabbedSelectorMode() {
	if (_tabbedPanel) {
		if (controller()->canShowThirdSection() && !Adaptive::OneColumn()) {
			session().settings().setTabbedSelectorSectionEnabled(true);
			session().saveSettingsDelayed();
			pushTabbedSelectorToThirdSection(
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
// (_botStart|_unblock|_joinChannel|{_muteUnmute&_discuss})

	auto buttonsBottom = bottom - _attachToggle->height();
	auto left = 0;
	_attachToggle->moveToLeft(left, buttonsBottom); left += _attachToggle->width();
	_field->moveToLeft(left, bottom - _field->height() - st::historySendPadding);
	auto right = st::historySendRight;
	_send->moveToRight(right, buttonsBottom); right += _send->width();
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

	if (hasDiscussionGroup()) {
		_muteUnmute->setGeometry(myrtlrect(
			0,
			fullWidthButtonRect.y(),
			width() / 2,
			fullWidthButtonRect.height()));
		_discuss->setGeometry(myrtlrect(
			width() / 2,
			fullWidthButtonRect.y(),
			width() - (width() / 2),
			fullWidthButtonRect.height()));
	} else {
		_muteUnmute->setGeometry(fullWidthButtonRect);
	}

	if (_aboutProxyPromotion) {
		_aboutProxyPromotion->moveToLeft(
			0,
			fullWidthButtonRect.y() - _aboutProxyPromotion->height());
	}
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
	onCheckFieldAutocomplete();
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

void HistoryWidget::onFieldResize() {
	moveFieldControls();
	updateHistoryGeometry();
	updateField();
}

void HistoryWidget::onFieldFocused() {
	if (_list) {
		_list->clearSelected(true);
	}
}

void HistoryWidget::onCheckFieldAutocomplete() {
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
			Local::readRecentHashtagsAndBots();
		} else if (autocomplete.query[0] == '@'
			&& cRecentInlineBots().isEmpty()) {
			Local::readRecentHashtagsAndBots();
		} else if (autocomplete.query[0] == '/'
			&& _peer->isUser()
			&& !_peer->asUser()->isBot()) {
			return;
		}
	}
	_fieldAutocomplete->showFiltered(
		_peer,
		autocomplete.query,
		autocomplete.fromStart);
}

void HistoryWidget::updateFieldPlaceholder() {
	if (_editMsgId) {
		_field->setPlaceholder(tr::lng_edit_message_text());
	} else {
		if (_inlineBot && !_inlineLookingUpBot) {
			_field->setPlaceholder(
				rpl::single(_inlineBot->botInfo->inlinePlaceholder.mid(1)),
				_inlineBot->username.size() + 2);
		} else {
			const auto peer = _history ? _history->peer.get() : nullptr;
			_field->setPlaceholder(
				((peer && peer->isChannel() && !peer->isMegagroup())
					? (session().data().notifySilentPosts(peer)
						? tr::lng_broadcast_silent_ph()
						: tr::lng_broadcast_ph())
					: tr::lng_message_ph()));
		}
	}
	updateSendButtonType();
}

bool HistoryWidget::showSendingFilesError(
		const Storage::PreparedList &list) const {
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
		if (list.files.size() > 1
			&& _peer->slowmodeApplied()
			&& !list.albumIsPossible) {
			return tr::lng_slowmode_no_many(tr::now);
		} else if (const auto left = _peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				formatDurationWords(left));
		}
		using Error = Storage::PreparedList::Error;
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

	ShowErrorToast(text);
	return true;
}

bool HistoryWidget::confirmSendingFiles(const QStringList &files) {
	return confirmSendingFiles(files, CompressConfirm::Auto);
}

bool HistoryWidget::confirmSendingFiles(not_null<const QMimeData*> data) {
	return confirmSendingFiles(data, CompressConfirm::Auto);
}

bool HistoryWidget::confirmSendingFiles(
		const QStringList &files,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	return confirmSendingFiles(
		Storage::PrepareMediaList(files, st::sendMediaPreviewSize),
		compressed,
		insertTextOnCancel);
}

bool HistoryWidget::confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (showSendingFilesError(list)) {
		return false;
	}

	const auto noCompressOption = (list.files.size() > 1)
		&& !list.allFilesForCompress
		&& !list.albumIsPossible;
	const auto boxCompressConfirm = noCompressOption
		? CompressConfirm::None
		: compressed;

	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	const auto text = _field->getTextWithTags();
	using SendLimit = SendFilesBox::SendLimit;
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		text,
		boxCompressConfirm,
		_peer->slowmodeApplied() ? SendLimit::One : SendLimit::Many,
		Api::SendType::Normal,
		sendMenuType());
	_field->setTextWithTags({});
	box->setConfirmedCallback(crl::guard(this, [=](
			Storage::PreparedList &&list,
			SendFilesWay way,
			TextWithTags &&caption,
			Api::SendOptions options,
			bool ctrlShiftEnter) {
		if (showSendingFilesError(list)) {
			return;
		}
		const auto type = (way == SendFilesWay::Files)
			? SendMediaType::File
			: SendMediaType::Photo;
		const auto album = (way == SendFilesWay::Album)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		uploadFilesAfterConfirmation(
			std::move(list),
			type,
			std::move(caption),
			replyToId(),
			options,
			album);
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

	ActivateWindow(controller());
	const auto shown = Ui::show(std::move(box));
	shown->setCloseByOutsideClick(false);

	return true;
}

bool HistoryWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	return confirmSendingFiles(
		std::move(list),
		compressed,
		insertTextOnCancel);
}

bool HistoryWidget::canSendFiles(not_null<const QMimeData*> data) const {
	if (!canWriteMessage()) {
		return false;
	}
	if (const auto urls = data->urls(); !urls.empty()) {
		if (ranges::find_if(
			urls,
			[](const QUrl &url) { return !url.isLocalFile(); }
		) == urls.end()) {
			return true;
		}
	}
	if (data->hasImage()) {
		const auto image = qvariant_cast<QImage>(data->imageData());
		if (!image.isNull()) {
			return true;
		}
	}
	return false;
}

bool HistoryWidget::confirmSendingFiles(
		not_null<const QMimeData*> data,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (!canWriteMessage()) {
		return false;
	}

	const auto hasImage = data->hasImage();

	if (const auto urls = data->urls(); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize);
		if (list.error != Storage::PreparedList::Error::NonLocalUrl) {
			if (list.error == Storage::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				confirmSendingFiles(
					std::move(list),
					compressed,
					emptyTextOnCancel);
				return true;
			}
		}
	}

	if (hasImage) {
		auto image = qvariant_cast<QImage>(data->imageData());
		if (!image.isNull()) {
			confirmSendingFiles(
				std::move(image),
				QByteArray(),
				compressed,
				insertTextOnCancel);
			return true;
		}
	}
	return false;
}

void HistoryWidget::uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		MsgId replyTo,
		Api::SendOptions options,
		std::shared_ptr<SendingAlbum> album) {
	Assert(canWriteMessage());

	const auto isAlbum = (album != nullptr);
	const auto compressImages = (type == SendMediaType::Photo);
	if (_peer->slowmodeApplied()
		&& ((list.files.size() > 1 && !album)
			|| (!list.files.empty()
				&& !caption.text.isEmpty()
				&& !list.canAddCaption(isAlbum, compressImages)))) {
		ShowErrorToast(tr::lng_slowmode_no_many(tr::now));
		return;
	}

	auto action = Api::SendAction(_history);
	action.replyTo = replyTo;
	action.options = options;
	session().api().sendFiles(
		std::move(list),
		type,
		std::move(caption),
		album,
		action);
}

void HistoryWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	if (!canWriteMessage()) return;

	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	session().api().sendFile(fileContent, type, action);
}

void HistoryWidget::subscribeToUploader() {
	if (_uploaderSubscriptions) {
		return;
	}
	using namespace Storage;
	session().uploader().photoReady(
	) | rpl::start_with_next([=](const UploadedPhoto &data) {
		if (data.edit) {
			photoEdited(data.fullId, data.options, data.file);
		} else {
			photoUploaded(data.fullId, data.options, data.file);
		}
	}, _uploaderSubscriptions);
	session().uploader().photoProgress(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		photoProgress(fullId);
	}, _uploaderSubscriptions);
	session().uploader().photoFailed(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		photoFailed(fullId);
	}, _uploaderSubscriptions);
	session().uploader().documentReady(
	) | rpl::start_with_next([=](const UploadedDocument &data) {
		if (data.edit) {
			documentEdited(data.fullId, data.options, data.file);
		} else {
			documentUploaded(data.fullId, data.options, data.file);
		}
	}, _uploaderSubscriptions);
	session().uploader().thumbDocumentReady(
	) | rpl::start_with_next([=](const UploadedThumbDocument &data) {
		thumbDocumentUploaded(
			data.fullId,
			data.options,
			data.file,
			data.thumb,
			data.edit);
	}, _uploaderSubscriptions);
	session().uploader().documentProgress(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		documentProgress(fullId);
	}, _uploaderSubscriptions);
	session().uploader().documentFailed(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		documentFailed(fullId);
	}, _uploaderSubscriptions);
}

void HistoryWidget::sendFileConfirmed(
		const std::shared_ptr<FileLoadResult> &file,
		const std::optional<FullMsgId> &oldId) {
	const auto isEditing = oldId.has_value();
	const auto channelId = peerToChannel(file->to.peer);
	const auto lastKeyboardUsed = lastForceReplyReplied(FullMsgId(
		channelId,
		file->to.replyTo));

	const auto newId = oldId.value_or(
		FullMsgId(channelId, session().data().nextLocalMessageId()));
	auto groupId = file->album ? file->album->groupId : uint64(0);
	if (file->album) {
		const auto proj = [](const SendingAlbum::Item &item) {
			return item.taskId;
		};
		const auto it = ranges::find(file->album->items, file->taskId, proj);
		Assert(it != file->album->items.end());

		it->msgId = newId;
	}
	subscribeToUploader();
	file->edit = isEditing;
	session().uploader().upload(newId, file);

	const auto itemToEdit = isEditing
		? session().data().message(newId)
		: nullptr;

	const auto history = session().data().history(file->to.peer);
	const auto peer = history->peer;

	auto action = Api::SendAction(history);
	action.options = file->to.options;
	action.clearDraft = false;
	action.replyTo = file->to.replyTo;
	action.generateLocal = true;
	session().api().sendAction(action);

	auto caption = TextWithEntities{
		file->caption.text,
		ConvertTextTagsToEntities(file->caption.tags)
	};
	const auto prepareFlags = Ui::ItemTextOptions(
		history,
		session().user()).flags;
	TextUtilities::PrepareForSending(caption, prepareFlags);
	TextUtilities::Trim(caption);
	auto localEntities = TextUtilities::EntitiesToMTP(caption.entities);

	if (itemToEdit) {
		if (const auto id = itemToEdit->groupId()) {
			groupId = id.value;
		}
	}

	auto flags = (isEditing ? MTPDmessage::Flags() : NewMessageFlags(peer))
		| MTPDmessage::Flag::f_entities
		| MTPDmessage::Flag::f_media;
	auto clientFlags = NewMessageClientFlags();
	if (file->to.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
	}
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	const auto silentPost = file->to.options.silent;
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		flags |= MTPDmessage::Flag::f_silent;
	}
	if (groupId) {
		flags |= MTPDmessage::Flag::f_grouped_id;
	}
	if (file->to.options.scheduled) {
		flags |= MTPDmessage::Flag::f_from_scheduled;
	} else {
		clientFlags |= MTPDmessage_ClientFlag::f_local_history_entry;
	}

	const auto messageFromId = channelPost ? 0 : session().userId();
	const auto messagePostAuthor = channelPost
		? App::peerName(session().user())
		: QString();

	if (file->type == SendMediaType::Photo) {
		const auto photoFlags = MTPDmessageMediaPhoto::Flag::f_photo | 0;
		const auto photo = MTP_messageMediaPhoto(
			MTP_flags(photoFlags),
			file->photo,
			MTPint());

		const auto mtpMessage = MTP_message(
			MTP_flags(flags),
			MTP_int(newId.msg),
			MTP_int(messageFromId),
			peerToMTP(file->to.peer),
			MTPMessageFwdHeader(),
			MTPint(),
			MTP_int(file->to.replyTo),
			MTP_int(HistoryItem::NewMessageDate(file->to.options.scheduled)),
			MTP_string(caption.text),
			photo,
			MTPReplyMarkup(),
			localEntities,
			MTP_int(1),
			MTPint(),
			MTP_string(messagePostAuthor),
			MTP_long(groupId),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>());

		if (itemToEdit) {
			itemToEdit->savePreviousMedia();
			itemToEdit->applyEdition(mtpMessage.c_message());
		} else {
			history->addNewMessage(
				mtpMessage,
				clientFlags,
				NewMessageType::Unread);
		}
	} else if (file->type == SendMediaType::File) {
		const auto documentFlags = MTPDmessageMediaDocument::Flag::f_document | 0;
		const auto document = MTP_messageMediaDocument(
			MTP_flags(documentFlags),
			file->document,
			MTPint());

		const auto mtpMessage = MTP_message(
			MTP_flags(flags),
			MTP_int(newId.msg),
			MTP_int(messageFromId),
			peerToMTP(file->to.peer),
			MTPMessageFwdHeader(),
			MTPint(),
			MTP_int(file->to.replyTo),
			MTP_int(HistoryItem::NewMessageDate(file->to.options.scheduled)),
			MTP_string(caption.text),
			document,
			MTPReplyMarkup(),
			localEntities,
			MTP_int(1),
			MTPint(),
			MTP_string(messagePostAuthor),
			MTP_long(groupId),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>());

		if (itemToEdit) {
			itemToEdit->savePreviousMedia();
			itemToEdit->applyEdition(mtpMessage.c_message());
		} else {
			history->addNewMessage(
				mtpMessage,
				clientFlags,
				NewMessageType::Unread);
		}
	} else if (file->type == SendMediaType::Audio) {
		if (!peer->isChannel() || peer->isMegagroup()) {
			flags |= MTPDmessage::Flag::f_media_unread;
		}
		const auto documentFlags = MTPDmessageMediaDocument::Flag::f_document | 0;
		const auto document = MTP_messageMediaDocument(
			MTP_flags(documentFlags),
			file->document,
			MTPint());
		history->addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(newId.msg),
				MTP_int(messageFromId),
				peerToMTP(file->to.peer),
				MTPMessageFwdHeader(),
				MTPint(),
				MTP_int(file->to.replyTo),
				MTP_int(
					HistoryItem::NewMessageDate(file->to.options.scheduled)),
				MTP_string(caption.text),
				document,
				MTPReplyMarkup(),
				localEntities,
				MTP_int(1),
				MTPint(),
				MTP_string(messagePostAuthor),
				MTP_long(groupId),
				//MTPMessageReactions(),
				MTPVector<MTPRestrictionReason>()),
			clientFlags,
			NewMessageType::Unread);
		// Voices can't be edited.
	} else {
		Unexpected("Type in sendFilesConfirmed.");
	}

	if (isEditing) {
		return;
	}

	session().data().sendHistoryChangeNotifications();
	if (_peer && file->to.peer == _peer->id) {
		App::main()->historyToDown(_history);
	}
	App::main()->dialogsToUp();
}

void HistoryWidget::photoUploaded(
		const FullMsgId &newId,
		Api::SendOptions options,
		const MTPInputFile &file) {
	session().api().sendUploadedPhoto(newId, file, options);
}

void HistoryWidget::documentUploaded(
		const FullMsgId &newId,
		Api::SendOptions options,
		const MTPInputFile &file) {
	session().api().sendUploadedDocument(newId, file, std::nullopt, options);
}

void HistoryWidget::documentEdited(
		const FullMsgId &newId,
		Api::SendOptions options,
		const MTPInputFile &file) {
	session().api().editUploadedFile(newId, file, std::nullopt, options, true);
}

void HistoryWidget::photoEdited(
		const FullMsgId &newId,
		Api::SendOptions options,
		const MTPInputFile &file) {
	session().api().editUploadedFile(newId, file, std::nullopt, options, false);
}

void HistoryWidget::thumbDocumentUploaded(
		const FullMsgId &newId,
		Api::SendOptions options,
		const MTPInputFile &file,
		const MTPInputFile &thumb,
		bool edit) {
	if (edit) {
		session().api().editUploadedFile(newId, file, thumb, options, true);
	} else {
		session().api().sendUploadedDocument(newId, file, thumb, options);
	}
}

void HistoryWidget::photoProgress(const FullMsgId &newId) {
	if (const auto item = session().data().message(newId)) {
		const auto photo = item->media()
			? item->media()->photo()
			: nullptr;
		updateSendAction(item->history(), SendAction::Type::UploadPhoto, 0);
		session().data().requestItemRepaint(item);
	}
}

void HistoryWidget::documentProgress(const FullMsgId &newId) {
	if (const auto item = session().data().message(newId)) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		const auto sendAction = (document && document->isVoiceMessage())
			? SendAction::Type::UploadVoice
			: SendAction::Type::UploadFile;
		const auto progress = (document && document->uploading())
			? document->uploadingData->offset
			: 0;

		updateSendAction(
			item->history(),
			sendAction,
			progress);
		session().data().requestItemRepaint(item);
	}
}

void HistoryWidget::photoFailed(const FullMsgId &newId) {
	if (const auto item = session().data().message(newId)) {
		updateSendAction(
			item->history(),
			SendAction::Type::UploadPhoto,
			-1);
		session().data().requestItemRepaint(item);
	}
}

void HistoryWidget::documentFailed(const FullMsgId &newId) {
	if (const auto item = session().data().message(newId)) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		const auto sendAction = (document && document->isVoiceMessage())
			? SendAction::Type::UploadVoice
			: SendAction::Type::UploadFile;
		updateSendAction(item->history(), sendAction, -1);
		session().data().requestItemRepaint(item);
	}
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
			const auto discuss = muteUnmute && hasDiscussionGroup();
			const auto update = false
				|| (_unblock->isHidden() == unblock)
				|| (!unblock && _botStart->isHidden() == botStart)
				|| (!unblock
					&& !botStart
					&& _joinChannel->isHidden() == joinChannel)
				|| (!unblock
					&& !botStart
					&& !joinChannel
					&& (_muteUnmute->isHidden() == muteUnmute
						|| _discuss->isHidden() == discuss));
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
	_updateHistoryItems.start(
		_lastScrolled + kSkipRepaintWhileScrollMs - ms);
	return true;
}

void HistoryWidget::onUpdateHistoryItems() {
	if (!_list) return;

	auto ms = crl::now();
	if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
		_list->update();
	} else {
		_updateHistoryItems.start(_lastScrolled + kSkipRepaintWhileScrollMs - ms);
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

	moveFieldControls();

	const auto pinnedBarTop = _topBar->bottomNoMargins();
	if (_pinnedBar) {
		_pinnedBar->cancel->moveToLeft(width() - _pinnedBar->cancel->width(), pinnedBarTop);
		_pinnedBar->shadow->setGeometryToLeft(0, pinnedBarTop + st::historyReplyHeight, width(), st::lineWidth);
	}
	const auto contactStatusTop = pinnedBarTop + (_pinnedBar ? st::historyReplyHeight : 0);
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

	updateHistoryGeometry(false, false, { ScrollChangeAdd, App::main() ? App::main()->contentScrollAddToY() : 0 });

	updateFieldSize();

	updateHistoryDownPosition();

	if (_membersDropdown) {
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}

	switch (_attachDragState) {
	case DragState::Files:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
	break;
	case DragState::PhotoFiles:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), (height() - st::dragMargin.top() - st::dragMargin.bottom()) / 2);
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
		_attachDragPhoto->resize(_attachDragDocument->width(), _attachDragDocument->height());
		_attachDragPhoto->move(st::dragMargin.left(), height() - _attachDragPhoto->height() - st::dragMargin.bottom());
	break;
	case DragState::Image:
		_attachDragPhoto->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragPhoto->move(st::dragMargin.left(), st::dragMargin.top());
	break;
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
	if (item == _replyEditMsg) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
	while (item == _replyReturn) {
		calcNextReplyReturn();
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		pinnedMsgVisibilityUpdated();
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

void HistoryWidget::itemEdited(HistoryItem *item) {
	if (item == _replyEditMsg) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateScrollColors() {
	_scroll->updateBars();
}

MsgId HistoryWidget::replyToId() const {
	return _replyToId ? _replyToId : (_kbReplyTo ? _kbReplyTo->id : 0);
}

int HistoryWidget::countInitialScrollTop() {
	auto result = ScrollMax;
	if (_history->scrollTopItem || (_migrated && _migrated->scrollTopItem)) {
		result = _list->historyScrollTop();
	} else if (_showAtMsgId && (_showAtMsgId > 0 || -_showAtMsgId < ServerMaxMsgId)) {
		auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
		auto itemTop = _list->itemTop(item);
		if (itemTop < 0) {
			setMsgId(0);
			return countInitialScrollTop();
		} else {
			const auto view = item->mainView();
			Assert(view != nullptr);

			result = itemTopForHighlight(view);
			enqueueMessageHighlight(view);
		}
	} else if (const auto top = unreadBarTop()) {
		result = *top;
	} else {
		return countAutomaticScrollTop();
	}
	return qMin(result, _scroll->scrollTopMax());
}

int HistoryWidget::countAutomaticScrollTop() {
	auto result = ScrollMax;
	if (const auto unread = firstUnreadMessage()) {
		result = _list->itemTop(unread);
		const auto possibleUnreadBarTop = _scroll->scrollTopMax()
			+ HistoryView::UnreadBar::height()
			- HistoryView::UnreadBar::marginTop();
		if (result < possibleUnreadBarTop) {
			const auto history = unread->data()->history();
			history->addUnreadBar();
			if (hasPendingResizedItems()) {
				updateListSize();
			}
			if (history->unreadBar() != nullptr) {
				setMsgId(ShowAtUnreadMsgId);
				result = countInitialScrollTop();
				App::wnd()->checkHistoryActivation();
				if (session().supportMode()) {
					history->unsetFirstUnreadMessage();
				}
				return result;
			}
		}
	}
	return qMin(result, _scroll->scrollTopMax());
}

void HistoryWidget::updateHistoryGeometry(bool initial, bool loadedDown, const ScrollChange &change) {
	if (!_history || (initial && _historyInited) || (!initial && !_historyInited)) {
		return;
	}
	if (_firstLoadRequest || _a_show.animating()) {
		return; // scrollTopMax etc are not working after recountHistoryGeometry()
	}

	auto newScrollHeight = height() - _topBar->height();
	if (_pinnedBar) {
		newScrollHeight -= st::historyReplyHeight;
	}
	if (_contactStatus) {
		newScrollHeight -= _contactStatus->height();
	}
	if (!editingMessage() && (isBlocked() || isBotStart() || isJoinChannel() || isMuteUnmute())) {
		newScrollHeight -= _unblock->height();
		if (_aboutProxyPromotion) {
			_aboutProxyPromotion->resizeToWidth(width());
			newScrollHeight -= _aboutProxyPromotion->height();
		}
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
	auto wasScrollTop = _scroll->scrollTop();
	auto wasScrollTopMax = _scroll->scrollTopMax();
	auto wasAtBottom = wasScrollTop + 1 > wasScrollTopMax;
	auto needResize = (_scroll->width() != width()) || (_scroll->height() != newScrollHeight);
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

		controller()->floatPlayerAreaUpdated().notify(true);
	}

	updateListSize();
	_updateHistoryGeometryRequired = false;

	if ((!initial && !wasAtBottom)
		|| (loadedDown
			&& (!_history->firstUnreadMessage()
				|| _history->unreadBar()
				|| _history->loadedAtBottom())
			&& (!_migrated
				|| !_migrated->firstUnreadMessage()
				|| _migrated->unreadBar()
				|| _history->loadedAtBottom()))) {
		const auto historyScrollTop = _list->historyScrollTop();
		if (!wasAtBottom && historyScrollTop == ScrollMax) {
			// History scroll top was not inited yet.
			// If we're showing locally unread messages, we get here
			// from destroyUnreadBar() before we have time to scroll
			// to good initial position, like top of an unread bar.
			return;
		}
		auto toY = qMin(_list->historyScrollTop(), _scroll->scrollTopMax());
		if (change.type == ScrollChangeAdd) {
			toY += change.value;
		} else if (change.type == ScrollChangeNoJumpToBottom) {
			toY = wasScrollTop;
		} else if (_addToScroll) {
			toY += _addToScroll;
			_addToScroll = 0;
		}
		toY = snap(toY, 0, _scroll->scrollTopMax());
		if (_scroll->scrollTop() == toY) {
			visibleAreaUpdated();
		} else {
			synteticScrollToY(toY);
		}
		return;
	}

	if (initial) {
		_historyInited = true;
		_scrollToAnimation.stop();
	}
	auto newScrollTop = initial
		? countInitialScrollTop()
		: countAutomaticScrollTop();
	if (_scroll->scrollTop() == newScrollTop) {
		visibleAreaUpdated();
	} else {
		synteticScrollToY(newScrollTop);
	}
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
	auto getUnreadBar = [this]() -> HistoryView::Element* {
		if (const auto bar = _migrated ? _migrated->unreadBar() : nullptr) {
			return bar;
		} else if (const auto bar = _history->unreadBar()) {
			return bar;
		}
		return nullptr;
	};
	if (const auto bar = getUnreadBar()) {
		const auto result = _list->itemTop(bar)
			+ HistoryView::UnreadBar::marginTop();
		if (bar->Has<HistoryView::DateBadge>()) {
			return result + bar->Get<HistoryView::DateBadge>()->height();
		}
		return result;
	}
	return std::nullopt;
}

HistoryView::Element *HistoryWidget::firstUnreadMessage() const {
	if (_migrated) {
		if (const auto result = _migrated->firstUnreadMessage()) {
			return result;
		}
	}
	return _history ? _history->firstUnreadMessage() : nullptr;
}

void HistoryWidget::addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceived(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry();
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceivedDown(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry(false, true, { ScrollChangeNoJumpToBottom, 0 });
	}
}

void HistoryWidget::countHistoryShowFrom() {
	if (_migrated
		&& _showAtMsgId == ShowAtUnreadMsgId
		&& _migrated->unreadCount()) {
		_migrated->calculateFirstUnreadMessage();
	}
	if ((_migrated && _migrated->firstUnreadMessage())
		|| (_showAtMsgId != ShowAtUnreadMsgId)
		|| !_history->unreadCount()) {
		_history->unsetFirstUnreadMessage();
	} else {
		_history->calculateFirstUnreadMessage();
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
			_botCommandStart->show();
		}
		_field->setMaxHeight(computeMaxFieldHeight());
		_kbShown = false;
		_kbReplyTo = nullptr;
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId && !_editMsgId) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
	}
	updateControlsGeometry();
	update();
}

int HistoryWidget::computeMaxFieldHeight() const {
	const auto available = height()
		- _topBar->height()
		- (_contactStatus ? _contactStatus->height() : 0)
		- (_pinnedBar ? st::historyReplyHeight : 0)
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

	auto haveUnreadBelowBottom = [&](History *history) {
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
	auto historyDownIsVisible = [&] {
		if (!_list || _firstLoadRequest) {
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
	auto unreadMentionsIsVisible = [this, showUnreadMentions] {
		if (!showUnreadMentions || _firstLoadRequest) {
			return false;
		}
		return (_history->getUnreadMentionsLoadedCount() > 0);
	};
	auto unreadMentionsIsShown = unreadMentionsIsVisible();
	if (unreadMentionsIsShown) {
		_unreadMentions->setUnreadCount(_history->getUnreadMentionsCount());
	}
	if (_unreadMentionsIsShown != unreadMentionsIsShown) {
		_unreadMentionsIsShown = unreadMentionsIsShown;
		_unreadMentionsShown.start([=] { updateUnreadMentionsPosition(); }, _unreadMentionsIsShown ? 0. : 1., _unreadMentionsIsShown ? 1. : 0., st::historyToDownDuration);
	}
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	_replyForwardPressed = QRect(0, _field->y() - st::historySendPadding - st::historyReplyHeight, st::historyReplySkip, st::historyReplyHeight).contains(e->pos());
	if (_replyForwardPressed && !_fieldBarCancel->isHidden()) {
		updateField();
	} else if (_inReplyEditForward) {
		if (readyToForward()) {
			const auto items = std::move(_toForward);
			App::main()->cancelForwarding(_history);
			auto list = ranges::view::all(
				items
			) | ranges::view::transform(
				&HistoryItem::fullId
			) | ranges::to_vector;
			Window::ShowForwardMessagesBox(controller(), std::move(list));
		} else {
			Ui::showPeerHistory(_peer, _editMsgId ? _editMsgId : replyToId());
		}
	} else if (_inPinnedMsg) {
		Assert(_pinnedBar != nullptr);
		Ui::showPeerHistory(_peer, _pinnedBar->msgId);
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		controller()->showBackFromStack();
		emit cancelled();
	} else if (e->key() == Qt::Key_PageDown) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_PageUp) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Down) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll->keyPressEvent(e);
		} else if ((e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier)) == Qt::ControlModifier) {
			replyToNextMessage();
		}
	} else if (e->key() == Qt::Key_Up) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
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
		} else if ((e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier)) == Qt::ControlModifier) {
			replyToPreviousMessage();
		}
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!_botStart->isHidden()) {
			sendBotStartCommand();
		}
		if (!_canSendMessages) {
			const auto submitting = Ui::InputField::ShouldSubmit(
				session().settings().sendSubmitWay(),
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

void HistoryWidget::replyToPreviousMessage() {
	if (!_history || _editMsgId) {
		return;
	}
	const auto fullId = FullMsgId(
		_history->channelId(),
		_replyToId);
	if (const auto item = session().data().message(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto previousView = view->previousInBlocks()) {
				const auto previous = previousView->data();
				Ui::showPeerHistoryAtItem(previous);
				replyToMessage(previous);
			}
		}
	} else if (const auto previous = _history->lastMessage()) {
		Ui::showPeerHistoryAtItem(previous);
		replyToMessage(previous);
	}
}

void HistoryWidget::replyToNextMessage() {
	if (!_history || _editMsgId) {
		return;
	}
	const auto fullId = FullMsgId(
		_history->channelId(),
		_replyToId);
	if (const auto item = session().data().message(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto nextView = view->nextInBlocks()) {
				const auto next = nextView->data();
				Ui::showPeerHistoryAtItem(next);
				replyToMessage(next);
			} else {
				clearHighlightMessages();
				cancelReply(false);
			}
		}
	}
}

bool HistoryWidget::showSlowmodeError() {
	const auto text = [&] {
		if (const auto left = _peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				formatDurationWords(left));
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
	ShowErrorToast(text);
	return true;
}

void HistoryWidget::onFieldTabbed() {
	if (_supportAutocomplete) {
		_supportAutocomplete->activate(_field.data());
	} else if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
	}
}

void HistoryWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	if (!_peer || !_peer->canWrite()) {
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		Ui::show(Box<InformBox>(errorText));
		return;
	}

	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	action.generateLocal = true;
	session().api().sendInlineResult(bot, result, action);

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = crl::now();
	onDraftSave();

	auto &bots = cRefRecentInlineBots();
	const auto index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		Local::writeRecentHashtagsAndBots();
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
}

HistoryWidget::PinnedBar::PinnedBar(MsgId msgId, HistoryWidget *parent)
: msgId(msgId)
, cancel(parent, st::historyReplyCancel)
, shadow(parent) {
}

HistoryWidget::PinnedBar::~PinnedBar() {
	cancel.destroyDelayed();
	shadow.destroyDelayed();
}

void HistoryWidget::updatePinnedBar(bool force) {
	update();
	if (!_pinnedBar) {
		return;
	}
	if (!force) {
		if (_pinnedBar->msg) {
			return;
		}
	}

	Assert(_history != nullptr);
	if (!_pinnedBar->msg) {
		_pinnedBar->msg = session().data().message(_history->channelId(), _pinnedBar->msgId);
	}
	if (_pinnedBar->msg) {
		_pinnedBar->text.setText(
			st::messageTextStyle,
			_pinnedBar->msg->inReplyText(),
			Ui::DialogTextOptions());
		update();
	} else if (force) {
		if (auto channel = _peer ? _peer->asChannel() : nullptr) {
			channel->clearPinnedMessage();
		}
		destroyPinnedBar();
		updateControlsGeometry();
	}
}

bool HistoryWidget::pinnedMsgVisibilityUpdated() {
	auto result = false;
	auto pinnedId = _peer->pinnedMessageId();
	if (pinnedId && !_peer->canPinMessages()) {
		auto it = Global::HiddenPinnedMessages().constFind(_peer->id);
		if (it != Global::HiddenPinnedMessages().cend()) {
			if (it.value() == pinnedId) {
				pinnedId = 0;
			} else {
				Global::RefHiddenPinnedMessages().remove(_peer->id);
				Local::writeUserSettings();
			}
		}
	}
	if (pinnedId) {
		if (!_pinnedBar) {
			_pinnedBar = std::make_unique<PinnedBar>(pinnedId, this);
			if (_a_show.animating()) {
				_pinnedBar->cancel->hide();
				_pinnedBar->shadow->hide();
			} else {
				_pinnedBar->cancel->show();
				_pinnedBar->shadow->show();
			}
			_pinnedBar->cancel->addClickHandler([=] {
				hidePinnedMessage();
			});
			orderWidgets();

			updatePinnedBar();
			result = true;

			const auto barTop = unreadBarTop();
			if (!barTop || _scroll->scrollTop() != *barTop) {
				synteticScrollToY(_scroll->scrollTop() + st::historyReplyHeight);
			}
		} else if (_pinnedBar->msgId != pinnedId) {
			_pinnedBar->msgId = pinnedId;
			_pinnedBar->msg = nullptr;
			_pinnedBar->text.clear();
			updatePinnedBar();
		}
		if (!_pinnedBar->msg) {
			session().api().requestMessageData(
				_peer->asChannel(),
				_pinnedBar->msgId,
				replyEditMessageDataCallback());
		}
	} else if (_pinnedBar) {
		destroyPinnedBar();
		result = true;
		const auto barTop = unreadBarTop();
		if (!barTop || _scroll->scrollTop() != *barTop) {
			synteticScrollToY(_scroll->scrollTop() - st::historyReplyHeight);
		}
		updateControlsGeometry();
	}
	return result;
}

void HistoryWidget::destroyPinnedBar() {
	_pinnedBar.reset();
	_inPinnedMsg = false;
}

bool HistoryWidget::sendExistingDocument(not_null<DocumentData*> document) {
	const auto error = _peer
		? Data::RestrictionError(_peer, ChatRestriction::f_send_stickers)
		: std::nullopt;
	if (error) {
		Ui::show(Box<InformBox>(*error), LayerOption::KeepOther);
		return false;
	} else if (!_peer || !_peer->canWrite()) {
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	auto message = Api::MessageToSend(_history);
	message.action.replyTo = replyToId();
	Api::SendExistingDocument(std::move(message), document);

	if (_fieldAutocomplete->stickersShown()) {
		clearFieldText();
		//_saveDraftText = true;
		//_saveDraftStart = crl::now();
		//onDraftSave();
		onCloudDraftSave(); // won't be needed if SendInlineBotResult will clear the cloud draft
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
	return true;
}

bool HistoryWidget::sendExistingPhoto(not_null<PhotoData*> photo) {
	const auto error = _peer
		? Data::RestrictionError(_peer, ChatRestriction::f_send_media)
		: std::nullopt;
	if (error) {
		Ui::show(Box<InformBox>(*error), LayerOption::KeepOther);
		return false;
	} else if (!_peer || !_peer->canWrite()) {
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	auto message = Api::MessageToSend(_history);
	message.action.replyTo = replyToId();
	Api::SendExistingPhoto(std::move(message), photo);

	hideSelectorControlsAnimated();

	_field->setFocus();
	return true;
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
				App::main()->setForwardDraft(
					_peer->id,
					{ 1, itemId });
			})));
		}
		return;
	}

	App::main()->cancelForwarding(_history);

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
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	onDraftSave();

	_field->setFocus();
}

void HistoryWidget::editMessage(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		editMessage(item);
	}
}

void HistoryWidget::editMessage(not_null<HistoryItem*> item) {
	if (const auto media = item->media()) {
		if (media->allowsEditCaption()) {
			Ui::show(Box<EditCaptionBox>(controller(), item));
			return;
		}
	}

	if (_recording) {
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
	_history->setEditDraft(std::make_unique<Data::Draft>(
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
	onDraftSave();

	_field->setFocus();
}

void HistoryWidget::pinMessage(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (item->canPin()) {
			Ui::show(Box<PinMessageBox>(item->history()->peer, item->id));
		}
	}
}

void HistoryWidget::unpinMessage(FullMsgId itemId) {
	const auto peer = _peer;
	if (!peer) {
		return;
	}

	Ui::show(Box<ConfirmBox>(tr::lng_pinned_unpin_sure(tr::now), tr::lng_pinned_unpin(tr::now), crl::guard(this, [=] {
		peer->clearPinnedMessage();

		Ui::hideLayer();
		MTP::send(
			MTPmessages_UpdatePinnedMessage(
				MTP_flags(0),
				peer->input,
				MTP_int(0)),
			rpcDone(&HistoryWidget::unpinDone));
	})));
}

void HistoryWidget::unpinDone(const MTPUpdates &updates) {
	session().api().applyUpdates(updates);
}

void HistoryWidget::hidePinnedMessage() {
	const auto pinnedId = _peer ? _peer->pinnedMessageId() : MsgId(0);
	if (!pinnedId) {
		if (pinnedMsgVisibilityUpdated()) {
			updateControlsGeometry();
			update();
		}
		return;
	}

	if (_peer->canPinMessages()) {
		unpinMessage(FullMsgId(
			_peer->isChannel() ? peerToChannel(_peer->id) : NoChannel,
			pinnedId));
	} else {
		Global::RefHiddenPinnedMessages().insert(_peer->id, pinnedId);
		Local::writeUserSettings();
		if (pinnedMsgVisibilityUpdated()) {
			updateControlsGeometry();
			update();
		}
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
		onDraftSave();
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
		onCloudDraftSave();
	}
}

int HistoryWidget::countMembersDropdownHeightMax() const {
	int result = height() - st::membersInnerDropdown.padding.top() - st::membersInnerDropdown.padding.bottom();
	result -= _tabbedSelectorToggle->height();
	accumulate_min(result, st::membersInnerHeightMax);
	return result;
}

void HistoryWidget::cancelEdit() {
	if (!_editMsgId) return;

	_replyEditMsg = nullptr;
	_editMsgId = 0;
	_history->clearEditDraft();
	applyDraft();

	if (_saveEditMsgRequestId) {
		MTP::cancel(_saveEditMsgRequestId);
		_saveEditMsgRequestId = 0;
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	onDraftSave();

	mouseMoveEvent(nullptr);
	if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !replyToId()) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}

	auto old = _textUpdateEvents;
	_textUpdateEvents = 0;
	onTextChange();
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
		onDraftSave();
	} else if (_editMsgId) {
		cancelEdit();
	} else if (readyToForward()) {
		App::main()->cancelForwarding(_history);
	} else if (_replyToId) {
		cancelReply();
	} else if (_kbReplyTo) {
		toggleKeyboard();
	}
}

void HistoryWidget::previewCancel() {
	MTP::cancel(base::take(_previewRequest));
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
	const auto newLinks = _parsedLinks.join(' ');
	if (_previewLinks != newLinks) {
		MTP::cancel(base::take(_previewRequest));
		_previewLinks = newLinks;
		if (_previewLinks.isEmpty()) {
			if (_previewData && _previewData->pendingTill >= 0) {
				previewCancel();
			}
		} else {
			const auto i = _previewCache.constFind(_previewLinks);
			if (i == _previewCache.cend()) {
				_previewRequest = MTP::send(
					MTPmessages_GetWebPagePreview(
						MTP_flags(0),
						MTP_string(_previewLinks),
						MTPVector<MTPMessageEntity>()),
					rpcDone(&HistoryWidget::gotPreview, _previewLinks));
			} else if (i.value()) {
				_previewData = session().data().webpage(i.value());
				updatePreview();
			} else {
				if (_previewData && _previewData->pendingTill >= 0) previewCancel();
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
	_previewRequest = MTP::send(
		MTPmessages_GetWebPagePreview(
			MTP_flags(0),
			MTP_string(_previewLinks),
			MTPVector<MTPMessageEntity>()),
		rpcDone(&HistoryWidget::gotPreview, _previewLinks));
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
			QString title, desc;
			if (_previewData->siteName.isEmpty()) {
				if (_previewData->title.isEmpty()) {
					if (_previewData->description.text.isEmpty()) {
						title = _previewData->author;
						desc = ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url);
					} else {
						title = _previewData->description.text;
						desc = _previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url) : _previewData->author;
					}
				} else {
					title = _previewData->title;
					desc = _previewData->description.text.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url) : _previewData->author) : _previewData->description.text;
				}
			} else {
				title = _previewData->siteName;
				desc = _previewData->title.isEmpty() ? (_previewData->description.text.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url) : _previewData->author) : _previewData->description.text) : _previewData->title;
			}
			if (title.isEmpty()) {
				if (_previewData->document) {
					title = tr::lng_attach_file(tr::now);
				} else if (_previewData->photo) {
					title = tr::lng_attach_photo(tr::now);
				}
			}
			_previewTitle.setText(
				st::msgNameStyle,
				title,
				Ui::NameTextOptions());
			_previewDescription.setText(
				st::messageTextStyle,
				TextUtilities::Clean(desc),
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
		onCheckFieldAutocomplete();
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
	} else if (_peer->isUser() && (_peer->asUser()->blockStatus() == UserData::BlockStatus::Unknown || _peer->asUser()->callsStatus() == UserData::CallsStatus::Unknown)) {
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
			|| (!isBlocked() && _joinChannel->isHidden() == isJoinChannel())
			|| (isMuteUnmute() && _discuss->isHidden() == hasDiscussionGroup())) {
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
	const auto weak = make_weak(this);
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
	const auto weak = make_weak(this);
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
		onInlineBotCancel();
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
	} else {
		emit cancelled();
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
	if (!Ui::isLayerShown() && !Core::App().locked()) {
		if (_nonEmptySelection
			|| (_list && _list->wasSelectedText())
			|| _recording
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
	if (!_peer || _peer->asChannel() != channel || !msgId) return;
	if (_editMsgId == msgId || _replyToId == msgId) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && _pinnedBar->msgId == msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateReplyEditText(not_null<HistoryItem*> item) {
	_replyEditMsgText.setText(
		st::messageTextStyle,
		item->inReplyText(),
		Ui::DialogTextOptions());
	if (!_field->isHidden() || _recording) {
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
					fullname = App::peerName(from);
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
		App::peerName(from),
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
						p.drawPixmap(to.x(), to.y(), image->pixSingle(drawMsgText->fullId(), image->width() / cIntRetinaFactor(), image->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
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
			const auto serviceColor = (_toForward.size() > 1)
				|| (firstMedia != nullptr)
				|| firstItem->serviceMsg();
			const auto preview = (_toForward.size() < 2 && firstMedia && firstMedia->hasReplyPreview())
				? firstMedia->replyPreview()
				: nullptr;
			if (preview) {
				auto to = QRect(forwardLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (preview->width() == preview->height()) {
					p.drawPixmap(to.x(), to.y(), preview->pix(firstItem->fullId()));
				} else {
					auto from = (preview->width() > preview->height()) ? QRect((preview->width() - preview->height()) / 2, 0, preview->height(), preview->height()) : QRect(0, (preview->height() - preview->width()) / 2, preview->width(), preview->width());
					p.drawPixmap(to, preview->pix(firstItem->fullId()), from);
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
		auto previewLeft = st::historyReplySkip + st::webPageLeft;
		p.fillRect(st::historyReplySkip, backy + st::msgReplyPadding.top(), st::webPageBar, st::msgReplyBarSize.height(), st::msgInReplyBarColor);
		if ((_previewData->photo && !_previewData->photo->isNull()) || (_previewData->document && _previewData->document->hasThumbnail() && !_previewData->document->isPatternWallPaper())) {
			const auto preview = _previewData->photo
				? _previewData->photo->getReplyPreview(Data::FileOrigin())
				: _previewData->document->getReplyPreview(Data::FileOrigin());
			if (preview) {
				auto to = QRect(previewLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (preview->width() == preview->height()) {
					p.drawPixmap(to.x(), to.y(), preview->pix(Data::FileOrigin()));
				} else {
					auto from = (preview->width() > preview->height()) ? QRect((preview->width() - preview->height()) / 2, 0, preview->height(), preview->height()) : QRect(0, (preview->height() - preview->width()) / 2, preview->width(), preview->width());
					p.drawPixmap(to, preview->pix(Data::FileOrigin()), from);
				}
			}
			previewLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::historyReplyNameFg);
		_previewTitle.drawElided(p, previewLeft, backy + st::msgReplyPadding.top(), width() - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
		p.setPen(st::historyComposeAreaFg);
		_previewDescription.drawElided(p, previewLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
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

	if (!_replyEditMsg || _replyEditMsg->history()->peer->isSelf()) return;

	QString editTimeLeftText;
	int updateIn = -1;
	auto timeSinceMessage = ItemDateTime(_replyEditMsg).msecsTo(QDateTime::currentDateTime());
	auto editTimeLeft = (Global::EditTimeLimit() * 1000LL) - timeSinceMessage;
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

void HistoryWidget::drawRecording(Painter &p, float64 recordActive) {
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	auto delta = qMin(_recordingLevel.current() / 0x4000, 1.);
	auto d = 2 * qRound(st::historyRecordSignalMin + (delta * (st::historyRecordSignalMax - st::historyRecordSignalMin)));
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(_attachToggle->x() + (_tabbedSelectorToggle->width() - d) / 2, _attachToggle->y() + (_attachToggle->height() - d) / 2, d, d);
	}

	auto duration = formatDurationText(_recordingSamples / Media::Player::kDefaultFrequency);
	p.setFont(st::historyRecordFont);

	p.setPen(st::historyRecordDurationFg);
	p.drawText(_attachToggle->x() + _tabbedSelectorToggle->width(), _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, duration);

	int32 left = _attachToggle->x() + _tabbedSelectorToggle->width() + st::historyRecordFont->width(duration) + ((_send->width() - st::historyRecordVoice.width()) / 2);
	int32 right = width() - _send->width();

	p.setPen(anim::pen(st::historyRecordCancel, st::historyRecordCancelActive, 1. - recordActive));
	p.drawText(left + (right - left - _recordCancelWidth) / 2, _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, tr::lng_record_cancel(tr::now));
}

void HistoryWidget::drawPinnedBar(Painter &p) {
	Expects(_pinnedBar != nullptr);

	auto top = _topBar->bottomNoMargins();
	bool serviceColor = false, hasForward = readyToForward();
	ImagePtr preview;
	p.fillRect(myrtlrect(0, top, width(), st::historyReplyHeight), st::historyPinnedBg);

	top += st::msgReplyPadding.top();
	QRect rbar(myrtlrect(st::msgReplyBarSkip + st::msgReplyBarPos.x(), top + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height()));
	p.fillRect(rbar, st::msgInReplyBarColor);

	int32 left = st::msgReplyBarSkip + st::msgReplyBarSkip;
	if (_pinnedBar->msg) {
		const auto media = _pinnedBar->msg->media();
		if (media && media->hasReplyPreview()) {
			if (const auto image = media->replyPreview()) {
				QRect to(left, top, st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				p.drawPixmap(to.x(), to.y(), image->pixSingle(_pinnedBar->msg->fullId(), image->width() / cIntRetinaFactor(), image->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
			}
			left += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::historyReplyNameFg);
		p.setFont(st::msgServiceNameFont);
		p.drawText(left, top + st::msgServiceNameFont->ascent, (media && media->poll()) ? tr::lng_pinned_poll(tr::now) : tr::lng_pinned_message(tr::now));

		p.setPen(st::historyComposeAreaFg);
		p.setTextPalette(st::historyComposeAreaPalette);
		_pinnedBar->text.drawElided(p, left, top + st::msgServiceNameFont->height, width() - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right());
		p.restoreTextPalette();
	} else {
		p.setFont(st::msgDateFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(left, top + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(tr::lng_profile_loading(tr::now), width() - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right()));
	}
}

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

	Window::SectionWidget::PaintBackground(this, e->rect());

	Painter p(this);
	const auto clip = e->rect();
	if (_list) {
		if (!_field->isHidden() || _recording) {
			drawField(p, clip);
			if (!_send->isHidden() && _recording) {
				drawRecording(p, _send->recordActiveRatio());
			}
		} else if (const auto error = writeRestriction()) {
			drawRestrictedWrite(p, *error);
		}
		if (_aboutProxyPromotion) {
			p.fillRect(_aboutProxyPromotion->geometry(), st::historyReplyBg);
		}
		if (_pinnedBar && !_pinnedBar->cancel->isHidden()) {
			drawPinnedBar(p);
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

void HistoryWidget::onScrollTimer() {
	auto d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
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
		_scrollTimer.start(15);
	} else {
		_scrollTimer.stop();
	}
}

void HistoryWidget::noSelectingScroll() {
	_scrollTimer.stop();
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
	setTabbedPanel(nullptr);
}
