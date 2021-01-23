/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_playback_progress.h"

#include "media/audio/media_audio.h"
#include "styles/style_media_view.h"

namespace Media {
namespace View {
namespace {

constexpr auto kPlaybackAnimationDurationMs = crl::time(200);

} // namespace

PlaybackProgress::PlaybackProgress()
: _valueAnimation([=](crl::time now) {
	return valueAnimationCallback(now);
})
, _availableTillAnimation([=](crl::time now) {
	return availableTillAnimationCallback(now);
}) {
}

void PlaybackProgress::updateState(
		const Player::TrackState &state,
		float64 loadedTillPercent) {
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
	const auto loadedTill = (loadedTillPercent != 0.)
		? int64(std::floor(loadedTillPercent * length))
		: -1;
	const auto availableTill = (length && loadedTill > position)
		? std::max(receivedTill, loadedTill)
		: receivedTill;

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
		? std::clamp(float64(position) / length, 0., 1.)
		: 0.;
	const auto availableTillProgress = (availableTill > position)
		? std::clamp(float64(availableTill) / length, 0., 1.)
		: -1.;
	const auto animatedPosition = position + (state.frequency * kPlaybackAnimationDurationMs / 1000);
	const auto animatedProgress = length ? qMax(float64(animatedPosition) / length, 0.) : 0.;
	if (length != _length || position != _position || wasInLoadingState) {
		const auto animated = length
			&& _length
			&& (animatedProgress > value())
			&& (position > _position)
			&& (position < _position + state.frequency);
		if (animated) {
			setValue(animatedProgress, animated);
		} else {
			setValue(progress, animated);
		}
		_position = position;
		_length = length;
	}
	if (availableTill != _availableTill) {
		setAvailableTill(availableTillProgress);
		_availableTill = availableTill;
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
		valueAnimationCallback(crl::now());
		a_value.start(value);
		_valueAnimation.start();
	} else {
		a_value = anim::value(value, value);
		_valueAnimation.stop();
	}
	emitUpdatedValue();
}

void PlaybackProgress::setAvailableTill(float64 value) {
	const auto current = a_availableTill.current();
	if (value > current && current > 0.) {
		availableTillAnimationCallback(crl::now());
		a_availableTill.start(value);
		_availableTillAnimation.start();
	} else if (value > a_value.current()) {
		a_availableTill = anim::value(a_value.current(), value);
		_availableTillAnimation.start();
	} else {
		a_availableTill = anim::value(-1., -1.);
		_availableTillAnimation.stop();
	}
	emitUpdatedValue();
}

bool PlaybackProgress::valueAnimationCallback(float64 now) {
	const auto time = (now - _valueAnimation.started());
	const auto dt = anim::Disabled()
		? 1.
		: (time / kPlaybackAnimationDurationMs);
	if (dt >= 1.) {
		a_value.finish();
	} else {
		a_value.update(dt, anim::linear);
	}
	emitUpdatedValue();
	return (dt < 1.);
}

bool PlaybackProgress::availableTillAnimationCallback(float64 now) {
	const auto time = now - _availableTillAnimation.started();
	const auto dt = anim::Disabled()
		? 1.
		: (time / kPlaybackAnimationDurationMs);
	if (dt >= 1.) {
		a_availableTill.finish();
	} else {
		a_availableTill.update(dt, anim::linear);
	}
	emitUpdatedValue();
	return (dt < 1.);
}

void PlaybackProgress::emitUpdatedValue() {
	if (_valueChanged) {
		const auto value = a_value.current();
		const auto availableTill = a_availableTill.current();
		_valueChanged(value, std::max(value, availableTill));
	}
}

} // namespace View
} // namespace Media
