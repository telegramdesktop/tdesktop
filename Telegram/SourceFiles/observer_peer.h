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
// 0x0000FFFFU for general peer updates (valid for any peer).
// 0xFFFF0000U for specific peer updates (valid for user / chat / channel).

enum class PeerUpdateFlag {
	NameChanged          = 0x00000001U,
	UsernameChanged      = 0x00000002U,
	PhotoChanged         = 0x00000004U,
	AboutChanged         = 0x00000008U,

	UserCanShareContact  = 0x00010000U,
	UserIsContact        = 0x00020000U,
	UserPhoneChanged     = 0x00040000U,

	ChatCanEdit          = 0x00010000U,

	ChannelAmIn          = 0x00010000U,
	ChannelAmEditor      = 0x00020000U,
	ChannelCanEditPhoto  = 0x00040000U,
	ChannelCanAddMembers = 0x00080000U,
};
Q_DECLARE_FLAGS(PeerUpdateFlags, PeerUpdateFlag);
Q_DECLARE_OPERATORS_FOR_FLAGS(PeerUpdateFlags);

struct PeerUpdate {
	PeerUpdate(PeerData *updated = nullptr) : peer(updated) {
	}
	PeerData *peer;
	PeerUpdateFlags flags = 0;

	// NameChanged data
	PeerData::Names oldNames;
	PeerData::NameFirstChars oldNameFirstChars;
};

void peerUpdatedDelayed(const PeerUpdate &update);
void peerUpdatedSendDelayed();

namespace internal {

using PeerUpdateHandler = Function<void, const PeerUpdate&>;
ConnectionId plainRegisterPeerObserver(PeerUpdateFlags events, PeerUpdateHandler &&handler);

} // namespace internal

template <typename ObserverType>
void registerPeerObserver(PeerUpdateFlags events, ObserverType *observer, void (ObserverType::*handler)(const PeerUpdate &)) {
	auto connection = internal::plainRegisterPeerObserver(events, func(observer, handler));
	observerRegistered(observer, connection);
}

} // namespace Notify
