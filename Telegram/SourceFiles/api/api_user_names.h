/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_user_names.h"
#include "mtproto/sender.h"

class ApiWrap;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class Usernames final {
public:
	enum class Error {
		TooMuch,
		Unknown,
	};

	explicit Usernames(not_null<ApiWrap*> api);

	[[nodiscard]] rpl::producer<Data::Usernames> loadUsernames(
		not_null<PeerData*> peer) const;
	[[nodiscard]] rpl::producer<rpl::no_value, Error> toggle(
		not_null<PeerData*> peer,
		const QString &username,
		bool active);
	[[nodiscard]] rpl::producer<> reorder(
		not_null<PeerData*> peer,
		const std::vector<QString> &usernames);

	void requestToCache(not_null<PeerData*> peer);
	[[nodiscard]] Data::Usernames cacheFor(PeerId id);

	static Data::Usernames FromTL(const MTPVector<MTPUsername> &usernames);

private:

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	using Key = PeerId;
	struct Entry final {
		rpl::event_stream<rpl::no_value, Error> done;
		std::vector<QString> usernames;
	};
	base::flat_map<Key, Entry> _toggleRequests;
	base::flat_map<Key, mtpRequestId> _reorderRequests;
	// Used for a seamless display of usernames list.
	std::pair<Key, Data::Usernames> _tinyCache;

};

} // namespace Api
