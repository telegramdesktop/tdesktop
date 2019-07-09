/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/algorithm.h"

#include <atomic>

namespace base {

class binary_guard {
public:
	binary_guard() = default;
	binary_guard(binary_guard &&other);
	binary_guard &operator=(binary_guard &&other);
	~binary_guard();

	binary_guard &operator=(std::nullptr_t);

	bool alive() const;
	binary_guard make_guard();

	explicit operator bool() const;

private:
	void destroy();

	std::atomic<bool> *_bothAlive = nullptr;

};

inline binary_guard::binary_guard(binary_guard &&other)
: _bothAlive(base::take(other._bothAlive)) {
}

inline binary_guard &binary_guard::operator=(binary_guard &&other) {
	if (this != &other) {
		destroy();
		_bothAlive = base::take(other._bothAlive);
	}
	return *this;
}

inline binary_guard::~binary_guard() {
	destroy();
}

inline binary_guard &binary_guard::operator=(std::nullptr_t) {
	destroy();
	return *this;
}

inline binary_guard::operator bool() const {
	return alive();
}

inline bool binary_guard::alive() const {
	return _bothAlive && _bothAlive->load();
}

inline void binary_guard::destroy() {
	if (const auto both = base::take(_bothAlive)) {
		auto old = true;
		if (!both->compare_exchange_strong(old, false)) {
			delete both;
		}
	}
}

inline binary_guard binary_guard::make_guard() {
	destroy();

	auto result = binary_guard();
	_bothAlive = result._bothAlive = new std::atomic<bool>(true);
	return result;
}

} // namespace base

namespace crl {

template <typename T, typename Enable>
struct guard_traits;

template <>
struct guard_traits<base::binary_guard, void> {
	static base::binary_guard create(base::binary_guard value) {
		return value;
	}
	static bool check(const base::binary_guard &guard) {
		return guard.alive();
	}

};

} // namespace crl
