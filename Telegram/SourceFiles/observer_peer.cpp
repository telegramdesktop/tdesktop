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
namespace {

using internal::PeerUpdateHandler;

using SmallUpdatesList = QVector<PeerUpdate>;
NeverFreedPointer<SmallUpdatesList> SmallUpdates;
using AllUpdatesList = QMap<PeerData*, PeerUpdate>;
NeverFreedPointer<AllUpdatesList> AllUpdates;

void StartCallback() {
	SmallUpdates.makeIfNull();
	AllUpdates.makeIfNull();
}
void FinishCallback() {
	SmallUpdates.clear();
	AllUpdates.clear();
}
ObservedEventRegistrator<PeerUpdate::Flags, PeerUpdateHandler> creator(StartCallback, FinishCallback);

} // namespace

namespace internal {

ConnectionId plainRegisterPeerObserver(PeerUpdate::Flags events, PeerUpdateHandler &&handler) {
	return creator.registerObserver(events, std_::forward<PeerUpdateHandler>(handler));
}

} // namespace internal

void mergePeerUpdate(PeerUpdate &mergeTo, const PeerUpdate &mergeFrom) {
	if (!(mergeTo.flags & PeerUpdate::Flag::NameChanged)) {
		if (mergeFrom.flags & PeerUpdate::Flag::NameChanged) {
			mergeTo.oldNames = mergeFrom.oldNames;
			mergeTo.oldNameFirstChars = mergeFrom.oldNameFirstChars;
		}
	}
	if (mergeFrom.flags & PeerUpdate::Flag::SharedMediaChanged) {
		mergeTo.mediaTypesMask |= mergeFrom.mediaTypesMask;
	}
	mergeTo.flags |= mergeFrom.flags;
}

void peerUpdatedDelayed(const PeerUpdate &update) {
	t_assert(creator.started());

	Global::RefHandleDelayedPeerUpdates().call();

	int existingUpdatesCount = SmallUpdates->size();
	for (int i = 0; i < existingUpdatesCount; ++i) {
		auto &existingUpdate = (*SmallUpdates)[i];
		if (existingUpdate.peer == update.peer) {
			mergePeerUpdate(existingUpdate, update);
			return;
		}
	}
	if (AllUpdates->isEmpty()) {
		if (existingUpdatesCount < 5) {
			SmallUpdates->push_back(update);
		} else {
			AllUpdates->insert(update.peer, update);
		}
	} else {
		auto it = AllUpdates->find(update.peer);
		if (it != AllUpdates->cend()) {
			mergePeerUpdate(it.value(), update);
			return;
		}
		AllUpdates->insert(update.peer, update);
	}
}

void peerUpdatedSendDelayed() {
	if (!creator.started()) return;

	App::emitPeerUpdated();

	if (SmallUpdates->isEmpty()) return;

	auto smallList = createAndSwap(*SmallUpdates);
	auto allList = createAndSwap(*AllUpdates);
	for_const (auto &update, smallList) {
		creator.notify(update.flags, update);
	}
	for_const (auto &update, allList) {
		creator.notify(update.flags, update);
	}
	if (SmallUpdates->isEmpty()) {
		std::swap(smallList, *SmallUpdates);
		SmallUpdates->resize(0);
	}
}

} // namespace Notify
