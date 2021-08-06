/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_common.h"

#include "editor/scene/scene.h"

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
	auto cropped = mods.crop.isValid()
		? image.copy(mods.crop)
		: image;
	QTransform transform;
	if (mods.flipped) {
		transform.scale(-1, 1);
	}
	if (mods.angle) {
		transform.rotate(mods.angle);
	}
	return cropped.transformed(transform);
}

bool PhotoModifications::empty() const {
	return !angle && !flipped && !crop.isValid() && !paint;
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
