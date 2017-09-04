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
#include "rpl/consumer.h"
#include "rpl/lifetime.h"

namespace rpl {

template <typename Value, typename Error = no_error>
class producer {
public:
	using value_type = Value;
	using error_type = Error;

	template <typename Generator, typename = std::enable_if<std::is_convertible<
		decltype(std::declval<Generator>()(std::declval<consumer<Value, Error>>())),
		lifetime
	>::value>>
	producer(Generator &&generator);

	template <
		typename OnNext,
		typename OnError,
		typename OnDone,
		typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
		typename = decltype(std::declval<OnError>()(std::declval<Error>())),
		typename = decltype(std::declval<OnDone>()())>
	lifetime start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) const;

private:
	base::lambda<lifetime(consumer<Value, Error>)> _generator;

};

template <typename Value, typename Error>
template <typename Generator, typename>
producer<Value, Error>::producer(Generator &&generator)
	: _generator(std::forward<Generator>(generator)) {
}

template <typename Value, typename Error>
template <
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename,
	typename,
	typename>
lifetime producer<Value, Error>::start(
		OnNext &&next,
		OnError &&error,
		OnDone &&done) const {
	auto result = consumer<Value, Error>(
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done));
	result.set_lifetime(_generator(result));
	return [result] { result.terminate(); };
}

template <typename Value, typename Error>
inline producer<Value, Error> duplicate(const producer<Value, Error> &producer) {
	return producer;
}

template <
	typename Value,
	typename Error,
	typename Method,
	typename = decltype(std::declval<Method>()(std::declval<producer<Value, Error>>()))>
inline decltype(auto) operator|(producer<Value, Error> &&producer, Method &&method) {
	return std::forward<Method>(method)(std::move(producer));
}

template <typename OnNext>
inline decltype(auto) bind_on_next(OnNext &&handler) {
	return [handler = std::forward<OnNext>(handler)](auto &&existing) mutable {
		using value_type = typename std::decay_t<decltype(existing)>::value_type;
		using error_type = typename std::decay_t<decltype(existing)>::error_type;
		return producer<no_value, error_type>([
			existing = std::move(existing),
			handler = std::forward<OnNext>(handler)
		](consumer<no_value, error_type> consumer) {
			return existing.start([handler = std::decay_t<OnNext>(handler)](
				value_type value) {
				handler(value);
			}, [consumer](error_type error) {
				consumer.put_error(error);
			}, [consumer] {
				consumer.put_done();
			});
		});
	};
}

template <typename OnError>
inline decltype(auto) bind_on_error(OnError &&handler) {
	return [handler = std::forward<OnError>(handler)](auto &&existing) mutable {
		using value_type = typename std::decay_t<decltype(existing)>::value_type;
		using error_type = typename std::decay_t<decltype(existing)>::error_type;
		return producer<value_type, no_error>([
			existing = std::move(existing),
			handler = std::forward<OnError>(handler)
		](consumer<value_type, no_error> consumer) {
			return existing.start([consumer](value_type value) {
				consumer.put_next(value);
			}, [handler = std::decay_t<OnError>(handler)](error_type value) {
				handler(value);
			}, [consumer] {
				consumer.put_done();
			});
		});
	};
}

template <typename OnDone>
inline decltype(auto) bind_on_done(OnDone &&handler) {
	return [handler = std::forward<OnDone>(handler)](auto &&existing) mutable {
		using value_type = typename std::decay_t<decltype(existing)>::value_type;
		using error_type = typename std::decay_t<decltype(existing)>::error_type;
		return producer<value_type, error_type>([
			existing = std::move(existing),
			handler = std::forward<OnDone>(handler)
		](consumer<value_type, error_type> consumer) {
			return existing.start([consumer](value_type value) {
				consumer.put_next(value);
			}, [consumer](error_type value) {
				consumer.put_error(value);
			}, [handler = std::decay_t<OnDone>(handler)] {
				handler();
			});
		});
	};
}

namespace details {

template <typename OnNext>
struct next_holder {
	OnNext next;
};

template <typename OnError>
struct error_holder {
	OnError error;
};

template <typename OnDone>
struct done_holder {
	OnDone done;
};

template <
	typename Value,
	typename Error,
	typename OnNext>
struct producer_with_next {
	producer<Value, Error> producer;
	OnNext next;
};

template <
	typename Value,
	typename Error,
	typename OnError>
struct producer_with_error {
	producer<Value, Error> producer;
	OnError error;
};

template <
	typename Value,
	typename Error,
	typename OnDone>
struct producer_with_done {
	producer<Value, Error> producer;
	OnDone done;
};

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError>
struct producer_with_next_error {
	producer<Value, Error> producer;
	OnNext next;
	OnError error;
};

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnDone>
struct producer_with_next_done {
	producer<Value, Error> producer;
	OnNext next;
	OnDone done;
};

template <
	typename Value,
	typename Error,
	typename OnError,
	typename OnDone>
struct producer_with_error_done {
	producer<Value, Error> producer;
	OnError error;
	OnDone done;
};

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
struct producer_with_next_error_done {
	producer<Value, Error> producer;
	OnNext next;
	OnError error;
	OnDone done;
};

struct lifetime_holder {
	lifetime &alive_while;
};

} // namespace details

template <typename OnNext>
inline details::next_holder<std::decay_t<OnNext>> on_next(OnNext &&handler) {
	return { std::forward<OnNext>(handler) };
}

template <typename OnError>
inline details::error_holder<std::decay_t<OnError>> on_error(OnError &&handler) {
	return { std::forward<OnError>(handler) };
}

template <typename OnDone>
inline details::done_holder<std::decay_t<OnDone>> on_done(OnDone &&handler) {
	return { std::forward<OnDone>(handler) };
}

inline details::lifetime_holder start(lifetime &alive_while) {
	return { alive_while };
}

namespace details {

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>()))>
inline producer_with_next<Value, Error, OnNext> operator|(
		producer<Value, Error> &&producer,
		next_holder<OnNext> &&handler) {
	return { std::move(producer), std::move(handler.next) };
}

template <
	typename Value,
	typename Error,
	typename OnError,
	typename = decltype(std::declval<OnError>()(std::declval<Error>()))>
inline producer_with_error<Value, Error, OnError> operator|(
		producer<Value, Error> &&producer,
		error_holder<OnError> &&handler) {
	return { std::move(producer), std::move(handler.error) };
}

template <
	typename Value,
	typename Error,
	typename OnDone,
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_done<Value, Error, OnDone> operator|(
		producer<Value, Error> &&producer,
		done_holder<OnDone> &&handler) {
	return { std::move(producer), std::move(handler.done) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
	typename = decltype(std::declval<OnError>()(std::declval<Error>()))>
inline producer_with_next_error<Value, Error, OnNext, OnError> operator|(
		producer_with_next<Value, Error, OnNext> &&producer_with_next,
		error_holder<OnError> &&handler) {
	return {
		std::move(producer_with_next.producer),
		std::move(producer_with_next.next),
		std::move(handler.error) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
	typename = decltype(std::declval<OnError>()(std::declval<Error>()))>
inline producer_with_next_error<Value, Error, OnNext, OnError> operator|(
		producer_with_error<Value, Error, OnError> &&producer_with_error,
		next_holder<OnNext> &&handler) {
	return {
		std::move(producer_with_error.producer),
		std::move(handler.next),
		std::move(producer_with_error.error) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnDone,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_next_done<Value, Error, OnNext, OnDone> operator|(
		producer_with_next<Value, Error, OnNext> &&producer_with_next,
		done_holder<OnDone> &&handler) {
	return {
		std::move(producer_with_next.producer),
		std::move(producer_with_next.next),
		std::move(handler.done) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnDone,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_next_done<Value, Error, OnNext, OnDone> operator|(
		producer_with_done<Value, Error, OnDone> &&producer_with_done,
		next_holder<OnNext> &&handler) {
	return {
		std::move(producer_with_done.producer),
		std::move(handler.next),
		std::move(producer_with_done.done) };
}

template <
	typename Value,
	typename Error,
	typename OnError,
	typename OnDone,
	typename = decltype(std::declval<OnError>()(std::declval<Error>())),
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_error_done<Value, Error, OnError, OnDone> operator|(
		producer_with_error<Value, Error, OnError> &&producer_with_error,
		done_holder<OnDone> &&handler) {
	return {
		std::move(producer_with_error.producer),
		std::move(producer_with_error.error),
		std::move(handler.done) };
}

template <
	typename Value,
	typename Error,
	typename OnError,
	typename OnDone,
	typename = decltype(std::declval<OnError>()(std::declval<Error>())),
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_error_done<Value, Error, OnError, OnDone> operator|(
		producer_with_done<Value, Error, OnDone> &&producer_with_done,
		error_holder<OnError> &&handler) {
	return {
		std::move(producer_with_done.producer),
		std::move(handler.error),
		std::move(producer_with_done.done) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
	typename = decltype(std::declval<OnError>()(std::declval<Error>())),
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_next_error_done<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone> operator|(
		producer_with_next_error<
			Value,
			Error,
			OnNext,
			OnError> &&producer_with_next_error,
		done_holder<OnDone> &&handler) {
	return {
		std::move(producer_with_next_error.producer),
		std::move(producer_with_next_error.next),
		std::move(producer_with_next_error.error),
		std::move(handler.done) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
	typename = decltype(std::declval<OnError>()(std::declval<Error>())),
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_next_error_done<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone> operator|(
		producer_with_next_done<
			Value,
			Error,
			OnNext,
			OnDone> &&producer_with_next_done,
		error_holder<OnError> &&handler) {
	return {
		std::move(producer_with_next_done.producer),
		std::move(producer_with_next_done.next),
		std::move(handler.error),
		std::move(producer_with_next_done.done) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename = decltype(std::declval<OnNext>()(std::declval<Value>())),
	typename = decltype(std::declval<OnError>()(std::declval<Error>())),
	typename = decltype(std::declval<OnDone>()())>
inline producer_with_next_error_done<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone> operator|(
		producer_with_error_done<
			Value,
			Error,
			OnError,
			OnDone> &&producer_with_error_done,
		next_holder<OnNext> &&handler) {
	return {
		std::move(producer_with_error_done.producer),
		std::move(handler.next),
		std::move(producer_with_error_done.error),
		std::move(producer_with_error_done.done) };
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
inline void operator|(
		producer_with_next_error_done<
			Value,
			Error,
			OnNext,
			OnError,
			OnDone> &&producer_with_next_error_done,
		lifetime_holder &&lifetime) {
	lifetime.alive_while.add(producer_with_next_error_done.producer.start(
		std::move(producer_with_next_error_done.next),
		std::move(producer_with_next_error_done.error),
		std::move(producer_with_next_error_done.done)));
}

template <typename Value, typename Error>
inline void operator|(
		producer<Value, Error> &&producer,
		lifetime_holder &&start_with_lifetime) {
	return std::move(producer)
		| on_next([](Value) {})
		| on_error([](Error) {})
		| on_done([] {})
		| std::move(start_with_lifetime);
}

template <typename Value, typename Error, typename OnNext>
inline void operator|(
		producer_with_next<Value, Error, OnNext> &&producer_with_next,
		lifetime_holder &&start_with_lifetime) {
	return std::move(producer_with_next)
		| on_error([](Error) {})
		| on_done([] {})
		| std::move(start_with_lifetime);
}

template <typename Value, typename Error, typename OnError>
inline void operator|(
		producer_with_error<Value, Error, OnError> &&producer_with_error,
		lifetime_holder &&start_with_lifetime) {
	return std::move(producer_with_error)
		| on_next([](Value) {})
		| on_done([] {})
		| std::move(start_with_lifetime);
}

template <typename Value, typename Error, typename OnDone>
inline void operator|(
		producer_with_done<Value, Error, OnDone> &&producer_with_done,
		lifetime_holder &&start_with_lifetime) {
	return std::move(producer_with_done)
		| on_next([](Value) {})
		| on_error([](Error) {})
		| std::move(start_with_lifetime);
}

template <typename Value, typename Error, typename OnNext, typename OnError>
inline void operator|(
		producer_with_next_error<
			Value,
			Error,
			OnNext,
			OnError> &&producer_with_next_error,
		lifetime_holder &&start_with_lifetime) {
	return std::move(producer_with_next_error)
		| on_done([] {})
		| std::move(start_with_lifetime);
}

template <typename Value, typename Error, typename OnNext, typename OnDone>
inline void operator|(
		producer_with_next_done<
			Value,
			Error,
			OnNext,
			OnDone> &&producer_with_next_done,
		lifetime_holder &&start_with_lifetime) {
	return std::move(producer_with_next_done)
		| on_error([](Error) {})
		| std::move(start_with_lifetime);
}

template <typename Value, typename Error, typename OnError, typename OnDone>
inline void operator|(
		producer_with_error_done<
			Value,
			Error,
			OnError,
			OnDone> &&producer_with_error_done,
		lifetime_holder &&start_with_lifetime) {
	return std::move(producer_with_error_done)
		| on_next([](Value) {})
		| std::move(start_with_lifetime);
}

} // namespace details
} // namespace rpl
