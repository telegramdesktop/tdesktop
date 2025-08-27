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
namespace {

using Type = MiniStarsType;
constexpr auto kDeformationMax = 0.1;

} // namespace

MiniStars::MiniStars(
	Fn<void(const QRect &r)> updateCallback,
	bool opaque,
	Type type)
: _availableAngles((type != Type::SlowStars && type != Type::SlowDiamondStars)
	? std::vector<Interval>{
		Interval{ -10, 40 },
		Interval{ 180 + 10 - 40, 40 },
		Interval{ 180 + 15, 50 },
		Interval{ -15 - 50, 50 },
	}
	: std::vector<Interval>{ Interval{ -90, 180 }, Interval{ 90, 180 } })
, _lifeLength((type != Type::SlowStars && type != Type::SlowDiamondStars)
	? Interval{ 150 / 5, 200 / 5 }
	: Interval{ 150 * 2, 200 * 2 })
, _deathTime((type != Type::SlowStars && type != Type::SlowDiamondStars)
	? Interval{ 1500, 2000 }
	: Interval{ 1500 * 2, 2000 * 2 })
, _size((type != Type::SlowStars)
	? Interval{ 5, 10 }
	: Interval{ 2, 4 })
, _alpha({ opaque ? 100 : 40, opaque ? 100 : 60 })
, _sinFactor({ 10, 190 })
, _spritesCount({ 0, ((type == Type::MonoStars) ? 1 : 2) })
, _appearProgressTill((type != Type::SlowStars
		&& type != Type::SlowDiamondStars)
	? 0.2
	: 0.01)
, _disappearProgressAfter(0.8)
, _distanceProgressStart(0.5)
, _sprite((type == Type::DiamondStars || type == Type::SlowDiamondStars)
	? u":/gui/icons/settings/starmini.svg"_q
	: u":/gui/icons/settings/star.svg"_q)
, _animation([=](crl::time now) {
	if (now > _nextBirthTime && !_paused) {
		createStar(now);
	}
	if (_rectToUpdate.isValid()) {
		updateCallback(base::take(_rectToUpdate));
	}
}) {
	if (type == Type::BiStars) {
		_secondSprite = std::make_unique<QSvgRenderer>(
			u":/gui/icons/settings/star.svg"_q);
	}
	if (anim::Disabled()) {
		const auto from = _deathTime.from + _deathTime.length;
		auto r = bytes::vector(from + 1);
		base::RandomFill(r.data(), r.size());

		for (auto i = -from; i < 0; i += randomInterval(_lifeLength, r[-i])) {
			createStar(i);
		}
		updateCallback(_rectToUpdate);
	} else {
		_animation.start();
	}
}

int MiniStars::randomInterval(
		const Interval &interval,
		const bytes::type &random) const {
	return interval.from + (uchar(random) % interval.length);
}

crl::time MiniStars::timeNow() const {
	return anim::Disabled() ? 0 : crl::now();
}

void MiniStars::paint(QPainter &p, const QRectF &rect) {
	const auto center = rect.center();
	const auto opacity = p.opacity();
	const auto now = timeNow();
	for (const auto &ministar : _ministars) {
		const auto progress = (now - ministar.birthTime)
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
		ministar.sprite->render(&p, renderRect);
		_rectToUpdate |= renderRect.toRect();
	}
	p.setOpacity(opacity);
}

void MiniStars::setPaused(bool paused) {
	_paused = paused;
}

void MiniStars::createStar(crl::time now) {
	constexpr auto kRandomSize = 9;
	auto random = bytes::vector(kRandomSize);
	base::RandomFill(random.data(), random.size());

	auto i = 0;
	auto next = [&] { return random[i++]; };

	_nextBirthTime = now + randomInterval(_lifeLength, next());

	const auto &angleInterval = _availableAngles[
		uchar(next()) % _availableAngles.size()];

	auto ministar = MiniStar{
		.birthTime = now,
		.deathTime = now + randomInterval(_deathTime, next()),
		.angle = randomInterval(angleInterval, next()),
		.size = float64(randomInterval(_size, next())),
		.alpha = float64(randomInterval(_alpha, next())) / 100.,
		.sinFactor = randomInterval(_sinFactor, next()) / 100.
			* ((uchar(next()) % 2) == 1 ? 1. : -1.),
		.sprite = ((randomInterval(_spritesCount, next()) && _secondSprite)
			? _secondSprite.get()
			: &_sprite),
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
