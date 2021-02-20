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
	std::shared_ptr<Image> photo,
	PhotoModifications modifications,
	std::shared_ptr<UndoController> undoController)
: RpWidget(parent)
, _photoSize(photo->size())
, _paint(base::make_unique_q<Paint>(
	this,
	modifications,
	_photoSize,
	std::move(undoController)))
, _crop(base::make_unique_q<Crop>(this, modifications, _photoSize))
, _photo(std::move(photo))
, _modifications(modifications) {

	rpl::combine(
		_modifications.value(),
		sizeValue()
	) | rpl::start_with_next([=](
			const PhotoModifications &mods, const QSize &size) {
		if (size.isEmpty()) {
			return;
		}
		const auto imageSizeF = [&] {
			const auto rotatedSize =
				FlipSizeByRotation(size, mods.angle);
			const auto m = _crop->cropMargins();
			const auto sizeForCrop = rotatedSize
				- QSize(m.left() + m.right(), m.top() + m.bottom());
			const auto originalSize = QSizeF(_photoSize);
			if ((originalSize.width() > sizeForCrop.width())
				|| (originalSize.height() > sizeForCrop.height())) {
				return originalSize.scaled(
					sizeForCrop,
					Qt::KeepAspectRatio);
			}
			return originalSize;
		}();
		const auto imageSize = QSize(imageSizeF.width(), imageSizeF.height());
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
			mods.flipped, imageSizeF);
		_paint->applyTransform(geometry, mods.angle, mods.flipped);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		p.fillRect(clip, Qt::transparent);

		p.setMatrix(_imageMatrix);

		p.drawPixmap(
			_imageRect,
			_photo->pix(_imageRect.width(), _imageRect.height()));
	}, lifetime());
}

void PhotoEditorContent::applyModifications(
		PhotoModifications modifications) {
	_modifications = std::move(modifications);
	update();
}

void PhotoEditorContent::save(PhotoModifications &modifications) {
	modifications.crop = _crop->saveCropRect();
	_paint->keepResult();
	if (!modifications.paint) {
		modifications.paint = _paint->saveScene();
	}
}

void PhotoEditorContent::applyMode(const PhotoEditorMode &mode) {
	const auto isTransform = (mode.mode == PhotoEditorMode::Mode::Transform);
	_crop->setVisible(isTransform);

	_paint->setAttribute(Qt::WA_TransparentForMouseEvents, isTransform);
	if (!isTransform) {
		_paint->updateUndoState();
	}

	if (mode.action == PhotoEditorMode::Action::Discard) {
		_paint->cancel();
	} else if (mode.action == PhotoEditorMode::Action::Save) {
		_paint->keepResult();
	}
	_mode = mode;
}

void PhotoEditorContent::applyBrush(const Brush &brush) {
	_paint->applyBrush(brush);
}

} // namespace Editor
