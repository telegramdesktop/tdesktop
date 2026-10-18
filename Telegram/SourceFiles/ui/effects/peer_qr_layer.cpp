/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_layer.h"

#include "ui/effects/animation_value.h"

#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kFalloffMin = 0.15;
constexpr auto kTableSize = 64;
constexpr auto kHeading = 225.;
constexpr auto kWanderAmplitude = 1.2;
constexpr auto kWanderSpeed = 0.025;
constexpr auto kWanderSlowAmplitude = 0.6;
constexpr auto kWanderSlowSpeed = 0.016;
constexpr auto kWanderSlowPhase = 1.;
constexpr auto kDriftStep = crl::time(1000);
constexpr auto kGlideHalfLife = crl::time(250);
constexpr auto kVelocityWindow = crl::time(100);
constexpr auto kStopGap = crl::time(40);
constexpr auto kDriftResumeDelay = crl::time(1700);
constexpr auto kDriftResumeRamp = crl::time(500);

using Table = std::array<float64, kTableSize + 1>;

struct AxisPlace {
	float64 screen = 0.;
	float64 factor = 1.;
};

[[nodiscard]] float64 Factor(float64 t) {
	const auto u = std::clamp(t, 0., 1.);
	return kFalloffMin + (1. - kFalloffMin) * u * u * (3. - 2. * u);
}

[[nodiscard]] Table BuildTable() {
	auto result = Table();
	const auto step = 1. / kTableSize;
	for (auto i = kTableSize; i != 0; --i) {
		const auto till = i * step;
		const auto from = till - step;
		const auto simpson = 1. / Factor(from)
			+ 4. / Factor(from + step / 2.)
			+ 1. / Factor(till);
		result[i - 1] = result[i] + simpson * step / 6.;
	}
	return result;
}

[[nodiscard]] const Table &Integrals() {
	static const auto result = BuildTable();
	return result;
}

[[nodiscard]] float64 Integral(float64 t) {
	const auto &table = Integrals();
	const auto position = std::clamp(t, 0., 1.) * kTableSize;
	const auto index = std::min(int(position), kTableSize - 1);
	const auto fraction = position - index;
	return table[index] + (table[index + 1] - table[index]) * fraction;
}

[[nodiscard]] float64 IntegralInverse(float64 value) {
	const auto &table = Integrals();
	const auto i = ranges::lower_bound(table, value, ranges::greater());
	const auto till = std::clamp(int(i - begin(table)), 1, kTableSize);
	const auto from = till - 1;
	const auto span = table[from] - table[till];
	const auto fraction = (span > 0.) ? ((table[from] - value) / span) : 0.;
	return (from + std::clamp(fraction, 0., 1.)) / kTableSize;
}

[[nodiscard]] float64 K(float64 d, float64 band) {
	if (d >= band) {
		return band - d;
	} else if (d >= 0.) {
		return band * Integral(d / band);
	}
	return band * Integrals().front() - d / kFalloffMin;
}

[[nodiscard]] float64 KInverse(float64 value, float64 band) {
	const auto edge = band * Integrals().front();
	if (value <= 0.) {
		return band - value;
	} else if (value <= edge) {
		return band * IntegralInverse(value / band);
	}
	return (edge - value) * kFalloffMin;
}

// WHY: the factor follows the on-screen distance from the side, not the
// layer one, so the measured size curve holds wherever an item is dragged.
[[nodiscard]] AxisPlace MapAxis(
		float64 flat,
		float64 center,
		float64 half,
		float64 band) {
	if (band <= 0.) {
		return { .screen = flat };
	}
	const auto shift = flat - center;
	const auto d = KInverse(std::abs(shift) + K(half, band), band);
	const auto distance = half - d;
	return {
		.screen = center + ((shift < 0.) ? -distance : distance),
		.factor = (d >= 0.) ? Factor(d / band) : kFalloffMin,
	};
}

[[nodiscard]] float64 GlideRate() {
	return std::log(2.) * 1000. / kGlideHalfLife;
}

} // namespace

PeerQrLayerPlace PeerQrLayerFalloff::place(QPointF flat) const {
	const auto center = area.center();
	const auto x = MapAxis(flat.x(), center.x(), area.width() / 2., band);
	const auto y = MapAxis(flat.y(), center.y(), area.height() / 2., band);
	return {
		.position = QPointF(x.screen, y.screen),
		.scale = x.factor * y.factor,
	};
}

QRectF PeerQrLayerFalloff::reach(float64 margin) const {
	const auto flatHalf = [&](float64 half) {
		return K(0., band) - K(half, band) + margin;
	};
	const auto x = flatHalf(area.width() / 2.);
	const auto y = flatHalf(area.height() / 2.);
	const auto center = area.center();
	return QRectF(center.x() - x, center.y() - y, 2. * x, 2. * y);
}

void PeerQrLayerMotion::press(QPointF position, crl::time now) {
	advanceGlide(now);
	if (base::take(_glide)) {
		_restSince = now;
	}
	_pressPosition = position;
	_pressOffset = _offset;
	_samples.clear();
	_samples.push_back({ .time = now, .position = position });
	_pressed = true;
	_dragging = false;
}

void PeerQrLayerMotion::move(QPointF position, crl::time now) {
	if (!_pressed) {
		return;
	} else if (!_dragging && position != _pressPosition) {
		_dragging = true;
		_pressOffset = _offset;
	}
	if (_dragging) {
		_offset = _pressOffset + (position - _pressPosition);
	}
	_samples.push_back({ .time = now, .position = position });
	_samples.erase(
		begin(_samples),
		ranges::lower_bound(
			_samples,
			now - kVelocityWindow,
			ranges::less(),
			&Sample::time));
}

void PeerQrLayerMotion::release(crl::time now, bool glide) {
	if (!base::take(_pressed) || !base::take(_dragging)) {
		return;
	}
	const auto velocity = glide ? releaseVelocity(now) : QPointF();
	const auto speed = std::hypot(velocity.x(), velocity.y());
	if (speed <= 0.) {
		_restSince = now;
		return;
	}
	const auto rate = GlideRate();
	const auto start = std::min(speed, float64(st::peerQrLayerGlideMax));
	const auto brake = float64(st::peerQrLayerGlideBrake);
	const auto seconds = std::log(1. + rate * start / brake) / rate;
	_glide = Glide{
		.from = _offset,
		.direction = velocity / speed,
		.speed = start,
		.startedAt = now,
		.duration = crl::time(seconds * 1000.),
	};
}

void PeerQrLayerMotion::advance(
		crl::time now,
		crl::time elapsed,
		float64 driftSpeed) {
	advanceGlide(now);
	if (elapsed < _driftElapsed) {
		_driftElapsed = elapsed;
	}
	const auto scale = driftScale(now);
	while (_driftElapsed < elapsed) {
		const auto step = std::min(elapsed - _driftElapsed, kDriftStep);
		const auto middle = (_driftElapsed + step / 2.) / 1000.;
		const auto wander = kWanderAmplitude * std::sin(kWanderSpeed * middle)
			+ kWanderSlowAmplitude * std::sin(
				kWanderSlowSpeed * middle + kWanderSlowPhase);
		const auto heading = kHeading * M_PI / 180. + wander;
		_offset += QPointF(std::cos(heading), std::sin(heading))
			* (driftSpeed * step / 1000. * scale);
		_driftElapsed += step;
	}
}

QPointF PeerQrLayerMotion::offset() const {
	return _offset;
}

bool PeerQrLayerMotion::pressed() const {
	return _pressed;
}

bool PeerQrLayerMotion::animating() const {
	return _dragging || _glide.has_value();
}

QPointF PeerQrLayerMotion::releaseVelocity(crl::time now) const {
	const auto from = ranges::lower_bound(
		_samples,
		now - kVelocityWindow,
		ranges::less(),
		&Sample::time);
	if (end(_samples) - from < 2) {
		return QPointF();
	}
	const auto &first = *from;
	const auto &last = _samples.back();
	if (last.time < now - kStopGap || last.time == first.time) {
		return QPointF();
	}
	return (last.position - first.position)
		* (1000. / (last.time - first.time));
}

float64 PeerQrLayerMotion::driftScale(crl::time now) const {
	if (_dragging || _glide) {
		return 0.;
	} else if (!_restSince) {
		return 1.;
	}
	const auto resumeAt = _restSince + kDriftResumeDelay;
	if (now <= resumeAt) {
		return 0.;
	}
	const auto progress = float64(now - resumeAt) / kDriftResumeRamp;
	return anim::sineInOut(1., std::min(progress, 1.));
}

void PeerQrLayerMotion::advanceGlide(crl::time now) {
	if (!_glide) {
		return;
	}
	const auto &glide = *_glide;
	const auto rate = GlideRate();
	const auto terminal = st::peerQrLayerGlideBrake / rate;
	const auto passed = std::clamp(
		now - glide.startedAt,
		crl::time(),
		glide.duration);
	const auto t = passed / 1000.;
	const auto distance = (glide.speed + terminal)
		* (1. - std::exp(-rate * t))
		/ rate
		- terminal * t;
	_offset = glide.from + glide.direction * distance;
	if (passed >= glide.duration) {
		_restSince = glide.startedAt + glide.duration;
		_glide = std::nullopt;
	}
}

} // namespace Ui
