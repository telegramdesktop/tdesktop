/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/session/send_as_peers.h"

#include "data/data_user.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Main {
namespace {

constexpr auto kRequestEach = 30 * crl::time(1000);

} // namespace

SendAsPeers::SendAsPeers(not_null<Session*> session)
: _session(session)
, _onlyMe({ session->user() }) {
}

void SendAsPeers::refresh(not_null<PeerData*> peer) {
	if (!peer->isMegagroup()) {
		return;
	}
	const auto now = crl::now();
	const auto i = _lastRequestTime.find(peer);
	const auto when = (i == end(_lastRequestTime)) ? -1 : i->second;
	if (when >= 0 && now < when + kRequestEach) {
		return;
	}
	_lastRequestTime[peer] = now;
	request(peer);
}

const std::vector<not_null<PeerData*>> &SendAsPeers::list(not_null<PeerData*> peer) {
	const auto i = _lists.find(peer);
	return (i != end(_lists)) ? i->second : _onlyMe;
}

rpl::producer<not_null<PeerData*>> SendAsPeers::updated() const {
	return _updates.events();
}

void SendAsPeers::request(not_null<PeerData*> peer) {
	_session->api().request(MTPchannels_GetSendAs(
		peer->input
	)).done([=](const MTPchannels_SendAsPeers &result) {
		auto list = std::vector<not_null<PeerData*>>();
		auto &owner = _session->data();
		result.match([&](const MTPDchannels_sendAsPeers &data) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			for (const auto &id : data.vpeers().v) {
				if (const auto peer = owner.peerLoaded(peerFromMTP(id))) {
					list.push_back(peer);
				}
			}
		});
		if (list.size() > 1) {
			auto &now = _lists[peer];
			if (now != list) {
				now = std::move(list);
				_updates.fire_copy(peer);
			}
		} else if (const auto i = _lists.find(peer); i != end(_lists)) {
			_lists.erase(i);
			_updates.fire_copy(peer);
		}
	}).send();
}

} // namespace Main
