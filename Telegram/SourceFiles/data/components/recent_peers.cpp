/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_peers.h"

#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kLimit = 48;

} // namespace

RecentPeers::RecentPeers(not_null<Main::Session*> session)
: _session(session) {
}

RecentPeers::~RecentPeers() = default;

const std::vector<not_null<PeerData*>> &RecentPeers::list() const {
	_session->local().readSearchSuggestions();

	return _list;
}

rpl::producer<> RecentPeers::updates() const {
	return _updates.events();
}

void RecentPeers::remove(not_null<PeerData*> peer) {
	const auto i = ranges::find(_list, peer);
	if (i != end(_list)) {
		_list.erase(i);
		_updates.fire({});
	}
	_session->local().writeSearchSuggestionsDelayed();
}

void RecentPeers::bump(not_null<PeerData*> peer) {
	_session->local().readSearchSuggestions();

	if (!_list.empty() && _list.front() == peer) {
		return;
	}
	auto i = ranges::find(_list, peer);
	if (i == end(_list)) {
		_list.push_back(peer);
		i = end(_list) - 1;
	}
	ranges::rotate(begin(_list), i, i + 1);
	_updates.fire({});

	_session->local().writeSearchSuggestionsDelayed();
}

void RecentPeers::clear() {
	_session->local().readSearchSuggestions();

	_list.clear();
	_updates.fire({});

	_session->local().writeSearchSuggestionsDelayed();
}

QByteArray RecentPeers::serialize() const {
	_session->local().readSearchSuggestions();

	if (_list.empty()) {
		return {};
	}
	auto size = 2 * sizeof(quint32); // AppVersion, count
	const auto count = std::min(int(_list.size()), kLimit);
	auto &&list = _list | ranges::views::take(count);
	for (const auto &peer : list) {
		size += Serialize::peerSize(peer);
	}
	auto stream = Serialize::ByteArrayWriter(size);
	stream
		<< quint32(AppVersion)
		<< quint32(_list.size());
	for (const auto &peer : list) {
		Serialize::writePeer(stream, peer);
	}
	return std::move(stream).result();
}

void RecentPeers::applyLocal(QByteArray serialized) {
	_list.clear();
	if (serialized.isEmpty()) {
		return;
	}
	auto stream = Serialize::ByteArrayReader(serialized);
	auto streamAppVersion = quint32();
	auto count = quint32();
	stream >> streamAppVersion >> count;
	if (!stream.ok()) {
		return;
	}
	_list.reserve(count);
	for (auto i = 0; i != int(count); ++i) {
		const auto peer = Serialize::readPeer(
			_session,
			streamAppVersion,
			stream);
		if (stream.ok() && peer) {
			_list.push_back(peer);
		} else {
			_list.clear();
			return;
		}
	}
}

} // namespace Data
