/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_corner_buttons.h"

#include "ui/widgets/scroll_area.h"
#include "ui/chat/chat_style.h"
#include "ui/special_buttons.h"
#include "base/qt/qt_key_modifiers.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "apiwrap.h"
#include "api/api_unread_things.h"
#include "data/data_document.h"
#include "data/data_messages.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "lang/lang_keys.h"
#include "ui/toast/toast.h"
#include "styles/style_chat.h"

namespace HistoryView {

CornerButtons::CornerButtons(
	not_null<Ui::ScrollArea*> parent,
	not_null<const Ui::ChatStyle*> st,
	not_null<CornerButtonsDelegate*> delegate)
: _scroll(parent)
, _delegate(delegate)
, _down(
	parent,
	st->value(parent->lifetime(), st::historyToDown))
, _mentions(
	parent,
	st->value(parent->lifetime(), st::historyUnreadMentions))
, _reactions(
		parent,
		st->value(parent->lifetime(), st::historyUnreadReactions)) {
	_down.widget->addClickHandler([=] { downClick(); });
	_mentions.widget->addClickHandler([=] { mentionsClick(); });
	_reactions.widget->addClickHandler([=] { reactionsClick(); });

	const auto filterScroll = [&](CornerButton &button) {
		button.widget->installEventFilter(this);
	};
	filterScroll(_down);
	filterScroll(_mentions);
	filterScroll(_reactions);

	SendMenu::SetupUnreadMentionsMenu(_mentions.widget.data(), [=] {
		return _delegate->cornerButtonsThread();
	});
	SendMenu::SetupUnreadReactionsMenu(_reactions.widget.data(), [=] {
		return _delegate->cornerButtonsThread();
	});
}

bool CornerButtons::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Wheel
		&& (o == _down.widget
			|| o == _mentions.widget
			|| o == _reactions.widget)) {
		return _scroll->viewportEvent(e);
	}
	return QObject::eventFilter(o, e);
}

void CornerButtons::downClick() {
	if (base::IsCtrlPressed() || !_replyReturn) {
		_delegate->cornerButtonsShowAtPosition(Data::UnreadMessagePosition);
	} else {
		_delegate->cornerButtonsShowAtPosition(_replyReturn->position());
	}
}

void CornerButtons::mentionsClick() {
	const auto history = lookupHistory();
	if (!history) {
		return;
	}
	const auto thread = _delegate->cornerButtonsThread();
	const auto msgId = thread->unreadMentions().minLoaded();
	const auto already = (_delegate->cornerButtonsCurrentId().msg == msgId);

	// Mark mention voice/video message as read.
	// See https://github.com/telegramdesktop/tdesktop/issues/5623
	if (msgId && already) {
		if (const auto item = thread->owner().message(history->peer, msgId)) {
			if (const auto media = item->media()) {
				if (const auto document = media->document()) {
					if (!media->webpage()
						&& (document->isVoiceMessage()
							|| document->isVideoMessage())) {
						document->owner().markMediaRead(document);
					}
				}
			}
		}
	}
	showAt(msgId);
}

void CornerButtons::reactionsClick() {
	const auto history = lookupHistory();
	if (!history) {
		return;
	}
	const auto thread = _delegate->cornerButtonsThread();
	showAt(thread->unreadReactions().minLoaded());
}

void CornerButtons::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = nullptr;
}

QVector<FullMsgId> CornerButtons::replyReturns() const {
	return _replyReturns;
}

void CornerButtons::setReplyReturns(QVector<FullMsgId> replyReturns) {
	_replyReturns = std::move(replyReturns);
	computeCurrentReplyReturn();
	if (!_replyReturn) {
		calculateNextReplyReturn();
	}
}

void CornerButtons::computeCurrentReplyReturn() {
	const auto thread = _delegate->cornerButtonsThread();
	_replyReturn = (!thread || _replyReturns.empty())
		? nullptr
		: thread->owner().message(_replyReturns.back());
}

void CornerButtons::skipReplyReturn(FullMsgId id) {
	while (_replyReturn) {
		if (_replyReturn->fullId() == id) {
			calculateNextReplyReturn();
		} else {
			break;
		}
	}
}

void CornerButtons::calculateNextReplyReturn() {
	_replyReturn = nullptr;
	while (!_replyReturns.empty() && !_replyReturn) {
		_replyReturns.pop_back();
		computeCurrentReplyReturn();
	}
	if (!_replyReturn) {
		updateJumpDownVisibility();
		updateUnreadThingsVisibility();
	}
}

void CornerButtons::pushReplyReturn(not_null<HistoryItem*> item) {
	_replyReturns.push_back(item->fullId());
	_replyReturn = item;

	if (!_replyReturnStarted) {
		_replyReturnStarted = true;
		item->history()->owner().itemRemoved(
		) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
			while (item == _replyReturn) {
				calculateNextReplyReturn();
			}
		}, _down.widget->lifetime());
	}
}

CornerButton &CornerButtons::buttonByType(Type type) {
	switch (type) {
	case Type::Down: return _down;
	case Type::Mentions: return _mentions;
	case Type::Reactions: return _reactions;
	}
	Unexpected("Type in CornerButtons::buttonByType.");
}

History *CornerButtons::lookupHistory() const {
	const auto thread = _delegate->cornerButtonsThread();
	return thread ? thread->owningHistory().get() : nullptr;
}

void CornerButtons::showAt(MsgId id) {
	if (const auto history = lookupHistory()) {
		if (const auto item = history->owner().message(history->peer, id)) {
			_delegate->cornerButtonsShowAtPosition(item->position());
		}
	}
}

void CornerButtons::updateVisibility(
		CornerButtonType type,
		bool shown) {
	auto &button = buttonByType(type);
	if (button.shown != shown) {
		button.shown = shown;
		button.animation.start(
			[=] { updatePositions(); },
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			st::historyToDownDuration);
	}
}

void CornerButtons::updateUnreadThingsVisibility() {
	if (_delegate->cornerButtonsIgnoreVisibility()) {
		return;
	}
	const auto thread = _delegate->cornerButtonsThread();
	if (!thread) {
		updateVisibility(Type::Mentions, false);
		updateVisibility(Type::Reactions, false);
		return;
	}
	auto &unreadThings = thread->session().api().unreadThings();
	unreadThings.preloadEnough(thread);

	const auto updateWithCount = [&](Type type, int count) {
		updateVisibility(
			type,
			(count > 0) && _delegate->cornerButtonsUnreadMayBeShown());
	};
	if (_delegate->cornerButtonsHas(Type::Mentions)
		&& unreadThings.trackMentions(thread)) {
		if (const auto count = thread->unreadMentions().count(0)) {
			_mentions.widget->setUnreadCount(count);
		}
		updateWithCount(
			Type::Mentions,
			thread->unreadMentions().loadedCount());
	} else {
		updateVisibility(Type::Mentions, false);
	}

	if (_delegate->cornerButtonsHas(Type::Reactions)
		&& unreadThings.trackReactions(thread)) {
		if (const auto count = thread->unreadReactions().count(0)) {
			_reactions.widget->setUnreadCount(count);
		}
		updateWithCount(
			Type::Reactions,
			thread->unreadReactions().loadedCount());
	} else {
		updateVisibility(Type::Reactions, false);
	}
}

void CornerButtons::updateJumpDownVisibility(std::optional<int> counter) {
	if (const auto shown = _delegate->cornerButtonsDownShown()) {
		updateVisibility(Type::Down, *shown);
	}
	if (counter) {
		_down.widget->setUnreadCount(*counter);
	}
}

void CornerButtons::updatePositions() {
	const auto checkVisibility = [](CornerButton &button) {
		const auto shouldBeHidden = !button.shown
			&& !button.animation.animating();
		if (shouldBeHidden != button.widget->isHidden()) {
			button.widget->setVisible(!shouldBeHidden);
		}
	};
	const auto shown = [](CornerButton &button) {
		return button.animation.value(button.shown ? 1. : 0.);
	};

	// All corner buttons is a child widgets of _scroll, not me.

	const auto historyDownShown = shown(_down);
	const auto unreadMentionsShown = shown(_mentions);
	const auto unreadReactionsShown = shown(_reactions);
	const auto skip = st::historyUnreadThingsSkip;
	{
		const auto top = anim::interpolate(
			0,
			_down.widget->height() + st::historyToDownPosition.y(),
			historyDownShown);
		_down.widget->moveToRight(
			st::historyToDownPosition.x(),
			_scroll->height() - top);
	}
	{
		const auto right = anim::interpolate(
			-_mentions.widget->width(),
			st::historyToDownPosition.x(),
			unreadMentionsShown);
		const auto shift = anim::interpolate(
			0,
			_down.widget->height() + skip,
			historyDownShown);
		const auto top = _scroll->height()
			- _mentions.widget->height()
			- st::historyToDownPosition.y()
			- shift;
		_mentions.widget->moveToRight(right, top);
	}
	{
		const auto right = anim::interpolate(
			-_reactions.widget->width(),
			st::historyToDownPosition.x(),
			unreadReactionsShown);
		const auto shift = anim::interpolate(
			0,
			_down.widget->height() + skip,
			historyDownShown
		) + anim::interpolate(
			0,
			_mentions.widget->height() + skip,
			unreadMentionsShown);
		const auto top = _scroll->height()
			- _reactions.widget->height()
			- st::historyToDownPosition.y()
			- shift;
		_reactions.widget->moveToRight(right, top);
	}

	checkVisibility(_down);
	checkVisibility(_mentions);
	checkVisibility(_reactions);
}

void CornerButtons::finishAnimations() {
	_down.animation.stop();
	_mentions.animation.stop();
	_reactions.animation.stop();
	updatePositions();
}

Fn<void(bool found)> CornerButtons::doneJumpFrom(
		FullMsgId targetId,
		FullMsgId originId,
		bool ignoreMessageNotFound) {
	return [=](bool found) {
		skipReplyReturn(targetId);
		if (originId) {
			if (const auto thread = _delegate->cornerButtonsThread()) {
				if (const auto item = thread->owner().message(originId)) {
					pushReplyReturn(item);
				}
			}
		}
		if (!found && !ignoreMessageNotFound) {
			Ui::Toast::Show(
				_scroll.get(),
				tr::lng_message_not_found(tr::now));
		}
	};
}

} // namespace HistoryView
