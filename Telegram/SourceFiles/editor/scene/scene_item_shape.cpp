/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_shape.h"

#include "ui/painter.h"
#include "ui/rect.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace Editor {
namespace {

constexpr auto kStarInnerRatio = 0.4316;
constexpr auto kRoundingRatio = 0.234;
constexpr auto kBubbleBodyRatio = 2. / 2.25;
constexpr auto kBubbleTailRatio = 0.75;
constexpr auto kArrowHeadRatio = 0.16;
constexpr auto kArrowHeadStrokeFactor = 2.5;
constexpr auto kArrowHeadAngle = 30. * M_PI / 180.;
constexpr auto kArrowMaxBendRatio = 0.35;
constexpr auto kMinShapeSide = 1.;
constexpr auto kStarBoxWidth = 1.902113;
constexpr auto kStarBoxHeight = 1.809017;

} // namespace

ItemShape::ItemShape(
	ShapeType shape,
	const QColor &color,
	float64 strokeWidth,
	bool fill,
	Data data)
: ItemBase(std::move(data))
, _shape(shape)
, _color(color)
, _strokeWidth(strokeWidth)
, _fill(fill) {
	if (_shape == ShapeType::Star) {
		setAspectRatio(kStarBoxHeight / kStarBoxWidth);
	} else if (_shape == ShapeType::Arrow) {
		setVerticalMinimumEnabled(false);
		updateArrowFrame();
	}
}

void ItemShape::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	PainterHighQualityEnabler hq(*p);
	p->save();
	p->setPen(QPen(
		_color,
		_strokeWidth,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	if (_shape == ShapeType::Arrow) {
		p->setBrush(Qt::NoBrush);
		p->drawPath(arrowShaftPath());
		p->setBrush(_color);
		p->drawPath(arrowHeadPath());
	} else {
		p->setBrush(_fill ? QBrush(_color) : QBrush());
		p->drawPath(shapePath());
	}
	p->restore();
	ItemBase::paint(p, option, w);
	if ((_shape == ShapeType::Arrow)
		&& (option->state & QStyle::State_Selected)) {
		paintHandle(
			p,
			bendHandleRect(),
			option->state & QStyle::State_HasFocus);
	}
}

QRectF ItemShape::boundingRect() const {
	const auto result = ItemBase::boundingRect();
	if (!_belowMinimum) {
		return result;
	}
	return result + Margins(_strokeWidth / 2. + 1.);
}

int ItemShape::type() const {
	return Type;
}

QColor ItemShape::color() const {
	return _color;
}

void ItemShape::setColor(const QColor &color) {
	_color = color;
	update();
}

void ItemShape::setStrokeWidth(float64 width) {
	_strokeWidth = width;
	updateArrowFrame();
	update();
}

void ItemShape::setBend(float64 bend) {
	_bend = std::clamp(bend, -1., 1.);
	updateArrowFrame();
	update();
}

float64 ItemShape::defaultAspectRatio() const {
	return (_shape == ShapeType::Star)
		? (kStarBoxHeight / kStarBoxWidth)
		: 1.;
}

void ItemShape::applyFrame(float64 width, float64 height) {
	applyFrameSize(width, height, false);
}

bool ItemShape::applyDraftFrame(float64 width, float64 height) {
	return applyFrameSize(width, height, true);
}

bool ItemShape::applyFrameSize(float64 width, float64 height, bool draft) {
	if (_shape == ShapeType::Arrow) {
		applyStretch(width, verticalSize(), draft);
		updateArrowFrame();
	} else {
		applyStretch(width, height, draft);
	}
	const auto fits = fitsMinimumSize();
	if (_belowMinimum != (draft && !fits)) {
		prepareGeometryChange();
		_belowMinimum = (draft && !fits);
	}
	update();
	return fits;
}

QRectF ItemShape::shapeRect() const {
	const auto content = contentRect();
	const auto inset = _strokeWidth;
	auto result = Rect(QSizeF(
		std::max(content.width() - inset, kMinShapeSide),
		std::max(content.height() - inset, kMinShapeSide)));
	result.moveCenter(rect::center(content));
	return result;
}

float64 ItemShape::direction() const {
	return flipped() ? -1. : 1.;
}

QPainterPath ItemShape::shapePath() const {
	const auto rect = shapeRect();
	auto path = QPainterPath();
	switch (_shape) {
	case ShapeType::Circle:
		path.addEllipse(rect);
		break;
	case ShapeType::Rectangle: {
		const auto rounding = kRoundingRatio
			* std::min(rect.width(), rect.height())
			/ 2.;
		path.addRoundedRect(rect, rounding, rounding);
		break;
	}
	case ShapeType::Star: {
		const auto outer = std::min(
			rect.width() / kStarBoxWidth,
			rect.height() / kStarBoxHeight);
		const auto inner = outer * kStarInnerRatio;
		const auto center = rect::center(rect)
			- QPointF(0., kStarBoxHeight * outer / 2. - outer);
		for (auto i = 0; i != 5; ++i) {
			const auto angle = -M_PI_2 + i * 2. * M_PI / 5.;
			const auto innerAngle = angle + M_PI / 5.;
			const auto outerPoint = center + QPointF(
				outer * std::cos(angle),
				outer * std::sin(angle));
			const auto innerPoint = center + QPointF(
				inner * std::cos(innerAngle),
				inner * std::sin(innerAngle));
			if (!i) {
				path.moveTo(outerPoint);
			} else {
				path.lineTo(outerPoint);
			}
			path.lineTo(innerPoint);
		}
		path.closeSubpath();
		break;
	}
	case ShapeType::Bubble: {
		const auto body = QRectF(
			rect.topLeft(),
			QSizeF(rect.width(), rect.height() * kBubbleBodyRatio));
		const auto rounding = kRoundingRatio
			* std::min(body.width(), body.height())
			/ 2.;
		path.addRoundedRect(body, rounding, rounding);

		const auto tip = QPointF(
			rect::center(rect).x()
				+ direction() * kBubbleTailRatio * rect.width() / 2.,
			rect.bottom());
		const auto center = rect::center(body);
		const auto tail = tip - center;
		const auto length = std::hypot(tail.x(), tail.y());
		if (length > 0) {
			const auto normal = QPointF(
				-tail.y() / length,
				tail.x() / length);
			const auto halfBase = std::min(body.width(), body.height())
				/ 4.;
			auto tailPath = QPainterPath();
			tailPath.moveTo(center + normal * halfBase);
			tailPath.lineTo(tip);
			tailPath.lineTo(center - normal * halfBase);
			tailPath.closeSubpath();
			path = path.united(tailPath);
		}
		break;
	}
	case ShapeType::Arrow:
		break;
	}
	return path;
}

float64 ItemShape::arrowHeadSide(float64 width) const {
	if (width <= 0) {
		return 0.;
	}
	return std::clamp(
		std::max(
			kArrowHeadRatio * width,
			kArrowHeadStrokeFactor * _strokeWidth),
		0.,
		width / 3.);
}

ItemShape::ArrowPoints ItemShape::arrowPoints() const {
	const auto rect = shapeRect();
	const auto side = arrowHeadSide(rect.width());
	const auto center = rect::center(rect);
	const auto dir = direction();
	const auto middle = _bend * kArrowMaxBendRatio * rect.width();
	return {
		.start = QPointF(
			center.x() - dir * rect.width() / 2.,
			center.y() - middle),
		.control = QPointF(
			center.x(),
			center.y() + 3. * middle),
		.end = QPointF(
			center.x() + dir * (rect.width() / 2. - side / 2.),
			center.y() - middle),
		.headSide = side,
	};
}

void ItemShape::updateArrowFrame() {
	if (_shape != ShapeType::Arrow) {
		return;
	}
	const auto width = horizontalSize();
	if (width <= 0) {
		return;
	}
	const auto side = arrowHeadSide(width);
	const auto middle = std::abs(_bend) * kArrowMaxBendRatio * width;
	const auto padding = side * 0.75 + _strokeWidth;
	setAspectRatio(2. * (middle + padding) / width);
}

QPainterPath ItemShape::arrowShaftPath() const {
	const auto points = arrowPoints();
	auto path = QPainterPath();
	path.moveTo(points.start);
	path.quadTo(points.control, points.end);
	return path;
}

QPainterPath ItemShape::arrowHeadPath() const {
	const auto points = arrowPoints();
	const auto delta = points.end - points.control;
	const auto length = std::hypot(delta.x(), delta.y());
	if (length <= 0) {
		return QPainterPath();
	}
	const auto forward = delta / length;
	const auto rotated = [&](float64 angle) {
		return QPointF(
			forward.x() * std::cos(angle) - forward.y() * std::sin(angle),
			forward.x() * std::sin(angle) + forward.y() * std::cos(angle));
	};
	const auto apex = points.end + forward * (points.headSide / 2.);
	auto path = QPainterPath();
	path.moveTo(apex);
	path.lineTo(apex - rotated(kArrowHeadAngle) * points.headSide);
	path.lineTo(apex - rotated(-kArrowHeadAngle) * points.headSide);
	path.closeSubpath();
	return path;
}

QRectF ItemShape::bendHandleRect() const {
	const auto points = arrowPoints();
	auto result = Rect(Size(scaledHandleSize()));
	result.moveCenter((points.start + points.end) * 0.25
		+ points.control * 0.5);
	return result;
}

bool ItemShape::overBendHandle(const QPointF &pos) const {
	return (_shape == ShapeType::Arrow)
		&& isSelected()
		&& bendHandleRect().contains(pos);
}

void ItemShape::updateBend(const QPointF &pos) {
	const auto rect = shapeRect();
	const auto limit = kArrowMaxBendRatio * rect.width();
	if (limit <= 0) {
		return;
	}
	setBend((pos.y() - rect::center(rect).y()) / limit);
}

void ItemShape::performFlip() {
	update();
}

std::shared_ptr<ItemBase> ItemShape::duplicate(Data data) const {
	auto result = std::make_shared<ItemShape>(
		_shape,
		_color,
		_strokeWidth,
		_fill,
		std::move(data));
	if (horizontalSize() > 0) {
		result->setAspectRatio(verticalSize() / horizontalSize());
	}
	result->setBend(_bend);
	return result;
}

void ItemShape::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	if ((event->button() == Qt::LeftButton)
		&& overBendHandle(event->pos())) {
		raiseToTop();
		_bendDragging = true;
		setCursor(Qt::ClosedHandCursor);
		return;
	}
	ItemBase::mousePressEvent(event);
}

void ItemShape::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
	if (_bendDragging) {
		updateBend(event->pos());
		return;
	}
	if (isHandling()
		&& !event->modifiers().testFlag(Qt::ShiftModifier)
		&& event->modifiers().testFlag(Qt::ControlModifier)) {
		applyStretch(
			std::abs(2. * event->pos().x()),
			(_shape == ShapeType::Arrow)
				? verticalSize()
				: std::abs(2. * event->pos().y()));
		updateArrowFrame();
		update();
		return;
	}
	ItemBase::mouseMoveEvent(event);
}

void ItemShape::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	if (_bendDragging && (event->button() == Qt::LeftButton)) {
		_bendDragging = false;
		setCursor(Qt::OpenHandCursor);
		return;
	}
	ItemBase::mouseReleaseEvent(event);
}

void ItemShape::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
	if (overBendHandle(event->pos())) {
		setCursor(_bendDragging
			? Qt::ClosedHandCursor
			: Qt::OpenHandCursor);
		return;
	}
	ItemBase::hoverMoveEvent(event);
}

ItemBase::Placement ItemShape::placement() const {
	auto result = ItemBase::placement();
	result.bend = _bend;
	return result;
}

void ItemShape::applyPlacement(const Placement &placement) {
	_bend = placement.bend;
	ItemBase::applyPlacement(placement);
	updateArrowFrame();
	update();
}

void ItemShape::save(SaveState state) {
	ItemBase::save(state);
	auto &saved = (state == SaveState::Keep) ? _keepedShape : _savedShape;
	saved = {
		.color = _color,
		.strokeWidth = _strokeWidth,
		.bend = _bend,
		.aspectRatio = (horizontalSize() > 0)
			? (verticalSize() / horizontalSize())
			: 1.,
		.fill = _fill,
	};
}

void ItemShape::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	const auto &saved = (state == SaveState::Keep)
		? _keepedShape
		: _savedShape;
	_color = saved.color;
	_strokeWidth = saved.strokeWidth;
	_bend = saved.bend;
	_fill = saved.fill;
	ItemBase::restore(state);
	setAspectRatio(saved.aspectRatio);
	updateArrowFrame();
	update();
}

} // namespace Editor
