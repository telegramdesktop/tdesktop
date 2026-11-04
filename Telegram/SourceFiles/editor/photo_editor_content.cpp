/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_content.h"

#include "base/algorithm.h"
#include "editor/editor_audio_disc_button.h"
#include "editor/editor_video_layer_button.h"
#include "editor/editor_crop.h"
#include "editor/editor_enhance_indicator.h"
#include "editor/editor_paint.h"
#include "editor/scene/scene_item_video.h"
#include "history/history_drag_area.h"
#include "media/view/media_view_pip.h"
#include "storage/storage_media_prepare.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_editor.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

namespace Editor {
namespace {

constexpr auto kExpansionRoomRatio = 0.7;
constexpr auto kLayoutSmoothTau = 60.;
constexpr auto kLayoutMaxFrameDelta = crl::time(64);
constexpr auto kLayoutEpsilon = 0.5;
constexpr auto kCanvasZoomMin = 0.5;
constexpr auto kCanvasZoomMax = 4.;
constexpr auto kCanvasWheelZoomBase = 1.0015;

[[nodiscard]] QImage TransformedMedia(
		const QImage &image,
		const PhotoModifications &mods) {
	if (!mods.angle && !mods.flipped) {
		return image;
	}
	auto transform = QTransform();
	if (mods.flipped) {
		transform.scale(-1, 1);
	}
	if (mods.angle) {
		transform.rotate(mods.angle);
	}
	return image.transformed(transform);
}

[[nodiscard]] float64 MaxDistance(const QRectF &a, const QRectF &b) {
	return std::max({
		std::abs(a.x() - b.x()),
		std::abs(a.y() - b.y()),
		std::abs(a.width() - b.width()),
		std::abs(a.height() - b.height()),
	});
}

} // namespace

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
, _composeAnimated(data.composeAnimated)
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
	data))
, _crop(base::make_unique_q<Crop>(
	this,
	modifications,
	_photoSize,
	data))
, _audioDisc(base::make_unique_q<AudioDiscButton>(this))
, _videoLayerButton(base::make_unique_q<VideoLayerButton>(this))
, _photo(std::move(photo))
, _background(data.story
	? data.story->canvasBackground
	: Media::Encode::DominantCanvasBackground(_photo->original()))
, _modifications(modifications)
, _canvas(QRect(QPoint(), _photoSize) | modifications.crop)
, _canvasMedia((data.story && !data.story->canvasMedia.isNull())
	? std::make_shared<Image>(QImage(data.story->canvasMedia))
	: nullptr)
, _canvasBackground(data.story
	? data.story->canvasBackground
	: Media::Encode::CanvasBackground()) {
	_enhance = _initialEnhance = modifications.enhance;
	_storyPhoto = data.story && data.story->videoObject.empty();
	_enhanceIndicator = std::make_unique<EnhanceIndicator>(this);
	_enhanceIndicator->setGeometry(rect());
	_crop->setExpansionAllowed(!_fixedCrop);
	if (canvasMode()) {
		_crop->hide();
		if (!modifications.canvasMediaRect.isEmpty()) {
			restoreCanvasPlacement(modifications.canvasMediaRect);
		}
	}
	if (data.story && !data.story->videoObject.empty()) {
		_pendingVideo = data.story->videoObject;
	}
	if (data.story && !data.story->sceneItems.isEmpty()) {
		const auto &files = data.story->sceneFiles;
		_paint->addStoryItems(data.story->sceneItems, [=](int index) {
			return (index >= 0 && index < files.size())
				? files[index]
				: QString();
		});
		_paint->keepResult();
	}
	if (data.story && data.story->storyLink) {
		_paint->addStoryLink(*data.story->storyLink);
		_paint->keepResult();
	}
	if (data.story && !data.story->storyAudio.empty()) {
		_paint->addStoryAudio(
			data.story->storyAudio,
			data.story->storyAudioSelected);
		_paint->keepResult();
	}
	_paint->resetSceneBaseline();
	_initialCanvasZoom = _canvasZoom;
	_initialCanvasShift = _canvasShift;
	_paint->setCanvasBackground(_background);
	_paint->setCropRect(QRectF(_crop->cropRect()));
	_layoutAnimation.init([=](crl::time now) {
		return layoutAnimationStep(now);
	});
	_crop->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove
			|| type == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			setExpansionRoom(
				mouse->modifiers().testFlag(Qt::ControlModifier));
		}
	}, _crop->lifetime());
	_crop->changes(
	) | rpl::on_next([=] {
		updateCanvas();
	}, lifetime());
	_crop->dragChanges(
	) | rpl::on_next([=](bool dragging) {
		_dragging = dragging;
		if (dragging && _layoutAnimation.animating()) {
			_layoutAnimation.stop();
			applyLayout(_layoutTarget);
		} else if (!dragging) {
			updateRoom();
		}
	}, lifetime());

	_audioDisc->hide();
	_audioDisc->setClickedCallback([=] {
		_paint->setAudioSelected(!_paint->audioSelected());
	});
	_audioDisc->removeRequests(
	) | rpl::on_next([=] {
		_paint->removeAudio();
	}, _audioDisc->lifetime());
	_videoLayerButton->hide();
	_videoLayerButton->setClickedCallback([=] {
		if (const auto video = _paint->backgroundVideo()) {
			const auto locked = !video->isBackgroundLocked();
			_paint->setBackgroundVideoLocked(locked);
			_videoLayerButton->setLocked(locked);
		}
	});
	_paint->videoClipSelections(
	) | rpl::on_next([=](std::shared_ptr<VideoClip> clip) {
		if (!clip) {
			if (const auto video = _paint->backgroundVideo()) {
				if (!video->isBackgroundLocked()) {
					video->setBackgroundLocked(true);
					_videoLayerButton->setLocked(true);
				}
			}
		} else if (const auto video = _paint->backgroundVideo()) {
			if (clip.get() == video->videoClip()) {
				_videoLayerButton->setLocked(false);
			}
		}
	}, lifetime());
	_paint->audioChanges(
	) | rpl::on_next([=] {
		updateAudioDisc();
	}, lifetime());
	_paint->audioSelectedChanges(
	) | rpl::on_next([=](bool selected) {
		_audioDisc->setActive(selected);
	}, lifetime());
	rpl::merge(
		_innerRect.value() | rpl::to_empty,
		sizeValue() | rpl::to_empty
	) | rpl::on_next([=] {
		updateAudioDiscGeometry();
		updateVideoLayerButtonGeometry();
		_enhanceIndicator->setGeometry(rect());
	}, lifetime());

	rpl::combine(
		_modifications.value(),
		sizeValue(),
		_canvas.value(),
		_room.value()
	) | rpl::on_next([=](
			const PhotoModifications &mods,
			const QSize &size,
			const QRect &canvas,
			bool room) {
		if (size.isEmpty()) {
			return;
		}
		const auto target = layoutTarget(
			canvasMode() ? PhotoModifications() : mods,
			size,
			canvas,
			room && !canvasMode());
		const auto animate = _animateLayout
			&& !_dragging
			&& !anim::Disabled()
			&& !_imageRectF.isEmpty()
			&& (target != _imageRectF);
		if (!animate) {
			_layoutAnimation.stop();
			applyLayout(target);
			return;
		}
		_layoutTarget = target;
		if (!_layoutAnimation.animating()) {
			_layoutLastFrame = crl::now();
			_layoutAnimation.start();
		}
	}, lifetime());

	paintRequest(
	) | rpl::on_next([=](const QRect &clip) {
		auto p = QPainter(this);

		p.fillRect(clip, Qt::transparent);
		if (_mode.mode == PhotoEditorMode::Mode::Paint) {
			_paint->paintCanvas(p);
			_paint->paintImage(p, _photo->pix(_photoSize));
		} else if (canvasMode()) {
			paintCanvas(p);
		} else {
			paintCanvasFill(p);
			p.setTransform(_imageMatrix);
			const auto size = _layoutAnimation.animating()
				? _layoutTarget.size().toSize()
				: _imageRect.size();
			p.setRenderHint(
				QPainter::SmoothPixmapTransform,
				(size != _imageRect.size()));
			p.drawPixmap(_imageRect, _photo->pixSingle(size));
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

PhotoEditorContent::~PhotoEditorContent() = default;

QRectF PhotoEditorContent::layoutTarget(
		const PhotoModifications &mods,
		QSize size,
		QRect canvas,
		bool room) const {
	const auto m = _crop->cropMargins();
	const auto full = FlipSizeByRotation(size, mods.angle)
		- QSize(m.left() + m.right(), m.top() + m.bottom());
	const auto fit = room
		? (QSizeF(full) * kExpansionRoomRatio)
		: QSizeF(full);
	const auto scale = std::min({
		1.,
		fit.width() / canvas.width(),
		fit.height() / canvas.height(),
	});
	const auto canvasSize = QSizeF(canvas.size()) * scale;
	return QRectF(
		QPointF(-canvasSize.width() / 2., -canvasSize.height() / 2.)
			- QPointF(canvas.topLeft()) * scale,
		QSizeF(_photoSize) * scale);
}

void PhotoEditorContent::applyLayout(QRectF imageRect) {
	const auto size = this->size();
	if (size.isEmpty() || imageRect.isEmpty()) {
		return;
	}
	const auto &current = _modifications.current();
	const auto mods = canvasMode() ? PhotoModifications() : current;
	const auto canvas = _canvas.current();
	const auto scale = imageRect.width() / _photoSize.width();
	const auto rounded = [](float64 value) {
		return int(base::SafeRound(value));
	};
	_imageRectF = imageRect;
	_imageRect = QRect(
		QPoint(rounded(imageRect.x()), rounded(imageRect.y())),
		QSize(rounded(imageRect.width()), rounded(imageRect.height())));
	const auto canvasRect = QRect(
		_imageRect.topLeft() + QPoint(
			rounded(canvas.x() * scale),
			rounded(canvas.y() * scale)),
		QSize(
			rounded(canvas.width() * scale),
			rounded(canvas.height() * scale)));

	_imageMatrix.reset();
	_imageMatrix.translate(size.width() / 2, size.height() / 2);
	if (mods.flipped) {
		_imageMatrix.scale(-1, 1);
	}
	_imageMatrix.rotate(mods.angle);

	const auto geometry = _imageMatrix.mapRect(_imageRect);
	_crop->applyTransform(
		rect(),
		geometry.topLeft(),
		mods.angle,
		mods.flipped,
		imageRect.size());
	_crop->setCornersLevel(mods.cornersLevel);
	const auto canvasGeometry = _imageMatrix.mapRect(canvasRect);
	_paint->applyTransform(
		geometry,
		canvasGeometry,
		QRectF(canvas),
		mods.angle,
		mods.flipped);
	if (canvasMode()) {
		layoutCanvasMedia(canvasGeometry);
	}
	if (_pendingVideo) {
		auto pending = base::take(_pendingVideo);
		_paint->addStoryVideoObject(std::move(*pending));
		_paint->keepResult();
		_paint->resetSceneBaseline();
		_selectVideoInPaint = false;
		updateVideoLayerButton();
	}

	_innerRect = canvasGeometry;
	update();
}

bool PhotoEditorContent::layoutAnimationStep(crl::time now) {
	const auto delta = std::clamp(
		now - _layoutLastFrame,
		crl::time(0),
		kLayoutMaxFrameDelta);
	_layoutLastFrame = now;
	if (delta <= 0) {
		return true;
	}
	const auto ratio = 1. - std::exp(-float64(delta) / kLayoutSmoothTau);
	const auto next = anim::interpolatedRectF(
		_imageRectF,
		_layoutTarget,
		ratio);
	const auto finished = MaxDistance(next, _layoutTarget) < kLayoutEpsilon;
	applyLayout(finished ? _layoutTarget : next);
	return !finished;
}

void PhotoEditorContent::updateCanvas() {
	_paint->setCropRect(QRectF(_crop->cropRect()));
	_animateLayout = true;
	_canvas = QRect(QPoint(), _photoSize) | _crop->cropRect();
	_animateLayout = false;
}

void PhotoEditorContent::updateRoom() {
	if (_dragging) {
		return;
	}
	_animateLayout = true;
	_room = _roomRequested
		&& !_fixedCrop
		&& !canvasMode()
		&& (_mode.mode == PhotoEditorMode::Mode::Transform);
	_animateLayout = false;
}

void PhotoEditorContent::setExpansionRoom(bool room) {
	_roomRequested = room;
	updateRoom();
}

void PhotoEditorContent::paintCanvasFill(QPainter &p) const {
	if (Rect(_photoSize).contains(_crop->cropRect())) {
		return;
	}
	auto image = QPainterPath();
	image.addRect(_imageMatrix.mapRect(QRectF(_imageRect)));
	const auto fill = _crop->cropPath()
		.translated(_crop->pos())
		.subtracted(image);
	if (fill.isEmpty()) {
		return;
	}
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setClipPath(fill);
	Media::Encode::PaintCanvasBackground(
		p,
		_crop->paintRect().translated(_crop->pos()),
		_background);
	p.restore();
}

bool PhotoEditorContent::canvasMode() const {
	return (_canvasMedia != nullptr);
}

bool PhotoEditorContent::canvasGesturesActive() const {
	return canvasMode()
		&& (_mode.mode == PhotoEditorMode::Mode::Transform);
}

bool PhotoEditorContent::canvasMoved() const {
	return canvasMode()
		&& ((_canvasZoom != 1.) || !_canvasShift.isNull());
}

bool PhotoEditorContent::edited() const {
	if (canvasMode()
		&& ((_canvasZoom != _initialCanvasZoom)
			|| (_canvasShift != _initialCanvasShift))) {
		return true;
	}
	return _paint->sceneChanged()
		|| (_enhance != _initialEnhance);
}

void PhotoEditorContent::restoreCanvasPlacement(QRect rect) {
	const auto base = canvasBasePlacement();
	if (base.isEmpty() || rect.isEmpty()) {
		return;
	}
	_canvasZoom = std::clamp(
		rect.width() / base.width(),
		kCanvasZoomMin,
		kCanvasZoomMax);
	_canvasShift = QRectF(rect).center() - base.center();
	_canvasRestored = rect;
}

float64 PhotoEditorContent::canvasScale() const {
	const auto canvas = _imageMatrix.mapRect(_imageRect);
	return canvas.isEmpty()
		? 1.
		: canvas.width() / float64(_photoSize.width());
}

QRectF PhotoEditorContent::canvasBasePlacement() const {
	const auto &mods = _modifications.current();
	const auto oriented = FlipSizeByRotation(
		_canvasMedia->size(),
		mods.angle);
	return QRectF(Media::Encode::CanvasPlacement(oriented, _photoSize));
}

QRectF PhotoEditorContent::canvasMediaRect() const {
	if (_canvasRestored) {
		return QRectF(*_canvasRestored);
	}
	const auto base = canvasBasePlacement();
	const auto size = base.size() * _canvasZoom;
	const auto center = base.center() + _canvasShift;
	return QRectF(
		center - QPointF(size.width() / 2., size.height() / 2.),
		size);
}

void PhotoEditorContent::layoutCanvasMedia(QRect canvas) {
	const auto &mods = _modifications.current();
	const auto scale = canvasScale();
	const auto media = canvasMediaRect();
	_canvasMediaDisplay = QRectF(
		canvas.x() + media.x() * scale,
		canvas.y() + media.y() * scale,
		media.width() * scale,
		media.height() * scale).toRect();

	const auto frameSize = FlipSizeByRotation(
		_canvasMediaDisplay.size(),
		mods.angle);
	_canvasFrameRect = QRect(
		QPoint(-frameSize.width() / 2, -frameSize.height() / 2),
		frameSize);
	_canvasMediaMatrix.reset();
	_canvasMediaMatrix.translate(
		_canvasMediaDisplay.x() + _canvasMediaDisplay.width() / 2.,
		_canvasMediaDisplay.y() + _canvasMediaDisplay.height() / 2.);
	if (mods.flipped) {
		_canvasMediaMatrix.scale(-1, 1);
	}
	_canvasMediaMatrix.rotate(mods.angle);
	_canvasDirty = true;
}

void PhotoEditorContent::paintCanvas(QPainter &p) {
	const auto canvas = _imageMatrix.mapRect(_imageRect);
	if (canvas.isEmpty() || _canvasFrameRect.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	Media::Encode::PaintCanvasBackground(p, canvas, _canvasBackground);
	p.setClipRect(canvas);
	p.setTransform(_canvasMediaMatrix);
	const auto size = _canvasFrameRect.size();
	p.drawPixmap(
		_canvasFrameRect,
		(_enhance > 0 && _storyPhoto)
			? enhancedCanvasPix(size)
			: _canvasMedia->pix(size));
	p.resetTransform();
	p.setClipping(false);
}

void PhotoEditorContent::recomposeCanvas() {
	const auto &mods = _modifications.current();
	auto media = TransformedMedia(_canvasMedia->original(), mods);
	if (_enhance > 0) {
		media = EnhanceImage(std::move(media), _enhance);
	}
	_photo = std::make_shared<Image>(Media::Encode::FitOnCanvas(
		media,
		_photoSize,
		_canvasBackground,
		Media::Encode::RoundedCanvasRect(canvasMediaRect())));
	_canvasDirty = false;
}

void PhotoEditorContent::clampCanvasShift() {
	const auto base = canvasBasePlacement();
	const auto center = base.center() + _canvasShift;
	const auto clamped = QPointF(
		std::clamp(center.x(), 0., float64(_photoSize.width())),
		std::clamp(center.y(), 0., float64(_photoSize.height())));
	_canvasShift = clamped - base.center();
}

void PhotoEditorContent::zoomCanvas(float64 factor, QPoint about) {
	const auto zoom = std::clamp(
		_canvasZoom * factor,
		kCanvasZoomMin,
		kCanvasZoomMax);
	const auto real = zoom / _canvasZoom;
	const auto canvas = _imageMatrix.mapRect(_imageRect);
	if (real == 1. || canvas.isEmpty()) {
		return;
	}
	const auto point = QPointF(about - canvas.topLeft()) / canvasScale();
	const auto base = canvasBasePlacement();
	const auto center = base.center() + _canvasShift;
	const auto shifted = point - (point - center) * real;
	_canvasZoom = zoom;
	_canvasShift = shifted - base.center();
	_canvasRestored = std::nullopt;
	clampCanvasShift();
	canvasChanged();
}

void PhotoEditorContent::canvasChanged() {
	layoutCanvasMedia(_imageMatrix.mapRect(_imageRect));
	update();
}

void PhotoEditorContent::adjustEnhance(int delta) {
	const auto step = delta / float64(QWheelEvent::DefaultDeltasPerStep);
	const auto next = std::clamp(
		_enhance + int(base::SafeRound(step * 5)),
		0,
		100);
	if (next == _enhance) {
		return;
	}
	_enhance = next;
	_canvasDirty = true;
	_enhancedPixmapAmount = -1;
	_enhanceIndicator->setValue(_enhance);
	update();
}

const QPixmap &PhotoEditorContent::enhancedCanvasPix(QSize size) {
	if (_enhancedPixmapAmount != _enhance
		|| _enhancedPixmapSize != size) {
		if (!_enhanceLut.valid) {
			_enhanceLut = PrepareEnhance(_canvasMedia->original());
		}
		auto image = _canvasMedia->pix(size).toImage();
		image = ApplyEnhance(std::move(image), _enhanceLut, _enhance);
		_enhancedPixmap = QPixmap::fromImage(std::move(image));
		_enhancedPixmapAmount = _enhance;
		_enhancedPixmapSize = size;
	}
	return _enhancedPixmap;
}

void PhotoEditorContent::mousePressEvent(QMouseEvent *e) {
	const auto canvas = _imageMatrix.mapRect(_imageRect);
	if (!canvasGesturesActive()
		|| (e->button() != Qt::LeftButton)
		|| !canvas.contains(e->pos())) {
		return RpWidget::mousePressEvent(e);
	}
	_canvasPress = e->pos();
	_canvasDragShift = _canvasShift;
	_canvasMoved = false;
}

void PhotoEditorContent::mouseMoveEvent(QMouseEvent *e) {
	if (!_canvasPress) {
		return RpWidget::mouseMoveEvent(e);
	}
	const auto delta = e->pos() - *_canvasPress;
	if (!_canvasMoved
		&& (delta.manhattanLength() < QApplication::startDragDistance())) {
		return;
	}
	if (!_canvasMoved) {
		_canvasMoved = true;
		setCursor(Qt::ClosedHandCursor);
	}
	_canvasShift = _canvasDragShift + QPointF(delta) / canvasScale();
	_canvasRestored = std::nullopt;
	clampCanvasShift();
	canvasChanged();
}

void PhotoEditorContent::mouseReleaseEvent(QMouseEvent *e) {
	if (!_canvasPress) {
		return RpWidget::mouseReleaseEvent(e);
	}
	_canvasPress = std::nullopt;
	if (_canvasMoved) {
		_canvasMoved = false;
		unsetCursor();
	}
}

void PhotoEditorContent::wheelEvent(QWheelEvent *e) {
	const auto position = e->position().toPoint();
	const auto canvas = _imageMatrix.mapRect(_imageRect);
	if (!canvasGesturesActive() || !canvas.contains(position)) {
		return RpWidget::wheelEvent(e);
	}
	const auto delta = e->angleDelta().y()
		? e->angleDelta().y()
		: e->angleDelta().x();
	if (e->modifiers().testFlag(Qt::ControlModifier)) {
		if (delta) {
			zoomCanvas(std::pow(kCanvasWheelZoomBase, delta), position);
		}
	} else if (_storyPhoto) {
		adjustEnhance(delta);
	}
	e->accept();
}

bool PhotoEditorContent::eventHook(QEvent *e) {
	if (canvasGesturesActive() && (e->type() == QEvent::NativeGesture)) {
		const auto gesture = static_cast<QNativeGestureEvent*>(e);
		const auto position = gesture->position().toPoint();
		const auto canvas = _imageMatrix.mapRect(_imageRect);
		if ((gesture->gestureType() == Qt::ZoomNativeGesture)
			&& canvas.contains(position)) {
			zoomCanvas(1. + gesture->value(), position);
			return true;
		}
	}
	return RpWidget::eventHook(e);
}

void PhotoEditorContent::applyModifications(
		PhotoModifications modifications) {
	const auto &was = _modifications.current();
	if (was.angle != modifications.angle) {
		_canvasRestored = std::nullopt;
	}
	_modifications = std::move(modifications);
	update();
}

void PhotoEditorContent::save(PhotoModifications &modifications) {
	if (canvasMode()) {
		const auto moved = canvasMoved();
		modifications.crop = QRect();
		modifications.canvasMediaRect = moved
			? Media::Encode::RoundedCanvasRect(canvasMediaRect())
			: QRect();
	} else {
		modifications.crop = _crop->saveCropRect();
	}
	modifications.enhance = _enhance;
	_paint->keepResult();

	const auto savedScene = _paint->saveScene();
	if (!modifications.paint) {
		modifications.paint = savedScene;
	}
}

void PhotoEditorContent::applyMode(const PhotoEditorMode &mode) {
	if (mode.mode != PhotoEditorMode::Mode::Paint) {
		_paint->disarmShapeTool();
	}
	if (mode.mode == PhotoEditorMode::Mode::Out) {
		if (mode.action == PhotoEditorMode::Action::Discard) {
			_paint->restoreScene();
		}
		return;
	}
	const auto isTransform = (mode.mode == PhotoEditorMode::Mode::Transform);
	_crop->setVisible(isTransform && !canvasMode());

	_paint->setAttribute(Qt::WA_TransparentForMouseEvents, isTransform);
	if (!isTransform) {
		if (_canvasDirty) {
			recomposeCanvas();
		}
		_paint->updateUndoState();
	} else {
		_paint->resetView();
	}

	if (mode.action == PhotoEditorMode::Action::Discard) {
		_paint->cancel();
	} else if (mode.action == PhotoEditorMode::Action::Save) {
		_paint->keepResult();
	}
	if ((mode.mode == PhotoEditorMode::Mode::Paint) && _selectVideoInPaint) {
		_selectVideoInPaint = false;
		_paint->selectVideoObject();
	}
	_mode = mode;
	updateCanvas();
	updateRoom();
	updateAudioDisc();
	updateVideoLayerButton();
	update();
}

void PhotoEditorContent::updateAudioDiscGeometry() {
	const auto inner = _innerRect.current();
	const auto skip = st::photoEditorAudioDiscSkip;
	const auto size = _audioDisc->width();
	const auto right = rect::right(inner);
	const auto bottom = rect::bottom(inner);
	if (width() - right >= size + skip) {
		_audioDisc->moveToLeft(right + skip, bottom - size);
	} else if (height() - bottom >= size + skip) {
		_audioDisc->moveToLeft(right - size, bottom + skip);
	} else {
		_audioDisc->moveToLeft(right - skip - size, bottom - skip - size);
	}
}

void PhotoEditorContent::updateAudioDisc() {
	const auto audio = _paint->audio();
	const auto shown = (audio != nullptr)
		&& (_mode.mode == PhotoEditorMode::Mode::Paint);
	_audioDisc->setTrack(audio);
	_audioDisc->setVisible(shown);
	if (shown) {
		_audioDisc->raise();
	}
}

void PhotoEditorContent::updateVideoLayerButtonGeometry() {
	const auto inner = _innerRect.current();
	if (inner.isEmpty()) {
		return;
	}
	const auto skip = st::photoEditorAudioDiscSkip;
	const auto size = _videoLayerButton->width();
	const auto right = rect::right(inner);
	const auto top = inner.top();
	if (width() - right >= size + skip) {
		_videoLayerButton->moveToLeft(right + skip, top);
	} else {
		_videoLayerButton->moveToLeft(right - skip - size, top + skip);
	}
}

void PhotoEditorContent::updateVideoLayerButton() {
	const auto video = _paint->backgroundVideo();
	const auto shown = (video != nullptr)
		&& (_mode.mode == PhotoEditorMode::Mode::Paint);
	_videoLayerButton->setVisible(shown);
	if (shown) {
		_videoLayerButton->setLocked(
			video->isBackgroundLocked(),
			anim::type::instant);
		_videoLayerButton->raise();
		updateVideoLayerButtonGeometry();
	}
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

void PhotoEditorContent::createShapeItem(
		ShapeType shape,
		const Brush &brush,
		bool fill) {
	_paint->createShapeItem(shape, brush, fill);
}

void PhotoEditorContent::armShapeTool(
		ShapeType shape,
		const Brush &brush,
		bool fill) {
	_paint->armShapeTool(shape, brush, fill);
}

void PhotoEditorContent::disarmShapeTool() {
	_paint->disarmShapeTool();
}

void PhotoEditorContent::applyBrushToSelectedShape(const Brush &brush) {
	_paint->applyBrushToSelectedShape(brush);
}

void PhotoEditorContent::clearSelection() {
	_paint->clearSelection();
}

void PhotoEditorContent::applyTextPrefs(const TextPrefs &prefs) {
	_paint->applyTextPrefs(prefs);
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

rpl::producer<TextPrefs> PhotoEditorContent::textPrefsUsed() const {
	return _paint->textPrefsUsed();
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

rpl::producer<QColor> PhotoEditorContent::shapeItemSelections() const {
	return _paint->shapeItemSelections();
}

auto PhotoEditorContent::videoClipSelections() const
-> rpl::producer<std::shared_ptr<VideoClip>> {
	return _paint->videoClipSelections();
}

rpl::producer<> PhotoEditorContent::shapeItemDeselections() const {
	return _paint->shapeItemDeselections();
}

rpl::producer<> PhotoEditorContent::audioChanges() const {
	return _paint->audioChanges();
}

rpl::producer<bool> PhotoEditorContent::audioSelectedChanges() const {
	return _paint->audioSelectedChanges();
}

rpl::producer<> PhotoEditorContent::audioVolumeChanges() const {
	return _audioDisc->volumeChanges();
}

bool PhotoEditorContent::audioSelected() const {
	return _paint->audioSelected();
}

std::shared_ptr<AudioTrack> PhotoEditorContent::audio() const {
	return _paint->audio();
}

void PhotoEditorContent::removeAudio() {
	_paint->removeAudio();
}

bool PhotoEditorContent::canEqualizeDurations() const {
	return _paint->canEqualizeDurations();
}

void PhotoEditorContent::matchDurations(crl::time duration) {
	_paint->matchDurations(duration);
}

bool PhotoEditorContent::durationsLinked() const {
	return _paint->durationsLinked();
}

void PhotoEditorContent::setDurationsLinked(bool linked) {
	_paint->setDurationsLinked(linked);
}

rpl::producer<> PhotoEditorContent::durationsLinkChanges() const {
	return _paint->durationsLinkChanges();
}

rpl::producer<bool> PhotoEditorContent::shapeToolStates() const {
	return _paint->shapeToolStates();
}

rpl::producer<> PhotoEditorContent::paintModeRequests() const {
	return _paintModeRequests.events();
}

bool PhotoEditorContent::handleKeyPress(not_null<QKeyEvent*> e) {
	if (e->matches(QKeySequence::Paste)) {
		return pasteFromClipboard();
	}
	return _paint->handleKeyPress(e);
}

bool PhotoEditorContent::pasteFromClipboard() {
	const auto data = QGuiApplication::clipboard()->mimeData();
	if (!_paint->canHandleMimeData(data)) {
		return false;
	}
	addMimeData(data);
	return true;
}

void PhotoEditorContent::addMimeData(not_null<const QMimeData*> data) {
	if (_mode.mode != PhotoEditorMode::Mode::Paint) {
		_paintModeRequests.fire({});
	}
	_paint->handleMimeData(data);
}

void PhotoEditorContent::addVideoObject(StoryVideoObject &&object) {
	_pendingVideo = std::move(object);
}

void PhotoEditorContent::addRoundVideo(
		QByteArray content,
		crl::time duration) {
	_paint->addRoundVideo(std::move(content), duration);
}

void PhotoEditorContent::setupDragArea() {
	auto dragEnterFilter = [=](const QMimeData *data) {
		return _paint->canHandleMimeData(data);
	};

	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		std::move(dragEnterFilter),
		nullptr,
		nullptr,
		[=](const QMimeData *data) {
			return _composeAnimated
				? Storage::MimeDataState::Media
				: Storage::MimeDataState::Image;
		},
		nullptr,
		true);

	areas.photo->setDroppedCallback([=](const QMimeData *data) {
		addMimeData(data);
	});
}

} // namespace Editor
