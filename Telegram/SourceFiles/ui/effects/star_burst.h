/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QPainterPath>
#include <QtSvg/QSvgRenderer>

namespace Ui {

struct StarBurstRange {
	float64 from = 0.;
	float64 till = 0.;
};

struct StarBurstSide {
	float64 sign = 1.;
	int count = 0;
	StarBurstRange angle;
	StarBurstRange reach;
};

// Angles in degrees (y down); reach, fall, size in extents; start in emitters.
struct StarBurstDescriptor {
	std::vector<StarBurstSide> sides;
	crl::time delay = 0;
	crl::time spread = 0;
	crl::time lifeMin = 0;
	crl::time lifeMax = 0;
	StarBurstRange fall;
	StarBurstRange startX;
	StarBurstRange startY;
	StarBurstRange size;
	StarBurstRange alpha;
	StarBurstRange twinkle;
	float64 appearTill = 0.2;
	float64 fadeAfter = 0.8;
	float64 deformation = 0.1;
	std::optional<QColor> color;
};

struct StarBurstFrame {
	QPointF origin;
	float64 emitter = 0.;
	float64 extent = 0.;
	crl::time elapsed = 0;
	QPainterPath clip;
};

class StarBurst final {
public:
	[[nodiscard]] static std::unique_ptr<StarBurst> Make(
		const StarBurstDescriptor &descriptor);

	[[nodiscard]] crl::time duration() const;
	void paint(QPainter &p, const StarBurstFrame &frame) const;

private:
	struct Star {
		crl::time birth = 0;
		crl::time life = 0;
		float64 side = 1.;
		float64 angle = 0.;
		float64 reach = 0.;
		float64 fall = 0.;
		QPointF start;
		float64 size = 0.;
		float64 alpha = 0.;
		float64 sinFactor = 0.;
	};

	explicit StarBurst(const StarBurstDescriptor &descriptor);

	std::vector<Star> _stars;
	mutable QSvgRenderer _sprite;
	float64 _appearTill = 0.;
	float64 _fadeAfter = 0.;
	float64 _deformation = 0.;
	crl::time _duration = 0;

};

} // namespace Ui
