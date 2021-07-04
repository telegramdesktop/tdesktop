/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene.h"

#include "editor/scene/scene_item_canvas.h"
#include "editor/scene/scene_item_line.h"
#include "editor/scene/scene_item_sticker.h"
#include "ui/rp_widget.h"

#include <QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

using ItemPtr = Scene::ItemPtr;

bool SkipMouseEvent(not_null<QGraphicsSceneMouseEvent*> event) {
	return event->isAccepted() || (event->button() == Qt::RightButton);
}

} // namespace

Scene::Scene(const QRectF &rect)
: QGraphicsScene(rect)
, _canvas(std::make_shared<ItemCanvas>()) {
	QGraphicsScene::addItem(_canvas.get());
	_canvas->clearPixmap();

	_canvas->grabContentRequests(
	) | rpl::start_with_next([=](ItemCanvas::Content &&content) {
		const auto item = std::make_shared<ItemLine>(
			std::move(content.pixmap));
		item->setPos(content.position);
		addItem(item);
		_canvas->setZValue(++_lastLineZ);
	}, _lifetime);
}

void Scene::cancelDrawing() {
	_canvas->cancelDrawing();
}

void Scene::addItem(std::shared_ptr<NumberedItem> item) {
	if (!item) {
		return;
	}
	item->setNumber(_itemNumber++);
	QGraphicsScene::addItem(item.get());
	_items.push_back(std::move(item));
	_addsItem.fire({});
}

void Scene::removeItem(not_null<QGraphicsItem*> item) {
	const auto it = ranges::find_if(_items, [&](const ItemPtr &i) {
		return i.get() == item;
	});
	if (it == end(_items)) {
		return;
	}
	removeItem(*it);
}

void Scene::removeItem(const ItemPtr &item) {
	// Scene loses ownership of an item.
	QGraphicsScene::removeItem(item.get());

	_items.erase(ranges::remove(_items, item), end(_items));
	_removesItem.fire({});
}

void Scene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	QGraphicsScene::mousePressEvent(event);
	if (SkipMouseEvent(event)) {
		return;
	}
	_canvas->handleMousePressEvent(event);
}

void Scene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	QGraphicsScene::mouseReleaseEvent(event);
	if (SkipMouseEvent(event)) {
		return;
	}
	_canvas->handleMouseReleaseEvent(event);
}

void Scene::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
	QGraphicsScene::mouseMoveEvent(event);
	if (SkipMouseEvent(event)) {
		return;
	}
	_canvas->handleMouseMoveEvent(event);
}

void Scene::applyBrush(const QColor &color, float size) {
	_canvas->applyBrush(color, size);
}

rpl::producer<> Scene::addsItem() const {
	return _addsItem.events();
}

rpl::producer<> Scene::removesItem() const {
	return _removesItem.events();
}

std::vector<ItemPtr> Scene::items(
		Qt::SortOrder order) const {
	auto copyItems = _items;

	ranges::sort(copyItems, [&](ItemPtr a, ItemPtr b) {
		const auto numA = static_cast<NumberedItem*>(a.get())->number();
		const auto numB = static_cast<NumberedItem*>(b.get())->number();
		return (order == Qt::AscendingOrder) ? (numA < numB) : (numA > numB);
	});

	return copyItems;
}

std::vector<MTPInputDocument> Scene::attachedStickers() const {
	const auto allItems = items();

	return ranges::views::all(
		allItems
	) | ranges::views::filter([](const ItemPtr &i) {
		return i->isVisible() && (i->type() == ItemSticker::Type);
	}) | ranges::views::transform([](const ItemPtr &i) {
		return static_cast<ItemSticker*>(i.get())->sticker();
	}) | ranges::to_vector;
}

Scene::~Scene() {
	// Prevent destroying by scene of all items.
	QGraphicsScene::removeItem(_canvas.get());
	for (const auto &item : items()) {
		// Scene loses ownership of an item.
		QGraphicsScene::removeItem(item.get());
	}
}

} // namespace Editor
