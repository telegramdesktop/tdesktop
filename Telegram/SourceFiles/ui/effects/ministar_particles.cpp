/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/ministar_particles.h"

#include "ui/painter.h"
#include "ui/color_int_conversion.h"
#include "base/random.h"

#include <QtGui/QPainterPath>
#include <QtMath>

namespace Ui {

StarParticles::StarParticles(Type type, int count, int size)
: _type(type)
, _count(count)
, _starSize(size) {
	generate();
}

void StarParticles::setSpeed(float speed) {
	_speed = speed;
}

void StarParticles::setVisible(float visible) {
	_visible = visible;
}

void StarParticles::setColor(const QColor &color) {
	setColors({ color });
}

void StarParticles::setColors(std::vector<QColor> colors) {
	Expects(!colors.empty());

	const auto was = int(_colors.size());
	_colors = std::move(colors);
	const auto now = int(_colors.size());

	_paintedCaches.clear();

	if (now != was) {
		if (now > 1) {
			auto buffered = base::BufferedRandom<uint32>(_particles.size());
			for (auto &p : _particles) {
				p.colorIndex = base::RandomIndex(now, buffered);
			}
		} else {
			for (auto &p : _particles) {
				p.colorIndex = 0;
			}
		}

	}
}

QImage StarParticles::generateStarCache(int size, QColor color) {
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		size * ratio,
		size * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);

	auto p = QPainter(&image);
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(color);
	p.setPen(Qt::NoPen);

	auto star = QPainterPath();
	const auto center = size / 2.;
	const auto outerRadius = size / 2.;
	const auto innerRadius = size / 5.;
	const auto points = 4;

	for (auto i = 0; i < points * 2; ++i) {
		const auto angle = M_PI * i / points - M_PI / 2;
		const auto radius = (i % 2 == 0) ? outerRadius : innerRadius;
		const auto x = center + radius * std::cos(angle);
		const auto y = center + radius * std::sin(angle);
		if (i == 0) {
			star.moveTo(x, y);
		} else {
			star.lineTo(x, y);
		}
	}
	star.closeSubpath();
	p.drawPath(star);

	return image;
}

void StarParticles::generate() {
	_particles.clear();
	_particles.reserve(_count);

	constexpr auto kRandomPerParticle = 13;
	auto random = bytes::vector(_count * kRandomPerParticle);
	base::RandomFill(random.data(), random.size());
	auto idx = 0;
	const auto next = [&] { return uchar(random[idx++]) / 255.; };

	const auto colors = int(_colors.size());

	for (auto i = 0; i < _count; ++i) {
		auto p = Particle();
		p.x = next();
		p.y = next();
		p.size = 0.3 + next() * 0.7;
		p.rotation = next() * 360.;
		p.vrotation = (next() - 0.5) * 180.;
		p.alpha = 0.3 + next() * 0.7;

		if (_type == Type::Right) {
			p.vx = 0.5 + next() * 1.5;
			p.vy = (next() - 0.5) * 1.5;
		} else if (_type == Type::Radial) {
			const auto angle = next() * 2 * M_PI;
			const auto speed = 0.5 + next();
			p.vx = std::cos(angle) * speed;
			p.vy = std::sin(angle) * speed;
		} else {
			const auto angle = next() * 2 * M_PI;
			const auto speed = 0.3 + next() * 0.5;
			p.vx = -std::cos(angle) * speed;
			p.vy = -std::sin(angle) * speed;
		}

		if (colors > 1) {
			p.colorIndex = int(base::SafeRound(next() * (colors - 1)));
		}

		_particles.push_back(p);
	}
}

void StarParticles::paint(
		QPainter &p,
		const QRect &rect,
		crl::time now,
		bool paused) {
	if (_lastTime == 0) {
		_lastTime = now;
		return;
	}

	if (paused) {
		if (!_pausedAt) {
			_pausedAt = now;
		}
		now = _pausedAt - _pauseOffset;
	} else {
		if (_pausedAt) {
			_pauseOffset += now - _pausedAt;
			_pausedAt = 0;
		}
		now = now - _pauseOffset;
	}

	const auto validate = [&](int colorIndex) {
		Expects(colorIndex >= 0 && colorIndex < _colors.size());

		if (_paintedCaches.empty()) {
			_paintedCaches.resize(_colors.size());
		} else if (const auto result = _paintedCaches[colorIndex]
			; !result.isNull()) {
			return result;
		}
		const auto &color = _colors[colorIndex];
		const auto colorKey = color.rgba();
		const auto i = _starCache.find(colorKey);
		const auto &cache = (i != end(_starCache))
			? i->second
			: _starCache.emplace(
				colorKey,
				generateStarCache(_starSize, color)).first->second;
		_paintedCaches[colorIndex] = cache;
		return cache;
	};

	const auto dt = (now - _lastTime) / 1000.;
	_lastTime = now;

	const auto visibleCount = int(_count * _visible);
	for (auto i = 0; i < visibleCount; ++i) {
		auto &particle = _particles[i];

		particle.x += particle.vx * _speed * dt;
		particle.y += particle.vy * _speed * dt;
		particle.rotation += particle.vrotation * dt;

		if (particle.x < -0.1 || particle.x > 1.1
			|| particle.y < -0.1 || particle.y > 1.1) {
			auto random = bytes::vector(1);
			base::RandomFill(random.data(), random.size());
			particle.x = (_type == Type::Right) ? 0. : 0.5;
			particle.y = (_type == Type::Right)
				? (uchar(random[0]) / 255.)
				: 0.5;
		}

		const auto x = rect.x() + particle.x * rect.width();
		const auto y = rect.y() + particle.y * rect.height();
		const auto cache = validate(particle.colorIndex);
		const auto size = cache.width()
			/ cache.devicePixelRatio() * particle.size;

		p.save();
		p.setOpacity(particle.alpha);
		p.translate(x, y);
		p.rotate(particle.rotation);
		p.drawImage(
			QRectF(-size / 2, -size / 2, size, size),
			cache);
		p.restore();
	}
}

} // namespace Ui
