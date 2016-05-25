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

namespace App {
// Temp forward declaration (while all peer updates are not done through observers).
void emitPeerUpdated();
} // namespace App

namespace Notify {
namespace internal {
namespace {

using PeerObserversList = ObserversList<PeerUpdateFlags, PeerUpdateHandler>;
NeverFreedPointer<PeerObserversList> PeerUpdateObservers;

using SmallUpdatesList = QVector<PeerUpdate>;
NeverFreedPointer<SmallUpdatesList> SmallUpdates;
using AllUpdatesList = QMap<PeerData*, PeerUpdate>;
NeverFreedPointer<AllUpdatesList> AllUpdates;

void StartCallback() {
	PeerUpdateObservers.makeIfNull();
	SmallUpdates.makeIfNull();
	AllUpdates.makeIfNull();
}
void FinishCallback() {
	PeerUpdateObservers.clear();
	SmallUpdates.clear();
	AllUpdates.clear();
}
void UnregisterCallback(int connectionIndex) {
	t_assert(!PeerUpdateObservers.isNull());
	unregisterObserver(*PeerUpdateObservers, connectionIndex);
}
ObservedEventRegistrator creator(StartCallback, FinishCallback, UnregisterCallback);

bool Started() {
	return !PeerUpdateObservers.isNull();
}

} // namespace

ConnectionId plainRegisterPeerObserver(PeerUpdateFlags events, PeerUpdateHandler &&handler) {
	t_assert(Started());
	auto connectionId = registerObserver(creator.event(), *PeerUpdateObservers
		, events, std_::forward<PeerUpdateHandler>(handler));
	return connectionId;
}

void mergePeerUpdate(PeerUpdate &mergeTo, const PeerUpdate &mergeFrom) {
	if (!(mergeTo.flags & PeerUpdateFlag::NameChanged)) {
		if (mergeFrom.flags & PeerUpdateFlag::NameChanged) {
			mergeTo.oldNames = mergeFrom.oldNames;
			mergeTo.oldNameFirstChars = mergeFrom.oldNameFirstChars;
		}
	}
	mergeTo.flags |= mergeFrom.flags;
}

} // namespace internal

void peerUpdatedDelayed(const PeerUpdate &update) {
	t_assert(internal::Started());

	int existingUpdatesCount = internal::SmallUpdates->size();
	for (int i = 0; i < existingUpdatesCount; ++i) {
		auto &existingUpdate = (*internal::SmallUpdates)[i];
		if (existingUpdate.peer == update.peer) {
			internal::mergePeerUpdate(existingUpdate, update);
			return;
		}
	}
	if (internal::AllUpdates->isEmpty()) {
		if (existingUpdatesCount < 5) {
			internal::SmallUpdates->push_back(update);
		} else {
			internal::AllUpdates->insert(update.peer, update);
		}
	} else {
		auto it = internal::AllUpdates->find(update.peer);
		if (it != internal::AllUpdates->cend()) {
			internal::mergePeerUpdate(it.value(), update);
			return;
		}
		internal::AllUpdates->insert(update.peer, update);
	}
}

void peerUpdatedSendDelayed() {
	App::emitPeerUpdated();

	t_assert(internal::Started());

	if (internal::SmallUpdates->isEmpty()) return;

	auto smallList = createAndSwap(*internal::SmallUpdates);
	auto allList = createAndSwap(*internal::AllUpdates);
	for_const (auto &update, smallList) {
		notifyObservers(*internal::PeerUpdateObservers, update.flags, update);
	}
	for_const (auto &update, allList) {
		notifyObservers(*internal::PeerUpdateObservers, update.flags, update);
	}
	if (internal::SmallUpdates->isEmpty()) {
		std::swap(smallList, *internal::SmallUpdates);
		internal::SmallUpdates->resize(0);
	}
}

} // namespace Notify
