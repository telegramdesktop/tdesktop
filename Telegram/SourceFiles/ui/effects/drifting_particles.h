/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/random.h"

namespace Ui {

class ParticlesRandom final {
public:
	[[nodiscard]] int index(int count) {
		return count ? int(next() % uint32(count)) : 0;
	}
	[[nodiscard]] float64 value() {
		return next() / (float64(std::numeric_limits<uint32>::max()) + 1.);
	}
	[[nodiscard]] float64 value(float64 from, float64 till) {
		return from + value() * (till - from);
	}
	[[nodiscard]] bool chance(int one) {
		return !index(one);
	}

private:
	[[nodiscard]] uint32 next() {
		return _buffered.next();
	}

	base::BufferedRandom<uint32> _buffered = base::BufferedRandom<uint32>(
		1024);

};

struct FourPointStarArgs {
	int size = 0;
	float64 corner = 0.85;
	float64 cornerRadius = 0.;
	int blurRadius = 0;
	QColor color = Qt::white;
};

[[nodiscard]] QImage FourPointStarImage(FourPointStarArgs args);

struct DriftingSprite {
	QImage image;
	int maxAlpha = 255;
	bool randomRotate = false;
};

class DriftingParticles final {
public:
	struct Config {
		std::vector<DriftingSprite> sprites;
		int count = 40;
		float64 speed = 4.;
		float64 excludeRadius = 0.;
		bool orbit = false;
		bool checkBounds = false;
		bool spawnInCircle = true;
		bool spread = false;
	};

	explicit DriftingParticles(Config config);

	void setGeometry(QRectF field, QRectF bounds, QRectF exclude);
	void setPaused(bool paused);
	void paint(QPainter &p);

private:
	struct Particle {
		crl::time bornTime = 0;
		crl::time lifeTime = 0;
		float64 x = 0.;
		float64 y = 0.;
		float64 vecX = 0.;
		float64 vecY = 0.;
		float64 rotate = 0.;
		int sprite = 0;
		int alpha = 0;
	};

	void generate(Particle &particle, crl::time now);
	[[nodiscard]] QPointF generatePosition();
	[[nodiscard]] QPointF drawingPosition(const Particle &particle) const;

	const Config _config;

	std::vector<Particle> _particles;
	std::vector<float64> _orbitAngles;

	QRectF _field;
	QRectF _bounds;
	QRectF _exclude;
	crl::time _lastTime = 0;
	crl::time _pausedAt = 0;
	crl::time _pauseOffset = 0;

	ParticlesRandom _random;

};

} // namespace Ui
