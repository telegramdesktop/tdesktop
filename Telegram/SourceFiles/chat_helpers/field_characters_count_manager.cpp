/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/field_characters_count_manager.h"

FieldCharsCountManager::FieldCharsCountManager() = default;

void FieldCharsCountManager::setCount(int count) {
	_previous = _current;
	_current = count;
	if (_previous != _current) {
		constexpr auto kMax = 15;
		const auto was = (_previous > kMax);
		const auto now = (_current > kMax);
		if (was != now) {
			_isLimitExceeded = now;
			_limitExceeds.fire({});
		}
	}
}

int FieldCharsCountManager::count() const {
	return _current;
}

bool FieldCharsCountManager::isLimitExceeded() const {
	return _isLimitExceeded;
}

rpl::producer<> FieldCharsCountManager::limitExceeds() const {
	return _limitExceeds.events();
}
