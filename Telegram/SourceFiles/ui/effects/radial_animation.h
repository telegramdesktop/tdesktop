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

namespace Ui {

class RadialAnimation {
public:
	RadialAnimation(AnimationCallbacks &&callbacks);

	float64 opacity() const {
		return _opacity;
	}
	bool animating() const {
		return _animation.animating();
	}

	void start(float64 prg);
	void update(float64 prg, bool finished, TimeMs ms);
	void stop();

	void step(TimeMs ms);
	void step() {
		step(getms());
	}

	void draw(Painter &p, const QRect &inner, int32 thickness, style::color color);

private:
	TimeMs _firstStart = 0;
	TimeMs _lastStart = 0;
	TimeMs _lastTime = 0;
	float64 _opacity = 0.;
	anim::value a_arcEnd;
	anim::value a_arcStart;
	BasicAnimation _animation;

};

} // namespace Ui
