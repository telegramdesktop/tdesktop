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

	bool alive() const;

	binary_guard &operator=(std::nullptr_t);
	explicit operator bool() const;

private:
	void destroy();

	std::atomic<bool> *_bothAlive = nullptr;

	friend std::pair<binary_guard, binary_guard> make_binary_guard();

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

inline binary_guard::~binary_guard() {
	destroy();
}

inline void binary_guard::destroy() {
	if (const auto both = base::take(_bothAlive)) {
		auto old = true;
		if (!both->compare_exchange_strong(old, false)) {
			delete both;
		}
	}
}

inline std::pair<binary_guard, binary_guard> make_binary_guard() {
	auto result = std::pair<binary_guard, binary_guard>();
	result.first._bothAlive
		= result.second._bothAlive
		= new std::atomic<bool>(true);
	return result;
}

} // namespace base
