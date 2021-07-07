/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_line.h"

#include <QGraphicsScene>

namespace Editor {

ItemLine::ItemLine(const QPixmap &&pixmap)
: _pixmap(std::move(pixmap))
, _rect(QPointF(), _pixmap.size() / cRetinaFactor()) {
}

QRectF ItemLine::boundingRect() const {
	return _rect;
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

void ItemLine::save(SaveState state) {
	if (state == SaveState::Keep) {
		_keeped = true;
	} else if (state == SaveState::Save) {
		_saved = true;
	}
}

void ItemLine::restore(SaveState state) {
}

bool ItemLine::hasState(SaveState state) const {
	if (state == SaveState::Keep) {
		return _keeped;
	} else if (state == SaveState::Save) {
		return _saved;
	}
	return false;
}

} // namespace Editor
