/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "media/view/media_clip_playback.h"

#include "styles/style_mediaview.h"
#include "media/media_audio.h"

namespace Media {
namespace Clip {

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
	if (_playing || state.state == Player::State::Stopped) {
		position = state.position;
	} else if (state.state == Player::State::StoppedAtEnd) {
		position = state.length;
	} else {
		position = 0;
	}

	auto progress = 0.;
	if (position > length) {
		progress = 1.;
	} else if (length) {
		progress = length ? snap(float64(position) / length, 0., 1.) : 0.;
	}
	if (length != _length || position != _position || wasInLoadingState) {
		auto animated = (length && _length && progress > value());
		setValue(progress, animated);
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
	return a_value.current();
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
	auto dt = ms / (2 * AudioVoiceMsgUpdateView);
	if (dt >= 1) {
		_a_value.stop();
		a_value.finish();
	} else {
		a_value.update(qMin(dt, 1.), anim::linear);
	}
	if (timer && _valueChanged) {
		_valueChanged(a_value.current());
	}
}

} // namespace Clip
} // namespace Media
