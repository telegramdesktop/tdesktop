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
#pragma once

#include "mtproto/sender.h"
#include "history/history_sparse_ids.h"
#include "storage/storage_sparse_ids_list.h"
#include "storage/storage_shared_media.h"

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
	SparseIdsLoadDirection direction);

SearchResult ParseSearchResult(
	not_null<PeerData*> peer,
	Storage::SharedMediaType type,
	MsgId messageId,
	SparseIdsLoadDirection direction,
	const MTPmessages_Messages &data);

class SingleSearchController : private MTP::Sender {
public:
	struct Query {
		using MediaType = Storage::SharedMediaType;

		PeerId peerId = 0;
		PeerId migratedPeerId = 0;
		MediaType type = MediaType::kCount;
		QString query;
		// from_id, min_date, max_date
	};

	SingleSearchController(const Query &query);

	rpl::producer<SparseIdsMergedSlice> idsSlice(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter);

	Query query() const {
		return _query;
	}

private:
	struct Data {
		explicit Data(not_null<PeerData*> peer) : peer(peer) {
		}

		not_null<PeerData*> peer;
		Storage::SparseIdsList list;
		base::flat_map<
			SparseIdsSliceBuilder::AroundData,
			mtpRequestId> requests;
	};
	using SliceUpdate = Storage::SparseIdsSliceUpdate;

	rpl::producer<SparseIdsSlice> simpleIdsSlice(
		PeerId peerId,
		MsgId aroundId,
		int limitBefore,
		int limitAfter);
	void requestMore(
		const SparseIdsSliceBuilder::AroundData &key,
		Data *listData);

	Query _query;
	Data _peerData;
	base::optional<Data> _migratedData;

};

class SearchController {
public:
	using Query = SingleSearchController::Query;
	void setQuery(const Query &query) {
		_controller = SingleSearchController(query);
	}

	Query query() const {
		return _controller ? _controller->query() : Query();
	}

	rpl::producer<SparseIdsMergedSlice> idsSlice(
			SparseIdsMergedSlice::UniversalMsgId aroundId,
			int limitBefore,
			int limitAfter) {
		Expects(_controller.has_value());
		return _controller->idsSlice(
			aroundId,
			limitBefore,
			limitAfter);
	}

private:
	base::optional<SingleSearchController> _controller;

};

class DelayedSearchController {
public:
	DelayedSearchController();

	using Query = SingleSearchController::Query;
	void setQuery(const Query &query);
	void setQuery(const Query &query, TimeMs delay) {
		_nextQuery = query;
		_timer.callOnce(delay);
	}
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

	rpl::producer<> sourceChanged() const {
		return _sourceChanges.events();
	}

private:
	SearchController _controller;
	Query _nextQuery;
	base::Timer _timer;
	rpl::event_stream<> _sourceChanges;

};

} // namespace Api
