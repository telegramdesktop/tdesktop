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
#pragma once

#include "core/observer.h"

namespace Notify {

// Generic notifications about updates of some PeerData.
// You can subscribe to them by Notify::registerPeerObserver().

enum class PeerUpdateFlag {
	//PeerNameChanged      = 0x0001,

	UserCanShareContact    = 0x1001,

	ChatCanEdit            = 0x2001,

	MegagroupCanEditPhoto  = 0x4001,
	MegagroupCanAddMembers = 0x4002,

	ChannelAmIn            = 0x8001,
};
Q_DECLARE_FLAGS(PeerUpdateFlags, PeerUpdateFlag);
Q_DECLARE_OPERATORS_FOR_FLAGS(PeerUpdateFlags);
struct PeerUpdate {
	PeerData *peer = nullptr;
	PeerUpdateFlags flags = 0;
};

void peerUpdated(const PeerUpdate &update);
void peerUpdatedDelayed(const PeerUpdate &update);
void peerUpdatedSendDelayed();

namespace internal {

using PeerUpdateHandler = Function<void, const PeerUpdate&>;
ConnectionId plainRegisterPeerObserver(PeerUpdateFlags events, PeerUpdateHandler &&handler);

} // namespace internal

template <typename ObserverType>
void registerPeerObserver(PeerUpdateFlags events, ObserverType *observer, void (ObserverType::*handler)(const PeerUpdate &)) {
	auto connection = internal::plainRegisterPeerObserver(events, func(observer, handler));

	// For derivatives of the Observer class we call special friend function observerRegistered().
	// For all other classes we call just a member function observerRegistered().
	using ObserverRegistered = internal::ObserverRegisteredGeneric<ObserverType, std_::is_base_of<Observer, ObserverType>::value>;
	ObserverRegistered::call(observer, connection);
}

} // namespace Notify
