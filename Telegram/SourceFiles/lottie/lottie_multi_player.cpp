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

MultiPlayer::MultiPlayer(
	Quality quality,
	std::shared_ptr<FrameRenderer> renderer)
: _quality(quality)
, _timer([=] { checkNextFrameRender(); })
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
	for (const auto &[animation, info] : _paused) {
		_renderer->remove(info.state);
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
		request,
		_quality));
	return _animations.back().get();
}

not_null<Animation*> MultiPlayer::append(
		const QByteArray &content,
		const FrameRequest &request) {
	_animations.push_back(std::make_unique<Animation>(
		this,
		content,
		request,
		_quality));
	return _animations.back().get();
}

void MultiPlayer::startAtRightTime(std::unique_ptr<SharedState> state) {
	if (_started == kTimeUnknown) {
		_started = crl::now();
		_lastSyncTime = kTimeUnknown;
		_delay = 0;
	}
	const auto lastSyncTime = (_lastSyncTime != kTimeUnknown)
		? _lastSyncTime
		: _started;
	const auto frameIndex = countFrameIndex(
		state.get(),
		lastSyncTime,
		_delay);
	state->start(this, _started, _delay, frameIndex);
	const auto request = state->frameForPaint()->request;
	_renderer->append(std::move(state), request);
}

int MultiPlayer::countFrameIndex(
		not_null<SharedState*> state,
		crl::time time,
		crl::time delay) const {
	Expects(time != kTimeUnknown);

	const auto rate = state->information().frameRate;
	Assert(rate != 0);

	const auto framesTime = time - _started - delay;
	return ((framesTime + 1) * rate - 1) / 1000;
}

void MultiPlayer::start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) {
	Expects(state != nullptr);

	const auto paused = _pausedBeforeStart.remove(animation);
	auto info = StartingInfo{ std::move(state), paused };
	if (_active.empty()
		|| (_lastSyncTime == kTimeUnknown
			&& _nextFrameTime == kTimeUnknown)) {
		addNewToActive(animation, std::move(info));
	} else {
		// We always try to mark as shown at the same time, so we start a new
		// animation at the same time we mark all existing as shown.
		_pendingToStart.emplace(animation, std::move(info));
	}
	_updates.fire({});
}

void MultiPlayer::addNewToActive(
		not_null<Animation*> animation,
		StartingInfo &&info) {
	_active.emplace(animation, info.state.get());
	startAtRightTime(std::move(info.state));
	if (info.paused) {
		_pendingPause.emplace(animation);
	}
}

void MultiPlayer::processPending() {
	Expects(_lastSyncTime != kTimeUnknown);

	for (const auto &animation : base::take(_pendingPause)) {
		pauseAndSaveState(animation);
	}
	for (const auto &animation : base::take(_pendingUnpause)) {
		unpauseAndKeepUp(animation);
	}
	for (auto &[animation, info] : base::take(_pendingToStart)) {
		addNewToActive(animation, std::move(info));
	}
	for (const auto &animation : base::take(_pendingRemove)) {
		removeNow(animation);
	}
}

void MultiPlayer::remove(not_null<Animation*> animation) {
	if (!_active.empty()) {
		_pendingRemove.emplace(animation);
	} else {
		removeNow(animation);
	}
}

void MultiPlayer::removeNow(not_null<Animation*> animation) {
	const auto i = _active.find(animation);
	if (i != end(_active)) {
		_renderer->remove(i->second);
		_active.erase(i);
	}
	const auto j = _paused.find(animation);
	if (j != end(_paused)) {
		_renderer->remove(j->second.state);
		_paused.erase(j);
	}

	_pendingRemove.remove(animation);
	_pendingToStart.remove(animation);
	_pendingPause.remove(animation);
	_pendingUnpause.remove(animation);
	_pausedBeforeStart.remove(animation);
	_animations.erase(
		ranges::remove(
			_animations,
			animation.get(),
			&std::unique_ptr<Animation>::get),
		end(_animations));

	if (_active.empty()) {
		_nextFrameTime = kTimeUnknown;
		_timer.cancel();
		if (_paused.empty()) {
			_started = kTimeUnknown;
			_lastSyncTime = kTimeUnknown;
			_delay = 0;
		}
	}
}

void MultiPlayer::pause(not_null<Animation*> animation) {
	if (_active.contains(animation)) {
		_pendingPause.emplace(animation);
	} else if (_paused.contains(animation)) {
		_pendingUnpause.remove(animation);
	} else if (const auto i = _pendingToStart.find(animation); i != end(_pendingToStart)) {
		i->second.paused = true;
	} else {
		_pausedBeforeStart.emplace(animation);
	}
}

void MultiPlayer::unpause(not_null<Animation*> animation) {
	if (const auto i = _paused.find(animation); i != end(_paused)) {
		if (_active.empty()) {
			unpauseFirst(animation, i->second.state);
			_paused.erase(i);
		} else {
			_pendingUnpause.emplace(animation);
		}
	} else if (_pendingPause.contains(animation)) {
		_pendingPause.remove(animation);
	} else {
		const auto i = _pendingToStart.find(animation);
		if (i != end(_pendingToStart)) {
			i->second.paused = false;
		} else {
			_pausedBeforeStart.remove(animation);
		}
	}
}

void MultiPlayer::unpauseFirst(
		not_null<Animation*> animation,
		not_null<SharedState*> state) {
	Expects(_lastSyncTime != kTimeUnknown);

	_active.emplace(animation, state);

	const auto now = crl::now();
	addTimelineDelay(now - _lastSyncTime);
	_lastSyncTime = now;

	markFrameShown();
}

void MultiPlayer::pauseAndSaveState(not_null<Animation*> animation) {
	Expects(_lastSyncTime != kTimeUnknown);

	const auto i = _active.find(animation);
	Assert(i != end(_active));
	_paused.emplace(
		animation,
		PausedInfo{ i->second, _lastSyncTime, _delay });
	_active.erase(i);
}

void MultiPlayer::unpauseAndKeepUp(not_null<Animation*> animation) {
	Expects(_lastSyncTime != kTimeUnknown);

	const auto i = _paused.find(animation);
	Assert(i != end(_paused));
	const auto state = i->second.state;
	const auto frameIndexAtPaused = countFrameIndex(
		state,
		i->second.pauseTime,
		i->second.pauseDelay);
	const auto frameIndexNow = countFrameIndex(
		state,
		_lastSyncTime,
		_delay);
	state->addTimelineDelay(
		(_delay - i->second.pauseDelay),
		frameIndexNow - frameIndexAtPaused);
	_active.emplace(animation, state);
	_paused.erase(i);
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
		return;
	}
	_nextFrameTime = next;
	checkNextFrameRender();
}

void MultiPlayer::checkNextFrameRender() {
	Expects(_nextFrameTime != kTimeUnknown);

	const auto now = crl::now();
	if (now < _nextFrameTime) {
		if (!_timer.isActive()) {
			_timer.callOnce(_nextFrameTime - now);
		}
	} else {
		_timer.cancel();

		markFrameDisplayed(now);
		addTimelineDelay(now - _nextFrameTime);
		_lastSyncTime = now;
		_nextFrameTime = kFrameDisplayTimeAlreadyDone;
		processPending();
		_updates.fire({});
	}
}

void MultiPlayer::updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) {
	const auto state = [&]() -> Lottie::SharedState* {
		const auto key = animation;
		if (const auto i = _active.find(animation); i != end(_active)) {
			return i->second;
		} else if (const auto j = _paused.find(animation);
				j != end(_paused)) {
			return j->second.state;
		} else if (const auto k = _pendingToStart.find(animation);
				k != end(_pendingToStart)) {
			return nullptr;
		}
		Unexpected("Animation in MultiPlayer::updateFrameRequest.");
	}();
	if (state) {
		_renderer->updateFrameRequest(state, request);
	}
}

void MultiPlayer::markFrameDisplayed(crl::time now) {
	Expects(!_active.empty());

	for (const auto &[animation, state] : _active) {
		const auto time = state->nextFrameDisplayTime();
		Assert(time != kTimeUnknown);
		if (time == kFrameDisplayTimeAlreadyDone) {
			continue;
		} else if (now >= time) {
			state->markFrameDisplayed(now);
		}
	}
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
	}
	auto count = 0;
	for (const auto &[animation, state] : _active) {
		if (state->markFrameShown()) {
			++count;
		}
	}
	if (count) {
		_renderer->frameShown();
	}
}

} // namespace Lottie
