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
#include "stdafx.h"
#include "ui/effects/cross_animation.h"

namespace Ui {

void CrossAnimation::paint(Painter &p, const style::CrossAnimation &st, style::color color, int x, int y, int outerWidth, float64 shown) {
	PainterHighQualityEnabler hq(p);

	auto deleteScale = shown + st.minScale * (1. - shown);
	auto deleteSkip = deleteScale * st.skip + (1. - deleteScale) * (st.size / 2);
	auto sqrt2 = sqrt(2.);
	auto deleteLeft = rtlpoint(x + deleteSkip, 0, outerWidth).x() + 0.;
	auto deleteTop = y + deleteSkip + 0.;
	auto deleteWidth = st.size - 2 * deleteSkip;
	auto deleteHeight = st.size - 2 * deleteSkip;
	auto deleteStroke = st.stroke / sqrt2;
	QPointF pathDelete[] = {
		{ deleteLeft, deleteTop + deleteStroke },
		{ deleteLeft + deleteStroke, deleteTop },
		{ deleteLeft + (deleteWidth / 2.), deleteTop + (deleteHeight / 2.) - deleteStroke },
		{ deleteLeft + deleteWidth - deleteStroke, deleteTop },
		{ deleteLeft + deleteWidth, deleteTop + deleteStroke },
		{ deleteLeft + (deleteWidth / 2.) + deleteStroke, deleteTop + (deleteHeight / 2.) },
		{ deleteLeft + deleteWidth, deleteTop + deleteHeight - deleteStroke },
		{ deleteLeft + deleteWidth - deleteStroke, deleteTop + deleteHeight },
		{ deleteLeft + (deleteWidth / 2.), deleteTop + (deleteHeight / 2.) + deleteStroke },
		{ deleteLeft + deleteStroke, deleteTop + deleteHeight },
		{ deleteLeft, deleteTop + deleteHeight - deleteStroke },
		{ deleteLeft + (deleteWidth / 2.) - deleteStroke, deleteTop + (deleteHeight / 2.) },
	};
	if (shown < 1.) {
		auto alpha = -(shown - 1.) * M_PI_2;
		auto cosalpha = cos(alpha);
		auto sinalpha = sin(alpha);
		auto shiftx = deleteLeft + (deleteWidth / 2.);
		auto shifty = deleteTop + (deleteHeight / 2.);
		for (auto &point : pathDelete) {
			auto x = point.x() - shiftx;
			auto y = point.y() - shifty;
			point.setX(shiftx + x * cosalpha - y * sinalpha);
			point.setY(shifty + y * cosalpha + x * sinalpha);
		}
	}
	QPainterPath path;
	path.moveTo(pathDelete[0]);
	for (int i = 1; i != base::array_size(pathDelete); ++i) {
		path.lineTo(pathDelete[i]);
	}
	p.fillPath(path, color);
}

} // namespace Ui
