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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "observer_peer.h"

#include "base/observer.h"

namespace Notify {
namespace {

using SmallUpdatesList = QVector<PeerUpdate>;
NeverFreedPointer<SmallUpdatesList> SmallUpdates;
using AllUpdatesList = QMap<PeerData*, PeerUpdate>;
NeverFreedPointer<AllUpdatesList> AllUpdates;

void StartCallback() {
	SmallUpdates.createIfNull();
	AllUpdates.createIfNull();
}
void FinishCallback() {
	SmallUpdates.clear();
	AllUpdates.clear();
}

base::Observable<PeerUpdate, PeerUpdatedHandler> PeerUpdatedObservable;

} // namespace

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
	SmallUpdates.createIfNull();
	AllUpdates.createIfNull();

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
	if (!SmallUpdates || !AllUpdates || SmallUpdates->empty()) return;

	auto smallList = base::take(*SmallUpdates);
	auto allList = base::take(*AllUpdates);
	for (auto &update : smallList) {
		PeerUpdated().notify(std::move(update), true);
	}
	for (auto &update : allList) {
		PeerUpdated().notify(std::move(update), true);
	}

	if (SmallUpdates->isEmpty()) {
		std::swap(smallList, *SmallUpdates);
		SmallUpdates->resize(0);
	}
}

base::Observable<PeerUpdate, PeerUpdatedHandler> &PeerUpdated() {
	return PeerUpdatedObservable;
}

} // namespace Notify
