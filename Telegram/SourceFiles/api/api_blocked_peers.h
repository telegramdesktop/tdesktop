/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class BlockedPeers final {
public:
	struct Slice {
		struct Item {
			PeerId id;
			TimeId date = 0;

			bool operator==(const Item &other) const;
			bool operator!=(const Item &other) const;
		};

		QVector<Item> list;
		int total = 0;

		bool operator==(const Slice &other) const;
		bool operator!=(const Slice &other) const;
	};

	explicit BlockedPeers(not_null<ApiWrap*> api);

	void reload();
	rpl::producer<Slice> slice();
	void request(int offset, Fn<void(Slice)> onDone);

	void block(not_null<PeerData*> peer);
	void unblock(not_null<PeerData*> peer, Fn<void()> onDone = nullptr);

private:
	const not_null<Main::Session*> _session;

	MTP::Sender _api;

	base::flat_map<not_null<PeerData*>, mtpRequestId> _blockRequests;
	mtpRequestId _requestId = 0;
	std::optional<Slice> _slice;
	rpl::event_stream<Slice> _changes;


};

} // namespace Api
