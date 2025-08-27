/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/snowflakes.h"

#include "base/random.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"

#include <QtCore/QtMath>

namespace Ui {
namespace {

[[nodiscard]] QImage PrepareSnowflake(QBrush brush) {
	constexpr auto kPenWidth = 1.;
	constexpr auto kTailCount = 6;
	constexpr auto kAngle = (-M_PI / 2.);
	constexpr auto kTailSize = 6.;
	constexpr auto kSubtailPositionRatio = 2 / 3.;
	constexpr auto kSubtailSize = kTailSize / 3;
	constexpr auto kSubtailAngle1 = -M_PI / 6.;
	constexpr auto kSubtailAngle2 = -M_PI - kSubtailAngle1;
	constexpr auto kSpriteSize = (kTailSize + kPenWidth / 2.) * 2;

	const auto x = float64(style::ConvertScaleExact(kSpriteSize / 2.));
	const auto y = float64(style::ConvertScaleExact(kSpriteSize / 2.));
	const auto tailSize = style::ConvertScaleExact(kTailSize);
	const auto subtailSize = style::ConvertScaleExact(kSubtailSize);
	const auto endTail = QPointF(
		std::cos(kAngle) * tailSize,
		std::sin(kAngle) * tailSize);
	const auto startSubtail = endTail * kSubtailPositionRatio;
	const auto endSubtail1 = startSubtail + QPointF(
		subtailSize * std::cos(kSubtailAngle1),
		subtailSize * std::sin(kSubtailAngle1));
	const auto endSubtail2 = startSubtail + QPointF(
		subtailSize * std::cos(kSubtailAngle2),
		subtailSize * std::sin(kSubtailAngle2));

	const auto pen = QPen(
		std::move(brush),
		style::ConvertScaleExact(kPenWidth),
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin);

	const auto s = style::ConvertScaleExact(kSpriteSize)
		* style::DevicePixelRatio();
	auto result = QImage(QSize(s, s), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		PainterHighQualityEnabler hq(p);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.translate(x, y);
		const auto step = 360. / kTailCount;
		for (auto i = 0; i < kTailCount; i++) {
			p.rotate(step);
			p.drawLine(QPointF(), endTail);
			p.drawLine(startSubtail, endSubtail1);
			p.drawLine(startSubtail, endSubtail2);
		}
	}
	return result;
}

} // namespace

Snowflakes::Snowflakes(Fn<void(const QRect &r)> updateCallback)
: _lifeLength({ 300 * 2, 100 * 2 })
, _deathTime({ 2000 * 5, 100 * 5 })
, _scale({ 60, 100 })
, _velocity({ 20 * 7, 4 * 7 })
, _angle({ 70, 40 })
, _relativeX({ 0, 100 })
, _relativeY({ -10, 70 })
, _appearProgressTill(200. / _deathTime.from)
, _disappearProgressAfter(_appearProgressTill)
, _dotMargins(3., 3., 3., 3.)
, _renderMargins(1., 1., 1., 1.)
, _animation([=](crl::time now) {
	if (now > _nextBirthTime && !_paused.at) {
		createParticle(now);
	}
	if (_rectToUpdate.isValid()) {
		updateCallback(base::take(_rectToUpdate));
	}
}) {
	{
		const auto from = _deathTime.from + _deathTime.length;
		auto r = bytes::vector(from + 1);
		base::RandomFill(r.data(), r.size());

		const auto now = crl::now();
		for (auto i = -from; i < 0; i += randomInterval(_lifeLength, r[-i])) {
			createParticle(now + i);
		}
		updateCallback(_rectToUpdate);
	}
	if (!anim::Disabled()) {
		_animation.start();
	}
}

int Snowflakes::randomInterval(
		const Interval &interval,
		const bytes::type &random) const {
	return interval.from + (uchar(random) % interval.length);
}

crl::time Snowflakes::timeNow() const {
	return _paused.at ? _paused.at : (crl::now() - _paused.diff);
}

void Snowflakes::paint(QPainter &p, const QRectF &rect) {
	const auto opacity = p.opacity();
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_brush);
	const auto now = timeNow();
	for (const auto &particle : _particles) {
		const auto progress = (now - particle.birthTime)
			/ float64(particle.deathTime - particle.birthTime);
		if (progress > 1.) {
			continue;
		}
		const auto appearProgress = std::clamp(
			progress / _appearProgressTill,
			0.,
			1.);
		const auto dissappearProgress = 1.
			- (std::clamp(progress - _disappearProgressAfter, 0., 1.)
				/ (1. - _disappearProgressAfter));

		p.setOpacity(appearProgress * dissappearProgress * opacity);

		const auto startX = rect.x() + rect.width() * particle.relativeX;
		const auto startY = rect.y() + rect.height() * particle.relativeY;
		const auto endX = startX + particle.velocityX;
		const auto endY = startY + particle.velocityY;

		const auto x = anim::interpolateF(startX, endX, progress);
		const auto y = anim::interpolateF(startY, endY, progress);

		if (particle.type == Type::Dot) {
			const auto renderRect = QRectF(x, y, 0., 0.)
				+ _dotMargins * particle.scale;
			p.drawEllipse(renderRect);
			_rectToUpdate |= renderRect.toRect() + _renderMargins;
		} else if (particle.type == Type::Snowflake) {
			const auto s = _sprite.size() / style::DevicePixelRatio();
			const auto h = s.height() / 2.;
			const auto pos = QPointF(x - h, y - h);
			p.drawImage(pos, _sprite);
			_rectToUpdate |= QRectF(pos, s).toRect() + _renderMargins;
		}
	}
	p.setOpacity(opacity);
}

void Snowflakes::setPaused(bool paused) {
	paused |= anim::Disabled();
	if (paused) {
		_paused.diff = 0;
		_paused.at = crl::now();
	} else {
		_paused.diff = _paused.at ? (crl::now() - _paused.at) : 0;
		_paused.at = 0;
	}
}

void Snowflakes::setBrush(QBrush brush) {
	_brush = std::move(brush);
	_sprite = PrepareSnowflake(_brush);
}

void Snowflakes::createParticle(crl::time now) {
	constexpr auto kRandomSize = 9;
	auto random = bytes::vector(kRandomSize);
	base::RandomFill(random.data(), random.size());

	auto i = 0;
	auto next = [&] { return random[i++]; };

	_nextBirthTime = now + randomInterval(_lifeLength, next());

	const auto angle = randomInterval(_angle, next());
	const auto velocity = randomInterval(_velocity, next());
	auto particle = Particle{
		.birthTime = now,
		.deathTime = now + randomInterval(_deathTime, next()),
		.scale = float64(randomInterval(_scale, next())) / 100.,
		.relativeX = float64(randomInterval(_relativeX, next())) / 100.,
		.relativeY = float64(randomInterval(_relativeY, next())) / 100.,
		.velocityX = std::cos(M_PI / 180. * angle) * velocity,
		.velocityY = std::sin(M_PI / 180. * angle) * velocity,
		.type = ((uchar(next()) % 2) == 1 ? Type::Snowflake : Type::Dot),
	};
	for (auto i = 0; i < _particles.size(); i++) {
		if (particle.birthTime > _particles[i].deathTime) {
			_particles[i] = particle;
			return;
		}
	}
	_particles.push_back(particle);
}


} // namespace Ui
