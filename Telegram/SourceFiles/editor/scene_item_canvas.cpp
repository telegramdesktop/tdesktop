/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene_item_canvas.h"

#include <QGraphicsScene>

namespace Editor {

ItemCanvas::ItemCanvas() {
	setAcceptedMouseButtons(0);
}

QRectF ItemCanvas::boundingRect() const {
	return scene()->sceneRect();
}

void ItemCanvas::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *,
		QWidget *) {
	_paintRequest.fire_copy(p);
}

int ItemCanvas::type() const {
	return Type;
}

rpl::producer<not_null<QPainter*>> ItemCanvas::paintRequest() const {
	return _paintRequest.events();
}

bool ItemCanvas::collidesWithItem(
		const QGraphicsItem *,
		Qt::ItemSelectionMode) const {
	return false;
}

bool ItemCanvas::collidesWithPath(
		const QPainterPath &,
		Qt::ItemSelectionMode) const {
	return false;
}

} // namespace Editor
