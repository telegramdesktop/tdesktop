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
#include "editor/scene/scene_item_base.h"
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

using ItemPtr = Scene::ItemPtr;

Paint::Paint(
	not_null<Ui::RpWidget*> parent,
	PhotoModifications &modifications,
	const QSize &imageSize,
	std::shared_ptr<Controllers> controllers)
: RpWidget(parent)
, _controllers(controllers)
, _lastZ(std::make_shared<float64>(9000.))
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
		const auto isUndo = (command == Undo::Undo);

		const auto filtered = _scene->items(isUndo
			? Qt::DescendingOrder
			: Qt::AscendingOrder);

		auto proj = [&](const ItemPtr &i) {
			return isUndo ? i->isVisible() : isItemHidden(i);
		};
		const auto it = ranges::find_if(filtered, std::move(proj));
		if (it != filtered.end()) {
			(*it)->setVisible(!isUndo);
		}

		_hasUndo = hasUndo();
		_hasRedo = hasRedo();
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
			const auto s = _scene->sceneRect().size();
			const auto size = std::min(s.width(), s.height()) / 2;
			const auto x = s.width() / 2;
			const auto y = s.height() / 2;
			const auto item = std::make_shared<ItemSticker>(
				document,
				_transform.zoom.value(),
				_lastZ,
				size,
				x,
				y);
			item->setFlip(_transform.flipped);
			item->setRotation(-_transform.angle);
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
}

std::shared_ptr<Scene> Paint::saveScene() const {
	_scene->clearSelection();
	return _scene->items().empty()
		? nullptr
		: ranges::none_of(_scene->items(), &QGraphicsItem::isVisible)
		? nullptr
		: _scene;
}

void Paint::cancel() {
	_scene->clearSelection();
	_scene->cancelDrawing();

	const auto filtered = _scene->items(Qt::AscendingOrder);
	if (filtered.empty()) {
		return;
	}

	for (const auto &item : filtered) {
		const auto it = ranges::find(
			_previousItems,
			item,
			&SavedItem::item);
		if (it == end(_previousItems)) {
			_scene->removeItem(item);
		} else {
			it->item->setVisible(!it->undid);
		}
	}

	_itemsToRemove.clear();
}

void Paint::keepResult() {
	_scene->clearSelection();
	_scene->cancelDrawing();

	for (const auto &item : _itemsToRemove) {
		_scene->removeItem(item);
	}
	_itemsToRemove.clear();

	const auto items = _scene->items();
	_previousItems = ranges::views::all(
		items
	) | ranges::views::transform([=](ItemPtr i) -> SavedItem {
		return { i, !i->isVisible() };
	}) | ranges::to_vector;
}

bool Paint::hasUndo() const {
	return ranges::any_of(_scene->items(), &QGraphicsItem::isVisible);
}

bool Paint::hasRedo() const {
	return ranges::any_of(
		_scene->items(),
		[=](const ItemPtr &i) { return isItemHidden(i); });
}

void Paint::clearRedoList() {
	const auto items = _scene->items(Qt::AscendingOrder);
	auto &&filtered = ranges::views::all(
		items
	) | ranges::views::filter(
		[=](const ItemPtr &i) { return isItemHidden(i); }
	);

	ranges::for_each(std::move(filtered), [&](ItemPtr item) {
		item->hide();
		_itemsToRemove.push_back(item);
	});

	_hasRedo = false;
}

bool Paint::isItemHidden(const ItemPtr &item) const {
	return !item->isVisible() && !isItemToRemove(item);
}

bool Paint::isItemToRemove(const ItemPtr &item) const {
	return ranges::contains(_itemsToRemove, item);
}

void Paint::updateUndoState() {
	_hasUndo = hasUndo();
	_hasRedo = hasRedo();
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
		const auto s = _scene->sceneRect().size();
		const auto size = std::min(s.width(), s.height()) / 2;
		const auto x = s.width() / 2;
		const auto y = s.height() / 2;
		if (!Ui::ValidateThumbDimensions(image.width(), image.height())) {
			_controllers->showBox(
				Box<InformBox>(tr::lng_edit_media_invalid_file(tr::now)));
			return;
		}

		const auto item = std::make_shared<ItemImage>(
			Ui::PixmapFromImage(std::move(image)),
			_transform.zoom.value(),
			_lastZ,
			size,
			x,
			y);
		item->setFlip(_transform.flipped);
		item->setRotation(-_transform.angle);
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

} // namespace Editor
