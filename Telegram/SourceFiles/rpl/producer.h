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

template <typename Value, typename Error>
class producer {
public:
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
		OnDone &&done);

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
		OnDone &&done) {
	auto result = consumer<Value, Error>(
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done));
	result.setLifetime(_generator(result));
	return [result] { result.terminate(); };
}

} // namespace rpl
