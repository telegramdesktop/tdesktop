/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Api {

namespace {

MessageIdsList HistoryItemsFromTL(
		not_null<Data::Session*> data,
		const QVector<MTPMessage> &messages) {
	auto result = MessageIdsList();
	for (const auto &message : messages) {
		const auto peerId = PeerFromMessage(message);
		if (const auto peer = data->peerLoaded(peerId)) {
			if (const auto lastDate = DateFromMessage(message)) {
				const auto item = data->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				result.push_back(item->fullId());
			}
		} else {
			LOG(("API Error: a search results with not loaded peer %1"
				).arg(peerId.value));
		}
	}
	return result;
}

} // namespace

MessagesSearch::MessagesSearch(
	not_null<Main::Session*> session,
	not_null<History*> history)
: _session(session)
, _history(history)
, _api(&session->mtp()) {
}

void MessagesSearch::searchMessages(const QString &query, PeerData *from) {
	_query = query;
	_from = from;
	_offsetId = {};
	searchRequest();
}

void MessagesSearch::searchMore() {
	if (_searchInHistoryRequest || _requestId) {
		return;
	}
	searchRequest();
}

void MessagesSearch::searchRequest() {
	const auto nextToken = _query
		+ QString::number(_from ? _from->id.value : 0);
	if (!_offsetId) {
		const auto it = _cacheOfStartByToken.find(nextToken);
		if (it != end(_cacheOfStartByToken)) {
			_requestId = 0;
			searchReceived(it->second, _requestId, nextToken);
			return;
		}
	}
	auto callback = [=](Fn<void()> finish) {
		const auto flags = _from
			? MTP_flags(MTPmessages_Search::Flag::f_from_id)
			: MTP_flags(0);
		_requestId = _api.request(MTPmessages_Search(
			flags,
			_history->peer->input,
			MTP_string(_query),
			(_from
				? _from->input
				: MTP_inputPeerEmpty()),
			MTPint(), // top_msg_id
			MTP_inputMessagesFilterEmpty(),
			MTP_int(0), // min_date
			MTP_int(0), // max_date
			MTP_int(_offsetId), // offset_id
			MTP_int(0), // add_offset
			MTP_int(SearchPerPage),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const TLMessages &result, mtpRequestId id) {
			_searchInHistoryRequest = 0;
			searchReceived(result, id, nextToken);
			finish();
		}).fail([=](const MTP::Error &error, mtpRequestId id) {
			_searchInHistoryRequest = 0;

			if (error.type() == u"SEARCH_QUERY_EMPTY"_q) {
				_messagesFounds.fire({ 0, MessageIdsList(), nextToken });
			} else if (_requestId == id) {
				_requestId = 0;
			}

			finish();
		}).send();
		return _requestId;
	};
	_searchInHistoryRequest = _session->data().histories().sendRequest(
		_history,
		Data::Histories::RequestType::History,
		std::move(callback));
}

void MessagesSearch::searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken) {
	if (requestId != _requestId) {
		return;
	}
	auto &owner = _session->data();
	auto found = result.match([&](const MTPDmessages_messages &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		const auto total = int(data.vmessages().v.size());
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_messagesSlice &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		// data.vnext_rate() is used only in global search.
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_channelMessages &data) {
		if (const auto channel = _history->peer->asChannel()) {
			channel->ptsReceived(data.vpts().v);
		} else {
			LOG(("API Error: "
				"received messages.channelMessages when no channel "
				"was passed!"));
		}
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		const auto total = int(data.vmessages().v.size());
		return FoundMessages{ total, std::move(items), nextToken };
	}, [](const MTPDmessages_messagesNotModified &data) {
		return FoundMessages{};
	});
	if (!_offsetId) {
		_cacheOfStartByToken.emplace(nextToken, result);
	}
	_requestId = 0;
	_offsetId = found.messages.empty()
		? MsgId()
		: found.messages.back().msg;
	_messagesFounds.fire(std::move(found));
}

rpl::producer<FoundMessages> MessagesSearch::messagesFounds() const {
	return _messagesFounds.events();
}

} // namespace Api
