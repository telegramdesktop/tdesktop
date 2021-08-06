/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_canvas.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

QRectF NormalizedRect(const QPointF& p1, const QPointF& p2) {
	return QRectF(
		std::min(p1.x(), p2.x()),
		std::min(p1.y(), p2.y()),
		std::abs(p2.x() - p1.x()) + 1,
		std::abs(p2.y() - p1.y()) + 1);
}

std::vector<QPointF> InterpolatedPoints(
		const QPointF &startPoint,
		const QPointF &endPoint) {
	std::vector<QPointF> points;

	const auto x1 = startPoint.x();
	const auto y1 = startPoint.y();
	const auto x2 = endPoint.x();
	const auto y2 = endPoint.y();

	// Difference of x and y values.
	const auto dx = x2 - x1;
	const auto dy = y2 - y1;

	// Absolute values of differences.
	const auto ix = std::abs(dx);
	const auto iy = std::abs(dy);

	// Larger of the x and y differences.
	const auto inc = ix > iy ? ix : iy;

	// Plot location.
	auto plotx = x1;
	auto ploty = y1;

	auto x = 0;
	auto y = 0;

	points.push_back(QPointF(plotx, ploty));

	for (auto i = 0; i <= inc; i++) {
		const auto xInc = x > inc;
		const auto yInc = y > inc;

		x += ix;
		y += iy;

		if (xInc) {
			x -= inc;
			plotx += 1 * ((dx < 0) ? -1 : 1);
		}

		if (yInc) {
			y -= inc;
			ploty += 1 * ((dy < 0) ? -1 : 1);
		}

		if (xInc || yInc) {
			points.push_back(QPointF(plotx, ploty));
		}
	}
	return points;
}

} // namespace

ItemCanvas::ItemCanvas() {
	setAcceptedMouseButtons({});
}

void ItemCanvas::clearPixmap() {
	_hq = nullptr;
	_p = nullptr;

	_pixmap = QPixmap(
		(scene()->sceneRect().size() * cIntRetinaFactor()).toSize());
	_pixmap.setDevicePixelRatio(cRetinaFactor());
	_pixmap.fill(Qt::transparent);

	_p = std::make_unique<Painter>(&_pixmap);
	_hq = std::make_unique<PainterHighQualityEnabler>(*_p);
	_p->setPen(Qt::NoPen);
	_p->setBrush(_brushData.color);
}

void ItemCanvas::applyBrush(const QColor &color, float size) {
	_brushData.color = color;
	_brushData.size = size;
	_p->setBrush(color);
	_brushMargins = QMarginsF(size, size, size, size);// / 2.;
}

QRectF ItemCanvas::boundingRect() const {
	return scene()->sceneRect();
}

void ItemCanvas::computeContentRect(const QPointF &p) {
	if (!scene()) {
		return;
	}
	const auto sceneSize = scene()->sceneRect().size();
	_contentRect = QRectF(
		QPointF(
			std::clamp(p.x() - _brushMargins.left(), 0., _contentRect.x()),
			std::clamp(p.y() - _brushMargins.top(), 0., _contentRect.y())
		),
		QPointF(
			std::clamp(
				p.x() + _brushMargins.right(),
				_contentRect.x() + _contentRect.width(),
				sceneSize.width()),
			std::clamp(
				p.y() + _brushMargins.bottom(),
				_contentRect.y() + _contentRect.height(),
				sceneSize.height())
		));
}

void ItemCanvas::drawLine(
		const QPointF &currentPoint,
		const QPointF &lastPoint) {
	const auto halfBrushSize = _brushData.size / 2.;
	const auto points = InterpolatedPoints(lastPoint, currentPoint);

	_rectToUpdate |= NormalizedRect(currentPoint, lastPoint) + _brushMargins;

	for (const auto &point : points) {
		_p->drawEllipse(point, halfBrushSize, halfBrushSize);
	}
}

void ItemCanvas::handleMousePressEvent(
		not_null<QGraphicsSceneMouseEvent*> e) {
	_lastPoint = e->scenePos();
	_contentRect = QRectF(_lastPoint, _lastPoint);
	_drawing = true;
}

void ItemCanvas::handleMouseMoveEvent(
		not_null<QGraphicsSceneMouseEvent*> e) {
	if (!_drawing) {
		return;
	}
	const auto scenePos = e->scenePos();
	drawLine(scenePos, _lastPoint);
	update(_rectToUpdate);
	computeContentRect(scenePos);
	_lastPoint = scenePos;
}

void ItemCanvas::handleMouseReleaseEvent(
		not_null<QGraphicsSceneMouseEvent*> e) {
	if (!_drawing) {
		return;
	}
	_drawing = false;

	if (_contentRect.isValid()) {
		const auto scaledContentRect = QRectF(
			_contentRect.x() * cRetinaFactor(),
			_contentRect.y() * cRetinaFactor(),
			_contentRect.width() * cRetinaFactor(),
			_contentRect.height() * cRetinaFactor());

		_grabContentRequests.fire({
			.pixmap = _pixmap.copy(scaledContentRect.toRect()),
			.position = _contentRect.topLeft(),
		});
	}
	clearPixmap();
	update();
}

void ItemCanvas::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *,
		QWidget *) {
	p->fillRect(_rectToUpdate, Qt::transparent);
	p->drawPixmap(0, 0, _pixmap);
	_rectToUpdate = QRectF();
}

rpl::producer<ItemCanvas::Content> ItemCanvas::grabContentRequests() const {
	return _grabContentRequests.events();
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

void ItemCanvas::cancelDrawing() {
	_drawing = false;
	_contentRect = QRectF();
	clearPixmap();
	update();
}

ItemCanvas::~ItemCanvas() {
	_hq = nullptr;
	_p = nullptr;
}

} // namespace Editor
