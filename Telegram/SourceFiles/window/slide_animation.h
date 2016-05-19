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

namespace Window {

enum class SlideDirection {
	FromRight,
	FromLeft,
};

class SlideAnimation {
public:
	SlideAnimation();

	void paintContents(Painter &p, const QRect &update) const;

	void setDirection(SlideDirection direction);
	void setPixmaps(const QPixmap &oldContentCache, const QPixmap &newContentCache);
	void setTopBarShadow(bool enabled);

	using RepaintCallback = Function<void>;
	void setRepaintCallback(RepaintCallback &&callback);

	using FinishedCallback = Function<void>;
	void setFinishedCallback(FinishedCallback &&callback);

	void start();

private:
	void step(float64 ms, bool timer);

	SlideDirection _direction = SlideDirection::FromRight;
	bool _topBarShadowEnabled = false;

	mutable Animation _animation;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_progress;

	RepaintCallback _repaintCallback;
	FinishedCallback _finishedCallback;

};

} // namespace Window
