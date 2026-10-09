/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QRect>
#include <QtGui/QColor>

class QPainter;

namespace Wallet {

struct ClockPose {
	float64 minute = 0.;
	float64 hour = 0.;
};
[[nodiscard]] ClockPose SendingClockPose(crl::time elapsed);

struct ClockStyle {
	int size = 0;
	int stroke = 0;
	int minuteHand = 0;
	int hourHand = 0;
};
void PaintClock(
	QPainter &p,
	const ClockStyle &st,
	QPointF center,
	ClockPose pose,
	const QColor &color,
	float64 scale = 1.);

// Passes of |duration| separated by at least |pause|, advanced from the owner's tick.
struct GlareCycle {
	void tick(crl::time now, crl::time duration, crl::time pause);
	[[nodiscard]] std::optional<float64> progress(crl::time now) const;

	crl::time birth = 0;
	crl::time death = 0;
};

struct GlareBand {
	float64 from = 0.;
	float64 till = 0.;
};
[[nodiscard]] GlareBand ComputeGlareBand(
	float64 progress,
	float64 extent,
	int width);

struct GlareStyle {
	float64 stroke = 0.;
	float64 slope = 0.;
	float64 border = 1.;
	float64 background = 0.;
};
void PaintGlare(
	QPainter &p,
	const QRectF &rect,
	float64 radius,
	GlareBand band,
	const GlareStyle &st,
	const QColor &color);

} // namespace Wallet
