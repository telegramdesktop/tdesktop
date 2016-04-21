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
#include "stdafx.h"
#include "ui/style/style_core_color.h"

namespace style {
namespace internal {
namespace {

typedef QMap<uint32, ColorData*> ColorDatas;
ColorDatas colorsMap;

uint32 colorKey(uchar r, uchar g, uchar b, uchar a) {
	return (((((uint32(r) << 8) | uint32(g)) << 8) | uint32(b)) << 8) | uint32(a);
}

} // namespace

void destroyColors() {
	for (auto colorData : colorsMap) {
		delete colorData;
	}
	colorsMap.clear();
}

Color::Color(const Color &c) : ptr(c.owner ? new ColorData(*c.ptr) : c.ptr), owner(c.owner) {
}

Color::Color(const QColor &c) : owner(false) {
	init(c.red(), c.green(), c.blue(), c.alpha());
}

Color::Color(uchar r, uchar g, uchar b, uchar a) : owner(false) {
	init(r, g, b, a);
}

Color &Color::operator=(const Color &c) {
	if (this != &c) {
		if (owner) {
			delete ptr;
		}
		ptr = c.owner ? new ColorData(*c.ptr) : c.ptr;
		owner = c.owner;
	}
	return *this;
}

void Color::init(uchar r, uchar g, uchar b, uchar a) {
	uint32 key = colorKey(r, g, b, a);
	auto i = colorsMap.constFind(key);
	if (i == colorsMap.cend()) {
		i = colorsMap.insert(key, new ColorData(r, g, b, a));
	}
	ptr = i.value();
}

Color::~Color() {
	if (owner) {
		delete ptr;
	}
}

ColorData::ColorData(uchar r, uchar g, uchar b, uchar a) : c(int(r), int(g), int(b), int(a)), p(c), b(c) {
}

void ColorData::set(const QColor &color) {
	c = color;
	p = QPen(color);
	b = QBrush(color);
}

} // namespace internal
} // namespace style
