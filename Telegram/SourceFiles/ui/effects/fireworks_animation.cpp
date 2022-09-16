/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/fireworks_animation.h"

#include "base/random.h"
#include "ui/painter.h"

namespace Ui {
namespace {

constexpr auto kParticlesCount = 60;
constexpr auto kFallCount = 30;
constexpr auto kFirstUpdateTime = crl::time(16);
constexpr auto kFireworkWidth = 480;
constexpr auto kFireworkHeight = 320;

QBrush Brush(int color) {
	return QBrush{ QColor(
		color & 0xFF,
		(color >> 8) & 0xFF,
		(color >> 16) & 0xFF)
	};
}

std::vector<QBrush> PrepareBrushes() {
	return {
		Brush(0xff2CBCE8),
		Brush(0xff9E04D0),
		Brush(0xffFECB02),
		Brush(0xffFD2357),
		Brush(0xff278CFE),
		Brush(0xff59B86C),
	};
}

[[nodiscard]] float64 RandomFloat01() {
	return base::RandomValue<uint32>()
		/ float64(std::numeric_limits<uint32>::max());
}

} // namespace

FireworksAnimation::FireworksAnimation(Fn<void()> repaint)
: _brushes(PrepareBrushes())
, _animation([=](crl::time now) { update(now); })
, _repaint(std::move(repaint)) {
	_smallSide = style::ConvertScale(2);
	_particles.reserve(kParticlesCount + kFallCount);
	for (auto i = 0; i != kParticlesCount; ++i) {
		initParticle(_particles.emplace_back(), false);
	}
	_animation.start();
}

void FireworksAnimation::update(crl::time now) {
	const auto passed = _lastUpdate ? (now - _lastUpdate) : kFirstUpdateTime;
	_lastUpdate = now;
	auto allFinished = true;
	for (auto &particle : _particles) {
		updateParticle(particle, passed);
		if (!particle.finished) {
			allFinished = false;
		}
	}
	if (allFinished) {
		_animation.stop();
	} else if (_fallingDown >= kParticlesCount / 2 && _speedCoef > 0.2) {
		startFall();
		_speedCoef -= passed / 16.0 * 0.15;
		if (_speedCoef < 0.2) {
			_speedCoef = 0.2;
		}
	}
	_repaint();
}

bool FireworksAnimation::paint(QPainter &p, const QRect &rect) {
	if (rect.isEmpty()) {
		return false;
	}
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setClipRect(rect);
	for (auto &particle : _particles) {
		if (!particle.finished) {
			paintParticle(p, particle, rect);
		}
	}
	p.setClipping(false);
	return _animation.animating();
}

void FireworksAnimation::paintParticle(
		QPainter &p,
		const Particle &particle,
		const QRect &rect) {
	const auto size = particle.size;
	const auto x = rect.x() + (particle.x * rect.width() / kFireworkWidth);
	const auto y = rect.y() + (particle.y * rect.height() / kFireworkHeight);
	p.setBrush(_brushes[particle.color]);
	if (particle.type == Particle::Type::Circle) {
		p.drawEllipse(x, y, size, size);
	} else {
		const auto rect = QRect(-size, -_smallSide, size, _smallSide);
		p.save();
		p.translate(x, y);
		p.rotate(particle.rotation);
		p.drawRoundedRect(rect, _smallSide, _smallSide);
		p.restore();
	}
}

void FireworksAnimation::updateParticle(Particle &particle, crl::time dt) {
	if (particle.finished) {
		return;
	}
	const auto moveCoef = dt / 16.;
	particle.x += particle.moveX * moveCoef;
	particle.y += particle.moveY * moveCoef;
	if (particle.xFinished != 0) {
		const auto dp = 0.5;
		if (particle.xFinished == 1) {
			particle.moveX += dp * moveCoef * 0.05;
			if (particle.moveX >= dp) {
				particle.xFinished = 2;
			}
		} else {
			particle.moveX -= dp * moveCoef * 0.05f;
			if (particle.moveX <= -dp) {
				particle.xFinished = 1;
			}
		}
	} else {
		if (particle.right) {
			if (particle.moveX < 0) {
				particle.moveX += moveCoef * 0.05f;
				if (particle.moveX >= 0) {
					particle.moveX = 0;
					particle.xFinished = particle.finishedStart;
				}
			}
		} else {
			if (particle.moveX > 0) {
				particle.moveX -= moveCoef * 0.05f;
				if (particle.moveX <= 0) {
					particle.moveX = 0;
					particle.xFinished = particle.finishedStart;
				}
			}
		}
	}
	const auto yEdge = -0.5;
	const auto wasNegative = (particle.moveY < yEdge);
	if (particle.moveY > yEdge) {
		particle.moveY += (1. / 3.) * moveCoef * _speedCoef;
	} else {
		particle.moveY += (1. / 3.) * moveCoef;
	}
	if (wasNegative && particle.moveY > yEdge) {
		++_fallingDown;
	}
	if (particle.type == Particle::Type::Rectangle) {
		particle.rotation += moveCoef * 10;
		if (particle.rotation > 360) {
			particle.rotation -= 360;
		}
	}
	if (particle.y >= kFireworkHeight) {
		particle.finished = true;
	}
}

void FireworksAnimation::startFall() {
	if (_startedFall) {
		return;
	}
	_startedFall = true;
	for (auto i = 0; i != kFallCount; ++i) {
		initParticle(_particles.emplace_back(), true);
	}
}

void FireworksAnimation::initParticle(Particle &particle, bool falling) {
	using Type = Particle::Type;
	using base::RandomIndex;

	particle.color = RandomIndex(_brushes.size());
	particle.type = RandomIndex(2) ? Type::Rectangle : Type::Circle;
	particle.right = (RandomIndex(2) == 1);
	particle.finishedStart = 1 + RandomIndex(2);
	if (particle.type == Type::Circle) {
		particle.size = style::ConvertScale(6 + RandomFloat01() * 3);
	} else {
		particle.size = style::ConvertScale(6 + RandomFloat01() * 6);
		particle.rotation = RandomIndex(360);
	}
	if (falling) {
		particle.y = -RandomFloat01() * kFireworkHeight * 1.2f;
		particle.x = 5 + RandomIndex(kFireworkWidth - 10);
		particle.xFinished = particle.finishedStart;
	} else {
		const auto xOffset = 4 + RandomIndex(10);
		const auto yOffset = kFireworkHeight / 4;
		if (particle.right) {
			particle.x = kFireworkWidth + xOffset;
		} else {
			particle.x = -xOffset;
		}
		particle.moveX = (particle.right ? -1 : 1) * (1.2 + RandomFloat01() * 4);
		particle.moveY = -(4 + RandomFloat01() * 4);
		particle.y = yOffset / 2 + RandomIndex(yOffset * 2);
	}
}

} // namespace Ui
