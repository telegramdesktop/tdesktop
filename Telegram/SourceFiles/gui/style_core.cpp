/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"

namespace {
	typedef QMap<QString, uint32> FontFamilyMap;
	FontFamilyMap _fontFamilyMap;
}

namespace style {
	FontData::FontData(uint32 size, uint32 flags, uint32 family, Font *other) : f(_fontFamilies[family]), m(f), _size(size), _flags(flags), _family(family) {
		if (other) {
			memcpy(modified, other, sizeof(modified));
		} else {
			memset(modified, 0, sizeof(modified));
		}
		modified[_flags] = Font(this);

		f.setPixelSize(size);
		f.setBold(_flags & FontBold);
		f.setItalic(_flags & FontItalic);
		f.setUnderline(_flags & FontUnderline);
		f.setStyleStrategy(QFont::PreferQuality);

		m = QFontMetrics(f);
		height = m.height();
		ascent = m.ascent();
		descent = m.descent();
		spacew = m.width(QLatin1Char(' '));
		elidew = m.width(QLatin1Char('.')) * 3;
	}

	Font FontData::bold(bool set) const {
		return otherFlagsFont(FontBold, set);
	}

	Font FontData::italic(bool set) const {
		return otherFlagsFont(FontItalic, set);
	}

	Font FontData::underline(bool set) const {
		return otherFlagsFont(FontUnderline, set);
	}

	uint32 FontData::flags() const {
		return _flags;
	}

	Font FontData::otherFlagsFont(uint32 flag, bool set) const {
		int32 newFlags = set ? (_flags | flag) : (_flags & ~flag);
		if (!modified[newFlags].v()) {
			modified[newFlags] = Font(_size, newFlags, _family, modified);
		}
		return modified[newFlags];
	}

	Font::Font(uint32 size, uint32 flags, const QString &family) {
		if (_fontFamilyMap.isEmpty()) {
			for (uint32 i = 0, s = style::_fontFamilies.size(); i != s; ++i) {
				_fontFamilyMap.insert(style::_fontFamilies.at(i), i);
			}
		}

		FontFamilyMap::const_iterator i = _fontFamilyMap.constFind(family);
		if (i == _fontFamilyMap.cend()) {
			style::_fontFamilies.push_back(family);
			i = _fontFamilyMap.insert(family, style::_fontFamilies.size() - 1);
		}
		init(i.value(), size, flags, 0);
	}

	Font::Font(uint32 size, uint32 flags, uint32 family) {
		init(size, flags, family, 0);
	}

	Font::Font(uint32 size, uint32 flags, uint32 family, Font *modified) {
		init(size, flags, family, modified);
	}

	void Font::init(uint32 size, uint32 flags, uint32 family, Font *modified) {
		uint32 key = _fontKey(size, flags, family);
		FontDatas::const_iterator i = _fontsMap.constFind(key);
		if (i == _fontsMap.cend()) {
			i = _fontsMap.insert(key, new FontData(size, flags, family, modified));
		}
		ptr = i.value();
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

	void Color::set(const QColor &newv) {
		if (!owner) {
			ptr = new ColorData(*ptr);
			owner = true;
		}
		ptr->set(newv);
	}

	void Color::set(uchar r, uchar g, uchar b, uchar a) {
		if (!owner) {
			ptr = new ColorData(*ptr);
			owner = true;
		}
		ptr->set(QColor(r, g, b, a));
	}

	void Color::init(uchar r, uchar g, uchar b, uchar a) {
		uint32 key = _colorKey(r, g, b, a);
		ColorDatas::const_iterator i = _colorsMap.constFind(key);
		if (i == _colorsMap.cend()) {
			i = _colorsMap.insert(key, new ColorData(r, g, b, a));
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

	void stopManager() {
		for (FontDatas::const_iterator i = _fontsMap.cbegin(), e = _fontsMap.cend(); i != e; ++i) {
			delete i.value();
		}
		_fontsMap.clear();

		for (ColorDatas::const_iterator i = _colorsMap.cbegin(), e = _colorsMap.cend(); i != e; ++i) {
			delete i.value();
		}
		_colorsMap.clear();
	}

};
