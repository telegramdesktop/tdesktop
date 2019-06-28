/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_multi_player.h"

#include "lottie/lottie_frame_renderer.h"
#include "lottie/lottie_animation.h"

#include <range/v3/algorithm/remove.hpp>

namespace Lottie {

std::shared_ptr<FrameRenderer> MakeFrameRenderer() {
	return FrameRenderer::CreateIndependent();
}

MultiPlayer::MultiPlayer(std::shared_ptr<FrameRenderer> renderer)
: _renderer(renderer ? std::move(renderer) : FrameRenderer::Instance()) {
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

crl::time MultiPlayer::startAtRightTime(not_null<SharedState*> state) {
	Expects(!_active.empty());
	Expects((_active.size() == 1) == (_started == kTimeUnknown));

	if (_active.size() == 1) {
		_started = crl::now();
		state->start(this, _started);
		return _started;
	}

	const auto now = crl::now();
	const auto rate = state->information().frameRate;
	Assert(rate != 0);

	const auto started = _started + _accumulatedDelay;
	const auto skipFrames = (now - started) * rate / 1000;
	const auto startAt = started + (1000 * skipFrames / rate);

	state->start(this, startAt);
	return startAt;
}

void MultiPlayer::start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) {
	Expects(state != nullptr);

	_active.emplace(animation, state.get());

	auto information = state->information();
	startAtRightTime(state.get());
	_renderer->append(std::move(state));
	_updates.fire({});

	crl::on_main_update_requests(
	) | rpl::start_with_next([=] {
		checkStep();
	}, _lifetime);

	_nextFrameTime = kTimeUnknown;
	_timer.cancel();
	checkStep();
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
		_accumulatedDelay = 0;
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
	if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	} else {
		checkNextFrameAvailability();
	}
}

void MultiPlayer::checkNextFrameAvailability() {
	if (_active.empty()) {
		return;
	}
	auto next = kTimeUnknown;
	for (const auto &[animation, state] : _active) {
		const auto time = state->nextFrameDisplayTime();
		if (time == kTimeUnknown) {
			return;
		}
		if (next == kTimeUnknown || next > time) {
			next = time;
		}
	}
	Assert(next != kTimeUnknown);
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

		const auto exact = std::exchange(_nextFrameTime, kTimeUnknown);
		markFrameDisplayed(now, now - exact);
		_updates.fire({});
		checkStep();
	}
}

void MultiPlayer::updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) {
	const auto i = _active.find(animation);
	Assert(i != _active.end());

	_renderer->updateFrameRequest(i->second, request);
}

void MultiPlayer::markFrameDisplayed(crl::time now, crl::time delayed) {
	Expects(!_active.empty());

	for (const auto &[animation, state] : _active) {
		const auto time = state->nextFrameDisplayTime();
		Assert(time != kTimeUnknown);
		if (now >= time) {
			state->markFrameDisplayed(now, delayed);
		}
	}
}

void MultiPlayer::markFrameShown() {
	if (_active.empty()) {
		return;
	}
	for (const auto &[animation, state] : _active) {
		state->markFrameShown();
	}
	_renderer->frameShown();
}

} // namespace Lottie
