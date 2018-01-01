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
#include <deque>

namespace rpl {
namespace details {

template <typename Type>
inline Type take(Type &value) {
	return std::exchange(value, Type{});
}

} // namespace details

class lifetime {
public:
	lifetime() = default;
	lifetime(lifetime &&other);
	lifetime &operator=(lifetime &&other);

	template <typename Destroy, typename = decltype(std::declval<Destroy>()())>
	lifetime(Destroy &&destroy);

	explicit operator bool() const { return !_callbacks.empty(); }

	template <typename Destroy, typename = decltype(std::declval<Destroy>()())>
	void add(Destroy &&destroy);
	void add(lifetime &&other);
	void destroy();

	template <typename Type, typename... Args>
	Type *make_state(Args&& ...args) {
		auto result = new Type(std::forward<Args>(args)...);
		add([result] {
			static_assert(sizeof(Type) > 0, "Can't delete unknown type.");
			delete result;
		});
		return result;
	}

	~lifetime() { destroy(); }

private:
	std::deque<base::lambda_once<void()>> _callbacks;

};

inline lifetime::lifetime(lifetime &&other)
: _callbacks(details::take(other._callbacks)) {
}

inline lifetime &lifetime::operator=(lifetime &&other) {
	std::swap(_callbacks, other._callbacks);
	other.destroy();
	return *this;
}

template <typename Destroy, typename>
inline lifetime::lifetime(Destroy &&destroy) {
	_callbacks.emplace_back(std::forward<Destroy>(destroy));
}

template <typename Destroy, typename>
inline void lifetime::add(Destroy &&destroy) {
	_callbacks.push_front(destroy);
}

inline void lifetime::add(lifetime &&other) {
	auto callbacks = details::take(other._callbacks);
	_callbacks.insert(
		_callbacks.begin(),
		std::make_move_iterator(callbacks.begin()),
		std::make_move_iterator(callbacks.end()));
}

inline void lifetime::destroy() {
	for (auto &callback : details::take(_callbacks)) {
		callback();
	}
}

} // namespace rpl
