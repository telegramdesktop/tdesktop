/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene.h"

#include "editor/scene_item_canvas.h"
#include "editor/scene_item_line.h"
#include "ui/rp_widget.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneMouseEvent>

namespace Editor {

Scene::Scene(const QRectF &rect)
: QGraphicsScene(rect)
, _canvas(new ItemCanvas) {
	clearPath();

	addItem(_canvas);

	_canvas->paintRequest(
	) | rpl::start_with_next([=](not_null<QPainter*> p) {
		p->fillRect(sceneRect(), Qt::transparent);

		p->setPen(QPen(_brushData.color, _brushData.size));
		p->drawPath(_path);
	}, _lifetime);
}

void Scene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	QGraphicsScene::mousePressEvent(event);
	if (event->isAccepted() || (event->button() == Qt::RightButton)) {
		return;
	}
	_mousePresses.fire({});
	_path.moveTo(event->scenePos());
	_drawing = true;
}

void Scene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	QGraphicsScene::mouseReleaseEvent(event);
	if (event->isAccepted() || (event->button() == Qt::RightButton)) {
		return;
	}
	_path.lineTo(event->scenePos());
	addLineItem();
	_drawing = false;
}

void Scene::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
	QGraphicsScene::mouseMoveEvent(event);
	if (event->isAccepted()
		|| (event->button() == Qt::RightButton)
		|| !_drawing) {
		return;
	}
	const auto scenePos = event->scenePos();
	_path.lineTo(scenePos);
	_path.moveTo(scenePos);
	_canvas->update();
}

void Scene::addLineItem() {
	if (_path.capacity() < 3) {
		return;
	}
	addItem(new ItemLine(
		_path,
		sceneRect().size().toSize() * cIntRetinaFactor(),
		_brushData.color,
		_brushData.size));
	_canvas->setZValue(++_lastLineZ);
	clearPath();
}

void Scene::applyBrush(const QColor &color, float size) {
	_brushData.color = color;
	_brushData.size = size;
}

void Scene::clearPath() {
	_path = QPainterPath();
	_path.setFillRule(Qt::WindingFill);
}

rpl::producer<> Scene::mousePresses() const {
	return _mousePresses.events();
}

Scene::~Scene() {
}

} // namespace Editor
