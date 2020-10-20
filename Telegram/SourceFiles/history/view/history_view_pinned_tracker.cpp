/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_tracker.h"

#include "data/data_changes.h"
#include "data/data_pinned_messages.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "apiwrap.h"

namespace HistoryView {
namespace {

constexpr auto kLoadedLimit = 4;
constexpr auto kPerPage = 40;

} // namespace

PinnedTracker::PinnedTracker(not_null<History*> history) : _history(history) {
	_history->session().changes().peerFlagsValue(
		_history->peer,
		Data::PeerUpdate::Flag::PinnedMessage
	) | rpl::start_with_next([=] {
		refreshData();
	}, _lifetime);
}

PinnedTracker::~PinnedTracker() {
	_history->owner().histories().cancelRequest(_beforeRequestId);
	_history->owner().histories().cancelRequest(_afterRequestId);
}

rpl::producer<PinnedId> PinnedTracker::shownMessageId() const {
	return _current.value();
}

void PinnedTracker::reset() {
	_current.reset(currentMessageId());
}

PinnedId PinnedTracker::currentMessageId() const {
	return _current.current();
}

void PinnedTracker::refreshData() {
	const auto now = _history->peer->currentPinnedMessages();
	if (!now) {
		_dataLifetime.destroy();
		_current = PinnedId();
	} else if (_data.get() != now) {
		_dataLifetime.destroy();
		_data = now;
		if (_aroundId) {
			setupViewer(now);
		}
	}
}

void PinnedTracker::trackAround(MsgId messageId) {
	if (_aroundId == messageId) {
		return;
	}
	_dataLifetime.destroy();
	_aroundId = messageId;
	if (!_aroundId) {
		_current = PinnedId();
	} else if (const auto now = _data.get()) {
		setupViewer(now);
	}
}

void PinnedTracker::setupViewer(not_null<Data::PinnedMessages*> data) {
	data->viewer(
		_aroundId,
		kLoadedLimit + 2
	) | rpl::start_with_next([=](const Data::PinnedAroundId &snapshot) {
		const auto i = ranges::lower_bound(snapshot.ids, _aroundId);
		const auto empty = snapshot.ids.empty();
		const auto before = (i - begin(snapshot.ids));
		const auto after = (end(snapshot.ids) - i);
		if (before < kLoadedLimit && !snapshot.skippedBefore) {
			load(
				Data::LoadDirection::Before,
				empty ? _aroundId : snapshot.ids.front());
		}
		if (after < kLoadedLimit && !snapshot.skippedAfter) {
			load(
				Data::LoadDirection::After,
				empty ? _aroundId : snapshot.ids.back());
		}
		if (snapshot.ids.empty()) {
			_current = PinnedId();
			return;
		}
		const auto count = std::max(
			snapshot.fullCount.value_or(1),
			int(snapshot.ids.size()));
		const auto index = snapshot.skippedBefore.has_value()
			? (*snapshot.skippedBefore + before)
			: snapshot.skippedAfter.has_value()
			? (count - *snapshot.skippedAfter - after)
			: 1;
		if (i != begin(snapshot.ids)) {
			_current = PinnedId{ *(i - 1), index - 1, count };
		} else if (snapshot.skippedBefore == 0) {
			_current = PinnedId{ snapshot.ids.front(), 0, count };
		}
	}, _dataLifetime);
}

void PinnedTracker::load(Data::LoadDirection direction, MsgId id) {
	const auto requestId = (direction == Data::LoadDirection::Before)
		? &_beforeRequestId
		: &_afterRequestId;
	const auto aroundId = (direction == Data::LoadDirection::Before)
		? &_beforeId
		: &_afterId;
	if (*requestId) {
		if (*aroundId == id) {
			return;
		}
		_history->owner().histories().cancelRequest(*requestId);
	}
	*aroundId = id;
	const auto send = [=](Fn<void()> finish) {
		const auto offsetId = [&] {
			switch (direction) {
			case Data::LoadDirection::Before: return id;
			case Data::LoadDirection::After: return id + 1;
			}
			Unexpected("Direction in PinnedTracker::load");
		}();
		const auto addOffset = [&] {
			switch (direction) {
			case Data::LoadDirection::Before: return 0;
			case Data::LoadDirection::After: return -kPerPage;
			}
			Unexpected("Direction in PinnedTracker::load");
		}();
		return _history->session().api().request(MTPmessages_Search(
			MTP_flags(0),
			_history->peer->input,
			MTP_string(QString()),
			MTP_inputPeerEmpty(),
			MTPint(), // top_msg_id
			MTP_inputMessagesFilterPinned(),
			MTP_int(0),
			MTP_int(0),
			MTP_int(offsetId),
			MTP_int(addOffset),
			MTP_int(kPerPage),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_int(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			*aroundId = 0;
			*requestId = 0;
			finish();

			apply(direction, id, result);
		}).fail([=](const RPCError &error) {
			*aroundId = 0;
			*requestId = 0;
			finish();
		}).send();
	};
	_beforeRequestId = _history->owner().histories().sendRequest(
		_history,
		Data::Histories::RequestType::History,
		send);
}

void PinnedTracker::apply(
		Data::LoadDirection direction,
		MsgId aroundId,
		const MTPmessages_Messages &result) {
	auto noSkipRange = MsgRange{ aroundId, aroundId };
	auto fullCount = std::optional<int>();
	auto messages = [&] {
		switch (result.type()) {
		case mtpc_messages_messages: {
			auto &d = result.c_messages_messages();
			_history->owner().processUsers(d.vusers());
			_history->owner().processChats(d.vchats());
			fullCount = d.vmessages().v.size();
			return &d.vmessages().v;
		} break;

		case mtpc_messages_messagesSlice: {
			auto &d = result.c_messages_messagesSlice();
			_history->owner().processUsers(d.vusers());
			_history->owner().processChats(d.vchats());
			fullCount = d.vcount().v;
			return &d.vmessages().v;
		} break;

		case mtpc_messages_channelMessages: {
			auto &d = result.c_messages_channelMessages();
			if (auto channel = _history->peer->asChannel()) {
				channel->ptsReceived(d.vpts().v);
			} else {
				LOG(("API Error: received messages.channelMessages when "
					"no channel was passed! (PinnedTracker::apply)"));
			}
			_history->owner().processUsers(d.vusers());
			_history->owner().processChats(d.vchats());
			fullCount = d.vcount().v;
			return &d.vmessages().v;
		} break;

		case mtpc_messages_messagesNotModified: {
			LOG(("API Error: received messages.messagesNotModified! "
				"(PinnedTracker::apply)"));
			return (const QVector<MTPMessage>*)nullptr;
		} break;
		}
		Unexpected("messages.Messages type in PinnedTracker::apply.");
	}();

	if (!messages) {
		return;
	}

	const auto addType = NewMessageType::Existing;
	auto list = std::vector<MsgId>();
	list.reserve(messages->size());
	for (const auto &message : *messages) {
		const auto item = _history->owner().addNewMessage(
			message,
			MTPDmessage_ClientFlags(),
			addType);
		if (item) {
			const auto itemId = item->id;
			if (item->isPinned()) {
				list.push_back(itemId);
			}
			accumulate_min(noSkipRange.from, itemId);
			accumulate_max(noSkipRange.till, itemId);
		}
	}
	if (aroundId && list.empty()) {
		noSkipRange = [&]() -> MsgRange {
			switch (direction) {
			case Data::LoadDirection::Before: // All old loaded.
				return { 0, noSkipRange.till };
			case Data::LoadDirection::Around: // All loaded.
				return { 0, ServerMaxMsgId };
			case Data::LoadDirection::After: // All new loaded.
				return { noSkipRange.from, ServerMaxMsgId };
			}
			Unexpected("Direction in PinnedTracker::apply.");
		}();
	}
	_history->peer->addPinnedSlice(std::move(list), noSkipRange, fullCount);
}

} // namespace HistoryView
