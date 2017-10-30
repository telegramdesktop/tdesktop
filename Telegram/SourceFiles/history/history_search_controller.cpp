/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_search_controller.h"

#include "auth_session.h"

namespace Api {
namespace {

constexpr auto kSharedMediaLimit = 100;
constexpr auto kDefaultSearchTimeoutMs = TimeMs(200);

} // namespace

MTPmessages_Search PrepareSearchRequest(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type,
		const QString &query,
		MsgId messageId,
		SparseIdsLoadDirection direction) {
	auto filter = [&] {
		using Type = Storage::SharedMediaType;
		switch (type) {
		case Type::Photo:
			return MTP_inputMessagesFilterPhotos();
		case Type::Video:
			return MTP_inputMessagesFilterVideo();
		case Type::MusicFile:
			return MTP_inputMessagesFilterMusic();
		case Type::File:
			return MTP_inputMessagesFilterDocument();
		case Type::VoiceFile:
			return MTP_inputMessagesFilterVoice();
		case Type::RoundVoiceFile:
			return MTP_inputMessagesFilterRoundVoice();
		case Type::RoundFile:
			return MTP_inputMessagesFilterRoundVideo();
		case Type::GIF:
			return MTP_inputMessagesFilterGif();
		case Type::Link:
			return MTP_inputMessagesFilterUrl();
		case Type::ChatPhoto:
			return MTP_inputMessagesFilterChatPhotos();
		}
		return MTP_inputMessagesFilterEmpty();
	}();

	auto minId = 0;
	auto maxId = 0;
	auto limit = messageId ? kSharedMediaLimit : 0;
	auto offsetId = [&] {
		switch (direction) {
		case SparseIdsLoadDirection::Before:
		case SparseIdsLoadDirection::Around: return messageId;
		case SparseIdsLoadDirection::After: return messageId + 1;
		}
		Unexpected("Direction in PrepareSearchRequest");
	}();
	auto addOffset = [&] {
		switch (direction) {
		case SparseIdsLoadDirection::Before: return 0;
		case SparseIdsLoadDirection::Around: return -limit / 2;
		case SparseIdsLoadDirection::After: return -limit;
		}
		Unexpected("Direction in PrepareSearchRequest");
	}();

	return MTPmessages_Search(
		MTP_flags(0),
		peer->input,
		MTP_string(query),
		MTP_inputUserEmpty(),
		filter,
		MTP_int(0),
		MTP_int(0),
		MTP_int(offsetId),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(maxId),
		MTP_int(minId));
}

SearchResult ParseSearchResult(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type,
		MsgId messageId,
		SparseIdsLoadDirection direction,
		const MTPmessages_Messages &data) {
	auto result = SearchResult();
	auto &messages = *[&] {
		switch (data.type()) {
		case mtpc_messages_messages: {
			auto &d = data.c_messages_messages();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			result.fullCount = d.vmessages.v.size();
			return &d.vmessages.v;
		} break;

		case mtpc_messages_messagesSlice: {
			auto &d = data.c_messages_messagesSlice();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			result.fullCount = d.vcount.v;
			return &d.vmessages.v;
		} break;

		case mtpc_messages_channelMessages: {
			auto &d = data.c_messages_channelMessages();
			if (auto channel = peer->asChannel()) {
				channel->ptsReceived(d.vpts.v);
			} else {
				LOG(("API Error: received messages.channelMessages when no channel was passed! (ParseSearchResult)"));
			}
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			result.fullCount = d.vcount.v;
			return &d.vmessages.v;
		} break;
		}
		Unexpected("messages.Messages type in ParseSearchResult()");
	}();

	result.noSkipRange = MsgRange{ messageId, messageId };
	auto addType = NewMessageExisting;
	result.messageIds.reserve(messages.size());
	for (auto &message : messages) {
		if (auto item = App::histories().addNewMessage(message, addType)) {
			if ((type == Storage::SharedMediaType::kCount)
				|| item->sharedMediaTypes().test(type)) {
				auto itemId = item->id;
				result.messageIds.push_back(itemId);
				accumulate_min(result.noSkipRange.from, itemId);
				accumulate_max(result.noSkipRange.till, itemId);
			}
		}
	}
	if (messageId && result.messageIds.empty()) {
		result.noSkipRange = [&]() -> MsgRange {
			switch (direction) {
			case SparseIdsLoadDirection::Before: // All old loaded.
				return { 0, result.noSkipRange.till };
			case SparseIdsLoadDirection::Around: // All loaded.
				return { 0, ServerMaxMsgId };
			case SparseIdsLoadDirection::After: // All new loaded.
				return { result.noSkipRange.from, ServerMaxMsgId };
			}
			Unexpected("Direction in ParseSearchResult");
		}();
	}
	return result;
}

SingleSearchController::SingleSearchController(const Query &query)
: _query(query)
, _peerData(App::peer(query.peerId))
, _migratedData(query.migratedPeerId
	? base::make_optional(Data(App::peer(query.migratedPeerId)))
	: base::none) {
}

rpl::producer<SparseIdsMergedSlice> SingleSearchController::idsSlice(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) {
	auto createSimpleViewer = [this](
			PeerId peerId,
			SparseIdsSlice::Key simpleKey,
			int limitBefore,
			int limitAfter) {
		return simpleIdsSlice(
			peerId,
			simpleKey,
			limitBefore,
			limitAfter);
	};
	return SparseIdsMergedSlice::CreateViewer(
		SparseIdsMergedSlice::Key(
			_query.peerId,
			_query.migratedPeerId,
			aroundId),
		limitBefore,
		limitAfter,
		std::move(createSimpleViewer));
}

rpl::producer<SparseIdsSlice> SingleSearchController::simpleIdsSlice(
		PeerId peerId,
		MsgId aroundId,
		int limitBefore,
		int limitAfter) {
	Expects(peerId != 0);
	Expects(IsServerMsgId(aroundId) || (aroundId == 0));
	Expects((aroundId != 0)
		|| (limitBefore == 0 && limitAfter == 0));
	Expects((_query.peerId == peerId)
		|| (_query.migratedPeerId == peerId));

	auto listData = (peerId == _query.peerId)
		? &_peerData
		: &*_migratedData;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto builder = lifetime.make_state<SparseIdsSliceBuilder>(
			aroundId,
			limitBefore,
			limitAfter);
		builder->insufficientAround()
			| rpl::start_with_next([=](
					const SparseIdsSliceBuilder::AroundData &data) {
				requestMore(data, listData);
			}, lifetime);

		auto pushNextSnapshot = [=] {
			consumer.put_next(builder->snapshot());
		};

		listData->list.sliceUpdated()
			| rpl::filter([=](const SliceUpdate &update) {
				return builder->applyUpdate(update);
			})
			| rpl::start_with_next(pushNextSnapshot, lifetime);

		Auth().data().itemRemoved()
			| rpl::filter([=](not_null<const HistoryItem*> item) {
				return (item->history()->peer->id == peerId);
			})
			| rpl::filter([=](not_null<const HistoryItem*> item) {
				return builder->removeOne(item->id);
			})
			| rpl::start_with_next(pushNextSnapshot, lifetime);

		Auth().data().historyCleared()
			| rpl::filter([=](not_null<const History*> history) {
				return (history->peer->id == peerId);
			})
			| rpl::filter([=] { return builder->removeAll(); })
			| rpl::start_with_next(pushNextSnapshot, lifetime);

		builder->checkInsufficient();

		return lifetime;
	};
}

void SingleSearchController::requestMore(
		const SparseIdsSliceBuilder::AroundData &key,
		Data *listData) {
	if (listData->requests.contains(key)) {
		return;
	}
	auto requestId = request(PrepareSearchRequest(
		listData->peer,
		_query.type,
		_query.query,
		key.aroundId,
		key.direction)
	).done([=](const MTPmessages_Messages &result) {
		auto parsed = ParseSearchResult(
			listData->peer,
			_query.type,
			key.aroundId,
			key.direction,
			result);
		listData->list.addSlice(
			std::move(parsed.messageIds),
			parsed.noSkipRange,
			parsed.fullCount);
	}).send();

	listData->requests.emplace(key, requestId);
}

DelayedSearchController::DelayedSearchController() {
	_timer.setCallback([this] { setQueryFast(_nextQuery); });
}

void DelayedSearchController::setQuery(const Query &query) {
	setQuery(
		query,
		query.query.isEmpty() ? 0 : kDefaultSearchTimeoutMs);
}

void DelayedSearchController::setQueryFast(const Query &query) {
	_controller.setQuery(query);
	_sourceChanges.fire({});
}

} // namespace Api
