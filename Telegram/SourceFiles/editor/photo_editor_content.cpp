/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_content.h"

#include "media/view/media_view_pip.h"

namespace Editor {

PhotoEditorContent::PhotoEditorContent(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<QPixmap> photo,
	PhotoModifications modifications)
: RpWidget(parent)
, _modifications(modifications) {

	rpl::combine(
		_modifications.value(),
		sizeValue()
	) | rpl::start_with_next([=](
			const PhotoModifications &mods, const QSize &size) {
		const auto rotatedSize =
			Media::View::FlipSizeByRotation(size, mods.angle);
		const auto imageSize = [&] {
			const auto originalSize = photo->size() / cIntRetinaFactor();
			if ((originalSize.width() > rotatedSize.width())
				|| (originalSize.height() > rotatedSize.height())) {
				return originalSize.scaled(
					rotatedSize,
					Qt::KeepAspectRatio);
			}
			return originalSize;
		}();
		_imageRect = QRect(
			QPoint(-imageSize.width() / 2, -imageSize.height() / 2),
			imageSize);

		_imageMatrix.reset();
		_imageMatrix.translate(size.width() / 2, size.height() / 2);
		if (mods.flipped) {
			_imageMatrix.scale(-1, 1);
		}
		_imageMatrix.rotate(mods.angle);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		p.fillRect(clip, Qt::transparent);

		p.setMatrix(_imageMatrix);

		p.drawPixmap(_imageRect, *photo);
	}, lifetime());
}

void PhotoEditorContent::applyModifications(
		PhotoModifications modifications) {
	_modifications = std::move(modifications);
	update();
}

} // namespace Editor
