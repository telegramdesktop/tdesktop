/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_chat_section.h"

#include "history/admin_log/history_admin_log_section.h"
#include "history/view/controls/history_view_top_controls.h"
#include "history/view/controls/history_view_bottom_controls.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/controls/history_view_compose_search.h"
#include "history/view/controls/history_view_draft_options.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "history/view/history_view_about_view.h"
#include "history/view/history_view_group_members_widget.h"
#include "history/view/history_view_paid_reaction_toast.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_scheduled_section.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_subsection_tabs.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_translate_tracker.h"
#include "history/view/history_view_self_forwards_tagger.h"
#include "history/view/history_view_draw_to_reply.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_inner_widget.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h" // GetErrorForSending.
#include "history/history_view_pull_to_next_channel.h"
#include "history/history_item_reply_markup.h"
#include "history/history_view_pull_to_next_channel.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "ui/chat/choose_theme_controller.h"
#include "ui/chat/pinned_bar.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/swipe_handler.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "base/timer_rpl.h"
#include "api/api_bot.h"
#include "api/api_chat_participants.h"
#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "boxes/premium_preview_box.h"
#include "data/business/data_shortcut_messages.h"
#include "settings/business/settings_quick_replies.h"
#include "ui/boxes/confirm_box.h"
#include "chat_helpers/bot_keyboard.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_selector.h"
#include "boxes/delete_messages_box.h"
#include "boxes/send_files_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "base/call_delayed.h"
#include "base/qt/qt_key_modifiers.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "core/click_handler_types.h"
#include "core/mime_type.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "media/player/media_player_instance.h"
#include "menu/menu_timecode_action.h"
#include "data/components/ephemeral_messages.h"
#include "data/components/recent_inline_bots.h"
#include "data/components/scheduled_messages.h"
#include "data/components/sponsored_messages.h"
#include "data/data_histories.h"
#include "data/data_history_messages.h"
#include "data/data_msg_id.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_replies_list.h"
#include "data/data_peer_values.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_shared_media.h"
#include "data/data_send_action.h"
#include "data/data_premium_limits.h"
#include "data/notify/data_notify_settings.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_account.h"
#include "storage/localimageloader.h"
#include "support/support_autocomplete.h"
#include "support/support_common.h"
#include "support/support_preload.h"
#include "inline_bots/inline_bot_result.h"
#include "info/profile/info_profile_values.h"
#include "iv/editor/iv_editor_session.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <limits>
#include <QtCore/QMimeData>

namespace HistoryView {

namespace {

enum class SendPermission {
	Anything,
	Files,
};

constexpr auto kShowMembersDropdownTimeoutMs = 300;
constexpr auto kScrollToVoiceAfterScrolledMs = crl::time(1000);
constexpr auto kPsaAboutPrefix = "cloud_lng_about_psa_";

[[nodiscard]] bool CanSendResolved(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		SendPermission permission) {
	switch (permission) {
	case SendPermission::Anything:
		return topic
			? Data::CanSendAnything(topic)
			: Data::CanSendAnything(peer);
	case SendPermission::Files:
		return topic
			? Data::CanSendAnyOf(topic, Data::FilesSendRestrictions())
			: Data::CanSendAnyOf(peer, Data::FilesSendRestrictions());
	}
	return false;
}

[[nodiscard]] std::optional<MsgId> ShowAtMsgIdFromPosition(
		not_null<History*> history,
		Data::MessagePosition position) {
	const auto migrated = history->migrateFrom();
	if (position == Data::UnreadMessagePosition) {
		return ShowAtUnreadMsgId;
	} else if (position == Data::MaxMessagePosition) {
		return ShowAtTheEndMsgId;
	}
	const auto fullId = position.fullId;
	if ((fullId.peer == history->peer->id)
		&& (IsServerMsgId(fullId.msg) || IsClientMsgId(fullId.msg))) {
		return fullId.msg;
	} else if (migrated
		&& (fullId.peer == migrated->peer->id)
		&& IsServerMsgId(fullId.msg)) {
		return -fullId.msg;
	}
	return std::nullopt;
}

[[nodiscard]] bool IsSpecialShowAtMsgId(MsgId id) {
	return (id == ShowAtTheEndMsgId)
		|| (id == ShowAndStartBotMsgId)
		|| (id == ShowAndMaybeStartBotMsgId)
		|| (id == ShowForChooseMessagesMsgId);
}

[[nodiscard]] FullMsgId ResolveHighlightId(
		not_null<History*> history,
		MsgId highlightId) {
	if (highlightId < 0) {
		const auto migrated = history->migrateFrom();
		if (migrated && IsServerMsgId(-highlightId)) {
			return FullMsgId(migrated->peer->id, -highlightId);
		}
	}
	return FullMsgId(history->peer->id, highlightId);
}

} // namespace

ChatMemento::ChatMemento(
	ChatViewId id,
	MsgId highlightId,
	MessageHighlightId highlight)
: _id(id)
, _highlightId(IsSpecialShowAtMsgId(highlightId) ? MsgId(0) : highlightId)
, _highlight(std::move(highlight))
, _activateChooseForReport(highlightId == ShowForChooseMessagesMsgId)
, _sendBotStart(highlightId == ShowAndStartBotMsgId)
, _maybeSendBotStart(highlightId == ShowAndMaybeStartBotMsgId) {
	if ((highlightId == ShowAtTheEndMsgId)
		|| _sendBotStart
		|| _maybeSendBotStart) {
		_list.setAroundPosition(Data::MaxMessagePosition);
	} else if (_highlightId || _id.sublist) {
		_list.setAroundPosition({
			.fullId = ResolveHighlightId(_id.history, _highlightId),
			.date = TimeId(0),
		});
	}
	if (!_list.aroundPosition()
		&& !_highlightId
		&& !_id.repliesRootId
		&& !_id.sublist) {
		setFromHistory(_id.history);
	}
}

ChatMemento::ChatMemento(
	Comments,
	not_null<HistoryItem*> commentsItem,
	MsgId commentId)
: ChatMemento({
	.history = commentsItem->history(),
	.repliesRootId = commentsItem->id,
}, commentId) {
}

void ChatMemento::setFromTopic(not_null<Data::ForumTopic*> topic) {
	_replies = topic->replies();
	if (!_list.aroundPosition()) {
		_list = *topic->listMemento();
	}
}

void ChatMemento::setFromHistory(not_null<History*> history) {
	const auto migrated = history->migrateFrom();
	const auto showAtMsgId = history->showAtMsgId;
	const auto scrollTopState = [&]() -> std::optional<ListMemento::ScrollTopState> {
		if (history->listScrollTopItemId) {
			return ListMemento::ScrollTopState{
				.item = {
					.fullId = history->listScrollTopItemId,
					.date = history->listScrollTopItemDate,
				},
				.shift = history->listScrollTopShift,
			};
		} else if (migrated && migrated->listScrollTopItemId) {
			return ListMemento::ScrollTopState{
				.item = {
					.fullId = migrated->listScrollTopItemId,
					.date = migrated->listScrollTopItemDate,
				},
				.shift = migrated->listScrollTopShift,
			};
		}
		return std::nullopt;
	}();
	if (scrollTopState) {
		_list.setAroundPosition(scrollTopState->item);
		_list.setScrollTopState(*scrollTopState);
	} else if (showAtMsgId == ShowAtUnreadMsgId) {
		if (history->session().supportMode()) {
			_list.setAroundPosition(Data::MaxMessagePosition);
		} else if (!history->trackUnreadMessages()) {
			_list.setAroundPosition(Data::MaxMessagePosition);
		} else {
			_list.setAroundPosition(Data::UnreadMessagePosition);
		}
	} else if (showAtMsgId == ShowAtTheEndMsgId) {
		_list.setAroundPosition(Data::MaxMessagePosition);
	} else if (IsServerMsgId(showAtMsgId) || IsClientMsgId(showAtMsgId)) {
		_list.setAroundPosition({
			.fullId = FullMsgId(history->peer->id, showAtMsgId),
			.date = TimeId(0),
		});
	} else if (migrated && IsServerMsgId(-showAtMsgId)) {
		_list.setAroundPosition({
			.fullId = FullMsgId(migrated->peer->id, -showAtMsgId),
			.date = TimeId(0),
		});
	}
}


Data::ForumTopic *ChatMemento::topicForRemoveRequests() const {
	return _id.repliesRootId
		? _id.history->peer->forumTopicFor(_id.repliesRootId)
		: nullptr;
}

Data::SavedSublist *ChatMemento::sublistForRemoveRequests() const {
	return _id.sublist;
}

void ChatMemento::setReadInformation(
		MsgId inboxReadTillId,
		int unreadCount,
		MsgId outboxReadTillId) {
	if (!_id.repliesRootId) {
		return;
	} else if (!_replies) {
		if (const auto forum = _id.history->asForum()) {
			if (const auto topic = forum->topicFor(_id.repliesRootId)) {
				_replies = topic->replies();
			}
		}
		if (!_replies) {
			_replies = std::make_shared<Data::RepliesList>(
				_id.history,
				_id.repliesRootId);
		}
	}
	_replies->setInboxReadTill(inboxReadTillId, unreadCount);
	_replies->setOutboxReadTill(outboxReadTillId);
}

object_ptr<Window::SectionWidget> ChatMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	if (!_list.aroundPosition().fullId
		&& _replies
		&& _replies->computeInboxReadTillFull() == MsgId(1)) {
		_list.setAroundPosition(Data::MinMessagePosition);
		_list.setScrollTopState(ListMemento::ScrollTopState{
			Data::MinMessagePosition
		});
	} else if (!_list.aroundPosition().fullId
		&& _id.sublist
		&& _id.sublist->computeInboxReadTillFull() == MsgId(1)) {
		_list.setAroundPosition(Data::MinMessagePosition);
		_list.setScrollTopState(ListMemento::ScrollTopState{
			Data::MinMessagePosition
		});
	}
	auto result = object_ptr<ChatWidget>(parent, controller, _id);
	result->setInternalState(geometry, this);
	return result;
}

void ChatMemento::setupTopicViewer() {
	if (_id.repliesRootId) {
		_id.history->owner().itemIdChanged(
		) | rpl::on_next([=](const Data::Session::IdChange &change) {
			if (_id.repliesRootId == change.oldId) {
				_id.repliesRootId = change.newId.msg;
				_replies = nullptr;
			}
		}, _lifetime);
	}
}

ChatWidget::ChatWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	ChatViewId id)
: Window::SectionWidget(parent, controller, id.history->peer)
, WindowListDelegate(controller)
, _history(id.history)
, _peer(_history->peer)
, _id(id)
, _repliesRootId(_id.repliesRootId)
, _repliesRoot(lookupRepliesRoot())
, _topic(lookupTopic())
, _areComments(computeAreComments())
, _sublist(_id.sublist)
, _monoforumPeerId((_sublist && _sublist->parentChat())
	? _sublist->sublistPeer()->id
	: PeerId())
, _sendAction(_repliesRootId
	? _history->owner().sendActionManager().repliesPainter(
		_history,
		_repliesRootId)
	: nullptr)
, _topBar(this, controller)
, _topBarShadow(this)
, _membersDropdownShowTimer([=] { showMembersDropdown(); })
, _paidReactionToast(std::make_unique<HistoryView::PaidReactionToast>(
	this,
	&session().data(),
	rpl::single(0),
	[=](not_null<const HistoryView::Element*> view) {
		return _inner
			&& (view->delegate().get() == _inner.data())
			&& !_inner->elementIntersectsRange(
				view,
				std::numeric_limits<int>::lowest(),
				0);
	}))
, _composeControls(std::make_unique<ComposeControls>(
	this,
	ComposeControlsDescriptor{
		.show = controller->uiShow(),
		.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
			listShowPremiumToast(emoji);
		},
		.mode = ComposeControls::Mode::Normal,
		.sendMenuDetails = [=] { return sendMenuDetails(); },
		.regularWindow = controller,
		.stickerOrEmojiChosen = controller->stickerOrEmojiChosen(),
		.customPlaceholder = _botKeyboardPlaceholder.value(),
		.scheduledToggleValue = _topic
			? rpl::single(rpl::empty_value()) | rpl::then(
				session().scheduledMessages().updates(_topic->owningHistory())
			) | rpl::map([=] {
				return session().scheduledMessages().hasFor(_topic);
			}) | rpl::type_erased
			: (_repliesRootId || _sublist)
			? (rpl::single(false) | rpl::type_erased)
			: rpl::single(rpl::empty_value()) | rpl::then(
				session().scheduledMessages().updates(_history)
			) | rpl::map([=] {
				return session().scheduledMessages().count(_history) > 0;
			}) | rpl::type_erased,
		.currentSuggest = [=] { return suggestOptions(); },
		.processShortcut = [=](QString shortcut) {
			const auto messages = &_peer->owner().shortcutMessages();
			const auto shortcutId = messages->lookupShortcutId(shortcut);
			if (shortcut.isEmpty()) {
				controller->showSettings(Settings::QuickRepliesId());
			} else if (!_peer->session().premium()) {
				ShowPremiumPreviewToBuy(
					controller,
					PremiumFeature::QuickReplies);
			} else if (shortcutId) {
				session().api().sendShortcutMessages(_peer, shortcutId);
				session().api().finishForwarding(prepareSendAction({}));
				if (const auto field = _composeControls->fieldForMention()) {
					_composeControls->setText(field->getTextWithTagsPart(
						field->textCursor().position()));
				}
			}
		},
		.moderateKeyActivateCallback = [=](int key) {
			const auto context = [=](FullMsgId itemId) {
				return _inner->prepareClickContext(Qt::LeftButton, itemId);
			};
			return _keyboard
				&& !_keyboard->isHidden()
				&& _keyboard->moderateKeyActivate(key, context);
		},
		.suggestPostToggleShown = _suggestPostToggleShown.value(),
		.suggestPostToggleActive = _suggestPostToggleActive.value(),
		.botKeyboardShownToggleShown
			= _botKeyboardShownToggleShown.value(),
		.botKeyboardHideToggleShown
			= _botKeyboardHideToggleShown.value(),
		.botCommandStartShownExtraGuard
			= _botCommandStartExtraGuard.value(),
	}))
, _bottom(std::make_unique<BottomControls>(
	this,
	BottomControlsDescriptor{
		.controller = controller,
		.history = _history.get(),
		.repliesRootId = _repliesRootId,
		.topic = _topic,
		.sublist = _sublist,
		.mode = (_sublist
			? BottomControlsMode::Sublist
			: _repliesRootId
			? BottomControlsMode::Replies
			: BottomControlsMode::History),
	}))
, _scroll(std::make_unique<Ui::ElasticScroll>(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll)))
, _pullToNext(std::make_unique<PullToNextChannel>(
	this,
	_scroll.get(),
	controller,
	[=] {
		return _inner
			&& _inner->loadedAtBottomKnown()
			&& _inner->loadedAtBottom();
	}))
, _cornerButtons(
		_scroll.get(),
		controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	controller->chatStyle()->paletteChanged(
	) | rpl::on_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	Window::ChatThemeValueFromPeer(
		controller,
		_peer
	) | rpl::on_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	setupRoot();
	setupShortcuts();

	_peer->updateFull();
	if (const auto channel = _peer->asMegagroup()) {
		if (!channel->mgInfo->adminsLoaded) {
			session().api().chatParticipants().requestAdmins(channel);
		}
	}

	refreshTopBarActiveChat();

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_topBar->deleteSelectionRequest(
	) | rpl::on_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->forwardSelectionRequest(
	) | rpl::on_next([=] {
		confirmForwardSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::on_next([=] {
		clearSelected();
	}, _topBar->lifetime());
	_topBar->cancelChooseForReportRequest(
	) | rpl::on_next([=] {
		this->controller()->clearChooseReportMessages();
	}, _topBar->lifetime());
	_topBar->searchRequest(
	) | rpl::on_next([=] {
		searchRequested();
	}, _topBar->lifetime());
	_topBar->membersShowAreaActive(
	) | rpl::on_next([=](bool active) {
		setMembersShowAreaActive(active);
	}, _topBar->lifetime());
	if (_sublist) {
		_topBar->setCustomTitle(tr::lng_contacts_loading(tr::now));
	}

	controller->adaptive().value(
	) | rpl::on_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_scroll->setHandleTouch(false);
	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_inner->lower();
	_topControls = std::make_unique<TopControls>(
		this,
		TopControlsDescriptor{
			.controller = controller,
			.history = _history.get(),
			.repliesRootId = _repliesRootId,
			.topic = _topic,
			.sublist = _sublist,
			.monoforumPeerId = _monoforumPeerId,
			.scroll = _scroll.get(),
			.list = _inner.data(),
			.keyboardReservedHeight = [=] {
				return (_kbScroll && !_kbScroll->isHidden())
					? _kbScroll->height()
					: 0;
			},
			.moveWithTopDelta = [=](int delta) {
				setGeometryWithTopMoved(geometry(), delta);
			},
			.relayout = [=] {
				updateControlsGeometry();
			},
			.relayoutWithScrollTopDelta = [=](int delta) {
				_scrollTopDelta = delta;
				updateControlsGeometry();
				_scrollTopDelta = 0;
			},
			.showAtStart = [=] {
				showAtStart();
			},
			.showAtPosition = [=](Data::MessagePosition position) {
				showAtPosition(position);
			},
			.preparePinnedClickContext = [=](FullMsgId itemId) {
				return _inner->prepareClickHandlerContext(itemId);
			},
		});
	_scroll->move(0, _topBar->height());
	_scroll->show();
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_scroll->setOverscrollEdges([=] {
		return _inner->loadedAtTopKnown() && _inner->loadedAtTop();
	}, [=] {
		return _inner->loadedAtBottomKnown() && _inner->loadedAtBottom();
	});
	if (_topic) {
		_pullToNext->setTopic(_topic);
	} else if (mode() == Mode::History) {
		_pullToNext->setHistory(_history);
	}
	_scroll->setBottomContentRequest([=] {
		return appendSponsoredMessages();
	});
	_scroll->scrolls(
	) | rpl::on_next([=] {
		onScroll();
	}, lifetime());
	if (session().supportMode() && mode() == Mode::History && !_topic) {
		_supportAutocomplete = std::make_unique<Support::Autocomplete>(
			this,
			&session());
		supportInitAutocomplete();
		_composeControls->fieldTabbed(
		) | rpl::on_next([=](
				not_null<Ui::InputField::TabbedRequest*> request) {
			if (_supportAutocomplete) {
				if (const auto field = _composeControls->fieldForMention()) {
					_supportAutocomplete->activate(field);
					request->handled = true;
				}
			}
		}, lifetime());
	}

	_inner->editMessageRequested(
	) | rpl::filter([=] {
		return !_bottom->isButtonActive();
	}) | rpl::on_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			const auto media = item->media();
			if (!media || media->webpage() || media->allowsEditCaption()) {
				if (!item->richPage()) {
					if (isChoosingTheme()) {
						toggleChooseChatTheme(_peer, false);
					}
					if (_composeSearch) {
						_composeSearch->hideAnimated();
					}
				}
				_composeControls->editMessage(
					fullId,
					_inner->getSelectedTextRange(item));
			} else if (media->todolist()) {
				Window::PeerMenuEditTodoList(controller, item);
			}
		}
	}, _inner->lifetime());

	_inner->replyToMessageRequested(
	) | rpl::on_next([=](ListWidget::ReplyToMessageRequest request) {
		const auto canSendReply = CanSendResolved(
			_peer,
			resolvedTopic(),
			SendPermission::Anything);
		const auto &to = request.to;
		const auto still = _history->owner().message(to.messageId);
		const auto allowInAnotherChat = still && still->allowsForward();
		const auto bottomBarActive = _bottom->isButtonActive();
		if (allowInAnotherChat
			&& (bottomBarActive
				|| !canSendReply
				|| request.forceAnotherChat)) {
			Controls::ShowReplyToChatBox(controller->uiShow(), { to });
		} else if (!bottomBarActive && canSendReply) {
			replyToMessage(to);
			_composeControls->focus();
			if (_composeSearch) {
				_composeSearch->hideAnimated();
			}
		}
	}, _inner->lifetime());

	_inner->showMessageRequested(
	) | rpl::on_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			showAtPosition(item->position());
		}
	}, _inner->lifetime());

	_inner->setInsertTextCallback([=](const QString &text) {
		if (const auto field = _composeControls->fieldForMention()) {
			Menu::InsertTextAtCursor(field, text);
		}
	});

	_composeControls->sendActionUpdates(
	) | rpl::on_next([=](ComposeControls::SendActionUpdate &&data) {
		if (mode() == Mode::Sublist) {
			return;
		} else if (!data.cancel) {
			session().sendProgressManager().update(
				_history,
				_repliesRootId,
				data.type,
				data.progress);
		} else {
			session().sendProgressManager().cancel(
				_history,
				_repliesRootId,
				data.type);
		}
	}, lifetime());

	_history->session().changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		if (update.item == _repliesRoot) {
			_repliesRoot = nullptr;
			updatePinnedVisibility();
			if (!_topic) {
				controller->showBackFromStack();
			}
		}
	}, lifetime());

	_history->session().changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		if (_kbReplyTo == update.item) {
			setKeyboardReplyTo(nullptr);
			updateBotKeyboard();
		}
	}, lifetime());

	if (_sublist) {
		subscribeToSublist();
	} else if (!_topic) {
		_history->session().changes().historyUpdates(
			_history,
			Data::HistoryUpdate::Flag::OutboxRead
		) | rpl::on_next([=] {
			_inner->update();
		}, lifetime());
	}

	session().api().sendActions(
	) | rpl::filter([=](const Api::SendAction &action) {
		if (_creatingBotTopic
			&& action.history == _creatingBotTopic->owningHistory()
			&& action.replyTo.topicRootId == _creatingBotTopic->rootId()) {
			Ui::PostponeCall(_creatingBotTopic, [=] {
				using namespace HistoryView;
				const auto topic = base::take(_creatingBotTopic);
				controller->showSection(
					std::make_shared<ChatMemento>(ChatViewId{
						.history = topic->owningHistory(),
						.repliesRootId = topic->rootId(),
					}),
					Window::SectionShow::Way::ClearStack);
			});
			return false;
		}
		return (action.history == _history)
			&& (action.replyTo.topicRootId == _repliesRootId)
			&& (action.replyTo.monoforumPeerId == _monoforumPeerId);
	}) | rpl::on_next([=](const Api::SendAction &action) {
		if (!action.replaceMediaOf) {
			const auto lastKeyboardUsed = lastForceReplyReplied(
				action.replyTo.messageId);
			const auto replyMatches = action.replyTo.messageId
				&& (action.replyTo.messageId
					== _composeControls->draftReplyingToMessage().messageId);
			auto cancelledReply = false;
			auto cancelledSuggest = false;
			if (action.options.scheduled || !_justMarkingAsRead) {
				if (replyMatches || lastKeyboardUsed) {
					cancelledReply = cancelReply(lastKeyboardUsed);
				}
				if (mode() == Mode::History) {
					cancelledSuggest = cancelSuggestPost();
				}
			}
			if (action.options.scheduled) {
				if (_topic) {
					crl::on_main(this, [=, t = _topic] {
						controller->showSection(
							std::make_shared<HistoryView::ScheduledMemento>(t));
					});
				} else if (mode() == Mode::History) {
					crl::on_main(this, [=, history = action.history] {
						controller->showSection(
							std::make_shared<HistoryView::ScheduledMemento>(
								history));
					});
				}
			} else {
				if (mode() == Mode::History) {
					showAtEnd();
				}
				if ((cancelledReply || cancelledSuggest)
					&& !action.clearDraft) {
					session().api().saveCurrentDraftToCloud();
				}
			}
		}
		if ((mode() == Mode::History)
			&& action.options.handleSupportSwitch) {
			handleSupportSwitch(action.history);
		}
	}, lifetime());
	if (mode() == Mode::History) {
		_topControls->subscribeToPinnedMessages();
	}

	using MediaSwitch = ::Media::Player::Instance::Switch;
	::Media::Player::instance()->switchToNextEvents(
	) | rpl::filter([=](const MediaSwitch &pair) {
		return (pair.from.type() == AudioMsgId::Type::Voice);
	}) | rpl::on_next([=](const MediaSwitch &pair) {
		scrollToCurrentVoiceMessage(pair.from.contextId(), pair.to);
	}, lifetime());

	_selfForwardsTagger = std::make_unique<HistoryView::SelfForwardsTagger>(
		controller,
		this,
		[=] { return _inner.data(); },
		_scroll.get(),
		[=] { return _history; });
	if ((mode() == Mode::History) && session().supportMode()) {
		session().data().chatListEntryRefreshes(
		) | rpl::on_next([=] {
			crl::on_main(this, [=] { checkSupportPreload(true); });
		}, lifetime());
	}

	setupTopicViewer();
	setupComposeControls();
	setupSwipeReplyAndBack();

	if (mode() != Mode::Sublist) {
		_kbScroll = base::make_unique_q<Ui::ScrollArea>(
			this,
			st::botKbScroll);
		_kbScroll->hide();
		_keyboard = _kbScroll->setOwnedWidget(
			object_ptr<BotKeyboard>(controller, _kbScroll.get()));
		_keyboard->sendCommandRequests(
		) | rpl::on_next([=](Bot::SendCommandRequest r) {
			sendBotCommand(std::move(r), {});
		}, lifetime());
	}

	_bottom->actionRequests(
	) | rpl::on_next([=](BottomControlsAction action) {
		switch (action) {
		case BottomControlsAction::Unblock:
			unblockUser();
			break;
		case BottomControlsAction::BotStart:
			sendBotStartCommand();
			break;
		case BottomControlsAction::JoinChannel:
			joinChannelAction();
			break;
		case BottomControlsAction::JoinGroup:
			joinGroupAction();
			break;
		case BottomControlsAction::MuteUnmute:
			toggleMuteUnmute();
			break;
		case BottomControlsAction::Report:
			reportSelectedMessages();
			break;
		}
	}, lifetime());

	_bottom->contentHeightValue(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, lifetime());

	_bottom->isButtonActiveValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		updateControlsVisibility();
	}, lifetime());

	refreshCanSendMessages();

	using PeerUpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		_peer,
		PeerUpdateFlag::Rights
			| PeerUpdateFlag::IsBlocked
			| PeerUpdateFlag::Notifications
			| PeerUpdateFlag::ChannelAmIn
			| PeerUpdateFlag::BotStartToken
			| PeerUpdateFlag::FullInfo
			| PeerUpdateFlag::Members
			| PeerUpdateFlag::ManagedBot
			| PeerUpdateFlag::StarsPerMessage
			| PeerUpdateFlag::Migration
			| PeerUpdateFlag::UnavailableReason
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		if (update.flags & PeerUpdateFlag::Migration) {
			handlePeerMigration();
			return;
		}
		if (update.flags & PeerUpdateFlag::UnavailableReason) {
			const auto unavailable = _peer->computeUnavailableReason();
			if (!unavailable.isEmpty()) {
				const auto account = not_null(&_peer->account());
				closeCurrent();
				if (const auto primary = Core::App().windowFor(account)) {
					primary->showToast(unavailable);
				}
				return;
			}
		}
		_bottom->applyPeerUpdate(update.flags);
		if (update.flags & (PeerUpdateFlag::FullInfo
			| PeerUpdateFlag::Rights)) {
			refreshCanSendMessages();
		}
		if (update.flags & (PeerUpdateFlag::FullInfo
			| PeerUpdateFlag::Rights
			| PeerUpdateFlag::ChannelAmIn
			| PeerUpdateFlag::StarsPerMessage)) {
			refreshSuggestPostToggle();
		}
		if (update.flags & PeerUpdateFlag::IsBlocked) {
			refreshAboutView(true);
		} else if (update.flags & (PeerUpdateFlag::FullInfo
			| PeerUpdateFlag::Rights
			| PeerUpdateFlag::Members
			| PeerUpdateFlag::ManagedBot
			| PeerUpdateFlag::StarsPerMessage)) {
			refreshAboutView();
		}
		if (update.flags & PeerUpdateFlag::FullInfo) {
			checkMaybeSendBotStart();
		}
		checkSuggestToGigagroup();
		updateControlsVisibility();
	}, lifetime());

	session().data().historyAccessLost(
	) | rpl::filter([=](not_null<History*> history) {
		return (history == _history);
	}) | rpl::on_next([=] {
		const auto was = _peer;
		const auto account = not_null(&was->account());
		closeCurrent();
		if (const auto primary = Core::App().windowFor(account)) {
			primary->showToast(was->isMegagroup()
				? tr::lng_group_not_accessible(tr::now)
				: tr::lng_channel_not_accessible(tr::now));
		}
	}, lifetime());

	if ((mode() == Mode::History) && !_topic) {
		session().data().sentToScheduled(
		) | rpl::filter([=](const Data::SentToScheduled &value) {
			return (value.history == _history);
		}) | rpl::on_next([=](const Data::SentToScheduled &value) {
			const auto id = value.scheduledId;
			crl::on_main(this, [=] {
				controller->showSection(
					std::make_shared<HistoryView::ScheduledMemento>(
						_history,
						id));
			});
		}, lifetime());

		session().data().sentFromScheduled(
		) | rpl::on_next([=](const Data::SentFromScheduled &value) {
			if (value.item->awaitingVideoProcessing()
				&& !_sentFromScheduledTip
				&& HistoryView::ShowScheduledVideoPublished(
					controller,
					value,
					crl::guard(this, [=] { _sentFromScheduledTip = false; }))) {
				_sentFromScheduledTip = true;
			}
		}, lifetime());
	}

	if (mode() == Mode::History) {
		using HistoryUpdateFlag = Data::HistoryUpdate::Flag;
		session().changes().historyUpdates(
			_history,
			HistoryUpdateFlag::BotKeyboard
				| HistoryUpdateFlag::CloudDraft
				| HistoryUpdateFlag::UnreadMentions
				| HistoryUpdateFlag::UnreadReactions
				| HistoryUpdateFlag::UnreadPollVotes
				| HistoryUpdateFlag::UnreadView
		) | rpl::on_next([=](const Data::HistoryUpdate &update) {
			const auto flags = update.flags;
			if (flags & HistoryUpdateFlag::BotKeyboard) {
				updateBotKeyboard();
			}
			if (flags & HistoryUpdateFlag::CloudDraft) {
				_composeControls->applyCloudDraft();
				refreshSuggestFromDraft();
			}
			if ((flags & HistoryUpdateFlag::UnreadMentions)
				|| (flags & HistoryUpdateFlag::UnreadReactions)
				|| (flags & HistoryUpdateFlag::UnreadPollVotes)) {
				_cornerButtons.updateUnreadThingsVisibility();
			}
			if (flags & HistoryUpdateFlag::UnreadView) {
				unreadCountUpdated();
			}
		}, lifetime());
	}

	if (mode() == Mode::History || mode() == Mode::Replies) {
		using MessageUpdateFlag = Data::MessageUpdate::Flag;
		session().changes().messageUpdates(
			MessageUpdateFlag::ReplyMarkup
				| MessageUpdateFlag::BotCallbackSent
		) | rpl::on_next([=](const Data::MessageUpdate &update) {
			const auto flags = update.flags;
			const auto inRepliesRoot = (update.item->history() == _history)
				&& _repliesRootId
				&& update.item->inThread(_repliesRootId);
			if (flags & MessageUpdateFlag::ReplyMarkup) {
				if (mode() == Mode::History) {
					if (_keyboard
							&& _keyboard->forMsgId()
								== update.item->fullId()) {
						updateBotKeyboard(update.item->history(), true);
					}
				} else if (inRepliesRoot) {
					if (keyboardSourceId() == update.item->fullId()) {
						updateBotKeyboard(update.item->history(), true);
					} else if (_repliesLastSlice) {
						maybeUpdateLastKeyboardFromSlice(
							*_repliesLastSlice,
							true);
					} else {
						updateBotKeyboard(update.item->history(), true);
					}
				}
			}
			if ((flags & MessageUpdateFlag::BotCallbackSent)
				&& (mode() == Mode::History || inRepliesRoot)) {
				botCallbackSent(update.item);
			}
		}, lifetime());
	}

	refreshSuggestFromDraft();

	orderWidgets();

	updateControlsVisibility();

	refreshSuggestPostToggle();

	refreshAboutView();

	if (_topControls) {
		_topControls->finishAnimating();
	}

	updateBotKeyboard();
}

ChatWidget::~ChatWidget() {
	if (_inner) {
		_inner->setAboutView(nullptr);
	}
	_aboutView = nullptr;
	_suggestOptions = nullptr;
	_chooseTheme = nullptr;
	base::take(_sendAction);
	clearSupportPreloadRequest();
	_supportPreloadHistory = nullptr;
	if (_repliesRootId) {
		controller()->sendingAnimation().clear();
	}
	if (_subsectionTabs && !_subsectionTabs->dying()) {
		_subsectionTabsLifetime.destroy();
		controller()->saveSubsectionTabs(base::take(_subsectionTabs));
	}
	if (mode() == Mode::History) {
		if (!_topic) {
			session().sponsoredMessages().clearItems(_history);
		}
		auto state = ListMemento();
		_inner->saveState(&state);
		saveHistoryScrollState(state);
	}
	if (const auto reserved = base::take(_creatingBotTopic)) {
		if (reserved->creating()) {
			reserved->discard();
		}
	}
	if (_topic) {
		if (_topic->creating()) {
			if (controller()->activeChatCurrent().topic() == _topic) {
				controller()->setActiveChatEntry(Dialogs::Key());
			}
			_emptyPainter = nullptr;
			_topic->discard();
			_topic = nullptr;
		} else {
			_inner->saveState(_topic->listMemento());
		}
	}
	if (_repliesRootId) {
		_history->owner().sendActionManager().repliesPainterRemoved(
			_history,
			_repliesRootId);
	}
}

void ChatWidget::orderWidgets() {
	if (_topControls) {
		_topControls->raise();
	}
	if (_subsectionTabs) {
		_subsectionTabs->raise();
	}
	_topBar->raise();
	_topBarShadow->raise();
	if (_kbScroll) {
		_kbScroll->raise();
	}
	_bottom->raise();
	_composeControls->raisePanels();
	if (_membersDropdown) {
		_membersDropdown->raise();
	}
	if (_chooseTheme) {
		_chooseTheme->raise();
	}
}

void ChatWidget::setMembersShowAreaActive(bool active) {
	if (!active) {
		_membersDropdownShowTimer.cancel();
	}
	if (active && (_peer->isChat() || _peer->isMegagroup())) {
		if (_membersDropdown) {
			_membersDropdown->otherEnter();
		} else if (!_membersDropdownShowTimer.isActive()) {
			_membersDropdownShowTimer.callOnce(kShowMembersDropdownTimeoutMs);
		}
	} else if (_membersDropdown) {
		_membersDropdown->otherLeave();
	}
}

void ChatWidget::showMembersDropdown() {
	if (!(_peer->isChat() || _peer->isMegagroup())) {
		return;
	}
	if (!_membersDropdown) {
		_membersDropdown.create(this, st::membersInnerDropdown);
		_membersDropdown->setOwnedWidget(
			object_ptr<HistoryView::GroupMembersWidget>(
				this,
				controller(),
				_peer));
		_membersDropdown->resizeToWidth(st::membersInnerWidth);
		_membersDropdown->setHiddenCallback([this] {
			_membersDropdown.destroyDelayed();
		});
		orderWidgets();
	}
	_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	_membersDropdown->moveToLeft(0, _topBar->height());
	_membersDropdown->otherEnter();
}

int ChatWidget::countMembersDropdownHeightMax() const {
	auto result = height() - rect::m::sum::v(st::membersInnerDropdown.padding);
	result -= (_bottom->contentHeight() > 0)
		? _bottom->contentHeight()
		: _composeControls->heightCurrent();
	accumulate_min(result, st::membersInnerHeightMax);
	return result;
}

void ChatWidget::setupRoot() {
	if (_repliesRootId && !_repliesRoot) {
		requestMessageData(_repliesRootId);
	}
}

void ChatWidget::requestMessageData(MsgId msgId) {
	if (!msgId) {
		return;
	}
	const auto peer = _peer;
	const auto callback = crl::guard(this, [=] {
		messageDataReceived(peer, msgId);
	});
	session().api().requestMessageData(_peer, msgId, callback);
}

void ChatWidget::messageDataReceived(
		not_null<PeerData*> peer,
		MsgId msgId) {
	if ((_peer == peer)
		&& msgId
		&& (_repliesRootId == msgId)) {
		_repliesRoot = lookupRepliesRoot();
		_areComments = computeAreComments();
		if (_repliesRoot) {
			_inner->update();
		}
	}
	updatePinnedVisibility();
}

void ChatWidget::setupTopicViewer() {
	if (!_repliesRootId) {
		return;
	}
	const auto owner = &_history->owner();
	owner->itemIdChanged(
	) | rpl::on_next([=](const Data::Session::IdChange &change) {
		if (_repliesRootId == change.oldId) {
			_repliesRootId = _id.repliesRootId = change.newId.msg;
			resetRepliesKeyboardState();
			_sendAction = owner->sendActionManager().repliesPainter(
				_history,
				_repliesRootId);
			_repliesRoot = lookupRepliesRoot();
			if (_topic && _topic->rootId() == change.oldId) {
				setTopic(_topic->forum()->enforceTopicFor(change.newId.msg));
			} else {
				refreshResolvedTopicRootState();
				refreshReplies();
				refreshTopBarActiveChat();
				if (_topic) {
					_topControls->subscribeToPinnedMessages();
				}
				refreshCanSendMessages();
			}
			_inner->update();
		}
	}, lifetime());

	if (_topic) {
		subscribeToTopic();
	}
}

void ChatWidget::subscribeToTopic() {
	Expects(_topic != nullptr);

	using Flag = Data::TopicUpdate::Flag;
	session().changes().topicUpdates(
		_topic,
		(Flag::UnreadMentions
			| Flag::UnreadReactions
			| Flag::UnreadPollVotes
			| Flag::Closed
			| Flag::CloudDraft)
	) | rpl::on_next([=](const Data::TopicUpdate &update) {
		if (update.flags
			& (Flag::UnreadMentions
				| Flag::UnreadReactions
				| Flag::UnreadPollVotes)) {
			_cornerButtons.updateUnreadThingsVisibility();
		}
		if (update.flags & Flag::Closed) {
			refreshCanSendMessages();
		}
		if (update.flags & Flag::CloudDraft) {
			_composeControls->applyCloudDraft();
			refreshSuggestFromDraft();
		}
	}, _topicLifetime);

	_topic->destroyed(
	) | rpl::on_next([=] {
		closeCurrent();
	}, _topicLifetime);

	if (!_topic->creating()) {
		_topControls->subscribeToPinnedMessages();

		if (!_topic->creatorId()) {
			_topic->forum()->requestTopic(_topic->rootId());
		}
	}

	_cornerButtons.updateUnreadThingsVisibility();
}

void ChatWidget::closeCurrent() {
	const auto thread = controller()->windowId().chat();
	if ((_sublist && thread == _sublist) || (_topic && thread == _topic)) {
		controller()->window().close();
	} else {
		controller()->showBackFromStack(Window::SectionShow(
			anim::type::normal,
			anim::activation::background));
	}
}

void ChatWidget::setTopic(Data::ForumTopic *topic) {
	if (_topic == topic) {
		return;
	}
	_topicLifetime.destroy();
	_topic = topic;
	_pullToNext->setTopic(topic);
	if (_topControls) {
		_topControls->setTopic(topic);
	}
	_bottom->setTopic(topic);
	refreshResolvedTopicRootState();
	refreshReplies();
	refreshTopBarActiveChat();
	validateSubsectionTabs();
	if (_topic) {
		subscribeToTopic();
	}
	refreshCanSendMessages();
	if (_topic && emptyShown()) {
		setupEmptyPainter();
	} else {
		_emptyPainter = nullptr;
	}
}

HistoryItem *ChatWidget::lookupRepliesRoot() const {
	return _repliesRootId
		? _history->owner().message(_peer, _repliesRootId)
		: nullptr;
}

Data::ForumTopic *ChatWidget::lookupTopic() {
	if (!_repliesRootId) {
		return nullptr;
	} else if (const auto forum = _history->asForum()) {
		if (const auto result = forum->topicFor(_repliesRootId)) {
			return result;
		} else {
			forum->requestTopic(_repliesRootId, crl::guard(this, [=] {
				if (const auto forum = _history->asForum()) {
					setTopic(forum->topicFor(_repliesRootId));
				}
			}));
		}
	}
	return nullptr;
}

MsgId ChatWidget::resolveTopicRootId(const FullReplyTo &replyTo) const {
	if (!_peer->isForum()) {
		return replyTo.topicRootId;
	}
	const auto replyToMessage = (replyTo.messageId.peer == _peer->id)
		? session().data().message(replyTo.messageId)
		: nullptr;
	return replyToMessage
		? replyToMessage->topicRootId()
		: replyTo.topicRootId;
}

MsgId ChatWidget::resolvedTopicRootId() const {
	if (_repliesRootId) {
		return _repliesRootId;
	}
	const auto custom = _composeControls->replyingToMessage();
	if (custom.messageId || custom.topicRootId) {
		if (const auto result = resolveTopicRootId(custom)) {
			return result;
		}
	} else if (_kbReplyTo) {
		if (const auto result = resolveTopicRootId(FullReplyTo{
			.messageId = _kbReplyTo->fullId(),
			.topicRootId = _repliesRootId,
			.monoforumPeerId = _monoforumPeerId,
		})) {
			return result;
		}
	}
	return _repliesRootId
		? _repliesRootId
		: (_history->asForum() && !_history->peer->isBot())
		? Data::ForumTopic::kGeneralId
		: MsgId();
}

Data::ForumTopic *ChatWidget::resolvedTopic() {
	const auto rootId = resolvedTopicRootId();
	if (!rootId) {
		return nullptr;
	} else if (_topic && (_topic->rootId() == rootId)) {
		return _topic;
	}
	const auto forum = _history->asForum();
	if (!forum) {
		return nullptr;
	}
	return forum->enforceTopicFor(rootId);
}

void ChatWidget::refreshCanSendMessages() {
	_canSendMessagesLifetime.destroy();
	const auto apply = [=](bool can) {
		const auto changed = (_canSendMessages != can);
		_canSendMessages = can;
		_bottom->setCanSendMessages(can);
		if (changed) {
			updateControlsVisibility();
		}
	};
	if (const auto topic = resolvedTopic()) {
		Data::CanSendAnythingValue(
			topic
		) | rpl::on_next(std::move(apply), _canSendMessagesLifetime);
	} else {
		Data::CanSendAnythingValue(
			_peer
		) | rpl::on_next(std::move(apply), _canSendMessagesLifetime);
	}
}

void ChatWidget::refreshResolvedTopicRootState() {
	const auto topicRootId = resolvedTopicRootId();
	if (_topControls) {
		_topControls->setRepliesRootId((_topic || _repliesRootId)
			? topicRootId
			: MsgId());
	}
	_composeControls->updateTopicRootId(topicRootId);
}

void ChatWidget::setKeyboardReplyTo(HistoryItem *item) {
	if (_kbReplyTo == item) {
		return;
	}
	_kbReplyTo = item;
	refreshResolvedTopicRootState();
	refreshCanSendMessages();
	_kbReplyToChanges.fire({});
}

bool ChatWidget::computeAreComments() const {
	return _repliesRoot && _repliesRoot->isDiscussionPost();
}

void ChatWidget::setupComposeControls() {
	auto replyThreadChanges = rpl::merge(
		_composeControls->replyingToMessageValue() | rpl::to_empty,
		_kbReplyToChanges.events() | rpl::to_empty);
	auto topicClosedChanges = rpl::single(
	) | rpl::then(session().changes().topicUpdates(
		Data::TopicUpdate::Flag::Closed
	) | rpl::filter([=](const Data::TopicUpdate &update) {
		return (update.topic->history() == _history)
			&& (update.topic->rootId() == resolvedTopicRootId());
	}) | rpl::to_empty);
	auto canSendTexts = rpl::combine(
		session().changes().peerFlagsValue(
			_peer,
			Data::PeerUpdate::Flag::Rights),
		Data::CanSendAnythingValue(_peer),
		rpl::duplicate(replyThreadChanges),
		rpl::duplicate(topicClosedChanges)
	) | rpl::map([=](auto, auto, auto, auto) {
		const auto topic = resolvedTopic();
		return topic
			? Data::CanSend(topic, ChatRestriction::SendOther)
			: Data::CanSend(_peer, ChatRestriction::SendOther);
	});
	auto writeRestriction = rpl::combine(
		session().frozenValue(),
		session().changes().peerFlagsValue(
			_peer,
			Data::PeerUpdate::Flag::Rights),
		Data::CanSendAnythingValue(_peer),
		std::move(replyThreadChanges),
		std::move(topicClosedChanges)
	) | rpl::map([=](
			const Main::FreezeInfo &info,
			auto,
			auto,
			auto,
			auto) {
		if (_bottom->isButtonActive()) {
			return Controls::WriteRestriction();
		}
		if (info) {
			return Controls::WriteRestriction{
				.type = Controls::WriteRestrictionType::Frozen,
			};
		}
		const auto allWithoutPolls = Data::AllSendRestrictions()
			& ~ChatRestriction::SendPolls;
		const auto topic = resolvedTopic();
		const auto canSendAnything = topic
			? Data::CanSendAnyOf(topic, allWithoutPolls)
			: Data::CanSendAnyOf(_peer, allWithoutPolls);
		const auto channel = _peer->asChannel();
		if ((mode() == Mode::History)
			&& !canSendAnything
			&& _peer->amMonoforumAdmin()
			&& channel
			&& !channel->monoforumDisabled()) {
			return Controls::WriteRestriction{
				.text = tr::lng_monoforum_choose_to_reply(tr::now),
				.type = Controls::WriteRestrictionType::Rights,
			};
		}
		auto topicRestriction = (!resolvedTopicRootId()
			|| !topic
			|| topic->canToggleClosed()
			|| !topic->closed())
			? Data::SendError()
			: tr::lng_forum_topic_closed(tr::now);
		const auto restriction = Data::RestrictionError(
			_peer,
			ChatRestriction::SendOther);
		auto text = !canSendAnything
			? (restriction
				? restriction
				: topicRestriction
				? std::move(topicRestriction)
				: tr::lng_group_not_accessible(tr::now))
			: topicRestriction
			? std::move(topicRestriction)
			: Data::SendError();
		return text ? Controls::WriteRestriction{
			.text = std::move(*text),
			.type = Controls::WriteRestrictionType::Rights,
			.boostsToLift = text.boostsToLift,
		} : Controls::WriteRestriction();
	});

	_composeControls->setHistory({
		.history = _history.get(),
		.topicRootId = resolvedTopicRootId(),
		.monoforumPeerId = _monoforumPeerId,
		.showSlowmodeError = [=] { return showSlowmodeError(); },
		.showScheduleSendError = [=] { return showScheduleSendError(); },
		.sendActionFactory = [=] { return prepareSendAction({}); },
		.sendWithText = [=](
				TextWithEntities &&text,
				Api::SendOptions options,
				Fn<void()> done) {
			sendWithTextOverride(std::move(text), options, std::move(done));
		},
		.slowmodeSecondsLeft = SlowmodeSecondsLeft(_peer),
		.sendDisabledBySlowmode = SendDisabledBySlowmode(_peer),
		.writeRestriction = std::move(writeRestriction),
		.canSendTexts = std::move(canSendTexts),
	});

	_composeControls->height(
	) | rpl::filter([=] {
		return !_bottom->isButtonActive();
	}) | rpl::on_next([=] {
		const auto wasMax = (_scroll->scrollTop() >= _scroll->scrollTopMax());
		updateControlsGeometry();
		if (wasMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	}, lifetime());

	_composeControls->cancelRequests(
	) | rpl::on_next([=] {
		listCancelRequest();
	}, lifetime());

	_composeControls->replyingToMessageValue(
	) | rpl::skip(1) | rpl::on_next([=](FullReplyTo) {
		refreshCanSendMessages();
		updateBotKeyboard();
	}, lifetime());

	_composeControls->editMsgIdValue(
	) | rpl::skip(1) | rpl::on_next([=](FullMsgId) {
		updateBotKeyboard();
	}, lifetime());

	_composeControls->replyCancelledExternal(
	) | rpl::on_next([=] {
		if (_ignoreReplyCancelledExternal
			|| !_keyboardReplyExternalVisible) {
			return;
		}
		toggleBotKeyboard(true);
	}, lifetime());

	_composeControls->sendRequests(
	) | rpl::on_next([=](Api::SendOptions options) {
		send(options);
	}, lifetime());

	_composeControls->scrollToMaxRequests(
	) | rpl::on_next([=] {
		send({});
	}, lifetime());

	_composeControls->sendVoiceRequests(
	) | rpl::on_next([=](const ComposeControls::VoiceToSend &data) {
		sendVoice(data);
	}, lifetime());

	_composeControls->sendCommandRequests(
	) | rpl::on_next([=](const QString &command) {
		sendBotCommand({
			.peer = _peer,
			.command = command,
			.replyTo = replyTo(),
		}, {});
		session().api().finishForwarding(prepareSendAction({}));
	}, lifetime());

	const auto saveEditMsgRequestId = lifetime().make_state<mtpRequestId>(0);
	_composeControls->editRequests(
	) | rpl::on_next([=](auto data) {
		if (const auto item = session().data().message(data.fullId)) {
			const auto spoiler = data.spoilered;
			edit(
				item,
				data.options,
				saveEditMsgRequestId,
				spoiler,
				data.videoCover);
		}
	}, lifetime());

	_composeControls->editMsgIdValue(
	) | rpl::filter([=](FullMsgId value) {
		return !value && (*saveEditMsgRequestId != 0);
	}) | rpl::on_next([=](FullMsgId) {
		session().api().request(
			base::take(*saveEditMsgRequestId)).cancel();
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::on_next([=](std::optional<bool> overrideCompress) {
		_choosingAttach = true;
		base::call_delayed(
			st::historyAttach.ripple.hideDuration,
			this,
			[=] { chooseAttach(overrideCompress); });
	}, lifetime());

	_composeControls->setSendAsFileConfirmed(crl::guard(this, [=](
			std::shared_ptr<Ui::PreparedBundle> bundle,
			Api::SendOptions options) {
		sendingFilesConfirmed(std::move(bundle), options);
	}));

	_composeControls->fileChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		controller()->hideLayer(anim::type::normal);
		controller()->sendingAnimation().appendSending(
			data.messageSendingFrom);
		auto messageToSend = Api::MessageToSend(
			prepareSendAction(data.options));
		messageToSend.textWithTags = base::take(data.caption);
		sendExistingDocument(
			data.document,
			std::move(messageToSend),
			data.messageSendingFrom.localId);
	}, lifetime());

	_composeControls->photoChosen(
	) | rpl::on_next([=](ChatHelpers::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo, chosen.options);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::on_next([=](ChatHelpers::InlineChosen chosen) {
		controller()->sendingAnimation().appendSending(
			chosen.messageSendingFrom);
		const auto localId = chosen.messageSendingFrom.localId;
		sendInlineResult(chosen.result, chosen.bot, chosen.options, localId);
	}, lifetime());

	_composeControls->jumpToItemRequests(
	) | rpl::on_next([=](FullReplyTo to) {
		if (const auto item = session().data().message(to.messageId)) {
			JumpToMessageClickHandler(item, {}, to.highlight())->onClick({});
		}
	}, lifetime());

	rpl::merge(
		_composeControls->scrollKeyEvents(),
		_inner->scrollKeyEvents()
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		if (e->key() == Qt::Key_Up
			&& !_bottom->isButtonActive()
			&& !_composeControls->isEditingMessage()
			&& !_composeControls->replyingToMessage().replying()) {
			const auto field = _composeControls->fieldForMention();
			if (field
				&& field->empty()
				&& _inner->lastMessageEditRequestNotify()) {
				return;
			}
		}
		_scroll->keyPressEvent(e);
	}, lifetime());

	_composeControls->editLastMessageRequests(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		if (!_inner->lastMessageEditRequestNotify()) {
			_scroll->keyPressEvent(e);
		}
	}, lifetime());

	_composeControls->replyNextRequests(
	) | rpl::on_next([=](ComposeControls::ReplyNextRequest &&data) {
		if (_composeControls->isEditingMessage()
			|| (!_topic && _history->isForum())) {
			return;
		}
		const auto reply = _composeControls->replyingToMessage();
		if (reply.messageId && reply.messageId.peer != _peer->id) {
			return;
		}
		using Direction = ComposeControls::ReplyNextRequest::Direction;
		_inner->replyNextMessage(
			data.replyId,
			data.direction == Direction::Next);
	}, lifetime());

	_composeControls->showScheduledRequests(
	) | rpl::on_next([=] {
		controller()->showSection(
			_topic
				? std::make_shared<HistoryView::ScheduledMemento>(_topic)
				: std::make_shared<HistoryView::ScheduledMemento>(_history));
	}, lifetime());

	_composeControls->suggestPostToggleClicks(
	) | rpl::on_next([=] {
		applySuggestOptions(
			{ .exists = 1 },
			SuggestMode::New);
		_composeControls->cancelReplyMessage();
	}, lifetime());

	_composeControls->botKeyboardToggleClicks(
	) | rpl::on_next([=] {
		toggleBotKeyboard(true);
	}, lifetime());

	_composeControls->hasSendTextValue(
	) | rpl::on_next([=](bool has) {
		const auto had = _fieldHasSendText;
		if (had == has) {
			return;
		}
		_fieldHasSendText = has;
		if (!had && has && _kbShown && keyboardRowsVisible()) {
			toggleBotKeyboard(true);
		} else {
			updateBotKeyboard();
		}
	}, lifetime());

	_composeControls->setPasteToastParent(_scroll.get());
	_composeControls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return Core::CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(
				data,
				std::nullopt,
				Core::ReadMimeText(data));
		}
		Unexpected("action in MimeData hook.");
	});

	_composeControls->lockShowStarts(
	) | rpl::on_next([=] {
		_cornerButtons.updateJumpDownVisibility();
		_cornerButtons.updateUnreadThingsVisibility();
	}, lifetime());

	_composeControls->viewportEvents(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());

	_composeControls->finishAnimating();
}

void ChatWidget::setupSwipeReplyAndBack() {
	const auto can = [=](not_null<HistoryItem*> still) {
		const auto canSendReply = CanSendResolved(
			_peer,
			resolvedTopic(),
			SendPermission::Anything);
		const auto allowInAnotherChat = still && still->allowsForward();
		const auto bottomBarActive = _bottom->isButtonActive();
		if (allowInAnotherChat && (bottomBarActive || !canSendReply)) {
			return true;
		} else if (!bottomBarActive && canSendReply) {
			return true;
		}
		return false;
	};

	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation > 0) {
			if (!_swipeBackData.callback) {
				_swipeBackData = Ui::Controls::SetupSwipeBack(
					this,
					[=]() -> std::pair<QColor, QColor> {
						const auto context = listPreparePaintContext({
							.theme = listChatTheme(),
						});
						return {
							context.st->msgServiceBg()->c,
							context.st->msgServiceFg()->c,
						};
					});
			}
			_swipeBackData.callback(data);
			return;
		} else if (_swipeBackData.lifetime) {
			_swipeBackData = {};
		}
		const auto changed = (_gestureHorizontal.msgBareId != data.msgBareId)
			|| (_gestureHorizontal.translation != data.translation)
			|| (_gestureHorizontal.reachRatio != data.reachRatio);
		if (changed) {
			_gestureHorizontal = data;
			const auto item = _peer->owner().message(
				_peer->id,
				MsgId{ data.msgBareId });
			if (item) {
				_history->owner().requestItemRepaint(item);
			}
		}
	};

	auto init = [=, show = controller()->uiShow()](
			Ui::Controls::SwipeHandlerInitData data) {
		auto result = Ui::Controls::SwipeHandlerFinishData();
		const auto horizontalScrollDelta = (data.direction == Qt::LeftToRight)
			? 1
			: -1;
		if (_inner->canConsumeHorizontalScroll(
				data.cursorPosition,
				horizontalScrollDelta)) {
			return result;
		}
		if (data.direction == Qt::RightToLeft) {
			if (_inner->hasVisibleSimilarChannels()) {
				return result;
			}
			return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
				controller()->showBackFromStack();
			});
		}
		if (_inner->elementInSelectionMode(nullptr).inSelectionMode) {
			return result;
		}
		const auto view = _inner->lookupItemByY(data.cursorPosition.y());
		if (!view
			|| (!view->data()->isRegular()
				&& (!view->data()->isEphemeral()
					|| view->data()->out()))
			|| view->data()->showSimilarChannels()
			|| view->data()->isService()) {
			return result;
		}
		const auto item = _inner->lookupItemByPoint(
			data.cursorPosition,
			view);
		if (!can(item)) {
			return result;
		}

		_inner->hideElementOverlay();
		const auto viewItemId = view->data()->fullId();
		const auto itemId = item->fullId();
		result.msgBareId = viewItemId.msg.bare;
		result.callback = [=] {
			const auto still = show->session().data().message(viewItemId);
			const auto view = still
				? _inner->viewByPosition(still->position())
				: nullptr;
			const auto selected = (still && view)
				? view->selectedQuote(_inner->getSelectedTextSelection(still))
				: SelectedQuote();
			const auto exact = selected.item
				? selected.item
				: show->session().data().message(itemId);
			if (!exact) {
				return;
			}
			Window::ActivateWindow(controller());
			_inner->replyToMessageRequestNotify({
				.messageId = exact->fullId(),
				.quote = selected.highlight.quote,
				.quoteOffset = selected.highlight.quoteOffset,
				.todoItemId = selected.highlight.todoItemId,
				.pollOption = selected.highlight.pollOption,
			});
		};
		return result;
	};

	Ui::Controls::SetupSwipeHandler({
		.widget = _inner,
		.scroll = _scroll.get(),
		.update = std::move(update),
		.init = std::move(init),
		.dontStart = _inner->touchMaybeSelectingValue(),
		.skipWheelEvent = [=](not_null<QWheelEvent*> event) {
			const auto delta = Ui::ScrollDelta(event);
			if (std::abs(delta.x()) <= std::abs(delta.y())) {
				return false;
			}
			return _inner->canConsumeHorizontalScroll(
				_inner->mapFromGlobal(event->globalPosition().toPoint()),
				delta.x());
		},
	});
}

void ChatWidget::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	_choosingAttach = false;
	if (!session().ephemeralMessages().isEphemeralBotReply(
			replyTo().messageId)) {
		if (const auto error = Data::AnyFileRestrictionError(_peer)) {
			Data::ShowSendErrorToast(controller(), _peer, error);
			return;
		} else if (showSlowmodeError()) {
			return;
		}
	}

	const auto filter = (overrideSendImagesAsPhotos == true)
		? FileDialog::PhotoVideoFilesFilter()
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

bool ChatWidget::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (_composeSearch) {
		_composeSearch->hideAnimated();
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

bool ChatWidget::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (_composeControls->confirmMediaEdit(list)) {
		return true;
	} else if (showSendingFilesError(list)) {
		return false;
	}

	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		_composeControls->getTextWithAppliedMarkdown(),
		_peer,
		Api::SendType::Normal,
		sendMenuDetails());
	box->setReplyTo(_composeControls->replyingToMessage());

	box->setConfirmedCallback(crl::guard(this, [=](
			std::shared_ptr<Ui::PreparedBundle> bundle,
			Api::SendOptions options,
			FullReplyTo currentReplyTo) {
		if (!currentReplyTo.messageId
				&& _composeControls->replyingToMessage().messageId) {
			_composeControls->cancelReplyMessage();
		}
		sendingFilesConfirmed(std::move(bundle), options);
	}));
	box->setCancelledCallback(_composeControls->restoreTextCallback(
		insertTextOnCancel));
	box->takeTextWithTagsRequests() | rpl::on_next([=](TextWithTags &&text) {
		_composeControls->setText(std::move(text));
	}, box->lifetime());

	Window::ActivateWindow(controller());
	controller()->show(std::move(box));

	return true;
}

bool ChatWidget::checkSendPayment(
		int messagesCount,
		Api::SendOptions options,
		Fn<void(int)> withPaymentApproved) {
	return _sendPayment.check(
		controller(),
		_peer,
		options,
		messagesCount,
		std::move(withPaymentApproved));
}

void ChatWidget::sendingFilesConfirmed(
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options) {
	if (showSendingFilesError(*bundle)) {
		return;
	}
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	if (bundle->totalCount > 1 && ephemeralReply) {
		controller()->showToast(
			tr::lng_ephemeral_reply_single_message(tr::now));
		return;
	}

	auto action = prepareSendAction(options);
	action.clearDraft = false;
	if (!ephemeralReply) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendingFilesConfirmed(bundle, copy);
		};
		const auto checked = checkSendPayment(
			bundle->totalCount,
			action.options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}

	const auto compress = bundle->way.sendImagesAsPhotos();
	const auto type = compress ? SendMediaType::Photo : SendMediaType::File;
	auto &api = session().api();
	for (auto &group : bundle->groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		api.sendFiles(std::move(group.list), type, album, action);
	}
	if (_composeControls->replyingToMessage().messageId
			== action.replyTo.messageId) {
		_composeControls->cancelReplyMessage();
		refreshTopBarActiveChat();
	}
	finishSending();
}

bool ChatWidget::confirmSendingFiles(
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

bool ChatWidget::showSlowmodeError() {
	const auto text = [&] {
		if (const auto left = _peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWordsSlowmode(left));
		} else if (_peer->slowmodeApplied()) {
			if (const auto item = _history->latestSendingMessage()) {
				showAtPosition(item->position());
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

bool ChatWidget::showScheduleSendError() {
	if (!_canSendMessages) {
		return false;
	}
	const auto richPage = _composeControls->shownRichMessage();
	const auto richMessage = (richPage != nullptr);
	const auto text = _composeControls->getTextWithAppliedMarkdown();
	auto request = SendingErrorRequest{
		.topicRootId = resolvedTopicRootId(),
		.forward = &_composeControls->forwardItems(),
		.text = richMessage ? nullptr : &text,
		.ignoreSlowmodeCountdown = true,
		.richMessage = richMessage,
	};
	request.messagesCount = ComputeSendingMessagesCount(_history, request);
	const auto error = GetErrorForSending(_peer, request);
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return true;
	}
	return false;
}

void ChatWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	if (_repliesRootId) {
		if (item->history() == _history && item->inThread(_repliesRootId)) {
			_cornerButtons.pushReplyReturn(item);
		}
	}
}

void ChatWidget::checkReplyReturns() {
	const auto currentTop = _scroll->scrollTop();
	while (const auto replyReturn = _cornerButtons.replyReturn()) {
		const auto position = replyReturn->position();
		const auto scrollTop = _inner->scrollTopForPosition(position);
		const auto below = scrollTop
			? (currentTop >= std::min(*scrollTop, _scroll->scrollTopMax()))
			: _inner->isBelowPosition(position);
		if (below) {
			_cornerButtons.calculateNextReplyReturn();
		} else {
			break;
		}
	}
}

void ChatWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	session().api().sendFile(fileContent, type, prepareSendAction({}));
}

bool ChatWidget::showSendingFilesError(
		const Ui::PreparedList &list) const {
	const auto show = controller()->uiShow();
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	return Data::ShowSendError(
		show,
		_peer,
		list,
		std::nullopt,
		false,
		ephemeralReply);
}

bool ChatWidget::showSendingFilesError(
		const Ui::PreparedBundle &bundle) const {
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	return Data::ShowSendError(
		controller()->uiShow(),
		_peer,
		bundle,
		false,
		ephemeralReply);
}

Api::SendAction ChatWidget::prepareSendAction(
		Api::SendOptions options) const {
	auto result = Api::SendAction(_history, options);
	result.replyTo = replyTo();

	if (mode() == Mode::History) {
		if (const auto forum = _history->asForum()) {
			if (forum->bot()
				&& Data::IsBotUserCreatesTopics(_history->peer)) {
				const auto readyRootId = [&]() -> MsgId {
					if (const auto id = result.replyTo.messageId) {
						if (const auto item = session().data().message(id)) {
							return item->topicRootId();
						}
					}
					return {};
				}();
				if (readyRootId) {
					result.replyTo.topicRootId = readyRootId;
				} else {
					if (!_creatingBotTopic) {
						_creatingBotTopic = forum->reserveNewBotTopic();
						auto draft = _history->forwardDraft(MsgId(0), PeerId());
						if (!draft.ids.empty()) {
							_history->setForwardDraft(MsgId(0), PeerId(), {});
							_history->setForwardDraft(
								_creatingBotTopic->rootId(),
								PeerId(),
								std::move(draft));
						}
					}
					result = Api::SendAction(_creatingBotTopic, options);
					result.replyTo.topicRootId = _creatingBotTopic->rootId();
				}
			}
		}
	}

	result.options.sendAs = _composeControls->sendAsPeer();
	result.options.suggest = suggestOptions();
	result.clearDraft = !Iv::Editor::IsComposeBoxOpen(
		&session(),
		_peer->id,
		_repliesRootId,
		_monoforumPeerId);
	return result;
}

void ChatWidget::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		if (const auto page = _composeControls->shownRichMessage()) {
			sendRichDraft(page, {});
		}
		return;
	}
	send({});
}

void ChatWidget::sendVoice(const ComposeControls::VoiceToSend &data) {
	const auto withPaymentApproved = [=](int approved) {
		auto copy = data;
		copy.options.starsApproved = approved;
		sendVoice(copy);
	};
	auto action = prepareSendAction(data.options);
	const auto checked = checkSendPayment(
		1 + int(_composeControls->forwardItems().size()),
		action.options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	session().api().sendVoiceMessage(
		data.bytes,
		data.waveform,
		data.duration,
		data.video,
		std::move(action));

	_composeControls->cancelReplyMessage();
	_composeControls->clearListenState();
	finishSending();
}

void ChatWidget::send(Api::SendOptions options) {
	if (const auto page = _composeControls->shownRichMessage()) {
		sendRichDraft(page, options);
		return;
	}
	if (!options.scheduled) {
		auto message = Api::MessageToSend(prepareSendAction(options));
		message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
		if (!session().ephemeralMessages().wouldSend(message)
			&& showSlowmodeError()) {
			return;
		}
	}

	sendTextWithTags(
		_composeControls->getTextWithAppliedMarkdown(),
		true,
		options,
		nullptr);
}

void ChatWidget::supportInitAutocomplete() {
	_supportAutocomplete->hide();

	_supportAutocomplete->insertRequests(
	) | rpl::on_next([=](const QString &text) {
		supportInsertText(text);
	}, _supportAutocomplete->lifetime());

	_supportAutocomplete->shareContactRequests(
	) | rpl::on_next([=](const Support::Contact &contact) {
		supportShareContact(contact);
	}, _supportAutocomplete->lifetime());
}

void ChatWidget::supportInsertText(const QString &text) {
	_composeControls->insertTextToField(text);
}

void ChatWidget::supportShareContact(Support::Contact contact) {
	supportInsertText(contact.comment);
	contact.comment = _composeControls->fieldLastText();

	const auto submit = [=](Qt::KeyboardModifiers modifiers) {
		auto options = Api::SendOptions{
			.sendAs = prepareSendAction({}).options.sendAs,
		};
		auto action = Api::SendAction(_history);
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
	) | rpl::on_next([=] {
		_composeControls->undoFieldChange();
	}, lifetime());
}

void ChatWidget::sendRichDraft(
		std::shared_ptr<const Iv::RichPage> page,
		Api::SendOptions options) {
	if (!page) {
		return;
	}
	const auto ephemeral = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	if (ephemeral && options.scheduled) {
		controller()->showToast(tr::lng_ephemeral_cant_schedule(tr::now));
		return;
	}
	if (!options.scheduled) {
		_cornerButtons.clearReplyReturns();
		if (!ephemeral && showSlowmodeError()) {
			return;
		}
	}

	auto request = SendingErrorRequest{
		.topicRootId = resolvedTopicRootId(),
		.forward = &_composeControls->forwardItems(),
		.messagesCount = 1,
		.ignoreSlowmodeCountdown = (options.scheduled != 0),
		.richMessage = true,
		.ignoreRestrictions = ephemeral,
	};
	request.messagesCount = ComputeSendingMessagesCount(_history, request);
	const auto error = GetErrorForSending(_peer, request);
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return;
	}

	const auto serialized = Iv::SerializeInputRichMessage(
		&session(),
		*page,
		Iv::SerializeInputRichMessageMode::FinalSubmit);
	if (serialized.status == Iv::SerializeInputRichMessageStatus::EmptyContent) {
		controller()->showToast(tr::lng_article_submit_empty(tr::now));
		return;
	} else if (serialized.status != Iv::SerializeInputRichMessageStatus::Success
		|| !serialized.value) {
		controller()->showToast(tr::lng_attach_failed(tr::now));
		return;
	}
	if (!session().premium()
		&& Iv::RichPageUsesPremiumFormatting(*page)) {
		if (Iv::RichPageIsFlattenSafe(*page)) {
			const auto weak = base::make_weak(this);
			Iv::Editor::OfferRichMessagePremiumChoice(
				controller()->uiShow(),
				&session(),
				*page,
				[=] {
					if (const auto strong = weak.get()) {
						strong->sendRichDraftWithoutFormatting(
							page,
							options);
					}
				});
		} else {
			Iv::Editor::ShowRichMessagesPremiumToast(
				controller()->uiShow());
		}
		return;
	}
	auto action = prepareSendAction(options);
	if (!options.scheduled && !ephemeral) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendRichDraft(page, copy);
		};
		const auto checked = checkSendPayment(
			request.messagesCount,
			action.options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}

	session().api().sendRichMessage(
		page,
		*serialized.value,
		action);

	_composeControls->clear();
	_composeControls->applyCloudDraft();
	session().sendProgressManager().update(
		_history,
		_repliesRootId,
		Api::SendProgressType::Typing,
		-1);
	finishSending();
}

void ChatWidget::sendRichDraftWithoutFormatting(
		std::shared_ptr<const Iv::RichPage> page,
		Api::SendOptions options) {
	if (!page) {
		return;
	}
	const auto flattened = Iv::FlattenRichPageToSimpleText(*page);
	sendTextWithTags(
		{
			flattened.text,
			TextUtilities::ConvertEntitiesToTextTags(flattened.entities),
		},
		false,
		options,
		nullptr);
	_composeControls->applyCloudDraft();
}

void ChatWidget::sendTextWithTags(
		TextWithTags textWithTags,
		bool useCurrentWebPageDraft,
		Api::SendOptions options,
		Fn<void()> done) {
	if (!options.scheduled) {
		_cornerButtons.clearReplyReturns();
	}

	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = textWithTags;
	if (useCurrentWebPageDraft) {
		message.webPage = _composeControls->webPageDraft();
	}
	if (options.scheduled
		&& session().ephemeralMessages().wouldSend(message)) {
		controller()->showToast(tr::lng_ephemeral_cant_schedule(tr::now));
		return;
	}

	const auto ephemeral = session().ephemeralMessages().wouldSend(message);
	auto request = SendingErrorRequest{
		.topicRootId = resolvedTopicRootId(),
		.forward = &_composeControls->forwardItems(),
		.text = &message.textWithTags,
		.ignoreSlowmodeCountdown = (options.scheduled != 0),
		.ignoreRestrictions = ephemeral,
	};
	request.messagesCount = ComputeSendingMessagesCount(_history, request);
	if (_canSendMessages) {
		const auto error = GetErrorForSending(_peer, request);
		if (error) {
			Data::ShowSendErrorToast(controller(), _peer, error);
			return;
		}
		if (!ephemeral) {
			const auto withPaymentApproved = [=](int approved) {
				auto copy = options;
				copy.starsApproved = approved;
				sendTextWithTags(
					textWithTags,
					useCurrentWebPageDraft,
					copy,
					done);
			};
			const auto checked = checkSendPayment(
				request.messagesCount,
				message.action.options,
				withPaymentApproved);
			if (!checked) {
				return;
			}
		}
	}

	const auto nextLocalMessageId = session().data().nextLocalMessageId();
	const auto hasText = !message.textWithTags.text.trimmed().isEmpty();

	if (const auto field = _composeControls->fieldForMention(); field
		&& hasText
		&& message.webPage.url.isEmpty()
		&& (field->document()->size().height() <= field->height())) {
		controller()->sendingAnimation().appendSending({
			.type = Ui::MessageSendingAnimationFrom::Type::Text,
			.localId = nextLocalMessageId,
			.globalStartGeometry = field->mapToGlobal(
				Rect(field->size())),
		});
	}

	const auto justMarkingAsRead = !hasText
		&& message.webPage.url.isEmpty();
	_justMarkingAsRead = justMarkingAsRead;
	session().api().sendMessage(std::move(message), nextLocalMessageId);
	_justMarkingAsRead = false;

	_composeControls->clear(justMarkingAsRead);
	session().sendProgressManager().update(
		_history,
		_repliesRootId,
		Api::SendProgressType::Typing,
		-1);

	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	finishSending();
	if (done) {
		done();
	}
}

void ChatWidget::sendWithTextOverride(
		TextWithEntities text,
		Api::SendOptions options,
		Fn<void()> done) {
	const auto useCurrentWebPageDraft
		= (text.text == _composeControls->prepareTextForEditMsg().text);
	sendTextWithTags({
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	}, useCurrentWebPageDraft, options, std::move(done));
}

void ChatWidget::edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId,
		bool spoilered,
		Api::VideoCoverEdit videoCover) {
	if (*saveEditMsgRequestId) {
		return;
	}
	const auto webpage = _composeControls->webPageDraft();
	const auto sending = _composeControls->prepareTextForEditMsg();

	const auto hasMediaWithCaption = item
		&& item->media()
		&& item->media()->allowsEditCaption();
	if (sending.text.isEmpty()
		&& (webpage.removed
			|| webpage.url.isEmpty()
			|| !webpage.manual)
		&& !hasMediaWithCaption) {
		if (item->computeSuggestionActions() == SuggestionActions::None) {
			controller()->show(Box<DeleteMessagesBox>(item));
		}
		return;
	} else {
		const auto limits = Data::PremiumLimits(&session());
		const auto maxTextSize = hasMediaWithCaption
			? limits.captionLengthCurrent()
			: limits.messageLengthCurrent();
		const auto remove = _composeControls->fieldCharacterCount()
			- maxTextSize;
		if (remove > 0) {
			controller()->showToast(
				tr::lng_edit_limit_reached(tr::now, lt_count, remove));
			return;
		}
	}

	const auto weak = base::make_weak(this);
	const auto history = _history;
	const auto editingId = item->fullId();
	const auto topicRootId = resolvedTopicRootId();
	const auto monoforumPeerId = _monoforumPeerId;
	const auto clearEditDraft = [=] {
		const auto draft = history->localEditDraft(
			topicRootId,
			monoforumPeerId);
		if (draft && draft->reply.messageId == editingId) {
			history->clearLocalEditDraft(topicRootId, monoforumPeerId);
			history->session().local().writeDrafts(history);
		}
	};

	const auto done = [=](mtpRequestId requestId) {
		crl::guard(weak, [=] {
			if (requestId == *saveEditMsgRequestId) {
				*saveEditMsgRequestId = 0;
				_composeControls->cancelEditMessage();
			}
		})();
		clearEditDraft();
	};

	const auto fail = [=](const QString &error, mtpRequestId requestId) {
		crl::guard(weak, [=] {
			if (requestId == *saveEditMsgRequestId) {
				*saveEditMsgRequestId = 0;
			}

			if (ranges::contains(Api::kDefaultEditMessagesErrors, error)) {
				controller()->showToast(tr::lng_edit_error(tr::now));
			} else if (error == u"MESSAGE_NOT_MODIFIED"_q) {
				_composeControls->cancelEditMessage();
			} else if (error == u"MESSAGE_EMPTY"_q) {
				_composeControls->selectAllFieldText();
				doSetInnerFocus();
			} else {
				controller()->showToast(tr::lng_edit_error(tr::now));
			}
			update();
		})();
		return true;
	};

	if (item->computeSuggestionActions()
		== SuggestionActions::AcceptAndDecline) {
		const auto fullId = item->fullId();
		const auto withPaymentApproved = [=](int approved) {
			if (const auto item = session().data().message(fullId)) {
				auto copy = options;
				copy.starsApproved = approved;
				edit(item, copy, saveEditMsgRequestId, spoilered, videoCover);
			}
		};
		const auto checked = checkSendPayment(
			1 + int(_composeControls->forwardItems().size()),
			options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}

	// Not guarded by 'this': 'done' and 'fail' check the weak pointer
	// themselves and still clear the local edit draft if we're already gone.
	*saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		webpage,
		options,
		done,
		fail,
		spoilered,
		videoCover);

	_composeControls->hidePanelsAnimated();
	doSetInnerFocus();
}

void ChatWidget::validateSubsectionTabs() {
	if (!_subsectionCheckLifetime) {
		if (const auto group = _history->peer->asMegagroup()) {
			_subsectionCheckLifetime = group->flagsValue(
			) | rpl::skip(
				1
			) | rpl::filter([=](Data::Flags<ChannelDataFlags>::Change change) {
				const auto mask = ChannelDataFlag::Forum
					| ChannelDataFlag::ForumTabs
					| ChannelDataFlag::MonoforumAdmin;
				return change.diff & mask;
			}) | rpl::on_next([=] {
				validateSubsectionTabs();
			});
		} else if (!_topic) {
			if (const auto user = _history->peer->asBot()) {
				_subsectionCheckLifetime = user->flagsValue(
				) | rpl::skip(
					1
				) | rpl::filter([=](Data::Flags<UserDataFlags>::Change change) {
					return change.diff & UserDataFlag::Forum;
				}) | rpl::on_next([=] {
					_subsectionTopicsLifetime.destroy();
					validateSubsectionTabs();
				});
			}
		}
	}
	if (!_subsectionTopicsLifetime && !_topic) {
		if (const auto user = _history->peer->asBot()) {
			if (const auto forum = user->forum()) {
				_subsectionTopicsLifetime = forum->topicsList()->fullSize().value(
				) | rpl::map([](int size) {
					return size > 0;
				}) | rpl::distinct_until_changed(
				) | rpl::skip(
					1
				) | rpl::on_next([=] {
					validateSubsectionTabs();
				});
			}
		}
	}
	const auto thread = _topic
		? (Data::Thread*)_topic
		: _sublist
		? (Data::Thread*)_sublist
		: (mode() == Mode::History)
		? (Data::Thread*)_history.get()
		: nullptr;
	if (!thread || !HistoryView::SubsectionTabs::UsedFor(_history)) {
		if (_subsectionTabs) {
			_subsectionTabsLifetime.destroy();
			_subsectionTabs = nullptr;
			updateControlsGeometry();

			if (const auto forum = _history->asForum()
				; forum && !_history->peer->isUser()) {
				controller()->showForum(forum, {
					Window::SectionShow::Way::Backward,
					anim::type::normal,
					anim::activation::background,
				});
			}
		}
		return;
	} else if (_subsectionTabs) {
		return;
	}
	_subsectionTabs = controller()->restoreSubsectionTabsFor(this, thread);
	if (!_subsectionTabs) {
		_subsectionTabs = std::make_unique<HistoryView::SubsectionTabs>(
			controller(),
			this,
			thread);
	}
	_subsectionTabs->removeRequests() | rpl::on_next([=] {
		_subsectionTabsLifetime.destroy();
		_subsectionTabs = nullptr;
		updateControlsGeometry();
	}, _subsectionTabsLifetime);
	_subsectionTabs->layoutRequests() | rpl::on_next([=] {
		_inner->overrideChatMode((_subsectionTabs->leftSkip() > 0)
			? ElementChatMode::Narrow
			: std::optional<ElementChatMode>());
		updateControlsGeometry();
		updateSubsectionTabsGeometry();
		orderWidgets();
	}, _subsectionTabsLifetime);
	_inner->overrideChatMode((_subsectionTabs->leftSkip() > 0)
		? ElementChatMode::Narrow
		: std::optional<ElementChatMode>());
	updateControlsGeometry();
	updateSubsectionTabsGeometry();
	orderWidgets();
}

void ChatWidget::unblockUser() {
	if (const auto user = _peer->asUser()) {
		const auto show = controller()->uiShow();
		Window::PeerMenuUnblockUserWithBotRestart(show, user);
	} else {
		updateControlsVisibility();
	}
}

void ChatWidget::sendBotStartCommand() {
	if (!_peer->isUser()
		|| !_peer->asUser()->isBot()
		|| !_canSendMessages) {
		updateControlsVisibility();
		return;
	}
	session().api().sendBotStart(controller()->uiShow(), _peer->asUser());
	updateControlsVisibility();
	updateControlsGeometry();
}

bool ChatWidget::clearMaybeSendStart() {
	if (!_maybeSendStart) {
		return false;
	} else if (!_peer->isFullLoaded()) {
		_peer->updateFull();
		return false;
	}
	_maybeSendStart = false;
	if (const auto user = _peer->asUser()) {
		if (user->blockStatus() == PeerData::BlockStatus::NotBlocked) {
			if (const auto info = user->botInfo.get()) {
				if (!info->startToken.isEmpty()) {
					return true;
				}
			}
		}
	}
	return false;
}

void ChatWidget::checkMaybeSendBotStart() {
	if (!_maybeSendStart || mode() != Mode::History) {
		return;
	}
	const auto empty = _inner->isEmpty();
	const auto loadedEmpty = empty
		&& _inner->loadedAtTop()
		&& _inner->loadedAtBottom();
	if ((!empty || loadedEmpty) && clearMaybeSendStart() && !empty) {
		sendBotStartCommand();
	}
}

void ChatWidget::joinChannelAction() {
	if (const auto channel = _peer->asChannel()) {
		session().api().joinChannel(channel);
	} else {
		updateControlsVisibility();
	}
}

void ChatWidget::joinGroupAction() {
	if (const auto channel = _peer->asChannel()) {
		session().api().joinChannel(channel);
	} else {
		updateControlsVisibility();
	}
}

void ChatWidget::toggleMuteUnmute() {
	const auto wasMuted = _history->muted();
	const auto muteForSeconds = Data::MuteValue{
		.unmute = wasMuted,
		.forever = !wasMuted,
	};
	session().data().notifySettings().update(_peer, muteForSeconds);
}

void ChatWidget::setChooseReportMessagesDetails(
		Data::ReportInput reportInput,
		Fn<void(std::vector<MsgId>)> callback) {
	if (!callback) {
		const auto refresh = _chooseForReport
			&& _chooseForReport->active;
		_chooseForReport = nullptr;
		if (_inner) {
			_inner->clearChooseReportReason();
		}
		if (refresh) {
			_bottom->setInReportMode(false);
			_topBar->clearChooseMessagesForReport();
			clearSelected();
			updateControlsVisibility();
			updateControlsGeometry();
		}
	} else {
		_chooseForReport = std::make_unique<ChooseMessagesForReport>(
			ChooseMessagesForReport{
				.reportInput = std::move(reportInput),
				.callback = std::move(callback) });
	}
}

void ChatWidget::activateChooseForReport() {
	if (!_chooseForReport) {
		return;
	}
	_chooseForReport->active = true;
	if (_inner) {
		_inner->setChooseReportReason(_chooseForReport->reportInput);
	}
	clearSelected();
	_bottom->setInReportMode(true);
	updateTopBarChooseForReport();
}

bool ChatWidget::showChooseReportMessages(
		not_null<PeerData*> peer,
		Data::ReportInput &&reportInput,
		Fn<void(std::vector<MsgId>)> &&done) {
	if (peer != _peer || mode() != Mode::History) {
		return false;
	}
	setChooseReportMessagesDetails(std::move(reportInput), std::move(done));
	activateChooseForReport();
	return true;
}

bool ChatWidget::clearChooseReportMessages() {
	setChooseReportMessagesDetails({}, nullptr);
	return true;
}

bool ChatWidget::toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show) {
	const auto update = [=] {
		updateControlsVisibility();
		updateControlsGeometry();
	};
	if (peer != _peer) {
		return false;
	} else if (_chooseTheme) {
		if (isChoosingTheme() && !show.value_or(false)) {
			const auto was = base::take(_chooseTheme);
			if (Ui::InFocusChain(this)) {
				setInnerFocus();
			}
			update();
			updateControlsVisibility();
			updateControlsGeometry();
		}
		return true;
	} else if (!show.value_or(true)) {
		return true;
	} else if (_composeControls->isRecording()) {
		controller()->showToast(tr::lng_chat_theme_cant_voice(tr::now));
		return true;
	}
	_chooseTheme = std::make_unique<Ui::ChooseThemeController>(
		this,
		controller(),
		peer);
	_chooseTheme->shouldBeShownValue(
	) | rpl::on_next(update, _chooseTheme->lifetime());
	orderWidgets();
	updateControlsVisibility();
	updateControlsGeometry();
	update();
	return true;
}

Ui::ChatTheme *ChatWidget::customChatTheme() const {
	return _theme.get();
}

void ChatWidget::updateTopBarChooseForReport() {
	if (_chooseForReport && _chooseForReport->active) {
		_topBar->showChooseMessagesForReport(
			_chooseForReport->reportInput);
	} else {
		_topBar->clearChooseMessagesForReport();
	}
	updateControlsVisibility();
	updateControlsGeometry();
}

void ChatWidget::reportSelectedMessages() {
	if (!_inner || !_chooseForReport) {
		return;
	}
	auto ids = _inner->getSelectedIds();
	if (ids.empty()) {
		return;
	}
	const auto done = _chooseForReport->callback;
	clearSelected();
	controller()->clearChooseReportMessages();
	if (done) {
		done(ranges::views::all(
			ids
		) | ranges::views::transform(&FullMsgId::msg) | ranges::to_vector);
	}
}

void ChatWidget::updateControlsVisibility() {
	const auto wasAtMax = _scroll
		&& (_scroll->scrollTop() >= _scroll->scrollTopMax());
	const auto keepAtMax = gsl::finally([&] {
		if (wasAtMax
			&& _scroll
			&& (_scroll->scrollTop() < _scroll->scrollTopMax())) {
			listScrollTo(_scroll->scrollTopMax());
		}
	});
	_bottom->updateControlsVisibility();
	const auto active = _bottom->isButtonActive();
	const auto choosingTheme = isChoosingTheme();
	const auto hasSublistReplacement = _bottom->hasOpenChatButton()
		|| _bottom->hasAboutHiddenAuthor();
	if (choosingTheme) {
		_chooseTheme->show();
		setInnerFocus();
	} else if (_chooseTheme) {
		_chooseTheme->hide();
	}
	if (choosingTheme && _kbScroll) {
		_kbScroll->hide();
		_kbShown = false;
		setKeyboardReplyTo(nullptr);
		hideKeyboardReplyToExternal();
	}
	if (active || choosingTheme) {
		if (_supportAutocomplete) {
			_supportAutocomplete->hide();
		}
		if (!hasSublistReplacement) {
			_composeControls->hide();
		}
	} else {
		if (!hasSublistReplacement) {
			_composeControls->show();
		}
	}
	if (_keyboard) {
		updateBotKeyboard();
	} else {
		updateControlsGeometry();
	}
}

bool ChatWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::MessageToSend messageToSend,
		std::optional<MsgId> localId) {
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(messageToSend.action.replyTo.messageId);
	const auto error = !ephemeralReply
		? Data::RestrictionError(_peer, ChatRestriction::SendStickers)
		: Data::SendError();
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return false;
	} else if ((!ephemeralReply && showSlowmodeError())
		|| ShowSendPremiumError(controller(), document)) {
		return false;
	}
	if (!ephemeralReply) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = messageToSend;
			copy.action.options.starsApproved = approved;
			sendExistingDocument(document, std::move(copy), localId);
		};
		const auto checked = checkSendPayment(
			1,
			messageToSend.action.options,
			withPaymentApproved);
		if (!checked) {
			return false;
		}
	}

	Api::SendExistingDocument(
		std::move(messageToSend),
		document,
		localId);

	_composeControls->clearFieldAfterStickerSend();
	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void ChatWidget::sendExistingPhoto(not_null<PhotoData*> photo) {
	sendExistingPhoto(photo, {});
}

bool ChatWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	const auto error = !ephemeralReply
		? Data::RestrictionError(_peer, ChatRestriction::SendPhotos)
		: Data::SendError();
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return false;
	} else if (!ephemeralReply && showSlowmodeError()) {
		return false;
	}

	const auto action = prepareSendAction(options);
	if (!ephemeralReply) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendExistingPhoto(photo, copy);
		};
		const auto checked = checkSendPayment(
			1,
			action.options,
			withPaymentApproved);
		if (!checked) {
			return false;
		}
	}

	Api::SendExistingPhoto(
		Api::MessageToSend(action),
		photo);

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void ChatWidget::sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		not_null<UserData*> bot) {
	if (!_canSendMessages) {
		return;
	} else if (showSlowmodeError()) {
		return;
	} else if (const auto error = result->getErrorOnSend(_history)) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return;
	}
	sendInlineResult(std::move(result), bot, {}, std::nullopt);
	//const auto callback = [=](Api::SendOptions options) {
	//	sendInlineResult(result, bot, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

void ChatWidget::sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId) {
	if (ShowEphemeralReplyTextOnlyError(
			controller()->uiShow(),
			&session(),
			replyTo().messageId)) {
		return;
	}
	auto action = prepareSendAction(options);
	action.generateLocal = true;
	const auto withPaymentApproved = [=](int approved) {
		auto copy = options;
		copy.starsApproved = approved;
		sendInlineResult(result, bot, copy, localMessageId);
	};
	const auto checked = checkSendPayment(
		1,
		action.options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	session().api().sendInlineResult(
		bot,
		result.get(),
		action,
		localMessageId);

	_composeControls->clear();
	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	bot->session().recentInlineBots().bump(bot);
	finishSending();
}

SendMenu::Details ChatWidget::sendMenuDetails() const {
	using Type = SendMenu::Type;
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	const auto type = ephemeralReply
		? Type::Disabled
		: (mode() != Mode::History)
		? ((_topic && !_peer->starsPerMessageChecked())
			? Type::Scheduled
			: Type::SilentOnly)
		: _peer->starsPerMessageChecked()
		? Type::SilentOnly
		: _peer->isSelf()
		? Type::Reminder
		: HistoryView::CanScheduleUntilOnline(_peer)
		? Type::ScheduledToUser
		: Type::Scheduled;
	return SendMenu::Details{
		.type = type,
		.barePeerId = (_sublist
			? _sublist->owningHistory()
			: _history)->peer->id.value,
		.bareTopicRootId = _topic ? _topic->rootId().bare : 0,
		.effectAllowed = _peer->isUser(),
	};
}

bool ChatWidget::processChosenSticker(ChatHelpers::FileChosen &&chosen) {
	_composeControls->processChosenSticker(std::move(chosen));
	return true;
}

FullReplyTo ChatWidget::replyTo() const {
	if (auto custom = _composeControls->replyingToMessage()) {
		const auto item = custom.messageId
			? session().data().message(custom.messageId)
			: nullptr;
		const auto sublistPeerId = item ? item->sublistPeerId() : PeerId();
		if (!item
			|| !_monoforumPeerId
			|| (sublistPeerId == _monoforumPeerId)) {
			const auto topicRootId = resolvedTopicRootId();
			// Never answer to a message in a wrong monoforum peer id.
			custom.topicRootId = topicRootId;
			custom.monoforumPeerId = _monoforumPeerId;
			return custom;
		}
	}
	if (const auto keyboard = keyboardReplyTo()) {
		return keyboard;
	}
	const auto topicRootId = resolvedTopicRootId();
	return FullReplyTo{
		.messageId = (topicRootId
			? FullMsgId(_peer->id, topicRootId)
			: FullMsgId()),
		.topicRootId = topicRootId,
		.monoforumPeerId = _monoforumPeerId,
	};
}

FullReplyTo ChatWidget::keyboardReplyTo() const {
	if (!_kbReplyTo) {
		return {};
	}
	const auto topicRootId = resolvedTopicRootId();
	return FullReplyTo{
		.messageId = _kbReplyTo->fullId(),
		.topicRootId = topicRootId,
		.monoforumPeerId = _monoforumPeerId,
	};
}

bool ChatWidget::realReplyOrEditActive() const {
	return _composeControls->isEditingMessage()
		|| _composeControls->replyingToMessage().replying();
}

FullMsgId ChatWidget::keyboardSourceId() const {
	if (mode() == Mode::History) {
		return _history->lastKeyboardId
			? FullMsgId(_peer->id, _history->lastKeyboardId)
			: FullMsgId();
	} else if (mode() == Mode::Replies) {
		return (_repliesKeyboardInited
				&& (_repliesKeyboardRootId == _repliesRootId)
				&& _repliesKeyboardId)
			? FullMsgId(_peer->id, _repliesKeyboardId)
			: FullMsgId();
	}
	return FullMsgId();
}

HistoryItem *ChatWidget::keyboardSourceItem() const {
	const auto id = keyboardSourceId();
	const auto item = id ? session().data().message(_peer, id.msg) : nullptr;
	return (item && itemBelongsToKeyboardView(item)) ? item : nullptr;
}

FullMsgId ChatWidget::keyboardSourceIdForHiddenState() const {
	if (!_keyboard) {
		return {};
	}
	const auto source = keyboardSourceId();
	if (source && source == _keyboard->forMsgId()) {
		return source;
	}
	const auto reply = _composeControls->replyingToMessage();
	const auto replyItem = reply
		? session().data().message(reply.messageId)
		: nullptr;
	return (replyItem
		&& itemBelongsToKeyboardView(replyItem)
		&& (_keyboard->forMsgId() == replyItem->fullId()))
		? replyItem->fullId()
		: FullMsgId();
}

bool ChatWidget::keyboardUiSuppressedByReplyOrEdit() const {
	if (_composeControls->isEditingMessage()) {
		return true;
	}
	const auto reply = _composeControls->replyingToMessage();
	if (!reply) {
		return false;
	}
	const auto replyItem = session().data().message(reply.messageId);
	return !(replyItem
		&& itemBelongsToKeyboardView(replyItem)
		&& _keyboard
		&& (_keyboard->forMsgId() == replyItem->fullId())
		&& _keyboard->hasMarkup());
}

MsgId ChatWidget::keyboardHiddenId() const {
	if (mode() == Mode::History) {
		return _history->lastKeyboardHiddenId;
	} else if (mode() == Mode::Replies
		&& (_repliesKeyboardRootId == _repliesRootId)) {
		return _repliesKeyboardHiddenId;
	}
	return 0;
}

void ChatWidget::setKeyboardHiddenId(MsgId id) {
	if (mode() == Mode::History) {
		_history->lastKeyboardHiddenId = id;
	} else if (mode() == Mode::Replies) {
		_repliesKeyboardRootId = _repliesRootId;
		_repliesKeyboardHiddenId = id;
	}
}

void ChatWidget::clearKeyboardHiddenId() {
	setKeyboardHiddenId(0);
}

bool ChatWidget::keyboardUsed() const {
	const auto sourceId = keyboardSourceIdForHiddenState();
	if (!sourceId) {
		return false;
	}
	if (sourceId == keyboardSourceId()) {
		if (mode() == Mode::History) {
			return _history->lastKeyboardUsed;
		} else if (mode() == Mode::Replies
			&& (_repliesKeyboardRootId == _repliesRootId)) {
			return _repliesKeyboardUsed;
		}
		return false;
	}
	return keyboardHiddenId() == sourceId.msg;
}

void ChatWidget::markKeyboardUsed() {
	const auto sourceId = keyboardSourceIdForHiddenState();
	if (!sourceId) {
		return;
	}
	if (sourceId == keyboardSourceId()) {
		if (mode() == Mode::History) {
			_history->lastKeyboardUsed = true;
		} else if (mode() == Mode::Replies) {
			_repliesKeyboardRootId = _repliesRootId;
			_repliesKeyboardUsed = true;
		}
	} else {
		setKeyboardHiddenId(sourceId.msg);
	}
}

void ChatWidget::showKeyboardReplyToExternal() {
	if (!_kbReplyTo) {
		hideKeyboardReplyToExternal();
		return;
	}
	_keyboardReplyExternalVisible = true;
	_composeControls->replyToMessageExternal(
		FullReplyTo{ .messageId = _kbReplyTo->fullId() });
}

void ChatWidget::hideKeyboardReplyToExternal() {
	_keyboardReplyExternalVisible = false;
	if (!_composeControls->replyingToMessageExternal()) {
		return;
	}
	_ignoreReplyCancelledExternal = true;
	_composeControls->cancelReplyMessageExternal();
	_ignoreReplyCancelledExternal = false;
}

bool ChatWidget::keyboardRowsVisible() const {
	return _kbScroll && !_kbScroll->isHidden();
}

void ChatWidget::updateKeyboardUiState(bool hasMarkup, bool suppress) {
	const auto rowsVisible = keyboardRowsVisible();
	const auto replyVisible = _keyboardReplyExternalVisible;
	const auto canShow = _canSendMessages && !_bottom->isButtonActive();
	_botKeyboardShownToggleShown = hasMarkup
		&& canShow
		&& !rowsVisible
		&& !suppress;
	_botKeyboardHideToggleShown = rowsVisible
		&& !suppress
		&& (!_peer->isUser() || !_keyboard->persistent());
	_botCommandStartExtraGuard = !hasMarkup && !replyVisible;
	_botKeyboardPlaceholder = ((rowsVisible || replyVisible)
		&& !suppress
		&& !_keyboard->placeholder().isEmpty())
		? _keyboard->placeholder()
		: QString();
}

bool ChatWidget::itemBelongsToKeyboardView(
		not_null<const HistoryItem*> item) const {
	if (mode() == Mode::Sublist || item->history() != _history) {
		return false;
	}
	if (mode() == Mode::Replies) {
		return _replies && item->inThread(_repliesRootId);
	}
	return true;
}

int ChatWidget::computeMaxFieldHeightForKeyboard(
		int contentTop,
		int bottom) const {
	const auto available = bottom - contentTop
		- (_composeControls->fieldHeaderShownCurrent()
			? st::historyReplyHeight
			: 0)
		- (2 * st::historySendPadding)
		- st::historyReplyHeight;
	return std::min(
		st::historyComposeFieldMaxHeight,
		std::max(0, available));
}

void ChatWidget::resetRepliesKeyboardState() {
	_repliesKeyboardRootId = _repliesRootId;
	_repliesKeyboardId = 0;
	_repliesKeyboardHiddenId = 0;
	_repliesKeyboardInited = false;
	_repliesKeyboardUsed = false;
	_repliesLastSlice = nullptr;
}

void ChatWidget::clearRepliesKeyboardState() {
	if (_repliesKeyboardId == _repliesKeyboardHiddenId) {
		_repliesKeyboardHiddenId = 0;
	}
	_repliesKeyboardRootId = _repliesRootId;
	_repliesKeyboardId = 0;
	_repliesKeyboardInited = true;
}

void ChatWidget::setRepliesKeyboardState(MsgId id) {
	_repliesKeyboardRootId = _repliesRootId;
	_repliesKeyboardId = id;
	_repliesKeyboardInited = true;
	_repliesKeyboardUsed = false;
}

SuggestOptions ChatWidget::suggestOptions(bool skipNoAdminCheck) const {
	const auto checked = skipNoAdminCheck
		|| _history->suggestDraftAllowed();
	return (checked && _suggestOptions)
		? _suggestOptions->values()
		: SuggestOptions();
}

void ChatWidget::applySuggestOptions(
		SuggestOptions suggest,
		SuggestMode suggestMode) {
	Expects(suggest.exists);

	_suggestOptions = std::make_unique<SuggestOptionsBar>(
		controller()->uiShow(),
		_peer,
		suggest,
		suggestMode);
	_suggestOptions->updates() | rpl::on_next([=] {
		update();
		_composeControls->saveFieldToHistoryLocalDraft();
		refreshTopBarActiveChat();
	}, _suggestOptions->lifetime());
	_composeControls->saveFieldToHistoryLocalDraft();
	_suggestPostToggleActive = (_suggestOptions != nullptr);
	updateControlsGeometry();
	update();
	refreshTopBarActiveChat();
}

bool ChatWidget::cancelSuggestPost() {
	if (!_suggestOptions) {
		return false;
	}
	_suggestOptions = nullptr;
	updateControlsGeometry();
	_composeControls->saveFieldToHistoryLocalDraft();
	_suggestPostToggleActive = (_suggestOptions != nullptr);
	update();
	refreshTopBarActiveChat();
	return true;
}

void ChatWidget::refreshSuggestPostToggle() {
	const auto has = _history->suggestDraftAllowed();
	_suggestPostToggleShown = has;
	if (!has && _suggestOptions) {
		cancelSuggestPost();
	}
}

void ChatWidget::refreshSuggestFromDraft() {
	if (!_history->suggestDraftAllowed()) {
		return;
	}
	const auto topicRootId = _topic ? _topic->rootId() : MsgId();
	const auto draft = _history->localDraft(
		topicRootId,
		_monoforumPeerId);
	if (draft && draft->suggest.exists) {
		applySuggestOptions(draft->suggest, SuggestMode::New);
	} else {
		cancelSuggestPost();
	}
}

ChatWidget::Mode ChatWidget::mode() const {
	if (_sublist) {
		return Mode::Sublist;
	} else if (_repliesRootId) {
		return Mode::Replies;
	}
	return Mode::History;
}

void ChatWidget::refreshTopBarActiveChat() {
	using namespace Dialogs;

	auto state = EntryState{
		.currentReplyTo = replyTo(),
		.currentSuggest = suggestOptions(),
	};
	auto painter = (HistoryView::SendActionPainter*)nullptr;
	switch (mode()) {
	case Mode::Sublist:
		state.key = Key{ _sublist };
		state.section = EntryState::Section::SavedSublist;
		painter = _sendAction.get();
		break;
	case Mode::Replies:
		state.key = _topic ? Key{ _topic } : Key{ _history };
		state.section = EntryState::Section::Replies;
		painter = _sendAction.get();
		break;
	case Mode::History:
		state.key = _topic ? Key{ _topic } : Key{ _history };
		state.section = EntryState::Section::History;
		painter = _history->sendActionPainter();
		break;
	}
	_topBar->setActiveChat(state, painter);
	_composeControls->setCurrentDialogsEntryState(state);
	controller()->setDialogsEntryState(state);
}

void ChatWidget::handlePeerMigration() {
	const auto current = _peer->migrateToOrMe();
	const auto chat = current->migrateFrom();
	if (!chat) {
		return;
	}
	const auto channel = current->asChannel();
	Assert(channel != nullptr);
	if (_peer != channel) {
		const auto showAtMsgId = _lastShownAt.msg;
		controller()->showPeerHistory(
			channel->id,
			Window::SectionShow::Way::ClearStack,
			(showAtMsgId > 0) ? (-showAtMsgId) : showAtMsgId);
		channel->session().api().chatParticipants()
			.requestCountDelayed(channel);
	} else {
		_inner->refreshViewer();
		if ((mode() == Mode::History) && _topControls) {
			_topControls->subscribeToPinnedMessages();
		}
	}
}

void ChatWidget::refreshUnreadCountBadge(std::optional<int> count) {
	if (count.has_value()) {
		_cornerButtons.updateJumpDownVisibility(count);
	}
}

void ChatWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	showAtPosition(position);
}

Data::Thread *ChatWidget::cornerButtonsThread() {
	return _sublist
		? static_cast<Data::Thread*>(_sublist)
		: _topic
		? static_cast<Data::Thread*>(_topic)
		: _history;
}

FullMsgId ChatWidget::cornerButtonsCurrentId() {
	return _lastShownAt;
}

bool ChatWidget::cornerButtonsIgnoreVisibility() {
	return animatingShow();
}

std::optional<bool> ChatWidget::cornerButtonsDownShown() {
	if (_composeControls->isLockPresent()
		|| _composeControls->isTTLButtonShown()) {
		return false;
	}
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax() || _cornerButtons.replyReturn()) {
		return true;
	} else if (_inner->loadedAtBottomKnown()) {
		return !_inner->loadedAtBottom() || unreadMessagesBelowBottom();
	}
	return std::nullopt;
}

bool ChatWidget::unreadMessagesBelowBottom() const {
	if (mode() != Mode::History) {
		return false;
	}
	const auto unread = [](History *history) {
		return history
			&& history->unreadCount() > 0
			&& history->trackUnreadMessages();
	};
	return (unread(_history) || unread(_history->migrateFrom()))
		&& _inner->unreadBarBelowVisibleBottom();
}

bool ChatWidget::cornerButtonsUnreadMayBeShown() {
	return _loaded
		&& !_composeControls->isLockPresent()
		&& !_composeControls->isTTLButtonShown();
}

bool ChatWidget::cornerButtonsHas(CornerButtonType type) {
	return _topic
		|| (mode() == Mode::History)
		|| (_sublist && type == CornerButtonType::Reactions)
		|| (type == CornerButtonType::Down);
}

void ChatWidget::showAtStart() {
	showAtPosition(Data::MinMessagePosition);
}

void ChatWidget::showAtEnd() {
	showAtPosition(Data::MaxMessagePosition);
}

void ChatWidget::finishSending() {
	_composeControls->hidePanelsAnimated();
	//if (_previewData && _previewData->pendingTill) previewCancel();
	doSetInnerFocus();
	showAtEnd();
	refreshTopBarActiveChat();
	if (_keyboard
			&& !_keyboard->hasMarkup()
			&& _keyboard->forceReply()
			&& !_kbReplyTo) {
		toggleBotKeyboard(true);
	}
}

void ChatWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId) {
	showAtPosition(position, originItemId, {});
}

void ChatWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId,
		const Window::SectionShow &params) {
	_lastShownAt = position.fullId;
	controller()->setActiveChatEntry(activeChat());
	const auto ignore = _repliesRootId
		&& (position.fullId.msg == _repliesRootId);
	_inner->showAtPosition(
		position,
		params,
		_cornerButtons.doneJumpFrom(position.fullId, originItemId, ignore));
}

void ChatWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

Dialogs::RowDescriptor ChatWidget::activeChat() const {
	const auto messageId = _lastShownAt
		? _lastShownAt
		: FullMsgId(_peer->id, ShowAtUnreadMsgId);
	if (_sublist) {
		return { _sublist, messageId };
	} else if (_topic) {
		return { _topic, messageId };
	}
	return { _history, messageId };
}

bool ChatWidget::preventsClose(Fn<void()> &&continueCallback) const {
	if (_composeControls->preventsClose(base::duplicate(continueCallback))) {
		return true;
	} else if (!_newTopicDiscarded
		&& _topic
		&& _topic->creating()) {
		const auto weak = base::make_weak(this);
		auto sure = [=](Fn<void()> &&close) {
			if (const auto strong = weak.get()) {
				strong->_newTopicDiscarded = true;
			}
			close();
			if (continueCallback) {
				continueCallback();
			}
		};
		controller()->show(Ui::MakeConfirmBox({
			.text = tr::lng_forum_discard_sure(tr::now),
			.confirmed = std::move(sure),
			.confirmText = tr::lng_record_lock_discard(),
			.confirmStyle = &st::attentionBoxButton,
		}));
		return true;
	}
	return false;
}

QPixmap ChatWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	const auto hideTopBarShadow = params.withTopBarShadow
		&& !params.fromBottom;
	if (hideTopBarShadow) {
		_topBarShadow->hide();
	}
	if (_bottom->isButtonActive()) {
		_composeControls->hide();
	} else {
		_composeControls->showForGrab();
	}
	updateControlsVisibility();
	if (params.fromBottom && _subsectionTabs) {
		_subsectionTabs->hide();
	}
	auto result = Ui::GrabWidget(this);
	if (hideTopBarShadow) {
		_topBarShadow->show();
	}
	_topControls->hide();
	if (_subsectionTabs) {
		_subsectionTabs->hide();
	}
	return result;
}

void ChatWidget::checkActivation() {
	_inner->checkActivation();
}

void ChatWidget::doSetInnerFocus() {
	if (_composeSearch
		&& _inner->getSelectedText().rich.text.isEmpty()
		&& _inner->getSelectedItems().empty()) {
		_composeSearch->setInnerFocus();
	} else if (isChoosingTheme()) {
		_chooseTheme->setFocus();
	} else if (!_inner->getSelectedText().rich.text.isEmpty()
		|| !_inner->getSelectedItems().empty()
		|| !_composeControls->focus()) {
		_inner->setFocus();
	}
}

bool ChatWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<ChatMemento*>(memento.get())) {
		if (logMemento->id() == _id) {
			if (params.reapplyLocalDraft) {
				_composeControls->applyDraft(
					ComposeControls::FieldHistoryAction::NewEntry);
				refreshSuggestFromDraft();
			} else if ((mode() == Mode::History)
				&& !logMemento->highlightId()
				&& !logMemento->sendBotStart()
				&& !logMemento->maybeSendBotStart()) {
				if (!_history->trackUnreadMessages()
					|| _inner->insideJumpToEndInsteadOfToUnread()) {
					showAtEnd();
				} else {
					showAtPosition(Data::UnreadMessagePosition);
				}
			} else {
				restoreState(logMemento);
				if ((mode() != Mode::History)
					&& !logMemento->highlightId()) {
					showAtPosition(Data::UnreadMessagePosition);
				}
			}
			return true;
		}
	}
	return false;
}

bool ChatWidget::sameTypeAs(not_null<Window::SectionMemento*> memento) {
	return dynamic_cast<ChatMemento*>(memento.get()) != nullptr;
}

void ChatWidget::setInternalState(
		const QRect &geometry,
		not_null<ChatMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

bool ChatWidget::pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) {
	return _composeControls->pushTabbedSelectorToThirdSection(
		thread,
		params);
}

bool ChatWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

std::shared_ptr<Window::SectionMemento> ChatWidget::createMemento() {
	auto result = std::make_shared<ChatMemento>(_id);
	saveState(result.get());
	return result;
}

bool ChatWidget::showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) {
	if (peerId != _peer->id) {
		return false;
	}
	const auto id = FullMsgId(_peer->id, messageId);
	const auto message = _history->owner().message(id);
	if (!message) {
		return false;
	} else if (_repliesRootId
		&& !message->inThread(_repliesRootId)
		&& id.msg != _repliesRootId) {
		return false;
	} else if (_sublist && message->savedSublist() != _sublist) {
		return false;
	}
	const auto originMessage = [&]() -> HistoryItem* {
		using OriginMessage = Window::SectionShow::OriginMessage;
		if (const auto origin = std::get_if<OriginMessage>(&params.origin)) {
			if (const auto returnTo = session().data().message(origin->id)) {
				if (returnTo->history() != _history) {
					return nullptr;
				} else if (_repliesRootId
					&& returnTo->inThread(_repliesRootId)) {
					return returnTo;
				} else if (_sublist
					&& returnTo->savedSublist() == _sublist) {
					return returnTo;
				} else if (!_repliesRootId && !_sublist) {
					return returnTo;
				}
			}
		}
		return nullptr;
	}();
	const auto currentReplyReturn = _cornerButtons.replyReturn();
	const auto originItemId = !originMessage
		? FullMsgId()
		: (currentReplyReturn != originMessage)
		? originMessage->fullId()
		: FullMsgId();
	showAtPosition(message->position(), originItemId, params);
	return true;
}

Window::SectionActionResult ChatWidget::sendBotCommand(
		Bot::SendCommandRequest request) {
	if (request.peer != _peer) {
		return Window::SectionActionResult::Fallback;
	}
	sendBotCommand(std::move(request), {});
	return Window::SectionActionResult::Handle;
}

bool ChatWidget::notify_switchInlineBotButtonReceived(
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	if (samePeerBot) {
		const auto to = controller()->dialogsEntryStateCurrent();
		if (!to.key.owningHistory()) {
			return false;
		}
		controller()->switchInlineQuery(to, samePeerBot, query);
		return true;
	} else if (const auto bot = _peer->asUser()) {
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

Window::SectionActionResult ChatWidget::hideSingleUseKeyboard(
		FullMsgId replyToId) {
	if (replyToId.peer != _peer->id) {
		return Window::SectionActionResult::Ignore;
	}
	const auto reply = _composeControls->replyingToMessage();
	const auto replyMatches = (reply.messageId == replyToId);
	const auto sourceId = keyboardSourceIdForHiddenState();
	const auto sourceMatches = (sourceId == replyToId);
	if (!replyMatches && !sourceMatches) {
		return Window::SectionActionResult::Fallback;
	}
	if (replyMatches) {
		cancelReply();
	}
	if (sourceMatches
		&& _keyboard
		&& _keyboard->singleUse()
		&& _keyboard->hasMarkup()) {
		if (keyboardRowsVisible() || _keyboardReplyExternalVisible) {
			toggleBotKeyboard(false);
		}
		markKeyboardUsed();
	}
	return Window::SectionActionResult::Handle;
}

bool ChatWidget::confirmSendingFiles(const QStringList &files) {
	return confirmSendingFiles(files, QString());
}

bool ChatWidget::confirmSendingFiles(not_null<const QMimeData*> data) {
	return confirmSendingFiles(data, std::nullopt);
}

bool ChatWidget::confirmSendingFiles(
		const QStringList &files,
		const QString &insertTextOnCancel) {
	const auto premium = controller()->session().user()->isPremium();
	return confirmSendingFiles(
		Storage::PrepareMediaList(files, st::sendMediaPreviewSize, premium),
		insertTextOnCancel);
}

void ChatWidget::replyToMessage(FullReplyTo id) {
	_composeControls->replyToMessage(std::move(id));
	refreshTopBarActiveChat();
}

void ChatWidget::saveHistoryScrollState(const ListMemento &state) {
	if (mode() != Mode::History || !_inner) {
		return;
	}
	const auto migrated = _history->migrateFrom();
	const auto aroundPosition = state.aroundPosition();
	const auto scrollTopState = state.scrollTopState();
	const auto useAroundPosition = scrollTopState.item
		|| (aroundPosition == Data::UnreadMessagePosition)
		|| (aroundPosition == Data::MaxMessagePosition);
	_history->showAtMsgId = (useAroundPosition
		? ShowAtMsgIdFromPosition(_history, aroundPosition)
		: std::nullopt).value_or((_lastShownAt
		&& migrated
		&& (_lastShownAt.peer == migrated->peer->id)
		&& IsServerMsgId(_lastShownAt.msg))
		? -_lastShownAt.msg
		: _lastShownAt
		? _lastShownAt.msg
		: ShowAtUnreadMsgId);
	const auto atBottom = (_scroll->scrollTop() >= _scroll->scrollTopMax())
		&& _inner->loadedAtBottomKnown()
		&& _inner->loadedAtBottom();
	if (atBottom || !scrollTopState.item) {
		_history->forgetScrollState();
		if (migrated) {
			migrated->forgetScrollState();
		}
		return;
	}
	const auto save = [](
			not_null<History*> history,
			ListMemento::ScrollTopState state) {
		history->listScrollTopItemId = state.item.fullId;
		history->listScrollTopItemDate = state.item.date;
		history->listScrollTopShift = state.shift;
	};
	const auto useMigrated = migrated
		&& (scrollTopState.item.fullId.peer == migrated->peer->id);
	if (useMigrated) {
		_history->forgetScrollState();
		save(migrated, scrollTopState);
	} else {
		_history->forgetScrollState();
		save(_history, scrollTopState);
		if (migrated) {
			migrated->forgetScrollState();
		}
	}
}

void ChatWidget::saveState(not_null<ChatMemento*> memento) {
	memento->setReplies(_replies);
	memento->setReplyReturns(_cornerButtons.replyReturns());
	_inner->saveState(memento->list());
	saveHistoryScrollState(*memento->list());
}

void ChatWidget::refreshReplies() {
	if (!_repliesRootId) {
		return;
	}
	auto old = base::take(_replies);
	setReplies(_topic
		? _topic->replies()
		: std::make_shared<Data::RepliesList>(_history, _repliesRootId));
	if (old) {
		_inner->refreshViewer();
	}
	updateBotKeyboard();
}

void ChatWidget::setReplies(std::shared_ptr<Data::RepliesList> replies) {
	_replies = std::move(replies);
	resetRepliesKeyboardState();
	_repliesLifetime.destroy();

	_replies->unreadCountValue(
	) | rpl::on_next([=](std::optional<int> count) {
		refreshUnreadCountBadge(count);
	}, lifetime());

	unreadCountUpdated();

	const auto isTopic = (_topic != nullptr);
	const auto isTopicCreating = isTopic && _topic->creating();
	rpl::combine(
		rpl::single(
			std::optional<int>()
		) | rpl::then(_replies->maybeFullCount()),
		_areComments.value()
	) | rpl::map([=](std::optional<int> count, bool areComments) {
		const auto sub = isTopic ? 1 : 0;
		return (count && (*count > sub))
			? (isTopic
				? tr::lng_forum_messages
				: areComments
				? tr::lng_comments_header
				: tr::lng_replies_header)(
					lt_count_decimal,
					rpl::single(*count - sub) | tr::to_count())
			: (isTopic
				? ((count.has_value() || isTopicCreating)
					? tr::lng_forum_no_messages
					: tr::lng_contacts_loading)
				: areComments
				? tr::lng_comments_header_none
				: tr::lng_replies_header_none)();
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](const QString &text) {
		_topBar->setCustomTitle(text);
	}, _repliesLifetime);
}

void ChatWidget::subscribeToSublist() {
	Expects(_sublist != nullptr);

	// Must be done before unreadCountUpdated(), or we auto-close.
	if (_sublist->unreadMark()) {
		_sublist->owner().histories().changeSublistUnreadMark(
			_sublist,
			false);
	}

	_sublist->unreadCountValue(
	) | rpl::on_next([=](std::optional<int> count) {
		refreshUnreadCountBadge(count);
	}, lifetime());

	using Flag = Data::SublistUpdate::Flag;
	session().changes().sublistUpdates(
		_sublist,
		(Flag::UnreadView
			| Flag::UnreadReactions
			| Flag::UnreadPollVotes
			| Flag::CloudDraft)
	) | rpl::on_next([=](const Data::SublistUpdate &update) {
		if (update.flags & Flag::UnreadView) {
			unreadCountUpdated();
		}
		if (update.flags
			& (Flag::UnreadReactions | Flag::UnreadPollVotes)) {
			_cornerButtons.updateUnreadThingsVisibility();
		}
		if (update.flags & Flag::CloudDraft) {
			_composeControls->applyCloudDraft();
			refreshSuggestFromDraft();
		}
	}, lifetime());

	_sublist->destroyed(
	) | rpl::on_next([=] {
		closeCurrent();
	}, lifetime());

	unreadCountUpdated();
	_topControls->subscribeToPinnedMessages();
}

void ChatWidget::unreadCountUpdated() {
	if (mode() == Mode::History) {
		const auto migrated = _history->migrateFrom();
		if (_history->unreadMark() || (migrated && migrated->unreadMark())) {
			crl::on_main(this, [=] {
				closeCurrent();
			});
			return;
		}
		const auto hideCounter = _history->isForum()
			|| !_history->trackUnreadMessages();
		refreshUnreadCountBadge(hideCounter
			? 0
			: _history->amMonoforumAdmin()
			? _history->chatListUnreadState().messages
			: _history->chatListBadgesState().unreadCounter);
	} else if (_sublist && _sublist->unreadMark()) {
		crl::on_main(this, [=] {
			const auto guard = base::make_weak(this);
			controller()->showPeerHistory(_sublist->owningHistory());
			if (guard) {
				closeCurrent();
			}
		});
	} else {
		refreshUnreadCountBadge(_replies
			? (_replies->unreadCountKnown()
				? _replies->unreadCountCurrent()
				: std::optional<int>())
			: _sublist
			? (_sublist->unreadCountKnown()
				? _sublist->unreadCountCurrent()
				: std::optional<int>())
			: std::optional<int>());
	}
}

void ChatWidget::restoreState(not_null<ChatMemento*> memento) {
	if (auto replies = memento->getReplies()) {
		setReplies(std::move(replies));
	} else if (!_replies && _repliesRootId) {
		refreshReplies();
	}
	_cornerButtons.setReplyReturns(memento->replyReturns());

	// Custom initial scroll for post comments, from "Discussion started".
	if (!memento->highlightId()
		&& _repliesRoot
		&& _repliesRoot->isDiscussionPost()
		&& _replies->computeInboxReadTillFull() == MsgId(1)) {
		_inner->overrideInitialScroll([=] {
			const auto divider = _replies ? _replies->divider() : nullptr;
			if (!divider) {
				return false;
			}
			const auto view = _inner->viewByPosition(divider->position());
			if (!view) {
				return false;
			}
			const auto top = std::max(view->y() - st::topBarHeight, 0);
			listScrollTo(top);
			return true;
		});
	}
	_inner->restoreState(memento->list());
	if (const auto highlight = memento->highlightId()) {
		auto params = Window::SectionShow(
			Window::SectionShow::Way::Forward,
			anim::type::instant);
		params.highlight = memento->highlight();
		showAtPosition(Data::MessagePosition{
			.fullId = ResolveHighlightId(_history, highlight),
			.date = TimeId(0),
		}, memento->originId(), params);
	}
	if (memento->activateChooseForReport()) {
		activateChooseForReport();
	}
	updateBotKeyboard();
	if (mode() == Mode::History) {
		// Must be done before unreadCountUpdated(), or we auto-close.
		if (_history->unreadMark()) {
			session().data().histories().changeDialogUnreadMark(
				_history,
				false);
		}
		const auto migrated = _history->migrateFrom();
		if (migrated && migrated->unreadMark()) {
			session().data().histories().changeDialogUnreadMark(
				migrated,
				false);
		}
		unreadCountUpdated();
	}
	if (memento->sendBotStart()) {
		sendBotStartCommand();
	} else if (memento->maybeSendBotStart()) {
		_maybeSendStart = true;
		checkMaybeSendBotStart();
	}
}

void ChatWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	recountChatWidth();
	updateControlsGeometry();
}

void ChatWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Window::Adaptive::ChatLayout::Normal
		: Window::Adaptive::ChatLayout::Wide;
	controller()->adaptive().setChatLayout(layout);
}

void ChatWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto wasAtBottom = !_scroll->isHidden()
		&& (_scroll->scrollTop() >= _scroll->scrollTopMax());
	const auto newScrollDelta = _scroll->isHidden()
		? std::nullopt
		: _scroll->scrollTop()
		? base::make_optional(takeTopDelta() + _scrollTopDelta)
		: 0;
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);
	const auto tabsLeftSkip = _subsectionTabs
		? _subsectionTabs->leftSkip()
		: 0;
	const auto tabsBottomSkip = _subsectionTabs
		? _subsectionTabs->bottomSkip()
		: 0;
	const auto innerWidth = contentWidth - tabsLeftSkip;
	const auto subsectionTabsTop = _topBar->bottomNoMargins();
	const auto topControlsTop = subsectionTabsTop
		+ (_subsectionTabs ? _subsectionTabs->topSkip() : 0);
	_topControls->move(tabsLeftSkip, topControlsTop);
	_topControls->resizeToWidth(innerWidth);
	const auto top = topControlsTop + _topControls->height();

	auto bottom = height();
	const auto bottomHeight = _bottom->contentHeight();
	if (bottomHeight > 0) {
		_bottom->setGeometry(0, bottom - bottomHeight, width(), bottomHeight);
		bottom -= bottomHeight;
	}
	if (isChoosingTheme()) {
		bottom -= _chooseTheme->height();
	} else {
		const auto maxFieldHeight = computeMaxFieldHeightForKeyboard(
			top,
			bottom
				- tabsBottomSkip
				- (_suggestOptions ? st::historyReplyHeight : 0));
		if (_kbScroll && _kbShown && keyboardRowsVisible() && _keyboard) {
			_keyboard->resizeToWidth(innerWidth, maxFieldHeight);
			const auto keyboardReserve = std::min(
				_keyboard->height(),
				maxFieldHeight - (maxFieldHeight / 2));
			_composeControls->setFieldMaxHeight(
				maxFieldHeight - keyboardReserve);
			const auto maxKbHeight = std::max(
				0,
				maxFieldHeight - _composeControls->fieldHeightCurrent());
			_keyboard->resizeToWidth(innerWidth, maxKbHeight);
			const auto kbHeight = std::min(
				_keyboard->height(),
				maxKbHeight);
			_kbScroll->setGeometry(
				tabsLeftSkip,
				bottom - kbHeight,
				innerWidth,
				kbHeight);
			bottom -= kbHeight;
		} else {
			_composeControls->setFieldMaxHeight(
				st::historyComposeFieldMaxHeight);
		}
		if (!bottomHeight) {
			bottom -= _composeControls->heightCurrent();
		}
	}
	const auto composeTop = bottom;
	if (_suggestOptions) {
		bottom -= st::historyReplyHeight;
	}
	bottom -= tabsBottomSkip;

	const auto scrollHeight = bottom - top;
	const auto scrollSize = QSize(innerWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(tabsLeftSkip, top);
	if (!_scroll->isHidden()) {
		if (wasAtBottom) {
			_scroll->scrollToY(_scroll->scrollTopMax());
		} else if (newScrollDelta && _scroll->scrollTop()) {
			_scroll->scrollToY(_scroll->scrollTop() + *newScrollDelta);
		}
		updateInnerVisibleArea();
	}
	_composeControls->move(0, composeTop);
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());
	if (_supportAutocomplete) {
		_supportAutocomplete->setBoundings(_scroll->geometry());
	}
	_composeControlsTop = composeTop;

	if (!animatingShow()) {
		updateSubsectionTabsGeometry();
	}

	_cornerButtons.updatePositions();
	_pullToNext->updateGeometry();
	if (_membersDropdown) {
		_membersDropdown->moveToLeft(0, _topBar->height());
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}
}

void ChatWidget::updateSubsectionTabsGeometry() {
	if (!_subsectionTabs) {
		return;
	}
	const auto subsectionTabsTop = _topBar->bottomNoMargins();
	const auto scrollBottom = _scroll->y() + _scroll->height();
	const auto areaHeight = scrollBottom
		+ _subsectionTabs->bottomSkip()
		- subsectionTabsTop;
	_subsectionTabs->setBoundingRect(
		{ 0, subsectionTabsTop, width(), areaHeight });
}

bool ChatWidget::contentOverlapped(const QRect &globalRect) {
	return _composeControls->overlaps(globalRect);
}

void ChatWidget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}

	const auto aboveHeight = _topBar->height();
	const auto bg = e->rect().intersected(
		QRect(0, aboveHeight, width(), height() - aboveHeight));
	SectionWidget::PaintBackground(controller(), _theme.get(), this, bg);

	if (_suggestOptions && !_bottom->isButtonActive()) {
		auto p = Painter(this);
		const auto backy = _composeControlsTop
			- st::historyReplyHeight;
		const auto backh = st::historyReplyHeight;
		p.fillRect(
			myrtlrect(0, backy, width(), backh),
			st::historyReplyBg);
		_suggestOptions->paintIcon(p, 0, backy, width());
		_suggestOptions->paintLines(
			p,
			st::historyReplySkip,
			backy,
			width());
		_suggestOptions->paintBar(p, 0, backy, width());
	}
}

bool ChatWidget::emptyShown() const {
	if (_topic) {
		return _inner->isEmpty()
			|| (_topic->lastKnownServerMessageId() == _repliesRootId);
	} else if (mode() == Mode::History) {
		return _inner->isEmpty();
	}
	return false;
}

bool ChatWidget::isChoosingTheme() const {
	return _chooseTheme && _chooseTheme->shouldBeShown();
}

void ChatWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	if (!_synteticScrollEvent) {
		_lastUserScrolled = crl::now();
	}
	updateInnerVisibleArea();
}

void ChatWidget::scrollToCurrentVoiceMessage(
		FullMsgId fromId,
		FullMsgId toId) {
	if (crl::now() <= _lastUserScrolled + kScrollToVoiceAfterScrolledMs) {
		return;
	}
	_inner->scrollToCurrentVoiceMessage(fromId, toId);
}

void ChatWidget::updateInnerVisibleArea() {
	if (!_inner->animatedScrolling()) {
		checkReplyReturns();
	}
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updatePinnedVisibility();
	_topControls->updatePinnedViewer();
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
	if (mode() == Mode::History) {
		auto state = ListMemento();
		_inner->saveState(&state);
		saveHistoryScrollState(state);
	}
	if (_lastScrollTop != scrollTop) {
		if (!_synteticScrollEvent) {
			_topControls->checkLastPinnedClickedIdReset(
				_lastScrollTop,
				scrollTop);
		}
		_lastScrollTop = scrollTop;
	}
}

void ChatWidget::updatePinnedVisibility() {
	if (_sublist) {
		setPinnedVisibility(true);
		return;
	} else if (!_loaded || !_repliesRootId) {
		return;
	} else if (!_topic && (!_repliesRoot || _repliesRoot->isEmpty())) {
		setPinnedVisibility(!_repliesRoot);
		return;
	}
	const auto rootItem = [&] {
		if (const auto group = _history->owner().groups().find(_repliesRoot)) {
			return group->items.front().get();
		}
		return _repliesRoot;
	};
	const auto view = _inner->viewByPosition(_topic
		? Data::MinMessagePosition
		: rootItem()->position());
	const auto visible = !view
		|| (view->y() + view->height() <= _scroll->scrollTop());
	setPinnedVisibility(visible || (_topic && !view->data()->isPinned()));
}

void ChatWidget::setPinnedVisibility(bool shown) {
	if (_topControls) {
		_topControls->setRepliesRootVisible(shown);
	}
}

void ChatWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	if (!params.fromBottom) {
		_topBar->show();
	}
	_topBar->setAnimatingMode(true);
	_topControls->setAnimatingMode(true);
	if (params.withTopBarShadow && !params.fromBottom) {
		_topBarShadow->show();
	}
	if (params.fromBottom && _subsectionTabs) {
		_subsectionTabs->show();
		orderWidgets();
	}
	_composeControls->showStarted();
}

void ChatWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	const auto replaced = _bottom->hasOpenChatButton()
		|| _bottom->hasAboutHiddenAuthor()
		|| _bottom->isButtonActive();
	if (replaced) {
		if (Ui::InFocusChain(this)) {
			_inner->setFocus();
		}
		_composeControls->hide();
		if (_supportAutocomplete) {
			_supportAutocomplete->hide();
		}
	} else {
		_composeControls->showFinished();
	}
	_inner->showFinished();
	updateSubsectionTabsGeometry();
	_topControls->show();
	_topControls->finishAnimating();
	if (_subsectionTabs) {
		_subsectionTabs->show();
	}

	// We should setup the drag area only after
	// the section animation is finished,
	// because after that the method showChildren() is called.
	setupDragArea();
	updatePinnedVisibility();

	if (_topic) {
		_topic->saveMeAsActiveSubsectionThread();
	} else if (_sublist) {
		_sublist->saveMeAsActiveSubsectionThread();
	} else if (mode() == Mode::History) {
		_history->saveMeAsActiveSubsectionThread();
	}

	updateControlsVisibility();
}

bool ChatWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ChatWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context ChatWidget::listContext() {
	switch (mode()) {
	case Mode::History:
		return Context::History;
	case Mode::Replies:
		return Context::Replies;
	case Mode::Sublist:
		return _sublist->parentChat()
			? Context::Monoforum
			: Context::SavedSublist;
	}
	Unexpected("Mode in ChatWidget::listContext().");
}

bool ChatWidget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	const auto scrolled = (_scroll->scrollTop() != top);
	_synteticScrollEvent = syntetic;
	if (scrolled) {
		_scroll->scrollToY(top);
	} else if (syntetic) {
		updateInnerVisibleArea();
	}
	_synteticScrollEvent = false;
	return scrolled;
}

void ChatWidget::listCancelRequest() {
	if (_chooseForReport && _chooseForReport->active) {
		controller()->clearChooseReportMessages();
		return;
	}
	if (_composeSearch) {
		if (_inner &&
			(!_inner->getSelectedItems().empty()
				|| !_inner->getSelectedText().rich.text.isEmpty())) {
			clearSelected();
		} else {
			_composeSearch->hideAnimated();
		}
		return;
	}
	if (isChoosingTheme()) {
		toggleChooseChatTheme(_peer, false);
		return;
	}
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	} else if (_composeControls->handleCancelRequest()) {
		refreshTopBarActiveChat();
		return;
	} else if (_suggestOptions && _composeControls->fieldTextEmpty()) {
		cancelSuggestPost();
		return;
	}
	controller()->showBackFromStack();
}

void ChatWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

void ChatWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	if (key == Qt::Key_Return || key == Qt::Key_Enter) {
		if (_bottom->botStartShown()) {
			sendBotStartCommand();
		}
		if (!_canSendMessages
			&& Ui::InputField::ShouldSubmit(
				Core::App().settings().sendSubmitWay(),
				e->modifiers())) {
			send({});
		}
	} else if ((key == Qt::Key_O)
		&& (e->modifiers() == Qt::ControlModifier)) {
		if (!_choosingAttach) {
			chooseAttach(std::nullopt);
		}
	} else {
		_composeControls->tryProcessKeyInput(e);
	}
}

void ChatWidget::checkSuggestToGigagroup() {
	if (mode() != Mode::History || _topic) {
		return;
	}
	const auto group = _peer->asMegagroup();
	if (!group || !group->owner().suggestToGigagroup(group)) {
		return;
	}
	InvokeQueued(this, [=] {
		if (!controller()->isLayerShown()) {
			group->owner().setSuggestToGigagroup(group, false);
			group->session().api().request(MTPhelp_DismissSuggestion(
				group->input(),
				MTP_string("convert_to_gigagroup")
			)).send();
			controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::lng_gigagroup_suggest_title());
				box->addRow(
					object_ptr<Ui::FlatLabel>(
						box,
						tr::lng_gigagroup_suggest_text(
						) | rpl::map(tr::rich),
						st::infoAboutGigagroup));
				box->addButton(
					tr::lng_gigagroup_suggest_more(),
					AboutGigagroupCallback(group, controller()));
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		}
	});
}

void ChatWidget::showAboutTopPromotion() {
	if (mode() != Mode::History || _topic) {
		return;
	} else if (!_history->useTopPromotion()
		|| _history->topPromotionAboutShown()) {
		return;
	}
	_history->markTopPromotionAboutShown();
	const auto type = _history->topPromotionType();
	const auto custom = type.isEmpty()
		? QString()
		: Lang::GetNonDefaultValue(kPsaAboutPrefix + type.toUtf8());
	const auto text = type.isEmpty()
		? tr::lng_proxy_sponsor_about(tr::now, tr::rich)
		: custom.isEmpty()
		? tr::lng_about_psa_default(tr::now, tr::rich)
		: tr::rich(custom);
	showInfoTooltip(text, nullptr);
}

void ChatWidget::showInfoTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	_topToast.show(
		_scroll.get(),
		&session(),
		text,
		std::move(hiddenCallback));
}

void ChatWidget::markLoaded() {
	if (!_loaded) {
		_loaded = true;
		crl::on_main(this, [=] {
			updatePinnedVisibility();
		});
		crl::on_main(this, [=] {
			requestSponsoredMessages();
		});
		crl::on_main(this, [=] {
			checkSuggestToGigagroup();
		});
		crl::on_main(this, [=] {
			showAboutTopPromotion();
		});
		if ((mode() == Mode::History) && session().supportMode()) {
			crl::on_main(this, [=] { checkSupportPreload(); });
		}
	}
}

void ChatWidget::clearSupportPreloadRequest() {
	if (_supportPreloadRequest) {
		_history->owner().histories().cancelRequest(_supportPreloadRequest);
		_supportPreloadRequest = 0;
	}
}

void ChatWidget::checkSupportPreload(bool force) {
	if ((mode() != Mode::History)
		|| !session().supportMode()
		|| !_loaded
		|| (controller()->activeChatEntryCurrent().key != activeChat().key)) {
		return;
	}

	const auto setting = session().settings().supportSwitch();
	const auto command = Support::GetSwitchCommand(setting);
	const auto descriptor = !command
		? Dialogs::RowDescriptor()
		: (*command == Shortcuts::Command::ChatNext)
		? controller()->resolveChatNext()
		: controller()->resolveChatPrevious();
	const auto history = descriptor.key.history();
	if (!history) {
		clearSupportPreloadRequest();
		_supportPreloadHistory = nullptr;
		return;
	} else if (_supportPreloadRequest
		&& (_supportPreloadHistory == history)
		&& !force) {
		return;
	} else if (_supportPreloadHistory == history) {
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

void ChatWidget::handleSupportSwitch(not_null<History*> updated) {
	if ((_history != updated) || !session().supportMode()) {
		return;
	}

	const auto setting = session().settings().supportSwitch();
	if (auto method = Support::GetSwitchMethod(setting)) {
		crl::on_main(this, std::move(method));
	}
}

rpl::producer<Data::MessagesSlice> ChatWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	if (_replies) {
		return repliesSource(aroundId, limitBefore, limitAfter);
	} else if (_sublist) {
		return sublistSource(aroundId, limitBefore, limitAfter);
	}
	return historySource(aroundId, limitBefore, limitAfter);
}

rpl::producer<Data::MessagesSlice> ChatWidget::repliesSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _replies->source(
		aroundId,
		limitBefore,
		limitAfter
	) | rpl::before_next([=](const Data::MessagesSlice &slice) {
		_repliesLastSlice = std::make_unique<Data::MessagesSlice>(slice);
		markLoaded();
		maybeUpdateLastKeyboardFromSlice(slice);
	});
}

rpl::producer<Data::MessagesSlice> ChatWidget::sublistSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _sublist->source(
		aroundId,
		limitBefore,
		limitAfter
	) | rpl::before_next([=](const Data::MessagesSlice &result) {
		 // after_next makes a copy of value.
		_topBar->setCustomTitle(!result.fullCount
			? tr::lng_contacts_loading(tr::now)
			: (_sublist->parentChat()
				? tr::lng_forum_messages
				: tr::lng_profile_saved_messages)(
					tr::now,
					lt_count_decimal,
					*result.fullCount));
		markLoaded();
	});
}

void ChatWidget::maybeUpdateLastKeyboardFromSlice(
		const Data::MessagesSlice &slice,
		bool force) {
	if (mode() == Mode::Sublist) {
		return;
	}
	if (slice.skippedAfter.value_or(1) != 0) {
		return;
	}
	const auto inited = (mode() == Mode::History)
		? _history->lastKeyboardInited
		: _repliesKeyboardInited;
	if (inited && !force) {
		return;
	}
	const auto currentId = keyboardSourceId();
	using Flag = ReplyMarkupFlag;
	for (auto i = slice.ids.rbegin(); i != slice.ids.rend(); ++i) {
		const auto item = session().data().message(*i);
		if (!item
				|| !itemBelongsToKeyboardView(item)
				|| item->isService()
				|| item->out()
				|| !item->definesReplyKeyboard()) {
			continue;
		}
		const auto flags = item->replyKeyboardFlags();
		if ((flags & Flag::Selective) && !item->mentionsMe()) {
			continue;
		}
		if (flags & Flag::None) {
			if (mode() == Mode::History) {
				if (currentId || !inited) {
					_history->clearLastKeyboard();
				}
			} else {
				if (currentId || !inited) {
					clearRepliesKeyboardState();
					updateBotKeyboard();
				}
			}
			return;
		}
		if (currentId == item->fullId()) {
			return;
		}
		if (mode() == Mode::History) {
			item->history()->setLastKeyboard(item->id, item->author()->id);
		} else {
			setRepliesKeyboardState(item->id);
			updateBotKeyboard();
		}
		return;
	}
	if (currentId || !inited) {
		if (mode() == Mode::History) {
			_history->clearLastKeyboard();
		} else {
			clearRepliesKeyboardState();
			updateBotKeyboard();
		}
	}
}

void ChatWidget::requestSponsoredMessages() {
	if (mode() != Mode::History || _topic) {
		return;
	} else if (session().sponsoredMessages().isTopBarFor(_history)) {
		return;
	}
	const auto checkState = [=] {
		using State = Data::SponsoredMessages::State;
		const auto state = session().sponsoredMessages().state(_history);
		if (state == State::InjectToMiddle) {
			injectSponsoredMessages();
		}
	};
	const auto history = _history;
	session().sponsoredMessages().request(
		_history,
		crl::guard(this, [=] {
			if (history == _history) {
				checkState();
			}
		}));
	checkState();
}

void ChatWidget::injectSponsoredMessages() {
	if (mode() != Mode::History || _topic || _injectingSponsored) {
		return;
	}
	_injectingSponsored = true;
	while (injectNextSponsoredMessage()) {
	}
	_injectingSponsored = false;
}

bool ChatWidget::injectNextSponsoredMessage() {
	auto &sponsored = session().sponsoredMessages();
	const auto state = sponsored.injectState(_history);
	if (!state) {
		return false;
	}
	const auto useUnreadBar = !state->lastInjected
		&& (_lastShownAt.msg == ShowAtUnreadMsgId);
	const auto anchor = state->lastInjected
		? state->lastInjected
		: useUnreadBar
		? nullptr
		: session().data().message(_lastShownAt);
	if (!anchor && !useUnreadBar) {
		return false;
	}
	const auto lookup = _inner->lookupInjectAfter(
		anchor,
		state->postsBetween,
		_scroll->height() * 2);
	if (!lookup.after
		|| (lookup.ranOffEnd
			&& (state->injectedAny
				|| !_inner->loadedAtBottomKnown()
				|| !_inner->loadedAtBottom()))) {
		return false;
	}
	const auto after = not_null{ lookup.after };
	const auto item = sponsored.injectItem(_history, after);
	return (item != nullptr) && _inner->insertAfter(after, item);
}

bool ChatWidget::appendSponsoredMessages() {
	if (mode() != Mode::History
		|| _topic
		|| !_inner->loadedAtBottomKnown()
		|| !_inner->loadedAtBottom()) {
		return false;
	}
	using Result = Data::SponsoredMessages::AppendResult;
	const auto tryToAppend = [=] {
		const auto result = session().sponsoredMessages().append(_history);
		if (result == Result::Appended && !showAppendedSponsored()) {
			return Result::None;
		}
		return result;
	};
	const auto result = tryToAppend();
	if (result == Result::MediaLoading
		&& !_historySponsoredPreloading) {
		session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			if (tryToAppend() != Result::MediaLoading) {
				_historySponsoredPreloading.destroy();
			}
		}, _historySponsoredPreloading);
	}
	return (result == Result::Appended);
}

bool ChatWidget::showAppendedSponsored() {
	auto shown = false;
	for (const auto &item : _history->clientSideMessages()) {
		if (item->isSponsored() && _inner->appendToEnd(item)) {
			shown = true;
		}
	}
	return shown;
}

rpl::producer<Data::MessagesSlice> ChatWidget::historySource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return Data::HistoryMessagesViewer(
		_history,
		aroundId,
		limitBefore,
		limitAfter
	) | rpl::before_next([=](const Data::MessagesSlice &slice) {
		markLoaded();
		maybeUpdateLastKeyboardFromSlice(slice);
	});
}

bool ChatWidget::listAllowsMultiSelect() {
	return true;
}

bool ChatWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return item->canBeSelected();
}

bool ChatWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return _sublist
		? (first->id < second->id)
		: first->position() < second->position();
}

void ChatWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto &item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canForward) {
			++state.canForwardCount;
		}
	}
	_topBar->showSelected(state);
	if (_chooseForReport && _chooseForReport->active) {
		_bottom->updateReportMessagesText(state.count);
	}
	if ((state.count > 0) && _composeSearch) {
		_composeSearch->hideAnimated();
	}
	if (!_inner->hasFocus() || !Ui::ScreenReaderModeActive()) {
		doSetInnerFocus();
	}
}

void ChatWidget::listMarkReadTill(not_null<HistoryItem*> item) {
	if (_replies) {
		_replies->readTill(item);
	} else if (_sublist) {
		_sublist->readTill(item);
	} else {
		session().data().histories().readInboxTill(item);
	}
}

void ChatWidget::listItemsAddedToEnd(
		const std::vector<not_null<Element*>> &items,
		int addedCount) {
	if (!_inner->markingMessagesRead()) {
		return;
	}
	const auto count = std::min(addedCount, int(items.size()));
	if (!count) {
		return;
	}
	auto readTill = (HistoryItem*)nullptr;
	for (auto i = end(items) - count; i != end(items); ++i) {
		const auto view = *i;
		const auto item = view->data();
		if (item->isRegular()
			&& !item->out()
			&& item->showNotification()
			&& listElementShownUnread(view)) {
			readTill = item;
		}
	}
	if (!readTill) {
		return;
	}
	if (readTill->isUnreadMention() && !readTill->isUnreadMedia()) {
		session().api().markContentsRead(readTill);
	}
	if (_replies || _sublist) {
		readTill->markClientSideAsRead();
		listMarkReadTill(readTill);
	} else {
		_inner->clearUnreadBar();
		session().data().histories().readInboxOnNewMessage(readTill);
	}
}

void ChatWidget::listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	session().api().markContentsRead(items);
}

bool ChatWidget::listAllowsReadEffect(not_null<const Element*>) {
	return true;
}

MessagesBarData ChatWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) {
	if (elements.empty()) {
		return {};
	} else if (_sublist && !_sublist->parentChat()) {
		return {};
	}
	const auto repliesTill = _replies
		? _replies->computeInboxReadTillFull()
		: MsgId();
	const auto sublistTill = _sublist
		? _sublist->computeInboxReadTillFull()
		: MsgId();
	const auto migrated = (_replies || _sublist)
		? nullptr
		: _history->migrateFrom();
	const auto migratedTill = (migrated && migrated->unreadCount() > 0)
		? migrated->inboxReadTillId()
		: 0;
	const auto historyTill = (_replies
		|| _sublist
		|| !_history->unreadCount()
		|| _history->amMonoforumAdmin())
		? 0
		: _history->inboxReadTillId();
	if (!_replies && !_sublist && !migratedTill && !historyTill) {
		return {};
	}
	const auto hidden = (_replies && (repliesTill < 2))
		|| (_sublist && (sublistTill < 2));
	auto skippedReadIncoming = false;
	for (auto i = 0, count = int(elements.size()); i != count; ++i) {
		const auto item = elements[i]->data();
		if (!item->isRegular()
			|| (_replies && !item->replyToId())
			|| (_sublist && !item->sublistPeerId())) {
			continue;
		}
		const auto inHistory = (item->history() == _history);
		const auto unread = (_replies && item->id > repliesTill)
			|| (_sublist && item->id > sublistTill)
			|| (migratedTill && (inHistory || item->id > migratedTill))
			|| (historyTill && inHistory && item->id > historyTill);
		if (unread
			&& (_replies || _sublist)
			&& (markLastAsRead
				|| item->out()
				|| (_replies && !item->replyToId()))) {
			if (markLastAsRead) {
				if (item->isUnreadMention() && !item->isUnreadMedia()) {
					session().api().markContentsRead(item);
				}
				item->markClientSideAsRead();
			}
			if (_replies) {
				_replies->readTill(item);
			} else {
				_sublist->readTill(item);
			}
			continue;
		}
		if (!item->out() && !unread) {
			skippedReadIncoming = true;
		}
		if (item->out()) {
			continue;
		}
		if (!unread) {
			continue;
		}
		if (markLastAsRead) {
			if (item->isUnreadMention() && !item->isUnreadMedia()) {
				session().api().markContentsRead(item);
			}
			listMarkReadTill(item);
			continue;
		}
		if (!skippedReadIncoming && !_replies && !_sublist) {
			return {};
		}
		return {
			.bar = {
				.element = elements[i],
				.hidden = hidden,
				.focus = true,
			},
			.text = tr::lng_unread_bar_some(),
		};
	}
	return {};
}

void ChatWidget::listContentRefreshed() {
	injectSponsoredMessages();
	checkMaybeSendBotStart();
	refreshAboutView();
	_bottom->updateControlsVisibility();
}

void ChatWidget::listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) {
	const auto key = _topic
		? Dialogs::Key(_topic)
		: (mode() == Mode::History)
		? Dialogs::Key(_history)
		: Dialogs::Key();
	if (!key) {
		link = nullptr;
		return;
	}
	const auto date = view->dateTime().date();
	if (!link) {
		link = std::make_shared<Window::DateClickHandler>(key, date);
	} else {
		static_cast<Window::DateClickHandler*>(link.get())->setDate(date);
	}
}

bool ChatWidget::listElementHideReply(not_null<const Element*> view) {
	if (_sublist) {
		return false;
	} else if (const auto reply = view->data()->Get<HistoryMessageReply>()) {
		const auto replyToPeerId = reply->externalPeerId()
			? reply->externalPeerId()
			: _peer->id;
		if (reply->fields().manualQuote) {
			return false;
		} else if (view->isTopicRootReply()) {
			return true;
		} else if (replyToPeerId == _peer->id) {
			return (_repliesRootId && reply->messageId() == _repliesRootId);
		} else if (const auto root = _repliesRoot) {
			const auto forwarded = root->Get<HistoryMessageForwarded>();
			if (forwarded
				&& forwarded->savedFromPeer
				&& forwarded->savedFromPeer->id == replyToPeerId
				&& forwarded->savedFromMsgId == reply->messageId()) {
				return true;
			}
		}
	}
	return false;
}

bool ChatWidget::listElementShownUnread(not_null<const Element*> view) {
	const auto item = view->data();
	return _replies
		? _replies->isServerSideUnread(item)
		: _sublist
		? _sublist->isServerSideUnread(item)
		: item->unread(item->history());
}

bool ChatWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return view->data()->isRegular();
}

void ChatWidget::listSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
	if (!_sublist || _sublist->parentChat()) {
		sendBotCommand({
			.peer = _peer,
			.command = command,
			.context = context,
		}, {});
	}
}

void ChatWidget::sendBotCommand(
		Bot::SendCommandRequest request,
		Api::SendOptions options) {
	if (_peer != request.peer.get()) {
		return;
	}
	const auto action = prepareSendAction(options);
	const auto keyboardId = keyboardSourceIdForHiddenState();
	const auto lastKeyboardUsed = keyboardId
		&& (keyboardId == request.replyTo.messageId);
	const auto outgoingReplyTo = request.replyTo
		? (!_peer->isUser() ? request.replyTo : replyTo())
		: ((mode() != Mode::History) ? replyTo() : FullReplyTo());
	const auto toSend = request.replyTo
		? request.command
		: Bot::WrapCommandInChat(_peer, request.command, request.context);
	auto message = Api::MessageToSend(action);
	message.textWithTags = { toSend, TextWithTags::Tags() };
	message.action.replyTo = outgoingReplyTo;
	const auto ephemeral = session().ephemeralMessages().wouldSend(message);
	if (!ephemeral && showSlowmodeError()) {
		return;
	}
	if (!ephemeral) {
		const auto withPaymentApproved = [=, request = request](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendBotCommand(request, copy);
		};
		const auto checked = checkSendPayment(
			1,
			message.action.options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}
	session().api().sendMessage(std::move(message));
	if (request.replyTo) {
		const auto replyMatches = (_composeControls->replyingToMessage()
			.messageId == outgoingReplyTo.messageId);
		if (replyMatches) {
			cancelReply();
		}
		if (_keyboard
				&& _keyboard->singleUse()
				&& _keyboard->hasMarkup()
				&& lastKeyboardUsed) {
			if (_kbShown) {
				toggleBotKeyboard(false);
			}
			markKeyboardUsed();
		}
	}
	finishSending();
}

void ChatWidget::updateBotKeyboard(History *h, bool force) {
	if (!_keyboard) {
		return;
	}
	if (h && h != _history) {
		return;
	}

	const auto rowsWereVisible = _kbShown && keyboardRowsVisible();
	const auto wasKbShown = _kbShown;
	const auto wasReplyExternal = _keyboardReplyExternalVisible;
	const auto wasMsgId = _keyboard->forMsgId();
	auto changed = false;
	const auto reply = _composeControls->replyingToMessage();
	auto replyItem = reply
		? session().data().message(reply.messageId)
		: nullptr;
	if (replyItem && !itemBelongsToKeyboardView(replyItem)) {
		replyItem = nullptr;
	}
	const auto editing = _composeControls->isEditingMessage();
	const auto realReplySource = reply && replyItem;
	const auto missingReplySource = reply && !replyItem;
	if (editing || missingReplySource) {
		changed = _keyboard->updateMarkup(nullptr, force);
	} else if (realReplySource) {
		changed = _keyboard->updateMarkup(replyItem, force);
	} else {
		changed = _keyboard->updateMarkup(keyboardSourceItem(), force);
	}
	if (changed && _keyboard->forMsgId() != wasMsgId) {
		_kbScroll->scrollTo({ 0, 0 });
	}

	const auto hasMarkup = _keyboard->hasMarkup();
	const auto sourceId = keyboardSourceIdForHiddenState();
	if (_keyboard->singleUse()
		&& hasMarkup
		&& (sourceId == _keyboard->forMsgId())
		&& keyboardUsed()) {
		setKeyboardHiddenId(sourceId.msg);
	}
	const auto forceReply = _keyboard->forceReply() && !realReplySource;
	const auto canShow = _canSendMessages
		&& !_bottom->isButtonActive()
		&& !_composeSearch;
	if (isChoosingTheme()) {
		_kbScroll->hide();
		_kbShown = false;
		setKeyboardReplyTo(nullptr);
		hideKeyboardReplyToExternal();
		updateKeyboardUiState(hasMarkup, true);
		updateControlsGeometry();
		update();
		return;
	}
	const auto rowsFromRealReply = realReplySource && hasMarkup;
	const auto suppressKeyboardUi = editing || (reply && !rowsFromRealReply);
	const auto shouldShowRows = canShow
		&& hasMarkup
		&& !editing
		&& !missingReplySource
		&& (rowsFromRealReply
			|| rowsWereVisible
			|| (!reply && !_fieldHasSendText && !kbWasHidden()));
	const auto shouldShowFakeReply = canShow
		&& !realReplyOrEditActive()
		&& (forceReply
			|| (shouldShowRows
				&& (_peer->isChat() || _peer->isChannel())));
	if (hasMarkup || forceReply) {
		if (canShow && (shouldShowRows || shouldShowFakeReply)) {
			if (shouldShowRows) {
				_kbScroll->show();
			} else {
				_kbScroll->hide();
			}
			_kbShown = shouldShowRows;
			setKeyboardReplyTo(shouldShowFakeReply
				? session().data().message(_keyboard->forMsgId())
				: nullptr);
			if (_kbReplyTo) {
				showKeyboardReplyToExternal();
			} else {
				hideKeyboardReplyToExternal();
			}
		} else {
			_kbScroll->hide();
			if (canShow && !suppressKeyboardUi) {
				_kbShown = false;
				setKeyboardReplyTo(nullptr);
			}
			hideKeyboardReplyToExternal();
		}
	} else {
		_kbScroll->hide();
		_kbShown = false;
		setKeyboardReplyTo(nullptr);
		hideKeyboardReplyToExternal();
	}

	updateKeyboardUiState(hasMarkup, suppressKeyboardUi);
	if (changed
		|| force
		|| (_kbShown != wasKbShown)
		|| (_keyboardReplyExternalVisible != wasReplyExternal)) {
		updateControlsGeometry();
		update();
	}
}

void ChatWidget::toggleBotKeyboard(bool manual) {
	if (!_keyboard) {
		return;
	}
	const auto fieldEnabled = _canSendMessages && !_bottom->isButtonActive();
	const auto rowsVisible = keyboardRowsVisible();
	const auto externalVisible = _keyboardReplyExternalVisible;
	const auto hiddenStateSourceId = keyboardSourceIdForHiddenState();
	if (rowsVisible) {
		if (manual && hiddenStateSourceId) {
			setKeyboardHiddenId(hiddenStateSourceId.msg);
		}
		_kbScroll->hide();
		_kbShown = false;
		setKeyboardReplyTo(nullptr);
		hideKeyboardReplyToExternal();
	} else if (externalVisible) {
		if (manual) {
			if (mode() == Mode::History) {
				_history->clearLastKeyboard();
			} else if (mode() == Mode::Replies) {
				clearRepliesKeyboardState();
			}
		}
		_kbScroll->hide();
		_kbShown = false;
		setKeyboardReplyTo(nullptr);
		hideKeyboardReplyToExternal();
	} else if (_kbShown || _kbReplyTo) {
		updateBotKeyboard();
		return;
	} else if (!_keyboard->hasMarkup() && _keyboard->forceReply()) {
		_kbScroll->hide();
		_kbShown = false;
		setKeyboardReplyTo((_peer->isChat()
				|| _peer->isChannel()
				|| _keyboard->forceReply())
			? session().data().message(_keyboard->forMsgId())
			: nullptr);
		if (_kbReplyTo
				&& fieldEnabled
				&& !realReplyOrEditActive()) {
			showKeyboardReplyToExternal();
		} else {
			hideKeyboardReplyToExternal();
		}
		if (manual && hiddenStateSourceId) {
			clearKeyboardHiddenId();
		}
	} else if (fieldEnabled) {
		_kbScroll->show();
		_kbShown = true;
		setKeyboardReplyTo((_peer->isChat()
				|| _peer->isChannel()
				|| _keyboard->forceReply())
			? session().data().message(_keyboard->forMsgId())
			: nullptr);
		if (_kbReplyTo && !realReplyOrEditActive()) {
			showKeyboardReplyToExternal();
		} else {
			hideKeyboardReplyToExternal();
		}
		if (manual && hiddenStateSourceId) {
			clearKeyboardHiddenId();
		}
	}
	updateKeyboardUiState(
		_keyboard->hasMarkup(),
		keyboardUiSuppressedByReplyOrEdit());
	updateControlsGeometry();
	update();
}

void ChatWidget::botCallbackSent(not_null<HistoryItem*> item) {
	if (!item->isRegular() || !_keyboard || _peer != item->history()->peer) {
		return;
	}
	const auto keyId = keyboardSourceIdForHiddenState();
	const auto lastKeyboardUsed = (keyId == FullMsgId(_peer->id, item->id));
	session().data().requestItemRepaint(item);
	const auto reply = _composeControls->replyingToMessage();
	if (reply.messageId == item->fullId()) {
		cancelReply();
	}
	if (_keyboard->singleUse()
			&& _keyboard->hasMarkup()
			&& lastKeyboardUsed) {
		if (keyboardRowsVisible()) {
			toggleBotKeyboard(false);
		}
		markKeyboardUsed();
	}
}

bool ChatWidget::kbWasHidden() const {
	return _keyboard
		&& (keyboardSourceIdForHiddenState()
			== FullMsgId(_peer->id, keyboardHiddenId()));
}

bool ChatWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	return (replyTo.peer == _peer->id)
		&& _keyboard
		&& _keyboard->forceReply()
		&& (_keyboard->forMsgId() == keyboardSourceId())
		&& (_keyboard->forMsgId().msg == replyTo.msg);
}

bool ChatWidget::lastForceReplyReplied() const {
	return _keyboard
		&& _keyboard->forceReply()
		&& _keyboard->forMsgId() == replyTo().messageId
		&& (_keyboard->forMsgId() == keyboardSourceId());
}

bool ChatWidget::cancelReply(bool lastKeyboardUsed) {
	const auto wasReply = bool(_composeControls->replyingToMessage());
	_composeControls->cancelReplyMessage();
	if (wasReply) {
		updateBotKeyboard();
		refreshTopBarActiveChat();
		updateControlsVisibility();
		updateControlsGeometry();
		update();
	}
	if (!_composeControls->isEditingMessage()
			&& _keyboard
			&& _keyboard->singleUse()
			&& _keyboard->forceReply()
			&& lastKeyboardUsed) {
		if (_kbReplyTo) {
			toggleBotKeyboard(false);
		}
	}
	return wasReply;
}

void ChatWidget::listSearch(
		const QString &query,
		const FullMsgId &context) {
	const auto inChat = !_sublist
		? Dialogs::Key(_history)
		: Data::SearchTagFromQuery(query)
		? Dialogs::Key(_sublist)
		: Dialogs::Key();
	controller()->searchMessages(query, inChat);
}

void ChatWidget::listHandleViaClick(not_null<UserData*> bot) {
	const auto canSendTexts = (mode() == Mode::History)
		? Data::CanSend(_peer, ChatRestriction::SendOther)
		: _bottom->canSendTexts();
	if (canSendTexts) {
		_composeControls->setText({ '@' + bot->username() + ' ' });
		setInnerFocus();
	}
}

not_null<Ui::ChatTheme*> ChatWidget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType ChatWidget::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionTypeFor(_peer, item);
}

CopyRestrictionType ChatWidget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyMediaRestrictionTypeFor(_peer, item);
}

CopyRestrictionType ChatWidget::listSelectRestrictionType() {
	return SelectRestrictionTypeFor(_peer);
}

auto ChatWidget::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return Data::PeerAllowedReactionsValue(_peer);
}

void ChatWidget::listShowPremiumToast(not_null<DocumentData*> document) {
	if (!_stickerToast) {
		_stickerToast = std::make_unique<HistoryView::StickerToast>(
			controller(),
			this,
			[=] { _stickerToast = nullptr; });
	}
	_stickerToast->showFor(document);
}

bool ChatWidget::handleDrawToReplyRequest(Data::DrawToReplyRequest request) {
	if (request.messageId.peer != _peer->id) {
		return false;
	}
	auto image = ResolveDrawToReplyImage(&session().data(), request);
	if (image.isNull()) {
		return false;
	}
	const auto replyTo = request.messageId;
	OpenDrawToReplyEditor(
		controller(),
		std::move(image),
		crl::guard(this, [=](QImage &&result) {
			if (result.isNull()) {
				return;
			}
			if (replyTo) {
				const auto item = session().data().message(replyTo);
				if (!item
					|| !item->isEphemeral()
					|| CanReplyToEphemeral(item)) {
					replyToMessage({ .messageId = replyTo });
				}
			}
			auto list = Storage::PrepareMediaFromImage(
				std::move(result),
				QByteArray(),
				st::sendMediaPreviewSize);
			confirmSendingFiles(std::move(list));
		}));
	return true;
}

void ChatWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	const auto item = session().data().message(context);
	const auto showDrawButton = CanSendResolved(
		_peer,
		resolvedTopic(),
		SendPermission::Files);
	controller()->openPhoto(
		photo,
		{
			context,
			(item && !_monoforumPeerId)
				? item->topicRootId()
				: _repliesRootId,
			_monoforumPeerId,
			showDrawButton,
		});
}

void ChatWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	const auto item = session().data().message(context);
	const auto showDrawButton = CanSendResolved(
		_peer,
		resolvedTopic(),
		SendPermission::Files);
	controller()->openDocument(
		document,
		showInMediaView,
		{
			context,
			(item && !_monoforumPeerId)
				? item->topicRootId()
				: _repliesRootId,
			_monoforumPeerId,
			showDrawButton,
		});
}

void ChatWidget::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
	if (_aboutView && _aboutView->view()) {
		return;
	} else if (!emptyShown()) {
		return;
	} else if (!_emptyPainter) {
		setupEmptyPainter();
	}
	_emptyPainter->paint(p, context.st, width(), _scroll->height());
}

QString ChatWidget::listElementAuthorRank(not_null<const Element*> view) {
	return (_topic && view->data()->from()->id == _topic->creatorId())
		? tr::lng_topic_author_badge(tr::now)
		: QString();
}

bool ChatWidget::listElementHideTopicButton(
		not_null<const Element*> view) {
	return _repliesRootId != 0;
}

History *ChatWidget::listTranslateHistory() {
	return _history;
}

void ChatWidget::listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) {
	if (_topControls) {
		_topControls->addTranslatedItems(tracker);
	}
}

Ui::ChatPaintContext ChatWidget::listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) {
	auto context = WindowListDelegate::listPreparePaintContext(
		std::move(args));
	context.gestureHorizontal = _gestureHorizontal;
	return context;
}

base::unique_qptr<Ui::PopupMenu> ChatWidget::listFillSenderUserpicMenu(
		PeerId userpicPeerId) {
	const auto searchInEntry = _topic
		? Dialogs::Key(_topic)
		: Dialogs::Key(_history);
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto senderPeer = _history->owner().peer(userpicPeerId);
	const auto groupPeer = (_history->peer->isChat()
		|| _history->peer->isMegagroup())
		? _history->peer.get()
		: nullptr;
	Window::FillSenderUserpicMenu(
		controller(),
		senderPeer,
		groupPeer,
		_composeControls->fieldForMention(),
		searchInEntry,
		Ui::Menu::CreateAddActionCallback(menu.get()));
	return menu->empty() ? nullptr : std::move(menu);
}

Ui::ElasticScroll *ChatWidget::listScrollArea() const {
	return _scroll.get();
}

bool ChatWidget::listShowForumThreadBars() const {
	return (mode() == Mode::History)
		&& !_topic
		&& _history->hasForumThreadBars();
}

void ChatWidget::setupEmptyPainter() {
	if (!_topic) {
		_emptyPainter = std::make_unique<EmptyPainter>(_history);
		return;
	}
	_emptyPainter = std::make_unique<EmptyPainter>(_topic, [=] {
		return controller()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, [=] {
		if (emptyShown()) {
			update();
		} else {
			_emptyPainter = nullptr;
		}
	});
}

void ChatWidget::refreshAboutView(bool force) {
	if (mode() != Mode::History) {
		return;
	}
	const auto refreshExisting = [&] {
		const auto was = _aboutView->view();
		if (_aboutView->refresh()) {
			_inner->aboutViewReplaced(was);
			_inner->updateSize();
			_inner->update();
		}
	};
	const auto refresh = [&] {
		if (force) {
			_inner->setAboutView(nullptr);
			_aboutView = nullptr;
		}
		if (!_aboutView) {
			_aboutView = std::make_unique<HistoryView::AboutView>(
				_history,
				_inner.data());
			_aboutView->setDisplayedEmptyOverride([=] {
				return _inner->isEmpty();
			});
			_aboutView->refreshRequests() | rpl::on_next([=] {
				if (_aboutView) {
					const auto was = _aboutView->view();
					if (_aboutView->refresh()) {
						_inner->aboutViewReplaced(was);
						_inner->updateSize();
						_inner->update();
					}
				}
			}, _aboutView->lifetime());
			_aboutView->destroyRequests() | rpl::on_next([=] {
				crl::on_main(this, [=] {
					refreshAboutView(true);
					update();
				});
			}, _aboutView->lifetime());
			_aboutView->sendIntroSticker() | rpl::on_next([=](
					not_null<DocumentData*> sticker) {
				sendExistingDocument(
					sticker,
					Api::MessageToSend(prepareSendAction({})),
					std::nullopt);
			}, _aboutView->lifetime());
			_inner->setAboutView(_aboutView.get());
		}
		if (_aboutView) {
			refreshExisting();
		}
	};
	const auto destroy = [&] {
		if (_aboutView) {
			_inner->setAboutView(nullptr);
			_aboutView = nullptr;
			_inner->updateSize();
			_inner->update();
		}
	};
	if (const auto user = _peer->asUser()) {
		if (const auto info = user->botInfo.get()) {
			refresh();
			if (!info->inited) {
				session().api().requestFullPeer(user);
			}
		} else if (!user->isContact()
			&& !user->phoneCountryCode().isEmpty()) {
			refresh();
		} else if (_inner->isEmpty()) {
			if (user->starsPerMessage() > 0
				|| (user->requiresPremiumToWrite()
					&& !user->session().premium())
				|| user->isFullLoaded()) {
				refresh();
			} else {
				session().api().requestFullPeer(user);
			}
		} else {
			destroy();
		}
	} else if (const auto monoforum = _peer->asChannel()) {
		if (monoforum->isMonoforum() && !monoforum->amMonoforumAdmin()) {
			refresh();
		} else {
			destroy();
		}
	} else {
		destroy();
	}
}

void ChatWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void ChatWidget::confirmForwardSelected() {
	ConfirmForwardSelectedItems(_inner);
}

void ChatWidget::clearSelected() {
	_inner->cancelSelection();
}

void ChatWidget::setupDragArea() {
	const auto filter = [=](const auto &d) {
		if (!_history || _composeControls->isRecording()) {
			return false;
		}
		return CanSendResolved(
			_peer,
			resolvedTopic(),
			SendPermission::Files);
	};
	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		filter,
		nullptr,
		[=] { updateControlsGeometry(); });

	const auto droppedCallback = [=](bool overrideSendImagesAsPhotos) {
		return [=](const QMimeData *data) {
			confirmSendingFiles(data, overrideSendImagesAsPhotos);
			Window::ActivateWindow(controller());
		};
	};
	areas.document->setDroppedCallback(droppedCallback(false));
	areas.photo->setDroppedCallback(droppedCallback(true));
}

void ChatWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !controller()->isLayerShown()
			&& (Core::App().activeWindow() == &controller()->window());
	}) | rpl::on_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 1) && request->handle([=] {
			searchRequested();
			return true;
		});
		request->check(Command::ShowChatMenu, 1) && request->handle([=] {
			Window::ActivateWindow(controller());
			_topBar->showPeerMenu();
			return true;
		});
		_canSendMessages
			&& request->check(Command::ShowScheduled, 1)
			&& request->handle([=] {
				controller()->showSection(_topic
					? std::make_shared<HistoryView::ScheduledMemento>(_topic)
					: std::make_shared<HistoryView::ScheduledMemento>(
						_history));
				return true;
			});
		if (mode() == Mode::History) {
			const auto channel = _peer->asChannel();
			const auto hasRecentActions = channel
				&& (channel->hasAdminRights() || channel->amCreator());
			if (hasRecentActions) {
				request->check(Command::ShowAdminLog, 1) && request->handle([=] {
					controller()->showSection(
						std::make_shared<AdminLog::SectionMemento>(channel));
					return true;
				});
			}
		}
		if ((mode() == Mode::History) && session().supportMode()) {
			request->check(Command::SupportToggleMuted)
				&& request->handle([=] {
					toggleMuteUnmute();
					return true;
				});
		}
	}, lifetime());
}

void ChatWidget::searchRequested() {
	if (_composeSearch) {
		_composeSearch->setInnerFocus();
	} else if (_sublist) {
		controller()->searchInChat(_sublist);
	} else if (mode() == Mode::History) {
		controller()->searchInChat(_history);
	} else if (!preventsClose(crl::guard(this, [=] { searchInTopic(); }))) {
		searchInTopic();
	}
}

void ChatWidget::searchInTopic() {
	if (_topic) {
		controller()->searchInChat(_topic);
	} else {
		const auto update = [=] {
			if (_composeSearch) {
				_composeControls->hide();
			} else {
				_composeControls->show();
			}
			updateBotKeyboard();
			updateControlsGeometry();
		};
		const auto from = (PeerData*)(nullptr);
		_composeSearch = std::make_unique<HistoryView::ComposeSearch>(
			this,
			controller(),
			_history,
			from);
		_composeSearch->setTopMsgId(_repliesRootId);

		update();
		doSetInnerFocus();

		using Activation = HistoryView::ComposeSearch::Activation;
		_composeSearch->activations(
		) | rpl::on_next([=](Activation activation) {
			auto params = Window::SectionShow();
			params.highlight = Window::SearchHighlightId(activation.query);
			showAtPosition(activation.item->position(), {}, params);
		}, _composeSearch->lifetime());

		_composeSearch->destroyRequests(
		) | rpl::take(1) | rpl::on_next([=] {
			_composeSearch = nullptr;

			update();
			doSetInnerFocus();
		}, _composeSearch->lifetime());
	}
}

bool ChatWidget::searchInChatEmbedded(
		QString query,
		Dialogs::Key chat,
		PeerData *searchFrom) {
	const auto sublist = chat.sublist();
	if (!sublist) {
		if ((mode() != Mode::History) || (chat.history() != _history)) {
			return false;
		} else if (_composeSearch) {
			_composeSearch->setQuery(query);
			_composeSearch->setInnerFocus();
			return true;
		}
		const auto search = crl::guard(this, [=] {
			const auto update = [=] {
				if (_composeSearch) {
					_composeControls->hide();
				} else {
					_composeControls->show();
				}
				updateBotKeyboard();
				updateControlsGeometry();
			};
			_composeSearch = std::make_unique<ComposeSearch>(
				this,
				controller(),
				_history,
				searchFrom,
				query);
			_composeSearch->setCalendarChat(Dialogs::Key(_history));

			update();
			doSetInnerFocus();

			using Activation = ComposeSearch::Activation;
			_composeSearch->activations(
			) | rpl::on_next([=](Activation activation) {
				auto params = Window::SectionShow(
					Window::SectionShow::Way::Forward,
					anim::type::instant);
				params.highlight = Window::SearchHighlightId(
					activation.query);
				showAtPosition(activation.item->position(), {}, params);
			}, _composeSearch->lifetime());

			_composeSearch->destroyRequests(
			) | rpl::take(1) | rpl::on_next([=] {
				_composeSearch = nullptr;

				update();
				doSetInnerFocus();
			}, _composeSearch->lifetime());
		});
		if (!preventsClose(search)) {
			search();
		}
		return true;
	}
	if (sublist != _sublist) {
		return false;
	} else if (_composeSearch) {
		_composeSearch->setQuery(query);
		_composeSearch->setInnerFocus();
		return true;
	}
	_composeSearch = std::make_unique<ComposeSearch>(
		this,
		controller(),
		_history,
		sublist->sublistPeer(),
		query);
	_composeSearch->setCalendarChat(Dialogs::Key(sublist));

	updateControlsGeometry();
	setInnerFocus();

	_composeSearch->activations(
	) | rpl::on_next([=](ComposeSearch::Activation activation) {
		const auto item = activation.item;
		auto params = ::Window::SectionShow(
			::Window::SectionShow::Way::ClearStack);
		params.highlight = Window::SearchHighlightId(activation.query);
		controller()->showPeerHistory(
			item->history()->peer->id,
			params,
			item->fullId().msg);
	}, _composeSearch->lifetime());

	_composeSearch->destroyRequests(
	) | rpl::take(
		1
	) | rpl::on_next([=] {
		_composeSearch = nullptr;

		updateControlsGeometry();
		setInnerFocus();
	}, _composeSearch->lifetime());

	return true;
}

} // namespace HistoryView
