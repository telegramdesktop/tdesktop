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

#include <gsl/gsl_assert>
#include <rpl/lifetime.h>
#include <rpl/details/callable.h>

namespace rpl {

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
		typename = std::enable_if_t<
			details::is_callable_v<OnNext, Value>>,
		typename = decltype(std::declval<OnError>()(
			std::declval<Error>())),
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
	class abstract_instance;

	template <typename OnNext, typename OnError, typename OnDone>
	class instance;

	template <typename OnNext, typename OnError, typename OnDone>
	std::shared_ptr<abstract_instance> ConstructInstance(
		OnNext &&next,
		OnError &&error,
		OnDone &&done);

	mutable std::shared_ptr<abstract_instance> _instance;

};


template <typename Value, typename Error>
class consumer<Value, Error>::abstract_instance {
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

};

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
class consumer<Value, Error>::instance
	: public consumer<Value, Error>::abstract_instance {
public:
	template <
		typename OnNextImpl,
		typename OnErrorImpl,
		typename OnDoneImpl>
	instance(
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
inline std::shared_ptr<
	typename consumer<Value, Error>::abstract_instance>
consumer<Value, Error>::ConstructInstance(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) {
	return std::make_shared<instance<
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
inline consumer<Value, Error>::consumer(
	OnNext &&next,
	OnError &&error,
	OnDone &&done) : _instance(ConstructInstance(
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done))) {
}

template <typename Value, typename Error>
inline bool consumer<Value, Error>::put_next(Value &&value) const {
	if (_instance) {
		if (_instance->put_next(std::move(value))) {
			return true;
		}
		_instance = nullptr;
	}
	return false;
}

template <typename Value, typename Error>
inline bool consumer<Value, Error>::put_next_copy(
		const Value &value) const {
	if (_instance) {
		if (_instance->put_next_copy(value)) {
			return true;
		}
		_instance = nullptr;
	}
	return false;
}

template <typename Value, typename Error>
inline void consumer<Value, Error>::put_error(Error &&error) const {
	if (_instance) {
		std::exchange(_instance, nullptr)->put_error(
			std::move(error));
	}
}

template <typename Value, typename Error>
inline void consumer<Value, Error>::put_error_copy(
		const Error &error) const {
	if (_instance) {
		std::exchange(_instance, nullptr)->put_error_copy(error);
	}
}

template <typename Value, typename Error>
inline void consumer<Value, Error>::put_done() const {
	if (_instance) {
		std::exchange(_instance, nullptr)->put_done();
	}
}

template <typename Value, typename Error>
inline void consumer<Value, Error>::add_lifetime(lifetime &&lifetime) const {
	if (_instance) {
		_instance->add_lifetime(std::move(lifetime));
	} else {
		lifetime.destroy();
	}
}

template <typename Value, typename Error>
template <typename Type, typename... Args>
inline Type *consumer<Value, Error>::make_state(Args&& ...args) const {
	Expects(_instance != nullptr);
	return _instance->template make_state<Type>(std::forward<Args>(args)...);
}

template <typename Value, typename Error>
inline void consumer<Value, Error>::terminate() const {
	if (_instance) {
		std::exchange(_instance, nullptr)->terminate();
	}
}

template <typename Value, typename Error>
inline void consumer<Value, Error>::abstract_instance::add_lifetime(
		lifetime &&lifetime) {
	if (_terminated) {
		lifetime.destroy();
	} else {
		_lifetime.add(std::move(lifetime));
	}
}

template <typename Value, typename Error>
template <typename Type, typename... Args>
inline Type *consumer<Value, Error>::abstract_instance::make_state(
		Args&& ...args) {
	Expects(!_terminated);
	return _lifetime.make_state<Type>(std::forward<Args>(args)...);
}

template <typename Value, typename Error>
inline void consumer<Value, Error>::abstract_instance::terminate() {
	if (!_terminated) {
		_terminated = true;
		base::take(_lifetime).destroy();
	}
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
bool consumer<Value, Error>::instance<OnNext, OnError, OnDone>::put_next(
		Value &&value) {
	if (this->_terminated) {
		return false;
	}
	auto handler = this->_next;
	details::callable_invoke(std::move(handler), std::move(value));
	return true;
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
bool consumer<Value, Error>::instance<OnNext, OnError, OnDone>::put_next_copy(
		const Value &value) {
	if (this->_terminated) {
		return false;
	}
	auto handler = this->_next;
	details::const_ref_call_invoke(std::move(handler), value);
	return true;
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
void consumer<Value, Error>::instance<OnNext, OnError, OnDone>::put_error(
		Error &&error) {
	if (!this->_terminated) {
		details::callable_invoke(
			std::move(this->_error),
			std::move(error));
		this->terminate();
	}
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
void consumer<Value, Error>::instance<OnNext, OnError, OnDone>::put_error_copy(
		const Error &error) {
	if (!this->_terminated) {
		details::const_ref_call_invoke(
			std::move(this->_error),
			error);
		this->terminate();
	}
}

template <typename Value, typename Error>
template <typename OnNext, typename OnError, typename OnDone>
void consumer<Value, Error>::instance<OnNext, OnError, OnDone>::put_done() {
	if (!this->_terminated) {
		std::move(this->_done)();
		this->terminate();
	}
}

} // namespace rpl
