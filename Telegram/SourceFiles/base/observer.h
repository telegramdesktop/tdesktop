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
#pragma once

#include <vector>
#include <deque>
#include <rpl/producer.h>
#include "base/type_traits.h"

namespace base {
namespace internal {

using ObservableCallHandlers = base::lambda<void()>;
void RegisterPendingObservable(ObservableCallHandlers *handlers);
void UnregisterActiveObservable(ObservableCallHandlers *handlers);
void UnregisterObservable(ObservableCallHandlers *handlers);

template <typename EventType>
struct SubscriptionHandlerHelper {
	using type = base::lambda<void(parameter_type<EventType>)>;
};

template <>
struct SubscriptionHandlerHelper<void> {
	using type = base::lambda<void()>;
};

template <typename EventType>
using SubscriptionHandler = typename SubscriptionHandlerHelper<EventType>::type;

class BaseObservableData {
};

template <typename EventType, typename Handler>
class CommonObservableData;

template <typename EventType, typename Handler>
class ObservableData;

} // namespace internal

class Subscription {
public:
	Subscription() = default;
	Subscription(const Subscription &) = delete;
	Subscription &operator=(const Subscription &) = delete;
	Subscription(Subscription &&other) : _node(base::take(other._node)), _removeAndDestroyMethod(other._removeAndDestroyMethod) {
	}
	Subscription &operator=(Subscription &&other) {
		qSwap(_node, other._node);
		qSwap(_removeAndDestroyMethod, other._removeAndDestroyMethod);
		return *this;
	}
	explicit operator bool() const {
		return (_node != nullptr);
	}
	void destroy() {
		if (_node) {
			(*_removeAndDestroyMethod)(base::take(_node));
		}
	}
	~Subscription() {
		destroy();
	}

private:
	struct Node {
		Node(const std::shared_ptr<internal::BaseObservableData> &observable)
		: observable(observable) {
		}
		Node *next = nullptr;
		Node *prev = nullptr;
		std::weak_ptr<internal::BaseObservableData> observable;
	};
	using RemoveAndDestroyMethod = void(*)(Node*);
	Subscription(Node *node, RemoveAndDestroyMethod removeAndDestroyMethod)
	: _node(node)
	, _removeAndDestroyMethod(removeAndDestroyMethod) {
	}

	Node *_node = nullptr;
	RemoveAndDestroyMethod _removeAndDestroyMethod;

	template <typename EventType, typename Handler>
	friend class internal::CommonObservableData;

	template <typename EventType, typename Handler>
	friend class internal::ObservableData;

};

namespace internal {

template <typename EventType, typename Handler, bool EventTypeIsSimple>
class BaseObservable;

template <typename EventType, typename Handler>
class CommonObservable {
public:
	Subscription add_subscription(Handler &&handler) {
		if (!_data) {
			_data = std::make_shared<ObservableData<EventType, Handler>>(this);
		}
		return _data->append(std::move(handler));
	}

private:
	std::shared_ptr<ObservableData<EventType, Handler>> _data;

	friend class CommonObservableData<EventType, Handler>;
	friend class BaseObservable<EventType, Handler, base::type_traits<EventType>::is_fast_copy_type::value>;

};

template <typename EventType, typename Handler>
class BaseObservable<EventType, Handler, true> : public internal::CommonObservable<EventType, Handler> {
public:
	void notify(EventType event, bool sync = false) {
		if (this->_data) {
			this->_data->notify(std::move(event), sync);
		}
	}

};

template <typename EventType, typename Handler>
class BaseObservable<EventType, Handler, false> : public internal::CommonObservable<EventType, Handler> {
public:
	void notify(EventType &&event, bool sync = false) {
		if (this->_data) {
			this->_data->notify(std::move(event), sync);
		}
	}
	void notify(const EventType &event, bool sync = false) {
		if (this->_data) {
			auto event_copy = event;
			this->_data->notify(std::move(event_copy), sync);
		}
	}

};

} // namespace internal

namespace internal {

template <typename EventType, typename Handler>
class CommonObservableData : public BaseObservableData {
public:
	CommonObservableData(CommonObservable<EventType, Handler> *observable) : _observable(observable) {
	}

	Subscription append(Handler &&handler) {
		auto node = new Node(_observable->_data, std::move(handler));
		if (_begin) {
			_end->next = node;
			node->prev = _end;
			_end = node;
		} else {
			_begin = _end = node;
		}
		return { _end, &CommonObservableData::removeAndDestroyNode };
	}

	bool empty() const {
		return !_begin;
	}

private:
	struct Node : public Subscription::Node {
		Node(
			const std::shared_ptr<BaseObservableData> &observer,
			Handler &&handler)
		: Subscription::Node(observer)
		, handler(std::move(handler)) {
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

	static void removeAndDestroyNode(Subscription::Node *node) {
		if (const auto that = node->observable.lock()) {
			static_cast<CommonObservableData*>(that.get())->remove(node);
		}
		delete static_cast<Node*>(node);
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
	}

	bool destroyMeIfEmpty() const {
		if (empty()) {
			_observable->_data.reset();
			return true;
		}
		return false;
	}

	CommonObservable<EventType, Handler> *_observable = nullptr;
	Node *_begin = nullptr;
	Node *_current = nullptr;
	Node *_end = nullptr;
	ObservableCallHandlers _callHandlers;

	friend class ObservableData<EventType, Handler>;

};

template <typename EventType, typename Handler>
class ObservableData : public CommonObservableData<EventType, Handler> {
public:
	using CommonObservableData<EventType, Handler>::CommonObservableData;

	void notify(EventType &&event, bool sync) {
		if (_handling) {
			sync = false;
		}
		if (sync) {
			_events.push_back(std::move(event));
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
			_events.push_back(std::move(event));
		}
	}

	~ObservableData() {
		UnregisterObservable(&this->_callHandlers);
	}

private:
	void callHandlers() {
		_handling = true;
		auto events = base::take(_events);
		for (auto &event : events) {
			this->notifyEnumerate([this, &event]() {
				this->_current->handler(event);
			});
			if (this->destroyMeIfEmpty()) {
				return;
			}
		}
		_handling = false;
		UnregisterActiveObservable(&this->_callHandlers);
	}

	std::deque<EventType> _events;
	bool _handling = false;

};

template <class Handler>
class ObservableData<void, Handler> : public CommonObservableData<void, Handler> {
public:
	using CommonObservableData<void, Handler>::CommonObservableData;

	void notify(bool sync) {
		if (_handling) {
			sync = false;
		}
		if (sync) {
			++_eventsCount;
			callHandlers();
		} else {
			if (!this->_callHandlers) {
				this->_callHandlers = [this]() {
					callHandlers();
				};
			}
			if (!_eventsCount) {
				RegisterPendingObservable(&this->_callHandlers);
			}
			++_eventsCount;
		}
	}

	~ObservableData() {
		UnregisterObservable(&this->_callHandlers);
	}

private:
	void callHandlers() {
		_handling = true;
		auto eventsCount = base::take(_eventsCount);
		for (int i = 0; i != eventsCount; ++i) {
			this->notifyEnumerate([this]() {
				this->_current->handler();
			});
			if (this->destroyMeIfEmpty()) {
				return;
			}
		}
		_handling = false;
		UnregisterActiveObservable(&this->_callHandlers);
	}

	int _eventsCount = 0;
	bool _handling = false;

};

template <typename Handler>
class BaseObservable<void, Handler, base::type_traits<void>::is_fast_copy_type::value> : public internal::CommonObservable<void, Handler> {
public:
	void notify(bool sync = false) {
		if (this->_data) {
			this->_data->notify(sync);
		}
	}

};

} // namespace internal

template <typename EventType, typename Handler = internal::SubscriptionHandler<EventType>>
class Observable : public internal::BaseObservable<EventType, Handler, base::type_traits<EventType>::is_fast_copy_type::value> {
public:
	Observable() = default;
	Observable(const Observable &other) = delete;
	Observable(Observable &&other) = default;
	Observable &operator=(const Observable &other) = delete;
	Observable &operator=(Observable &&other) = default;

};

template <typename Type>
class Variable {
public:
	Variable(parameter_type<Type> startValue = Type()) : _value(startValue) {
	}
	Variable(Variable &&other) = default;
	Variable &operator=(Variable &&other) = default;

	parameter_type<Type> value() const {
		return _value;
	}

	void setForced(parameter_type<Type> newValue, bool sync = false) {
		_value = newValue;
		changed().notify(_value, sync);
	}

	void set(parameter_type<Type> newValue, bool sync = false) {
		if (_value != newValue) {
			setForced(newValue, sync);
		}
	}

	template <typename Callback>
	void process(Callback callback, bool sync = false) {
		callback(_value);
		changed().notify(_value, sync);
	}

	Observable<Type> &changed() const {
		return _changed;
	}

private:
	Type _value;
	mutable Observable<Type> _changed;

};

class Subscriber {
protected:
	template <typename EventType, typename Handler, typename Lambda>
	int subscribe(base::Observable<EventType, Handler> &observable, Lambda &&handler) {
		_subscriptions.push_back(observable.add_subscription(std::forward<Lambda>(handler)));
		return _subscriptions.size();
	}

	template <typename EventType, typename Handler, typename Lambda>
	int subscribe(base::Observable<EventType, Handler> *observable, Lambda &&handler) {
		return subscribe(*observable, std::forward<Lambda>(handler));
	}

	template <typename Type, typename Lambda>
	int subscribe(const base::Variable<Type> &variable, Lambda &&handler) {
		return subscribe(variable.changed(), std::forward<Lambda>(handler));
	}

	template <typename Type, typename Lambda>
	int subscribe(const base::Variable<Type> *variable, Lambda &&handler) {
		return subscribe(variable->changed(), std::forward<Lambda>(handler));
	}

	void unsubscribe(int index) {
		if (!index) return;
		auto count = static_cast<int>(_subscriptions.size());
		Assert(index > 0 && index <= count);
		_subscriptions[index - 1].destroy();
		if (index == count) {
			while (index > 0 && !_subscriptions[--index]) {
				_subscriptions.pop_back();
			}
		}
	}

	~Subscriber() {
		auto subscriptions = base::take(_subscriptions);
		for (auto &subscription : subscriptions) {
			subscription.destroy();
		}
	}

private:
	std::vector<base::Subscription> _subscriptions;

};

void HandleObservables();

template <
	typename Type,
	typename = std::enable_if_t<!std::is_same_v<Type, void>>>
inline auto ObservableViewer(base::Observable<Type> &observable) {
	return rpl::make_producer<Type>([&observable](
			const auto &consumer) {
		auto lifetime = rpl::lifetime();
		lifetime.make_state<base::Subscription>(
			observable.add_subscription([consumer](auto &&update) {
				consumer.put_next_forward(
					std::forward<decltype(update)>(update));
			}));
		return lifetime;
	});
}

rpl::producer<> ObservableViewer(base::Observable<void> &observable);

} // namespace base
