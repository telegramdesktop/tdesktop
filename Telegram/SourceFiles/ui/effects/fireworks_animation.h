/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Ui {

class FireworksAnimation final {
public:
	explicit FireworksAnimation(Fn<void()> repaint);

	bool paint(QPainter &p, const QRect &rect);

private:
	struct Particle {
		enum class Type : uchar {
			Circle,
			Rectangle
		};

		float64 x = 0.;
		float64 y = 0.;
		float64 moveX = 0.;
		float64 moveY = 0.;
		uint16 rotation = 0;

		Type type = Type::Circle;
		uchar color = 0;
		bool right = false;
		uchar size = 0;
		uchar xFinished = 0;
		uchar finishedStart = 0;
		bool finished = false;
	};

	void update(crl::time now);
	void startFall();
	void paintParticle(
		QPainter &p,
		const Particle &particle,
		const QRect &rect);
	void initParticle(Particle &particle, bool falling);
	void updateParticle(Particle &particle, crl::time dt);

	std::vector<Particle> _particles;
	std::vector<QBrush> _brushes;
	Ui::Animations::Basic _animation;
	Fn<void()> _repaint;
	crl::time _lastUpdate = 0;
	float64 _speedCoef = 1.;
	int _fallingDown = 0;
	int _smallSide = 0;
	bool _startedFall = false;

};

} // namespace Ui
