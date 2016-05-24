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
#include "core/observer.h"

namespace Notify {
namespace {

UnregisterObserverCallback UnregisterCallbacks[256];

} // namespace

// Observer base interface.
Observer::~Observer() {
	for_const (auto connection, _connections) {
		unregisterObserver(connection);
	}
}

void Observer::observerRegistered(ConnectionId connection) {
	_connections.push_back(connection);
}

void unregisterObserver(ConnectionId connection) {
	auto event = static_cast<ObservedEvent>(connection >> 24);
	auto connectionIndex = int(connection & 0x00FFFFFFU) - 1;
	if (connectionIndex >= 0 && UnregisterCallbacks[event]) {
		UnregisterCallbacks[event](connectionIndex);
	}
}

UnregisterObserverCallbackCreator::UnregisterObserverCallbackCreator(ObservedEvent event, UnregisterObserverCallback callback) {
	UnregisterCallbacks[event] = callback;
}

namespace internal {

void observerRegisteredDefault(Observer *observer, ConnectionId connection) {
	observer->observerRegistered(connection);
}

} // namespace internal

} // namespace Notify
