/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

class StarParticles final {
public:
	enum class Type {
		Right,
		Radial,
		RadialInside,
	};

	StarParticles(Type type, int count, int size);

	void setSpeed(float speed);
	void setVisible(float visible);
	void setColor(const QColor &color);
	void paint(QPainter &p, const QRect &rect, crl::time now);

private:
	struct Particle {
		float x = 0.;
		float y = 0.;
		float vx = 0.;
		float vy = 0.;
		float rotation = 0.;
		float vrotation = 0.;
		float size = 0.;
		float alpha = 0.;
		crl::time born = 0;
	};

	void generate();
	[[nodiscard]] QImage generateStarCache(int size, QColor color);

	Type _type;
	int _count = 0;
	int _starSize = 0;
	float _speed = 1.f;
	float _visible = 1.f;
	QColor _color = QColor(255, 200, 70);
	std::vector<Particle> _particles;
	base::flat_map<QRgb, QImage> _starCache;
	crl::time _lastTime = 0;
};

} // namespace Ui
