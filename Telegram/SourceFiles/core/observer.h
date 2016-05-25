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

#include "core/vector_of_moveable.h"

namespace Notify {

using ConnectionId = uint32;

// startObservers() must be called after main() started (not in a global variable constructor).
// finishObservers() must be called before main() finished (not in a global variable destructor).
void startObservers();
void finishObservers();

using StartObservedEventCallback = void(*)();
using FinishObservedEventCallback = void(*)();

namespace internal {

using ObservedEvent = uchar;
using StartCallback = void(*)(void*);
using FinishCallback = void(*)(void*);
using UnregisterCallback = void(*)(void*,int connectionIndex);

class BaseObservedEventRegistrator {
public:
	BaseObservedEventRegistrator(void *that
		, StartCallback startCallback
		, FinishCallback finishCallback
		, UnregisterCallback unregisterCallback);

protected:
	inline ObservedEvent event() const {
		return _event;
	}

private:
	ObservedEvent _event;

};

// Handler is one of Function<> instantiations.
template <typename Flags, typename Handler>
struct ObserversList {
	struct Entry {
		Flags flags;
		Handler handler;
	};
	std_::vector_of_moveable<Entry> entries;
	QVector<int> freeIndices;
};

// If no filtering by flags is done, you can use Flags=int and this value.
constexpr int UniversalFlag = 0x01;

} // namespace internal

// Objects of this class should be constructed in global scope.
// startCallback will be called from Notify::startObservers().
// finishCallback will be called from Notify::finishObservers().
template <typename Flags, typename Handler>
class ObservedEventRegistrator : public internal::BaseObservedEventRegistrator {
public:
	ObservedEventRegistrator(StartObservedEventCallback startCallback,
		FinishObservedEventCallback finishCallback) : internal::BaseObservedEventRegistrator(reinterpret_cast<void*>(this),
			ObservedEventRegistrator<Flags, Handler>::start,
			ObservedEventRegistrator<Flags, Handler>::finish,
			ObservedEventRegistrator<Flags, Handler>::unregister)
		, _startCallback(startCallback), _finishCallback(finishCallback) {
	}

	bool started() const {
		return _list != nullptr;
	}

	ConnectionId registerObserver(Flags flags, Handler &&handler) {
		t_assert(started());

		int connectionIndex = doRegisterObserver(flags, std_::forward<Handler>(handler));
		return (static_cast<uint32>(event()) << 24) | static_cast<uint32>(connectionIndex + 1);
	}

	template <typename... Args>
	void notify(Flags flags, Args&&... args) {
		t_assert(started());

		for (auto &entry : _list->entries) {
			if (!entry.handler.isNull() && (flags & entry.flags)) {
				entry.handler.call(std_::forward<Args>(args)...);
			}
		}
	}

private:
	using Self = ObservedEventRegistrator<Flags, Handler>;
	static void start(void *vthat) {
		Self *that = reinterpret_cast<Self*>(vthat);

		t_assert(!that->started());
		if (that->_startCallback) that->_startCallback();
		that->_list = new internal::ObserversList<Flags, Handler>();
	}
	static void finish(void *vthat) {
		Self *that = reinterpret_cast<Self*>(vthat);

		if (that->_finishCallback) that->_finishCallback();
		delete that->_list;
		that->_list = nullptr;
	}
	static void unregister(void *vthat, int connectionIndex) {
		Self *that = reinterpret_cast<Self*>(vthat);

		t_assert(that->started());

		auto &entries(that->_list->entries);
		if (entries.size() <= connectionIndex) return;

		if (entries.size() == connectionIndex + 1) {
			for (entries.pop_back(); !entries.isEmpty() && entries.back().handler.isNull();) {
				entries.pop_back();
			}
		} else {
			entries[connectionIndex].handler = Handler();
			that->_list->freeIndices.push_back(connectionIndex);
		}
	}

	int doRegisterObserver(Flags flags, Handler &&handler) {
		while (!_list->freeIndices.isEmpty()) {
			auto freeIndex = _list->freeIndices.back();
			_list->freeIndices.pop_back();

			if (freeIndex < _list->entries.size()) {
				_list->entries[freeIndex] = { flags, std_::move(handler) };
				return freeIndex;
			}
		}
		_list->entries.push_back({ flags, std_::move(handler) });
		return _list->entries.size() - 1;
	}

	StartObservedEventCallback _startCallback;
	FinishObservedEventCallback _finishCallback;
	internal::ObserversList<Flags, Handler> *_list = nullptr;

};

// If no filtering of notifications by Flags is intended use this class.
template <typename Handler>
class SimpleObservedEventRegistrator {
public:
	SimpleObservedEventRegistrator(StartObservedEventCallback startCallback,
		FinishObservedEventCallback finishCallback) : _implementation(startCallback, finishCallback) {
	}

	bool started() const {
		return _implementation.started();
	}

	ConnectionId registerObserver(Handler &&handler) {
		return _implementation.registerObserver(internal::UniversalFlag, std_::forward<Handler>(handler));
	}

	template <typename... Args>
	void notify(Args&&... args) {
		return _implementation.notify(internal::UniversalFlag, std_::forward<Args>(args)...);
	}

private:
	ObservedEventRegistrator<int, Handler> _implementation;

};

// Each observer type should have observerRegistered(Notify::ConnectionId connection) method.
// Usually it is done by deriving the type from the Notify::Observer base class.
// In destructor it should call Notify::unregisterObserver(connection) for all the connections.

class Observer;
namespace internal {
void observerRegisteredDefault(Observer *observer, ConnectionId connection);
} // namespace internal

void unregisterObserver(ConnectionId connection);

class Observer {
public:
	virtual ~Observer() = 0;

private:
	void observerRegistered(ConnectionId connection);
	friend void internal::observerRegisteredDefault(Observer *observer, ConnectionId connection);

	QVector<ConnectionId> _connections;

};

namespace internal {

template <typename ObserverType, int>
struct ObserverRegisteredGeneric {
	static inline void call(ObserverType *observer, ConnectionId connection) {
		observer->observerRegistered(connection);
	}
};

template <typename ObserverType>
struct ObserverRegisteredGeneric<ObserverType, true> {
	static inline void call(ObserverType *observer, ConnectionId connection) {
		observerRegisteredDefault(observer, connection);
	}
};

} // namespace internal

template <typename ObserverType>
inline void observerRegistered(ObserverType *observer, ConnectionId connection) {
	// For derivatives of the Observer class we call special friend function observerRegistered().
	// For all other classes we call just a member function observerRegistered().
	using ObserverRegistered = internal::ObserverRegisteredGeneric<ObserverType, std_::is_base_of<Observer, ObserverType>::value>;
	ObserverRegistered::call(observer, connection);
}

} // namespace Notify
