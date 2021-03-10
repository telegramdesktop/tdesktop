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

int NumberedItem::number() const {
	return _number;
}

void NumberedItem::setNumber(int number) {
	_number = number;
}

ItemBase::ItemBase(
	rpl::producer<float64> zoomValue,
	std::shared_ptr<float64> zPtr,
	int size,
	int x,
	int y)
: _lastZ(zPtr)
, _selectPen(QBrush(Qt::white), 1, Qt::DashLine, Qt::SquareCap, Qt::RoundJoin)
, _selectPenInactive(
	QBrush(Qt::gray),
	1,
	Qt::DashLine,
	Qt::SquareCap,
	Qt::RoundJoin)
, _horizontalSize(size) {
	setFlags(QGraphicsItem::ItemIsMovable
		| QGraphicsItem::ItemIsSelectable
		| QGraphicsItem::ItemIsFocusable);
	setAcceptHoverEvents(true);
	setPos(x, y);

	const auto &handleSize = st::photoEditorItemHandleSize;
	std::move(
		zoomValue
	) | rpl::start_with_next([=](float64 zoom) {
		_scaledHandleSize = handleSize / zoom;
		_scaledInnerMargins = QMarginsF(
			_scaledHandleSize,
			_scaledHandleSize,
			_scaledHandleSize,
			_scaledHandleSize) * 0.5;
	}, _lifetime);
}

QRectF ItemBase::boundingRect() const {
	return innerRect() + _scaledInnerMargins;
}

QRectF ItemBase::contentRect() const {
	return innerRect() - _scaledInnerMargins;
}

QRectF ItemBase::innerRect() const {
	const auto &hSize = _horizontalSize;
	const auto &vSize = _verticalSize;
	return QRectF(-hSize / 2, -vSize / 2, hSize, vSize);
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
		_horizontalSize = std::clamp(
			(dx > dy ? dx : dy),
			st::photoEditorItemMinSize,
			st::photoEditorItemMaxSize);
		updateVerticalSize();

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
		(_horizontalSize / 2) - (_scaledHandleSize / 2),
		0 - (_scaledHandleSize / 2),
		_scaledHandleSize,
		_scaledHandleSize);
}

QRectF ItemBase::leftHandleRect() const {
	return QRectF(
		(-_horizontalSize / 2) - (_scaledHandleSize / 2),
		0 - (_scaledHandleSize / 2),
		_scaledHandleSize,
		_scaledHandleSize);
}

bool ItemBase::isHandling() const {
	return _handle != HandleType::None;
}

float64 ItemBase::size() const {
	return _horizontalSize;
}

void ItemBase::updateVerticalSize() {
	_verticalSize = _horizontalSize * _aspectRatio;
}

void ItemBase::setAspectRatio(float64 aspectRatio) {
	_aspectRatio = aspectRatio;
	updateVerticalSize();
}

ItemBase::HandleType ItemBase::handleType(const QPointF &pos) const {
	return rightHandleRect().contains(pos)
		? HandleType::Right
		: leftHandleRect().contains(pos)
		? HandleType::Left
		: HandleType::None;
}

} // namespace Editor
