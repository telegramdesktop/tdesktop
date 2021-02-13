/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QGraphicsScene>

namespace Editor {

enum class PhotoEditorMode {
	Transform,
	Paint,
};

struct PhotoModifications {
	int angle = 0;
	bool flipped = false;
	QRect crop;
	std::shared_ptr<QGraphicsScene> paint = nullptr;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const;
	~PhotoModifications();

};

[[nodiscard]] QImage ImageModified(
	QImage image,
	const PhotoModifications &mods);

} // namespace Editor
