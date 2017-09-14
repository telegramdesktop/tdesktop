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

#include <mutex>
#include <gsl/gsl_assert>
#include <rpl/lifetime.h>

namespace rpl {
namespace details {

template <typename Arg>
const Arg &const_ref_val();

template <
	typename Func,
	typename Arg,
	typename = decltype(std::declval<Func>()(const_ref_val<Arg>()))>
void const_ref_call_helper(Func &handler, const Arg &value, int) {
	handler(value);
}

template <
	typename Func,
	typename Arg>
void const_ref_call_helper(Func &handler, const Arg &value, double) {
	auto copy = value;
	handler(std::move(copy));
}

} // namespace details

struct no_value {
	no_value() = delete;
};

struct no_error {
	no_error() = delete;
};

struct empty_value {
};

struct empty_error {
};

template <typename Value = empty_value, typename Error = no_error>
class consumer {
public:
	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
		typename = decltype(std::declval<OnError>()(std::declval<Error>())),
		typename = decltype(std::declval<OnDone>()())>
	consumer(
		OnNext &&next,
		OnError &&error,
		OnDone &&done);

	bool put_next(Value &&value) const;
	bool put_next_copy(const Value &value) const;
	bool put_next_forward(Value &&value) const {
		return put_next(std::move(value));
	}
	bool put_next_forward(const Value &value) const {
		return put_next_copy(value);
	}
	void put_error(Error &&error) const;
	void put_error_copy(const Error &error) const;
	void put_error_forward(Error &&error) const {
		return put_error(std::move(error));
	}
	void put_error_forward(const Error &error) const {
		return put_error_copy(error);
	}
	void put_done() const;

	void add_lifetime(lifetime &&lifetime) const;

	template <typename Type, typename... Args>
	Type *make_state(Args&& ...args) const;

	void terminate() const;

	bool operator==(const consumer &other) const {
		return _instance == other._instance;
	}
	bool operator!=(const consumer &other) const {
		return !(*this == other);
	}
	bool operator<(const consumer &other) const {
		return _instance < other._instance;
	}
	bool operator>(const consumer &other) const {
		return other < *this;
	}
	bool operator<=(const consumer &other) const {
		return !(other < *this);
	}
	bool operator>=(const consumer &other) const {
		return !(*this < other);
	}

private:
	class abstract_consumer_instance;

	template <typename OnNext, typename OnError, typename OnDone>
	class consumer_instance;

	template <typename OnNext, typename OnError, typename OnDone>
	std::shared_ptr<abstract_consumer_instance> ConstructInstance(
		OnNext &&next,
		OnError &&error,
		OnDone &&done);

	mutable std::shared_ptr<abstract_consumer_instance> _instance;

};


template <typename Value, typename Error>
class consumer<Value, Error>::abstract_consumer_instance {
public:
	virtual bool put_next(Value &&value) = 0;
	virtual bool put_next_copy(const Value &value) = 0;
	virtual void put_error(Error &&error) = 0;
	virtual void put_error_copy(const Error &error) = 0;
	virtual void put_done() = 0;

	void add_lifetime(lifetime &&lifetime);

	template <typename Type, typename... Args>
	Type *make_state(Args&& ...args);

	void terminate();

protected:
	lifetime _lifetime;
	bool _terminated = false;
	std::mutex _mutex;

};

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
class consumer<Value, Error>::consumer_instance
	: public consumer<Value, Error>::abstract_consumer_instance {
public:
	template <typename OnNextImpl, typename OnErrorImpl, typename OnDoneImpl>
	consumer_instance(
		OnNextImpl &&next,
		OnErrorImpl &&error,
		OnDoneImpl &&done)
		: _next(std::forward<OnNextImpl>(next))
		, _error(std::forward<OnErrorImpl>(error))
		, _done(std::forward<OnDoneImpl>(done)) {
	}

	bool put_next(Value &&value) override;
	bool put_next_copy(const Value &value) override;
	void put_error(Error &&error) override;
	void put_error_copy(const Error &error) override;
	void put_done() override;

private:
	OnNext _next;
	OnError _error;
	OnDone _done;

};

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
std::shared_ptr<typename consumer<Value, Error>::abstract_consumer_instance>
consumer<Value, Error>::ConstructInstance(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) {
	return std::make_shared<consumer_instance<
		std::decay_t<OnNext>,
		std::decay_t<OnError>,
		std::decay_t<OnDone>>>(
			std::forward<OnNext>(next),
			std::forward<OnError>(error),
			std::forward<OnDone>(done));
}

template <typename Value, typename Error>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename,
	typename,
	typename>
consumer<Value, Error>::consumer(
	OnNext &&next,
	OnError &&error,
	OnDone &&done) : _instance(ConstructInstance(
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done))) {
}

template <typename Value, typename Error>
bool consumer<Value, Error>::put_next(Value &&value) const {
	if (_instance) {
		if (_instance->put_next(std::move(value))) {
			return true;
		}
		_instance = nullptr;
	}
	return false;
}

template <typename Value, typename Error>
bool consumer<Value, Error>::put_next_copy(const Value &value) const {
	if (_instance) {
		if (_instance->put_next_copy(value)) {
			return true;
		}
		_instance = nullptr;
	}
	return false;
}

template <typename Value, typename Error>
void consumer<Value, Error>::put_error(Error &&error) const {
	if (_instance) {
		std::exchange(_instance, nullptr)->put_error(std::move(error));
	}
}

template <typename Value, typename Error>
void consumer<Value, Error>::put_error_copy(const Error &error) const {
	if (_instance) {
		std::exchange(_instance, nullptr)->put_error_copy(error);
	}
}

template <typename Value, typename Error>
void consumer<Value, Error>::put_done() const {
	if (_instance) {
		std::exchange(_instance, nullptr)->put_done();
	}
}

template <typename Value, typename Error>
void consumer<Value, Error>::add_lifetime(lifetime &&lifetime) const {
	if (_instance) {
		_instance->add_lifetime(std::move(lifetime));
	} else {
		lifetime.destroy();
	}
}

template <typename Value, typename Error>
template <typename Type, typename... Args>
Type *consumer<Value, Error>::make_state(Args&& ...args) const {
	Expects(_instance != nullptr);
	return _instance->template make_state<Type>(std::forward<Args>(args)...);
}

template <typename Value, typename Error>
void consumer<Value, Error>::terminate() const {
	if (_instance) {
		std::exchange(_instance, nullptr)->terminate();
	}
}

template <typename Value, typename Error>
void consumer<Value, Error>::abstract_consumer_instance::add_lifetime(
		lifetime &&lifetime) {
	std::unique_lock<std::mutex> lock(_mutex);
	if (_terminated) {
		lock.unlock();

		lifetime.destroy();
	} else {
		_lifetime.add(std::move(lifetime));
	}
}

template <typename Value, typename Error>
template <typename Type, typename... Args>
Type *consumer<Value, Error>::abstract_consumer_instance::make_state(
		Args&& ...args) {
	std::unique_lock<std::mutex> lock(_mutex);
	Expects(!_terminated);
	return _lifetime.template make_state<Type>(std::forward<Args>(args)...);
}

template <typename Value, typename Error>
void consumer<Value, Error>::abstract_consumer_instance::terminate() {
	std::unique_lock<std::mutex> lock(_mutex);
	if (!_terminated) {
		_terminated = true;
		auto handler = std::exchange(_lifetime, lifetime());
		lock.unlock();

		handler.destroy();
	}
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
bool consumer<Value, Error>::consumer_instance<OnNext, OnError, OnDone>::put_next(
		Value &&value) {
	std::unique_lock<std::mutex> lock(this->_mutex);
	if (this->_terminated) {
		return false;
	}
	auto handler = this->_next;
	lock.unlock();

	handler(std::move(value));
	return true;
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
bool consumer<Value, Error>::consumer_instance<OnNext, OnError, OnDone>::put_next_copy(
		const Value &value) {
	std::unique_lock<std::mutex> lock(this->_mutex);
	if (this->_terminated) {
		return false;
	}
	auto handler = this->_next;
	lock.unlock();

	details::const_ref_call_helper(handler, value, 0);
	return true;
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
void consumer<Value, Error>::consumer_instance<OnNext, OnError, OnDone>::put_error(
		Error &&error) {
	std::unique_lock<std::mutex> lock(this->_mutex);
	if (!this->_terminated) {
		auto handler = std::move(this->_error);
		lock.unlock();

		handler(std::move(error));
		this->terminate();
	}
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
void consumer<Value, Error>::consumer_instance<OnNext, OnError, OnDone>::put_error_copy(
		const Error &error) {
	std::unique_lock<std::mutex> lock(this->_mutex);
	if (!this->_terminated) {
		auto handler = std::move(this->_error);
		lock.unlock();

		details::const_ref_call_helper(handler, error, 0);
		this->terminate();
	}
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
void consumer<Value, Error>::consumer_instance<OnNext, OnError, OnDone>::put_done() {
	std::unique_lock<std::mutex> lock(this->_mutex);
	if (!this->_terminated) {
		auto handler = std::move(this->_done);
		lock.unlock();

		handler();
		this->terminate();
	}
}

} // namespace rpl
