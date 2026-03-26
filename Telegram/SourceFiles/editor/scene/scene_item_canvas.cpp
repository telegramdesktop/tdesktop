/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_canvas.h"
#include "styles/style_editor.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QtMath>

namespace Editor {
namespace {

constexpr auto kMinPointDistanceBase = 2.0;
constexpr auto kMaxPointDistance = 15.0;
constexpr auto kSmoothingStrength = 0.5;
constexpr auto kSegmentOverlap = 3;
constexpr auto kOriginStartJumpRatio = 0.25;
constexpr auto kHalfStrength = kSmoothingStrength / 2.0;
constexpr auto kInvStrength = 1.0 - kSmoothingStrength;

[[nodiscard]] float64 PointDistance(const QPointF &a, const QPointF &b) {
	const auto dx = a.x() - b.x();
	const auto dy = a.y() - b.y();
	return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] bool IsValidPoint(const QPointF &point) {
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

} // namespace

ItemCanvas::ItemCanvas() {
	setAcceptedMouseButtons({});
}

void ItemCanvas::clearPixmap() {
	_hq = nullptr;
	_p = nullptr;

	_pixmap = QPixmap(
		(scene()->sceneRect().size() * style::DevicePixelRatio()).toSize());
	_pixmap.setDevicePixelRatio(style::DevicePixelRatio());
	_pixmap.fill(Qt::transparent);

	_p = std::make_unique<Painter>(&_pixmap);
	_hq = std::make_unique<PainterHighQualityEnabler>(*_p);
	_p->setPen(Qt::NoPen);
	_p->setBrush(_brushData.color);
}

void ItemCanvas::applyBrush(const QColor &color, float size, Brush::Tool tool) {
	_brushData.color = color;
	_brushData.size = size;
	_brushData.tool = tool;
	_p->setBrush(color);
	const auto width = strokeWidth(1.0);
	const auto extra = (tool == Brush::Tool::Arrow)
		? arrowHeadLength()
		: 0.;
	const auto margin = width + extra;
	_brushMargins = QMarginsF(margin, margin, margin, margin);
}

QRectF ItemCanvas::boundingRect() const {
	return scene()->sceneRect();
}

void ItemCanvas::computeContentRect(const QPointF &p) {
	if (!scene() || !IsValidPoint(p)) {
		return;
	}
	const auto sceneSize = scene()->sceneRect().size();
	const auto contentLeft = std::max(0., _contentRect.x());
	const auto contentTop = std::max(0., _contentRect.y());
	_contentRect = QRectF(
		QPointF(
			std::clamp(p.x() - _brushMargins.left(), 0., contentLeft),
			std::clamp(p.y() - _brushMargins.top(), 0., contentTop)),
		QPointF(
			std::clamp(
				p.x() + _brushMargins.right(),
				contentLeft + _contentRect.width(),
				sceneSize.width()),
			std::clamp(
				p.y() + _brushMargins.bottom(),
				contentTop + _contentRect.height(),
				sceneSize.height())));
}

std::vector<ItemCanvas::StrokePoint> ItemCanvas::smoothStroke(
		const std::vector<StrokePoint> &points) const {
	if (points.size() < 4) {
		return points;
	}
	auto result = std::vector<StrokePoint>();
	result.reserve(points.size());
	result.push_back(points[0]);
	result.push_back(points[1]);
	for (auto i = 2; i < int(points.size()) - 1; ++i) {
		const auto &prev = points[i - 1].pos;
		const auto &curr = points[i].pos;
		const auto &next = points[i + 1].pos;
		const auto smoothed = curr * kInvStrength
			+ (prev + next) * kHalfStrength;
		result.push_back({
			.pos = smoothed,
			.pressure = points[i].pressure,
			.time = points[i].time,
		});
	}
	result.push_back(points.back());
	return result;
}

float64 ItemCanvas::strokeWidth(float64 pressure) const {
	auto width = _brushData.size * pressure;
	if (_brushData.tool == Brush::Tool::Marker) {
		width *= st::photoEditorMarkerSizeMultiplier;
	}
	return width;
}

QColor ItemCanvas::strokeColor() const {
	if (_brushData.tool == Brush::Tool::Eraser) {
		return QColor(0, 0, 0, 255);
	}
	auto color = _brushData.color;
	if (_brushData.tool == Brush::Tool::Marker) {
		color.setAlphaF(color.alphaF() * st::photoEditorMarkerOpacity);
	}
	return color;
}

float64 ItemCanvas::arrowHeadLength() const {
	return _brushData.size * st::photoEditorArrowHeadLengthFactor;
}

void ItemCanvas::renderSegment(
		const std::vector<StrokePoint> &points,
		int startIdx) {
	if (points.size() < 2 || startIdx >= int(points.size()) - 1) {
		return;
	}
	if (!IsValidPoint(points.back().pos)) {
		return;
	}
	auto path = QPainterPath();
	const auto effectiveStart = std::max(0, startIdx);
	if (!IsValidPoint(points[effectiveStart].pos)) {
		return;
	}
	path.moveTo(points[effectiveStart].pos);
	for (auto i = effectiveStart; i < int(points.size()) - 1; ++i) {
		const auto &p0 = points[i].pos;
		const auto &p1 = points[i + 1].pos;
		if (!IsValidPoint(p0) || !IsValidPoint(p1)) {
			return;
		}
		const auto ctrl = (p0 + p1) / 2.0;
		if (!IsValidPoint(ctrl)) {
			return;
		}
		if (i == effectiveStart) {
			path.lineTo(ctrl);
		} else {
			path.quadTo(p0, ctrl);
		}
	}
	path.lineTo(points.back().pos);
	const auto count = points.size() - std::max(0, startIdx);
	const auto avgPressure = count > 0
		? std::accumulate(
			points.begin() + std::max(0, startIdx),
			points.end(),
			0.0,
			[](float64 sum, const StrokePoint &p) {
				return sum + p.pressure;
			}) / count
		: 1.0;
	const auto width = strokeWidth(avgPressure);
	auto stroker = QPainterPathStroker();
	stroker.setWidth(width);
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);
	const auto outline = stroker.createStroke(path);
	const auto color = strokeColor();
	if (_brushData.tool == Brush::Tool::Marker) {
		_p->save();
		_p->setCompositionMode(QPainter::CompositionMode_Source);
		_p->fillPath(outline, color);
		_p->restore();
	} else {
		_p->fillPath(outline, color);
	}
	_rectToUpdate |= outline.boundingRect() + _brushMargins;
}

void ItemCanvas::drawIncrementalStroke() {
	if (_currentStroke.size() < 2) {
		return;
	}
	const auto startIdx = std::max(
		0,
		_lastRenderedIndex - kSegmentOverlap);
	auto segment = std::vector<StrokePoint>(
		_currentStroke.begin() + startIdx,
		_currentStroke.end());
	if (segment.size() < 2) {
		return;
	}
	if (segment.size() >= 4) {
		for (auto i = 0; i < 2; ++i) {
			segment = smoothStroke(segment);
		}
	}
	renderSegment(
		segment,
		std::min(kSegmentOverlap, int(segment.size()) - 1));
	_lastRenderedIndex = std::max(
		0,
		int(_currentStroke.size()) - kSegmentOverlap);
}

void ItemCanvas::drawArrowHead() {
	if (_brushData.tool != Brush::Tool::Arrow) {
		return;
	}
	if (_currentStroke.size() < 2) {
		return;
	}
	const auto tip = _currentStroke.back().pos;
	const auto minDistance = _brushData.size
		* st::photoEditorArrowHeadMinDistanceFactor;
	auto base = _currentStroke.front().pos;
	for (auto i = int(_currentStroke.size()) - 2; i >= 0; --i) {
		const auto &p = _currentStroke[i].pos;
		if (PointDistance(tip, p) >= minDistance) {
			base = p;
			break;
		}
	}
	auto direction = tip - base;
	const auto length = std::sqrt(
		direction.x() * direction.x()
			+ direction.y() * direction.y());
	if (length <= 0.) {
		return;
	}
	direction /= length;
	const auto angle = qDegreesToRadians(
		double(st::photoEditorArrowHeadAngleDegrees));
	const auto sinA = std::sin(angle);
	const auto cosA = std::cos(angle);
	const auto rotate = [&](const QPointF &v, float64 s, float64 c) {
		return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
	};
	const auto headLength = arrowHeadLength();
	const auto left = tip - rotate(direction, sinA, cosA) * headLength;
	const auto right = tip - rotate(direction, -sinA, cosA) * headLength;

	auto arrow = QPainterPath();
	arrow.moveTo(tip);
	arrow.lineTo(left);
	arrow.moveTo(tip);
	arrow.lineTo(right);
	auto stroker = QPainterPathStroker();
	stroker.setWidth(strokeWidth(_currentStroke.back().pressure));
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);
	const auto outline = stroker.createStroke(arrow);
	_p->fillPath(outline, strokeColor());
	_rectToUpdate |= outline.boundingRect() + _brushMargins;
	computeContentRect(left);
	computeContentRect(right);
}

void ItemCanvas::addStrokePoint(const QPointF &point, int64 time) {
	if (!IsValidPoint(point)) {
		return;
	}
	if ((_currentStroke.size() == 1)
		&& (_currentStroke.front().pos == QPointF())
		&& (point != QPointF())) {
		if (const auto currentScene = scene()) {
			const auto size = currentScene->sceneRect().size();
			const auto maxStartJump = std::max(size.width(), size.height())
				* kOriginStartJumpRatio;
			if (PointDistance(_currentStroke.front().pos, point)
				> maxStartJump) {
				_currentStroke.front() = {
					.pos = point,
					.pressure = 1.0,
					.time = time,
				};
				_lastPointTime = time;
				_contentRect = QRectF(point, point) + _brushMargins;
				return;
			}
		}
	}
	if (!_currentStroke.empty()) {
		const auto distance = PointDistance(
			point,
			_currentStroke.back().pos);
		const auto minDistance = (_zoom > 1.0)
			? (kMinPointDistanceBase / _zoom)
			: (kMinPointDistanceBase * _zoom);
		if (distance < minDistance) {
			return;
		}
		const auto maxDistance = (_zoom > 1.0)
			? (kMaxPointDistance / _zoom)
			: kMaxPointDistance;
		if (distance > maxDistance) {
			const auto steps = int(std::ceil(distance / maxDistance));
			const auto lastPos = _currentStroke.back().pos;
			const auto lastPressure = _currentStroke.back().pressure;
			for (auto i = 1; i < steps; ++i) {
				const auto t = float64(i) / steps;
				const auto interpolated = lastPos * (1.0 - t) + point * t;
				const auto interpTime = _lastPointTime
					+ int64((time - _lastPointTime) * t);
				_currentStroke.push_back({
					.pos = interpolated,
					.pressure = lastPressure,
					.time = interpTime,
				});
			}
		}
	}
	_currentStroke.push_back({
		.pos = point,
		.pressure = 1.0,
		.time = time,
	});
	_lastPointTime = time;
	computeContentRect(point);
}

void ItemCanvas::handleMousePressEvent(
		not_null<QGraphicsSceneMouseEvent*> e) {
	_lastPoint = e->scenePos();
	if (!IsValidPoint(_lastPoint)) {
		_drawing = false;
		return;
	}
	_rectToUpdate = QRectF();
	_contentRect = QRectF();
	_currentStroke.clear();
	_lastRenderedIndex = 0;
	_lastPointTime = 0;
	const auto now = crl::now();
	addStrokePoint(_lastPoint, now);
	_contentRect = QRectF(_lastPoint, _lastPoint) + _brushMargins;
	_drawing = true;
}

void ItemCanvas::handleMouseMoveEvent(
		not_null<QGraphicsSceneMouseEvent*> e) {
	if (!_drawing) {
		return;
	}
	const auto scenePos = e->scenePos();
	if (!IsValidPoint(scenePos)) {
		return;
	}
	const auto now = crl::now();
	addStrokePoint(scenePos, now);
	_lastPoint = scenePos;
	if (_currentStroke.size() - _lastRenderedIndex >= 3) {
		drawIncrementalStroke();
		update(_rectToUpdate);
	}
}

void ItemCanvas::handleMouseReleaseEvent(
		not_null<QGraphicsSceneMouseEvent*> e) {
	if (!_drawing) {
		return;
	}
	_drawing = false;
	drawIncrementalStroke();
	drawArrowHead();
	update(_rectToUpdate);
	if (_contentRect.isValid()) {
		const auto scaledContentRect = QRectF(
			_contentRect.x() * style::DevicePixelRatio(),
			_contentRect.y() * style::DevicePixelRatio(),
			_contentRect.width() * style::DevicePixelRatio(),
			_contentRect.height() * style::DevicePixelRatio());
		_grabContentRequests.fire({
			.pixmap = _pixmap.copy(scaledContentRect.toRect()),
			.position = _contentRect.topLeft(),
			.clear = (_brushData.tool == Brush::Tool::Eraser),
		});
	}
	_currentStroke.clear();
	_lastRenderedIndex = 0;
	_lastPointTime = 0;
	_currentPath = QPainterPath();
	clearPixmap();
	update();
}

void ItemCanvas::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *,
		QWidget *) {
	p->fillRect(_rectToUpdate, Qt::transparent);
	if (_brushData.tool == Brush::Tool::Eraser) {
		p->save();
		p->setOpacity(st::photoEditorEraserPreviewOpacity);
		p->drawPixmap(0, 0, _pixmap);
		p->restore();
	} else {
		p->drawPixmap(0, 0, _pixmap);
	}
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
	_currentStroke.clear();
	_lastRenderedIndex = 0;
	_lastPointTime = 0;
	_currentPath = QPainterPath();
	_contentRect = QRectF();
	clearPixmap();
	update();
}

void ItemCanvas::updateZoom(float64 zoom) {
	_zoom = zoom;
}

ItemCanvas::~ItemCanvas() {
	_hq = nullptr;
	_p = nullptr;
}

} // namespace Editor
