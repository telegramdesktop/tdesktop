/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_peers.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kLimit = 48;
constexpr auto kMaxRememberedOpenChats = 32;

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
		<< quint32(count);
	for (const auto &peer : list) {
		Serialize::writePeer(stream, peer);
	}
	return std::move(stream).result();
}

void RecentPeers::applyLocal(QByteArray serialized) {
	_list.clear();
	if (serialized.isEmpty()) {
		DEBUG_LOG(("Suggestions: Bad RecentPeers local, empty."));
		return;
	}
	auto stream = Serialize::ByteArrayReader(serialized);
	auto streamAppVersion = quint32();
	auto count = quint32();
	stream >> streamAppVersion >> count;
	if (!stream.ok()) {
		DEBUG_LOG(("Suggestions: Bad RecentPeers local, not ok."));
		return;
	}
	DEBUG_LOG(("Suggestions: "
		"Start RecentPeers read, count: %1, version: %2."
		).arg(count
		).arg(streamAppVersion));
	_list.reserve(count);
	for (auto i = 0; i != int(count); ++i) {
		const auto streamPosition = stream.underlying().device()->pos();
		const auto peer = Serialize::readPeer(
			_session,
			streamAppVersion,
			stream);
		if (stream.ok() && peer) {
			_list.push_back(peer);
		} else {
			_list.clear();
			DEBUG_LOG(("Suggestions: Failed RecentPeers reading %1 / %2."
				).arg(i + 1
				).arg(count));
			DEBUG_LOG(("Failed bytes: %1.").arg(
				QString::fromUtf8(serialized.mid(streamPosition).toHex())));
			return;
		}
	}
	DEBUG_LOG(
		("Suggestions: RecentPeers read OK, count: %1").arg(_list.size()));
}

std::vector<not_null<Thread*>> RecentPeers::collectChatOpenHistory() const {
	_session->local().readSearchSuggestions();
	return _opens;
}

void RecentPeers::chatOpenPush(not_null<Thread*> thread) {
	const auto i = ranges::find(_opens, thread);
	if (i == end(_opens)) {
		while (_opens.size() >= kMaxRememberedOpenChats) {
			_opens.pop_back();
		}
		_opens.insert(begin(_opens), thread);
	} else if (i != begin(_opens)) {
		ranges::rotate(begin(_opens), i, i + 1);
	}
}

void RecentPeers::chatOpenRemove(not_null<Thread*> thread) {
	_opens.erase(ranges::remove(_opens, thread), end(_opens));
}

void RecentPeers::chatOpenKeepUserpics(
		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> userpics) {
	_chatOpenUserpicsCache = std::move(userpics);
}

} // namespace Data
