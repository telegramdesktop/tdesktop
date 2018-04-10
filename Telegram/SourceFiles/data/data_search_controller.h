/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "data/data_sparse_ids.h"
#include "storage/storage_sparse_ids_list.h"
#include "storage/storage_shared_media.h"
#include "base/value_ordering.h"
#include "base/timer.h"

namespace Data {
enum class LoadDirection : char;
} // namespace Data

namespace Api {

struct SearchResult {
	std::vector<MsgId> messageIds;
	MsgRange noSkipRange;
	int fullCount = 0;
};

MTPmessages_Search PrepareSearchRequest(
	not_null<PeerData*> peer,
	Storage::SharedMediaType type,
	const QString &query,
	MsgId messageId,
	Data::LoadDirection direction);

SearchResult ParseSearchResult(
	not_null<PeerData*> peer,
	Storage::SharedMediaType type,
	MsgId messageId,
	Data::LoadDirection direction,
	const MTPmessages_Messages &data);

class SearchController : private MTP::Sender {
public:
	using IdsList = Storage::SparseIdsList;
	struct Query {
		using MediaType = Storage::SharedMediaType;

		PeerId peerId = 0;
		PeerId migratedPeerId = 0;
		MediaType type = MediaType::kCount;
		QString query;
		// from_id, min_date, max_date

		friend inline auto value_ordering_helper(const Query &value) {
			return std::tie(
				value.peerId,
				value.migratedPeerId,
				value.type,
				value.query);
		}

	};
	struct SavedState {
		Query query;
		IdsList peerList;
		base::optional<IdsList> migratedList;
	};

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
		CacheEntry(const Query &query);

		Data peerData;
		base::optional<Data> migratedData;
	};

	struct CacheLess {
		inline bool operator()(const Query &a, const Query &b) const {
			return (a < b);
		}
	};
	using Cache = base::flat_map<
		Query,
		std::unique_ptr<CacheEntry>,
		CacheLess>;

	rpl::producer<SparseIdsSlice> simpleIdsSlice(
		PeerId peerId,
		MsgId aroundId,
		const Query &query,
		int limitBefore,
		int limitAfter);
	void requestMore(
		const SparseIdsSliceBuilder::AroundData &key,
		const Query &query,
		Data *listData);

	Cache _cache;
	Cache::iterator _current = _cache.end();

};

class DelayedSearchController {
public:
	DelayedSearchController();

	using Query = SearchController::Query;
	using SavedState = SearchController::SavedState;

	void setQuery(const Query &query);
	void setQuery(const Query &query, TimeMs delay);
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
