/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_top_message_hint.h"

#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace {

[[nodiscard]] MTPMessage PreparePreviewMessage(const MTPDmessage &data) {
	using Flag = MTPDmessage::Flag;
	const auto removeFlags = Flag::f_from_scheduled
		| Flag::f_grouped_id
		| Flag::f_quick_reply_shortcut_id
		| Flag::f_report_delivery_until_date
		| Flag::f_schedule_repeat_period;
	return MTP_message(
		MTP_flags(data.vflags().v & ~removeFlags),
		data.vid(),
		data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
		MTP_int(data.vfrom_boosts_applied().value_or_empty()),
		data.vfrom_rank() ? *data.vfrom_rank() : MTPstring(),
		data.vpeer_id(),
		data.vsaved_peer_id() ? *data.vsaved_peer_id() : MTPPeer(),
		data.vfwd_from() ? *data.vfwd_from() : MTPMessageFwdHeader(),
		MTP_long(data.vvia_bot_id().value_or_empty()),
		MTP_long(data.vvia_business_bot_id().value_or_empty()),
		(data.vguestchat_via_from()
			? *data.vguestchat_via_from()
			: MTPPeer()),
		data.vreply_to() ? *data.vreply_to() : MTPMessageReplyHeader(),
		data.vdate(),
		data.vmessage(),
		data.vmedia() ? *data.vmedia() : MTPMessageMedia(),
		data.vreply_markup() ? *data.vreply_markup() : MTPReplyMarkup(),
		(data.ventities()
			? *data.ventities()
			: MTPVector<MTPMessageEntity>()),
		MTP_int(data.vviews().value_or_empty()),
		MTP_int(data.vforwards().value_or_empty()),
		data.vreplies() ? *data.vreplies() : MTPMessageReplies(),
		MTP_int(data.vedit_date().value_or_empty()),
		data.vpost_author() ? *data.vpost_author() : MTPstring(),
		MTPlong(), // grouped_id
		data.vreactions() ? *data.vreactions() : MTPMessageReactions(),
		(data.vrestriction_reason()
			? *data.vrestriction_reason()
			: MTPVector<MTPRestrictionReason>()),
		MTP_int(data.vttl_period().value_or_empty()),
		MTPint(), // quick_reply_shortcut_id
		MTP_long(data.veffect().value_or_empty()),
		data.vfactcheck() ? *data.vfactcheck() : MTPFactCheck(),
		MTPint(), // report_delivery_until_date
		MTP_long(data.vpaid_message_stars().value_or_empty()),
		(data.vsuggested_post()
			? *data.vsuggested_post()
			: MTPSuggestedPost()),
		MTPint(), // schedule_repeat_period
		(data.vsummary_from_language()
			? *data.vsummary_from_language()
			: MTPstring()),
		data.vrich_message() ? *data.vrich_message() : MTPRichMessage());
}

} // namespace

HistoryTopMessageHint::HistoryTopMessageHint(not_null<History*> history)
: _history(history) {
}

bool HistoryTopMessageHint::apply(MsgId id, TimeId date) {
	const auto server = _history->lastServerMessage();
	const auto last = _history->lastMessage();
	if (!IsServerMsgId(id)
		|| (date <= 0)
		|| !eligible()
		|| (id <= _id)
		|| unavailable(id)
		|| (server && server->id >= id)
		|| (last && !last->isRegular() && last->date() > date)
		|| (id <= _history->maxMsgId())
		|| hasRegularMessageAfter(id)) {
		return false;
	}
	forgetPreview();
	_id = id;
	_date = date;
	const auto fetch = !item();
	_history->updateChatListSortPosition();
	return fetch;
}

bool HistoryTopMessageHint::active(MsgId id) const {
	return (id == _id) && date();
}

void HistoryTopMessageHint::loaded(MsgId id, const MTPMessage *message) {
	if (!_date || (id != _id)) {
		return;
	} else if (message && active(id) && !hasRegularMessageAfter(id)) {
		if (!heldTarget()) {
			createPreview(*message);
		}
		if (item()) {
			_history->updateChatListEntry();
			return;
		}
	}
	clear();
}

void HistoryTopMessageHint::clear() {
	const auto shown = base::take(_date);
	forgetPreview();
	if (shown && _history->inChatList()) {
		_history->updateChatListSortPosition();
	}
}

void HistoryTopMessageHint::lastMessageSet(
		HistoryItem *item,
		bool replaced) {
	if (!item || (item->isRegular() ? (item->id >= _id) : replaced)) {
		clear();
	}
}

void HistoryTopMessageHint::itemRemoved(not_null<HistoryItem*> item) {
	if (item->id == _id) {
		clear();
	}
}

void HistoryTopMessageHint::unknownMessageDeleted(MsgId id) {
	if (id == _id) {
		clear();
	}
}

void HistoryTopMessageHint::clearedUpTill(MsgId availableMinId) {
	if (_id <= availableMinId) {
		clear();
	}
}

TimeId HistoryTopMessageHint::date() const {
	return (_date && eligible() && !unavailable(_id)) ? _date : 0;
}

HistoryItem *HistoryTopMessageHint::item() const {
	if (!date()) {
		return nullptr;
	} else if (const auto held = heldTarget()) {
		return held;
	} else if (!_previewId) {
		return nullptr;
	}
	const auto preview = _history->owner().message(
		_history->peer,
		_previewId);
	return (preview && !preview->isEmpty()) ? preview : nullptr;
}

bool HistoryTopMessageHint::unread(not_null<const HistoryItem*> item) const {
	if (!_previewId || (item->id != _previewId)) {
		return item->unread(_history);
	} else if (!item->out()) {
		return (_id > _history->inboxReadTillId());
	}
	const auto channel = _history->peer->asChannel();
	return channel
		&& channel->isMegagroup()
		&& (_id > _history->outboxReadTillId());
}

bool HistoryTopMessageHint::eligible() const {
	const auto channel = _history->peer->asChannel();
	return channel && channel->amIn() && _history->inChatList();
}

bool HistoryTopMessageHint::unavailable(MsgId id) const {
	const auto channel = _history->peer->asChannel();
	return (channel && (id <= channel->availableMinId()))
		|| _history->isUnknownMessageDeleted(id);
}

bool HistoryTopMessageHint::hasRegularMessageAfter(MsgId id) const {
	for (const auto &item : _history->_items) {
		if (item->isRegular() && IsServerMsgId(item->id) && item->id > id) {
			return true;
		}
	}
	return false;
}

HistoryItem *HistoryTopMessageHint::heldTarget() const {
	const auto item = _history->owner().message(_history->peer, _id);
	return (item && item->isRegular() && !item->isEmpty()) ? item : nullptr;
}

void HistoryTopMessageHint::createPreview(const MTPMessage &message) {
	const auto make = [&](const auto &data) {
		return _history->makeMessage(
			_history->nextNonHistoryEntryId(),
			data,
			MessageFlag::FakeHistoryItem).get();
	};
	const auto item = message.match([&](const MTPDmessage &data) {
		return make(PreparePreviewMessage(data).c_message());
	}, [&](const MTPDmessageService &data) {
		return make(data);
	}, [](const MTPDmessageEmpty &) -> HistoryItem* {
		return nullptr;
	});
	if (!item) {
		return;
	}
	_previewId = item->id;
	_history->owner().itemRemoved(
		item->fullId()
	) | rpl::on_next([=](not_null<const HistoryItem*> removed) {
		previewRemoved(removed);
	}, _previewLifetime);
}

void HistoryTopMessageHint::previewRemoved(
		not_null<const HistoryItem*> item) {
	_previewId = 0;
	_previewLifetime.destroy();
	_history->lastItemDialogsView().itemInvalidated(item);
	clear();
}

void HistoryTopMessageHint::forgetPreview() {
	const auto id = base::take(_previewId);
	_previewLifetime.destroy();
	const auto item = id
		? _history->owner().message(_history->peer, id)
		: nullptr;
	if (!item) {
		return;
	}
	_history->lastItemDialogsView().itemInvalidated(item);
	const auto session = &_history->session();

	// May run inside a loop that is about to destroy this item too.
	crl::on_main(session, [session, fullId = item->fullId()] {
		if (const auto item = session->data().message(fullId)) {
			item->destroy();
		}
	});
}

bool History::applyTopMessageHint(MsgId id, TimeId date) {
	if (!_topMessageHint) {
		_topMessageHint = std::make_unique<HistoryTopMessageHint>(this);
	}
	return _topMessageHint->apply(id, date);
}

bool History::topMessageHintActive(MsgId id) const {
	return _topMessageHint && _topMessageHint->active(id);
}

void History::topMessageHintLoaded(MsgId id, const MTPMessage *message) {
	if (const auto hint = _topMessageHint.get()) {
		hint->loaded(id, message);
	}
}

void History::clearTopMessageHint() {
	if (const auto hint = _topMessageHint.get()) {
		hint->clear();
	}
}

TimeId History::topMessageHintDate() const {
	return _topMessageHint ? _topMessageHint->date() : TimeId();
}

HistoryItem *History::chatListPreviewMessage() const {
	const auto item = _topMessageHint ? _topMessageHint->item() : nullptr;
	return item ? item : chatListMessage();
}

bool History::chatListPreviewMessageUnread(
		not_null<const HistoryItem*> item) {
	return _topMessageHint
		? _topMessageHint->unread(item)
		: item->unread(this);
}
