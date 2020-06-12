/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_changes.h"

#include "main/main_session.h"

namespace Data {

template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::updated(
		not_null<DataType*> data,
		Flags flags) {
	sendRealtimeNotifications(data, flags);
}

template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::sendRealtimeNotifications(
		not_null<DataType*> data,
		Flags flags) {
	auto clearRealtime = false;
	for (auto i = 0; i != kCount; ++i) {
		const auto flag = static_cast<Flag>(1U << i);
		if (flags & flag) {
			_realtimeStreams[i].fire({ data, flags });
		}
	}
}

template <typename DataType, typename UpdateType>
rpl::producer<UpdateType> Changes::Manager<DataType, UpdateType>::updates(
		Flags flags) const {
	return _stream.events(
	) | rpl::filter([=](const UpdateType &update) {
		return (update.flags & flags);
	});
}

template <typename DataType, typename UpdateType>
rpl::producer<UpdateType> Changes::Manager<DataType, UpdateType>::updates(
		not_null<DataType*> data,
		Flags flags) const {
	return _stream.events(
	) | rpl::filter([=](const UpdateType &update) {
		const auto [updateData, updateFlags] = update;
		return (updateData == data) && (updateFlags & flags);
	});
}

template <typename DataType, typename UpdateType>
auto Changes::Manager<DataType, UpdateType>::realtimeUpdates(Flag flag) const
-> rpl::producer<UpdateType> {
	return _realtimeStreams[CountBit(flag)].events();
}

template <typename DataType, typename UpdateType>
rpl::producer<UpdateType> Changes::Manager<DataType, UpdateType>::flagsValue(
		not_null<DataType*> data,
		Flags flags) const {
	return rpl::single(
		UpdateType{ data, flags }
	) | rpl::then(updates(data, flags));
}

template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::sendNotifications() {
	for (const auto [data, flags] : base::take(_updates)) {
		_stream.fire({ data, flags });
	}
}

Changes::Changes(not_null<Main::Session*> session) : _session(session) {
}

Main::Session &Changes::session() const {
	return *_session;
}

void Changes::nameUpdated(
		not_null<PeerData*> peer,
		base::flat_set<QChar> oldFirstLetters) {
	_nameStream.fire({ peer, std::move(oldFirstLetters) });
}

rpl::producer<NameUpdate> Changes::realtimeNameUpdates() const {
	return _nameStream.events();
}

rpl::producer<NameUpdate> Changes::realtimeNameUpdates(
		not_null<PeerData*> peer) const {
	return _nameStream.events() | rpl::filter([=](const NameUpdate &update) {
		return (update.peer == peer);
	});
}

void Changes::peerUpdated(not_null<PeerData*> peer, PeerUpdate::Flags flags) {
	_peerChanges.updated(peer, flags);
	scheduleNotifications();
}

rpl::producer<PeerUpdate> Changes::peerUpdates(
		PeerUpdate::Flags flags) const {
	return _peerChanges.updates(flags);
}

rpl::producer<PeerUpdate> Changes::peerUpdates(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) const {
	return _peerChanges.updates(peer, flags);
}

rpl::producer<PeerUpdate> Changes::peerFlagsValue(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) const {
	return _peerChanges.flagsValue(peer, flags);
}

rpl::producer<PeerUpdate> Changes::realtimePeerUpdates(
		PeerUpdate::Flag flag) const {
	return _peerChanges.realtimeUpdates(flag);
}

void Changes::historyUpdated(
		not_null<History*> history,
		HistoryUpdate::Flags flags) {
	_historyChanges.updated(history, flags);
	scheduleNotifications();
}

rpl::producer<HistoryUpdate> Changes::historyUpdates(
		HistoryUpdate::Flags flags) const {
	return _historyChanges.updates(flags);
}

rpl::producer<HistoryUpdate> Changes::historyUpdates(
		not_null<History*> history,
		HistoryUpdate::Flags flags) const {
	return _historyChanges.updates(history, flags);
}

rpl::producer<HistoryUpdate> Changes::historyFlagsValue(
		not_null<History*> history,
		HistoryUpdate::Flags flags) const {
	return _historyChanges.flagsValue(history, flags);
}

void Changes::scheduleNotifications() {
	if (!_notify) {
		_notify = true;
		crl::on_main(&session(), [=] {
			sendNotifications();
		});
	}
}

void Changes::sendNotifications() {
	if (!_notify) {
		return;
	}
	_notify = false;
	_peerChanges.sendNotifications();
	_historyChanges.sendNotifications();
}

} // namespace Data
