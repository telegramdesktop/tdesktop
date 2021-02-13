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
	if (mods.paint) {
		Painter p(&image);
		PainterHighQualityEnabler hq(p);

		mods.paint->render(&p, image.rect());
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

bool PhotoModifications::empty() const {
	return !angle && !flipped && !crop.isValid();
}

PhotoModifications::operator bool() const {
	return !empty();
}

PhotoModifications::~PhotoModifications() {
	if (paint && (paint.use_count() == 1)) {
		paint->deleteLater();
	}
}

} // namespace Editor
