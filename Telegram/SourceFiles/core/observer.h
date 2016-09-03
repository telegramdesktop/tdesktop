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
		FinishObservedEventCallback finishCallback) : internal::BaseObservedEventRegistrator(static_cast<void*>(this),
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

		auto &entries = _list->entries;
		// This way of iterating (i < entries.size() should be used
		// because some entries can be removed from the end of the
		// entries list while the loop is still running.
		for (int i = 0; i < entries.size(); ++i) {
			auto &entry = entries[i];
			if (!entry.handler.isNull() && (flags & entry.flags)) {
				entry.handler.call(std_::forward<Args>(args)...);
			}
		}
	}

private:
	using Self = ObservedEventRegistrator<Flags, Handler>;
	static void start(void *vthat) {
		Self *that = static_cast<Self*>(vthat);

		t_assert(!that->started());
		if (that->_startCallback) that->_startCallback();
		that->_list = new internal::ObserversList<Flags, Handler>();
	}
	static void finish(void *vthat) {
		Self *that = static_cast<Self*>(vthat);

		if (that->_finishCallback) that->_finishCallback();
		delete that->_list;
		that->_list = nullptr;
	}
	static void unregister(void *vthat, int connectionIndex) {
		Self *that = static_cast<Self*>(vthat);

		t_assert(that->started());

		auto &entries = that->_list->entries;
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

namespace base {
namespace internal {

using ObservableCallHandlers = base::lambda_unique<void()>;
void RegisterPendingObservable(ObservableCallHandlers *handlers);
void UnregisterActiveObservable(ObservableCallHandlers *handlers);
void UnregisterObservable(ObservableCallHandlers *handlers);

template <typename EventType>
struct SubscriptionHandlerHelper {
	using type = base::lambda_unique<void(const EventType &)>;
};

template <>
struct SubscriptionHandlerHelper<void> {
	using type = base::lambda_unique<void()>;
};

template <typename EventType>
using SubscriptionHandler = typename SubscriptionHandlerHelper<EventType>::type;

// Required because QShared/WeakPointer can't point to void.
class BaseObservableData {
};

template <typename EventType>
class CommonObservableData;

template <typename EventType>
class ObservableData;

} // namespace internal

class Subscription {
public:
	Subscription() = default;
	Subscription(const Subscription &) = delete;
	Subscription &operator=(const Subscription &) = delete;
	Subscription(Subscription &&other) : _node(createAndSwap(other._node)), _removeMethod(other._removeMethod) {
	}
	Subscription &operator=(Subscription &&other) {
		qSwap(_node, other._node);
		qSwap(_removeMethod, other._removeMethod);
		return *this;
	}
	void destroy() {
		if (_node) {
			(*_removeMethod)(_node);
			delete _node;
			_node = nullptr;
		}
	}
	~Subscription() {
		destroy();
	}

private:
	struct Node {
		Node(const QSharedPointer<internal::BaseObservableData> &observable) : observable(observable) {
		}
		Node *next = nullptr;
		Node *prev = nullptr;
		QWeakPointer<internal::BaseObservableData> observable;
	};
	using RemoveMethod = void(*)(Node*);
	Subscription(Node *node, RemoveMethod removeMethod) : _node(node), _removeMethod(removeMethod) {
	}

	Node *_node = nullptr;
	RemoveMethod _removeMethod;

	template <typename EventType>
	friend class internal::CommonObservableData;

	template <typename EventType>
	friend class internal::ObservableData;

};

template <typename EventType>
class Observable;

namespace internal {

template <typename EventType>
class CommonObservable {
public:
	using Handler = typename CommonObservableData<EventType>::Handler;

	Subscription subscribe(Handler &&handler) {
		if (!_data) {
			_data = MakeShared<ObservableData<EventType>>(this);
		}
		return _data->append(std_::forward<Handler>(handler));
	}

private:
	QSharedPointer<ObservableData<EventType>> _data;

	friend class CommonObservableData<EventType>;
	friend class Observable<EventType>;

};

} // namespace internal

template <typename EventType>
class Observable : public internal::CommonObservable<EventType> {
public:
	void notify(EventType &&event, bool sync = false) {
		if (this->_data) {
			this->_data->notify(std_::move(event), sync);
		}
	}

};

namespace internal {

template <typename EventType>
class CommonObservableData : public BaseObservableData {
public:
	using Handler = SubscriptionHandler<EventType>;

	CommonObservableData(CommonObservable<EventType> *observable) : _observable(observable) {
	}

	Subscription append(Handler &&handler) {
		auto node = new Node(_observable->_data, std_::forward<Handler>(handler));
		if (_begin) {
			_end->next = node;
			node->prev = _end;
			_end = node;
		} else {
			_begin = _end = node;
		}
		return { _end, &CommonObservableData::destroyNode };
	}

	bool empty() const {
		return !_begin;
	}

private:
	struct Node : public Subscription::Node {
		Node(const QSharedPointer<BaseObservableData> &observer, Handler &&handler) : Subscription::Node(observer), handler(std_::move(handler)) {
		}
		Handler handler;
	};

	void remove(Subscription::Node *node) {
		if (node->prev) {
			node->prev->next = node->next;
		}
		if (node->next) {
			node->next->prev = node->prev;
		}
		if (_begin == node) {
			_begin = static_cast<Node*>(node->next);
		}
		if (_end == node) {
			_end = static_cast<Node*>(node->prev);
		}
		if (_current == node) {
			_current = static_cast<Node*>(node->prev);
		} else if (!_begin) {
			_observable->_data.reset();
		}
	}

	static void destroyNode(Subscription::Node *node) {
		if (auto that = node->observable.toStrongRef()) {
			static_cast<CommonObservableData*>(that.data())->remove(node);
		}
	}

	template <typename CallCurrent>
	void notifyEnumerate(CallCurrent callCurrent) {
		_current = _begin;
		do {
			callCurrent();
			if (_current) {
				_current = static_cast<Node*>(_current->next);
			} else if (_begin) {
				_current = _begin;
			} else {
				break;
			}
		} while (_current);

		if (empty()) {
			_observable->_data.reset();
		}
	}

	CommonObservable<EventType> *_observable = nullptr;
	Node *_begin = nullptr;
	Node *_current = nullptr;
	Node *_end = nullptr;
	ObservableCallHandlers _callHandlers;

	friend class ObservableData<EventType>;

};

template <typename EventType>
class ObservableData : public CommonObservableData<EventType> {
public:
	using CommonObservableData<EventType>::CommonObservableData;

	void notify(EventType &&event, bool sync) {
		if (_handling) {
			sync = false;
		}
		if (sync) {
			_events.push_back(std_::move(event));
			callHandlers();
		} else {
			if (!this->_callHandlers) {
				this->_callHandlers = [this]() {
					callHandlers();
				};
			}
			if (_events.empty()) {
				RegisterPendingObservable(&this->_callHandlers);
			}
			_events.push_back(std_::move(event));
		}
	}

	~ObservableData() {
		UnregisterObservable(&this->_callHandlers);
	}

private:
	void callHandlers() {
		_handling = true;
		auto events = createAndSwap(_events);
		for (auto &event : events) {
			this->notifyEnumerate([this, &event]() {
				this->_current->handler(event);
			});
		}
		_handling = false;
		UnregisterActiveObservable(&this->_callHandlers);
	}

	std_::vector_of_moveable<EventType> _events;
	bool _handling = false;

};

template <>
class ObservableData<void> : public CommonObservableData<void> {
public:
	using CommonObservableData<void>::CommonObservableData;

	void notify(bool sync) {
		if (_handling) {
			sync = false;
		}
		if (sync) {
			++_eventsCount;
			callHandlers();
		} else {
			if (!_callHandlers) {
				_callHandlers = [this]() {
					callHandlers();
				};
			}
			if (!_eventsCount) {
				RegisterPendingObservable(&_callHandlers);
			}
			++_eventsCount;
		}
	}

	~ObservableData() {
		UnregisterObservable(&_callHandlers);
	}

private:
	void callHandlers() {
		_handling = true;
		auto eventsCount = createAndSwap(_eventsCount);
		for (int i = 0; i != eventsCount; ++i) {
			notifyEnumerate([this]() {
				_current->handler();
			});
		}
		_handling = false;
		UnregisterActiveObservable(&_callHandlers);
	}

	int _eventsCount = 0;
	bool _handling = false;

};

} // namespace internal

template <>
class Observable<void> : public internal::CommonObservable<void> {
public:
	void notify(bool sync = false) {
		if (_data) {
			_data->notify(sync);
		}
	}

};

class Subscriber {
protected:
	template <typename EventType, typename Lambda>
	int subscribe(base::Observable<EventType> &observable, Lambda &&handler) {
		_subscriptions.push_back(observable.subscribe(std_::forward<Lambda>(handler)));
		return _subscriptions.size() - 1;
	}

	template <typename EventType, typename Lambda>
	int subscribe(base::Observable<EventType> *observable, Lambda &&handler) {
		return subscribe(*observable, std_::forward<Lambda>(handler));
	}

	void unsubscribe(int index) {
		t_assert(index >= 0 && index < _subscriptions.size());
		_subscriptions[index].destroy();
	}

	~Subscriber() {
		auto subscriptions = createAndSwap(_subscriptions);
		for (auto &subscription : subscriptions) {
			subscription.destroy();
		}
	}

private:
	std_::vector_of_moveable<base::Subscription> _subscriptions;

};

void HandleObservables();

} // namespace base
