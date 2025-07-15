/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct StarsRating {
	int level = 0;
	int levelStars = 0;
	int currentStars = 0;
	int nextLevelStars = 0;

	explicit operator bool() const {
		return level != 0 || levelStars != 0;
	}

	friend inline bool operator==(StarsRating, StarsRating) = default;
};

} // namespace Data
