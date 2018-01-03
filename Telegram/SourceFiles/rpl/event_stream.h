/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include <rpl/range.h>
#include <rpl/then.h>
#include <rpl/range.h>
#include <algorithm>
#include "base/assertion.h"
#include "base/index_based_iterator.h"

namespace rpl {

// Currently not thread-safe :(

template <typename Value = empty_value>
class event_stream {
public:
	event_stream();
	event_stream(event_stream &&other);
	event_stream &operator=(event_stream &&other);

	template <typename OtherValue>
	void fire_forward(OtherValue &&value) const;
	void fire(Value &&value) const {
		return fire_forward(std::move(value));
	}
	void fire_copy(const Value &value) const {
		return fire_forward(value);
	}
	auto events() const {
		return make_producer<Value>([weak = make_weak()](
				const auto &consumer) {
			if (auto strong = weak.lock()) {
				auto result = [weak, consumer] {
					if (auto strong = weak.lock()) {
						auto it = std::find(
							strong->consumers.begin(),
							strong->consumers.end(),
							consumer);
						if (it != strong->consumers.end()) {
							it->terminate();
						}
					}
				};
				strong->consumers.push_back(std::move(consumer));
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
	struct Data {
		std::vector<consumer<Value, no_error>> consumers;
		int depth = 0;
	};
	std::weak_ptr<Data> make_weak() const;

	mutable std::shared_ptr<Data> _data;

};

template <typename Value>
inline event_stream<Value>::event_stream() {
}

template <typename Value>
inline event_stream<Value>::event_stream(event_stream &&other)
: _data(details::take(other._data)) {
}

template <typename Value>
inline event_stream<Value> &event_stream<Value>::operator=(
		event_stream &&other) {
	_data = details::take(other._data);
	return *this;
}

template <typename Value>
template <typename OtherValue>
inline void event_stream<Value>::fire_forward(
		OtherValue &&value) const {
	auto copy = _data;
	if (!copy) {
		return;
	}

	++copy->depth;
	auto &consumers = copy->consumers;
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
			if (copy->depth == 1) {
				consumers.erase(removeFrom.base(), consumers.end());
			}
		}
	}
	--copy->depth;
}

template <typename Value>
inline auto event_stream<Value>::make_weak() const
-> std::weak_ptr<Data> {
	if (!_data) {
		_data = std::make_shared<Data>();
	}
	return _data;
}


template <typename Value>
inline event_stream<Value>::~event_stream() {
	if (auto data = details::take(_data)) {
		for (auto &consumer : data->consumers) {
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
