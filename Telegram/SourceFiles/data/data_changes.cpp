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
		Flags flags,
		bool dropScheduled) {
	sendRealtimeNotifications(data, flags);
	if (dropScheduled) {
		const auto i = _updates.find(data);
		if (i != _updates.end()) {
			flags |= i->second;
			_updates.erase(i);
		}
		_stream.fire({ data, flags });
	} else {
		_updates[data] |= flags;
	}
}

template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::sendRealtimeNotifications(
		not_null<DataType*> data,
		Flags flags) {
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
	return _realtimeStreams[details::CountBit(flag)].events();
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

rpl::producer<HistoryUpdate> Changes::realtimeHistoryUpdates(
		HistoryUpdate::Flag flag) const {
	return _historyChanges.realtimeUpdates(flag);
}

void Changes::messageUpdated(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) {
	const auto drop = (flags & MessageUpdate::Flag::Destroyed);
	_messageChanges.updated(item, flags, drop);
	if (!drop) {
		scheduleNotifications();
	}
}

rpl::producer<MessageUpdate> Changes::messageUpdates(
		MessageUpdate::Flags flags) const {
	return _messageChanges.updates(flags);
}

rpl::producer<MessageUpdate> Changes::messageUpdates(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) const {
	return _messageChanges.updates(item, flags);
}

rpl::producer<MessageUpdate> Changes::messageFlagsValue(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) const {
	return _messageChanges.flagsValue(item, flags);
}

rpl::producer<MessageUpdate> Changes::realtimeMessageUpdates(
		MessageUpdate::Flag flag) const {
	return _messageChanges.realtimeUpdates(flag);
}

void Changes::entryUpdated(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) {
	_entryChanges.updated(entry, flags);
	scheduleNotifications();
}

rpl::producer<EntryUpdate> Changes::entryUpdates(
		EntryUpdate::Flags flags) const {
	return _entryChanges.updates(flags);
}

rpl::producer<EntryUpdate> Changes::entryUpdates(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) const {
	return _entryChanges.updates(entry, flags);
}

rpl::producer<EntryUpdate> Changes::entryFlagsValue(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) const {
	return _entryChanges.flagsValue(entry, flags);
}

rpl::producer<EntryUpdate> Changes::realtimeEntryUpdates(
		EntryUpdate::Flag flag) const {
	return _entryChanges.realtimeUpdates(flag);
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
	_messageChanges.sendNotifications();
	_entryChanges.sendNotifications();
}

} // namespace Data
