/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_paint.h"

#include "base/platform/base_platform_haptic.h"
#include "core/mime_type.h"
#include "editor/controllers/controllers.h"
#include "editor/scene/scene_item_canvas.h"
#include "editor/scene/scene_item_image.h"
#include "editor/scene/scene_item_sticker.h"
#include "editor/scene/scene_item_text.h"
#include "editor/scene/scene.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"

#include <QGraphicsView>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtCore/QMimeData>

namespace Editor {
namespace {

constexpr auto kMaxBrush = 25.;
constexpr auto kMinBrush = 1.;
constexpr auto kMinCanvasZoom = 1.;
constexpr auto kMaxCanvasZoom = 8.;
constexpr auto kCanvasZoomStep = 1.15;
constexpr auto kZoomEpsilon = 0.0001;
constexpr auto kMinItemZoom = 0.1;
constexpr auto kMaxItemZoom = 10.;
constexpr auto kCanvasZoomStepFine = 1.015;
constexpr auto kZoomSmoothTau = 60.;
constexpr auto kZoomMaxFrameDelta = crl::time(64);

std::shared_ptr<Scene> EnsureScene(
		PhotoModifications &mods,
		const QSize &size) {
	if (!mods.paint) {
		mods.paint = std::make_shared<Scene>(QRectF(QPointF(), size));
	}
	return mods.paint;
}

} // namespace

using ItemPtr = std::shared_ptr<QGraphicsItem>;

Paint::Paint(
	not_null<Ui::RpWidget*> parent,
	PhotoModifications &modifications,
	const QSize &imageSize,
	std::shared_ptr<Controllers> controllers,
	Fn<QImage(QRect)> blurSource,
	bool fixedCrop)
: RpWidget(parent)
, _controllers(controllers)
, _scene(EnsureScene(modifications, imageSize))
, _view(base::make_unique_q<QGraphicsView>(_scene.get(), this))
, _viewport(_view->viewport())
, _imageSize(imageSize)
, _fixedCrop(fixedCrop) {
	Expects(modifications.paint != nullptr);

	_scene->setBlurSource(std::move(blurSource));

	{
		constexpr auto kDefaultFontSizeDivisor = 15.;
		const auto shortSide = std::min(
			imageSize.width(),
			imageSize.height());
		_scene->setTextDefaults(
			QColor(255, 255, 255),
			shortSide / kDefaultFontSizeDivisor,
			int(TextStyle::Plain));
	}

	keepResult();

	_view->show();
	_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setFrameStyle(int(QFrame::NoFrame) | QFrame::Plain);
	_view->setBackgroundBrush(Qt::transparent);
	_view->setAttribute(Qt::WA_TranslucentBackground, true);
	_viewport->setAutoFillBackground(false);
	_viewport->setAttribute(Qt::WA_TranslucentBackground, true);
	_viewport->installEventFilter(this);

	_scene->textEditStates(
	) | rpl::on_next([=](bool editing) {
		_textEditing = editing;
	}, lifetime());

	// Undo / Redo.
	controllers->undoController->performRequestChanges(
	) | rpl::on_next([=](const Undo &command) {
		if (_textEditing.current()) {
			return;
		}
		if (command == Undo::Undo) {
			_scene->performUndo();
		} else {
			_scene->performRedo();
		}

		_hasUndo = _scene->hasUndo();
		_hasRedo = _scene->hasRedo();
	}, lifetime());

	controllers->undoController->setCanPerformChanges(rpl::merge(
		rpl::combine(
			_hasUndo.value(),
			_textEditing.value()
		) | rpl::map([](bool enable, bool editing) {
			return UndoController::EnableRequest{
				.command = Undo::Undo,
				.enable = enable && !editing,
			};
		}),
		rpl::combine(
			_hasRedo.value(),
			_textEditing.value()
		) | rpl::map([](bool enable, bool editing) {
			return UndoController::EnableRequest{
				.command = Undo::Redo,
				.enable = enable && !editing,
			};
		})));

	if (controllers->stickersPanelController) {
		using ShowRequest = StickersPanelController::ShowRequest;

		controllers->stickersPanelController->setShowRequestChanges(
			controllers->stickersPanelController->stickerChosen(
			) | rpl::map_to(ShowRequest::HideAnimated));

		controllers->stickersPanelController->stickerChosen(
		) | rpl::on_next([=](not_null<DocumentData*> document) {
			const auto item = std::make_shared<ItemSticker>(
				document,
				itemBaseData());
			_scene->addItem(item);
			_scene->clearSelection();
		}, lifetime());
	}

	rpl::merge(
		controllers->stickersPanelController
			? controllers->stickersPanelController->stickerChosen(
				) | rpl::to_empty
			: rpl::never<>() | rpl::type_erased,
		_scene->addsItem()
	) | rpl::on_next([=] {
		clearRedoList();
		updateUndoState();
	}, lifetime());

	_scene->removesItem(
	) | rpl::on_next([=] {
		updateUndoState();
	}, lifetime());

	_zoomAnimation.init([=](crl::time now) {
		return zoomAnimationStep(now);
	});

}

bool Paint::zoomSceneItems(float64 wheelDelta, bool fine) {
	if (!wheelDelta) {
		return false;
	}
	const auto step = wheelDelta
		/ float64(QWheelEvent::DefaultDeltasPerStep);
	const auto base = fine ? kCanvasZoomStepFine : kCanvasZoomStep;
	return zoomSceneItemsByFactor(std::pow(base, step));
}

bool Paint::zoomSceneItemsByFactor(float64 factor) {
	const auto center = rect::center(_scene->sceneRect());
	auto applied = false;
	for (const auto &item : _scene->items()) {
		const auto raw = item.get();
		const auto oldScale = raw->scale();
		const auto newScale = std::clamp(
			oldScale * factor,
			kMinItemZoom,
			kMaxItemZoom);
		if (std::abs(newScale - oldScale) < kZoomEpsilon) {
			continue;
		}
		const auto ratio = newScale / oldScale;
		raw->setScale(newScale);
		const auto pos = raw->pos();
		raw->setPos(center + (pos - center) * ratio);
		applied = true;
	}
	if (!applied && std::abs(factor - 1.) > kZoomEpsilon) {
		if (!_zoomAtLimit) {
			_zoomAtLimit = true;
			base::Platform::Haptic();
		}
	} else if (applied) {
		_zoomAtLimit = false;
	}
	return applied;
}

void Paint::zoomCanvas(float64 factor, QPoint viewportPoint, bool animated) {
	const auto view = _view.get();
	if (!view || !_viewport) {
		return;
	}
	const auto current = _zoomAnimation.animating()
		? _zoomTarget
		: _transform.userZoom;
	const auto raw = current * factor;
	const auto newTarget = std::clamp(raw, kMinCanvasZoom, kMaxCanvasZoom);
	const auto pushingPastLimit
		= (factor > 1. && raw > kMaxCanvasZoom + kZoomEpsilon)
		|| (factor < 1. && raw < kMinCanvasZoom - kZoomEpsilon);
	if (std::abs(newTarget - current) < kZoomEpsilon) {
		if (pushingPastLimit && !_zoomAtLimit) {
			_zoomAtLimit = true;
			base::Platform::Haptic();
		}
		return;
	}
	_zoomAtLimit = false;
	_zoomTarget = newTarget;
	_zoomFocus = viewportPoint;
	_zoomAnchorScene = view->mapToScene(viewportPoint);
	if (animated) {
		if (!_zoomAnimation.animating()) {
			_zoomLastFrame = crl::now();
			_zoomAnimation.start();
		}
	} else {
		_zoomAnimation.stop();
		applyCanvasZoom(newTarget, false);
	}
}

void Paint::applyCanvasZoom(float64 zoom, bool subpixel) {
	const auto view = _view.get();
	if (!view || !_viewport) {
		return;
	}
	_transform.userZoom = zoom;
	updateViewGeometry();
	applyViewTransform();
	const auto sceneAtFocus = view->mapToScene(_zoomFocus);
	const auto center = view->mapToScene(rect::center(_viewport->rect()));
	view->centerOn(center - (sceneAtFocus - _zoomAnchorScene));
	if (subpixel) {
		const auto landed = view->viewportTransform().map(_zoomAnchorScene);
		auto residual = QPointF(_zoomFocus) - landed;
		residual.setX(std::clamp(residual.x(), -1., 1.));
		residual.setY(std::clamp(residual.y(), -1., 1.));
		view->setTransform(view->transform()
			* QTransform().translate(residual.x(), residual.y()));
	}
	if (const auto parent = parentWidget()) {
		parent->update(geometry());
	}
}

bool Paint::zoomAnimationStep(crl::time now) {
	const auto delta = std::clamp(
		now - _zoomLastFrame,
		crl::time(0),
		kZoomMaxFrameDelta);
	_zoomLastFrame = now;

	const auto shown = _transform.userZoom;
	const auto ratio = 1. - std::exp(-float64(delta) / kZoomSmoothTau);
	auto next = shown + (_zoomTarget - shown) * ratio;
	const auto finished = std::abs(next - _zoomTarget) < kZoomEpsilon;
	if (finished) {
		next = _zoomTarget;
	}
	applyCanvasZoom(next, !finished);
	return !finished;
}

void Paint::panSceneItems(QPointF sceneDelta) {
	if (sceneDelta.isNull()) {
		return;
	}
	for (const auto &item : _scene->items()) {
		item->setPos(item->pos() + sceneDelta);
	}
}

QPointF Paint::mapWidgetDeltaToScene(QPoint delta) const {
	if (!_view) {
		return QPointF(delta);
	}
	return _view->mapToScene(delta) - _view->mapToScene(QPoint());
}

Paint::~Paint() {
	_scene->cancelTextEditing();
	if (_viewport) {
		_viewport->removeEventFilter(this);
	}
}

void Paint::updateViewGeometry() {
	if (_imageGeometry.isEmpty()) {
		return;
	}
	const auto target = (_transform.userZoom - kMinCanvasZoom) > kZoomEpsilon
		? _outerGeometry
		: _imageGeometry;
	if (geometry() != target) {
		setGeometry(target);
	}
	_view->setGeometry(rect());
}

void Paint::applyTransform(QRect geometry, int angle, bool flipped) {
	if (geometry.isEmpty()) {
		return;
	}
	_imageGeometry = geometry;
	_outerGeometry = parentWidget() ? parentWidget()->rect() : geometry;

	const auto center = (_transform.fitZoom <= 0.)
		|| _view->viewport()->rect().isEmpty()
		? rect::center(_scene->sceneRect())
		: _view->mapToScene(_view->viewport()->rect().center());
	const auto size = geometry.size();

	const auto rotatedImageSize = QTransform()
		.rotate(angle)
		.mapRect(QRect(QPoint(), _imageSize));

	const auto ratioW = size.width() / float64(rotatedImageSize.width())
		* (flipped ? -1 : 1);
	const auto ratioH = size.height() / float64(rotatedImageSize.height());

	_view->setGeometry(rect());

	_transform = {
		.angle = angle,
		.flipped = flipped,
		.fitZoom = ((std::abs(ratioW) + std::abs(ratioH)) / 2.),
		.ratioW = ratioW,
		.ratioH = ratioH,
		.userZoom = _transform.userZoom,
	};
	updateViewGeometry();
	applyViewTransform();
	_view->centerOn(center);
	if (const auto parent = parentWidget()) {
		parent->update();
	}
}

std::shared_ptr<Scene> Paint::saveScene() const {
	_scene->save(SaveState::Save);
	return _scene->items().empty()
		? nullptr
		: _scene;
}

void Paint::restoreScene() {
	_scene->restore(SaveState::Save);
}

void Paint::cancel() {
	_scene->restore(SaveState::Keep);
}

void Paint::keepResult() {
	_scene->save(SaveState::Keep);
}

void Paint::clearRedoList() {
	_scene->clearRedoList();

	_hasRedo = false;
}

void Paint::updateUndoState() {
	_hasUndo = _scene->hasUndo();
	_hasRedo = _scene->hasRedo();
}

void Paint::applyBrush(const Brush &brush) {
	_scene->applyBrush(
		brush.color,
		(kMinBrush + float64(kMaxBrush - kMinBrush) * brush.sizeRatio),
		brush.tool);
}

void Paint::createTextItem() {
	_scene->createTextAtCenter(-_transform.angle);
}

void Paint::clearSelection() {
	_scene->clearSelection();
}

void Paint::setTextColor(const QColor &color) {
	_scene->setTextColor(color);
}

void Paint::setSelectedTextColor(const QColor &color) {
	_scene->setSelectedTextColor(color);
}

rpl::producer<QColor> Paint::textColorRequests() const {
	return _scene->textColorRequests();
}

rpl::producer<QColor> Paint::textItemSelections() const {
	return _scene->textItemSelections();
}

rpl::producer<> Paint::textItemDeselections() const {
	return _scene->textItemDeselections();
}

rpl::producer<bool> Paint::textEditStates() const {
	return _scene->textEditStates();
}

void Paint::handleMimeData(const QMimeData *data) {
	const auto add = [&](QImage image) {
		if (image.isNull()) {
			return;
		}
		if (!Ui::ValidateThumbDimensions(image.width(), image.height())) {
			_controllers->show->showBox(
				Ui::MakeInformBox(tr::lng_edit_media_invalid_file()));
			return;
		}

		const auto item = std::make_shared<ItemImage>(
			Ui::PixmapFromImage(std::move(image)),
			itemBaseData());
		_scene->addItem(item);
		_scene->clearSelection();
	};

	using Error = Ui::PreparedList::Error;
	const auto premium = false; // Don't support > 2GB files here.
	const auto list = Core::ReadMimeUrls(data);
	auto result = !list.isEmpty()
		? Storage::PrepareMediaList(
			list.mid(0, 1),
			_imageSize.width() / 2,
			premium)
		: Ui::PreparedList(Error::EmptyFile, QString());
	if (result.error == Error::None) {
		add(base::take(result.files.front().preview));
	} else if (auto read = Core::ReadMimeImage(data)) {
		add(std::move(read.image));
	}
}

void Paint::paintImage(QPainter &p, const QPixmap &image) const {
	if (_view->geometry().isEmpty()) {
		return;
	}
	p.save();
	p.setClipRect(geometry(), Qt::IntersectClip);
	p.translate(pos());
	p.setTransform(_view->viewportTransform(), true);
	p.drawPixmap(Rect(_imageSize), image);
	p.restore();
}

void Paint::resetView() {
	_zoomAnimation.stop();
	_zoomTarget = kMinCanvasZoom;
	if (_transform.userZoom == kMinCanvasZoom) {
		return;
	}
	_transform.userZoom = kMinCanvasZoom;
	updateViewGeometry();
	applyViewTransform();
	_view->centerOn(rect::center(_scene->sceneRect()));
	if (const auto parent = parentWidget()) {
		parent->update(geometry());
	}
}

ItemBase::Data Paint::itemBaseData() const {
	const auto s = _scene->sceneRect().toRect().size();
	const auto size = std::min(s.width(), s.height()) / 2;
	const auto x = s.width() / 2;
	const auto y = s.height() / 2;
	return ItemBase::Data{
		.initialZoom = _transform.zoom,
		.zPtr = _scene->lastZ(),
		.size = size,
		.x = x,
		.y = y,
		.flipped = _transform.flipped,
		.rotation = -_transform.angle,
		.imageSize = _imageSize,
	};
}

void Paint::applyViewTransform() {
	_view->setTransform(QTransform()
		.scale(
			_transform.ratioW * _transform.userZoom,
			_transform.ratioH * _transform.userZoom)
		.rotate(_transform.angle));
	_transform.zoom = _transform.fitZoom * _transform.userZoom;
	_scene->updateZoom(_transform.zoom);
}

bool Paint::eventFilter(QObject *obj, QEvent *e) {
	if (obj != _viewport) {
		return RpWidget::eventFilter(obj, e);
	}
	const auto view = _view.get();
	if (!view || !_viewport) {
		return true;
	}
	if (e->type() == QEvent::Wheel) {
		const auto wheel = static_cast<QWheelEvent*>(e);
		const auto raw = wheel->angleDelta();
		const auto delta = raw.y() ? raw.y() : raw.x();
		if (!delta) {
			return true;
		}

		if (_fixedCrop) {
			zoomSceneItems(
				delta,
				wheel->modifiers().testFlag(Qt::ShiftModifier));
			return true;
		}
		const auto step = delta / float64(QWheelEvent::DefaultDeltasPerStep);
		zoomCanvas(
			std::pow(kCanvasZoomStep, step),
			wheel->position().toPoint(),
			false);
		return true;
	} else if (e->type() == QEvent::NativeGesture) {
		const auto gesture = static_cast<QNativeGestureEvent*>(e);
		if (gesture->gestureType() != Qt::ZoomNativeGesture) {
			return RpWidget::eventFilter(obj, e);
		}
		const auto factor = 1. + gesture->value();
		if (_fixedCrop) {
			zoomSceneItemsByFactor(factor);
		} else {
			zoomCanvas(factor, gesture->pos(), true);
		}
		return true;
	} else if (e->type() == QEvent::MouseButtonPress) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (mouse->button() == Qt::MiddleButton) {
			_pan = {
				.active = (_fixedCrop
					|| _transform.userZoom > kMinCanvasZoom),
				.point = mouse->pos(),
			};
			if (_pan.active) {
				_viewport->setCursor(Qt::ClosedHandCursor);
			}
			return true;
		}
	} else if (e->type() == QEvent::MouseMove) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (_pan.active) {
			const auto point = mouse->pos();
			const auto delta = point - _pan.point;
			_pan.point = point;

			if (_fixedCrop) {
				panSceneItems(mapWidgetDeltaToScene(delta));
			} else if (_transform.userZoom > kMinCanvasZoom) {
				view->horizontalScrollBar()->setValue(
					view->horizontalScrollBar()->value() - delta.x());
				view->verticalScrollBar()->setValue(
					view->verticalScrollBar()->value() - delta.y());
				if (const auto parent = parentWidget()) {
					parent->update(geometry());
				}
			}
			return true;
		}
	} else if (e->type() == QEvent::MouseButtonRelease) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (mouse->button() == Qt::MiddleButton) {
			if (_pan.active) {
				_viewport->unsetCursor();
			}
			_pan.active = false;
			return true;
		}
	}
	return RpWidget::eventFilter(obj, e);
}

} // namespace Editor
