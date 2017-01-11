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

#include "styles/style_widgets.h"

namespace Ui {

class RippleAnimation {
public:
	using UpdateCallback = base::lambda_copy<void()>;

	// White upon transparent mask, like colorizeImage(black-white-mask, white).
	RippleAnimation(const style::RippleAnimation &st, QImage mask, const UpdateCallback &update);

	void add(QPoint origin, int startRadius = 0);
	void addFading();
	void lastStop();
	void lastUnstop();
	void lastFinish();

	void paint(QPainter &p, int x, int y, int outerWidth, TimeMs ms, const QColor *colorOverride = nullptr);

	bool empty() const {
		return _ripples.isEmpty();
	}

	static QImage maskByDrawer(QSize size, bool filled, base::lambda<void(QPainter &p)> &&drawer);
	static QImage rectMask(QSize size);
	static QImage roundRectMask(QSize size, int radius);
	static QImage ellipseMask(QSize size);

	~RippleAnimation() {
		clear();
	}

private:
	void clear();
	void clearFinished();

	const style::RippleAnimation &_st;
	QPixmap _mask;
	UpdateCallback _update;

	class Ripple;
	QList<Ripple*> _ripples;

};

} // namespace Ui
