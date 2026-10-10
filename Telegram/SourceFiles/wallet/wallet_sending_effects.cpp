/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_sending_effects.h"

#include <QtCore/QtMath>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>

namespace Wallet {
namespace {

constexpr auto kClockHourTurn = crl::time(2000);
constexpr auto kClockMinuteTurnsPerHourTurn = 3;

[[nodiscard]] QLinearGradient GlareGradient(
		const QRectF &rect,
		GlareBand band,
		float64 slope,
		const QColor &color,
		float64 strength) {
	auto middle = color;
	if (strength != 1.) {
		middle.setAlphaF(middle.alphaF() * strength);
	}
	auto edge = middle;
	edge.setAlphaF(0.);
	const auto start = rect.topLeft() + QPointF(band.from, 0.);
	const auto end = start
		+ (band.till - band.from) / (1. + slope * slope) * QPointF(1., slope);
	auto result = QLinearGradient(start, end);
	result.setStops({ { 0., edge }, { 0.5, middle }, { 1., edge } });
	return result;
}

} // namespace

ClockPose SendingClockPose(crl::time elapsed) {
	const auto progress = (elapsed % kClockHourTurn)
		/ float64(kClockHourTurn);
	return {
		.minute = kClockMinuteTurnsPerHourTurn * progress,
		.hour = 0.25 + progress,
	};
}

void PaintClock(
		QPainter &p,
		const ClockStyle &st,
		QPointF center,
		ClockPose pose,
		const QColor &color,
		float64 scale) {
	const auto stroke = float64(st.stroke);
	const auto radius = (st.size - stroke) / 2.;
	p.save();
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	auto pen = QPen(color, stroke);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(center, radius, radius);
	const auto hand = [&](float64 turns, int reach) {
		const auto angle = 2. * M_PI * turns;
		const auto length = reach - stroke / 2.;
		p.drawLine(
			center,
			center + QPointF(std::sin(angle), -std::cos(angle)) * length);
	};
	hand(pose.minute, st.minuteHand);
	hand(pose.hour, st.hourHand);
	p.restore();
}

void GlareCycle::tick(crl::time now, crl::time duration, crl::time pause) {
	if (now - death > pause) {
		birth = now;
		death = now + duration;
	}
}

std::optional<float64> GlareCycle::progress(crl::time now) const {
	if (!birth) {
		return std::nullopt;
	}
	const auto result = (now - birth) / float64(death - birth);
	if (result < 0. || result > 1.) {
		return std::nullopt;
	}
	return result;
}

GlareBand ComputeGlareBand(float64 progress, float64 extent, int width) {
	const auto from = -width + (extent + 2 * width) * progress;
	return { .from = from, .till = from + width };
}

void PaintGlare(
		QPainter &p,
		const QRectF &rect,
		float64 radius,
		GlareBand band,
		const GlareStyle &st,
		const QColor &color) {
	if (st.background > 0.) {
		p.setPen(Qt::NoPen);
		p.setBrush(GlareGradient(rect, band, st.slope, color, st.background));
		p.drawRoundedRect(rect, radius, radius);
	}
	if (st.border > 0.) {
		const auto half = st.stroke / 2.;
		const auto gradient = GlareGradient(
			rect,
			band,
			st.slope,
			color,
			st.border);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(gradient), st.stroke));
		p.drawRoundedRect(
			rect - QMarginsF(half, half, half, half),
			radius - half,
			radius - half);
	}
}

} // namespace Wallet
