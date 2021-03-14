/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene.h"

#include "editor/scene_item_canvas.h"
#include "editor/scene_item_line.h"
#include "editor/scene_item_sticker.h"
#include "ui/rp_widget.h"

#include <QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

bool SkipMouseEvent(not_null<QGraphicsSceneMouseEvent*> event) {
	return event->isAccepted() || (event->button() == Qt::RightButton);
}

} // namespace

Scene::Scene(const QRectF &rect)
: QGraphicsScene(rect)
, _canvas(new ItemCanvas) {
	QGraphicsScene::addItem(_canvas);
	_canvas->clearPixmap();

	_canvas->grabContentRequests(
	) | rpl::start_with_next([=](ItemCanvas::Content &&content) {
		const auto item = new ItemLine(std::move(content.pixmap));
		item->setPos(content.position);
		addItem(item);
		_canvas->setZValue(++_lastLineZ);
	}, _lifetime);
}

void Scene::addItem(not_null<NumberedItem*> item) {
	item->setNumber(_itemNumber++);
	QGraphicsScene::addItem(item);
	_addsItem.fire({});
}

void Scene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	QGraphicsScene::mousePressEvent(event);
	if (SkipMouseEvent(event)) {
		return;
	}
	_canvas->handleMousePressEvent(event);
	_mousePresses.fire({});
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

rpl::producer<> Scene::mousePresses() const {
	return _mousePresses.events();
}

rpl::producer<> Scene::addsItem() const {
	return _addsItem.events();
}

std::vector<QGraphicsItem*> Scene::items(Qt::SortOrder order) const {
	using Item = QGraphicsItem;
	auto rawItems = QGraphicsScene::items();

	auto filteredItems = ranges::views::all(
		rawItems
	) | ranges::views::filter([](Item *i) {
		return i->type() != ItemCanvas::Type;
	}) | ranges::to_vector;

	ranges::sort(filteredItems, [&](not_null<Item*> a, not_null<Item*> b) {
		const auto numA = qgraphicsitem_cast<NumberedItem*>(a)->number();
		const auto numB = qgraphicsitem_cast<NumberedItem*>(b)->number();
		return (order == Qt::AscendingOrder) ? (numA < numB) : (numA > numB);
	});

	return filteredItems;
}

std::vector<MTPInputDocument> Scene::attachedStickers() const {
	const auto allItems = items();

	return ranges::views::all(
		allItems
	) | ranges::views::filter([](QGraphicsItem *i) {
		return i->isVisible() && (i->type() == ItemSticker::Type);
	}) | ranges::views::transform([](QGraphicsItem *i) {
		return qgraphicsitem_cast<ItemSticker*>(i)->sticker();
	}) | ranges::to_vector;
}

Scene::~Scene() {
}

} // namespace Editor
