/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Editor {

struct PhotoModifications {
	int angle = 0;
	bool flipped = false;

	[[nodiscard]] bool empty() const {
		return !angle && !flipped;
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

};

[[nodiscard]] QImage ImageModified(
	QImage image,
	const PhotoModifications &mods);

} // namespace Editor
