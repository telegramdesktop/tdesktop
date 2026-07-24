/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_content.h"

#include "editor/editor_crop.h"
#include "editor/editor_paint.h"
#include "history/history_drag_area.h"
#include "media/view/media_view_pip.h"
#include "storage/storage_media_prepare.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>

namespace Editor {

using Media::View::FlipSizeByRotation;
using Media::View::RotatedRect;

PhotoEditorContent::PhotoEditorContent(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Image> photo,
	PhotoModifications modifications,
	std::shared_ptr<Controllers> controllers,
	EditorData data)
: RpWidget(parent)
, _photoSize(photo->size())
, _fixedCrop(data.fixedCrop)
, _paint(base::make_unique_q<Paint>(
	this,
	modifications,
	_photoSize,
	std::move(controllers),
	[photo](QRect rect) {
		const auto &img = photo->original();
		const auto dpr = img.devicePixelRatio();
		const auto pixelRect = QRect(
			int(rect.x() * dpr),
			int(rect.y() * dpr),
			int(rect.width() * dpr),
			int(rect.height() * dpr));
		auto result = img.copy(pixelRect.intersected(img.rect()));
		result.setDevicePixelRatio(dpr);
		return result;
	},
	data.fixedCrop))
, _crop(base::make_unique_q<Crop>(
	this,
	modifications,
	_photoSize,
	std::move(data)))
, _photo(std::move(photo))
, _modifications(modifications) {

	rpl::combine(
		_modifications.value(),
		sizeValue()
	) | rpl::on_next([=](
			const PhotoModifications &mods, const QSize &size) {
		if (size.isEmpty()) {
			return;
		}
		const auto imageSizeF = [&] {
			const auto rotatedSize
				= FlipSizeByRotation(size, mods.angle);
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
		_crop->setCornersLevel(mods.cornersLevel);
		_paint->applyTransform(geometry, mods.angle, mods.flipped);

		_innerRect = geometry;
	}, lifetime());

	paintRequest(
	) | rpl::on_next([=](const QRect &clip) {
		auto p = QPainter(this);

		p.fillRect(clip, Qt::transparent);
		if (_mode.mode == PhotoEditorMode::Mode::Paint) {
			_paint->paintImage(p, _photo->pix(_photoSize));
		} else {
			p.setTransform(_imageMatrix);
			p.drawPixmap(_imageRect, _photo->pix(_imageRect.size()));
		}
	}, lifetime());

	setupDragArea();

	if (_fixedCrop) {
		const auto pan = _crop->lifetime().make_state<
			std::optional<QPoint>
		>();
		_crop->events(
		) | rpl::on_next([=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::Wheel) {
				const auto wheel = static_cast<QWheelEvent*>(e.get());
				const auto raw = wheel->angleDelta();
				_paint->zoomSceneItems(
					raw.y() ? raw.y() : raw.x(),
					wheel->modifiers().testFlag(Qt::ShiftModifier));
				e->accept();
			} else if (type == QEvent::MouseButtonPress) {
				const auto mouse = static_cast<QMouseEvent*>(e.get());
				if (mouse->button() == Qt::MiddleButton) {
					*pan = mouse->pos();
					_crop->setCursor(Qt::ClosedHandCursor);
					e->accept();
				}
			} else if (type == QEvent::MouseMove) {
				if (pan->has_value()) {
					const auto mouse = static_cast<QMouseEvent*>(e.get());
					const auto point = mouse->pos();
					const auto delta = point - **pan;
					*pan = point;
					_paint->panSceneItems(
						_paint->mapWidgetDeltaToScene(delta));
					e->accept();
				}
			} else if (type == QEvent::MouseButtonRelease) {
				const auto mouse = static_cast<QMouseEvent*>(e.get());
				if (mouse->button() == Qt::MiddleButton && pan->has_value()) {
					pan->reset();
					_crop->unsetCursor();
					e->accept();
				}
			}
		}, _crop->lifetime());
	}
}

void PhotoEditorContent::applyModifications(
		PhotoModifications modifications) {
	_modifications = std::move(modifications);
	update();
}

void PhotoEditorContent::save(PhotoModifications &modifications) {
	modifications.crop = _crop->saveCropRect();
	_paint->keepResult();

	const auto savedScene = _paint->saveScene();
	if (!modifications.paint) {
		modifications.paint = savedScene;
	}
}

void PhotoEditorContent::applyMode(const PhotoEditorMode &mode) {
	if (mode.mode == PhotoEditorMode::Mode::Out) {
		if (mode.action == PhotoEditorMode::Action::Discard) {
			_paint->restoreScene();
		}
		return;
	}
	const auto isTransform = (mode.mode == PhotoEditorMode::Mode::Transform);
	_crop->setVisible(isTransform);

	_paint->setAttribute(Qt::WA_TransparentForMouseEvents, isTransform);
	if (!isTransform) {
		_paint->updateUndoState();
	} else {
		_paint->resetView();
	}

	if (mode.action == PhotoEditorMode::Action::Discard) {
		_paint->cancel();
	} else if (mode.action == PhotoEditorMode::Action::Save) {
		_paint->keepResult();
	}
	_mode = mode;
	update();
}

void PhotoEditorContent::applyAspectRatio(float64 ratio) {
	_crop->setAspectRatio(ratio);
}

void PhotoEditorContent::applyBrush(const Brush &brush) {
	_paint->applyBrush(brush);
}

void PhotoEditorContent::createTextItem() {
	_paint->createTextItem();
}

void PhotoEditorContent::clearSelection() {
	_paint->clearSelection();
}

void PhotoEditorContent::setTextColor(const QColor &color) {
	_paint->setTextColor(color);
}

void PhotoEditorContent::setSelectedTextColor(const QColor &color) {
	_paint->setSelectedTextColor(color);
}

rpl::producer<QColor> PhotoEditorContent::textColorRequests() const {
	return _paint->textColorRequests();
}

rpl::producer<QColor> PhotoEditorContent::textItemSelections() const {
	return _paint->textItemSelections();
}

rpl::producer<> PhotoEditorContent::textItemDeselections() const {
	return _paint->textItemDeselections();
}

rpl::producer<bool> PhotoEditorContent::textEditStates() const {
	return _paint->textEditStates();
}

bool PhotoEditorContent::handleKeyPress(not_null<QKeyEvent*> e) const {
	return false;
}

void PhotoEditorContent::setupDragArea() {
	auto dragEnterFilter = [=](const QMimeData *data) {
		return (_mode.mode == PhotoEditorMode::Mode::Paint)
			? Storage::ValidatePhotoEditorMediaDragData(data)
			: false;
	};

	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		std::move(dragEnterFilter),
		nullptr,
		nullptr,
		[](const QMimeData *d) { return Storage::MimeDataState::Image; },
		true);

	areas.photo->setDroppedCallback([=](const QMimeData *data) {
		_paint->handleMimeData(data);
	});
}

} // namespace Editor
