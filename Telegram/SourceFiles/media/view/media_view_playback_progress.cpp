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

PlaybackProgress::PlaybackProgress() : _a_value(animation(this, &PlaybackProgress::step_value)) {
}

void PlaybackProgress::updateState(const Player::TrackState &state) {
	qint64 position = 0, length = state.length;

	auto wasInLoadingState = _inLoadingState;
	if (wasInLoadingState) {
		_inLoadingState = false;
		if (_inLoadingStateChanged) {
			_inLoadingStateChanged(false);
		}
	}

	_playing = !Player::IsStopped(state.state);
	if (Player::IsStoppedAtEnd(state.state)) {
		position = state.length;
	} else if (!Player::IsStoppedOrStopping(state.state)) {
		position = state.position;
	} else {
		position = 0;
	}

	auto progress = 0.;
	if (position > length) {
		progress = 1.;
	} else if (length) {
		progress = snap(float64(position) / length, 0., 1.);
	}
	auto animatedPosition = position + (state.frequency * kPlaybackAnimationDurationMs / 1000);
	auto animatedProgress = length ? qMax(float64(animatedPosition) / length, 0.) : 0.;
	if (length != _length || position != _position || wasInLoadingState) {
		if (auto animated = (length && _length && animatedProgress > value())) {
			setValue(animatedProgress, animated);
		} else {
			setValue(progress, animated);
		}
		_position = position;
		_length = length;
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

float64 PlaybackProgress::value(crl::time ms) {
	_a_value.step(ms);
	return value();
}

void PlaybackProgress::setValue(float64 value, bool animated) {
	if (animated) {
		a_value.start(value);
		_a_value.start();
	} else {
		a_value = anim::value(value, value);
		_a_value.stop();
	}
	if (_valueChanged) {
		_valueChanged(a_value.current());
	}
}

void PlaybackProgress::step_value(float64 ms, bool timer) {
	auto dt = anim::Disabled() ? 1. : (ms / kPlaybackAnimationDurationMs);
	if (dt >= 1.) {
		_a_value.stop();
		a_value.finish();
	} else {
		a_value.update(dt, anim::linear);
	}
	if (timer && _valueChanged) {
		_valueChanged(a_value.current());
	}
}

} // namespace View
} // namespace Media
