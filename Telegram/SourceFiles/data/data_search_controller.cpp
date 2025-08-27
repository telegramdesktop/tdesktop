/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_search_controller.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_messages.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "history/history.h"
#include "history/history_item.h"
#include "apiwrap.h"

namespace Api {
namespace {

constexpr auto kSharedMediaLimit = 100;
constexpr auto kFirstSharedMediaLimit = 0;
constexpr auto kHistoryLimit = 50;
constexpr auto kDefaultSearchTimeoutMs = crl::time(200);

} // namespace

MTPMessagesFilter PrepareSearchFilter(Storage::SharedMediaType type) {
	using Type = Storage::SharedMediaType;
	switch (type) {
	case Type::Photo:
		return MTP_inputMessagesFilterPhotos();
	case Type::Video:
		return MTP_inputMessagesFilterVideo();
	case Type::PhotoVideo:
		return MTP_inputMessagesFilterPhotoVideo();
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
	case Type::Pinned:
		return MTP_inputMessagesFilterPinned();
	}
	return MTP_inputMessagesFilterEmpty();
}

std::optional<GlobalMediaRequest> PrepareGlobalMediaRequest(
		not_null<Main::Session*> session,
		int32 offsetRate,
		Data::MessagePosition offsetPosition,
		Storage::SharedMediaType type,
		const QString &query) {
	const auto filter = PrepareSearchFilter(type);
	if (query.isEmpty() && filter.type() == mtpc_inputMessagesFilterEmpty) {
		return std::nullopt;
	}

	const auto minDate = 0;
	const auto maxDate = 0;
	const auto folderId = 0;
	const auto limit = offsetPosition.fullId.peer
		? kSharedMediaLimit
		: kFirstSharedMediaLimit;
	return MTPmessages_SearchGlobal(
		MTP_flags(MTPmessages_SearchGlobal::Flag::f_folder_id), // No archive
		MTP_int(folderId),
		MTP_string(query),
		filter,
		MTP_int(minDate),
		MTP_int(maxDate),
		MTP_int(offsetRate),
		(offsetPosition.fullId.peer
			? session->data().peer(PeerId(offsetPosition.fullId.peer))->input
			: MTP_inputPeerEmpty()),
		MTP_int(offsetPosition.fullId.msg),
		MTP_int(limit));
}

GlobalMediaResult ParseGlobalMediaResult(
		not_null<Main::Session*> session,
		const MTPmessages_Messages &data) {
	auto result = GlobalMediaResult();

	auto messages = (const QVector<MTPMessage>*)nullptr;
	data.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		messages = &data.vmessages().v;
	});
	data.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const MTPDmessages_messages &data) {
		result.fullCount = data.vmessages().v.size();
	}, [&](const MTPDmessages_messagesSlice &data) {
		result.fullCount = data.vcount().v;
		result.offsetRate = data.vnext_rate().value_or_empty();
	}, [&](const MTPDmessages_channelMessages &data) {
		result.fullCount = data.vcount().v;
	});
	data.match([&](const MTPDmessages_channelMessages &data) {
		LOG(("API Error: received messages.channelMessages when "
			"no channel was passed! (ParseSearchResult)"));
	}, [](const auto &) {});

	const auto addType = NewMessageType::Existing;
	result.messageIds.reserve(messages->size());
	for (const auto &message : *messages) {
		const auto item = session->data().addNewMessage(
			message,
			MessageFlags(),
			addType);
		if (item) {
			result.messageIds.push_back(item->position());
		}
	}
	return result;
}

std::optional<SearchRequest> PrepareSearchRequest(
		not_null<PeerData*> peer,
		MsgId topicRootId,
	    PeerId monoforumPeerId,
		Storage::SharedMediaType type,
		const QString &query,
		MsgId messageId,
		Data::LoadDirection direction) {
	const auto filter = PrepareSearchFilter(type);
	if (query.isEmpty() && filter.type() == mtpc_inputMessagesFilterEmpty) {
		return std::nullopt;
	}

	const auto minId = 0;
	const auto maxId = 0;
	const auto limit = messageId ? kSharedMediaLimit : kFirstSharedMediaLimit;
	const auto offsetId = [&] {
		switch (direction) {
		case Data::LoadDirection::Before:
		case Data::LoadDirection::Around: return messageId;
		case Data::LoadDirection::After: return messageId + 1;
		}
		Unexpected("Direction in PrepareSearchRequest");
	}();
	const auto addOffset = [&] {
		switch (direction) {
		case Data::LoadDirection::Before: return 0;
		case Data::LoadDirection::Around: return -limit / 2;
		case Data::LoadDirection::After: return -limit;
		}
		Unexpected("Direction in PrepareSearchRequest");
	}();
	const auto hash = uint64(0);

	const auto mtpOffsetId = int(std::clamp(
		offsetId.bare,
		int64(0),
		int64(0x3FFFFFFF)));
	using Flag = MTPmessages_Search::Flag;
	return MTPmessages_Search(
		MTP_flags((topicRootId ? Flag::f_top_msg_id : Flag(0))
			| (monoforumPeerId ? Flag::f_saved_peer_id : Flag(0))),
		peer->input,
		MTP_string(query),
		MTP_inputPeerEmpty(),
		(monoforumPeerId
			? peer->owner().peer(monoforumPeerId)->input
			: MTPInputPeer()),
		MTPVector<MTPReaction>(), // saved_reaction
		MTP_int(topicRootId),
		filter,
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(mtpOffsetId),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(maxId),
		MTP_int(minId),
		MTP_long(hash));
}

SearchResult ParseSearchResult(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type,
		MsgId messageId,
		Data::LoadDirection direction,
		const SearchRequestResult &data) {
	auto result = SearchResult();
	result.noSkipRange = MsgRange{ messageId, messageId };

	auto messages = [&] {
		switch (data.type()) {
		case mtpc_messages_messages: {
			auto &d = data.c_messages_messages();
			peer->owner().processUsers(d.vusers());
			peer->owner().processChats(d.vchats());
			result.fullCount = d.vmessages().v.size();
			return &d.vmessages().v;
		} break;

		case mtpc_messages_messagesSlice: {
			auto &d = data.c_messages_messagesSlice();
			peer->owner().processUsers(d.vusers());
			peer->owner().processChats(d.vchats());
			result.fullCount = d.vcount().v;
			return &d.vmessages().v;
		} break;

		case mtpc_messages_channelMessages: {
			const auto &d = data.c_messages_channelMessages();
			if (const auto channel = peer->asChannel()) {
				channel->ptsReceived(d.vpts().v);
				channel->processTopics(d.vtopics());
			} else {
				LOG(("API Error: received messages.channelMessages when "
					"no channel was passed! (ParseSearchResult)"));
			}
			peer->owner().processUsers(d.vusers());
			peer->owner().processChats(d.vchats());
			result.fullCount = d.vcount().v;
			return &d.vmessages().v;
		} break;

		case mtpc_messages_messagesNotModified: {
			LOG(("API Error: received messages.messagesNotModified! "
				"(ParseSearchResult)"));
			return (const QVector<MTPMessage>*)nullptr;
		} break;
		}
		Unexpected("messages.Messages type in ParseSearchResult()");
	}();

	if (!messages) {
		return result;
	}

	const auto addType = NewMessageType::Existing;
	result.messageIds.reserve(messages->size());
	for (const auto &message : *messages) {
		const auto item = peer->owner().addNewMessage(
			message,
			MessageFlags(),
			addType);
		if (item) {
			const auto itemId = item->id;
			if ((type == Storage::SharedMediaType::kCount)
				|| item->sharedMediaTypes().test(type)) {
				result.messageIds.push_back(itemId);
			}
			accumulate_min(result.noSkipRange.from, itemId);
			accumulate_max(result.noSkipRange.till, itemId);
		}
	}
	if (messageId && result.messageIds.empty()) {
		result.noSkipRange = [&]() -> MsgRange {
			switch (direction) {
			case Data::LoadDirection::Before: // All old loaded.
				return { 0, result.noSkipRange.till };
			case Data::LoadDirection::Around: // All loaded.
				return { 0, ServerMaxMsgId };
			case Data::LoadDirection::After: // All new loaded.
				return { result.noSkipRange.from, ServerMaxMsgId };
			}
			Unexpected("Direction in ParseSearchResult");
		}();
	}
	return result;
}

HistoryRequest PrepareHistoryRequest(
		not_null<PeerData*> peer,
		MsgId messageId,
		Data::LoadDirection direction) {
	const auto minId = 0;
	const auto maxId = 0;
	const auto limit = kHistoryLimit;
	const auto offsetId = [&] {
		switch (direction) {
		case Data::LoadDirection::Before:
		case Data::LoadDirection::Around: return messageId;
		case Data::LoadDirection::After: return messageId + 1;
		}
		Unexpected("Direction in PrepareSearchRequest");
	}();
	const auto addOffset = [&] {
		switch (direction) {
		case Data::LoadDirection::Before: return 0;
		case Data::LoadDirection::Around: return -limit / 2;
		case Data::LoadDirection::After: return -limit;
		}
		Unexpected("Direction in PrepareSearchRequest");
	}();
	const auto hash = uint64(0);
	const auto offsetDate = int32(0);

	const auto mtpOffsetId = int(std::clamp(
		offsetId.bare,
		int64(0),
		int64(0x3FFFFFFF)));
	return MTPmessages_GetHistory(
		peer->input,
		MTP_int(mtpOffsetId),
		MTP_int(offsetDate),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(maxId),
		MTP_int(minId),
		MTP_long(hash));
}

HistoryResult ParseHistoryResult(
		not_null<PeerData*> peer,
		MsgId messageId,
		Data::LoadDirection direction,
		const HistoryRequestResult &data) {
	return ParseSearchResult(
		peer,
		Storage::SharedMediaType::kCount,
		messageId,
		direction,
		data);
}

SearchController::CacheEntry::CacheEntry(
	not_null<Main::Session*> session,
	const Query &query)
: peerData(session->data().peer(query.peerId))
, migratedData(query.migratedPeerId
	? base::make_optional(Data(session->data().peer(query.migratedPeerId)))
	: std::nullopt) {
}

SearchController::SearchController(not_null<Main::Session*> session)
: _session(session) {
}

bool SearchController::hasInCache(const Query &query) const {
	return query.query.isEmpty() || _cache.contains(query);
}

void SearchController::setQuery(const Query &query) {
	if (query.query.isEmpty()) {
		_cache.clear();
		_current = _cache.end();
	} else {
		_current = _cache.find(query);
	}
	if (_current == _cache.end()) {
		_current = _cache.emplace(
			query,
			std::make_unique<CacheEntry>(_session, query)).first;
	}
}

rpl::producer<SparseIdsMergedSlice> SearchController::idsSlice(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) {
	Expects(_current != _cache.cend());

	auto query = (const Query&)_current->first;
	auto createSimpleViewer = [=](
			PeerId peerId,
			MsgId topicRootId,
			PeerId monoforumPeerId,
			SparseIdsSlice::Key simpleKey,
			int limitBefore,
			int limitAfter) {
		return simpleIdsSlice(
			peerId,
			topicRootId,
			monoforumPeerId,
			simpleKey,
			query,
			limitBefore,
			limitAfter);
	};
	return SparseIdsMergedSlice::CreateViewer(
		SparseIdsMergedSlice::Key(
			query.peerId,
			query.topicRootId,
			query.monoforumPeerId,
			query.migratedPeerId,
			aroundId),
		limitBefore,
		limitAfter,
		std::move(createSimpleViewer));
}

rpl::producer<SparseIdsSlice> SearchController::simpleIdsSlice(
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		MsgId aroundId,
		const Query &query,
		int limitBefore,
		int limitAfter) {
	Expects(peerId != 0);
	Expects(IsServerMsgId(aroundId) || (aroundId == 0));
	Expects((aroundId != 0)
		|| (limitBefore == 0 && limitAfter == 0));
	Expects((query.peerId == peerId
		&& query.topicRootId == topicRootId
		&& query.monoforumPeerId == monoforumPeerId)
		|| (query.migratedPeerId == peerId
			&& MsgId(0) == topicRootId
			&& PeerId(0) == monoforumPeerId));

	auto it = _cache.find(query);
	if (it == _cache.end()) {
		return [=](auto) { return rpl::lifetime(); };
	}

	auto listData = (peerId == query.peerId)
		? &it->second->peerData
		: &*it->second->migratedData;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto builder = lifetime.make_state<SparseIdsSliceBuilder>(
			aroundId,
			limitBefore,
			limitAfter);
		builder->insufficientAround(
		) | rpl::start_with_next([=](
				const SparseIdsSliceBuilder::AroundData &data) {
			requestMore(data, query, listData);
		}, lifetime);

		auto pushNextSnapshot = [=] {
			consumer.put_next(builder->snapshot());
		};

		listData->list.sliceUpdated(
		) | rpl::filter([=](const SliceUpdate &update) {
			return builder->applyUpdate(update);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		_session->data().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return (item->history()->peer->id == peerId)
				&& (!topicRootId || item->topicRootId() == topicRootId)
				&& (!monoforumPeerId
					|| item->sublistPeerId() == monoforumPeerId);
		}) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return builder->removeOne(item->id);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		_session->data().historyCleared(
		) | rpl::filter([=](not_null<const History*> history) {
			return (history->peer->id == peerId);
		}) | rpl::filter([=] {
			return builder->removeAll();
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using Result = Storage::SparseIdsListResult;
		listData->list.query(Storage::SparseIdsListQuery(
			aroundId,
			limitBefore,
			limitAfter
		)) | rpl::filter([=](const Result &result) {
			return builder->applyInitial(result);
		}) | rpl::start_with_next_done(
			pushNextSnapshot,
			[=] { builder->checkInsufficient(); },
			lifetime);

		return lifetime;
	};
}

auto SearchController::saveState() -> SavedState {
	auto result = SavedState();
	if (_current != _cache.end()) {
		result.query = _current->first;
		result.peerList = std::move(_current->second->peerData.list);
		if (auto &migrated = _current->second->migratedData) {
			result.migratedList = std::move(migrated->list);
		}
	}
	return result;
}

void SearchController::restoreState(SavedState &&state) {
	if (!state.query.peerId) {
		return;
	}

	auto it = _cache.find(state.query);
	if (it == _cache.end()) {
		it = _cache.emplace(
			state.query,
			std::make_unique<CacheEntry>(_session, state.query)).first;
	}
	auto replace = Data(it->second->peerData.peer);
	replace.list = std::move(state.peerList);
	it->second->peerData = std::move(replace);
	if (auto &migrated = state.migratedList) {
		Assert(it->second->migratedData.has_value());
		auto replace = Data(it->second->migratedData->peer);
		replace.list = std::move(*migrated);
		it->second->migratedData = std::move(replace);
	}
	_current = it;
}

void SearchController::requestMore(
		const SparseIdsSliceBuilder::AroundData &key,
		const Query &query,
		Data *listData) {
	if (listData->requests.contains(key)) {
		return;
	}
	auto prepared = PrepareSearchRequest(
		listData->peer,
		query.topicRootId,
		query.monoforumPeerId,
		query.type,
		query.query,
		key.aroundId,
		key.direction);
	if (!prepared) {
		return;
	}
	auto &histories = _session->data().histories();
	const auto type = ::Data::Histories::RequestType::History;
	const auto history = _session->data().history(listData->peer);
	auto requestId = histories.sendRequest(history, type, [=](Fn<void()> finish) {
		return _session->api().request(
			std::move(*prepared)
		).done([=](const SearchRequestResult &result) {
			listData->requests.remove(key);
			auto parsed = ParseSearchResult(
				listData->peer,
				query.type,
				key.aroundId,
				key.direction,
				result);
			listData->list.addSlice(
				std::move(parsed.messageIds),
				parsed.noSkipRange,
				parsed.fullCount);
			finish();
		}).fail([=] {
			finish();
		}).send();
	});
	listData->requests.emplace(key, [=] {
		_session->data().histories().cancelRequest(requestId);
	});
}

DelayedSearchController::DelayedSearchController(
	not_null<Main::Session*> session)
: _controller(session) {
	_timer.setCallback([this] { setQueryFast(_nextQuery); });
}

void DelayedSearchController::setQuery(const Query &query) {
	setQuery(query, kDefaultSearchTimeoutMs);
}

void DelayedSearchController::setQuery(
		const Query &query,
		crl::time delay) {
	if (currentQuery() == query) {
		_timer.cancel();
		return;
	}
	if (_controller.hasInCache(query)) {
		setQueryFast(query);
	} else {
		_nextQuery = query;
		_timer.callOnce(delay);
	}
}

void DelayedSearchController::setQueryFast(const Query &query) {
	_controller.setQuery(query);
	_currentQueryChanges.fire_copy(query.query);
}

} // namespace Api
