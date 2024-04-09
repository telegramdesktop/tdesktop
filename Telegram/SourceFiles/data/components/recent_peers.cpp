/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_peers.h"

namespace Data {

RecentPeers::RecentPeers(not_null<Main::Session*> session)
: _session(session) {
}

RecentPeers::~RecentPeers() = default;

} // namespace Data
