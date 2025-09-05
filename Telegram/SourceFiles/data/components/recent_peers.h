/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/userpic_view.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Thread;

class RecentPeers final {
public:
	explicit RecentPeers(not_null<Main::Session*> session);
	~RecentPeers();

	[[nodiscard]] const std::vector<not_null<PeerData*>> &list() const;
	[[nodiscard]] rpl::producer<> updates() const;

	void remove(not_null<PeerData*> peer);
	void bump(not_null<PeerData*> peer);
	void clear();

	[[nodiscard]] QByteArray serialize() const;
	void applyLocal(QByteArray serialized);

	[[nodiscard]] auto collectChatOpenHistory() const
		-> std::vector<not_null<Thread*>>;
	void chatOpenPush(not_null<Thread*> thread);
	void chatOpenRemove(not_null<Thread*> thread);
	void chatOpenKeepUserpics(
		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> userpics);

private:
	const not_null<Main::Session*> _session;

	std::vector<not_null<PeerData*>> _list;
	std::vector<not_null<Thread*>> _opens;
	base::flat_map<
		not_null<PeerData*>,
		Ui::PeerUserpicView> _chatOpenUserpicsCache;

	rpl::event_stream<> _updates;

};

} // namespace Data
