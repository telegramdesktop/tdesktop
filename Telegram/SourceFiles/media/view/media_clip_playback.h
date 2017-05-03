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
	Playback(Ui::ContinuousSlider *slider);

	void updateState(const Player::TrackState &state);
	void updateLoadingState(float64 progress);

	void setFadeOpacity(float64 opacity) {
		_slider->setFadeOpacity(opacity);
	}
	void setChangeProgressCallback(Ui::ContinuousSlider::Callback &&callback) {
		_slider->setChangeProgressCallback(std::move(callback));
	}
	void setChangeFinishedCallback(Ui::ContinuousSlider::Callback &&callback) {
		_slider->setChangeFinishedCallback(std::move(callback));
	}
	void setGeometry(int x, int y, int w, int h) {
		_slider->setGeometry(x, y, w, h);
	}
	void hide() {
		_slider->hide();
	}
	void show() {
		_slider->show();
	}
	void moveToLeft(int x, int y) {
		_slider->moveToLeft(x, y);
	}
	void resize(int w, int h) {
		_slider->resize(w, h);
	}
	void setDisabled(bool disabled) {
		_slider->setDisabled(disabled);
	}

private:
	Ui::ContinuousSlider *_slider;

	int64 _position = 0;
	int64 _length = 0;

	bool _playing = false;

};

} // namespace Clip
} // namespace Media
