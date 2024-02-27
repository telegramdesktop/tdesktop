/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_replies_section.h"

#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/controls/history_view_draft_options.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/history_view_translate_tracker.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h" // GetErrorTextForSending.
#include "ui/chat/pinned_bar.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/message_sending_animation_controller.h"
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

RepliesMemento::RepliesMemento(
	not_null<History*> history,
	MsgId rootId,
	MsgId highlightId,
	const TextWithEntities &highlightPart,
	int highlightPartOffsetHint)
: _history(history)
, _rootId(rootId)
, _highlightPart(highlightPart)
, _highlightPartOffsetHint(highlightPartOffsetHint)
, _highlightId(highlightId) {
	if (highlightId) {
		_list.setAroundPosition({
			.fullId = FullMsgId(_history->peer->id, highlightId),
			.date = TimeId(0),
		});
	}
}

RepliesMemento::RepliesMemento(
	not_null<HistoryItem*> commentsItem,
	MsgId commentId)
: RepliesMemento(commentsItem->history(), commentsItem->id, commentId) {
}

void RepliesMemento::setFromTopic(not_null<Data::ForumTopic*> topic) {
	_replies = topic->replies();
	if (!_list.aroundPosition()) {
		_list = *topic->listMemento();
	}
}


Data::ForumTopic *RepliesMemento::topicForRemoveRequests() const {
	return _history->peer->forumTopicFor(_rootId);
}

void RepliesMemento::setReadInformation(
		MsgId inboxReadTillId,
		int unreadCount,
		MsgId outboxReadTillId) {
	if (!_replies) {
		if (const auto forum = _history->asForum()) {
			if (const auto topic = forum->topicFor(_rootId)) {
				_replies = topic->replies();
			}
		}
		if (!_replies) {
			_replies = std::make_shared<Data::RepliesList>(
				_history,
				_rootId);
		}
	}
	_replies->setInboxReadTill(inboxReadTillId, unreadCount);
	_replies->setOutboxReadTill(outboxReadTillId);
}

object_ptr<Window::SectionWidget> RepliesMemento::createWidget(
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
	}
	auto result = object_ptr<RepliesWidget>(
		parent,
		controller,
		_history,
		_rootId);
	result->setInternalState(geometry, this);
	return result;
}

void RepliesMemento::setupTopicViewer() {
	_history->owner().itemIdChanged(
	) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
		if (_rootId == change.oldId) {
			_rootId = change.newId.msg;
			_replies = nullptr;
		}
	}, _lifetime);
}

RepliesWidget::RepliesWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	MsgId rootId)
: Window::SectionWidget(parent, controller, history->peer)
, _history(history)
, _rootId(rootId)
, _root(lookupRoot())
, _topic(lookupTopic())
, _areComments(computeAreComments())
, _sendAction(history->owner().sendActionManager().repliesPainter(
	history,
	rootId))
, _topBar(this, controller)
, _topBarShadow(this)
, _composeControls(std::make_unique<ComposeControls>(
	this,
	controller,
	[=](not_null<DocumentData*> emoji) { listShowPremiumToast(emoji); },
	ComposeControls::Mode::Normal,
	SendMenu::Type::SilentOnly))
, _translateBar(std::make_unique<TranslateBar>(this, controller, history))
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
		history->peer
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	setupRoot();
	setupRootView();
	setupShortcuts();
	setupTranslateBar();

	_history->peer->updateFull();

	refreshTopBarActiveChat();

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	if (_rootView) {
		_rootView->move(0, _topBar->height());
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
		searchInTopic();
	}, _topBar->lifetime());

	controller->adaptive().value(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
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
				_composeControls->editMessage(fullId);
			}
		}
	}, _inner->lifetime());

	_inner->replyToMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		const auto canSendReply = _topic
			? Data::CanSendAnything(_topic)
			: Data::CanSendAnything(_history->peer);
		if (_joinGroup || !canSendReply) {
			Controls::ShowReplyToChatBox(controller->uiShow(), { fullId });
		} else {
			replyToMessage(fullId);
			_composeControls->focus();
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
		if (!data.cancel) {
			session().sendProgressManager().update(
				_history,
				_rootId,
				data.type,
				data.progress);
		} else {
			session().sendProgressManager().cancel(
				_history,
				_rootId,
				data.type);
		}
	}, lifetime());

	_history->session().changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		if (update.item == _root) {
			_root = nullptr;
			updatePinnedVisibility();
			if (!_topic) {
				controller->showBackFromStack();
			}
		}
	}, lifetime());

	if (!_topic) {
		_history->session().changes().historyUpdates(
			_history,
			Data::HistoryUpdate::Flag::OutboxRead
		) | rpl::start_with_next([=] {
			_inner->update();
		}, lifetime());
	}

	setupTopicViewer();
	setupComposeControls();
	orderWidgets();

	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
}

RepliesWidget::~RepliesWidget() {
	base::take(_sendAction);
	session().api().saveCurrentDraftToCloud();
	controller()->sendingAnimation().clear();
	if (_topic) {
		if (_topic->creating()) {
			_emptyPainter = nullptr;
			_topic->discard();
			_topic = nullptr;
		} else {
			_inner->saveState(_topic->listMemento());
		}
	}
	_history->owner().sendActionManager().repliesPainterRemoved(
		_history,
		_rootId);
}

void RepliesWidget::orderWidgets() {
	_translateBar->raise();
	if (_topicReopenBar) {
		_topicReopenBar->bar().raise();
	}
	if (_rootView) {
		_rootView->raise();
	}
	if (_pinnedBar) {
		_pinnedBar->raise();
	}
	if (_topBar) {
		_topBar->raise();
	}
	_topBarShadow->raise();
	_composeControls->raisePanels();
}

void RepliesWidget::setupRoot() {
	if (!_root) {
		const auto done = crl::guard(this, [=] {
			_root = lookupRoot();
			if (_root) {
				_areComments = computeAreComments();
				_inner->update();
			}
			updatePinnedVisibility();
		});
		_history->session().api().requestMessageData(
			_history->peer,
			_rootId,
			done);
	}
}

void RepliesWidget::setupRootView() {
	if (_topic) {
		return;
	}
	_rootView = std::make_unique<Ui::PinnedBar>(this, [=] {
		return controller()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, controller()->gifPauseLevelChanged());
	_rootView->setContent(rpl::combine(
		RootViewContent(
			_history,
			_rootId,
			[bar = _rootView.get()] { bar->customEmojiRepaint(); }),
		_rootVisible.value()
	) | rpl::map([=](Ui::MessageBarContent &&content, bool show) {
		const auto shown = !content.title.isEmpty() && !content.text.empty();
		_shownPinnedItem = shown
			? _history->owner().message(_history->peer->id, _rootId)
			: nullptr;
		return show ? std::move(content) : Ui::MessageBarContent();
	}));

	controller()->adaptive().oneColumnValue(
	) | rpl::start_with_next([=](bool one) {
		_rootView->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _rootView->lifetime());

	_rootView->barClicks(
	) | rpl::start_with_next([=] {
		showAtStart();
	}, lifetime());

	_rootViewHeight = 0;
	_rootView->heightValue(
	) | rpl::start_with_next([=](int height) {
		if (const auto delta = height - _rootViewHeight) {
			_rootViewHeight = height;
			setGeometryWithTopMoved(geometry(), delta);
		}
	}, _rootView->lifetime());
}

void RepliesWidget::setupTopicViewer() {
	const auto owner = &_history->owner();
	owner->itemIdChanged(
	) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
		if (_rootId == change.oldId) {
			_rootId = change.newId.msg;
			_composeControls->updateTopicRootId(_rootId);
			_sendAction = owner->sendActionManager().repliesPainter(
				_history,
				_rootId);
			_root = lookupRoot();
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

void RepliesWidget::subscribeToTopic() {
	Expects(_topic != nullptr);

	_topicReopenBar = std::make_unique<TopicReopenBar>(this, _topic);
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
		controller()->showBackFromStack(Window::SectionShow(
			anim::type::normal,
			anim::activation::background));
	}, _topicLifetime);

	if (!_topic->creating()) {
		subscribeToPinnedMessages();

		if (!_topic->creatorId()) {
			_topic->forum()->requestTopic(_topic->rootId());
		}
	}

	_cornerButtons.updateUnreadThingsVisibility();
}

void RepliesWidget::subscribeToPinnedMessages() {
	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	session().changes().entryUpdates(
		EntryUpdateFlag::HasPinnedMessages
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		if (_pinnedTracker
			&& (update.flags & EntryUpdateFlag::HasPinnedMessages)
			&& (_topic == update.entry.get())) {
			checkPinnedBarState();
		}
	}, lifetime());

	setupPinnedTracker();
}

void RepliesWidget::setTopic(Data::ForumTopic *topic) {
	if (_topic == topic) {
		return;
	}
	_topicLifetime.destroy();
	_topic = topic;
	refreshReplies();
	refreshTopBarActiveChat();
	if (_topic) {
		if (_rootView) {
			_shownPinnedItem = nullptr;
			_rootView = nullptr;
			_rootViewHeight = 0;
		}
		subscribeToTopic();
	}
	if (_topic && emptyShown()) {
		setupEmptyPainter();
	} else {
		_emptyPainter = nullptr;
	}
}

HistoryItem *RepliesWidget::lookupRoot() const {
	return _history->owner().message(_history->peer, _rootId);
}

Data::ForumTopic *RepliesWidget::lookupTopic() {
	if (const auto forum = _history->asForum()) {
		if (const auto result = forum->topicFor(_rootId)) {
			return result;
		} else {
			forum->requestTopic(_rootId, crl::guard(this, [=] {
				if (const auto forum = _history->asForum()) {
					setTopic(forum->topicFor(_rootId));
				}
			}));
		}
	}
	return nullptr;
}

bool RepliesWidget::computeAreComments() const {
	return _root && _root->isDiscussionPost();
}

void RepliesWidget::setupComposeControls() {
	auto topicWriteRestrictions = rpl::single(
	) | rpl::then(session().changes().topicUpdates(
		Data::TopicUpdate::Flag::Closed
	) | rpl::filter([=](const Data::TopicUpdate &update) {
		return (update.topic->history() == _history)
			&& (update.topic->rootId() == _rootId);
	}) | rpl::to_empty) | rpl::map([=] {
		const auto topic = _topic
			? _topic
			: _history->peer->forumTopicFor(_rootId);
		return (!topic || topic->canToggleClosed() || !topic->closed())
			? std::optional<QString>()
			: tr::lng_forum_topic_closed(tr::now);
	});
	auto writeRestriction = rpl::combine(
		session().changes().peerFlagsValue(
			_history->peer,
			Data::PeerUpdate::Flag::Rights),
		Data::CanSendAnythingValue(_history->peer),
		std::move(topicWriteRestrictions)
	) | rpl::map([=](auto, auto, std::optional<QString> topicRestriction) {
		const auto allWithoutPolls = Data::AllSendRestrictions()
			& ~ChatRestriction::SendPolls;
		const auto canSendAnything = _topic
			? Data::CanSendAnyOf(_topic, allWithoutPolls)
			: Data::CanSendAnyOf(_history->peer, allWithoutPolls);
		const auto restriction = Data::RestrictionError(
			_history->peer,
			ChatRestriction::SendOther);
		auto text = !canSendAnything
			? (restriction
				? restriction
				: topicRestriction
				? std::move(topicRestriction)
				: tr::lng_group_not_accessible(tr::now))
			: topicRestriction
			? std::move(topicRestriction)
			: std::optional<QString>();
		return text ? Controls::WriteRestriction{
			.text = std::move(*text),
			.type = Controls::WriteRestrictionType::Rights,
		} : Controls::WriteRestriction();
	});

	_composeControls->setHistory({
		.history = _history.get(),
		.topicRootId = _topic ? _topic->rootId() : MsgId(0),
		.showSlowmodeError = [=] { return showSlowmodeError(); },
		.sendActionFactory = [=] { return prepareSendAction({}); },
		.slowmodeSecondsLeft = SlowmodeSecondsLeft(_history->peer),
		.sendDisabledBySlowmode = SendDisabledBySlowmode(_history->peer),
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

	_composeControls->sendVoiceRequests(
	) | rpl::start_with_next([=](ComposeControls::VoiceToSend &&data) {
		sendVoice(std::move(data));
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
			edit(item, data.options, saveEditMsgRequestId);
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
		const auto localId = data.messageSendingFrom.localId;
		sendExistingDocument(data.document, data.options, localId);
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
			JumpToMessageClickHandler(
				item,
				{},
				to.quote,
				to.quoteOffset
			)->onClick({});
		}
	}, lifetime());

	_composeControls->scrollKeyEvents(
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

	if (const auto channel = _history->peer->asChannel()) {
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

void RepliesWidget::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	_choosingAttach = false;
	if (const auto error = Data::AnyFileRestrictionError(_history->peer)) {
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

bool RepliesWidget::confirmSendingFiles(
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

bool RepliesWidget::confirmSendingFiles(
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
		_history->peer,
		Api::SendType::Normal,
		SendMenu::Type::SilentOnly); // #TODO replies schedule

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

void RepliesWidget::sendingFilesConfirmed(
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
		_history->peer->slowmodeApplied());
	const auto type = way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
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
	if (_composeControls->replyingToMessage().messageId
			== action.replyTo.messageId) {
		_composeControls->cancelReplyMessage();
		refreshTopBarActiveChat();
	}
	finishSending();
}

bool RepliesWidget::confirmSendingFiles(
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

bool RepliesWidget::showSlowmodeError() {
	const auto text = [&] {
		if (const auto left = _history->peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWordsSlowmode(left));
		} else if (_history->peer->slowmodeApplied()) {
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

void RepliesWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	if (item->history() == _history && item->inThread(_rootId)) {
		_cornerButtons.pushReplyReturn(item);
	}
}

void RepliesWidget::checkReplyReturns() {
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

void RepliesWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	// #TODO replies schedule
	session().api().sendFile(fileContent, type, prepareSendAction({}));
}

bool RepliesWidget::showSendingFilesError(
		const Ui::PreparedList &list) const {
	return showSendingFilesError(list, std::nullopt);
}

bool RepliesWidget::showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const {
	const auto text = [&] {
		const auto peer = _history->peer;
		const auto error = Data::FileRestrictionError(peer, list, compress);
		if (error) {
			return *error;
		} else if (const auto left = _history->peer->slowmodeSecondsLeft()) {
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

Api::SendAction RepliesWidget::prepareSendAction(
	Api::SendOptions options) const {
	auto result = Api::SendAction(_history, options);
	result.replyTo = replyTo();
	result.options.sendAs = _composeControls->sendAsPeer();
	return result;
}

void RepliesWidget::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		return;
	}
	send({});
	// #TODO replies schedule
	//const auto callback = [=](Api::SendOptions options) { send(options); };
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

void RepliesWidget::sendVoice(ComposeControls::VoiceToSend &&data) {
	auto action = prepareSendAction(data.options);
	session().api().sendVoiceMessage(
		data.bytes,
		data.waveform,
		data.duration,
		std::move(action));

	_composeControls->cancelReplyMessage();
	_composeControls->clearListenState();
	finishSending();
}

void RepliesWidget::send(Api::SendOptions options) {
	if (!options.scheduled && showSlowmodeError()) {
		return;
	}

	if (!options.scheduled) {
		_cornerButtons.clearReplyReturns();
	}

	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
	message.webPage = _composeControls->webPageDraft();

	const auto error = GetErrorTextForSending(
		_history->peer,
		{
			.topicRootId = _topic ? _topic->rootId() : MsgId(0),
			.forward = &_composeControls->forwardItems(),
			.text = &message.textWithTags,
			.ignoreSlowmodeCountdown = (options.scheduled != 0),
		});
	if (!error.isEmpty()) {
		controller()->showToast(error);
		return;
	}

	session().api().sendMessage(std::move(message));

	_composeControls->clear();
	session().sendProgressManager().update(
		_history,
		_rootId,
		Api::SendProgressType::Typing,
		-1);

	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	finishSending();
}

void RepliesWidget::edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId) {
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
		crl::guard(this, fail));

	_composeControls->hidePanelsAnimated();
	doSetInnerFocus();
}

void RepliesWidget::refreshJoinGroupButton() {
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
	const auto channel = _history->peer->asChannel();
	const auto canSend = !channel->isForum()
		? Data::CanSendAnything(channel)
		: (_topic && Data::CanSendAnything(_topic));
	if (channel->amIn() || canSend) {
		set(nullptr);
	} else {
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

void RepliesWidget::sendExistingDocument(
		not_null<DocumentData*> document) {
	sendExistingDocument(document, {}, std::nullopt);
	// #TODO replies schedule
	//const auto callback = [=](Api::SendOptions options) {
	//	sendExistingDocument(document, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

bool RepliesWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		std::optional<MsgId> localId) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::SendStickers);
	if (error) {
		controller()->showToast(*error);
		return false;
	} else if (showSlowmodeError()
		|| ShowSendPremiumError(controller(), document)) {
		return false;
	}

	Api::SendExistingDocument(
		Api::MessageToSend(prepareSendAction(options)),
		document,
		localId);

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void RepliesWidget::sendExistingPhoto(not_null<PhotoData*> photo) {
	sendExistingPhoto(photo, {});
	// #TODO replies schedule
	//const auto callback = [=](Api::SendOptions options) {
	//	sendExistingPhoto(photo, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

bool RepliesWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::SendPhotos);
	if (error) {
		controller()->showToast(*error);
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	Api::SendExistingPhoto(
		Api::MessageToSend(prepareSendAction(options)),
		photo);

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void RepliesWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	const auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		controller()->showToast(errorText);
		return;
	}
	sendInlineResult(result, bot, {}, std::nullopt);
	//const auto callback = [=](Api::SendOptions options) {
	//	sendInlineResult(result, bot, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

void RepliesWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId) {
	auto action = prepareSendAction(options);
	action.generateLocal = true;
	session().api().sendInlineResult(bot, result, action, localMessageId);

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

SendMenu::Type RepliesWidget::sendMenuType() const {
	// #TODO replies schedule
	return _history->peer->isSelf()
		? SendMenu::Type::Reminder
		: HistoryView::CanScheduleUntilOnline(_history->peer)
		? SendMenu::Type::ScheduledToUser
		: SendMenu::Type::Scheduled;
}

FullReplyTo RepliesWidget::replyTo() const {
	if (auto custom = _composeControls->replyingToMessage()) {
		custom.topicRootId = _rootId;
		return custom;
	}
	return FullReplyTo{
		.messageId = FullMsgId(_history->peer->id, _rootId),
		.topicRootId = _rootId,
	};
}

void RepliesWidget::refreshTopBarActiveChat() {
	using namespace Dialogs;
	const auto state = EntryState{
		.key = (_topic ? Key{ _topic } : Key{ _history }),
		.section = EntryState::Section::Replies,
		.currentReplyTo = replyTo(),
	};
	_topBar->setActiveChat(state, _sendAction.get());
	_composeControls->setCurrentDialogsEntryState(state);
	controller()->setCurrentDialogsEntryState(state);
}

void RepliesWidget::refreshUnreadCountBadge(std::optional<int> count) {
	if (count.has_value()) {
		_cornerButtons.updateJumpDownVisibility(count);
	}
}

void RepliesWidget::updatePinnedViewer() {
	if (_scroll->isHidden() || !_topic || !_pinnedTracker) {
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
		_minPinnedId = Data::ResolveMinPinnedId(_history->peer, _rootId);
	}
	if (_pinnedClickedId && _minPinnedId && _minPinnedId >= _pinnedClickedId) {
		// After click on the last pinned message we should the top one.
		_pinnedTracker->trackAround(ServerMaxMsgId - 1);
	} else {
		_pinnedTracker->trackAround(std::min(lessThanId, lastClickedId));
	}
}

void RepliesWidget::checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop) {
	if (_scroll->isHidden() || !_topic) {
		return;
	}
	if (wasScrollTop < nowScrollTop && _pinnedClickedId) {
		// User scrolled down.
		_pinnedClickedId = FullMsgId();
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	}
}

void RepliesWidget::setupTranslateBar() {
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

void RepliesWidget::setupPinnedTracker() {
	Expects(_topic != nullptr);

	_pinnedTracker = std::make_unique<HistoryView::PinnedTracker>(_topic);
	_pinnedBar = nullptr;

	SharedMediaViewer(
		&_topic->session(),
		Storage::SharedMediaKey(
			_topic->channel()->id,
			_rootId,
			Storage::SharedMediaType::Pinned,
			ServerMaxMsgId - 1),
		1,
		1
	) | rpl::filter([=](const SparseIdsSlice &result) {
		return result.fullCount().has_value();
	}) | rpl::start_with_next([=](const SparseIdsSlice &result) {
		_topic->setHasPinnedMessages(*result.fullCount() != 0);
		if (result.skippedAfter() == 0) {
			auto &settings = _history->session().settings();
			const auto peerId = _history->peer->id;
			const auto hiddenId = settings.hiddenPinnedMessageId(
				peerId,
				_rootId);
			const auto last = result.size() ? result[result.size() - 1] : 0;
			if (hiddenId && hiddenId != last) {
				settings.setHiddenPinnedMessageId(peerId, _rootId, 0);
				_history->session().saveSettingsDelayed();
			}
		}
		checkPinnedBarState();
	}, _topicLifetime);
}

void RepliesWidget::checkPinnedBarState() {
	Expects(_pinnedTracker != nullptr);
	Expects(_inner != nullptr);

	const auto peer = _history->peer;
	const auto hiddenId = peer->canPinMessages()
		? MsgId(0)
		: peer->session().settings().hiddenPinnedMessageId(
			peer->id,
			_rootId);
	const auto currentPinnedId = Data::ResolveTopPinnedId(peer, _rootId);
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
	_pinnedBar = std::make_unique<Ui::PinnedBar>(this, [=] {
		return controller()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, controller()->gifPauseLevelChanged());
	auto pinnedRefreshed = Info::Profile::SharedMediaCountValue(
		_history->peer,
		_rootId,
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
		std::move(markupRefreshed),
		_rootVisible.value()
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

	if (animatingShow()) {
		_pinnedBar->hide();
	}
}

void RepliesWidget::clearHidingPinnedBar() {
	if (!_hidingPinnedBar) {
		return;
	}
	if (const auto delta = -_pinnedBarHeight) {
		_pinnedBarHeight = 0;
		setGeometryWithTopMoved(geometry(), delta);
	}
	_hidingPinnedBar = nullptr;
}

void RepliesWidget::refreshPinnedBarButton(bool many, HistoryItem *item) {
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
			std::make_shared<PinnedMemento>(_topic, id.message.msg));
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
						_inner->prepareClickHandlerContext(item->fullId()),
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

void RepliesWidget::hidePinnedMessage() {
	Expects(_pinnedBar != nullptr);

	const auto id = _pinnedTracker->currentMessageId();
	if (!id.message) {
		return;
	}
	if (_history->peer->canPinMessages()) {
		Window::ToggleMessagePinned(controller(), id.message, false);
	} else {
		const auto callback = [=] {
			if (_pinnedTracker) {
				checkPinnedBarState();
			}
		};
		Window::HidePinnedBar(
			controller(),
			_history->peer,
			_rootId,
			crl::guard(this, callback));
	}
}

void RepliesWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	showAtPosition(position);
}

Data::Thread *RepliesWidget::cornerButtonsThread() {
	return _topic ? static_cast<Data::Thread*>(_topic) : _history;
}

FullMsgId RepliesWidget::cornerButtonsCurrentId() {
	return _lastShownAt;
}

bool RepliesWidget::cornerButtonsIgnoreVisibility() {
	return animatingShow();
}

std::optional<bool> RepliesWidget::cornerButtonsDownShown() {
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

bool RepliesWidget::cornerButtonsUnreadMayBeShown() {
	return _loaded
		&& !_composeControls->isLockPresent()
		&& !_composeControls->isTTLButtonShown();
}

bool RepliesWidget::cornerButtonsHas(CornerButtonType type) {
	return _topic || (type == CornerButtonType::Down);
}

void RepliesWidget::showAtStart() {
	showAtPosition(Data::MinMessagePosition);
}

void RepliesWidget::showAtEnd() {
	showAtPosition(Data::MaxMessagePosition);
}

void RepliesWidget::finishSending() {
	_composeControls->hidePanelsAnimated();
	//if (_previewData && _previewData->pendingTill) previewCancel();
	doSetInnerFocus();
	showAtEnd();
	refreshTopBarActiveChat();
}

void RepliesWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId) {
	showAtPosition(position, originItemId, {});
}

void RepliesWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId,
		const Window::SectionShow &params) {
	_lastShownAt = position.fullId;
	controller()->setActiveChatEntry(activeChat());
	const auto ignore = (position.fullId.msg == _rootId);
	_inner->showAtPosition(
		position,
		params,
		_cornerButtons.doneJumpFrom(position.fullId, originItemId, ignore));
}

void RepliesWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> RepliesWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor RepliesWidget::activeChat() const {
	const auto messageId = _lastShownAt
		? _lastShownAt
		: FullMsgId(_history->peer->id, ShowAtUnreadMsgId);
	if (_topic) {
		return { _topic, messageId };
	}
	return { _history, messageId };
}

bool RepliesWidget::preventsClose(Fn<void()> &&continueCallback) const {
	if (_composeControls->preventsClose(base::duplicate(continueCallback))) {
		return true;
	} else if (!_newTopicDiscarded
		&& _topic
		&& _topic->creating()) {
		const auto weak = Ui::MakeWeak(this);
		auto sure = [=](Fn<void()> &&close) {
			if (const auto strong = weak.data()) {
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

QPixmap RepliesWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
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
	if (_rootView) {
		_rootView->hide();
	}
	if (_pinnedBar) {
		_pinnedBar->hide();
	}
	_translateBar->hide();
	return result;
}

void RepliesWidget::checkActivation() {
	_inner->checkActivation();
}

void RepliesWidget::doSetInnerFocus() {
	if (!_inner->getSelectedText().rich.text.isEmpty()
		|| !_inner->getSelectedItems().empty()
		|| !_composeControls->focus()) {
		_inner->setFocus();
	}
}

bool RepliesWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<RepliesMemento*>(memento.get())) {
		if (logMemento->getHistory() == history()
			&& logMemento->getRootId() == _rootId) {
			restoreState(logMemento);
			if (!logMemento->highlightId()) {
				showAtPosition(Data::UnreadMessagePosition);
			}
			if (params.reapplyLocalDraft) {
				_composeControls->applyDraft(
					ComposeControls::FieldHistoryAction::NewEntry);
			}
			return true;
		}
	}
	return false;
}

void RepliesWidget::setInternalState(
		const QRect &geometry,
		not_null<RepliesMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

bool RepliesWidget::pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) {
	return _composeControls->pushTabbedSelectorToThirdSection(
		thread,
		params);
}

bool RepliesWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

std::shared_ptr<Window::SectionMemento> RepliesWidget::createMemento() {
	auto result = std::make_shared<RepliesMemento>(history(), _rootId);
	saveState(result.get());
	return result;
}

bool RepliesWidget::showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) {
	if (peerId != _history->peer->id) {
		return false;
	}
	const auto id = FullMsgId(_history->peer->id, messageId);
	const auto message = _history->owner().message(id);
	if (!message || (!message->inThread(_rootId) && id.msg != _rootId)) {
		return false;
	}
	const auto originMessage = [&]() -> HistoryItem* {
		using OriginMessage = Window::SectionShow::OriginMessage;
		if (const auto origin = std::get_if<OriginMessage>(&params.origin)) {
			if (const auto returnTo = session().data().message(origin->id)) {
				if (returnTo->history() != _history) {
					return nullptr;
				} else if (returnTo->inThread(_rootId)) {
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

Window::SectionActionResult RepliesWidget::sendBotCommand(
		Bot::SendCommandRequest request) {
	if (request.peer != _history->peer) {
		return Window::SectionActionResult::Ignore;
	}
	listSendBotCommand(request.command, request.context);
	return Window::SectionActionResult::Handle;
}

bool RepliesWidget::confirmSendingFiles(const QStringList &files) {
	return confirmSendingFiles(files, QString());
}

bool RepliesWidget::confirmSendingFiles(not_null<const QMimeData*> data) {
	return confirmSendingFiles(data, std::nullopt);
}

bool RepliesWidget::confirmSendingFiles(
		const QStringList &files,
		const QString &insertTextOnCancel) {
	const auto premium = controller()->session().user()->isPremium();
	return confirmSendingFiles(
		Storage::PrepareMediaList(files, st::sendMediaPreviewSize, premium),
		insertTextOnCancel);
}

void RepliesWidget::replyToMessage(FullReplyTo id) {
	_composeControls->replyToMessage(std::move(id));
	refreshTopBarActiveChat();
}

void RepliesWidget::saveState(not_null<RepliesMemento*> memento) {
	memento->setReplies(_replies);
	memento->setReplyReturns(_cornerButtons.replyReturns());
	_inner->saveState(memento->list());
}

void RepliesWidget::refreshReplies() {
	auto old = base::take(_replies);
	setReplies(_topic
		? _topic->replies()
		: std::make_shared<Data::RepliesList>(_history, _rootId));
	if (old) {
		_inner->refreshViewer();
	}
}

void RepliesWidget::setReplies(std::shared_ptr<Data::RepliesList> replies) {
	_replies = std::move(replies);
	_repliesLifetime.destroy();

	_replies->unreadCountValue(
	) | rpl::start_with_next([=](std::optional<int> count) {
		refreshUnreadCountBadge(count);
	}, lifetime());

	refreshUnreadCountBadge(_replies->unreadCountKnown()
		? _replies->unreadCountCurrent()
		: std::optional<int>());

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

void RepliesWidget::restoreState(not_null<RepliesMemento*> memento) {
	if (auto replies = memento->getReplies()) {
		setReplies(std::move(replies));
	} else if (!_replies) {
		refreshReplies();
	}
	_cornerButtons.setReplyReturns(memento->replyReturns());
	_inner->restoreState(memento->list());
	if (const auto highlight = memento->highlightId()) {
		auto params = Window::SectionShow(
			Window::SectionShow::Way::Forward,
			anim::type::instant);
		params.highlightPart = memento->highlightPart();
		params.highlightPartOffsetHint = memento->highlightPartOffsetHint();
		showAtPosition(Data::MessagePosition{
			.fullId = FullMsgId(_history->peer->id, highlight),
			.date = TimeId(0),
		}, {}, params);
	}
}

void RepliesWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	recountChatWidth();
	updateControlsGeometry();
}

void RepliesWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Window::Adaptive::ChatLayout::Normal
		: Window::Adaptive::ChatLayout::Wide;
	controller()->adaptive().setChatLayout(layout);
}

void RepliesWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: _scroll->scrollTop()
		? base::make_optional(_scroll->scrollTop()
			+ topDelta()
			+ _scrollTopDelta)
		: 0;
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);
	if (_rootView) {
		_rootView->resizeToWidth(contentWidth);
	}
	auto top = _topBar->height() + _rootViewHeight;
	if (_pinnedBar) {
		_pinnedBar->move(0, top);
		_pinnedBar->resizeToWidth(contentWidth);
		top += _pinnedBarHeight;
	}
	if (_topicReopenBar) {
		_topicReopenBar->bar().move(0, top);
		top += _topicReopenBar->bar().height();
	}
	_translateBar->move(0, top);
	_translateBar->resizeToWidth(contentWidth);
	top += _translateBarHeight;

	const auto bottom = height();
	const auto controlsHeight = _joinGroup
		? _joinGroup->height()
		: _composeControls->heightCurrent();
	const auto scrollHeight = bottom - top - controlsHeight;
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(0, top);
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}
	if (_joinGroup) {
		_joinGroup->setGeometry(
			0,
			bottom - _joinGroup->height(),
			contentWidth,
			_joinGroup->height());
	}
	_composeControls->move(0, bottom - controlsHeight);
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());

	_cornerButtons.updatePositions();
}

void RepliesWidget::paintEvent(QPaintEvent *e) {
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

bool RepliesWidget::emptyShown() const {
	return _topic
		&& (_inner->isEmpty()
			|| (_topic->lastKnownServerMessageId() == _rootId));
}

void RepliesWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void RepliesWidget::updateInnerVisibleArea() {
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

void RepliesWidget::updatePinnedVisibility() {
	if (!_loaded) {
		return;
	} else if (!_topic && (!_root || _root->isEmpty())) {
		setPinnedVisibility(!_root);
		return;
	}
	const auto rootItem = [&] {
		if (const auto group = _history->owner().groups().find(_root)) {
			return group->items.front().get();
		}
		return _root;
	};
	const auto view = _inner->viewByPosition(_topic
		? Data::MinMessagePosition
		: rootItem()->position());
	const auto visible = !view
		|| (view->y() + view->height() <= _scroll->scrollTop());
	setPinnedVisibility(visible || (_topic && !view->data()->isPinned()));
}

void RepliesWidget::setPinnedVisibility(bool shown) {
	if (animatingShow()) {
		return;
	} else if (!_topic) {
		if (!_rootViewInitScheduled) {
			const auto height = shown ? st::historyReplyHeight : 0;
			if (const auto delta = height - _rootViewHeight) {
				_rootViewHeight = height;
				if (_scroll->scrollTop() == _scroll->scrollTopMax()) {
					setGeometryWithTopMoved(geometry(), delta);
				} else {
					updateControlsGeometry();
				}
			}
		}
		_rootVisible = shown;
		if (!_rootViewInited) {
			_rootView->finishAnimating();
			if (!_rootViewInitScheduled) {
				_rootViewInitScheduled = true;
				InvokeQueued(this, [=] {
					_rootViewInited = true;
				});
			}
		}
	} else {
		_rootVisible = shown;
	}
}

void RepliesWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void RepliesWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	if (_joinGroup) {
		if (Ui::InFocusChain(this)) {
			_inner->setFocus();
		}
		_composeControls->hide();
	} else {
		_composeControls->showFinished();
	}
	_inner->showFinished();
	if (_rootView) {
		_rootView->show();
	}
	if (_pinnedBar) {
		_pinnedBar->show();
	}
	_translateBar->show();
	if (_topicReopenBar) {
		_topicReopenBar->bar().show();
	}

	// We should setup the drag area only after
	// the section animation is finished,
	// because after that the method showChildren() is called.
	setupDragArea();
	updatePinnedVisibility();
}

bool RepliesWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect RepliesWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context RepliesWidget::listContext() {
	return Context::Replies;
}

bool RepliesWidget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	const auto scrolled = (_scroll->scrollTop() != top);
	_synteticScrollEvent = syntetic;
	if (scrolled) {
		_scroll->scrollToY(top);
	} else if (syntetic) {
		updateInnerVisibleArea();
	}
	_synteticScrollEvent = false;
	return syntetic;
}

void RepliesWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	} else if (_composeControls->handleCancelRequest()) {
		refreshTopBarActiveChat();
		return;
	}
	controller()->showBackFromStack();
}

void RepliesWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

void RepliesWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	_composeControls->tryProcessKeyInput(e);
}

rpl::producer<Data::MessagesSlice> RepliesWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _replies->source(
		aroundId,
		limitBefore,
		limitAfter
	) | rpl::before_next([=] { // after_next makes a copy of value.
		if (!_loaded) {
			_loaded = true;
			crl::on_main(this, [=] {
				updatePinnedVisibility();
			});
		}
	});
}

bool RepliesWidget::listAllowsMultiSelect() {
	return true;
}

bool RepliesWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return item->isRegular() && !item->isService();
}

bool RepliesWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void RepliesWidget::listSelectionChanged(SelectedItems &&items) {
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
	if (items.empty()) {
		doSetInnerFocus();
	}
}

void RepliesWidget::listMarkReadTill(not_null<HistoryItem*> item) {
	_replies->readTill(item);
}

void RepliesWidget::listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	session().api().markContentsRead(items);
}

MessagesBarData RepliesWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	if (elements.empty()) {
		return {};
	}
	const auto till = _replies->computeInboxReadTillFull();
	const auto hidden = (till < 2);
	for (auto i = 0, count = int(elements.size()); i != count; ++i) {
		const auto item = elements[i]->data();
		if (item->isRegular() && item->id > till) {
			if (item->out() || !item->replyToId()) {
				_replies->readTill(item);
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

void RepliesWidget::listContentRefreshed() {
}

void RepliesWidget::listUpdateDateLink(
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

bool RepliesWidget::listElementHideReply(not_null<const Element*> view) {
	if (const auto reply = view->data()->Get<HistoryMessageReply>()) {
		const auto replyToPeerId = reply->externalPeerId()
			? reply->externalPeerId()
			: _history->peer->id;
		if (reply->fields().manualQuote) {
			return false;
		} else if (replyToPeerId == _history->peer->id) {
			return (reply->messageId() == _rootId);
		} else if (_root) {
			const auto forwarded = _root->Get<HistoryMessageForwarded>();
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

bool RepliesWidget::listElementShownUnread(not_null<const Element*> view) {
	return _replies->isServerSideUnread(view->data());
}

bool RepliesWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return view->data()->isRegular();
}

void RepliesWidget::listSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
	const auto text = Bot::WrapCommandInChat(
		_history->peer,
		command,
		context);
	auto message = Api::MessageToSend(prepareSendAction({}));
	message.textWithTags = { text };
	session().api().sendMessage(std::move(message));
	finishSending();
}

void RepliesWidget::listSearch(
		const QString &query,
		const FullMsgId &context) {
	controller()->searchMessages(query, _history);
}

void RepliesWidget::listHandleViaClick(not_null<UserData*> bot) {
	_composeControls->setText({ '@' + bot->username() + ' ' });
}

not_null<Ui::ChatTheme*> RepliesWidget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType RepliesWidget::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType RepliesWidget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyMediaRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType RepliesWidget::listSelectRestrictionType() {
	return SelectRestrictionTypeFor(_history->peer);
}

auto RepliesWidget::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return Data::PeerAllowedReactionsValue(_history->peer);
}

void RepliesWidget::listShowPremiumToast(not_null<DocumentData*> document) {
	if (!_stickerToast) {
		_stickerToast = std::make_unique<HistoryView::StickerToast>(
			controller(),
			this,
			[=] { _stickerToast = nullptr; });
	}
	_stickerToast->showFor(document);
}

void RepliesWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { context, _rootId });
}

void RepliesWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(
		document,
		showInMediaView,
		{ context, _rootId });
}

void RepliesWidget::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
	if (!emptyShown()) {
		return;
	} else if (!_emptyPainter) {
		setupEmptyPainter();
	}
	_emptyPainter->paint(p, context.st, width(), _scroll->height());
}

QString RepliesWidget::listElementAuthorRank(not_null<const Element*> view) {
	return (_topic && view->data()->from()->id == _topic->creatorId())
		? tr::lng_topic_author_badge(tr::now)
		: QString();
}

History *RepliesWidget::listTranslateHistory() {
	return _history;
}

void RepliesWidget::listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) {
	if (_shownPinnedItem) {
		tracker->add(_shownPinnedItem);
	}
}

void RepliesWidget::setupEmptyPainter() {
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

void RepliesWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void RepliesWidget::confirmForwardSelected() {
	ConfirmForwardSelectedItems(_inner);
}

void RepliesWidget::clearSelected() {
	_inner->cancelSelection();
}

void RepliesWidget::setupDragArea() {
	const auto filter = [=](const auto &d) {
		if (!_history || _composeControls->isRecording()) {
			return false;
		}
		const auto peer = _history->peer;
		return _topic
			? Data::CanSendAnyOf(_topic, Data::FilesSendRestrictions())
			: Data::CanSendAnyOf(peer, Data::FilesSendRestrictions());
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

void RepliesWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return _topic
			&& Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !controller()->isLayerShown()
			&& (Core::App().activeWindow() == &controller()->window());
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 1) && request->handle([=] {
			searchInTopic();
			return true;
		});
	}, lifetime());
}

void RepliesWidget::searchInTopic() {
	if (_topic) {
		controller()->searchInChat(_topic);
	}
}

} // namespace HistoryView
