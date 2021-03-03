/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene_item_line.h"

#include <QGraphicsScene>

namespace Editor {
namespace {

QPixmap PathToPixmap(
		const QPainterPath &path,
		const QSize &size,
		const QColor &brushColor,
		float brushSize) {
	auto pixmap = QPixmap(size);
	pixmap.setDevicePixelRatio(cRetinaFactor());
	pixmap.fill(Qt::transparent);
	Painter p(&pixmap);
	p.setPen(QPen(brushColor, brushSize));
	p.drawPath(path);
	return pixmap;
}

} // namespace

ItemLine::ItemLine(
	const QPainterPath &path,
	const QSize &size,
	const QColor &brushColor,
	float brushSize)
: _pixmap(PathToPixmap(path, size, brushColor, brushSize)) {
	Expects(path.capacity() > 0);
}

QRectF ItemLine::boundingRect() const {
	return scene()->sceneRect();
}

void ItemLine::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *,
		QWidget *) {
	p->drawPixmap(0, 0, _pixmap);
}

bool ItemLine::collidesWithItem(
		const QGraphicsItem *,
		Qt::ItemSelectionMode) const {
	return false;
}
bool ItemLine::collidesWithPath(
		const QPainterPath &,
		Qt::ItemSelectionMode) const {
	return false;
}

} // namespace Editor
