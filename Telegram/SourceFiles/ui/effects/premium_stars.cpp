/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_stars.h"

#include "base/random.h"
#include "ui/effects/animation_value_f.h"

#include <QtCore/QtMath>

namespace Ui {
namespace Premium {

constexpr auto kDeformationMax = 0.1;

MiniStars::MiniStars(Fn<void(const QRect &r)> updateCallback, bool opaque)
: _availableAngles({
	Interval{ -10, 40 },
	Interval{ 180 + 10 - 40, 40 },
	Interval{ 180 + 15, 50 },
	Interval{ -15 - 50, 50 },
})
, _lifeLength({ 150, 200 })
, _deathTime({ 1500, 2000 })
, _size({ 10, 20 })
, _alpha({ opaque ? 100 : 40, opaque ? 100 : 60 })
, _sinFactor({ 10, 190 })
, _appearProgressTill(0.2)
, _disappearProgressAfter(0.8)
, _distanceProgressStart(0.5)
, _sprite(u":/gui/icons/settings/starmini.svg"_q)
, _animation([=](crl::time now) {
	if (now > _nextBirthTime && !_paused) {
		createStar(now);
		_nextBirthTime = now + randomInterval(_lifeLength);
	}
	if (_rectToUpdate.isValid()) {
		updateCallback(base::take(_rectToUpdate));
	}
}) {
	if (anim::Disabled()) {
		const auto from = _deathTime.from + _deathTime.length;
		for (auto i = -from; i < 0; i += randomInterval(_lifeLength)) {
			createStar(i);
		}
		updateCallback(_rectToUpdate);
	} else {
		_animation.start();
	}
}

int MiniStars::randomInterval(const Interval &interval) const {
	return interval.from + base::RandomIndex(interval.length);
}

crl::time MiniStars::timeNow() const {
	return anim::Disabled() ? 0 : crl::now();
}

void MiniStars::paint(Painter &p, const QRectF &rect) {
	const auto center = rect.center();
	const auto opacity = p.opacity();
	for (const auto &ministar : _ministars) {
		const auto progress = (timeNow() - ministar.birthTime)
			/ float64(ministar.deathTime - ministar.birthTime);
		if (progress > 1.) {
			continue;
		}
		const auto appearProgress = std::clamp(
			progress / _appearProgressTill,
			0.,
			1.);
		const auto rsin = float(std::sin(ministar.angle * M_PI / 180.));
		const auto rcos = float(std::cos(ministar.angle * M_PI / 180.));
		const auto end = QPointF(
			rect.width() / kSizeFactor * rcos,
			rect.height() / kSizeFactor * rsin);

		const auto alphaProgress = 1.
			- (std::clamp(progress - _disappearProgressAfter, 0., 1.)
				/ (1. - _disappearProgressAfter));
		p.setOpacity(ministar.alpha
			* alphaProgress
			* appearProgress
			* opacity);

		const auto deformResult = progress * 360;
		const auto rsinDeform = float(
			std::sin(ministar.sinFactor * deformResult * M_PI / 180.));
		const auto deformH = 1. + kDeformationMax * rsinDeform;
		const auto deformW = 1. / deformH;

		const auto distanceProgress = _distanceProgressStart + progress;
		const auto starSide = ministar.size * appearProgress;
		const auto widthFade = (std::abs(rcos) >= std::abs(rsin));
		const auto starWidth = starSide
			* (widthFade ? alphaProgress : 1.)
			* deformW;
		const auto starHeight = starSide
			* (!widthFade ? alphaProgress : 1.)
			* deformH;
		const auto renderRect = QRectF(
			center.x()
				+ anim::interpolateF(0, end.x(), distanceProgress)
				- starWidth / 2.,
			center.y()
				+ anim::interpolateF(0, end.y(), distanceProgress)
				- starHeight / 2.,
			starWidth,
			starHeight);
		_sprite.render(&p, renderRect);
		_rectToUpdate |= renderRect.toRect();
	}
	p.setOpacity(opacity);
}

void MiniStars::setPaused(bool paused) {
	_paused = paused;
}

int MiniStars::angle() const {
	const auto &interval = _availableAngles[
		base::RandomIndex(_availableAngles.size())];
	return base::RandomIndex(interval.length) + interval.from;
}

void MiniStars::createStar(crl::time now) {
	auto ministar = MiniStar{
		.birthTime = now,
		.deathTime = now + randomInterval(_deathTime),
		.angle = angle(),
		.size = float64(randomInterval(_size)),
		.alpha = float64(randomInterval(_alpha)) / 100.,
		.sinFactor = randomInterval(_sinFactor) / 100.
			* (base::RandomIndex(2) == 1 ? 1. : -1.),
	};
	for (auto i = 0; i < _ministars.size(); i++) {
		if (ministar.birthTime > _ministars[i].deathTime) {
			_ministars[i] = ministar;
			return;
		}
	}
	_ministars.push_back(ministar);
}


} // namespace Premium
} // namespace Ui
