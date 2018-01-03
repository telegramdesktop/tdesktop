/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
