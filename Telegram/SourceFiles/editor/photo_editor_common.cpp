/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_common.h"

namespace Editor {

QImage ImageModified(QImage image, const PhotoModifications &mods) {
	if (!mods) {
		return image;
	}
	QTransform transform;
	if (mods.flipped) {
		transform.scale(-1, 1);
	}
	if (mods.angle) {
		transform.rotate(mods.angle);
	}
	auto newImage = image.transformed(transform);
	if (mods.crop.isValid()) {
		newImage = newImage.copy(mods.crop);
	}
	return newImage;
}

} // namespace Editor
