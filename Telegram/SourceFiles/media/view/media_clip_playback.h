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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

struct AudioPlaybackState;

namespace Media {
namespace Clip {

class Playback : public TWidget {
	Q_OBJECT

public:
	Playback(QWidget *parent);

	void updateState(const AudioPlaybackState &playbackState, bool reset);
	void setFadeOpacity(float64 opacity);

signals:
	void seekProgress(float64 progress);
	void seekFinished(float64 progress);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;

private:
	void step_progress(float64 ms, bool timer);
	void updateCallback() {
		update();
	}
	void setOver(bool over);

	bool _over = false;
	FloatAnimation _a_over;

	int64 _position = 0;
	int64 _duration = 0;
	anim::fvalue a_progress = { 0., 0. };
	Animation _a_progress;

	bool _mouseDown = false;
	float64 _downProgress = 0.;

	float64 _fadeOpacity = 1.;
	bool _playing = false;

};

} // namespace Clip
} // namespace Media
