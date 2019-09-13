/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/animation_value.h"

#include "ui/painter.h"

#include <QtCore/QtMath> // M_PI

namespace anim {
namespace {

bool AnimationsDisabled = false;

} // namespace

transition linear = [](const float64 &delta, const float64 &dt) {
	return delta * dt;
};

transition sineInOut = [](const float64 &delta, const float64 &dt) {
	return -(delta / 2) * (cos(M_PI * dt) - 1);
};

transition halfSine = [](const float64 &delta, const float64 &dt) {
	return delta * sin(M_PI * dt / 2);
};

transition easeOutBack = [](const float64 &delta, const float64 &dt) {
	static constexpr auto s = 1.70158;

	const float64 t = dt - 1;
	return delta * (t * t * ((s + 1) * t + s) + 1);
};

transition easeInCirc = [](const float64 &delta, const float64 &dt) {
	return -delta * (sqrt(1 - dt * dt) - 1);
};

transition easeOutCirc = [](const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1;
	return delta * sqrt(1 - t * t);
};

transition easeInCubic = [](const float64 &delta, const float64 &dt) {
	return delta * dt * dt * dt;
};

transition easeOutCubic = [](const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1;
	return delta * (t * t * t + 1);
};

transition easeInQuint = [](const float64 &delta, const float64 &dt) {
	const float64 t2 = dt * dt;
	return delta * t2 * t2 * dt;
};

transition easeOutQuint = [](const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1, t2 = t * t;
	return delta * (t2 * t2 * t + 1);
};

bool Disabled() {
	return AnimationsDisabled;
}

void SetDisabled(bool disabled) {
	AnimationsDisabled = disabled;
}

void DrawStaticLoading(
		QPainter &p,
		QRectF rect,
		int stroke,
		QPen pen,
		QBrush brush) {
	PainterHighQualityEnabler hq(p);

	p.setBrush(brush);
	pen.setWidthF(stroke);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.drawEllipse(rect);

	const auto center = rect.center();
	const auto first = QPointF(center.x(), rect.y() + 1.5 * stroke);
	const auto delta = center.y() - first.y();
	const auto second = QPointF(center.x() + delta * 2 / 3., center.y());
	if (delta > 0) {
		QPainterPath path;
		path.moveTo(first);
		path.lineTo(center);
		path.lineTo(second);
		p.drawPath(path);
	}
}

} // anim
