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

#include "styles/style_widgets.h"

namespace Ui {

class RectShadow {
public:
	enum class Side {
		Left   = 0x01,
		Top    = 0x02,
		Right  = 0x04,
		Bottom = 0x08,
	};
	Q_DECLARE_FLAGS(Sides, Side);
	Q_DECLARE_FRIEND_OPERATORS_FOR_FLAGS(Sides);

	RectShadow(const style::icon &topLeft);

	void paint(Painter &p, const QRect &box, int shifty, Sides sides = Side::Left | Side::Top | Side::Right | Side::Bottom);
	style::margins getDimensions(int shifty) const;

private:
	int _size, _pixsize;
	int _thickness = 0;
	QPixmap _corners, _left, _top, _right, _bottom;

};
Q_DECLARE_OPERATORS_FOR_FLAGS(RectShadow::Sides);

} // namespace Ui
