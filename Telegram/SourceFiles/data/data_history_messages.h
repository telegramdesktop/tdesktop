/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_sparse_ids_list.h"

class History;
class SparseIdsSlice;
class SparseIdsMergedSlice;

namespace Data {

struct MessagesSlice;
struct MessagePosition;

class HistoryMessages final {
public:
	void addNew(MsgId messageId);
	void addExisting(MsgId messageId, MsgRange noSkipRange);
	void addSlice(
		std::vector<MsgId> &&messageIds,
		MsgRange noSkipRange,
		std::optional<int> count);
	void removeOne(MsgId messageId);
	void removeAll();
	void invalidateBottom();

	[[nodiscard]] Storage::SparseIdsListResult snapshot(
		const Storage::SparseIdsListQuery &query) const;
	[[nodiscard]] auto sliceUpdated() const
		-> rpl::producer<Storage::SparseIdsSliceUpdate>;
	[[nodiscard]] rpl::producer<MsgId> oneRemoved() const;
	[[nodiscard]] rpl::producer<> allRemoved() const;
	[[nodiscard]] rpl::producer<> bottomInvalidated() const;

private:
	Storage::SparseIdsList _chat;
	rpl::event_stream<MsgId> _oneRemoved;
	rpl::event_stream<> _allRemoved;
	rpl::event_stream<> _bottomInvalidated;

};

[[nodiscard]] rpl::producer<SparseIdsSlice> HistoryViewer(
	not_null<History*> history,
	MsgId aroundId,
	int limitBefore,
	int limitAfter);

[[nodiscard]] rpl::producer<SparseIdsMergedSlice> HistoryMergedViewer(
	not_null<History*> history,
	/*Universal*/MsgId universalAroundId,
	int limitBefore,
	int limitAfter);

[[nodiscard]] rpl::producer<MessagesSlice> HistoryMessagesViewer(
	not_null<History*> history,
	MessagePosition aroundId,
	int limitBefore,
	int limitAfter);

} // namespace Data
