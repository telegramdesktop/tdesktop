/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_line.h"

#include <QGraphicsScene>
#include <QtGui/QPainter>

namespace Editor {

ItemLine::ItemLine(QPixmap &&pixmap)
: _pixmap(std::move(pixmap))
, _rect(QPointF(), _pixmap.size() / float64(style::DevicePixelRatio())) {
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

const QPixmap &ItemLine::pixmap() const {
	return _pixmap;
}

void ItemLine::setPixmap(QPixmap pixmap) {
	_pixmap = std::move(pixmap);
	update();
}

bool ItemLine::applyEraser(const QPixmap &mask, const QPointF &maskPos) {
	if (mask.isNull()) {
		return false;
	}
	const auto maskSize = mask.size() / float64(mask.devicePixelRatio());
	const auto localTopLeft = maskPos - pos();
	const auto localRect = QRectF(localTopLeft, maskSize);
	if (!localRect.intersects(_rect)) {
		return false;
	}
	auto image = _pixmap.toImage().convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	p.drawPixmap(localTopLeft, mask);
	p.end();
	_pixmap = QPixmap::fromImage(std::move(image));
	update();
	return true;
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
	auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
	saved = {
		.saved = true,
		.status = status(),
	};
}

void ItemLine::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	const auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
	setStatus(saved.status);
}

bool ItemLine::hasState(SaveState state) const {
	const auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
	return saved.saved;
}

} // namespace Editor
