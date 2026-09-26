/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "base/random.h"

namespace Ui::Premium {

class StarParticles final {
public:
	enum class Glyph {
		None,
		Star,
		Dollar,
	};

	explicit StarParticles(Fn<void(const QRect &)> update);

	void setColor(QColor color);
	void setColors(QColor color1, QColor color2);
	void setGlyph(Glyph glyph);
	void paint(QPainter &p, const QRectF &field);
	void setPaused(bool paused);
	void fling(float64 strength);

private:
	struct Particle {
		crl::time birthTime = 0;
		crl::time deathTime = 0;
		float64 angle = 0.;
		float64 radiusFactor = 0.;
		float64 distance = 0.;
		float64 alpha = 0.;
		float64 flipProgress = 0.;
		int sizeIndex = 0;
		int colorIndex = 0;
	};
	struct Sprite {
		QImage image;
		QSizeF size;
	};

	void ensureParticles();
	void createParticle(crl::time now, Particle &particle);
	void rebuildSprites(int ratio);
	void tick(crl::time now);
	void updateSpeedScale(crl::time now);
	[[nodiscard]] float64 driftStep(crl::time delta) const;

	const Fn<void(const QRect &)> _update;

	static constexpr int kColorSteps = 20;
	std::array<std::array<Sprite, kColorSteps>, 3> _sprites;
	std::vector<Particle> _particles;

	Ui::Animations::Basic _animation;
	base::BufferedRandom<uint32> _random;

	QColor _color1;
	QColor _color2;
	Glyph _glyph = Glyph::None;
	float64 _maxSpriteExtent = 0.;
	bool _spritesDirty = true;
	int _spritesRatio = 0;

	std::array<float64, 3> _fieldAngle = { { 0., 0., 0. } };
	QRectF _field;

	float64 _speedScale = 1.;
	float64 _flingMax = 1.;
	crl::time _flingStart = 0;
	crl::time _lastTime = 0;
	crl::time _pausedAt = 0;

	bool _paused = false;
	uint8_t _idleCounter = 0;

	QRect _rectToUpdate;

};

} // namespace Ui::Premium
