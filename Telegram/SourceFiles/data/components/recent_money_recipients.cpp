/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_money_recipients.h"

#include "core/version.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kLimit = 48;

} // namespace

RecentMoneyRecipients::RecentMoneyRecipients(
		not_null<Main::Session*> session)
: _session(session) {
}

auto RecentMoneyRecipients::list() const
-> const std::vector<not_null<UserData*>> & {
	_session->local().readSearchSuggestions();
	return _list;
}

rpl::producer<> RecentMoneyRecipients::updates() const {
	return _updates.events();
}

void RecentMoneyRecipients::bump(not_null<UserData*> user) {
	_session->local().readSearchSuggestions();

	if (&user->session() != _session
		|| user->id == _session->userPeerId()
		|| user->isSelf()) {
		return;
	}
	if (!_list.empty() && _list.front()->id == user->id) {
		return;
	}
	if (_list.size() == 1 && _list.front()->id == _session->userPeerId()) {
		_list.clear();
	}
	const auto i = ranges::find(_list, user->id, &PeerData::id);
	if (i == end(_list)) {
		if (int(_list.size()) >= kLimit) {
			_list.pop_back();
		}
		_list.insert(begin(_list), user);
	} else {
		ranges::rotate(begin(_list), i, i + 1);
	}
	_session->local().writeSearchSuggestionsDelayed();
	_updates.fire({});
}

void RecentMoneyRecipients::remove(not_null<UserData*> user) {
	_session->local().readSearchSuggestions();

	const auto i = ranges::find(_list, user->id, &PeerData::id);
	if (i != end(_list)) {
		_list.erase(i);
		_session->local().writeSearchSuggestionsDelayed();
		_updates.fire({});
	}
}

void RecentMoneyRecipients::clear() {
	_session->local().readSearchSuggestions();

	const auto self = _session->user();
	if (_list.size() == 1 && _list.front() == self) {
		return;
	}
	_list = { self };
	_session->local().writeSearchSuggestionsDelayed();
	_updates.fire({});
}

void RecentMoneyRecipients::fillIfEmpty(
		Fn<bool(not_null<UserData*>)> eligible) {
	_session->local().readSearchSuggestions();

	if (!_list.empty()) {
		return;
	}
	const auto append = [&](const std::vector<not_null<PeerData*>> &list) {
		for (const auto &peer : list) {
			const auto user = peer->asUser();
			if (int(_list.size()) == kLimit) {
				return;
			} else if (user
				&& user->id != _session->userPeerId()
				&& !ranges::contains(_list, not_null{ user })
				&& eligible(user)) {
				_list.push_back(user);
			}
		}
	};
	append(_session->recentPeers().list());
	append(_session->topPeers().list());
	if (!_list.empty()) {
		_session->local().writeSearchSuggestionsDelayed();
		_updates.fire({});
	}
}

QByteArray RecentMoneyRecipients::serialize() const {
	_session->local().readSearchSuggestions();

	if (_list.empty()) {
		return {};
	}
	auto size = 2 * sizeof(quint32);
	for (const auto &user : _list) {
		size += Serialize::peerSize(user);
	}
	auto stream = Serialize::ByteArrayWriter(size);
	stream
		<< quint32(AppVersion)
		<< quint32(_list.size());
	for (const auto &user : _list) {
		Serialize::writePeer(stream, user);
	}
	return std::move(stream).result();
}

void RecentMoneyRecipients::applyLocal(QByteArray serialized) {
	_list.clear();
	if (serialized.isEmpty()) {
		return;
	}
	auto stream = Serialize::ByteArrayReader(std::move(serialized));
	auto streamAppVersion = quint32();
	auto count = quint32();
	stream >> streamAppVersion >> count;
	if (!stream.ok() || count > kLimit) {
		return;
	}
	auto list = std::vector<not_null<UserData*>>();
	list.reserve(count);
	auto cleared = false;
	for (auto i = quint32(); i != count; ++i) {
		const auto device = stream.underlying().device();
		const auto position = device->pos();
		auto serializedId = quint64();
		stream >> serializedId;
		if (!stream.ok()) {
			return;
		}
		const auto peerId = DeserializePeerId(serializedId);
		const auto userId = peerToUser(peerId);
		if (!userId
			|| peerFromUser(userId) != peerId
			|| !device->seek(position)) {
			return;
		}
		const auto peer = Serialize::readPeer(
			_session,
			streamAppVersion,
			stream);
		if (!stream.ok() || !peer) {
			return;
		}
		const auto user = peer->asUser();
		if (!user) {
			return;
		}
		if (userId == _session->userId() || user->isSelf()) {
			cleared = true;
		} else if (ranges::find(list, user->id, &PeerData::id) == end(list)) {
			list.push_back(user);
		}
	}
	if (list.empty() && cleared) {
		list.push_back(_session->user());
	}
	_list = std::move(list);
}

} // namespace Data
