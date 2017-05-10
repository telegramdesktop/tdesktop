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

Playback::Playback(Ui::ContinuousSlider *slider) : _slider(slider) {
}

void Playback::updateState(const Player::TrackState &state) {
	qint64 position = 0, length = state.length;

	auto wasDisabled = _slider->isDisabled();
	if (wasDisabled) setDisabled(false);

	_playing = !Player::IsStopped(state.state);
	if (_playing || state.state == Player::State::Stopped) {
		position = state.position;
	} else if (state.state == Player::State::StoppedAtEnd) {
		position = state.length;
	} else {
		position = 0;
	}

	float64 progress = 0.;
	if (position > length) {
		progress = 1.;
	} else if (length) {
		progress = length ? snap(float64(position) / length, 0., 1.) : 0.;
	}
	if (length != _length || position != _position || wasDisabled) {
		auto animated = (length && _length&& progress > _slider->value());
		_slider->setValue(progress, animated);
		_position = position;
		_length = length;
	}
	_slider->update();
}

void Playback::updateLoadingState(float64 progress) {
	setDisabled(true);
	auto animated = progress > _slider->value();
	_slider->setValue(progress, animated);
}

} // namespace Clip
} // namespace Media
