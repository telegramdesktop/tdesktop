/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "observer_peer.h"

#include "core/observer.h"

namespace Notify {
namespace internal {
namespace {

constexpr ObservedEvent PeerUpdateEvent = 0x01;
ObserversList<PeerUpdateFlags, PeerUpdateHandler> PeerUpdateObservers;

void UnregisterCallback(int connectionIndex) {
	unregisterObserver(PeerUpdateObservers, connectionIndex);
}
UnregisterObserverCallbackCreator creator(PeerUpdateEvent, UnregisterCallback);

QVector<PeerUpdate> SmallPeerUpdates;
QMap<PeerData*, PeerUpdate> AllPeerUpdates;

} // namespace

ConnectionId plainRegisterPeerObserver(PeerUpdateFlags events, PeerUpdateHandler &&handler) {
	auto connectionId = registerObserver(PeerUpdateObservers, events, std_::forward<PeerUpdateHandler>(handler));
	t_assert(connectionId >= 0 && connectionId < 0x01000000);
	return (static_cast<uint32>(PeerUpdateEvent) << 24) | static_cast<uint32>(connectionId + 1);
}

void mergePeerUpdate(PeerUpdate &mergeTo, const PeerUpdate &mergeFrom) {
	mergeTo.flags |= mergeFrom.flags;

	// merge fields used in mergeFrom.flags
}

} // namespace internal

void peerUpdated(const PeerUpdate &update) {
	notifyObservers(internal::PeerUpdateObservers, update.flags, update);
}

void peerUpdatedDelayed(const PeerUpdate &update) {
	int alreadySavedCount = internal::SmallPeerUpdates.size();
	for (int i = 0; i < alreadySavedCount; ++i) {
		if (internal::SmallPeerUpdates.at(i).peer == update.peer) {
			internal::mergePeerUpdate(internal::SmallPeerUpdates[i], update);
			return;
		}
	}
	if (internal::AllPeerUpdates.isEmpty()) {
		if (alreadySavedCount < 5) {
			internal::SmallPeerUpdates.push_back(update);
		} else {
			internal::AllPeerUpdates.insert(update.peer, update);
		}
	} else {
		auto it = internal::AllPeerUpdates.find(update.peer);
		if (it != internal::AllPeerUpdates.cend()) {
			internal::mergePeerUpdate(it.value(), update);
			return;
		}
		internal::AllPeerUpdates.insert(update.peer, update);
	}
}

void peerUpdatedSendDelayed() {
	if (internal::SmallPeerUpdates.isEmpty()) return;

	decltype(internal::SmallPeerUpdates) smallList;
	decltype(internal::AllPeerUpdates) allList;
	std::swap(smallList, internal::SmallPeerUpdates);
	std::swap(allList, internal::AllPeerUpdates);
	for_const (auto &update, smallList) {
		peerUpdated(update);
	}
	for_const (auto &update, allList) {
		peerUpdated(update);
	}
	if (internal::SmallPeerUpdates.isEmpty()) {
		std::swap(smallList, internal::SmallPeerUpdates);
		internal::SmallPeerUpdates.resize(0);
	}
}

} // namespace Notify
