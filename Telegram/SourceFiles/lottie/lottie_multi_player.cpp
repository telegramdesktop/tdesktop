/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_multi_player.h"

#include "lottie/lottie_frame_renderer.h"
#include "lottie/lottie_animation.h"
#include "logs.h"

#include <range/v3/algorithm/remove.hpp>

namespace Lottie {

std::shared_ptr<FrameRenderer> MakeFrameRenderer() {
	return FrameRenderer::CreateIndependent();
}

MultiPlayer::MultiPlayer(std::shared_ptr<FrameRenderer> renderer)
: _timer([=] { checkNextFrameRender(); })
, _renderer(renderer ? std::move(renderer) : FrameRenderer::Instance()) {
	crl::on_main_update_requests(
	) | rpl::start_with_next([=] {
		checkStep();
	}, _lifetime);
}

MultiPlayer::~MultiPlayer() {
	for (const auto &[animation, state] : _active) {
		_renderer->remove(state);
	}
}

not_null<Animation*> MultiPlayer::append(
		FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
		FnMut<void(QByteArray &&cached)> put, // Unknown thread.
		const QByteArray &content,
		const FrameRequest &request) {
	_animations.push_back(std::make_unique<Animation>(
		this,
		std::move(get),
		std::move(put),
		content,
		request));
	return _animations.back().get();
}

not_null<Animation*> MultiPlayer::append(
		const QByteArray &content,
		const FrameRequest &request) {
	_animations.push_back(std::make_unique<Animation>(
		this,
		content,
		request));
	return _animations.back().get();
}

void MultiPlayer::startAtRightTime(not_null<SharedState*> state) {
	Expects(!_active.empty());
	Expects((_active.size() == 1) == (_started == kTimeUnknown));

	const auto now = crl::now();
	if (_active.size() == 1) {
		_started = now;
	}

	const auto rate = state->information().frameRate;
	Assert(rate != 0);

	const auto started = _started + _delay;
	const auto skipFrames = (now - started) * rate / 1000;

	state->start(this, _started, _delay, skipFrames);
}

void MultiPlayer::start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) {
	Expects(state != nullptr);

	if (_nextFrameTime == kTimeUnknown) {
		appendToActive(animation, std::move(state));
	} else {
		// We always try to mark as shown at the same time, so we start a new
		// animation at the same time we mark all existing as shown.
		_pendingToStart.emplace(animation, std::move(state));
	}
}

void MultiPlayer::appendPendingToActive() {
	for (auto &[animation, state] : base::take(_pendingToStart)) {
		appendToActive(animation, std::move(state));
	}
}

void MultiPlayer::appendToActive(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) {
	Expects(_nextFrameTime == kTimeUnknown);

	_active.emplace(animation, state.get());

	auto information = state->information();
	startAtRightTime(state.get());
	_renderer->append(std::move(state));
	_updates.fire({});
}

void MultiPlayer::remove(not_null<Animation*> animation) {
	const auto i = _active.find(animation);
	if (i != end(_active)) {
		_renderer->remove(i->second);
	}
	_animations.erase(
		ranges::remove(
			_animations,
			animation.get(),
			&std::unique_ptr<Animation>::get),
		end(_animations));

	if (_active.empty()) {
		_started = kTimeUnknown;
		_delay = 0;
		_nextFrameTime = kTimeUnknown;
		_timer.cancel();
	}
}

void MultiPlayer::failed(not_null<Animation*> animation, Error error) {
	//_updates.fire({ animation, error });
}

rpl::producer<MultiUpdate> MultiPlayer::updates() const {
	return _updates.events();
}

void MultiPlayer::checkStep() {
	if (_active.empty() || _nextFrameTime == kFrameDisplayTimeAlreadyDone) {
		return;
	} else if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	} else {
		checkNextFrameAvailability();
	}
}

void MultiPlayer::checkNextFrameAvailability() {
	Expects(_nextFrameTime == kTimeUnknown);

	auto next = kTimeUnknown;
	for (const auto &[animation, state] : _active) {
		const auto time = state->nextFrameDisplayTime();
		if (time == kTimeUnknown) {
			for (const auto &[animation, state] : _active) {
				if (state->nextFrameDisplayTime() != kTimeUnknown) {
					PROFILE_LOG(("PLAYER -------- SOME READY, BUT NOT ALL"));
					break;
				}
			}
			return;
		} else if (time == kFrameDisplayTimeAlreadyDone) {
			continue;
		}
		if (next == kTimeUnknown || next > time) {
			next = time;
		}
	}
	if (next == kTimeUnknown) {
		PROFILE_LOG(("PLAYER ALL DISPLAYED, WAITING PAINT."));
		return;
	}
	PROFILE_LOG(("PLAYER NEXT FRAME TIME: %1").arg(next));
	_nextFrameTime = next;
	checkNextFrameRender();
}

void MultiPlayer::checkNextFrameRender() {
	Expects(_nextFrameTime != kTimeUnknown);

	const auto now = crl::now();
	if (now < _nextFrameTime) {
		if (!_timer.isActive()) {
			PROFILE_LOG(("PLAYER TIMER FOR: %1").arg(_nextFrameTime - now));
			_timer.callOnce(_nextFrameTime - now);
		}
	} else {
		_timer.cancel();

		markFrameDisplayed(now);
		addTimelineDelay(now - _nextFrameTime);

		_nextFrameTime = kFrameDisplayTimeAlreadyDone;
		_updates.fire({});
	}
}

void MultiPlayer::updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) {
	const auto i = _active.find(animation);
	Assert(i != _active.end());

	_renderer->updateFrameRequest(i->second, request);
}

void MultiPlayer::markFrameDisplayed(crl::time now) {
	Expects(!_active.empty());

	auto displayed = 0;
	auto waiting = 0;
	for (const auto &[animation, state] : _active) {
		const auto time = state->nextFrameDisplayTime();
		Assert(time != kTimeUnknown);
		if (time == kFrameDisplayTimeAlreadyDone) {
			continue;
		} else if (now >= time) {
			++displayed;
			state->markFrameDisplayed(now);
		} else {
			++waiting;
		}
	}
	PROFILE_LOG(("PLAYER FRAME DISPLAYED AT: %1 (MARKED %2, WAITING %3)").arg(now).arg(displayed).arg(waiting));
}

void MultiPlayer::addTimelineDelay(crl::time delayed) {
	Expects(!_active.empty());

	for (const auto &[animation, state] : _active) {
		state->addTimelineDelay(delayed);
	}
	_delay += delayed;
}

void MultiPlayer::markFrameShown() {
	if (_nextFrameTime == kFrameDisplayTimeAlreadyDone) {
		_nextFrameTime = kTimeUnknown;
		appendPendingToActive();
	}
	auto count = 0;
	for (const auto &[animation, state] : _active) {
		if (state->markFrameShown() != kTimeUnknown) {
			++count;
		}
	}
	PROFILE_LOG(("PLAYER MARKED SHOWN %1 OF %2").arg(count).arg(_active.size()));
	if (count) {
		_renderer->frameShown();
	}
}

} // namespace Lottie
