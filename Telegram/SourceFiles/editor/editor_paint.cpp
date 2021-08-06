/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_paint.h"

#include "boxes/confirm_box.h"
#include "editor/controllers/controllers.h"
#include "editor/scene/scene.h"
#include "editor/scene/scene_item_canvas.h"
#include "editor/scene/scene_item_image.h"
#include "editor/scene/scene_item_sticker.h"
#include "lottie/lottie_single_player.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/ui_utility.h"

#include <QGraphicsView>
#include <QtCore/QMimeData>

namespace Editor {
namespace {

constexpr auto kMaxBrush = 25.;
constexpr auto kMinBrush = 1.;

constexpr auto kViewStyle = "QGraphicsView {\
		background-color: transparent;\
		border: 0px\
	}"_cs;

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
, _imageSize(imageSize) {
	Expects(modifications.paint != nullptr);

	keepResult();

	_view->show();
	_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setStyleSheet(kViewStyle.utf8());

	// Undo / Redo.
	controllers->undoController->performRequestChanges(
	) | rpl::start_with_next([=](const Undo &command) {
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
		) | rpl::start_with_next([=](not_null<DocumentData*> document) {
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
			: rpl::never<>() | rpl::type_erased(),
		_scene->addsItem()
	) | rpl::start_with_next([=] {
		clearRedoList();
		updateUndoState();
	}, lifetime());

	_scene->removesItem(
	) | rpl::start_with_next([=] {
		updateUndoState();
	}, lifetime());

}

void Paint::applyTransform(QRect geometry, int angle, bool flipped) {
	if (geometry.isEmpty()) {
		return;
	}
	setGeometry(geometry);
	const auto size = geometry.size();

	const auto rotatedImageSize = QMatrix()
		.rotate(angle)
		.mapRect(QRect(QPoint(), _imageSize));

	const auto ratioW = size.width() / float64(rotatedImageSize.width())
		* (flipped ? -1 : 1);
	const auto ratioH = size.height() / float64(rotatedImageSize.height());

	_view->setTransform(QTransform().scale(ratioW, ratioH).rotate(angle));
	_view->setGeometry(QRect(QPoint(), size));

	_transform = {
		.angle = angle,
		.flipped = flipped,
		.zoom = size.width() / float64(_scene->sceneRect().width()),
	};
	_scene->updateZoom(_transform.zoom);
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
		(kMinBrush + float64(kMaxBrush - kMinBrush) * brush.sizeRatio));
}

void Paint::handleMimeData(const QMimeData *data) {
	const auto add = [&](QImage image) {
		if (image.isNull()) {
			return;
		}
		if (!Ui::ValidateThumbDimensions(image.width(), image.height())) {
			_controllers->showBox(
				Box<InformBox>(tr::lng_edit_media_invalid_file(tr::now)));
			return;
		}

		const auto item = std::make_shared<ItemImage>(
			Ui::PixmapFromImage(std::move(image)),
			itemBaseData());
		_scene->addItem(item);
		_scene->clearSelection();
	};

	using Error = Ui::PreparedList::Error;
	auto result = data->hasUrls()
		? Storage::PrepareMediaList(
			data->urls().mid(0, 1),
			_imageSize.width() / 2)
		: Ui::PreparedList(Error::EmptyFile, QString());
	if (result.error == Error::None) {
		add(base::take(result.files.front().preview));
	} else if (data->hasImage()) {
		add(qvariant_cast<QImage>(data->imageData()));
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

} // namespace Editor
