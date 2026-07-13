/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/voice_once_particles.h"

#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "base/random.h"

namespace Ui {
namespace {

constexpr auto kMaxDelta = crl::time(20);

} // namespace

TimerParticles::TimerParticles(int count) : _count(count) {
}

void TimerParticles::update(crl::time dt) {
	for (auto i = _particles.begin(); i != _particles.end();) {
		if (i->currentTime >= i->lifeTime) {
			i = _particles.erase(i);
			continue;
		}
		const auto progress = i->currentTime / i->lifeTime;
		i->alpha = (1. - progress) * (1. - progress);
		i->x += i->vx * i->velocity * dt / 200.;
		i->y += i->vy * i->velocity * dt / 200.;
		i->currentTime += dt;
		++i;
	}
}

void TimerParticles::paint(
		QPainter &p,
		QPointF emit,
		QPointF direction,
		const QColor &color,
		float64 alpha) {
	const auto now = crl::now();
	const auto dt = _lastTime ? std::min(kMaxDelta, now - _lastTime) : 0;
	_lastTime = now;

	constexpr auto kMaxPerFrame = 3;
	constexpr auto kRandomPerParticle = 3;
	const auto free = _count - int(_particles.size());
	const auto count = std::min(std::clamp(free / 12, 1, kMaxPerFrame), free);
	const auto dx = direction.x();
	const auto dy = direction.y();
	auto random = std::array<uchar, kMaxPerFrame * kRandomPerParticle>();
	base::RandomFill(random.data(), count * kRandomPerParticle);
	auto index = 0;
	const auto next = [&] { return random[index++] / 256.; };
	for (auto i = 0; i != count; ++i) {
		const auto angle = (next() * 140. - 70.) * M_PI / 180.;
		const auto c = std::cos(angle);
		const auto s = std::sin(angle);
		auto particle = Particle();
		particle.x = emit.x();
		particle.y = emit.y();
		particle.vx = dx * c - dy * s;
		particle.vy = dx * s + dy * c;
		particle.alpha = 1.;
		particle.currentTime = 0.;
		particle.lifeTime = 400. + next() * 100.;
		particle.velocity = style::ConvertScaleExact(20. + next() * 4.);
		_particles.push_back(particle);
	}

	update(dt);

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	const auto radius = style::ConvertScaleExact(1.66) / 2.;
	for (const auto &particle : _particles) {
		auto c = color;
		c.setAlphaF(color.alphaF() * particle.alpha * alpha);
		p.setBrush(c);
		p.drawEllipse(QPointF(particle.x, particle.y), radius, radius);
	}
}

WaveformParticles::WaveformParticles(int count) : _count(count) {
}

void WaveformParticles::clear() {
	_particles.clear();
}

void WaveformParticles::paint(
		QPainter &p,
		const QRectF &emitArea,
		const QColor &color,
		float64 alpha) {
	const auto now = crl::now();
	const auto dt = _lastTime ? std::min(kMaxDelta, now - _lastTime) : 0;
	_lastTime = now;

	const auto gravity = style::ConvertScaleExact(0.33);
	for (auto i = _particles.begin(); i != _particles.end();) {
		i->t -= dt / i->d;
		if (i->t < 0.) {
			i = _particles.erase(i);
			continue;
		}
		i->x += i->vx * i->v * dt / 500.;
		i->y += i->vy * i->v * dt / 500.;
		i->vy -= gravity * dt / 500.;
		++i;
	}

	constexpr auto kMaxPerFrame = 4;
	constexpr auto kRandomPerParticle = 5;
	const auto emit = emitArea.isNull()
		? 0
		: std::min(kMaxPerFrame, _count - int(_particles.size()));
	if (emit > 0) {
		auto random = std::array<uchar, kMaxPerFrame * kRandomPerParticle>();
		base::RandomFill(random.data(), emit * kRandomPerParticle);
		auto index = 0;
		const auto next = [&] { return random[index++] / 256.; };
		for (auto i = 0; i != emit; ++i) {
			const auto angle = (next() * 200. - 125.) * M_PI / 180.;
			const auto c = std::cos(angle);
			const auto s = std::sin(angle);
			auto particle = Particle();
			particle.x = emitArea.left() + emitArea.width() * next();
			particle.y = emitArea.top() + emitArea.height() * next();
			particle.vx = (c - s) * 0.8;
			particle.vy = (s + c) - 0.2;
			particle.t = 1.;
			particle.v = style::ConvertScaleExact(10. + next() * 7.);
			particle.d = 420. + next() * (550. - 420.);
			_particles.push_back(particle);
		}
	}

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	const auto radius = style::ConvertScaleExact(1.33) / 2.;
	for (const auto &particle : _particles) {
		auto c = color;
		c.setAlphaF(color.alphaF() * alpha * particle.t);
		p.setBrush(c);
		p.drawEllipse(QPointF(particle.x, particle.y), radius, radius);
	}
}

} // namespace Ui
