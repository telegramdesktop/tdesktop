/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_star_particles.h"

#include "base/algorithm.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/style/style_core.h"

#include <QtMath>
#include <QtSvg/QSvgRenderer>

namespace Ui::Premium {
namespace {

constexpr auto kParticleCount = 100;
constexpr auto kGlyphParticleCount = 75;
constexpr auto kDollarParticleCount = 60;
constexpr auto kDollarSpawnChance = 0.13; // Share of particles that are coins.
constexpr auto kDollarSpeedScale = 2.5; // Coins fly outward faster than stars.
constexpr auto kDollarLifeScale = 2.5; // ...and live longer, for longer trails.
constexpr auto kDollarSpawnRadiusFactor = 0.2; // Coins start near the center.
constexpr auto kDollarFlipPerSecond = 1.1; // Horizontal coin-flip frequency.
constexpr auto kDollarSpriteAlpha = 255;
constexpr auto kIdleLimit = 5;
constexpr auto kMinDelta = crl::time(4);
constexpr auto kMaxDelta = crl::time(50);
constexpr auto kLifeMin = crl::time(2000);
constexpr auto kLifeRand = 1000;
constexpr auto kAppearMs = 200.;
constexpr auto kFadeOutSpan = 150.;
constexpr auto kRadiusResolution = 1000;
constexpr auto kAngleSteps = 360;
constexpr auto kAlphaBasePercent = 50;
constexpr auto kAlphaRangePercent = 50;
constexpr auto kFatness = 0.98;
constexpr auto kSpriteAlpha = 200;
constexpr auto kBoundsFactor = 1.6;
constexpr auto kDriftDp = 4.;
constexpr auto kDriftDivisor = 660.;
constexpr auto kSoftScale = 0.66;
constexpr auto kMinSoftRatio = 2;
constexpr auto kRoundDp = 0.8;
constexpr auto kMsPerSecond = 1000.;
constexpr auto kDegreesToRadians = M_PI / 180.;
constexpr auto k2Pi = 2. * M_PI;
constexpr auto kRandomBuffer = 1024;
constexpr auto kFlingRampUpMs = crl::time(600);
constexpr auto kFlingTotalMs = crl::time(2000);
constexpr auto kFlingThresholdLow = 60.;
constexpr auto kFlingThresholdHigh = 180.;
constexpr auto kFlingSpeedLow = 5.;
constexpr auto kFlingSpeedMedium = 9.;
constexpr auto kFlingSpeedHigh = 15.;

constexpr auto kSizesDp = std::array{ 3., 8., 7. };
constexpr auto kDollarSizesDp = std::array{ 16., 8., 7. };
constexpr auto kRotationDegPerSec = std::array{ 9., 7.2, 6. };

[[nodiscard]] QPainterPath StarPath(float64 size) {
	const auto half = size / 2.;
	const auto mid = half * kFatness;
	auto path = QPainterPath();
	path.moveTo(0., half);
	path.lineTo(mid, mid);
	path.lineTo(half, 0.);
	path.lineTo(size - mid, mid);
	path.lineTo(size, half);
	path.lineTo(size - mid, size - mid);
	path.lineTo(half, size);
	path.lineTo(mid, size - mid);
	path.closeSubpath();
	return path;
}

[[nodiscard]] float64 Overshoot(float64 t) {
	constexpr auto kTension = 2.;
	const auto u = t - 1.;
	return u * u * ((kTension + 1.) * u + kTension) + 1.;
}

} // namespace

StarParticles::StarParticles(Fn<void(const QRect &)> update)
: _update(std::move(update))
, _animation([=](crl::time now) {
	if (++_idleCounter >= kIdleLimit) {
		_animation.stop();
		return;
	}
	tick(now);
	if (_rectToUpdate.isValid()) {
		_update(base::take(_rectToUpdate));
	}
})
, _random(kRandomBuffer) {
}

void StarParticles::setColor(QColor color) {
	setColors(color, color);
}

void StarParticles::setColors(QColor color1, QColor color2) {
	if (_color1 == color1 && _color2 == color2) {
		return;
	}
	_color1 = color1;
	_color2 = color2;
	_spritesDirty = true;
}

void StarParticles::setGlyph(Glyph glyph) {
	if (_glyph == glyph) {
		return;
	}
	_glyph = glyph;
	_spritesDirty = true;
}

void StarParticles::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (paused) {
		_pausedAt = crl::now();
		_animation.stop();
	} else {
		if (_pausedAt) {
			const auto delta = crl::now() - _pausedAt;
			for (auto &particle : _particles) {
				particle.birthTime += delta;
				particle.deathTime += delta;
			}
			if (_flingStart) {
				_flingStart += delta;
			}
			_pausedAt = 0;
		}
		_lastTime = crl::now();
	}
}

float64 StarParticles::driftStep(crl::time delta) const {
	return style::ConvertScale(kDriftDp) * (delta / kDriftDivisor);
}

void StarParticles::fling(float64 strength) {
	_flingMax = (strength < kFlingThresholdLow)
		? kFlingSpeedLow
		: (strength < kFlingThresholdHigh)
		? kFlingSpeedMedium
		: kFlingSpeedHigh;
	const auto now = crl::now();
	_flingStart = now;
	_idleCounter = 0;
	if (!_paused && !_animation.animating()) {
		_lastTime = now;
		_animation.start();
	}
}

void StarParticles::updateSpeedScale(crl::time now) {
	if (!_flingStart) {
		_speedScale = 1.;
		return;
	}
	const auto elapsed = std::max<crl::time>(0, now - _flingStart);
	if (elapsed >= kFlingTotalMs) {
		_flingStart = 0;
		_speedScale = 1.;
	} else if (elapsed < kFlingRampUpMs) {
		_speedScale = 1.
			+ (_flingMax - 1.) * (elapsed / float64(kFlingRampUpMs));
	} else {
		const auto progress = (elapsed - kFlingRampUpMs)
			/ float64(kFlingTotalMs - kFlingRampUpMs);
		_speedScale = _flingMax + (1. - _flingMax) * progress;
	}
}

void StarParticles::createParticle(crl::time now, Particle &particle) {
	if (_glyph == Glyph::Dollar) {
		const auto roll = base::RandomIndex(kRadiusResolution, _random)
			/ float64(kRadiusResolution);
		particle.sizeIndex = (roll < kDollarSpawnChance)
			? 0
			: (1 + base::RandomIndex(int(_sprites.size()) - 1, _random));
	} else {
		particle.sizeIndex = base::RandomIndex(int(_sprites.size()), _random);
	}
	const auto dollar = (_glyph == Glyph::Dollar) && (particle.sizeIndex == 0);
	particle.radiusFactor = base::RandomIndex(kRadiusResolution, _random)
		/ float64(kRadiusResolution);
	if (dollar) {
		particle.radiusFactor *= kDollarSpawnRadiusFactor;
	}
	particle.angle = base::RandomIndex(kAngleSteps, _random)
		* kDegreesToRadians;
	particle.alpha = (kAlphaBasePercent
		+ base::RandomIndex(kAlphaRangePercent, _random)) / 100.;
	particle.colorIndex = base::RandomIndex(kColorSteps, _random);
	particle.flipProgress = dollar
		? (base::RandomIndex(kRadiusResolution, _random)
			* 2. / kRadiusResolution)
		: 0.;
	particle.birthTime = now;
	const auto life = kLifeMin + base::RandomIndex(kLifeRand, _random);
	particle.deathTime = now
		+ (dollar ? crl::time(life * kDollarLifeScale) : life);
	particle.distance = 0.;
}

void StarParticles::ensureParticles() {
	if (!_particles.empty()) {
		return;
	}
	const auto now = crl::now();
	const auto perMs = driftStep(crl::time(1));
	_particles.resize((_glyph == Glyph::Dollar)
		? kDollarParticleCount
		: (_glyph == Glyph::Star)
		? kGlyphParticleCount
		: kParticleCount);
	for (auto &particle : _particles) {
		createParticle(now, particle);
		const auto life = particle.deathTime - particle.birthTime;
		const auto shift = base::RandomIndex(
			int(std::max<crl::time>(life, 1)),
			_random);
		particle.birthTime = now - shift;
		particle.deathTime = particle.birthTime + life;
		particle.distance = perMs * shift;
	}
	_lastTime = now;
}

void StarParticles::tick(crl::time now) {
	ensureParticles();
	const auto delta = std::clamp(now - _lastTime, kMinDelta, kMaxDelta);
	_lastTime = now;
	updateSpeedScale(now);

	const auto radius = _field.isEmpty()
		? 0.
		: std::min(_field.width(), _field.height()) / 2.;
	const auto bound = radius * kBoundsFactor;
	const auto step = driftStep(delta) * _speedScale;
	const auto flipStep = (delta / kMsPerSecond)
		* kDollarFlipPerSecond
		* _speedScale;
	for (auto &particle : _particles) {
		if (now >= particle.deathTime) {
			createParticle(now, particle);
			continue;
		}
		const auto dollar = (_glyph == Glyph::Dollar) && (particle.sizeIndex == 0);
		particle.distance += dollar ? (step * kDollarSpeedScale) : step;
		if (dollar) {
			particle.flipProgress += flipStep;
		}
		if (radius > 0.
			&& (particle.radiusFactor * radius + particle.distance) > bound) {
			createParticle(now, particle);
		}
	}
	for (auto i = 0; i != int(_fieldAngle.size()); ++i) {
		_fieldAngle[i] += kRotationDegPerSec[i]
			* (delta / kMsPerSecond)
			* kDegreesToRadians;
		_fieldAngle[i] -= k2Pi * std::floor(_fieldAngle[i] / k2Pi);
	}

	if (radius > 0.) {
		const auto center = rect::center(_field);
		const auto margin = bound + _maxSpriteExtent;
		_rectToUpdate |= QRectF(
			center - QPointF(margin, margin),
			Size(2. * margin)).toAlignedRect();
	}
}

void StarParticles::rebuildSprites(int ratio) {
	_spritesDirty = false;
	_spritesRatio = ratio;
	const auto round = style::ConvertScale(kRoundDp) * ratio;
	const auto pad = int(base::SafeRound(round)) + ratio;
	const auto bakePath = [&](int side, QColor fill) {
		const auto full = side + 2 * pad;
		auto image = QImage(full, full, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			auto hq = PainterHighQualityEnabler(p);
			p.translate(pad, pad);
			auto pen = QPen(fill);
			pen.setWidthF(round * 2.);
			pen.setJoinStyle(Qt::RoundJoin);
			pen.setCapStyle(Qt::RoundCap);
			p.setPen(pen);
			p.setBrush(fill);
			p.drawPath(StarPath(side));
		}
		const auto soft = std::max(
			1,
			int(base::SafeRound(full * kSoftScale)));
		image = image.scaled(
			soft,
			soft,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation
		).scaled(
			full,
			full,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		image.setDevicePixelRatio(ratio);
		return Sprite{
			.image = std::move(image),
			.size = Size(full / float64(ratio)),
		};
	};
	auto glyph = (_glyph == Glyph::Dollar)
		? std::make_unique<QSvgRenderer>(
			u":/gui/icons/settings/premium_dollar.svg"_q)
		: (_glyph == Glyph::Star)
		? std::make_unique<QSvgRenderer>(u":/gui/icons/settings/star.svg"_q)
		: nullptr;
	const auto bakeGlyph = [&](int side, QColor fill) {
		const auto full = side + 2 * pad;
		auto image = QImage(full, full, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			glyph->render(&p, QRectF(pad, pad, side, side));
			p.setCompositionMode(QPainter::CompositionMode_SourceIn);
			p.fillRect(QRectF(0, 0, full, full), fill);
		}
		image.setDevicePixelRatio(ratio);
		return Sprite{
			.image = std::move(image),
			.size = Size(full / float64(ratio)),
		};
	};
	const auto sizes = (_glyph == Glyph::Dollar)
		? kDollarSizesDp
		: (_glyph == Glyph::Star)
		? std::array<float64, 3>{ { 8., 9., 10. } }
		: kSizesDp;
	_maxSpriteExtent = 0.;
	for (auto i = 0; i != int(_sprites.size()); ++i) {
		const auto side = std::max(
			1,
			int(base::SafeRound(style::ConvertScale(sizes[i]) * ratio)));
		const auto useGlyph = (glyph != nullptr) && (i == 0);
		for (auto j = 0; j != kColorSteps; ++j) {
			const auto t = (kColorSteps > 1)
				? (j / float64(kColorSteps - 1))
				: 0.;
			auto fill = anim::color(_color1, _color2, t);
			fill.setAlpha((useGlyph && (_glyph == Glyph::Dollar))
				? kDollarSpriteAlpha
				: kSpriteAlpha);
			_sprites[i][j] = useGlyph
				? bakeGlyph(side, fill)
				: bakePath(side, fill);
		}
		_maxSpriteExtent = std::max(
			_maxSpriteExtent,
			_sprites[i][0].size.width());
	}
}

void StarParticles::paint(QPainter &p, const QRectF &field) {
	if (!_color1.isValid() || field.isEmpty()) {
		return;
	}
	ensureParticles();
	_idleCounter = 0;
	const auto ratio = std::max(
		int(std::ceil(p.device()->devicePixelRatioF())),
		kMinSoftRatio);
	if (_spritesDirty || _spritesRatio != ratio) {
		rebuildSprites(ratio);
	}
	if (!_paused && !_animation.animating()) {
		_lastTime = crl::now();
		_animation.start();
	}
	_field = field;

	const auto center = rect::center(field);
	const auto radius = std::min(field.width(), field.height()) / 2.;
	const auto now = (_paused && _pausedAt) ? _pausedAt : crl::now();
	const auto baseOpacity = p.opacity();
	for (const auto &particle : _particles) {
		const auto age = now - particle.birthTime;
		const auto remaining = particle.deathTime - now;
		if (age < 0 || remaining <= 0) {
			continue;
		}
		const auto appear = std::clamp(age / kAppearMs, 0., 1.);
		const auto scale = Overshoot(appear);
		if (scale <= 0.) {
			continue;
		}
		const auto fade = std::clamp(remaining / kFadeOutSpan, 0., 1.);
		const auto alpha = particle.alpha * fade;
		const auto radial = particle.radiusFactor * radius
			+ particle.distance;
		const auto theta = particle.angle
			+ ((_glyph == Glyph::Dollar)
				? 0.
				: _fieldAngle[particle.sizeIndex]);
		const auto position = QPointF(
			center.x() + radial * std::cos(theta),
			center.y() + radial * std::sin(theta));
		const auto &sprite
			= _sprites[particle.sizeIndex][particle.colorIndex];
		const auto width = sprite.size.width() * scale;
		const auto height = sprite.size.height() * scale;
		p.setOpacity(baseOpacity * alpha);
		if ((_glyph == Glyph::Dollar) && (particle.sizeIndex == 0)) {
			const auto flip = std::cos(M_PI * particle.flipProgress);
			p.save();
			p.translate(position);
			p.scale(flip, 1.);
			p.drawImage(
				QRectF(-width / 2., -height / 2., width, height),
				sprite.image);
			p.restore();
		} else {
			p.drawImage(
				QRectF(
					position.x() - width / 2.,
					position.y() - height / 2.,
					width,
					height),
				sprite.image);
		}
	}
	p.setOpacity(baseOpacity);
}

} // namespace Ui::Premium
