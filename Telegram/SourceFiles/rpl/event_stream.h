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

#include "producer.h"
#include "base/algorithm.h"

namespace rpl {

template <typename Value>
class event_stream {
public:
	event_stream();

	void fire(Value value);
	producer<Value, no_error> events();

	~event_stream();

private:
	std::weak_ptr<event_stream*> weak() const {
		return _strong;
	}
	void addConsumer(consumer<Value, no_error> &&consumer) {
		_consumers.push_back(std::move(consumer));
	}
	void removeConsumer(const consumer<Value, no_error> &consumer) {
		auto it = base::find(_consumers, consumer);
		if (it != _consumers.end()) {
			it->terminate();
		}
	}

	std::shared_ptr<event_stream*> _strong;
	std::vector<consumer<Value, no_error>> _consumers;

};

template <typename Value>
event_stream<Value>::event_stream()
	: _strong(std::make_shared<event_stream*>(this)) {
}

template <typename Value>
void event_stream<Value>::fire(Value value) {
	base::push_back_safe_remove_if(_consumers, [&](auto &consumer) {
		return !consumer.putNext(value);
	});
}

template <typename Value>
producer<Value, no_error> event_stream<Value>::events() {
	return producer<Value, no_error>([weak = weak()](consumer<Value, no_error> consumer) {
		if (auto strong = weak.lock()) {
			auto result = [weak, consumer] {
				if (auto strong = weak.lock()) {
					(*strong)->removeConsumer(consumer);
				}
			};
			(*strong)->addConsumer(std::move(consumer));
			return lifetime(std::move(result));
		}
		return lifetime();
	});
}

template <typename Value>
event_stream<Value>::~event_stream() {
	for (auto &consumer : _consumers) {
		consumer.putDone();
	}
}

} // namespace rpl
