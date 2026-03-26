/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_paint.h"

#include "core/mime_type.h"
#include "editor/controllers/controllers.h"
#include "editor/scene/scene_item_canvas.h"
#include "editor/scene/scene_item_image.h"
#include "editor/scene/scene_item_sticker.h"
#include "editor/scene/scene.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"

#include <QGraphicsView>
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
	std::shared_ptr<Controllers> controllers)
: RpWidget(parent)
, _controllers(controllers)
, _scene(EnsureScene(modifications, imageSize))
, _view(base::make_unique_q<QGraphicsView>(_scene.get(), this))
, _viewport(_view->viewport())
, _imageSize(imageSize) {
	Expects(modifications.paint != nullptr);

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

	// Undo / Redo.
	controllers->undoController->performRequestChanges(
	) | rpl::on_next([=](const Undo &command) {
		if (command == Undo::Undo) {
			_scene->performUndo();
		} else {
			_scene->performRedo();
		}

		_hasUndo = _scene->hasUndo();
		_hasRedo = _scene->hasRedo();
	}, lifetime());

	controllers->undoController->setCanPerformChanges(rpl::merge(
		_hasUndo.value() | rpl::map([](bool enable) {
			return UndoController::EnableRequest{
				.command = Undo::Undo,
				.enable = enable,
			};
		}),
		_hasRedo.value() | rpl::map([](bool enable) {
			return UndoController::EnableRequest{
				.command = Undo::Redo,
				.enable = enable,
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

}

Paint::~Paint() {
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
		const auto delta = wheel->angleDelta().y();
		if (!delta) {
			return true;
		}

		const auto step = delta / float64(QWheelEvent::DefaultDeltasPerStep);
		const auto factor = std::pow(kCanvasZoomStep, step);
		const auto newZoom = std::clamp(
			_transform.userZoom * factor,
			kMinCanvasZoom,
			kMaxCanvasZoom);
		if (std::abs(newZoom - _transform.userZoom) < 0.0001) {
			return true;
		}

		const auto viewportPoint = wheel->position().toPoint();
		const auto globalPoint = _viewport->mapToGlobal(viewportPoint);
		const auto scenePoint = view->mapToScene(viewportPoint);
		_transform.userZoom = newZoom;
		updateViewGeometry();
		applyViewTransform();
		const auto scenePointAfter = view->mapToScene(
			_viewport->mapFromGlobal(globalPoint));
		const auto center = view->mapToScene(rect::center(_viewport->rect()));
		view->centerOn(center - (scenePointAfter - scenePoint));
		if (const auto parent = parentWidget()) {
			parent->update(geometry());
		}
		return true;
	} else if (e->type() == QEvent::MouseButtonPress) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (mouse->button() == Qt::MiddleButton) {
			_pan = {
				.active = (_transform.userZoom > kMinCanvasZoom),
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

			if (_transform.userZoom > kMinCanvasZoom) {
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
