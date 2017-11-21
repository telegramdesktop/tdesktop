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
#include <rpl/details/superset_type.h>
#include <rpl/details/callable.h>

#if defined _DEBUG
#define RPL_PRODUCER_TYPE_ERASED_ALWAYS
#endif // _DEBUG

namespace rpl {
namespace details {

template <typename Value, typename Error>
const consumer<Value, Error> &const_ref_consumer();

template <typename Lambda>
class mutable_lambda_wrap {
public:
	mutable_lambda_wrap(Lambda &&lambda)
		: _lambda(std::move(lambda)) {
	}
	mutable_lambda_wrap(const mutable_lambda_wrap &other) = default;
	mutable_lambda_wrap(mutable_lambda_wrap &&other) = default;
	mutable_lambda_wrap &operator=(
		const mutable_lambda_wrap &other) = default;
	mutable_lambda_wrap &operator=(
		mutable_lambda_wrap &&other) = default;

	template <typename... Args>
	auto operator()(Args&&... args) const {
		return (const_cast<mutable_lambda_wrap*>(this)->_lambda)(
			std::forward<Args>(args)...);
	}

private:
	Lambda _lambda;

};

// Type-erased copyable mutable lambda using base::lambda.
template <typename Value, typename Error>
class type_erased_generator final {
public:
	template <typename Handlers>
	using consumer_type = consumer<Value, Error, Handlers>;
	using value_type = Value;
	using error_type = Error;

	type_erased_generator(
		const type_erased_generator &other) = default;
	type_erased_generator(
		type_erased_generator &&other) = default;
	type_erased_generator &operator=(
		const type_erased_generator &other) = default;
	type_erased_generator &operator=(
		type_erased_generator &&other) = default;

	template <
		typename Generator,
		typename = std::enable_if_t<
			std::is_convertible_v<
				decltype(std::declval<Generator>()(
					const_ref_consumer<Value, Error>())),
				lifetime> &&
			!std::is_same_v<
				std::decay_t<Generator>,
				type_erased_generator>>>
	type_erased_generator(Generator other) : _implementation(
		mutable_lambda_wrap<Generator>(std::move(other))) {
	}
	template <
		typename Generator,
		typename = std::enable_if_t<
			std::is_convertible_v<
				decltype(std::declval<Generator>()(
					const_ref_consumer<Value, Error>())),
				lifetime> &&
			!std::is_same_v<
				std::decay_t<Generator>,
				type_erased_generator>>>
	type_erased_generator &operator=(Generator other) {
		_implementation = mutable_lambda_wrap<Generator>(
			std::move(other));
		return *this;
	}

	template <typename Handlers>
	lifetime operator()(const consumer_type<Handlers> &consumer) {
		return _implementation(consumer);
	}

private:
	base::lambda<lifetime(const consumer_type<type_erased_handlers<Value, Error>> &)> _implementation;

};

} // namespace details

template <
	typename Value = empty_value,
	typename Error = no_error,
	typename Generator = details::type_erased_generator<
		Value,
		Error>>
class producer;

template <
	typename Value1,
	typename Value2,
	typename Error1,
	typename Error2,
	typename Generator>
struct superset_type<
		producer<Value1, Error1, Generator>,
		producer<Value2, Error2, Generator>> {
	using type = producer<
		superset_type_t<Value1, Value2>,
		superset_type_t<Error1, Error2>,
		Generator>;
};

template <
	typename Value,
	typename Error,
	typename Generator1,
	typename Generator2>
struct superset_type<
		producer<Value, Error, Generator1>,
		producer<Value, Error, Generator2>> {
	using type = producer<Value, Error>;
};

template <
	typename Value,
	typename Error,
	typename Generator>
struct superset_type<
		producer<Value, Error, Generator>,
		producer<Value, Error, Generator>> {
	using type = producer<Value, Error, Generator>;
};

namespace details {

template <typename Value, typename Error, typename Generator>
class producer_base {
public:
	template <typename Handlers>
	using consumer_type = consumer<Value, Error, Handlers>;
	using value_type = Value;
	using error_type = Error;

	template <
		typename OtherGenerator,
		typename = std::enable_if_t<
			std::is_constructible_v<Generator, OtherGenerator&&>>>
	producer_base(OtherGenerator &&generator);

	producer_base(const producer_base &other) = default;
	producer_base(producer_base &&other) = default;
	producer_base &operator=(const producer_base &other) = default;
	producer_base &operator=(producer_base &&other) = default;

	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = std::enable_if_t<
			is_callable_v<OnNext, Value>
			&& is_callable_v<OnError, Error>
			&& is_callable_v<OnDone>>>
	void start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done,
		lifetime &alive_while) &&;

	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = std::enable_if_t<
			is_callable_v<OnNext, Value>
			&& is_callable_v<OnError, Error>
			&& is_callable_v<OnDone>>>
	[[nodiscard]] lifetime start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) &&;

	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = std::enable_if_t<
			is_callable_v<OnNext, Value>
			&& is_callable_v<OnError, Error>
			&& is_callable_v<OnDone>>>
	void start_copy(
		OnNext &&next,
		OnError &&error,
		OnDone &&done,
		lifetime &alive_while) const &;

	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = std::enable_if_t<
			is_callable_v<OnNext, Value>
			&& is_callable_v<OnError, Error>
			&& is_callable_v<OnDone>>>
	[[nodiscard]] lifetime start_copy(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) const &;

	template <typename Handlers>
	void start_existing(
		const consumer_type<Handlers> &consumer,
		lifetime &alive_while) &&;

	template <typename Handlers>
	[[nodiscard]] lifetime start_existing(
		const consumer_type<Handlers> &consumer) &&;

private:
	Generator _generator;

	template <
		typename OtherValue,
		typename OtherError,
		typename OtherGenerator>
	friend class ::rpl::producer;

};

template <typename Value, typename Error, typename Generator>
template <typename OtherGenerator, typename>
inline producer_base<Value, Error, Generator>::producer_base(
	OtherGenerator &&generator)
: _generator(std::forward<OtherGenerator>(generator)) {
}

template <typename Value, typename Error, typename Generator>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename>
inline void producer_base<Value, Error, Generator>::start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done,
		lifetime &alive_while) && {
	return std::move(*this).start_existing(
		make_consumer<Value, Error>(
			std::forward<OnNext>(next),
			std::forward<OnError>(error),
			std::forward<OnDone>(done)),
		alive_while);
}

template <typename Value, typename Error, typename Generator>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename>
[[nodiscard]] inline lifetime producer_base<Value, Error, Generator>::start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) && {
	auto result = lifetime();
	std::move(*this).start_existing(
		make_consumer<Value, Error>(
			std::forward<OnNext>(next),
			std::forward<OnError>(error),
			std::forward<OnDone>(done)),
		result);
	return result;
}

template <typename Value, typename Error, typename Generator>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename>
inline void producer_base<Value, Error, Generator>::start_copy(
		OnNext &&next,
		OnError &&error,
		OnDone &&done,
		lifetime &alive_while) const & {
	auto copy = *this;
	return std::move(copy).start_existing(
		make_consumer<Value, Error>(
			std::forward<OnNext>(next),
			std::forward<OnError>(error),
			std::forward<OnDone>(done)),
		alive_while);
}

template <typename Value, typename Error, typename Generator>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename>
[[nodiscard]] inline lifetime producer_base<Value, Error, Generator>::start_copy(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) const & {
	auto result = lifetime();
	auto copy = *this;
	std::move(copy).start_existing(
		make_consumer<Value, Error>(
			std::forward<OnNext>(next),
			std::forward<OnError>(error),
			std::forward<OnDone>(done)),
		result);
	return result;
}

template <typename Value, typename Error, typename Generator>
template <typename Handlers>
inline void producer_base<Value, Error, Generator>::start_existing(
		const consumer_type<Handlers> &consumer,
		lifetime &alive_while) && {
	alive_while.add(consumer.terminator());
	consumer.add_lifetime(std::move(_generator)(consumer));
}

template <typename Value, typename Error, typename Generator>
template <typename Handlers>
[[nodiscard]] inline lifetime producer_base<Value, Error, Generator>::start_existing(
		const consumer_type<Handlers> &consumer) && {
	auto result = lifetime();
	std::move(*this).start_existing(consumer, result);
	return result;
}

template <typename Value, typename Error>
using producer_base_type_erased = producer_base<
	Value,
	Error,
	type_erased_generator<Value, Error>>;

} // namespace details

template <typename Value, typename Error, typename Generator>
class producer final
: public details::producer_base<Value, Error, Generator> {
	using parent_type = details::producer_base<
		Value,
		Error,
		Generator>;

public:
	using parent_type::parent_type;

};

template <typename Value, typename Error>
class producer<
	Value,
	Error,
	details::type_erased_generator<Value, Error>> final
: public details::producer_base_type_erased<Value, Error> {
	using parent_type = details::producer_base_type_erased<
		Value,
		Error>;

public:
	using parent_type::parent_type;;

	producer(const producer &other) = default;
	producer(producer &&other) = default;
	producer &operator=(const producer &other) = default;
	producer &operator=(producer &&other) = default;

	template <
		typename Generic,
		typename = std::enable_if_t<!std::is_same_v<
			Generic,
			details::type_erased_generator<Value, Error>>>>
	producer(const details::producer_base<Value, Error, Generic> &other)
		: parent_type(other._generator) {
	}

	template <
		typename Generic,
		typename = std::enable_if_t<!std::is_same_v<
			Generic,
			details::type_erased_generator<Value, Error>>>>
	producer(details::producer_base<Value, Error, Generic> &&other)
		: parent_type(std::move(other._generator)) {
	}

	template <
		typename Generic,
		typename = std::enable_if_t<!std::is_same_v<
			Generic,
			details::type_erased_generator<Value, Error>>>>
	producer &operator=(
			const details::producer_base<Value, Error, Generic> &other) {
		this->_generator = other._generator;
		return *this;
	}

	template <
		typename Generic,
		typename = std::enable_if_t<!std::is_same_v<
			Generic,
			details::type_erased_generator<Value, Error>>>>
	producer &operator=(
			details::producer_base<Value, Error, Generic> &&other) {
		this->_generator = std::move(other._generator);
		return *this;
	}

};

template <
	typename Value = empty_value,
	typename Error = no_error,
	typename Generator,
	typename = std::enable_if_t<
		std::is_convertible_v<
			decltype(std::declval<Generator>()(
				details::const_ref_consumer<Value, Error>())),
			lifetime>>>
inline auto make_producer(Generator &&generator)
#ifdef RPL_PRODUCER_TYPE_ERASED_ALWAYS
-> producer<Value, Error> {
#else // RPL_CONSUMER_TYPE_ERASED_ALWAYS
-> producer<Value, Error, std::decay_t<Generator>> {
#endif // !RPL_CONSUMER_TYPE_ERASED_ALWAYS
	return std::forward<Generator>(generator);
}

template <typename Value, typename Error, typename Generator>
inline producer<Value, Error, Generator> duplicate(
		const producer<Value, Error, Generator> &value) {
	return value;
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename Method,
	typename = decltype(std::declval<Method>()(
		std::declval<producer<Value, Error, Generator>>()))>
inline auto operator|(
		producer<Value, Error, Generator> &&value,
		Method &&method) {
	return std::forward<Method>(method)(std::move(value));
}

namespace details {

struct with_none {
};

struct lifetime_with_none {
	lifetime &alive_while;
};

template <typename OnNext>
struct with_next {
	OnNext next;
};

template <typename OnNext>
struct lifetime_with_next {
	lifetime &alive_while;
	OnNext next;
};

template <typename OnError>
struct with_error {
	OnError error;
};

template <typename OnError>
struct lifetime_with_error {
	lifetime &alive_while;
	OnError error;
};

template <typename OnDone>
struct with_done {
	OnDone done;
};

template <typename OnDone>
struct lifetime_with_done {
	lifetime &alive_while;
	OnDone done;
};

template <typename OnNext, typename OnError>
struct with_next_error {
	OnNext next;
	OnError error;
};

template <typename OnNext, typename OnError>
struct lifetime_with_next_error {
	lifetime &alive_while;
	OnNext next;
	OnError error;
};

template <typename OnError, typename OnDone>
struct with_error_done {
	OnError error;
	OnDone done;
};

template <typename OnError, typename OnDone>
struct lifetime_with_error_done {
	lifetime &alive_while;
	OnError error;
	OnDone done;
};

template <typename OnNext, typename OnDone>
struct with_next_done {
	OnNext next;
	OnDone done;
};

template <typename OnNext, typename OnDone>
struct lifetime_with_next_done {
	lifetime &alive_while;
	OnNext next;
	OnDone done;
};

template <typename OnNext, typename OnError, typename OnDone>
struct with_next_error_done {
	OnNext next;
	OnError error;
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

inline auto start()
-> details::with_none {
	return {};
}

inline auto start(lifetime &alive_while)
-> details::lifetime_with_none {
	return { alive_while };
}

template <typename OnNext>
inline auto start_with_next(OnNext &&next)
-> details::with_next<std::decay_t<OnNext>> {
	return { std::forward<OnNext>(next) };
}

template <typename OnNext>
inline auto start_with_next(OnNext &&next, lifetime &alive_while)
-> details::lifetime_with_next<std::decay_t<OnNext>> {
	return { alive_while, std::forward<OnNext>(next) };
}

template <typename OnError>
inline auto start_with_error(OnError &&error)
-> details::with_error<std::decay_t<OnError>> {
	return { std::forward<OnError>(error) };
}

template <typename OnError>
inline auto start_with_error(OnError &&error, lifetime &alive_while)
-> details::lifetime_with_error<std::decay_t<OnError>> {
	return { alive_while, std::forward<OnError>(error) };
}

template <typename OnDone>
inline auto start_with_done(OnDone &&done)
-> details::with_done<std::decay_t<OnDone>> {
	return { std::forward<OnDone>(done) };
}

template <typename OnDone>
inline auto start_with_done(OnDone &&done, lifetime &alive_while)
-> details::lifetime_with_done<std::decay_t<OnDone>> {
	return { alive_while, std::forward<OnDone>(done) };
}

template <typename OnNext, typename OnError>
inline auto start_with_next_error(
	OnNext &&next,
	OnError &&error)
-> details::with_next_error<
		std::decay_t<OnNext>,
		std::decay_t<OnError>> {
	return {
		std::forward<OnNext>(next),
		std::forward<OnError>(error)
	};
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
	OnDone &&done)
-> details::with_error_done<
		std::decay_t<OnError>,
		std::decay_t<OnDone>> {
	return {
		std::forward<OnError>(error),
		std::forward<OnDone>(done)
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
	OnDone &&done)
-> details::with_next_done<
		std::decay_t<OnNext>,
		std::decay_t<OnDone>> {
	return {
		std::forward<OnNext>(next),
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
	OnDone &&done)
-> details::with_next_error_done<
		std::decay_t<OnNext>,
		std::decay_t<OnError>,
		std::decay_t<OnDone>> {
	return {
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
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

template <typename Value, typename Error, typename Generator>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_none &&handlers) {
	return std::move(value).start(
		[] {},
		[] {},
		[] {});
}

template <typename Value, typename Error, typename Generator>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_none &&handlers) {
	std::move(value).start(
		[] {},
		[] {},
		[] {},
		handlers.alive_while);
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename = std::enable_if_t<is_callable_v<OnNext, Value>>>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_next<OnNext> &&handlers) {
	return std::move(value).start(
		std::move(handlers.next),
		[] {},
		[] {});
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename = std::enable_if_t<is_callable_v<OnNext, Value>>>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_next<OnNext> &&handlers) {
	std::move(value).start(
		std::move(handlers.next),
		[] {},
		[] {},
		handlers.alive_while);
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnError,
	typename = std::enable_if_t<is_callable_v<OnError, Error>>>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_error<OnError> &&handlers) {
	return std::move(value).start(
		[] {},
		std::move(handlers.error),
		[] {});
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnError,
	typename = std::enable_if_t<is_callable_v<OnError, Error>>>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_error<OnError> &&handlers) {
	std::move(value).start(
		[] {},
		std::move(handlers.error),
		[] {},
		handlers.alive_while);
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnDone,
	typename = std::enable_if_t<is_callable_v<OnDone>>>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_done<OnDone> &&handlers) {
	return std::move(value).start(
		[] {},
		[] {},
		std::move(handlers.done));
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnDone,
	typename = std::enable_if_t<is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_done<OnDone> &&handlers) {
	std::move(value).start(
		[] {},
		[] {},
		std::move(handlers.done),
		handlers.alive_while);
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename OnError,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnError, Error>>>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_next_error<OnNext, OnError> &&handlers) {
	return std::move(value).start(
		std::move(handlers.next),
		std::move(handlers.error),
		[] {});
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename OnError,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnError, Error>>>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_next_error<OnNext, OnError> &&handlers) {
	std::move(value).start(
		std::move(handlers.next),
		std::move(handlers.error),
		[] {},
		handlers.alive_while);
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnError,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnError, Error> &&
		is_callable_v<OnDone>>>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_error_done<OnError, OnDone> &&handlers) {
	return std::move(value).start(
		[] {},
		std::move(handlers.error),
		std::move(handlers.done));
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnError,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnError, Error> &&
		is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_error_done<OnError, OnDone> &&handlers) {
	std::move(value).start(
		[] {},
		std::move(handlers.error),
		std::move(handlers.done),
		handlers.alive_while);
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnDone>>>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_next_done<OnNext, OnDone> &&handlers) {
	return std::move(value).start(
		std::move(handlers.next),
		[] {},
		std::move(handlers.done));
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_next_done<OnNext, OnDone> &&handlers) {
	std::move(value).start(
		std::move(handlers.next),
		[] {},
		std::move(handlers.done),
		handlers.alive_while);
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnError, Error> &&
		is_callable_v<OnDone>>>
[[nodiscard]] inline lifetime operator|(
		producer<Value, Error, Generator> &&value,
		with_next_error_done<
			OnNext,
			OnError,
			OnDone> &&handlers) {
	return std::move(value).start(
		std::move(handlers.next),
		std::move(handlers.error),
		std::move(handlers.done));
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename = std::enable_if_t<
		is_callable_v<OnNext, Value> &&
		is_callable_v<OnError, Error> &&
		is_callable_v<OnDone>>>
inline void operator|(
		producer<Value, Error, Generator> &&value,
		lifetime_with_next_error_done<
			OnNext,
			OnError,
			OnDone> &&handlers) {
	std::move(value).start(
		std::move(handlers.next),
		std::move(handlers.error),
		std::move(handlers.done),
		handlers.alive_while);
}

} // namespace details
} // namespace rpl
