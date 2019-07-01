/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_single_player.h"

#include "lottie/lottie_frame_renderer.h"

namespace Lottie {

SinglePlayer::SinglePlayer(
	const QByteArray &content,
	const FrameRequest &request)
: _animation(this, content, request)
, _timer([=] { checkNextFrameRender(); })
, _renderer(FrameRenderer::Instance()) {
}

SinglePlayer::SinglePlayer(
	FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
	FnMut<void(QByteArray &&cached)> put, // Unknown thread.
	const QByteArray &content,
	const FrameRequest &request)
: _animation(this, std::move(get), std::move(put), content, request)
, _timer([=] { checkNextFrameRender(); })
, _renderer(FrameRenderer::Instance()) {
}

SinglePlayer::~SinglePlayer() {
	if (_state) {
		_renderer->remove(_state);
	}
}

void SinglePlayer::start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) {
	Expects(animation == &_animation);

	_state = state.get();
	auto information = state->information();
	state->start(this, crl::now());
	_renderer->append(std::move(state));
	_updates.fire({ std::move(information) });

	crl::on_main_update_requests(
	) | rpl::start_with_next([=] {
		checkStep();
	}, _lifetime);
}

void SinglePlayer::failed(not_null<Animation*> animation, Error error) {
	Expects(animation == &_animation);

	_updates.fire_error(std::move(error));
}

rpl::producer<Update, Error> SinglePlayer::updates() const {
	return _updates.events();
}

bool SinglePlayer::ready() const {
	return _animation.ready();
}

QImage SinglePlayer::frame(const FrameRequest &request) const {
	return _animation.frame(request);
}

void SinglePlayer::checkStep() {
	if (_nextFrameTime == kFrameDisplayTimeAlreadyDone) {
		return;
	} else if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	} else {
		checkNextFrameAvailability();
	}
}

void SinglePlayer::checkNextFrameAvailability() {
	Expects(_state != nullptr);
	Expects(_nextFrameTime == kTimeUnknown);

	_nextFrameTime = _state->nextFrameDisplayTime();
	Assert(_nextFrameTime != kFrameDisplayTimeAlreadyDone);
	if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	}
}

void SinglePlayer::checkNextFrameRender() {
	Expects(_nextFrameTime != kTimeUnknown);

	const auto now = crl::now();
	if (now < _nextFrameTime) {
		if (!_timer.isActive()) {
			_timer.callOnce(_nextFrameTime - now);
		}
	} else {
		_timer.cancel();

		_state->markFrameDisplayed(now);
		_state->addTimelineDelay(now - _nextFrameTime);

		_nextFrameTime = kFrameDisplayTimeAlreadyDone;
		_updates.fire({ DisplayFrameRequest() });
	}
}

void SinglePlayer::updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) {
	Expects(animation == &_animation);
	Expects(_state != nullptr);

	_renderer->updateFrameRequest(_state, request);
}

void SinglePlayer::markFrameShown() {
	Expects(_state != nullptr);

	if (_nextFrameTime == kFrameDisplayTimeAlreadyDone) {
		_nextFrameTime = kTimeUnknown;
	}
	if (_state->markFrameShown()) {
		_renderer->frameShown();
	}
}

} // namespace Lottie
