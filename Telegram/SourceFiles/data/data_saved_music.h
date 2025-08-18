/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_abstract_sparse_ids.h"

#include "history/history_item.h"

class PeerData;

namespace Data {

class Session;
struct FileOrigin;

class SavedMusic final {
public:
	explicit SavedMusic(not_null<Session*> owner);
	~SavedMusic();

	[[nodiscard]] static bool Supported(PeerId peerId);

	[[nodiscard]] bool countKnown(PeerId peerId) const;
	[[nodiscard]] int count(PeerId peerId) const;
	[[nodiscard]] const std::vector<not_null<HistoryItem*>> &list(
		PeerId peerId) const;
	void loadMore(PeerId peerId);

	[[nodiscard]] rpl::producer<PeerId> changed() const;

	void loadIds();
	[[nodiscard]] bool has(not_null<DocumentData*> document) const;
	void save(not_null<DocumentData*> document, FileOrigin origin);
	void remove(not_null<DocumentData*> document);

	void apply(not_null<UserData*> user, const MTPDocument *last);

	void clear();

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;

	struct Entry {
		base::flat_map<MsgId, not_null<DocumentData*>> musicIdFromMsgId;
		base::flat_map<not_null<DocumentData*>, OwnedItem> musicIdToMsg;
		std::vector<not_null<HistoryItem*>> list;
		History *history = nullptr;
		mtpRequestId requestId = 0;
		int total = -1;
		bool loaded = false;
		bool reloading = false;
	};

	void loadMore(PeerId peerId, bool reload);
	[[nodiscard]] Entry *lookupEntry(PeerId peerId);
	[[nodiscard]] const Entry *lookupEntry(PeerId peerId) const;
	[[nodiscard]] uint64 firstPageHash(const Entry &entry) const;
	[[nodiscard]] not_null<HistoryItem*> musicIdToMsg(
		PeerId peerId,
		Entry &entry,
		not_null<DocumentData*> id);

	const not_null<Session*> _owner;

	std::vector<DocumentId> _myIds;
	crl::time _lastReceived = 0;
	mtpRequestId _loadIdsRequest = 0;

	std::unordered_map<PeerId, Entry> _entries;
	rpl::event_stream<PeerId> _changed;

};

using SavedMusicSlice = AbstractSparseIds<
	std::vector<not_null<HistoryItem*>>>;

[[nodiscard]] rpl::producer<SavedMusicSlice> SavedMusicList(
	not_null<PeerData*> peer,
	HistoryItem *aroundId,
	int limit);

} // namespace Data
