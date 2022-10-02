/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

#include <QSvgRenderer>

namespace Ui {
namespace Premium {

class MiniStars final {
public:
	MiniStars(Fn<void(const QRect &r)> updateCallback, bool opaque = false);

	void paint(QPainter &p, const QRectF &rect);
	void setPaused(bool paused);

	static constexpr auto kSizeFactor = 1.5;

private:
	struct MiniStar {
		crl::time birthTime = 0;
		crl::time deathTime = 0;
		int angle = 0;
		float64 size = 0.;
		float64 alpha = 0.;
		float64 sinFactor = 0.;
	};

	struct Interval {
		int from = 0;
		int length = 0;
	};

	void createStar(crl::time now);
	[[nodiscard]] int angle() const;
	[[nodiscard]] crl::time timeNow() const;
	[[nodiscard]] int randomInterval(const Interval &interval) const;

	const std::vector<Interval> _availableAngles;
	const Interval _lifeLength;
	const Interval _deathTime;
	const Interval _size;
	const Interval _alpha;
	const Interval _sinFactor;

	const float64 _appearProgressTill;
	const float64 _disappearProgressAfter;
	const float64 _distanceProgressStart;

	QSvgRenderer _sprite;

	Ui::Animations::Basic _animation;

	std::vector<MiniStar> _ministars;

	crl::time _nextBirthTime = 0;
	bool _paused = false;

	QRect _rectToUpdate;

};

} // namespace Premium
} // namespace Ui
