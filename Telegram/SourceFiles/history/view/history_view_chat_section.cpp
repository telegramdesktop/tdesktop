/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_chat_section.h"

#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/controls/history_view_compose_search.h"
#include "history/view/controls/history_view_draft_options.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_scheduled_section.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_subsection_tabs.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/history_view_translate_tracker.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h" // GetErrorForSending.
#include "ui/chat/pinned_bar.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/swipe_handler.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/ui_utility.h"
#include "base/timer_rpl.h"
#include "api/api_bot.h"
#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "ui/boxes/confirm_box.h"
#include "chat_helpers/tabbed_selector.h"
#include "boxes/delete_messages_box.h"
#include "boxes/send_files_box.h"
#include "boxes/premium_limits_box.h"
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
#include "data/components/scheduled_messages.h"
#include "data/data_histories.h"
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
#include "data/data_shared_media.h"
#include "data/data_send_action.h"
#include "data/data_premium_limits.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_account.h"
#include "storage/localimageloader.h"
#include "inline_bots/inline_bot_result.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QMimeData>

namespace HistoryView {
namespace {

rpl::producer<Ui::MessageBarContent> RootViewContent(
		not_null<History*> history,
		MsgId rootId,
		Fn<void()> repaint) {
	return MessageBarContentByItemId(
		&history->session(),
		FullMsgId(history->peer->id, rootId),
		std::move(repaint)
	) | rpl::map([=](Ui::MessageBarContent &&content) {
		const auto item = history->owner().message(history->peer, rootId);
		if (!item) {
			content.text = Ui::Text::Link(tr::lng_deleted_message(tr::now));
		}
		const auto sender = (item && item->discussionPostOriginalSender())
			? item->discussionPostOriginalSender()
			: history->peer.get();
		content.title = sender->name().isEmpty()
			? "Message"
			: sender->name();
		return std::move(content);
	});
}

} // namespace

ChatMemento::ChatMemento(
	ChatViewId id,
	MsgId highlightId,
	MessageHighlightId highlight)
: _id(id)
, _highlightId(highlightId)
, _highlight(std::move(highlight)) {
	if (highlightId || _id.sublist) {
		_list.setAroundPosition({
			.fullId = FullMsgId(_id.history->peer->id, highlightId),
			.date = TimeId(0),
		});
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
		) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
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
, _topBars(std::make_unique<Ui::RpWidget>(this))
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
		.scheduledToggleValue = _topic
			? rpl::single(rpl::empty_value()) | rpl::then(
				session().scheduledMessages().updates(_topic->owningHistory())
			) | rpl::map([=] {
				return session().scheduledMessages().hasFor(_topic);
			}) | rpl::type_erased()
			: rpl::single(false),
	}))
, _translateBar(
	std::make_unique<TranslateBar>(_topBars.get(), controller, _history))
, _scroll(std::make_unique<Ui::ScrollArea>(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll),
	false))
, _cornerButtons(
		_scroll.get(),
		controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	controller->chatStyle()->paletteChanged(
	) | rpl::start_with_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	Window::ChatThemeValueFromPeer(
		controller,
		_peer
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	setupRoot();
	setupRootView();
	setupOpenChatButton();
	setupAboutHiddenAuthor();
	setupShortcuts();
	setupTranslateBar();

	_peer->updateFull();

	refreshTopBarActiveChat();

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	if (_repliesRootView) {
		_repliesRootView->move(0, 0);
	}

	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->forwardSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmForwardSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());
	_topBar->searchRequest(
	) | rpl::start_with_next([=] {
		searchRequested();
	}, _topBar->lifetime());

	controller->adaptive().value(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		onScroll();
	}, lifetime());

	_inner->editMessageRequested(
	) | rpl::filter([=] {
		return !_joinGroup;
	}) | rpl::start_with_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			const auto media = item->media();
			if (!media || media->webpage() || media->allowsEditCaption()) {
				_composeControls->editMessage(
					fullId,
					_inner->getSelectedTextRange(item));
			} else if (media->todolist()) {
				Window::PeerMenuEditTodoList(controller, item);
			}
		}
	}, _inner->lifetime());

	_inner->replyToMessageRequested(
	) | rpl::start_with_next([=](ListWidget::ReplyToMessageRequest request) {
		const auto canSendReply = _topic
			? Data::CanSendAnything(_topic)
			: Data::CanSendAnything(_peer);
		const auto &to = request.to;
		const auto still = _history->owner().message(to.messageId);
		const auto allowInAnotherChat = still && still->allowsForward();
		if (allowInAnotherChat
			&& (_joinGroup || !canSendReply || request.forceAnotherChat)) {
			Controls::ShowReplyToChatBox(controller->uiShow(), { to });
		} else if (!_joinGroup && canSendReply) {
			replyToMessage(to);
			_composeControls->focus();
			if (_composeSearch) {
				_composeSearch->hideAnimated();
			}
		}
	}, _inner->lifetime());

	_inner->showMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			showAtPosition(item->position());
		}
	}, _inner->lifetime());

	_composeControls->sendActionUpdates(
	) | rpl::start_with_next([=](ComposeControls::SendActionUpdate &&data) {
		if (!_repliesRootId) {
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
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		if (update.item == _repliesRoot) {
			_repliesRoot = nullptr;
			updatePinnedVisibility();
			if (!_topic) {
				controller->showBackFromStack();
			}
		}
	}, lifetime());

	if (_sublist) {
		subscribeToSublist();
	} else if (!_topic) {
		_history->session().changes().historyUpdates(
			_history,
			Data::HistoryUpdate::Flag::OutboxRead
		) | rpl::start_with_next([=] {
			_inner->update();
		}, lifetime());
	} else {
		session().api().sendActions(
		) | rpl::filter([=](const Api::SendAction &action) {
			return (action.history == _history)
				&& (action.replyTo.topicRootId == _topic->topicRootId());
		}) | rpl::start_with_next([=](const Api::SendAction &action) {
			if (action.options.scheduled) {
				_composeControls->cancelReplyMessage();
				crl::on_main(this, [=, t = _topic] {
					controller->showSection(
						std::make_shared<HistoryView::ScheduledMemento>(t));
				});
			}
		}, lifetime());
	}

	setupTopicViewer();
	setupComposeControls();
	setupSwipeReplyAndBack();
	orderWidgets();

	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
}

ChatWidget::~ChatWidget() {
	base::take(_sendAction);
	if (_repliesRootId || _sublist) {
		session().api().saveCurrentDraftToCloud();
	}
	if (_repliesRootId) {
		controller()->sendingAnimation().clear();
	}
	if (_subsectionTabs && !_subsectionTabs->dying()) {
		_subsectionTabsLifetime.destroy();
		controller()->saveSubsectionTabs(base::take(_subsectionTabs));
	}
	if (_topic) {
		if (_topic->creating()) {
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
	_topBars->raise();
	_translateBar->raise();
	if (_topicReopenBar) {
		_topicReopenBar->bar().raise();
	}
	if (_repliesRootView) {
		_repliesRootView->raise();
	}
	if (_pinnedBar) {
		_pinnedBar->raise();
	}
	if (_subsectionTabs) {
		_subsectionTabs->raise();
	}
	_topBar->raise();
	_topBarShadow->raise();
	_composeControls->raisePanels();
}

void ChatWidget::setupRoot() {
	if (_repliesRootId && !_repliesRoot) {
		const auto done = crl::guard(this, [=] {
			_repliesRoot = lookupRepliesRoot();
			if (_repliesRoot) {
				_areComments = computeAreComments();
				_inner->update();
			}
			updatePinnedVisibility();
		});
		_history->session().api().requestMessageData(
			_peer,
			_repliesRootId,
			done);
	}
}

void ChatWidget::setupRootView() {
	if (_topic || !_repliesRootId) {
		return;
	}
	_repliesRootView = std::make_unique<Ui::PinnedBar>(_topBars.get(), [=] {
		return controller()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, controller()->gifPauseLevelChanged());
	_repliesRootView->setContent(rpl::combine(
		RootViewContent(
			_history,
			_repliesRootId,
			[bar = _repliesRootView.get()] { bar->customEmojiRepaint(); }),
		_repliesRootVisible.value()
	) | rpl::map([=](Ui::MessageBarContent &&content, bool show) {
		const auto shown = !content.title.isEmpty() && !content.text.empty();
		_shownPinnedItem = shown
			? _history->owner().message(_peer->id, _repliesRootId)
			: nullptr;
		return show ? std::move(content) : Ui::MessageBarContent();
	}));

	controller()->adaptive().oneColumnValue(
	) | rpl::start_with_next([=](bool one) {
		_repliesRootView->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _repliesRootView->lifetime());

	_repliesRootView->barClicks(
	) | rpl::start_with_next([=] {
		showAtStart();
	}, lifetime());

	_repliesRootViewHeight = 0;
	_repliesRootView->heightValue(
	) | rpl::start_with_next([=](int height) {
		if (const auto delta = height - _repliesRootViewHeight) {
			_repliesRootViewHeight = height;
			setGeometryWithTopMoved(geometry(), delta);
		}
	}, _repliesRootView->lifetime());
}

void ChatWidget::setupTopicViewer() {
	if (!_repliesRootId) {
		return;
	}
	const auto owner = &_history->owner();
	owner->itemIdChanged(
	) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
		if (_repliesRootId == change.oldId) {
			_repliesRootId = _id.repliesRootId = change.newId.msg;
			_composeControls->updateTopicRootId(_repliesRootId);
			_sendAction = owner->sendActionManager().repliesPainter(
				_history,
				_repliesRootId);
			_repliesRoot = lookupRepliesRoot();
			if (_topic && _topic->rootId() == change.oldId) {
				setTopic(_topic->forum()->topicFor(change.newId.msg));
			} else {
				refreshReplies();
				refreshTopBarActiveChat();
				if (_topic) {
					subscribeToPinnedMessages();
				}
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

	_topicReopenBar = std::make_unique<TopicReopenBar>(
		_topBars.get(),
		_topic);
	_topicReopenBar->bar().setVisible(!animatingShow());
	_topicReopenBarHeight = _topicReopenBar->bar().height();
	_topicReopenBar->bar().heightValue(
	) | rpl::start_with_next([=] {
		const auto height = _topicReopenBar->bar().height();
		_scrollTopDelta = (height - _topicReopenBarHeight);
		if (_scrollTopDelta) {
			_topicReopenBarHeight = height;
			updateControlsGeometry();
			_scrollTopDelta = 0;
		}
	}, _topicReopenBar->bar().lifetime());

	using Flag = Data::TopicUpdate::Flag;
	session().changes().topicUpdates(
		_topic,
		(Flag::UnreadMentions
			| Flag::UnreadReactions
			| Flag::CloudDraft)
	) | rpl::start_with_next([=](const Data::TopicUpdate &update) {
		if (update.flags & (Flag::UnreadMentions | Flag::UnreadReactions)) {
			_cornerButtons.updateUnreadThingsVisibility();
		}
		if (update.flags & Flag::CloudDraft) {
			_composeControls->applyCloudDraft();
		}
	}, _topicLifetime);

	_topic->destroyed(
	) | rpl::start_with_next([=] {
		closeCurrent();
	}, _topicLifetime);

	if (!_topic->creating()) {
		subscribeToPinnedMessages();

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

void ChatWidget::subscribeToPinnedMessages() {
	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	session().changes().entryUpdates(
		EntryUpdateFlag::HasPinnedMessages
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		if (_pinnedTracker
			&& (update.flags & EntryUpdateFlag::HasPinnedMessages)
			&& (_topic == update.entry.get()
				|| _sublist == update.entry.get())) {
			checkPinnedBarState();
		}
	}, lifetime());

	setupPinnedTracker();
}

void ChatWidget::setTopic(Data::ForumTopic *topic) {
	if (_topic == topic) {
		return;
	}
	_topicLifetime.destroy();
	_topic = topic;
	refreshReplies();
	refreshTopBarActiveChat();
	validateSubsectionTabs();
	if (_topic) {
		if (_repliesRootView) {
			_shownPinnedItem = nullptr;
			_repliesRootView = nullptr;
			_repliesRootViewHeight = 0;
		}
		subscribeToTopic();
	}
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

bool ChatWidget::computeAreComments() const {
	return _repliesRoot && _repliesRoot->isDiscussionPost();
}

void ChatWidget::setupComposeControls() {
	auto topicWriteRestrictions = rpl::single(
	) | rpl::then(session().changes().topicUpdates(
		Data::TopicUpdate::Flag::Closed
	) | rpl::filter([=](const Data::TopicUpdate &update) {
		return (update.topic->history() == _history)
			&& (update.topic->rootId() == _repliesRootId);
	}) | rpl::to_empty) | rpl::map([=] {
		const auto topic = _topic
			? _topic
			: _peer->forumTopicFor(_repliesRootId);
		return (!topic || topic->canToggleClosed() || !topic->closed())
			? Data::SendError()
			: tr::lng_forum_topic_closed(tr::now);
	});
	auto writeRestriction = rpl::combine(
		session().frozenValue(),
		session().changes().peerFlagsValue(
			_peer,
			Data::PeerUpdate::Flag::Rights),
		Data::CanSendAnythingValue(_peer),
		(_repliesRootId
			? std::move(topicWriteRestrictions)
			: (rpl::single(Data::SendError()) | rpl::type_erased()))
	) | rpl::map([=](
			const Main::FreezeInfo &info,
			auto,
			auto,
			Data::SendError topicRestriction) {
		if (info) {
			return Controls::WriteRestriction{
				.type = Controls::WriteRestrictionType::Frozen,
			};
		}
		const auto allWithoutPolls = Data::AllSendRestrictions()
			& ~ChatRestriction::SendPolls;
		const auto canSendAnything = _topic
			? Data::CanSendAnyOf(_topic, allWithoutPolls)
			: Data::CanSendAnyOf(_peer, allWithoutPolls);
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
		.topicRootId = _topic ? _topic->rootId() : MsgId(),
		.monoforumPeerId = _monoforumPeerId,
		.showSlowmodeError = [=] { return showSlowmodeError(); },
		.sendActionFactory = [=] { return prepareSendAction({}); },
		.slowmodeSecondsLeft = SlowmodeSecondsLeft(_peer),
		.sendDisabledBySlowmode = SendDisabledBySlowmode(_peer),
		.writeRestriction = std::move(writeRestriction),
	});

	_composeControls->height(
	) | rpl::filter([=] {
		return !_joinGroup;
	}) | rpl::start_with_next([=] {
		const auto wasMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		updateControlsGeometry();
		if (wasMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	}, lifetime());

	_composeControls->cancelRequests(
	) | rpl::start_with_next([=] {
		listCancelRequest();
	}, lifetime());

	_composeControls->sendRequests(
	) | rpl::start_with_next([=](Api::SendOptions options) {
		send(options);
	}, lifetime());

	_composeControls->scrollToMaxRequests(
	) | rpl::start_with_next([=] {
		listScrollTo(_scroll->scrollTopMax());
	}, lifetime());

	_composeControls->sendVoiceRequests(
	) | rpl::start_with_next([=](const ComposeControls::VoiceToSend &data) {
		sendVoice(data);
	}, lifetime());

	_composeControls->sendCommandRequests(
	) | rpl::start_with_next([=](const QString &command) {
		if (showSlowmodeError()) {
			return;
		}
		listSendBotCommand(command, FullMsgId());
		session().api().finishForwarding(prepareSendAction({}));
	}, lifetime());

	const auto saveEditMsgRequestId = lifetime().make_state<mtpRequestId>(0);
	_composeControls->editRequests(
	) | rpl::start_with_next([=](auto data) {
		if (const auto item = session().data().message(data.fullId)) {
			const auto spoiler = data.spoilered;
			edit(item, data.options, saveEditMsgRequestId, spoiler);
		}
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::start_with_next([=](std::optional<bool> overrideCompress) {
		_choosingAttach = true;
		base::call_delayed(
			st::historyAttach.ripple.hideDuration,
			this,
			[=] { chooseAttach(overrideCompress); });
	}, lifetime());

	_composeControls->fileChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
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
	) | rpl::start_with_next([=](ChatHelpers::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo, chosen.options);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::start_with_next([=](ChatHelpers::InlineChosen chosen) {
		controller()->sendingAnimation().appendSending(
			chosen.messageSendingFrom);
		const auto localId = chosen.messageSendingFrom.localId;
		sendInlineResult(chosen.result, chosen.bot, chosen.options, localId);
	}, lifetime());

	_composeControls->jumpToItemRequests(
	) | rpl::start_with_next([=](FullReplyTo to) {
		if (const auto item = session().data().message(to.messageId)) {
			JumpToMessageClickHandler(item, {}, to.highlight())->onClick({});
		}
	}, lifetime());

	rpl::merge(
		_composeControls->scrollKeyEvents(),
		_inner->scrollKeyEvents()
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, lifetime());

	_composeControls->editLastMessageRequests(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		if (!_inner->lastMessageEditRequestNotify()) {
			_scroll->keyPressEvent(e);
		}
	}, lifetime());

	_composeControls->replyNextRequests(
	) | rpl::start_with_next([=](ComposeControls::ReplyNextRequest &&data) {
		using Direction = ComposeControls::ReplyNextRequest::Direction;
		_inner->replyNextMessage(
			data.replyId,
			data.direction == Direction::Next);
	}, lifetime());

	_composeControls->showScheduledRequests(
	) | rpl::start_with_next([=] {
		controller()->showSection(
			_topic
				? std::make_shared<HistoryView::ScheduledMemento>(_topic)
				: std::make_shared<HistoryView::ScheduledMemento>(_history));
	}, lifetime());

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
	) | rpl::start_with_next([=] {
		_cornerButtons.updateJumpDownVisibility();
		_cornerButtons.updateUnreadThingsVisibility();
	}, lifetime());

	_composeControls->viewportEvents(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());

	_composeControls->finishAnimating();

	if (const auto channel = _peer->asChannel()) {
		channel->updateFull();
		if (!channel->isBroadcast()) {
			rpl::combine(
				Data::CanSendAnythingValue(channel),
				channel->flagsValue()
			) | rpl::start_with_next([=] {
				refreshJoinGroupButton();
			}, lifetime());
		} else {
			refreshJoinGroupButton();
		}
	}
}

void ChatWidget::setupSwipeReplyAndBack() {
	const auto can = [=](not_null<HistoryItem*> still) {
		const auto canSendReply = _topic
			? Data::CanSendAnything(_topic)
			: Data::CanSendAnything(_peer);
		const auto allowInAnotherChat = still && still->allowsForward();
		if (allowInAnotherChat && (_joinGroup || !canSendReply)) {
			return true;
		} else if (!_joinGroup && canSendReply) {
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
			int cursorTop,
			Qt::LayoutDirection direction) {
		if (direction == Qt::RightToLeft) {
			return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
				controller()->showBackFromStack();
			});
		}
		auto result = Ui::Controls::SwipeHandlerFinishData();
		if (_inner->elementInSelectionMode(nullptr).inSelectionMode) {
			return result;
		}
		const auto view = _inner->lookupItemByY(cursorTop);
		if (!view
			|| !view->data()->isRegular()
			|| view->data()->isService()) {
			return result;
		}
		if (!can(view->data())) {
			return result;
		}

		result.msgBareId = view->data()->fullId().msg.bare;
		result.callback = [=, itemId = view->data()->fullId()] {
			const auto still = show->session().data().message(itemId);
			const auto view = _inner->viewByPosition(still->position());
			const auto selected = view
				? view->selectedQuote(_inner->getSelectedTextRange(still))
				: SelectedQuote();
			const auto replyToItemId = (selected.item
				? selected.item
				: still)->fullId();
			_inner->replyToMessageRequestNotify({
				.messageId = replyToItemId,
				.quote = selected.highlight.quote,
				.quoteOffset = selected.highlight.quoteOffset,
				.todoItemId = selected.highlight.todoItemId,
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
	});
}

void ChatWidget::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	_choosingAttach = false;
	if (const auto error = Data::AnyFileRestrictionError(_peer)) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return;
	} else if (showSlowmodeError()) {
		return;
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
	box->setCancelledCallback(_composeControls->restoreTextCallback(
		insertTextOnCancel));

	//ActivateWindow(controller());
	controller()->show(std::move(box));

	return true;
}

void ChatWidget::sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	Expects(list.filesToProcess.empty());

	if (showSendingFilesError(list, way.sendImagesAsPhotos())) {
		return;
	}
	auto groups = DivideByGroups(
		std::move(list),
		way,
		_peer->slowmodeApplied());
	auto bundle = PrepareFilesBundle(
		std::move(groups),
		way,
		std::move(caption),
		ctrlShiftEnter);
	sendingFilesConfirmed(std::move(bundle), options);
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
	const auto withPaymentApproved = [=](int approved) {
		auto copy = options;
		copy.starsApproved = approved;
		sendingFilesConfirmed(bundle, copy);
	};
	const auto checked = checkSendPayment(
		bundle->totalCount,
		options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	const auto compress = bundle->way.sendImagesAsPhotos();
	const auto type = compress ? SendMediaType::Photo : SendMediaType::File;
	auto action = prepareSendAction(options);
	action.clearDraft = false;
	if (bundle->sendComment) {
		auto message = Api::MessageToSend(action);
		message.textWithTags = base::take(bundle->caption);
		session().api().sendMessage(std::move(message));
	}
	for (auto &group : bundle->groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		session().api().sendFiles(
			std::move(group.list),
			type,
			base::take(bundle->caption),
			album,
			action);
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
	return showSendingFilesError(list, std::nullopt);
}

bool ChatWidget::showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const {
	const auto error = [&]() -> Data::SendError {
		const auto peer = _peer;
		const auto error = Data::FileRestrictionError(peer, list, compress);
		if (error) {
			return error;
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
	if (!error) {
		return false;
	} else if (error.text == u"(toolarge)"_q) {
		const auto fileSize = list.files.back().size;
		controller()->show(
			Box(FileSizeLimitBox, &session(), fileSize, nullptr));
		return true;
	}

	Data::ShowSendErrorToast(controller(), _peer, error);
	return true;
}

Api::SendAction ChatWidget::prepareSendAction(
		Api::SendOptions options) const {
	auto result = Api::SendAction(_history, options);
	result.replyTo = replyTo();
	result.options.sendAs = _composeControls->sendAsPeer();
	return result;
}

void ChatWidget::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
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
	const auto checked = checkSendPayment(
		1,
		data.options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	auto action = prepareSendAction(data.options);
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
	if (!options.scheduled && showSlowmodeError()) {
		return;
	}

	if (!options.scheduled) {
		_cornerButtons.clearReplyReturns();
	}

	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
	message.webPage = _composeControls->webPageDraft();

	auto request = SendingErrorRequest{
		.topicRootId = _topic ? _topic->rootId() : MsgId(0),
		.forward = &_composeControls->forwardItems(),
		.text = &message.textWithTags,
		.ignoreSlowmodeCountdown = (options.scheduled != 0),
	};
	request.messagesCount = ComputeSendingMessagesCount(_history, request);
	const auto error = GetErrorForSending(_peer, request);
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return;
	}
	if (!options.scheduled) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			send(copy);
		};
		const auto checked = checkSendPayment(
			request.messagesCount,
			options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}
	session().api().sendMessage(std::move(message));

	_composeControls->clear();
	if (_repliesRootId) {
		session().sendProgressManager().update(
			_history,
			_repliesRootId,
			Api::SendProgressType::Typing,
			-1);
	}

	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	finishSending();
}

void ChatWidget::edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId,
		bool spoilered) {
	if (*saveEditMsgRequestId) {
		return;
	}
	const auto webpage = _composeControls->webPageDraft();
	const auto sending = _composeControls->prepareTextForEditMsg();

	const auto hasMediaWithCaption = item
		&& item->media()
		&& item->media()->allowsEditCaption();
	if (sending.text.isEmpty() && !hasMediaWithCaption) {
		if (item) {
			controller()->show(Box<DeleteMessagesBox>(item, false));
		} else {
			doSetInnerFocus();
		}
		return;
	} else {
		const auto maxCaptionSize = !hasMediaWithCaption
			? MaxMessageSize
			: Data::PremiumLimits(&session()).captionLengthCurrent();
		const auto remove = _composeControls->fieldCharacterCount()
			- maxCaptionSize;
		if (remove > 0) {
			controller()->showToast(
				tr::lng_edit_limit_reached(tr::now, lt_count, remove));
			return;
		}
	}

	lifetime().add([=] {
		if (!*saveEditMsgRequestId) {
			return;
		}
		session().api().request(base::take(*saveEditMsgRequestId)).cancel();
	});

	const auto done = [=](mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
			_composeControls->cancelEditMessage();
		}
	};

	const auto fail = [=](const QString &error, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
		}

		if (ranges::contains(Api::kDefaultEditMessagesErrors, error)) {
			controller()->showToast(tr::lng_edit_error(tr::now));
		} else if (error == u"MESSAGE_NOT_MODIFIED"_q) {
			_composeControls->cancelEditMessage();
		} else if (error == u"MESSAGE_EMPTY"_q) {
			doSetInnerFocus();
		} else {
			controller()->showToast(tr::lng_edit_error(tr::now));
		}
		update();
		return true;
	};

	*saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		webpage,
		options,
		crl::guard(this, done),
		crl::guard(this, fail),
		spoilered);

	_composeControls->hidePanelsAnimated();
	doSetInnerFocus();
}

void ChatWidget::validateSubsectionTabs() {
	if (!_subsectionCheckLifetime && _history->peer->isMegagroup()) {
		_subsectionCheckLifetime = _history->peer->asChannel()->flagsValue(
		) | rpl::skip(
			1
		) | rpl::filter([=](Data::Flags<ChannelDataFlags>::Change change) {
			const auto mask = ChannelDataFlag::Forum
				| ChannelDataFlag::ForumTabs
				| ChannelDataFlag::MonoforumAdmin;
			return change.diff & mask;
		}) | rpl::start_with_next([=] {
			validateSubsectionTabs();
		});
	}
	const auto thread = _topic ? (Data::Thread*)_topic : _sublist;
	if (!thread || !HistoryView::SubsectionTabs::UsedFor(_history)) {
		if (_subsectionTabs) {
			_subsectionTabsLifetime.destroy();
			_subsectionTabs = nullptr;
			updateControlsGeometry();
			if (const auto forum = _history->asForum()) {
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
	_subsectionTabs->removeRequests() | rpl::start_with_next([=] {
		_subsectionTabsLifetime.destroy();
		_subsectionTabs = nullptr;
		updateControlsGeometry();
	}, _subsectionTabsLifetime);
	_subsectionTabs->layoutRequests() | rpl::start_with_next([=] {
		_inner->overrideChatMode((_subsectionTabs->leftSkip() > 0)
			? ElementChatMode::Narrow
			: std::optional<ElementChatMode>());
		updateControlsGeometry();
		orderWidgets();
	}, _subsectionTabsLifetime);
	_inner->overrideChatMode((_subsectionTabs->leftSkip() > 0)
		? ElementChatMode::Narrow
		: std::optional<ElementChatMode>());
	updateControlsGeometry();
	orderWidgets();
}

void ChatWidget::refreshJoinGroupButton() {
	if (!_repliesRootId) {
		return;
	}
	const auto set = [&](std::unique_ptr<Ui::FlatButton> button) {
		if (!button && !_joinGroup) {
			return;
		}
		const auto atMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		_joinGroup = std::move(button);
		if (!animatingShow()) {
			if (button) {
				button->show();
				_composeControls->hide();
			} else {
				_composeControls->show();
			}
		}
		updateControlsGeometry();
		if (atMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	};
	const auto channel = _peer->asChannel();
	const auto canSend = !channel->isForum()
		? Data::CanSendAnything(channel)
		: (_topic && Data::CanSendAnything(_topic));
	if (channel->amIn() || canSend) {
		_canSendTexts = true;
		set(nullptr);
	} else {
		_canSendTexts = false;
		if (!_joinGroup) {
			set(std::make_unique<Ui::FlatButton>(
				this,
				QString(),
				st::historyComposeButton));
			_joinGroup->setClickedCallback([=] {
				session().api().joinChannel(channel);
			});
		}
		_joinGroup->setText((channel->isBroadcast()
			? tr::lng_profile_join_channel(tr::now)
			: (channel->requestToJoin() && !channel->amCreator())
			? tr::lng_profile_apply_to_join_group(tr::now)
			: tr::lng_profile_join_group(tr::now)).toUpper());
	}
}

bool ChatWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::MessageToSend messageToSend,
		std::optional<MsgId> localId) {
	const auto error = Data::RestrictionError(
		_peer,
		ChatRestriction::SendStickers);
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return false;
	} else if (showSlowmodeError()
		|| ShowSendPremiumError(controller(), document)) {
		return false;
	}
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

	Api::SendExistingDocument(
		std::move(messageToSend),
		document,
		localId);

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
	const auto error = Data::RestrictionError(
		_peer,
		ChatRestriction::SendPhotos);
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	const auto withPaymentApproved = [=](int approved) {
		auto copy = options;
		copy.starsApproved = approved;
		sendExistingPhoto(photo, copy);
	};
	const auto checked = checkSendPayment(
		1,
		options,
		withPaymentApproved);
	if (!checked) {
		return false;
	}

	Api::SendExistingPhoto(
		Api::MessageToSend(prepareSendAction(options)),
		photo);

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void ChatWidget::sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		not_null<UserData*> bot) {
	if (const auto error = result->getErrorOnSend(_history)) {
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
	const auto withPaymentApproved = [=](int approved) {
		auto copy = options;
		copy.starsApproved = approved;
		sendInlineResult(result, bot, copy, localMessageId);
	};
	const auto checked = checkSendPayment(
		1,
		options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	auto action = prepareSendAction(options);
	action.generateLocal = true;
	session().api().sendInlineResult(
		bot,
		result.get(),
		action,
		localMessageId);

	_composeControls->clear();
	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	auto &bots = cRefRecentInlineBots();
	const auto index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		bot->session().local().writeRecentHashtagsAndBots();
	}
	finishSending();
}

SendMenu::Details ChatWidget::sendMenuDetails() const {
	using Type = SendMenu::Type;
	const auto type = (_topic && !_peer->starsPerMessageChecked())
		? Type::Scheduled
		: Type::SilentOnly;
	return SendMenu::Details{ .type = type };
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
			// Never answer to a message in a wrong monoforum peer id.
			custom.topicRootId = _repliesRootId;
			custom.monoforumPeerId = _monoforumPeerId;
			return custom;
		}
	}
	return FullReplyTo{
		.messageId = (_repliesRootId
			? FullMsgId(_peer->id, _repliesRootId)
			: FullMsgId()),
		.topicRootId = _repliesRootId,
		.monoforumPeerId = _monoforumPeerId,
	};
}

void ChatWidget::refreshTopBarActiveChat() {
	using namespace Dialogs;

	const auto state = EntryState{
		.key = (_sublist
			? Key{ _sublist }
			: _topic
			? Key{ _topic }
			: Key{ _history }),
		.section = _sublist
			? EntryState::Section::SavedSublist
			: EntryState::Section::Replies,
		.currentReplyTo = replyTo(),
		.currentSuggest = SuggestPostOptions(),
	};
	_topBar->setActiveChat(state, _sendAction.get());
	_composeControls->setCurrentDialogsEntryState(state);
	controller()->setDialogsEntryState(state);
}

void ChatWidget::refreshUnreadCountBadge(std::optional<int> count) {
	if (count.has_value()) {
		_cornerButtons.updateJumpDownVisibility(count);
	}
}

void ChatWidget::updatePinnedViewer() {
	if (_scroll->isHidden() || (!_topic && !_sublist) || !_pinnedTracker) {
		return;
	}
	const auto visibleBottom = _scroll->scrollTop() + _scroll->height();
	auto [view, offset] = _inner->findViewForPinnedTracking(visibleBottom);
	const auto lessThanId = !view
		? (ServerMaxMsgId - 1)
		: (view->data()->id + (offset > 0 ? 1 : 0));
	const auto lastClickedId = !_pinnedClickedId
		? (ServerMaxMsgId - 1)
		: _pinnedClickedId.msg;
	if (_pinnedClickedId
		&& lessThanId <= lastClickedId
		&& !_inner->animatedScrolling()) {
		_pinnedClickedId = FullMsgId();
	}
	if (_pinnedClickedId && !_minPinnedId) {
		_minPinnedId = Data::ResolveMinPinnedId(
			_peer,
			_repliesRootId,
			_monoforumPeerId);
	}
	if (_pinnedClickedId && _minPinnedId && _minPinnedId >= _pinnedClickedId) {
		// After click on the last pinned message we should the top one.
		_pinnedTracker->trackAround(ServerMaxMsgId - 1);
	} else {
		_pinnedTracker->trackAround(std::min(lessThanId, lastClickedId));
	}
}

void ChatWidget::checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop) {
	if (_scroll->isHidden() || (!_topic && !_sublist)) {
		return;
	}
	if (wasScrollTop < nowScrollTop && _pinnedClickedId) {
		// User scrolled down.
		_pinnedClickedId = FullMsgId();
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	}
}

void ChatWidget::setupOpenChatButton() {
	if (!_sublist || _sublist->sublistPeer()->isSavedHiddenAuthor()) {
		return;
	} else if (_sublist->parentChat()) {
		_canSendTexts = true;
		return;
	}
	_openChatButton = std::make_unique<Ui::FlatButton>(
		this,
		(_sublist->sublistPeer()->isBroadcast()
			? tr::lng_saved_open_channel(tr::now)
			: _sublist->sublistPeer()->isUser()
			? tr::lng_saved_open_chat(tr::now)
			: tr::lng_saved_open_group(tr::now)),
		st::historyComposeButton);

	_openChatButton->setClickedCallback([=] {
		controller()->showPeerHistory(
			_sublist->sublistPeer(),
			Window::SectionShow::Way::Forward);
	});
}

void ChatWidget::setupAboutHiddenAuthor() {
	if (!_sublist || !_sublist->sublistPeer()->isSavedHiddenAuthor()) {
		return;
	} else if (_sublist->parentChat()) {
		_canSendTexts = true;
		return;
	}
	_aboutHiddenAuthor = std::make_unique<Ui::RpWidget>(this);
	_aboutHiddenAuthor->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_aboutHiddenAuthor.get());
		auto rect = _aboutHiddenAuthor->rect();

		p.fillRect(rect, st::historyReplyBg);

		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect.marginsRemoved(
				QMargins(st::historySendPadding, 0, st::historySendPadding, 0)),
			tr::lng_saved_about_hidden(tr::now),
			style::al_center);
	}, _aboutHiddenAuthor->lifetime());
}

void ChatWidget::setupTranslateBar() {
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
		if (const auto delta = height - _translateBarHeight) {
			_translateBarHeight = height;
			setGeometryWithTopMoved(geometry(), delta);
		}
	}, _translateBar->lifetime());

	_translateBar->finishAnimating();
}

void ChatWidget::setupPinnedTracker() {
	Expects(_topic || _sublist);

	const auto thread = _topic ? (Data::Thread*)_topic : _sublist;
	_pinnedTracker = std::make_unique<HistoryView::PinnedTracker>(thread);
	_pinnedBar = nullptr;

	SharedMediaViewer(
		&session(),
		Storage::SharedMediaKey(
			_peer->id,
			_repliesRootId,
			_monoforumPeerId,
			Storage::SharedMediaType::Pinned,
			ServerMaxMsgId - 1),
		1,
		1
	) | rpl::filter([=](const SparseIdsSlice &result) {
		return result.fullCount().has_value();
	}) | rpl::start_with_next([=](const SparseIdsSlice &result) {
		thread->setHasPinnedMessages(*result.fullCount() != 0);
		if (result.skippedAfter() == 0) {
			auto &settings = _history->session().settings();
			const auto peerId = _peer->id;
			const auto hiddenId = settings.hiddenPinnedMessageId(
				peerId,
				_repliesRootId,
				_monoforumPeerId);
			const auto last = result.size() ? result[result.size() - 1] : 0;
			if (hiddenId && hiddenId != last) {
				settings.setHiddenPinnedMessageId(
					peerId,
					_repliesRootId,
					_monoforumPeerId,
					0);
				_history->session().saveSettingsDelayed();
			}
		}
		checkPinnedBarState();
	}, lifetime());
}

void ChatWidget::checkPinnedBarState() {
	Expects(_pinnedTracker != nullptr);
	Expects(_inner != nullptr);

	const auto hiddenId = _peer->canPinMessages()
		? MsgId(0)
		: _peer->session().settings().hiddenPinnedMessageId(
			_peer->id,
			_repliesRootId,
			_monoforumPeerId);
	const auto currentPinnedId = Data::ResolveTopPinnedId(
		_peer,
		_repliesRootId,
		_monoforumPeerId);
	const auto universalPinnedId = !currentPinnedId
		? MsgId(0)
		: currentPinnedId.msg;
	if (universalPinnedId == hiddenId) {
		if (_pinnedBar) {
			_pinnedBar->setContent(rpl::single(Ui::MessageBarContent()));
			_pinnedTracker->reset();
			_shownPinnedItem = nullptr;
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
	_pinnedBar = std::make_unique<Ui::PinnedBar>(_topBars.get(), [=] {
		return controller()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, controller()->gifPauseLevelChanged());
	auto pinnedRefreshed = Info::Profile::SharedMediaCountValue(
		_peer,
		_repliesRootId,
		_monoforumPeerId,
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
	auto customButtonItem = HistoryView::PinnedBarItemWithCustomButton(
		&session(),
		_pinnedTracker->shownMessageId());
	rpl::combine(
		rpl::duplicate(pinnedRefreshed),
		rpl::duplicate(customButtonItem)
	) | rpl::start_with_next([=](bool many, HistoryItem *item) {
		refreshPinnedBarButton(many, item);
	}, _pinnedBar->lifetime());

	_pinnedBar->setContent(rpl::combine(
		HistoryView::PinnedBarContent(
			&session(),
			_pinnedTracker->shownMessageId(),
			[bar = _pinnedBar.get()] { bar->customEmojiRepaint(); }),
		std::move(pinnedRefreshed),
		std::move(customButtonItem),
		_repliesRootVisible.value()
	) | rpl::map([=](Ui::MessageBarContent &&content, auto, auto, bool show) {
		const auto shown = !content.title.isEmpty() && !content.text.empty();
		_shownPinnedItem = shown
			? _history->owner().message(
				_pinnedTracker->currentMessageId().message)
			: nullptr;
		return (show || content.count > 1)
			? std::move(content)
			: Ui::MessageBarContent();
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
			showAtPosition(item->position());
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
		if (const auto delta = height - _pinnedBarHeight) {
			_pinnedBarHeight = height;
			setGeometryWithTopMoved(geometry(), delta);
		}
	}, _pinnedBar->lifetime());

	orderWidgets();
}

void ChatWidget::clearHidingPinnedBar() {
	if (!_hidingPinnedBar) {
		return;
	}
	if (const auto delta = -_pinnedBarHeight) {
		_pinnedBarHeight = 0;
		setGeometryWithTopMoved(geometry(), delta);
	}
	_hidingPinnedBar = nullptr;
}

void ChatWidget::refreshPinnedBarButton(bool many, HistoryItem *item) {
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
		const auto thread = _topic ? (Data::Thread*)_topic : _sublist;
		controller()->showSection(
			std::make_shared<PinnedMemento>(thread, id.message.msg));
	};
	const auto context = [copy = _inner](FullMsgId itemId) {
		if (const auto raw = copy.data()) {
			return raw->prepareClickHandlerContext(itemId);
		}
		return ClickHandlerContext();
	};
	auto customButton = CreatePinnedBarCustomButton(this, item, context);
	if (customButton) {
		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
		};
		const auto buttonRaw = customButton.data();
		const auto state = buttonRaw->lifetime().make_state<State>();
		_pinnedBar->contextMenuRequested(
		) | rpl::start_with_next([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(buttonRaw);
			state->menu->addAction(
				tr::lng_settings_events_pinned(tr::now),
				openSection);
			state->menu->popup(QCursor::pos());
		}, buttonRaw->lifetime());
		_pinnedBar->setRightButton(std::move(customButton));
		return;
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

void ChatWidget::hidePinnedMessage() {
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
			_repliesRootId,
			_monoforumPeerId,
			crl::guard(this, callback));
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
		return !_inner->loadedAtBottom();
	}
	return std::nullopt;
}

bool ChatWidget::cornerButtonsUnreadMayBeShown() {
	return _loaded
		&& !_composeControls->isLockPresent()
		&& !_composeControls->isTTLButtonShown();
}

bool ChatWidget::cornerButtonsHas(CornerButtonType type) {
	return _topic
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
	if (params.withTopBarShadow) _topBarShadow->hide();
	if (_joinGroup) {
		_composeControls->hide();
	} else {
		_composeControls->showForGrab();
	}
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_topBars->hide();
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
			} else {
				restoreState(logMemento);
				if (!logMemento->highlightId()) {
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
	if (!_repliesRootId) {
		return Window::SectionActionResult::Fallback;
	} else if (request.peer != _peer) {
		return Window::SectionActionResult::Ignore;
	}
	listSendBotCommand(request.command, request.context);
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

void ChatWidget::saveState(not_null<ChatMemento*> memento) {
	memento->setReplies(_replies);
	memento->setReplyReturns(_cornerButtons.replyReturns());
	_inner->saveState(memento->list());
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
}

void ChatWidget::setReplies(std::shared_ptr<Data::RepliesList> replies) {
	_replies = std::move(replies);
	_repliesLifetime.destroy();

	_replies->unreadCountValue(
	) | rpl::start_with_next([=](std::optional<int> count) {
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
	) | rpl::start_with_next([=](const QString &text) {
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
	) | rpl::start_with_next([=](std::optional<int> count) {
		refreshUnreadCountBadge(count);
	}, lifetime());

	using Flag = Data::SublistUpdate::Flag;
	session().changes().sublistUpdates(
		_sublist,
		Flag::UnreadView | Flag::UnreadReactions | Flag::CloudDraft
	) | rpl::start_with_next([=](const Data::SublistUpdate &update) {
		if (update.flags & Flag::UnreadView) {
			unreadCountUpdated();
		}
		if (update.flags & Flag::UnreadReactions) {
			_cornerButtons.updateUnreadThingsVisibility();
		}
		if (update.flags & Flag::CloudDraft) {
			_composeControls->applyCloudDraft();
		}
	}, lifetime());

	_sublist->destroyed(
	) | rpl::start_with_next([=] {
		closeCurrent();
	}, lifetime());

	unreadCountUpdated();
	subscribeToPinnedMessages();
}

void ChatWidget::unreadCountUpdated() {
	if (_sublist && _sublist->unreadMark()) {
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
	_inner->restoreState(memento->list());
	if (const auto highlight = memento->highlightId()) {
		auto params = Window::SectionShow(
			Window::SectionShow::Way::Forward,
			anim::type::instant);
		params.highlight = memento->highlight();
		showAtPosition(Data::MessagePosition{
			.fullId = FullMsgId(_peer->id, highlight),
			.date = TimeId(0),
		}, {}, params);
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

	const auto newScrollDelta = _scroll->isHidden()
		? std::nullopt
		: _scroll->scrollTop()
		? base::make_optional(topDelta() + _scrollTopDelta)
		: 0;
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);
	const auto tabsLeftSkip = _subsectionTabs
		? _subsectionTabs->leftSkip()
		: 0;
	const auto innerWidth = contentWidth - tabsLeftSkip;
	const auto subsectionTabsTop = _topBar->bottomNoMargins();
	_topBars->move(tabsLeftSkip, subsectionTabsTop
		+ (_subsectionTabs ? _subsectionTabs->topSkip() : 0));
	if (_repliesRootView) {
		_repliesRootView->resizeToWidth(innerWidth);
	}
	auto top = _repliesRootViewHeight;
	if (_pinnedBar) {
		_pinnedBar->move(0, top);
		_pinnedBar->resizeToWidth(innerWidth);
		top += _pinnedBarHeight;
	}
	if (_topicReopenBar) {
		_topicReopenBar->bar().move(0, top);
		top += _topicReopenBar->bar().height();
	}
	_translateBar->move(0, top);
	_translateBar->resizeToWidth(innerWidth);
	top += _translateBarHeight;

	auto bottom = height();
	if (_openChatButton) {
		_openChatButton->resizeToWidth(width());
		bottom -= _openChatButton->height();
		_openChatButton->move(0, bottom);
	} else if (_aboutHiddenAuthor) {
		_aboutHiddenAuthor->resize(width(), st::historyUnblock.height);
		bottom -= _aboutHiddenAuthor->height();
		_aboutHiddenAuthor->move(0, bottom);
	} else if (_joinGroup) {
		_joinGroup->resizeToWidth(width());
		bottom -= _joinGroup->height();
		_joinGroup->move(0, bottom);
	} else {
		bottom -= _composeControls->heightCurrent();
	}

	_topBars->resize(innerWidth, top + st::lineWidth);
	top += _topBars->y();

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
		const auto newScrollTop = (newScrollDelta && _scroll->scrollTop())
			? (_scroll->scrollTop() + *newScrollDelta)
			: std::optional<int>();
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}
	_composeControls->move(0, bottom);
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());

	if (_subsectionTabs) {
		const auto scrollBottom = _scroll->y() + scrollHeight;
		const auto areaHeight = scrollBottom - subsectionTabsTop;
		_subsectionTabs->setBoundingRect(
			{ 0, subsectionTabsTop, width(), areaHeight });
	}

	_cornerButtons.updatePositions();
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
}

bool ChatWidget::emptyShown() const {
	return _topic
		&& (_inner->isEmpty()
			|| (_topic->lastKnownServerMessageId() == _repliesRootId));
}

void ChatWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void ChatWidget::updateInnerVisibleArea() {
	if (!_inner->animatedScrolling()) {
		checkReplyReturns();
	}
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updatePinnedVisibility();
	updatePinnedViewer();
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
	if (_lastScrollTop != scrollTop) {
		if (!_synteticScrollEvent) {
			checkLastPinnedClickedIdReset(_lastScrollTop, scrollTop);
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
	if (animatingShow()) {
	} else if (_sublist) {
		_repliesRootVisible = shown;
	} else if (!_repliesRootId) {
		return;
	} else if (!_topic) {
		if (!_repliesRootViewInitScheduled) {
			const auto height = shown ? st::historyReplyHeight : 0;
			if (const auto delta = height - _repliesRootViewHeight) {
				_repliesRootViewHeight = height;
				if (_scroll->scrollTop() == _scroll->scrollTopMax()) {
					setGeometryWithTopMoved(geometry(), delta);
				} else {
					updateControlsGeometry();
				}
			}
		}
		_repliesRootVisible = shown;
		if (!_repliesRootViewInited) {
			_repliesRootView->finishAnimating();
			if (!_repliesRootViewInitScheduled) {
				_repliesRootViewInitScheduled = true;
				InvokeQueued(this, [=] {
					_repliesRootViewInited = true;
				});
			}
		}
	} else {
		_repliesRootVisible = shown;
	}
}

void ChatWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void ChatWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	if (_joinGroup || _openChatButton || _aboutHiddenAuthor) {
		if (Ui::InFocusChain(this)) {
			_inner->setFocus();
		}
		_composeControls->hide();
	} else {
		_composeControls->showFinished();
	}
	_inner->showFinished();
	_topBars->show();
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
	}
}

bool ChatWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ChatWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context ChatWidget::listContext() {
	return !_sublist
		? Context::Replies
		: _sublist->parentChat()
		? Context::Monoforum
		: Context::SavedSublist;
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
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	} else if (_composeControls->handleCancelRequest()) {
		refreshTopBarActiveChat();
		return;
	}
	controller()->showBackFromStack();
}

void ChatWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

void ChatWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	_composeControls->tryProcessKeyInput(e);
}

void ChatWidget::markLoaded() {
	if (!_loaded) {
		_loaded = true;
		crl::on_main(this, [=] {
			updatePinnedVisibility();
		});
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
	Unexpected("ChatWidget::listSource in unknown mode");
}

rpl::producer<Data::MessagesSlice> ChatWidget::repliesSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _replies->source(
		aroundId,
		limitBefore,
		limitAfter
	) | rpl::before_next([=] { // after_next makes a copy of value.
		markLoaded();
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
		_topBar->setCustomTitle(result.fullCount
			? tr::lng_forum_messages(
				tr::now,
				lt_count_decimal,
				*result.fullCount)
			: tr::lng_contacts_loading(tr::now));
		markLoaded();
	});
}

bool ChatWidget::listAllowsMultiSelect() {
	return true;
}

bool ChatWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return item->isRegular() && !item->isService();
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
	if ((state.count > 0) && _composeSearch) {
		_composeSearch->hideAnimated();
	}
	if (items.empty()) {
		doSetInnerFocus();
	}
}

void ChatWidget::listMarkReadTill(not_null<HistoryItem*> item) {
	if (_replies) {
		_replies->readTill(item);
	} else if (_sublist) {
		_sublist->readTill(item);
	}
}

void ChatWidget::listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	session().api().markContentsRead(items);
}

MessagesBarData ChatWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	if ((!_sublist && !_replies) || elements.empty()) {
		return {};
	}
	const auto till = _replies
		? _replies->computeInboxReadTillFull()
		: _sublist->computeInboxReadTillFull();
	const auto hidden = (till < 2);
	for (auto i = 0, count = int(elements.size()); i != count; ++i) {
		const auto item = elements[i]->data();
		if (item->isRegular() && item->id > till) {
			if (item->out() || (_replies && !item->replyToId())) {
				if (_replies) {
					_replies->readTill(item);
				} else {
					_sublist->readTill(item);
				}
			} else {
				return {
					.bar = {
						.element = elements[i],
						.hidden = hidden,
						.focus = true,
					},
					.text = tr::lng_unread_bar_some(),
				};
			}
		}
	}
	return {};
}

void ChatWidget::listContentRefreshed() {
}

void ChatWidget::listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) {
	if (!_topic) {
		link = nullptr;
		return;
	}
	const auto date = view->dateTime().date();
	if (!link) {
		link = std::make_shared<Window::DateClickHandler>(_topic, date);
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
		sendBotCommandWithOptions(command, context, {});
	}
}

void ChatWidget::sendBotCommandWithOptions(
		const QString &command,
		const FullMsgId &context,
		Api::SendOptions options) {
	const auto withPaymentApproved = [=](int approved) {
		auto copy = options;
		copy.starsApproved = approved;
		sendBotCommandWithOptions(command, context, copy);
	};
	const auto checked = checkSendPayment(
		1,
		options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	const auto text = Bot::WrapCommandInChat(
		_peer,
		command,
		context);
	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = { text };
	session().api().sendMessage(std::move(message));
	finishSending();
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
	if (_canSendTexts) {
		_composeControls->setText({ '@' + bot->username() + ' ' });
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

void ChatWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(
		photo,
		{ context, _repliesRootId, _monoforumPeerId });
}

void ChatWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(
		document,
		showInMediaView,
		{ context, _repliesRootId, _monoforumPeerId });
}

void ChatWidget::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
	if (!emptyShown()) {
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
	return true;
}

History *ChatWidget::listTranslateHistory() {
	return _history;
}

void ChatWidget::listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) {
	if (_shownPinnedItem) {
		tracker->add(_shownPinnedItem);
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
	Window::FillSenderUserpicMenu(
		controller(),
		_history->owner().peer(userpicPeerId),
		_composeControls->fieldForMention(),
		searchInEntry,
		Ui::Menu::CreateAddActionCallback(menu.get()));
	return menu->empty() ? nullptr : std::move(menu);
}

void ChatWidget::setupEmptyPainter() {
	Expects(_topic != nullptr);

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
		return _topic
			? Data::CanSendAnyOf(_topic, Data::FilesSendRestrictions())
			: Data::CanSendAnyOf(_peer, Data::FilesSendRestrictions());
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
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 1) && request->handle([=] {
			searchRequested();
			return true;
		});
	}, lifetime());
}

void ChatWidget::searchRequested() {
	if (_sublist) {
		controller()->searchInChat(_sublist);
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
		) | rpl::start_with_next([=](Activation activation) {
			showAtPosition(activation.item->position());
		}, _composeSearch->lifetime());

		_composeSearch->destroyRequests(
		) | rpl::take(1) | rpl::start_with_next([=] {
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
	if (!sublist || sublist != _sublist) {
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

	updateControlsGeometry();
	setInnerFocus();

	_composeSearch->activations(
	) | rpl::start_with_next([=](ComposeSearch::Activation activation) {
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
	) | rpl::start_with_next([=] {
		_composeSearch = nullptr;

		updateControlsGeometry();
		setInnerFocus();
	}, _composeSearch->lifetime());

	return true;
}

} // namespace HistoryView
