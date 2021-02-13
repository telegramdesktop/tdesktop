/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_content.h"

#include "editor/editor_crop.h"
#include "editor/editor_paint.h"
#include "media/view/media_view_pip.h"

namespace Editor {

using Media::View::FlipSizeByRotation;
using Media::View::RotatedRect;

PhotoEditorContent::PhotoEditorContent(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<QPixmap> photo,
	PhotoModifications modifications)
: RpWidget(parent)
, _paint(base::make_unique_q<Paint>(this, modifications, photo->size()))
, _crop(base::make_unique_q<Crop>(this, modifications, photo->size()))
, _photo(photo)
, _modifications(modifications) {

	rpl::combine(
		_modifications.value(),
		sizeValue()
	) | rpl::start_with_next([=](
			const PhotoModifications &mods, const QSize &size) {
		if (size.isEmpty()) {
			return;
		}
		const auto imageSize = [&] {
			const auto rotatedSize =
				FlipSizeByRotation(size, mods.angle);
			const auto m = _crop->cropMargins();
			const auto sizeForCrop = rotatedSize
				- QSize(m.left() + m.right(), m.top() + m.bottom());
			const auto originalSize = photo->size();
			if ((originalSize.width() > sizeForCrop.width())
				|| (originalSize.height() > sizeForCrop.height())) {
				return originalSize.scaled(
					sizeForCrop,
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

		const auto geometry = _imageMatrix.mapRect(_imageRect);
		_crop->applyTransform(
			geometry + _crop->cropMargins(),
			mods.angle,
			mods.flipped);
		_paint->applyTransform(geometry, mods.angle, mods.flipped);
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

void PhotoEditorContent::save(PhotoModifications &modifications) {
	modifications.crop = _crop->saveCropRect(_imageRect, _photo->rect());
	if (!modifications.paint) {
		modifications.paint = _paint->saveScene();
	}
}

void PhotoEditorContent::applyMode(PhotoEditorMode mode) {
	_crop->setVisible(mode == PhotoEditorMode::Transform);
	_paint->applyMode(mode);
	_mode = mode;
}

} // namespace Editor
