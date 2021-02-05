/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_content.h"

namespace Editor {

PhotoEditorContent::PhotoEditorContent(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<QPixmap> photo)
: RpWidget(parent) {

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto imageSize = [&] {
			const auto originalSize = photo->size() / cIntRetinaFactor();
			if ((originalSize.width() > size.width())
				&& (originalSize.height() > size.height())) {
				return originalSize.scaled(
					size,
					Qt::KeepAspectRatio);
			}
			return originalSize;
		}();
		_imageRect = QRect(
			QPoint(
				(size.width() - imageSize.width()) / 2,
				(size.height() - imageSize.height()) / 2),
			imageSize);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		p.fillRect(clip, Qt::transparent);
		p.drawPixmap(_imageRect, *photo, photo->rect());
	}, lifetime());
}

} // namespace Editor
