/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Ui {

class Snowflakes final {
public:
	Snowflakes(Fn<void(const QRect &r)> updateCallback);

	void paint(QPainter &p, const QRectF &rect);
	void setPaused(bool paused);
	void setBrush(QBrush brush);

private:
	enum class Type {
		Dot,
		Snowflake,
	};

	struct Interval {
		int from = 0;
		int length = 0;
	};

	struct Particle {
		crl::time birthTime = 0;
		crl::time deathTime = 0;
		float64 scale = 0.;
		float64 alpha = 0.;
		float64 relativeX = 0.; // Relative to a width.
		float64 relativeY = 0.; // Relative to a height.
		float64 velocityX = 0.;
		float64 velocityY = 0.;
		Type type;
	};

	void createParticle(crl::time now);
	[[nodiscard]] crl::time timeNow() const;
	[[nodiscard]] int randomInterval(
		const Interval &interval,
		const gsl::byte &random) const;

	const Interval _lifeLength;
	const Interval _deathTime;
	const Interval _scale;
	const Interval _velocity;
	const Interval _angle;
	const Interval _relativeX;
	const Interval _relativeY;

	const float64 _appearProgressTill;
	const float64 _disappearProgressAfter;
	const QMarginsF _dotMargins;
	const QMargins _renderMargins;

	Ui::Animations::Basic _animation;
	QImage _sprite;

	std::vector<Particle> _particles;

	crl::time _nextBirthTime = 0;
	struct {
		crl::time diff = 0;
		crl::time at = 0;
	} _paused;
	QBrush _brush;

	QRect _rectToUpdate;

};

} // namespace Ui
