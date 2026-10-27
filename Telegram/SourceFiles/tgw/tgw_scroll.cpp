/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_scroll.h"

#include "ui/effects/animations.h"

namespace Tgw {
namespace {

// WHY: a corrected flight chases a changing destination in such segments
// until it arrives or recorded user input takes the list over: any count
// or time limit would end it short of an available destination or jump.
constexpr auto kContinuation = crl::time(240);

[[nodiscard]] float64 Progress(float64 value, int from, int to) {
	return (from == to) ? 1. : ((value - from) / (to - from));
}

} // namespace

void ScrollFlight::start(
		int from,
		int to,
		Fn<uint32()> consumed,
		uint32 count,
		Fn<uint32()> inputs,
		Fn<std::optional<int>()> end) {
	_consumed = std::move(consumed);
	_inputs = std::move(inputs);
	_end = std::move(end);
	_progress = 0.;
	_anchorProgress = 0.;
	_from = from;
	_to = to;
	_placed = from;
	_anchor = from;
	_target = to;
	_travel = to - from;
	_desired = to;
	_seen = count;
	_inputsSeen = _inputs ? _inputs() : 0;
	++_generation;
	_state = State::Inactive;
	_pending = false;
}

void ScrollFlight::animate(
		not_null<Ui::Animations::Simple*> animation,
		Fn<void()> callback,
		float64 from,
		float64 to,
		crl::time duration,
		anim::transition transition) {
	_animation = animation;
	_callback = std::move(callback);
	_animationFrom = from;
	_animationTo = to;
	animation->start(_callback, from, to, duration, std::move(transition));
}

void ScrollFlight::frame(
		int reference,
		float64 value,
		int scrollTop,
		bool finished,
		const Fn<void(int)> &apply) {
	if ((_state == State::Completed) || (_state == State::Cancelled)) {
		return;
	}
	const auto count = _consumed ? _consumed() : _seen;
	if (_state == State::Inactive) {
		if (count == _seen) {
			_placed = int(base::SafeRound(value));
			_progress = finished ? 1. : Progress(value, _from, _to);
			apply(reference + _placed);
			return;
		}
		_state = State::Riding;
		_anchor = _placed;
		_anchorProgress = _progress;
	}
	if (count != _seen) {
		const auto carried = (scrollTop - reference) - _placed;
		_seen = count;
		_placed += carried;
		_anchor += carried;
		_target += carried;
		_desired += carried;
	}
	if ((_state == State::Continuation) && takenOver()) {
		_state = State::Cancelled;
		_animation->stop();
		return;
	}
	const auto progress = finished ? 1. : Progress(value, _from, _to);
	const auto end = _end ? _end() : std::nullopt;
	if (end) {
		_desired = *end - reference;
	} else if (!_end) {
		_desired = _to;
	}
	if (base::take(_pending)) {
		if (_desired == _placed) {
			_state = State::Completed;
			_animation->stop();
			return;
		}
		_anchor = _placed;
		_anchorProgress = _progress;
		_target = _desired;
		_travel = _desired - _placed;
	}
	if (_desired != _target) {
		const auto step = _target - _placed;
		const auto residual = _desired - _target;
		const auto need = (_travel < 0)
			? (_placed - _desired)
			: (_desired - _placed);
		const auto remaining = std::abs(_travel) * (1. - _progress);
		if (finished && (std::abs(residual) <= std::abs(step))) {
			_target = _desired;
		} else if (!finished && (need >= 0) && (need <= 2. * remaining)) {
			_anchor = _placed;
			_anchorProgress = _progress;
			_target = _desired;
		} else if (finished || (need < 0)) {
			const auto start = (need < 0) ? _placed : _target;
			if (continueFrom(start)) {
				_placed = start;
				apply(reference + start);
			}
			return;
		}
	}
	_placed = place(progress);
	_progress = progress;
	const auto generation = _generation;
	apply(reference + _placed);
	if (!finished || (_generation != generation)) {
		return;
	}
	const auto moved = (_consumed && (_consumed() != _seen))
		|| (_end && (_end() != end));
	if (!moved) {
		_state = State::Completed;
	} else if (continueFrom(_placed)) {
		_pending = true;
	}
}

bool ScrollFlight::takenOver() const {
	return _inputs && (_inputs() != _inputsSeen);
}

bool ScrollFlight::continueFrom(int start) {
	if (takenOver()) {
		_state = State::Cancelled;
		_animation->stop();
		return false;
	}
	_anchor = start;
	_anchorProgress = 0.;
	_target = _desired;
	_travel = _desired - start;
	_progress = 0.;
	_state = State::Continuation;
	_animation->stop();
	_animation->start(
		_callback,
		_animationFrom,
		_animationTo,
		kContinuation,
		anim::sineInOut);
	return true;
}

int ScrollFlight::place(float64 progress) const {
	const auto fraction = (_anchorProgress >= 1.)
		? 1.
		: std::clamp(
			(progress - _anchorProgress) / (1. - _anchorProgress),
			0.,
			1.);
	return _anchor + int(base::SafeRound((_target - _anchor) * fraction));
}

} // namespace Tgw
