/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene_item_base.h"

#include "styles/style_editor.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>

namespace Editor {
namespace {

auto Normalized(float64 angle) {
	return angle
		+ ((std::abs(angle) < 360) ? 0 : (-360 * (angle < 0 ? -1 : 1)));
}

QPen PenStyled(QPen pen, Qt::PenStyle style) {
	pen.setStyle(style);
	return pen;
}

} // namespace

ItemBase::ItemBase(std::shared_ptr<float64> zPtr, int size, int x, int y)
: _lastZ(zPtr)
, _handleSize(st::photoEditorItemHandleSize)
, _innerMargins(
	_handleSize / 2,
	_handleSize / 2,
	_handleSize / 2,
	_handleSize / 2)
, _selectPen(QBrush(Qt::white), 1, Qt::DashLine, Qt::SquareCap, Qt::RoundJoin)
, _selectPenInactive(
	QBrush(Qt::gray),
	1,
	Qt::DashLine,
	Qt::SquareCap,
	Qt::RoundJoin)
, _size(size) {
	setFlags(QGraphicsItem::ItemIsMovable
		| QGraphicsItem::ItemIsSelectable
		| QGraphicsItem::ItemIsFocusable);
	setAcceptHoverEvents(true);
	setPos(x, y);
}

QRectF ItemBase::boundingRect() const {
	return innerRect() + _innerMargins;
}

QRectF ItemBase::innerRect() const {
	return QRectF(-_size / 2, -_size / 2, _size, _size);
}

void ItemBase::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *) {
	if (!(option->state & QStyle::State_Selected)) {
		return;
	}
	PainterHighQualityEnabler hq(*p);
	const auto &pen = (option->state & QStyle::State_HasFocus)
		? _selectPen
		: _selectPenInactive;
	p->setPen(pen);
	p->drawRect(innerRect());

	p->setPen(PenStyled(pen, Qt::SolidLine));
	p->setBrush(st::photoEditorItemBaseHandleFg);
	p->drawEllipse(rightHandleRect());
	p->drawEllipse(leftHandleRect());
}

void ItemBase::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
	if (isHandling()) {
		const auto mousePos = event->pos();
		const auto isLeft = (_handle == HandleType::Left);
		// Resize.
		const auto p = isLeft ? (mousePos * -1) : mousePos;
		const auto dx = int(2.0 * p.x());
		const auto dy = int(2.0 * p.y());
		prepareGeometryChange();
		_size = std::clamp(
			(dx > dy ? dx : dy),
			st::photoEditorItemMinSize,
			st::photoEditorItemMaxSize);

		// Rotate.
		const auto origin = mapToScene(boundingRect().center());
		const auto pos = mapToScene(mousePos);

		const auto diff = pos - origin;
		const auto angle = Normalized((isLeft ? 180 : 0)
			+ (std::atan2(diff.y(), diff.x()) * 180 / M_PI));
		setRotation(angle);
	} else {
		QGraphicsItem::mouseMoveEvent(event);
	}
}

void ItemBase::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
	setCursor(isHandling()
		? Qt::ClosedHandCursor
		: (handleType(event->pos()) != HandleType::None) && isSelected()
		? Qt::OpenHandCursor
		: Qt::ArrowCursor);
	QGraphicsItem::hoverMoveEvent(event);
}

void ItemBase::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	setZValue((*_lastZ)++);
	if (event->button() == Qt::LeftButton) {
		_handle = handleType(event->pos());
		if (isHandling()) {
			setCursor(Qt::ClosedHandCursor);
		}
	} else {
		QGraphicsItem::mousePressEvent(event);
	}
}

void ItemBase::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	if ((event->button() == Qt::LeftButton) && isHandling()) {
		_handle = HandleType::None;
	} else {
		QGraphicsItem::mouseReleaseEvent(event);
	}
}

int ItemBase::type() const {
	return Type;
}

QRectF ItemBase::rightHandleRect() const {
	return QRectF(
		(_size / 2) - (_handleSize / 2),
		0 - (_handleSize / 2),
		_handleSize,
		_handleSize);
}

QRectF ItemBase::leftHandleRect() const {
	return QRectF(
		(-_size / 2) - (_handleSize / 2),
		0 - (_handleSize / 2),
		_handleSize,
		_handleSize);
}

bool ItemBase::isHandling() const {
	return _handle != HandleType::None;
}

int ItemBase::size() const {
	return _size;
}

ItemBase::HandleType ItemBase::handleType(const QPointF &pos) const {
	return rightHandleRect().contains(pos)
		? HandleType::Right
		: leftHandleRect().contains(pos)
		? HandleType::Left
		: HandleType::None;
}

} // namespace Editor
