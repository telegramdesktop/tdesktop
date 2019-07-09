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
#include <optional>
#include "base/assertion.h"
#include "base/index_based_iterator.h"

namespace rpl {

// Currently not thread-safe :(

template <typename Value = empty_value, typename Error = no_error>
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

	template <typename OtherError>
	void fire_error_forward(OtherError &&error) const;
	void fire_error(Error &&error) const {
		return fire_error_forward(std::move(error));
	}
	void fire_error_copy(const Error &error) const {
		return fire_error_forward(error);
	}

	void fire_done() const;

#if defined _MSC_VER && _MSC_VER >= 1914 && _MSC_VER < 1916
	producer<Value, Error> events() const {
#else // _MSC_VER >= 1914 && _MSC_VER < 1916
	auto events() const {
#endif // _MSC_VER >= 1914 && _MSC_VER < 1916
		return make_producer<Value, Error>([weak = make_weak()](
				const auto &consumer) {
			if (const auto strong = weak.lock()) {
				auto result = [weak, consumer] {
					if (const auto strong = weak.lock()) {
						const auto it = std::find(
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
		return single<Value&&, Error>(std::move(value)) | then(events());
	}
	auto events_starting_with_copy(const Value &value) const {
		return single<const Value&, Error>(value) | then(events());
	}
	bool has_consumers() const {
		return (_data != nullptr) && !_data->consumers.empty();
	}

	~event_stream();

private:
	struct Data {
		std::vector<consumer<Value, Error>> consumers;
		int depth = 0;
	};
	std::weak_ptr<Data> make_weak() const;

	mutable std::shared_ptr<Data> _data;

};

template <typename Value, typename Error>
inline event_stream<Value, Error>::event_stream() {
}

template <typename Value, typename Error>
inline event_stream<Value, Error>::event_stream(event_stream &&other)
: _data(details::take(other._data)) {
}

template <typename Value, typename Error>
inline event_stream<Value, Error> &event_stream<Value, Error>::operator=(
		event_stream &&other) {
	if (this != &other) {
		std::swap(_data, other._data);
		other.fire_done();
	}
	return *this;
}

template <typename Value, typename Error>
template <typename OtherValue>
inline void event_stream<Value, Error>::fire_forward(
		OtherValue &&value) const {
	if (!_data) {
		return;
	}
	const auto copy = _data;
	auto &consumers = copy->consumers;
	if (consumers.empty()) {
		return;
	}

	++copy->depth;
	const auto begin = base::index_based_begin(consumers);
	const auto end = base::index_based_end(consumers);

	// Copy value for every consumer except the last.
	const auto prev = end - 1;
	auto staleFrom = std::remove_if(begin, prev, [&](const auto &consumer) {
		return !consumer.put_next_copy(value);
	});

	// Perhaps move value for the last consumer.
	if (prev->put_next_forward(std::forward<OtherValue>(value))) {
		if (staleFrom != prev) {
			*staleFrom++ = std::move(*prev);
		} else {
			++staleFrom;
		}
	}

	if (staleFrom != end) {
		// Move new consumers.
		const auto newEnd = base::index_based_end(consumers);
		if (newEnd != end) {
			Assert(newEnd > end);
			for (auto i = end; i != newEnd;) {
				*staleFrom++ = *i++;
			}
		}

		// Erase stale consumers.
		if (copy->depth == 1) {
			consumers.erase(staleFrom.base(), consumers.end());
		}
	}
	--copy->depth;
}

template <typename Value, typename Error>
template <typename OtherError>
inline void event_stream<Value, Error>::fire_error_forward(
		OtherError &&error) const {
	if (!_data) {
		return;
	}
	const auto data = std::move(_data);
	const auto &consumers = data->consumers;
	if (consumers.empty()) {
		return;
	}
	const auto begin = base::index_based_begin(consumers);
	const auto end = base::index_based_end(consumers);

	// Copy error for every consumer except the last.
	const auto prev = end - 1;
	std::for_each(begin, prev, [&](const auto &consumer) {
		consumer.put_error_copy(error);
	});

	// Perhaps move error for the last consumer.
	prev->put_error_forward(std::forward<OtherError>(error));

	// Just drop any new consumers.
}

template <typename Value, typename Error>
void event_stream<Value, Error>::fire_done() const {
	if (const auto data = details::take(_data)) {
		for (const auto &consumer : data->consumers) {
			consumer.put_done();
		}
	}
}

template <typename Value, typename Error>
inline auto event_stream<Value, Error>::make_weak() const
-> std::weak_ptr<Data> {
	if (!_data) {
		_data = std::make_shared<Data>();
	}
	return _data;
}

template <typename Value, typename Error>
inline event_stream<Value, Error>::~event_stream() {
	fire_done();
}

template <typename Value, typename Error>
inline auto start_to_stream(
		event_stream<Value, Error> &stream,
		lifetime &alive_while) {
	if constexpr (std::is_same_v<Error, no_error>) {
		return start_with_next_done([&](auto &&value) {
			stream.fire_forward(std::forward<decltype(value)>(value));
		}, [&] {
			stream.fire_done();
		}, alive_while);
	} else {
		return start_with_next_error_done([&](auto &&value) {
			stream.fire_forward(std::forward<decltype(value)>(value));
		}, [&](auto &&error) {
			stream.fire_error_forward(std::forward<decltype(error)>(error));
		}, [&] {
			stream.fire_done();
		}, alive_while);
	}
}

namespace details {

class start_spawning_helper {
public:
	start_spawning_helper(lifetime &alive_while)
	: _lifetime(alive_while) {
	}

	template <typename Value, typename Error, typename Generator>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		auto stream = _lifetime.make_state<event_stream<Value, Error>>();
		auto values = std::vector<Value>();
		if constexpr (std::is_same_v<Error, rpl::no_error>) {
			auto collecting = stream->events().start(
				[&](Value &&value) { values.push_back(std::move(value)); },
				[](const Error &error) {},
				[] {});
			std::move(initial) | start_to_stream(*stream, _lifetime);
			collecting.destroy();

			return vector(std::move(values)) | then(stream->events());
		} else {
			auto maybeError = std::optional<Error>();
			auto collecting = stream->events().start(
				[&](Value && value) { values.push_back(std::move(value)); },
				[&](Error &&error) { maybeError = std::move(error); },
				[] {});
			std::move(initial) | start_to_stream(*stream, _lifetime);
			collecting.destroy();

			if (maybeError.has_value()) {
				return rpl::producer<Value, Error>([
					error = std::move(*maybeError)
				](const auto &consumer) mutable {
					consumer.put_error(std::move(error));
				});
			}
			return rpl::producer<Value, Error>(vector<Value, Error>(
				std::move(values)
			) | then(stream->events()));
		}
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
