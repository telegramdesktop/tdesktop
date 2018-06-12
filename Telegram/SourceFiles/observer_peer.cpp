/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
			mergeTo.oldNameFirstLetters = mergeFrom.oldNameFirstLetters;
		}
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

rpl::producer<PeerUpdate> PeerUpdateViewer(
		PeerUpdate::Flags flags) {
	return [=](const auto &consumer) {
		auto lifetime = rpl::lifetime();
		lifetime.make_state<base::Subscription>(
			PeerUpdated().add_subscription({ flags, [=](
					const PeerUpdate &update) {
				consumer.put_next_copy(update);
			}}));
		return lifetime;
	};
}

rpl::producer<PeerUpdate> PeerUpdateViewer(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) {
	return PeerUpdateViewer(
		flags
	) | rpl::filter([=](const PeerUpdate &update) {
		return (update.peer == peer);
	});
}

rpl::producer<PeerUpdate> PeerUpdateValue(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) {
	auto initial = PeerUpdate(peer);
	initial.flags = flags;
	return rpl::single(
		initial
	) | rpl::then(PeerUpdateViewer(peer, flags));
}

} // namespace Notify
