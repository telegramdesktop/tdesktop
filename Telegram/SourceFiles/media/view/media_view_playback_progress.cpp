/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_playback_progress.h"

#include "media/audio/media_audio.h"
#include "styles/style_mediaview.h"

namespace Media {
namespace View {
namespace {

constexpr auto kPlaybackAnimationDurationMs = crl::time(200);

} // namespace

PlaybackProgress::PlaybackProgress()
: _a_value([=](float64 ms) { step_value(ms); })
, _a_receivedTill([=](float64 ms) { step_receivedTill(ms); }) {
}

void PlaybackProgress::updateState(const Player::TrackState &state) {
	_playing = !Player::IsStopped(state.state);
	const auto length = state.length;
	const auto position = Player::IsStoppedAtEnd(state.state)
		? state.length
		: Player::IsStoppedOrStopping(state.state)
		? 0
		: state.position;
	const auto receivedTill = (length && state.receivedTill > position)
		? state.receivedTill
		: -1;

	const auto wasInLoadingState = _inLoadingState;
	if (wasInLoadingState) {
		_inLoadingState = false;
		if (_inLoadingStateChanged) {
			_inLoadingStateChanged(false);
		}
	}

	const auto progress = (position > length)
		? 1.
		: length
		? snap(float64(position) / length, 0., 1.)
		: 0.;
	const auto receivedTillProgress = (receivedTill > position)
		? snap(float64(receivedTill) / length, 0., 1.)
		: -1.;
	const auto animatedPosition = position + (state.frequency * kPlaybackAnimationDurationMs / 1000);
	const auto animatedProgress = length ? qMax(float64(animatedPosition) / length, 0.) : 0.;
	if (length != _length || position != _position || wasInLoadingState) {
		if (auto animated = (length && _length && animatedProgress > value())) {
			setValue(animatedProgress, animated);
		} else {
			setValue(progress, animated);
		}
		_position = position;
		_length = length;
	}
	if (receivedTill != _receivedTill) {
		setReceivedTill(receivedTillProgress);
		_receivedTill = receivedTill;
	}
}

void PlaybackProgress::updateLoadingState(float64 progress) {
	if (!_inLoadingState) {
		_inLoadingState = true;
		if (_inLoadingStateChanged) {
			_inLoadingStateChanged(true);
		}
	}
	auto animated = (progress > value());
	setValue(progress, animated);
}

float64 PlaybackProgress::value() const {
	return qMin(a_value.current(), 1.);
}

void PlaybackProgress::setValue(float64 value, bool animated) {
	if (animated) {
		a_value.start(value);
		_a_value.start();
	} else {
		a_value = anim::value(value, value);
		_a_value.stop();
	}
	emitUpdatedValue();
}

void PlaybackProgress::setReceivedTill(float64 value) {
	const auto current = a_receivedTill.current();
	if (value > current && current > 0.) {
		a_receivedTill.start(value);
		_a_receivedTill.start();
	} else if (value > a_value.current()) {
		a_receivedTill = anim::value(a_value.current(), value);
		_a_receivedTill.start();
	} else {
		a_receivedTill = anim::value(-1., -1.);
		_a_receivedTill.stop();
	}
	emitUpdatedValue();
}

void PlaybackProgress::step_value(float64 now) {
	const auto time = (now - _a_value.started());
	const auto dt = anim::Disabled()
		? 1.
		: (time / kPlaybackAnimationDurationMs);
	if (dt >= 1.) {
		_a_value.stop();
		a_value.finish();
	} else {
		a_value.update(dt, anim::linear);
	}
	emitUpdatedValue();
}

void PlaybackProgress::step_receivedTill(float64 now) {
	const auto time = now - _a_receivedTill.started();
	const auto dt = anim::Disabled()
		? 1.
		: (time / kPlaybackAnimationDurationMs);
	if (dt >= 1.) {
		_a_receivedTill.stop();
		a_receivedTill.finish();
	} else {
		a_receivedTill.update(dt, anim::linear);
	}
	emitUpdatedValue();
}

void PlaybackProgress::emitUpdatedValue() {
	if (_valueChanged) {
		const auto value = a_value.current();
		const auto receivedTill = a_receivedTill.current();
		_valueChanged(value, std::max(value, receivedTill));
	}
}

} // namespace View
} // namespace Media
