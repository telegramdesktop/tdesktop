/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_blocked_peers.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kBlockedFirstSlice = 16;
constexpr auto kBlockedPerPage = 40;

BlockedPeers::Slice TLToSlice(
		const MTPcontacts_Blocked &blocked,
		Data::Session &owner) {
	const auto create = [&](int count, const QVector<MTPPeerBlocked> &list) {
		auto slice = BlockedPeers::Slice();
		slice.total = std::max(count, list.size());
		slice.list.reserve(list.size());
		for (const auto &contact : list) {
			contact.match([&](const MTPDpeerBlocked &data) {
				slice.list.push_back({
					.id = peerFromMTP(data.vpeer_id()),
					.date = data.vdate().v,
				});
			});
		}
		return slice;
	};
	return blocked.match([&](const MTPDcontacts_blockedSlice &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return create(data.vcount().v, data.vblocked().v);
	}, [&](const MTPDcontacts_blocked &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return create(0, data.vblocked().v);
	});
}

} // namespace

BlockedPeers::BlockedPeers(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

bool BlockedPeers::Slice::Item::operator==(const Item &other) const {
	return (id == other.id) && (date == other.date);
}

bool BlockedPeers::Slice::Item::operator!=(const Item &other) const {
	return !(*this == other);
}

bool BlockedPeers::Slice::operator==(const BlockedPeers::Slice &other) const {
	return (total == other.total) && (list == other.list);
}

bool BlockedPeers::Slice::operator!=(const BlockedPeers::Slice &other) const {
	return !(*this == other);
}

void BlockedPeers::block(not_null<PeerData*> peer) {
	if (peer->isBlocked()) {
		_session->changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::IsBlocked);
	} else if (_blockRequests.find(peer) == end(_blockRequests)) {
		const auto requestId = _api.request(MTPcontacts_Block(
			peer->input
		)).done([=](const MTPBool &result) {
			_blockRequests.erase(peer);
			peer->setIsBlocked(true);
			if (_slice) {
				_slice->list.insert(
					_slice->list.begin(),
					{ peer->id, base::unixtime::now() });
				++_slice->total;
				_changes.fire_copy(*_slice);
			}
		}).fail([=](const MTP::Error &error) {
			_blockRequests.erase(peer);
		}).send();

		_blockRequests.emplace(peer, requestId);
	}
}

void BlockedPeers::unblock(not_null<PeerData*> peer, Fn<void()> onDone) {
	if (!peer->isBlocked()) {
		_session->changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::IsBlocked);
		return;
	} else if (_blockRequests.find(peer) != end(_blockRequests)) {
		return;
	}
	const auto requestId = _api.request(MTPcontacts_Unblock(
		peer->input
	)).done([=](const MTPBool &result) {
		_blockRequests.erase(peer);
		peer->setIsBlocked(false);
		if (_slice) {
			auto &list = _slice->list;
			for (auto i = list.begin(); i != list.end(); ++i) {
				if (i->id == peer->id) {
					list.erase(i);
					break;
				}
			}
			if (_slice->total > list.size()) {
				--_slice->total;
			}
			_changes.fire_copy(*_slice);
		}
		if (onDone) {
			onDone();
		}
	}).fail([=](const MTP::Error &error) {
		_blockRequests.erase(peer);
	}).send();
	_blockRequests.emplace(peer, requestId);
}

void BlockedPeers::reload() {
	if (_requestId) {
		return;
	}
	request(0, [=](Slice &&slice) {
		if (!_slice || *_slice != slice) {
			_slice = slice;
			_changes.fire(std::move(slice));
		}
	});
}

auto BlockedPeers::slice() -> rpl::producer<BlockedPeers::Slice> {
	if (!_slice) {
		reload();
	}
	return _slice
		? _changes.events_starting_with_copy(*_slice)
		: (_changes.events() | rpl::type_erased());
}

void BlockedPeers::request(int offset, Fn<void(BlockedPeers::Slice)> onDone) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPcontacts_GetBlocked(
		MTP_int(offset),
		MTP_int(offset ? kBlockedPerPage : kBlockedFirstSlice)
	)).done([=](const MTPcontacts_Blocked &result) {
		_requestId = 0;
		onDone(TLToSlice(result, _session->data()));
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
	}).send();
}

} // namespace Api
