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

#include <rpl/producer.h>
#include <rpl/range.h>
#include <rpl/then.h>
#include <rpl/range.h>
#include "base/algorithm.h"
#include "base/assertion.h"
#include "base/index_based_iterator.h"

namespace rpl {

// Currently not thread-safe :(

template <typename Value = empty_value>
class event_stream {
public:
	event_stream();
	event_stream(event_stream &&other);

	template <typename OtherValue>
	void fire_forward(OtherValue &&value) const;
	void fire(Value &&value) const {
		return fire_forward(std::move(value));
	}
	void fire_copy(const Value &value) const {
		return fire_forward(value);
	}
	auto events() const {
		using consumer_type = consumer<Value, no_error>;
		return make_producer<Value>([weak = weak()](
			const consumer_type &consumer) {
			if (auto strong = weak.lock()) {
				auto result = [weak, consumer] {
					if (auto strong = weak.lock()) {
						auto it = base::find(*strong, consumer);
						if (it != strong->end()) {
							it->terminate();
						}
					}
				};
				strong->push_back(std::move(consumer));
				return lifetime(std::move(result));
			}
			return lifetime();
		});
	}
	auto events_starting_with(Value &&value) const {
		return single(std::move(value)) | then(events());
	}
	auto events_starting_with_copy(const Value &value) const {
		return single(value) | then(events());
	}

	~event_stream();

private:
	std::weak_ptr<std::vector<consumer<Value, no_error>>> weak() const;

	mutable std::shared_ptr<std::vector<consumer<Value, no_error>>> _consumers;

};

template <typename Value>
inline event_stream<Value>::event_stream() {
}

template <typename Value>
inline event_stream<Value>::event_stream(event_stream &&other)
: _consumers(base::take(other._consumers)) {
}

template <typename Value>
template <typename OtherValue>
inline void event_stream<Value>::fire_forward(
		OtherValue &&value) const {
	if (!_consumers) {
		return;
	}

	auto &consumers = *_consumers;
	auto begin = base::index_based_begin(consumers);
	auto end = base::index_based_end(consumers);
	if (begin != end) {
		// Copy value for every consumer except the last.
		auto prev = end - 1;
		auto removeFrom = std::remove_if(begin, prev, [&](auto &consumer) {
			return !consumer.put_next_copy(value);
		});

		// Perhaps move value for the last consumer.
		if (prev->put_next_forward(std::forward<OtherValue>(value))) {
			if (removeFrom != prev) {
				*removeFrom++ = std::move(*prev);
			} else {
				++removeFrom;
			}
		}

		if (removeFrom != end) {
			// Move new consumers.
			auto newEnd = base::index_based_end(consumers);
			if (newEnd != end) {
				Assert(newEnd > end);
				while (end != newEnd) {
					*removeFrom++ = *end++;
				}
			}

			// Erase stale consumers.
			consumers.erase(removeFrom.base(), consumers.end());
		}
	}
}

template <typename Value>
inline std::weak_ptr<std::vector<consumer<Value, no_error>>> event_stream<Value>::weak() const {
	if (!_consumers) {
		_consumers = std::make_shared<std::vector<consumer<Value, no_error>>>();
	}
	return _consumers;
}


template <typename Value>
inline event_stream<Value>::~event_stream() {
	if (auto consumers = base::take(_consumers)) {
		for (auto &consumer : *consumers) {
			consumer.put_done();
		}
	}
}

template <typename Value>
inline auto start_to_stream(
		event_stream<Value> &stream,
		lifetime &alive_while) {
	return start_with_next([&stream](auto &&value) {
		stream.fire_forward(std::forward<decltype(value)>(value));
	}, alive_while);
}

namespace details {

class start_spawning_helper {
public:
	start_spawning_helper(lifetime &alive_while)
	: _lifetime(alive_while) {
	}

	template <typename Value, typename Error, typename Generator>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		auto stream = _lifetime.make_state<event_stream<Value>>();
		auto collected = std::vector<Value>();
		{
			auto collecting = stream->events().start(
				[&collected](Value &&value) {
					collected.push_back(std::move(value));
				},
				[](const Error &error) {},
				[] {});
			std::move(initial) | start_to_stream(*stream, _lifetime);
		}
		return vector(std::move(collected))
				| then(stream->events());
	}

private:
	lifetime &_lifetime;

};

} // namespace details

inline auto start_spawning(lifetime &alive_while)
-> details::start_spawning_helper {
	return details::start_spawning_helper(alive_while);
}

} // namespace rpl
