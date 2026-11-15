/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_view_motion.h"

#include "wallet/wallet_card_flight.h"

#include <QtGui/QPolygonF>

namespace Wallet {
namespace {

constexpr auto kLiftDuration = crl::time(100);
constexpr auto kSlideDuration = crl::time(180);
constexpr auto kTurnDuration = crl::time(370);
constexpr auto kTurnPeak = crl::time(205);
constexpr auto kGlideDuration = crl::time(205);
constexpr auto kDimDuration = crl::time(350);
constexpr auto kBlurDelay = crl::time(350);
constexpr auto kBlurDuration = crl::time(290);
constexpr auto kButtonDelay = crl::time(300);
constexpr auto kButtonDuration = crl::time(300);
constexpr auto kFallbackDuration = crl::time(200);
constexpr auto kFallbackButtonDelay = crl::time(100);
constexpr auto kFallbackButtonDuration = crl::time(250);
constexpr auto kCloseBlurDuration = crl::time(100);
constexpr auto kCloseDimDuration = crl::time(310);
constexpr auto kCloseLabelDuration = kCloseBlurDuration + kCloseDimDuration;
constexpr auto kCloseButtonDelay = crl::time(130);
constexpr auto kCloseButtonDuration = crl::time(110);
constexpr auto kCloseCardDelay = crl::time(80);
constexpr auto kCloseFlightDuration = crl::time(175);
constexpr auto kCloseSlideDuration = crl::time(105);
constexpr auto kLiftReturnDuration = crl::time(60);
constexpr auto kOpeningEnd = std::max({
	kSlideDuration + kTurnDuration,
	kBlurDelay + kBlurDuration,
	kButtonDelay + kButtonDuration,
	kFallbackButtonDelay + kFallbackButtonDuration,
});
constexpr auto kTurnOvershoot = 0.037;
constexpr auto kGrowthLimit = 1.02;
constexpr auto kSlotFreeEnd = 1.41;
constexpr auto kSlotAngle = -90.;
constexpr auto kFallbackScale = 0.9;

struct Slot {
	QRectF rect;
	QPointF mouth;
	float64 height = 0.;
	float64 scale = 0.;
	float64 length = 0.;
	float64 end = 0.;
};

[[nodiscard]] float64 Ramp(float64 time, float64 start, float64 duration) {
	if (time >= start + duration) {
		return 1.;
	} else if (time <= start) {
		return 0.;
	}
	return (time - start) / duration;
}

[[nodiscard]] float64 Turn(float64 time) {
	const auto damping = -std::log(kTurnOvershoot);
	const auto zeta = damping / std::sqrt(M_PI * M_PI + damping * damping);
	const auto root = std::sqrt(1. - zeta * zeta);
	const auto w = M_PI / kTurnPeak;
	return 1. - std::exp(-zeta * w / root * time)
		* (std::cos(w * time) + zeta / root * std::sin(w * time));
}

[[nodiscard]] std::optional<Slot> ComputeSlot(const CardViewPlaces &places) {
	if (!places.slot || places.rest.isEmpty() || places.slot->isEmpty()) {
		return std::nullopt;
	}
	const auto rect = QRectF(*places.slot);
	const auto card = QSizeF(places.rest.size());
	const auto opening = rect.width() - rect.height() / 2.;
	if (opening <= 0.) {
		return std::nullopt;
	}
	const auto height = opening / card.height();
	const auto scale = height * (1. + kSlotFreeEnd) / 2.;
	const auto length = scale * card.width();
	const auto end = -rect.height() / length;
	if (1. + (end - 1.) * (kSlotFreeEnd - 1.) <= 0.) {
		return std::nullopt;
	}
	return Slot{
		.rect = rect,
		.mouth = QPointF(rect.center().x(), rect.y() + rect.height()),
		.height = height,
		.scale = scale,
		.length = length,
		.end = end,
	};
}

[[nodiscard]] CardViewPose RestPose(QRect rest) {
	return { .center = QRectF(rest).center(), .rest = true };
}

[[nodiscard]] CardViewPose Mix(
		const CardViewPose &from,
		const CardViewPose &to,
		float64 progress) {
	if (progress <= 0.) {
		return from;
	} else if (progress >= 1.) {
		return to;
	}
	const auto mix = [&](float64 a, float64 b) {
		return a + (b - a) * progress;
	};
	return {
		.center = from.center + (to.center - from.center) * progress,
		.angle = mix(from.angle, to.angle),
		.length = mix(from.length, to.length),
		.height = mix(from.height, to.height),
		.lean = mix(from.lean, to.lean),
		.opacity = mix(from.opacity, to.opacity),
	};
}

[[nodiscard]] CardViewPose Scaled(
		CardViewPose pose,
		float64 scale,
		float64 opacity) {
	pose.length *= scale;
	pose.height *= scale;
	pose.opacity *= opacity;
	pose.rest = false;
	return pose;
}

[[nodiscard]] CardViewPose Appearing(QRect rest, float64 progress) {
	return (progress < 1.)
		? Scaled(
			RestPose(rest),
			kFallbackScale + (1. - kFallbackScale) * progress,
			progress)
		: RestPose(rest);
}

[[nodiscard]] CardViewPose Fading(
		const CardViewPose &pose,
		float64 progress) {
	const auto square = progress * progress;
	return (progress > 0.)
		? Scaled(pose, 1. - (1. - kFallbackScale) * square, 1. - square)
		: pose;
}

[[nodiscard]] CardViewPose SlidePose(const Slot &slot, float64 out) {
	const auto spread = kSlotFreeEnd - 1.;
	return {
		.center = slot.mouth + QPointF(0., (out - 0.5) * slot.length),
		.angle = kSlotAngle,
		.length = slot.scale,
		.height = slot.height * (1. + (out - 0.5) * spread),
		.lean = spread / (2. + (2. * out - 1.) * spread),
	};
}

[[nodiscard]] CardViewPose TurnPose(
		QRect rest,
		const Slot &slot,
		float64 turn,
		float64 glide) {
	const auto out = SlidePose(slot, 1.);
	const auto to = QRectF(rest).center();
	auto scale = slot.scale + (1. - slot.scale) * turn;
	if (turn > 1.) {
		scale = std::min(scale, kGrowthLimit);
	}
	return {
		.center = out.center + (to - out.center) * glide,
		.angle = kSlotAngle * (1. - turn),
		.length = scale,
		.height = scale,
		.lean = out.lean * (1. - glide),
	};
}

} // namespace

bool ValidCardViewSlot(const CardViewPlaces &places) {
	return ComputeSlot(places).has_value();
}

CardViewFrame CardViewRestFrame(const CardViewPlaces &places) {
	const auto slot = ComputeSlot(places);
	return {
		.card = RestPose(places.rest),
		.slot = slot ? slot->rect : QRectF(),
		.slide = 1.,
		.glide = 1.,
		.lift = slot ? 1. : 0.,
		.dim = 1.,
		.blur = 1.,
		.label = 1.,
		.button = 1.,
		.patch = slot.has_value(),
		.finished = true,
	};
}

std::optional<QTransform> CardViewTransform(
		QSize card,
		const CardViewPose &pose) {
	const auto finite = std::isfinite(pose.center.x())
		&& std::isfinite(pose.center.y())
		&& std::isfinite(pose.angle)
		&& std::isfinite(pose.length)
		&& std::isfinite(pose.height)
		&& std::isfinite(pose.lean);
	if (card.isEmpty()
		|| !finite
		|| pose.length <= 0.
		|| pose.height <= 0.
		|| std::abs(pose.lean) >= 1.) {
		return std::nullopt;
	}
	const auto size = QSizeF(card);
	if (pose.rest) {
		return QTransform::fromTranslate(
			pose.center.x() - size.width() / 2.,
			pose.center.y() - size.height() / 2.);
	}
	const auto half = size.width() * pose.length / 2.;
	const auto side = size.height() * pose.height / 2.;
	const auto freeEnd = side * (1. + pose.lean);
	const auto qrEnd = side * (1. - pose.lean);
	const auto place = QTransform()
		.translate(pose.center.x(), pose.center.y())
		.rotate(pose.angle);
	const auto quad = place.map(QPolygonF({
		QPointF(-half, -freeEnd),
		QPointF(half, -qrEnd),
		QPointF(half, qrEnd),
		QPointF(-half, freeEnd),
	}));
	const auto rest = QPolygonF({
		QPointF(),
		QPointF(size.width(), 0.),
		QPointF(size.width(), size.height()),
		QPointF(0., size.height()),
	});
	auto result = QTransform();
	if (!QTransform::quadToQuad(rest, quad, result)) {
		return std::nullopt;
	}
	return result;
}

void CardViewMotion::open(const CardViewPlaces &places) {
	_from = CardViewFrame();
	_loss = std::nullopt;
	_rebase = std::nullopt;
	_insertDuration = 0.;
	_closing = false;
	_slot = ValidCardViewSlot(places);
}

void CardViewMotion::close(
		const CardViewFrame &from,
		const CardViewPlaces &places) {
	const auto slot = (from.patch && from.card.opacity > 0.)
		? ComputeSlot(places)
		: std::nullopt;
	_from = from;
	_loss = std::nullopt;
	_rebase = std::nullopt;
	_insertDuration = !slot
		? 0.
		: (from.slide < 1.)
		? (kCloseSlideDuration * (from.slide - slot->end) / (1. - slot->end))
		: float64(kCloseSlideDuration);
	_closing = true;
	_slot = slot.has_value();
	if ((from.rebase < 1. || !_slot) && cardEnd() > 0.) {
		_rebase = Rebase{ from, places, 0., cardEnd() };
	}
}

void CardViewMotion::loseSlot(
		const CardViewFrame &painted,
		crl::time elapsed,
		const CardViewPlaces &places) {
	if (!_slot) {
		return;
	}
	const auto continuing = painted.card.opacity > 0. && elapsed < cardEnd();
	_loss = Loss{
		.card = painted.card,
		.slot = painted.slot,
		.clip = painted.clip,
		.at = elapsed,
	};
	_slot = false;
	if (_closing && !continuing) {
		_loss->card.opacity = 0.;
	}
	_rebase = continuing
		? std::make_optional(Rebase{
			painted,
			places,
			float64(elapsed),
			cardEnd(),
		})
		: std::nullopt;
}

bool CardViewMotion::retarget(
		const CardViewFrame &painted,
		crl::time elapsed,
		const CardViewPlaces &places) {
	const auto end = cardEnd();
	if ((_slot && !ValidCardViewSlot(places))
		|| (!_slot && !_loss && !_closing)
		|| painted.card.opacity <= 0.
		|| elapsed >= end) {
		return false;
	}
	if (_loss && !_closing) {
		_loss = Loss{
			.card = painted.card,
			.slot = painted.slot,
			.clip = painted.clip,
			.at = elapsed,
		};
	}
	_rebase = Rebase{ painted, places, float64(elapsed), end };
	return true;
}

float64 CardViewMotion::cardEnd() const {
	if (!_closing) {
		return float64(kSlideDuration + kTurnDuration);
	} else if (_loss) {
		return (_loss->card.opacity > 0.)
			? float64(_loss->at + kFallbackDuration)
			: 0.;
	}
	const auto delay = _from.finished ? kCloseCardDelay : 0;
	return !_slot
		? ((_from.card.opacity > 0.) ? delay + kFallbackDuration : 0.)
		: delay
			+ ((_from.slide < 1.) ? 0. : _from.glide * kCloseFlightDuration)
			+ _insertDuration;
}

bool CardViewMotion::usesSlot() const {
	return _slot;
}

CardViewFrame CardViewMotion::frame(
		crl::time elapsed,
		const CardViewPlaces &places) const {
	const auto time = float64(elapsed);
	const auto &target = (_rebase && time < _rebase->end)
		? _rebase->places
		: places;
	auto result = _closing ? closing(time, target) : opening(time, target);
	if (_rebase && time < _rebase->end) {
		const auto &from = _rebase->from;
		const auto progress = FlightEaseOut(
			Ramp(time, _rebase->at, _rebase->end - _rebase->at));
		if (_slot) {
			result.card = Mix(from.card, result.card, progress);
			result.slot = QRectF(
				from.slot.topLeft()
					+ (result.slot.topLeft() - from.slot.topLeft()) * progress,
				from.slot.size()
					+ (result.slot.size() - from.slot.size()) * progress);
			result.clip = from.clip + (result.clip - from.clip) * progress;
		}
		result.rebase = progress;
	}
	return result;
}

CardViewFrame CardViewMotion::opening(
		float64 time,
		const CardViewPlaces &places) const {
	const auto slot = _slot ? ComputeSlot(places) : std::nullopt;
	const auto slotted = _slot || _loss.has_value();
	const auto blur = Ramp(time, kBlurDelay, kBlurDuration);
	auto result = CardViewFrame{
		.slide = 1.,
		.glide = 1.,
		.dim = Ramp(time, 0., kDimDuration),
		.blur = blur,
		.label = blur,
		.button = slotted
			? Ramp(time, kButtonDelay, kButtonDuration)
			: Ramp(time, kFallbackButtonDelay, kFallbackButtonDuration),
		.finished = (time >= kOpeningEnd),
	};
	if (_loss) {
		const auto lost = float64(_loss->at);
		const auto arrival = float64(kSlideDuration + kTurnDuration);
		const auto progress = FlightEaseOut(
			Ramp(time, lost, arrival - lost));
		result.card = (_loss->card.opacity > 0.)
			? Mix(_loss->card, RestPose(places.rest), progress)
			: _loss->card;
		result.slot = _loss->slot;
		result.clip = _loss->clip * (1. - progress);
		result.glide = progress;
	} else if (slot) {
		result.slot = slot->rect;
		result.patch = true;
		result.lift = Ramp(time, 0., kLiftDuration);
		result.slide = Ramp(time, 0., kSlideDuration);
		if (time < kSlideDuration) {
			result.card = SlidePose(*slot, result.slide);
			result.clip = 1.;
			result.glide = 0.;
		} else if (time < kSlideDuration + kTurnDuration) {
			const auto turn = time - kSlideDuration;
			result.glide = FlightEaseOut(turn / kGlideDuration);
			result.card = TurnPose(
				places.rest,
				*slot,
				Turn(turn),
				result.glide);
		} else {
			result.card = RestPose(places.rest);
		}
	} else {
		result.glide = FlightEaseOut(Ramp(time, 0., kFallbackDuration));
		result.card = Appearing(places.rest, result.glide);
	}
	return result;
}

CardViewFrame CardViewMotion::closing(
		float64 time,
		const CardViewPlaces &places) const {
	const auto slot = _slot ? ComputeSlot(places) : std::nullopt;
	const auto rested = _from.finished;
	const auto blurDuration = _from.blur * kCloseBlurDuration;
	const auto dimDuration = _from.dim * kCloseDimDuration;
	const auto labelDuration = _from.label * kCloseLabelDuration;
	const auto buttonDelay = rested ? float64(kCloseButtonDelay) : 0.;
	const auto buttonDuration = _from.button * kCloseButtonDuration;
	const auto cardDelay = rested ? float64(kCloseCardDelay) : 0.;
	const auto from = (rested && slot)
		? RestPose(places.rest)
		: _from.card;
	auto result = CardViewFrame{
		.dim = _from.dim * (1. - Ramp(time, blurDuration, dimDuration)),
		.blur = _from.blur * (1. - Ramp(time, 0., blurDuration)),
		.label = _from.label * (1. - Ramp(time, 0., labelDuration)),
		.button = _from.button
			* (1. - Ramp(time, buttonDelay, buttonDuration)),
	};
	auto end = std::max({
		blurDuration + dimDuration,
		labelDuration,
		buttonDelay + buttonDuration,
	});
	if (_loss) {
		const auto lost = float64(_loss->at);
		result.card = Fading(
			_loss->card,
			Ramp(time, lost, kFallbackDuration));
		result.slot = _loss->slot;
		result.clip = _loss->clip;
		if (_loss->card.opacity > 0.) {
			end = std::max(end, lost + kFallbackDuration);
		}
	} else if (slot) {
		const auto inside = (_from.slide < 1.);
		const auto start = inside ? _from.slide : 1.;
		const auto back = inside ? 0. : (_from.glide * kCloseFlightDuration);
		const auto insert = cardDelay + back;
		const auto inserted = insert + _insertDuration;
		result.slot = slot->rect;
		if (time < insert) {
			const auto progress = Ramp(time, cardDelay, back);
			result.card = Mix(
				from,
				SlidePose(*slot, 1.),
				progress * progress * progress);
			result.slide = 1.;
		} else if (time < inserted) {
			const auto progress = Ramp(time, insert, _insertDuration);
			result.slide = start + (slot->end - start) * progress;
			result.card = SlidePose(*slot, result.slide);
			result.clip = 1.;
		} else {
			result.slide = slot->end;
			result.card = SlidePose(*slot, slot->end);
			result.card.opacity = 0.;
		}
		const auto lifting = 1. - _from.lift;
		const auto lifted = [&](float64 at) {
			return _from.lift
				+ lifting * Ramp(at, 0., lifting * kLiftDuration);
		};
		const auto returning = std::max(inserted - kLiftReturnDuration, 0.);
		result.lift = (time < returning)
			? lifted(time)
			: (lifted(returning)
				* (1. - Ramp(time, returning, inserted - returning)));
		result.patch = (time < inserted);
		end = std::max(end, inserted);
	} else {
		result.card = Fading(
			from,
			Ramp(time, cardDelay, kFallbackDuration));
		result.slot = _from.slot;
		result.clip = _from.clip;
		if (from.opacity > 0.) {
			end = std::max(end, cardDelay + kFallbackDuration);
		}
	}
	result.finished = (time >= end);
	return result;
}

} // namespace Wallet
