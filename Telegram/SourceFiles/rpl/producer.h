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

#include "base/lambda.h"
#include <rpl/consumer.h>
#include <rpl/lifetime.h>

namespace rpl {
namespace details {

template <typename Lambda>
class mutable_lambda_wrap {
public:
	mutable_lambda_wrap(Lambda &&lambda)
		: _lambda(std::move(lambda)) {
	}

	template <typename... Args>
	auto operator()(Args&&... args) const {
		return (const_cast<mutable_lambda_wrap*>(this)->_lambda)(
			std::forward<Args>(args)...);
	}

private:
	Lambda _lambda;

};

// Type-erased copyable mutable lambda using base::lambda.
template <typename Function> class mutable_lambda;

template <typename Return, typename ...Args>
class mutable_lambda<Return(Args...)> {
public:

	// Copy / move construct / assign from an arbitrary type.
	template <
		typename Lambda,
		typename = std::enable_if_t<std::is_convertible<
			decltype(std::declval<Lambda>()(
				std::declval<Args>()...)),
			Return
		>::value>>
	mutable_lambda(Lambda other) : _implementation(
		mutable_lambda_wrap<Lambda>(std::move(other))) {
	}

	template <
		typename ...OtherArgs,
		typename = std::enable_if_t<
			(sizeof...(Args) == sizeof...(OtherArgs))>>
	Return operator()(OtherArgs&&... args) {
		return _implementation(std::forward<OtherArgs>(args)...);
	}

private:
	base::lambda<Return(Args...)> _implementation;

};

} // namespace details

template <typename Value = empty_value, typename Error = no_error>
class producer {
public:
	using value_type = Value;
	using error_type = Error;
	using consumer_type = consumer<Value, Error>;

	template <
		typename Generator,
		typename = std::enable_if<std::is_convertible<
			decltype(std::declval<Generator>()(
				std::declval<consumer_type>())),
			lifetime>::value>>
	producer(Generator &&generator);

	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = std::enable_if_t<
			details::is_callable_v<OnNext, Value>
			&& details::is_callable_v<OnError, Error>
			&& details::is_callable_v<OnDone>>>
	lifetime start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) &&;

	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = std::enable_if_t<
			details::is_callable_v<OnNext, Value>
			&& details::is_callable_v<OnError, Error>
			&& details::is_callable_v<OnDone>>>
	lifetime start_copy(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) const &;

	lifetime start_existing(const consumer_type &consumer) &&;

private:
	details::mutable_lambda<
		lifetime(const consumer_type &)> _generator;

};

template <typename Value, typename Error>
template <typename Generator, typename>
inline producer<Value, Error>::producer(Generator &&generator)
: _generator(std::forward<Generator>(generator)) {
}

template <typename Value, typename Error>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename>
inline lifetime producer<Value, Error>::start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) && {
	return std::move(*this).start_existing(consumer<Value, Error>(
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done)));
}

template <typename Value, typename Error>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename>
inline lifetime producer<Value, Error>::start_copy(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) const & {
	auto copy = *this;
	return std::move(copy).start(
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done));
}

template <typename Value, typename Error>
inline lifetime producer<Value, Error>::start_existing(
		const consumer_type &consumer) && {
	consumer.add_lifetime(std::move(_generator)(consumer));
	return [consumer] { consumer.terminate(); };
}

template <typename Value, typename Error>
inline producer<Value, Error> duplicate(
		const producer<Value, Error> &value) {
	return value;
}

template <
	typename Value,
	typename Error,
	typename Method,
	typename = decltype(std::declval<Method>()(
		std::declval<producer<Value, Error>>()))>
inline auto operator|(producer<Value, Error> &&value, Method &&method) {
	return std::forward<Method>(method)(std::move(value));
}

namespace details {

struct lifetime_with_none {
	lifetime &alive_while;
};

template <typename OnNext>
struct lifetime_with_next {
	lifetime &alive_while;
	OnNext next;
};

template <typename OnError>
struct lifetime_with_error {
	lifetime &alive_while;
	OnError error;
};

template <typename OnDone>
struct lifetime_with_done {
	lifetime &alive_while;
	OnDone done;
};

template <typename OnNext, typename OnError>
struct lifetime_with_next_error {
	lifetime &alive_while;
	OnNext next;
	OnError error;
};

template <typename OnError, typename OnDone>
struct lifetime_with_error_done {
	lifetime &alive_while;
	OnError error;
	OnDone done;
};

template <typename OnNext, typename OnDone>
struct lifetime_with_next_done {
	lifetime &alive_while;
	OnNext next;
	OnDone done;
};

template <typename OnNext, typename OnError, typename OnDone>
struct lifetime_with_next_error_done {
	lifetime &alive_while;
	OnNext next;
	OnError error;
	OnDone done;
};

} // namespace details

inline auto start(lifetime &alive_while)
-> details::lifetime_with_none {
	return { alive_while };
}

template <typename OnNext>
inline auto start_with_next(OnNext &&next, lifetime &alive_while)
-> details::lifetime_with_next<std::decay_t<OnNext>> {
	return { alive_while, std::forward<OnNext>(next) };
}

template <typename OnError>
inline auto start_with_error(OnError &&error, lifetime &alive_while)
-> details::lifetime_with_error<std::decay_t<OnError>> {
	return { alive_while, std::forward<OnError>(error) };
}

template <typename OnDone>
inline auto start_with_done(OnDone &&done, lifetime &alive_while)
-> details::lifetime_with_done<std::decay_t<OnDone>> {
	return { alive_while, std::forward<OnDone>(done) };
}

template <typename OnNext, typename OnError>
inline auto start_with_next_error(
	OnNext &&next,
	OnError &&error,
	lifetime &alive_while)
-> details::lifetime_with_next_error<
		std::decay_t<OnNext>,
		std::decay_t<OnError>> {
	return {
		alive_while,
		std::forward<OnNext>(next),
		std::forward<OnError>(error)
	};
}

template <typename OnError, typename OnDone>
inline auto start_with_error_done(
	OnError &&error,
	OnDone &&done,
	lifetime &alive_while)
-> details::lifetime_with_error_done<
		std::decay_t<OnError>,
		std::decay_t<OnDone>> {
	return {
		alive_while,
		std::forward<OnError>(error),
		std::forward<OnDone>(done)
	};
}

template <typename OnNext, typename OnDone>
inline auto start_with_next_done(
	OnNext &&next,
	OnDone &&done,
	lifetime &alive_while)
-> details::lifetime_with_next_done<
		std::decay_t<OnNext>,
		std::decay_t<OnDone>> {
	return {
		alive_while,
		std::forward<OnNext>(next),
		std::forward<OnDone>(done)
	};
}

template <typename OnNext, typename OnError, typename OnDone>
inline auto start_with_next_error_done(
	OnNext &&next,
	OnError &&error,
	OnDone &&done,
	lifetime &alive_while)
-> details::lifetime_with_next_error_done<
		std::decay_t<OnNext>,
		std::decay_t<OnError>,
		std::decay_t<OnDone>> {
	return {
		alive_while,
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done)
	};
}

namespace details {

template <typename Value, typename Error>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_none &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			[](const Value&) {},
			[](const Error&) {},
			[] {}));
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename = std::enable_if_t<is_callable_v<OnNext, Value>>>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_next<OnNext> &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			std::move(lifetime.next),
			[](const Error&) {},
			[] {}));
}

template <
	typename Value,
	typename Error,
	typename OnError,
	typename = std::enable_if_t<is_callable_v<OnError, Error>>>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_error<OnError> &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			[](const Value&) {},
			std::move(lifetime.error),
			[] {}));
}

template <
	typename Value,
	typename Error,
	typename OnDone,
	typename = std::enable_if_t<is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_done<OnDone> &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			[](const Value&) {},
			[](const Error&) {},
			std::move(lifetime.done)));
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnError, Error>>>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_next_error<OnNext, OnError> &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			std::move(lifetime.next),
			std::move(lifetime.error),
			[] {}));
}

template <
	typename Value,
	typename Error,
	typename OnError,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnError, Error> &&
		is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_error_done<OnError, OnDone> &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			[](const Value&) {},
			std::move(lifetime.error),
			std::move(lifetime.done)));
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_next_done<OnNext, OnDone> &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			std::move(lifetime.next),
			[](const Error&) {},
			std::move(lifetime.done)));
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnError, Error> &&
		is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error> &&value,
		lifetime_with_next_error_done<
			OnNext,
			OnError,
			OnDone> &&lifetime) {
	lifetime.alive_while.add(
		std::move(value).start(
			std::move(lifetime.next),
			std::move(lifetime.error),
			std::move(lifetime.done)));
}

} // namespace details
} // namespace rpl
