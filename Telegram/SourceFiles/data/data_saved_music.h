/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_abstract_sparse_ids.h"

class PeerData;

namespace Data {

class Session;

class SavedMusic final {
public:
	explicit SavedMusic(not_null<Session*> owner);

	[[nodiscard]] static bool Supported(PeerId peerId);

	[[nodiscard]] bool countKnown(PeerId peerId) const;
	[[nodiscard]] int count(PeerId peerId) const;
	[[nodiscard]] const std::vector<not_null<DocumentData*>> &list(
		PeerId peerId) const;
	void loadMore(PeerId peerId);

	[[nodiscard]] rpl::producer<PeerId> changed() const;

	[[nodiscard]] bool has(not_null<DocumentData*> document) const;
	void save(not_null<DocumentData*> document);
	void remove(not_null<DocumentData*> document);

private:
	struct Entry {
		std::vector<not_null<DocumentData*>> list;
		mtpRequestId requestId = 0;
		int total = -1;
		bool loaded = false;
		bool reloading = false;
	};

	void loadMore(PeerId peerId, bool reload);
	[[nodiscard]] Entry *lookupEntry(PeerId peerId);
	[[nodiscard]] const Entry *lookupEntry(PeerId peerId) const;
	[[nodiscard]] uint64 firstPageHash(const Entry &entry) const;

	const not_null<Session*> _owner;

	std::unordered_map<PeerId, Entry> _entries;
	rpl::event_stream<PeerId> _changed;

};

using SavedMusicSlice = AbstractSparseIds<
	std::vector<not_null<DocumentData*>>>;

[[nodiscard]] rpl::producer<SavedMusicSlice> SavedMusicList(
	not_null<PeerData*> peer,
	DocumentData *aroundId,
	int limit);

} // namespace Data
