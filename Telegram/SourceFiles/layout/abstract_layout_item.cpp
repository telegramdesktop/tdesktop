/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout/abstract_layout_item.h"

AbstractLayoutItem::AbstractLayoutItem() {
}

int AbstractLayoutItem::maxWidth() const {
	return _maxw;
}
int AbstractLayoutItem::minHeight() const {
	return _minh;
}

int AbstractLayoutItem::resizeGetHeight(int width) {
	_width = qMin(width, _maxw);
	_height = _minh;
	return _height;
}

int AbstractLayoutItem::width() const {
	return _width;
}
int AbstractLayoutItem::height() const {
	return _height;
}

void AbstractLayoutItem::setPosition(int position) {
	_position = position;
}
int AbstractLayoutItem::position() const {
	return _position;
}

bool AbstractLayoutItem::hasPoint(QPoint point) const {
	return QRect(0, 0, width(), height()).contains(point);
}

AbstractLayoutItem::~AbstractLayoutItem() {
}
