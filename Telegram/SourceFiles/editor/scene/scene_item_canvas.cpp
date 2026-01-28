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

constexpr auto kMinPointDistanceBase = 2.0;
constexpr auto kMaxPointDistance = 15.0;
constexpr auto kSmoothingStrength = 0.5;
constexpr auto kPressureDecay = 0.95;
constexpr auto kMinPressure = 0.3;
constexpr auto kBatchUpdateInterval = 16;
constexpr auto kSegmentOverlap = 3;
constexpr auto kHalfStrength = kSmoothingStrength / 2.0;
constexpr auto kInvStrength = 1.0 - kSmoothingStrength;

[[nodiscard]] float64 PointDistance(const QPointF &a, const QPointF &b) {
	const auto dx = a.x() - b.x();
	const auto dy = a.y() - b.y();
	return std::sqrt(dx * dx + dy * dy);
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
			std::clamp(p.y() - _brushMargins.top(), 0., _contentRect.y())),
		QPointF(
			std::clamp(
				p.x() + _brushMargins.right(),
				_contentRect.x() + _contentRect.width(),
				sceneSize.width()),
			std::clamp(
				p.y() + _brushMargins.bottom(),
				_contentRect.y() + _contentRect.height(),
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

void ItemCanvas::renderSegment(
		const std::vector<StrokePoint> &points,
		int startIdx) {
	if (points.size() < 2 || startIdx >= int(points.size()) - 1) {
		return;
	}
	auto path = QPainterPath();
	const auto effectiveStart = std::max(0, startIdx);
	path.moveTo(points[effectiveStart].pos);
	for (auto i = effectiveStart; i < int(points.size()) - 1; ++i) {
		const auto &p0 = points[i].pos;
		const auto &p1 = points[i + 1].pos;
		const auto ctrl = (p0 + p1) / 2.0;
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
	const auto width = _brushData.size * avgPressure;
	auto stroker = QPainterPathStroker();
	stroker.setWidth(width);
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);
	const auto outline = stroker.createStroke(path);
	_p->fillPath(outline, _brushData.color);
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

void ItemCanvas::addStrokePoint(const QPointF &point, int64 time) {
	if (!_currentStroke.empty()) {
		const auto distance = PointDistance(
			point,
			_currentStroke.back().pos);
		const auto minDistance = kMinPointDistanceBase * std::min(1.0, _zoom);
		if (distance < minDistance) {
			return;
		}
		if (distance > kMaxPointDistance) {
			const auto steps = int(std::ceil(distance / kMaxPointDistance));
			const auto &lastPos = _currentStroke.back().pos;
			const auto &lastPressure = _currentStroke.back().pressure;
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
	const auto timeDelta = _lastPointTime
		? std::max(int64(1), time - _lastPointTime)
		: kBatchUpdateInterval;
	const auto speed = !_currentStroke.empty()
		? PointDistance(point, _currentStroke.back().pos) / timeDelta
		: 0.0;
	const auto pressureFromSpeed = std::clamp(
		1.0 - speed * 0.1,
		kMinPressure,
		1.0);
	const auto pressure = _currentStroke.empty()
		? 1.0
		: _currentStroke.back().pressure * kPressureDecay
			+ pressureFromSpeed * (1.0 - kPressureDecay);
	_currentStroke.push_back({
		.pos = point,
		.pressure = pressure,
		.time = time,
	});
	_lastPointTime = time;
	computeContentRect(point);
}

void ItemCanvas::handleMousePressEvent(
		not_null<QGraphicsSceneMouseEvent*> e) {
	_lastPoint = e->scenePos();
	_rectToUpdate = QRectF();
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
