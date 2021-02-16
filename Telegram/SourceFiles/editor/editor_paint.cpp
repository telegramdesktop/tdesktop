/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_paint.h"

#include "editor/undo_controller.h"
#include "base/event_filter.h"
#include "styles/style_boxes.h"

#include <QGraphicsItemGroup>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>

namespace Editor {
namespace {

constexpr auto kMaxBrush = 25.;
constexpr auto kMinBrush = 1.;

constexpr auto kViewStyle = "QGraphicsView {\
		background-color: transparent;\
		border: 0px\
	}"_cs;

std::shared_ptr<QGraphicsScene> EnsureScene(PhotoModifications &mods) {
	if (!mods.paint) {
		mods.paint = std::make_shared<QGraphicsScene>(nullptr);
	}
	return mods.paint;
}

auto GroupsFilter(QGraphicsItem *i) {
	return i->type() == QGraphicsItemGroup::Type;
}

} // namespace

Paint::Paint(
	not_null<Ui::RpWidget*> parent,
	PhotoModifications &modifications,
	const QSize &imageSize,
	std::shared_ptr<UndoController> undoController)
: RpWidget(parent)
, _scene(EnsureScene(modifications))
, _view(base::make_unique_q<QGraphicsView>(_scene.get(), this))
, _imageSize(imageSize) {
	Expects(modifications.paint != nullptr);

	keepResult();

	_view->show();
	_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setStyleSheet(kViewStyle.utf8());

	_scene->setSceneRect(0, 0, imageSize.width(), imageSize.height());

	initDrawing();

	// Undo / Redo.
	undoController->performRequestChanges(
	) | rpl::start_with_next([=](const Undo &command) {
		const auto isUndo = (command == Undo::Undo);

		const auto filtered = groups(isUndo
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

	undoController->setCanPerformChanges(rpl::merge(
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
}

void Paint::initDrawing() {
	using Result = base::EventFilterResult;

	auto callback = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		const auto isPress = (type == QEvent::GraphicsSceneMousePress);
		const auto isMove = (type == QEvent::GraphicsSceneMouseMove);
		const auto isRelease = (type == QEvent::GraphicsSceneMouseRelease);
		if (!isPress && !isMove && !isRelease) {
			return Result::Continue;
		}
		const auto e = static_cast<QGraphicsSceneMouseEvent*>(event.get());

		const auto &size = _brushData.size;
		const auto &color = _brushData.color;
		const auto mousePoint = e->scenePos();
		if (isPress) {
			_hasUndo = true;
			clearRedoList();

			auto dot = _scene->addEllipse(
				mousePoint.x() - size / 2,
				mousePoint.y() - size / 2,
				size,
				size,
				QPen(Qt::NoPen),
				QBrush(color));
			_brushData.group = _scene->createItemGroup(
				QList<QGraphicsItem*>{ std::move(dot) });
		}
		if (isMove && _brushData.group) {
			_brushData.group->addToGroup(_scene->addLine(
				_brushData.lastPoint.x(),
				_brushData.lastPoint.y(),
				mousePoint.x(),
				mousePoint.y(),
				QPen(color, size, Qt::SolidLine, Qt::RoundCap)));
		}
		if (isRelease) {
			_brushData.group = nullptr;
		}
		_brushData.lastPoint = mousePoint;

		return Result::Cancel;
	};

	base::install_event_filter(this, _scene.get(), std::move(callback));
}

std::shared_ptr<QGraphicsScene> Paint::saveScene() const {
	return _scene;
}

void Paint::cancel() {
	const auto filtered = groups(Qt::AscendingOrder);
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
	return ranges::any_of(groups(), &QGraphicsItem::isVisible);
}

bool Paint::hasRedo() const {
	return ranges::any_of(
		groups(),
		[=](QGraphicsItem *i) { return isItemHidden(i); });
}

void Paint::clearRedoList() {
	const auto items = groups(Qt::AscendingOrder);
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

std::vector<QGraphicsItem*> Paint::groups(Qt::SortOrder order) const {
	const auto items = _scene->items(order);
	return ranges::views::all(
		items
	) | ranges::views::filter(GroupsFilter) | ranges::to_vector;
}

void Paint::applyBrush(const Brush &brush) {
	_brushData.color = brush.color;
	_brushData.size =
		(kMinBrush + float64(kMaxBrush - kMinBrush) * brush.sizeRatio);
}

} // namespace Editor
