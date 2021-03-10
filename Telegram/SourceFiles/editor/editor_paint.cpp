/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_paint.h"

#include "editor/scene.h"
#include "editor/scene_item_base.h"
#include "editor/scene_item_canvas.h"
#include "editor/scene_item_sticker.h"
#include "editor/controllers.h"
#include "base/event_filter.h"

#include <QGraphicsView>

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

Paint::Paint(
	not_null<Ui::RpWidget*> parent,
	PhotoModifications &modifications,
	const QSize &imageSize,
	std::shared_ptr<Controllers> controllers)
: RpWidget(parent)
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

	_scene->mousePresses(
	) | rpl::start_with_next([=] {
		_hasUndo = true;
		clearRedoList();
	}, lifetime());

	_scene->addsItem(
	) | rpl::start_with_next([=] {
		updateUndoState();
	}, lifetime());

	// Undo / Redo.
	controllers->undoController->performRequestChanges(
	) | rpl::start_with_next([=](const Undo &command) {
		const auto isUndo = (command == Undo::Undo);

		const auto filtered = _scene->items(isUndo
			? Qt::DescendingOrder
			: Qt::AscendingOrder);

		auto proj = [&](QGraphicsItem *i) {
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
		controllers->stickersPanelController->setShowRequestChanges(
			controllers->stickersPanelController->stickerChosen(
			) | rpl::map_to(std::optional<bool>(false)));

		controllers->stickersPanelController->stickerChosen(
		) | rpl::start_with_next([=](not_null<DocumentData*> document) {
			const auto s = _scene->sceneRect().size();
			const auto size = std::min(s.width(), s.height()) / 2;
			const auto x = s.width() / 2;
			const auto y = s.height() / 2;
			const auto item = new ItemSticker(
				document,
				_zoom.value(),
				_lastZ,
				size,
				x,
				y);
			item->setZValue((*_lastZ)++);
			_scene->addItem(item);
			_scene->clearSelection();
		}, lifetime());
	}
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

	_zoom = size.width() / float64(_scene->sceneRect().width());
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
	const auto filtered = _scene->items(Qt::AscendingOrder);
	if (filtered.empty()) {
		return;
	}

	for (const auto &group : filtered) {
		const auto it = ranges::find(
			_previousItems,
			group,
			&SavedItem::item);
		if (it == end(_previousItems)) {
			_scene->removeItem(group);
		} else {
			it->item->setVisible(!it->undid);
		}
	}

	_itemsToRemove.clear();
}

void Paint::keepResult() {
	for (const auto &item : _itemsToRemove) {
		_scene->removeItem(item);
	}

	const auto items = _scene->items();
	_previousItems = ranges::views::all(
		items
	) | ranges::views::transform([=](QGraphicsItem *i) -> SavedItem {
		return { i, !i->isVisible() };
	}) | ranges::to_vector;
}

bool Paint::hasUndo() const {
	return ranges::any_of(_scene->items(), &QGraphicsItem::isVisible);
}

bool Paint::hasRedo() const {
	return ranges::any_of(
		_scene->items(),
		[=](QGraphicsItem *i) { return isItemHidden(i); });
}

void Paint::clearRedoList() {
	const auto items = _scene->items(Qt::AscendingOrder);
	auto &&filtered = ranges::views::all(
		items
	) | ranges::views::filter(
		[=](QGraphicsItem *i) { return isItemHidden(i); }
	);

	ranges::for_each(std::move(filtered), [&](QGraphicsItem *item) {
		item->hide();
		_itemsToRemove.push_back(item);
	});

	_hasRedo = false;
}

bool Paint::isItemHidden(not_null<QGraphicsItem*> item) const {
	return !item->isVisible() && !isItemToRemove(item);
}

bool Paint::isItemToRemove(not_null<QGraphicsItem*> item) const {
	return ranges::contains(_itemsToRemove, item.get());
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

} // namespace Editor
