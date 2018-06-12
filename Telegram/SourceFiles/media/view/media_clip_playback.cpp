/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_clip_playback.h"

#include "styles/style_mediaview.h"
#include "media/media_audio.h"

namespace Media {
namespace Clip {
namespace {

constexpr auto kPlaybackAnimationDurationMs = TimeMs(200);

} // namespace

Playback::Playback() : _a_value(animation(this, &Playback::step_value)) {
}

void Playback::updateState(const Player::TrackState &state) {
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

void Playback::updateLoadingState(float64 progress) {
	if (!_inLoadingState) {
		_inLoadingState = true;
		if (_inLoadingStateChanged) {
			_inLoadingStateChanged(true);
		}
	}
	auto animated = (progress > value());
	setValue(progress, animated);
}

float64 Playback::value() const {
	return qMin(a_value.current(), 1.);
}

float64 Playback::value(TimeMs ms) {
	_a_value.step(ms);
	return value();
}

void Playback::setValue(float64 value, bool animated) {
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

void Playback::step_value(float64 ms, bool timer) {
	auto dt = ms / kPlaybackAnimationDurationMs;
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

} // namespace Clip
} // namespace Media
