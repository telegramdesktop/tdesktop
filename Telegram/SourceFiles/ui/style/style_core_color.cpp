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
#include "ui/style/style_core_color.h"

namespace style {
namespace internal {

Color::Proxy Color::operator[](const style::palette &paletteOverride) const {
	auto index = main_palette::indexOfColor(*this);
	return Proxy((index >= 0) ? paletteOverride.colorAtIndex(index) : (*this));
}

ColorData::ColorData(uchar r, uchar g, uchar b, uchar a) : c(int(r), int(g), int(b), int(a)), p(c), b(c) {
}

void ColorData::set(uchar r, uchar g, uchar b, uchar a) {
	this->c = QColor(int(r), int(g), int(b), int(a));
	this->p = QPen(c);
	this->b = QBrush(c);
}

} // namespace internal
} // namespace style
