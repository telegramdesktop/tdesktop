/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;

namespace Ui {

class TimerParticles final {
public:
	explicit TimerParticles(int count = 40);

	void paint(
		QPainter &p,
		QPointF emit,
		QPointF direction,
		const QColor &color,
		float64 alpha);

private:
	struct Particle {
		float64 x = 0.;
		float64 y = 0.;
		float64 vx = 0.;
		float64 vy = 0.;
		float64 velocity = 0.;
		float64 alpha = 0.;
		float64 lifeTime = 0.;
		float64 currentTime = 0.;
	};

	void update(crl::time dt);

	std::vector<Particle> _particles;
	const int _count = 0;
	crl::time _lastTime = 0;

};

class WaveformParticles final {
public:
	explicit WaveformParticles(int count = 250);

	void paint(
		QPainter &p,
		const QRectF &emitArea,
		const QColor &color,
		float64 alpha);
	void clear();

private:
	struct Particle {
		float64 x = 0.;
		float64 y = 0.;
		float64 v = 0.;
		float64 vx = 0.;
		float64 vy = 0.;
		float64 t = 0.;
		float64 d = 0.;
	};

	std::vector<Particle> _particles;
	const int _count = 0;
	crl::time _lastTime = 0;

};

struct VoiceOnceParticles {
	TimerParticles radial;
	WaveformParticles waveform;
};

} // namespace Ui
