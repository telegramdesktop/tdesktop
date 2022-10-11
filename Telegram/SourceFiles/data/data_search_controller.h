/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_sparse_ids.h"
#include "storage/storage_sparse_ids_list.h"
#include "storage/storage_shared_media.h"
#include "base/timer.h"
#include "base/qt/qt_compare.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
enum class LoadDirection : char;
} // namespace Data

namespace Api {

struct SearchResult {
	std::vector<MsgId> messageIds;
	MsgRange noSkipRange;
	int fullCount = 0;
};

using SearchRequest = MTPmessages_Search;
using SearchRequestResult = MTPmessages_Messages;

std::optional<SearchRequest> PrepareSearchRequest(
	not_null<PeerData*> peer,
	MsgId topicRootId,
	Storage::SharedMediaType type,
	const QString &query,
	MsgId messageId,
	Data::LoadDirection direction);

SearchResult ParseSearchResult(
	not_null<PeerData*> peer,
	Storage::SharedMediaType type,
	MsgId messageId,
	Data::LoadDirection direction,
	const SearchRequestResult &data);

class SearchController final {
public:
	using IdsList = Storage::SparseIdsList;
	struct Query {
		using MediaType = Storage::SharedMediaType;

		PeerId peerId = 0;
		MsgId topicRootId = 0;
		PeerId migratedPeerId = 0;
		MediaType type = MediaType::kCount;
		QString query;
		// from_id, min_date, max_date

		friend inline std::strong_ordering operator<=>(
			const Query &a,
			const Query &b) noexcept = default;

	};
	struct SavedState {
		Query query;
		IdsList peerList;
		std::optional<IdsList> migratedList;
	};

	explicit SearchController(not_null<Main::Session*> session);
	void setQuery(const Query &query);
	bool hasInCache(const Query &query) const;

	Query query() const {
		Expects(_current != _cache.cend());

		return _current->first;
	}

	rpl::producer<SparseIdsMergedSlice> idsSlice(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter);

	SavedState saveState();
	void restoreState(SavedState &&state);

private:
	struct Data {
		explicit Data(not_null<PeerData*> peer) : peer(peer) {
		}

		not_null<PeerData*> peer;
		IdsList list;
		base::flat_map<
			SparseIdsSliceBuilder::AroundData,
			rpl::lifetime> requests;
	};
	using SliceUpdate = Storage::SparseIdsSliceUpdate;

	struct CacheEntry {
		CacheEntry(not_null<Main::Session*> session, const Query &query);

		Data peerData;
		std::optional<Data> migratedData;
	};

	using Cache = base::flat_map<Query, std::unique_ptr<CacheEntry>>;

	rpl::producer<SparseIdsSlice> simpleIdsSlice(
		PeerId peerId,
		MsgId topicRootId,
		MsgId aroundId,
		const Query &query,
		int limitBefore,
		int limitAfter);
	void requestMore(
		const SparseIdsSliceBuilder::AroundData &key,
		const Query &query,
		Data *listData);

	const not_null<Main::Session*> _session;
	Cache _cache;
	Cache::iterator _current = _cache.end();

};

class DelayedSearchController {
public:
	explicit DelayedSearchController(not_null<Main::Session*> session);

	using Query = SearchController::Query;
	using SavedState = SearchController::SavedState;

	void setQuery(const Query &query);
	void setQuery(const Query &query, crl::time delay);
	void setQueryFast(const Query &query);

	Query currentQuery() const {
		return _controller.query();
	}

	rpl::producer<SparseIdsMergedSlice> idsSlice(
			SparseIdsMergedSlice::UniversalMsgId aroundId,
			int limitBefore,
			int limitAfter) {
		return _controller.idsSlice(
			aroundId,
			limitBefore,
			limitAfter);
	}

	rpl::producer<QString> currentQueryValue() const {
		return _currentQueryChanges.events_starting_with(
			currentQuery().query);
	}

	SavedState saveState() {
		return _controller.saveState();
	}

	void restoreState(SavedState &&state) {
		_controller.restoreState(std::move(state));
	}

private:
	SearchController _controller;
	Query _nextQuery;
	base::Timer _timer;
	rpl::event_stream<QString> _currentQueryChanges;

};

} // namespace Api
