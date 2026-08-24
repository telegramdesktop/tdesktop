/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/drifting_particles.h"

#include "ui/painter.h"
#include "ui/rect.h"

#include <QtCore/QtMath>

namespace Ui {
namespace {

constexpr auto kMinLifeTime = crl::time(2000);
constexpr auto kRandLifeTime = 1000;
constexpr auto kFadeOutTime = crl::time(200);
constexpr auto kFadeOutDuration = 150.;
constexpr auto kAppearDuration = 200.;
constexpr auto kSpeedDivider = 660.;
constexpr auto kMinDelta = crl::time(4);
constexpr auto kMaxDelta = crl::time(50);
constexpr auto kOvershootTension = 2.;
constexpr auto kDistributionTries = 10;
constexpr auto kRandomRotateDegrees = 45;
constexpr auto kOrbitPeriod = 40000.;
constexpr auto kOrbitPeriodStep = 10000.;

[[nodiscard]] float64 Overshoot(float64 progress) {
	const auto t = progress - 1.;
	return t * t * ((kOvershootTension + 1.) * t + kOvershootTension) + 1.;
}

[[nodiscard]] QPainterPath FourPointStarPath(
		int size,
		float64 corner,
		int ratio) {
	const auto half = (size * ratio / 2) / float64(ratio);
	const auto mid = int(half * ratio * corner) / float64(ratio);
	auto result = QPainterPath();
	result.moveTo(0, half);
	result.lineTo(mid, mid);
	result.lineTo(half, 0);
	result.lineTo(size - mid, mid);
	result.lineTo(size, half);
	result.lineTo(size - mid, size - mid);
	result.lineTo(half, size);
	result.lineTo(mid, size - mid);
	result.closeSubpath();
	return result;
}

[[nodiscard]] QPainterPath RoundCorners(
		const QPainterPath &path,
		float64 radius) {
	auto points = std::vector<QPointF>();
	for (auto i = 0; i != path.elementCount(); ++i) {
		const auto element = path.elementAt(i);
		if (element.type == QPainterPath::CurveToElement
			|| element.type == QPainterPath::CurveToDataElement) {
			return path;
		}
		points.push_back(QPointF(element.x, element.y));
	}
	const auto count = int(points.size());
	if (count < 3) {
		return path;
	}
	const auto cut = [&](QPointF from, QPointF to) {
		const auto delta = to - from;
		const auto length = std::hypot(delta.x(), delta.y());
		return (length > 0.)
			? (from + delta * (std::min(radius, length / 2.) / length))
			: from;
	};
	auto result = QPainterPath();
	for (auto i = 0; i != count; ++i) {
		const auto previous = points[(i + count - 1) % count];
		const auto current = points[i];
		const auto next = points[(i + 1) % count];
		if (!i) {
			result.moveTo(cut(current, previous));
		} else {
			result.lineTo(cut(current, previous));
		}
		result.quadTo(current, cut(current, next));
	}
	result.closeSubpath();
	return result;
}

void BlurCoverage(
		std::vector<int> &values,
		int width,
		int height,
		int radius) {
	const auto div = 2 * radius + 1;
	auto blurred = std::vector<int>(values.size());
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != width; ++x) {
			auto sum = 0;
			for (auto i = -radius; i <= radius; ++i) {
				sum += values[y * width + std::clamp(x + i, 0, width - 1)];
			}
			blurred[y * width + x] = sum / div;
		}
	}
	for (auto x = 0; x != width; ++x) {
		for (auto y = 0; y != height; ++y) {
			auto sum = 0;
			for (auto i = -radius; i <= radius; ++i) {
				sum += blurred[std::clamp(y + i, 0, height - 1) * width + x];
			}
			values[y * width + x] = sum / div;
		}
	}
}

} // namespace

QImage FourPointStarImage(FourPointStarArgs args) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		Size(args.size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto path = FourPointStarPath(args.size, args.corner, ratio);
	if (args.cornerRadius > 0.) {
		path = RoundCorners(path, args.cornerRadius);
	}
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawPath(path);
	}
	const auto width = result.width();
	const auto height = result.height();
	auto coverage = std::vector<int>(width * height);
	for (auto y = 0; y != height; ++y) {
		const auto line = reinterpret_cast<const uint32*>(
			result.constScanLine(y));
		for (auto x = 0; x != width; ++x) {
			coverage[y * width + x] = qAlpha(line[x]);
		}
	}
	if (args.blurRadius > 0) {
		BlurCoverage(coverage, width, height, args.blurRadius);
	}
	const auto red = args.color.red();
	const auto green = args.color.green();
	const auto blue = args.color.blue();
	const auto alpha = args.color.alpha();
	for (auto y = 0; y != height; ++y) {
		const auto line = reinterpret_cast<uint32*>(result.scanLine(y));
		for (auto x = 0; x != width; ++x) {
			const auto value = coverage[y * width + x] * alpha / 255;
			line[x] = qRgba(
				red * value / 255,
				green * value / 255,
				blue * value / 255,
				value);
		}
	}
	return result;
}

DriftingParticles::DriftingParticles(Config config)
: _config(std::move(config))
, _particles(_config.count)
, _orbitAngles(_config.sprites.size(), 0.) {
}

void DriftingParticles::setGeometry(
		QRectF field,
		QRectF bounds,
		QRectF exclude) {
	if (_field == field && _bounds == bounds && _exclude == exclude) {
		return;
	}
	_field = field;
	_bounds = bounds;
	_exclude = exclude;

	const auto now = crl::now() - _pauseOffset;
	for (auto &particle : _particles) {
		generate(particle, now);
	}
}

void DriftingParticles::setPaused(bool paused) {
	if (paused) {
		if (!_pausedAt) {
			_pausedAt = crl::now();
		}
	} else if (_pausedAt) {
		_pauseOffset += crl::now() - _pausedAt;
		_pausedAt = 0;
	}
}

QPointF DriftingParticles::generatePosition() {
	if (_config.spread) {
		auto bestDistance = 0.;
		auto best = QPointF(
			_random.value(_field.left(), _field.right()),
			_random.value(_field.top(), _field.bottom()));
		for (auto i = 0; i != kDistributionTries; ++i) {
			const auto candidate = QPointF(
				_random.value(_field.left(), _field.right()),
				_random.value(_field.top(), _field.bottom()));
			auto minDistance = std::numeric_limits<float64>::max();
			for (const auto &particle : _particles) {
				const auto dx = particle.x - candidate.x();
				const auto dy = particle.y - candidate.y();
				minDistance = std::min(minDistance, dx * dx + dy * dy);
			}
			if (minDistance > bestDistance) {
				bestDistance = minDistance;
				best = candidate;
			}
		}
		return best;
	} else if (_config.spawnInCircle) {
		const auto exclude = _config.excludeRadius;
		const auto radius = exclude
			+ _random.value() * (_field.width() - exclude);
		const auto angle = _random.value() * 2. * M_PI;
		const auto center = rect::center(_field);
		return QPointF(
			center.x() + radius * std::sin(angle),
			center.y() + radius * std::cos(angle));
	}
	return QPointF(
		_random.value(_field.left(), _field.right()),
		_random.value(_field.top(), _field.bottom()));
}

void DriftingParticles::generate(Particle &particle, crl::time now) {
	const auto index = _random.index(int(_config.sprites.size()));
	const auto &sprite = _config.sprites[index];
	particle.sprite = index;
	particle.bornTime = now;
	particle.lifeTime = now + kMinLifeTime + _random.index(kRandLifeTime);
	particle.rotate = sprite.randomRotate
		? _random.value(-kRandomRotateDegrees, kRandomRotateDegrees)
		: 0.;

	const auto position = generatePosition();
	particle.x = position.x();
	particle.y = position.y();

	const auto center = rect::center(_field);
	const auto angle = std::atan2(
		particle.y - center.y(),
		particle.x - center.x());
	particle.vecX = std::cos(angle);
	particle.vecY = std::sin(angle);
	particle.alpha = int(base::SafeRound(
		_random.value(0.5, 1.) * sprite.maxAlpha));
}

QPointF DriftingParticles::drawingPosition(const Particle &particle) const {
	if (!_config.orbit) {
		return QPointF(particle.x, particle.y);
	}
	const auto center = rect::center(_field);
	const auto radians = _orbitAngles[particle.sprite] * M_PI / 180.;
	const auto cosine = std::cos(radians);
	const auto sine = std::sin(radians);
	const auto dx = particle.x - center.x();
	const auto dy = particle.y - center.y();
	return QPointF(
		center.x() + dx * cosine - dy * sine,
		center.y() + dx * sine + dy * cosine);
}

void DriftingParticles::paint(QPainter &p) {
	if (_field.isEmpty()) {
		return;
	}
	const auto paused = (_pausedAt != 0);
	const auto now = (paused ? _pausedAt : crl::now()) - _pauseOffset;
	const auto delta = float64(
		std::clamp(now - _lastTime, kMinDelta, kMaxDelta));
	_lastTime = now;

	if (_config.orbit && !paused) {
		for (auto i = 0; i != int(_orbitAngles.size()); ++i) {
			_orbitAngles[i] += 360.
				* (delta / (kOrbitPeriod + i * kOrbitPeriodStep));
		}
	}
	const auto speed = _config.speed * (delta / kSpeedDivider);
	auto hq = PainterHighQualityEnabler(p);
	const auto opacity = p.opacity();
	for (auto &particle : _particles) {
		const auto position = drawingPosition(particle);
		if (!_exclude.contains(position)) {
			const auto left = particle.lifeTime - now;
			const auto out = (left < kFadeOutTime)
				? std::clamp(1. - (left / kFadeOutDuration), 0., 1.)
				: 0.;
			const auto appear = std::clamp(
				(now - particle.bornTime) / kAppearDuration,
				0.,
				1.);
			const auto scale = (appear < 1.) ? Overshoot(appear) : 1.;
			const auto &image = _config.sprites[particle.sprite].image;
			const auto size = image.size() / image.devicePixelRatio();
			const auto target = QRectF(
				position.x() - size.width() / 2.,
				position.y() - size.height() / 2.,
				size.width(),
				size.height());
			p.setOpacity(opacity * (particle.alpha / 255.) * (1. - out));
			if (scale == 1. && !particle.rotate) {
				p.drawImage(target.topLeft(), image);
			} else {
				p.save();
				p.translate(position);
				p.scale(scale, scale);
				p.rotate(particle.rotate);
				p.translate(-position);
				p.drawImage(target, image);
				p.restore();
			}
		}
		if (!paused) {
			particle.x += particle.vecX * speed;
			particle.y += particle.vecY * speed;
		}
		if (now > particle.lifeTime
			|| (_config.checkBounds && !_bounds.contains(position))) {
			generate(particle, now);
		}
	}
	p.setOpacity(opacity);
}

} // namespace Ui
