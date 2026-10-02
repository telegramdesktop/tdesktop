/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/star_burst.h"

#include "ui/effects/animation_value.h"
#include "ui/effects/drifting_particles.h"

#include <QtCore/QFile>
#include <QtCore/QtMath>

namespace Ui {
namespace {

[[nodiscard]] float64 Value(
		ParticlesRandom &random,
		const StarBurstRange &range) {
	return random.value(range.from, range.till);
}

} // namespace

StarBurst::StarBurst(const StarBurstDescriptor &descriptor)
: _appearTill(descriptor.appearTill)
, _fadeAfter(descriptor.fadeAfter)
, _deformation(descriptor.deformation)
, _duration(descriptor.delay + descriptor.spread + descriptor.lifeMax) {
	const auto path = u":/gui/icons/settings/starmini.svg"_q;
	if (const auto color = descriptor.color) {
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			_sprite.load(file.readAll().replace(
				QByteArray("#fff"),
				color->name().toUtf8()));
		}
	} else {
		_sprite.load(path);
	}
	if (!_sprite.isValid()) {
		return;
	}
	auto total = 0;
	for (const auto &side : descriptor.sides) {
		total += side.count;
	}
	auto random = ParticlesRandom();
	_stars.reserve(total);
	for (const auto &side : descriptor.sides) {
		for (auto i = 0; i != side.count; ++i) {
			_stars.push_back({
				.birth = descriptor.delay
					+ crl::time(random.value(0., descriptor.spread)),
				.life = crl::time(random.value(
					descriptor.lifeMin,
					descriptor.lifeMax)),
				.side = side.sign,
				.angle = Value(random, side.angle),
				.reach = Value(random, side.reach),
				.fall = Value(random, descriptor.fall),
				.start = QPointF(
					side.sign * Value(random, descriptor.startX),
					Value(random, descriptor.startY)),
				.size = Value(random, descriptor.size),
				.alpha = Value(random, descriptor.alpha),
				.sinFactor = Value(random, descriptor.twinkle)
					* (random.chance(2) ? -1. : 1.),
			});
		}
	}
}

std::unique_ptr<StarBurst> StarBurst::Make(
		const StarBurstDescriptor &descriptor) {
	auto result = std::unique_ptr<StarBurst>(new StarBurst(descriptor));
	return result->_sprite.isValid() ? std::move(result) : nullptr;
}

crl::time StarBurst::duration() const {
	return _duration;
}

void StarBurst::paint(QPainter &p, const StarBurstFrame &frame) const {
	const auto origin = frame.origin;
	const auto emitter = frame.emitter;
	const auto extent = frame.extent;
	p.save();
	if (!frame.clip.isEmpty()) {
		p.setClipPath(frame.clip, Qt::IntersectClip);
	}
	const auto opacity = p.opacity();
	for (const auto &star : _stars) {
		const auto elapsed = frame.elapsed - star.birth;
		if (elapsed < 0 || elapsed >= star.life) {
			continue;
		}
		const auto progress = elapsed / float64(star.life);
		const auto appear = std::clamp(progress / _appearTill, 0., 1.);
		const auto fade = 1. - std::clamp(
			(progress - _fadeAfter) / (1. - _fadeAfter),
			0.,
			1.);
		const auto travel = anim::easeOutCubic(1., progress);
		const auto radians = star.angle * M_PI / 180.;
		const auto centre = origin
			+ star.start * emitter
			+ QPointF(star.side * std::cos(radians), std::sin(radians))
				* (star.reach * extent * travel)
			+ QPointF(0., star.fall * extent * progress * progress);
		const auto deformH = 1. + _deformation
			* std::sin(star.sinFactor * progress * 2. * M_PI);
		const auto deformW = 1. / deformH;
		const auto side = star.size * extent * appear;
		const auto width = side * fade * deformW;
		const auto height = side * deformH;
		p.setOpacity(opacity * star.alpha * appear * fade);
		_sprite.render(
			&p,
			QRectF(
				centre.x() - width / 2.,
				centre.y() - height / 2.,
				width,
				height));
	}
	p.setOpacity(opacity);
	p.restore();
}

} // namespace Ui
