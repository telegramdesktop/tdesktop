/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_single_player.h"

#include "lottie/lottie_frame_renderer.h"

namespace Lottie {
namespace {

} // namespace

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

void SinglePlayer::start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) {
	Expects(animation == &_animation);

	_state = state.get();
	auto information = state->information();
	state->start(this, crl::now());
	_renderer = FrameRenderer::Instance();
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

SinglePlayer::~SinglePlayer() {
	if (_state) {
		_renderer->remove(_state);
	}
}

rpl::producer<Update, Error> SinglePlayer::updates() {
	return _updates.events();
}

bool SinglePlayer::ready() const {
	return _animation.ready();
}

QImage SinglePlayer::frame(const FrameRequest &request) const {
	return _animation.frame(request);
}

void SinglePlayer::checkStep() {
	if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	} else {
		checkNextFrameAvailability();
	}
}

void SinglePlayer::checkNextFrameAvailability() {
	Expects(_state != nullptr);

	_nextFrameTime = _state->nextFrameDisplayTime();
	if (_nextFrameTime != kTimeUnknown) {
		checkStep();
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

		_nextFrameTime = kTimeUnknown;
		const auto position = markFrameDisplayed(now);
		_updates.fire({ DisplayFrameRequest{ position } });
	}
}

void SinglePlayer::updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) {
	Expects(animation == &_animation);
	Expects(_state != nullptr);

	_renderer->updateFrameRequest(_state, request);
}

crl::time SinglePlayer::markFrameDisplayed(crl::time now) {
	Expects(_state != nullptr);

	return _state->markFrameDisplayed(now);
}

crl::time SinglePlayer::markFrameShown() {
	Expects(_renderer != nullptr);

	const auto result = _state->markFrameShown();
	_renderer->frameShown(_state);

	return result;
}

} // namespace Lottie
