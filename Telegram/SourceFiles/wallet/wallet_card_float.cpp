/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_float.h"

#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kSlideDuration = crl::time(150);
constexpr auto kSlideTimeConstant = crl::time(48);
constexpr auto kLinger = crl::time(200);
constexpr auto kOutDuration = crl::time(60);
constexpr auto kOutTimeConstant = crl::time(15);
constexpr auto kEntryScale = 0.9;
constexpr auto kEntryRotation = 2.;
constexpr auto kEntryLift = 0.175;
constexpr auto kOutLift = 0.226;
constexpr auto kOutScale = 0.944;
constexpr auto kCorners = 4;

[[nodiscard]] bool Visible(const CardFold &fold) {
	return fold.valid && (fold.opacity > 0.);
}

[[nodiscard]] float64 Ease(
		crl::time elapsed,
		crl::time duration,
		crl::time constant) {
	if (elapsed <= 0) {
		return 0.;
	} else if (elapsed >= duration) {
		return 1.;
	}
	const auto reached = [&](crl::time time) {
		return 1. - std::exp(-time / float64(constant));
	};
	return reached(elapsed) / reached(duration);
}

[[nodiscard]] CardFold Pose(
		const CardFold &rest,
		QPolygonF quad,
		float64 opacity,
		float64 ink) {
	auto result = rest;
	auto transform = QTransform();
	result.opacity = opacity;
	result.fold = ink;
	result.valid = rest.valid
		&& (quad.size() == kCorners)
		&& QTransform::quadToQuad(rest.quad, quad, transform);
	if (result.valid) {
		result.quad = std::move(quad);
		result.transform = transform;
	}
	return result;
}

[[nodiscard]] CardFold Mix(
		const CardFold &rest,
		const CardFold &from,
		const CardFold &to,
		float64 progress) {
	if (from.quad.size() != kCorners || to.quad.size() != kCorners) {
		return CardFold();
	}
	auto quad = QPolygonF();
	quad.reserve(kCorners);
	for (auto i = 0; i != kCorners; ++i) {
		quad.push_back(from.quad[i] + (to.quad[i] - from.quad[i]) * progress);
	}
	return Pose(
		rest,
		std::move(quad),
		from.opacity + (to.opacity - from.opacity) * progress,
		from.fold + (to.fold - from.fold) * progress);
}

[[nodiscard]] CardFold Above(const CardFold &rest, float64 ink) {
	const auto area = QRectF(rest.rest);
	const auto center = area.center();
	const auto transform = QTransform()
		.translate(center.x(), center.y() - kEntryLift * area.height())
		.rotate(kEntryRotation)
		.scale(kEntryScale, kEntryScale)
		.translate(-center.x(), -center.y());
	return Pose(rest, transform.map(rest.quad), 1., ink);
}

[[nodiscard]] CardFold Leave(
		const CardFold &rest,
		const CardFold &from,
		float64 ink,
		crl::time elapsed) {
	if (from.quad.size() != kCorners) {
		return CardFold();
	}
	const auto progress = std::clamp(
		elapsed / float64(kOutDuration),
		0.,
		1.);
	const auto eased = Ease(elapsed, kOutDuration, kOutTimeConstant);
	const auto scale = 1. - (1. - kOutScale) * progress;
	const auto lift = kOutLift * rest.rest.width() * eased;
	auto center = QPointF();
	for (const auto &corner : from.quad) {
		center += corner / kCorners;
	}
	const auto transform = QTransform()
		.translate(center.x(), center.y() - lift)
		.scale(scale, scale)
		.translate(-center.x(), -center.y());
	return Pose(
		rest,
		transform.map(from.quad),
		from.opacity * (1. - progress),
		from.fold + (ink - from.fold) * eased);
}

} // namespace

CardFloat::CardFloat(CardFloatArgs &&args)
: _inPlace(std::move(args.inPlace))
, _paint(std::move(args.paint))
, _changed(std::move(args.changed))
, _layer(base::make_unique_q<Ui::RpWidget>(args.body.get()))
, _animation([=](crl::time now) { return tick(now); }) {
	const auto layer = _layer.get();
	layer->setAttribute(Qt::WA_TransparentForMouseEvents);
	layer->hide();
	args.body->sizeValue() | rpl::on_next([=](QSize size) {
		layer->setGeometry(QRect(QPoint(), size));
	}, layer->lifetime());
	layer->paintRequest() | rpl::on_next([=](QRect clip) {
		paint(clip);
	}, layer->lifetime());

	std::move(args.arriving) | rpl::on_next([=](bool arriving) {
		setArriving(arriving);
	}, _lifetime);
	anim::Disables() | rpl::filter([=](bool disabled) {
		return disabled && shown();
	}) | rpl::on_next([=] {
		finish(_inPlace());
	}, _lifetime);
}

CardFloat::~CardFloat() = default;

void CardFloat::bring() {
	if (anim::Disabled()) {
		return;
	} else if (_phase == Phase::Back || _phase == Phase::Out) {
		startSegment(Phase::In, crl::now());
		return;
	} else if (_phase != Phase::Hidden) {
		return;
	}
	const auto live = _inPlace();
	const auto rest = ComputeCardFold(live.rest, 0.);
	const auto visible = Visible(live);
	if (!rest.valid || (visible && !live.fold)) {
		return;
	}
	auto from = visible ? live : Above(rest, live.fold);
	if (!from.valid) {
		return;
	}
	_from = std::move(from);
	_fromShadow = visible ? 0. : 1.;
	_fold = _from;
	_shadow = _fromShadow;
	_phase = Phase::In;
	_started = crl::now();
	_layer->show();
	_layer->update();
	_animation.start();
	_changed();
}

void CardFloat::update() {
	if (!shown()) {
		return;
	} else if (_phase == Phase::Hold || _phase == Phase::Linger) {
		auto rest = ComputeCardFold(_inPlace().rest, 0.);
		if (rest.valid) {
			_fold = std::move(rest);
		}
	}
	_layer->update();
}

bool CardFloat::shown() const {
	return (_phase != Phase::Hidden);
}

const CardFold &CardFloat::fold() const {
	return _fold;
}

not_null<Ui::RpWidget*> CardFloat::widget() const {
	return _layer.get();
}

bool CardFloat::tick(crl::time now) {
	const auto live = _inPlace();
	const auto rest = ComputeCardFold(live.rest, 0.);
	if (anim::Disabled() || !rest.valid) {
		finish(live);
	} else {
		step(now, live, rest);
	}
	return shown() && (_phase != Phase::Hold);
}

void CardFloat::step(
		crl::time now,
		const CardFold &live,
		const CardFold &rest) {
	const auto elapsed = now - _started;
	const auto slide = Ease(elapsed, kSlideDuration, kSlideTimeConstant);
	switch (_phase) {
	case Phase::In:
		if (elapsed < kSlideDuration) {
			present(
				Mix(rest, _from, rest, slide),
				_fromShadow + (1. - _fromShadow) * slide);
		} else {
			_phase = _arriving ? Phase::Hold : Phase::Linger;
			_started = now;
			if (_arriving) {
				_animation.stop();
			}
			present(rest, 1.);
		}
		break;
	case Phase::Linger:
		if (elapsed >= kLinger) {
			startSegment(Visible(live) ? Phase::Back : Phase::Out, now);
		}
		break;
	case Phase::Back:
		if (!Visible(live)) {
			startSegment(Phase::Out, now);
		} else if (elapsed < kSlideDuration) {
			present(
				Mix(rest, _from, live, slide),
				_fromShadow * (1. - slide));
		} else {
			finish(live);
		}
		break;
	case Phase::Out:
		if (Visible(live)) {
			startSegment(Phase::Back, now);
		} else if (elapsed < kOutDuration) {
			present(
				Leave(rest, _from, live.fold, elapsed),
				_fromShadow * (1. - elapsed / float64(kOutDuration)));
		} else {
			finish(Leave(rest, _from, live.fold, kOutDuration));
		}
		break;
	case Phase::Hidden:
	case Phase::Hold:
		break;
	}
}

void CardFloat::setArriving(bool arriving) {
	_arriving = arriving;
	if (arriving && _phase == Phase::Linger) {
		_phase = Phase::Hold;
		_animation.stop();
	} else if (!arriving && _phase == Phase::Hold) {
		_phase = Phase::Linger;
		_started = crl::now();
		// WHY: the wait runs on animation ticks, not a base::Timer, so the
		// leave starts on a frame of the same clock that ended the landing.
		_animation.start();
	}
}

void CardFloat::startSegment(Phase phase, crl::time now) {
	_phase = phase;
	_from = _fold;
	_fromShadow = _shadow;
	_started = now;
}

void CardFloat::present(CardFold fold, float64 shadow) {
	if (!fold.valid) {
		return;
	}
	_fold = std::move(fold);
	_shadow = shadow;
	_layer->update();
	_changed();
}

void CardFloat::finish(CardFold fold) {
	if (!shown()) {
		return;
	}
	_fold = std::move(fold);
	_shadow = 0.;
	_phase = Phase::Hidden;
	_layer->hide();
	_animation.stop();
	_changed();
}

void CardFloat::paint(QRect clip) {
	if (!shown() || !_fold.valid) {
		return;
	}
	auto p = Painter(_layer.get());
	paintShadow(p);
	_paint(p, _fold, clip);
}

void CardFloat::paintShadow(QPainter &p) {
	if (_shadow <= 0.) {
		return;
	}
	validateShadow();
	const auto extend = Ui::BoxShadow::ExtendFor(st::walletCardFloatShadow);
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(p.opacity() * _shadow);
	p.setTransform(_fold.transform, true);
	p.drawImage(QRectF(_fold.rest).marginsAdded(extend), _shadowCache);
	p.restore();
}

void CardFloat::validateShadow() {
	const auto &st = st::walletCardFloatShadow;
	const auto ratio = style::DevicePixelRatio();
	const auto extend = Ui::BoxShadow::ExtendFor(st);
	const auto box = QRect(
		QPoint(extend.left(), extend.top()),
		_fold.rest.size());
	const auto size = box.marginsAdded(extend).size() * ratio;
	if (_shadowCache.size() == size
		&& _shadowCache.devicePixelRatio() == ratio) {
		return;
	}
	_shadowCache = QImage(size, QImage::Format_ARGB32_Premultiplied);
	_shadowCache.setDevicePixelRatio(ratio);
	_shadowCache.fill(Qt::transparent);
	auto p = QPainter(&_shadowCache);
	Ui::BoxShadow(st).paint(p, box, st::walletCardRadius);
}

} // namespace Wallet
