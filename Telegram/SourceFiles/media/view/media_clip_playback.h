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
#pragma once

#include "ui/widgets/continuous_sliders.h"

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player

namespace Clip {

class Playback {
public:
	Playback();

	void setValueChangedCallback(base::lambda<void(float64)> callback) {
		_valueChanged = std::move(callback);
	}
	void setInLoadingStateChangedCallback(base::lambda<void(bool)> callback) {
		_inLoadingStateChanged = std::move(callback);
	}
	void setValue(float64 value, bool animated);

	void updateState(const Player::TrackState &state);
	void updateLoadingState(float64 progress);

private:
	float64 value() const;
	void step_value(float64 ms, bool timer);

	// This can animate for a very long time (like in music playing),
	// so it should be a BasicAnimation, not an Animation.
	anim::value a_value;
	BasicAnimation _a_value;
	base::lambda<void(float64)> _valueChanged;

	bool _inLoadingState = false;
	base::lambda<void(bool)> _inLoadingStateChanged;

	int64 _position = 0;
	int64 _length = 0;

	bool _playing = false;

};

} // namespace Clip
} // namespace Media
