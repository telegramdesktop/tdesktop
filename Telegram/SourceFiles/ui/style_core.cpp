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

namespace style {
namespace {
using ModulesList = QList<internal::ModuleBase*>;
NeverFreedPointer<ModulesList> styleModules;

typedef QMap<QString, int> FontFamilyMap;
FontFamilyMap fontFamilyMap;

typedef QVector<QString> FontFamilies;
FontFamilies _fontFamilies;

typedef QMap<uint32, FontData*> FontDatas;
FontDatas fontsMap;

typedef QMap<uint32, ColorData*> ColorDatas;
ColorDatas colorsMap;

int spriteWidthValue = 0;
QPixmap *spriteData = nullptr;

inline uint32 fontKey(int size, uint32 flags, int family) {
	return (((uint32(family) << 10) | uint32(size)) << 3) | flags;
}

} // namespace

namespace internal {

void registerModule(ModuleBase *module) {
	styleModules.makeIfNull();
	styleModules->push_back(module);
}

void unregisterModule(ModuleBase *module) {
	styleModules->removeOne(module);
	if (styleModules->isEmpty()) {
		styleModules.clear();
	}
}

int registerFontFamily(const QString &family) {
	auto result = fontFamilyMap.value(family, -1);
	if (result < 0) {
		result = _fontFamilies.size();
		fontFamilyMap.insert(family, result);
		_fontFamilies.push_back(family);
	}
	return result;
}

int spriteWidth() {
	return spriteWidthValue;
}

} // namespace internal

	FontData::FontData(int size, uint32 flags, int family, Font *other) : f(_fontFamilies[family]), m(f), _size(size), _flags(flags), _family(family) {
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
		spacew = width(QLatin1Char(' '));
		elidew = width(QLatin1Char('.')) * 3;
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

	int FontData::size() const {
		return _size;
	}

	uint32 FontData::flags() const {
		return _flags;
	}

	int FontData::family() const {
		return _family;
	}

	Font FontData::otherFlagsFont(uint32 flag, bool set) const {
		int32 newFlags = set ? (_flags | flag) : (_flags & ~flag);
		if (!modified[newFlags].v()) {
			modified[newFlags] = Font(_size, newFlags, _family, modified);
		}
		return modified[newFlags];
	}

	Font::Font(int size, uint32 flags, const QString &family) {
		if (fontFamilyMap.isEmpty()) {
			for (uint32 i = 0, s = style::_fontFamilies.size(); i != s; ++i) {
				fontFamilyMap.insert(style::_fontFamilies.at(i), i);
			}
		}

		auto i = fontFamilyMap.constFind(family);
		if (i == fontFamilyMap.cend()) {
			style::_fontFamilies.push_back(family);
			i = fontFamilyMap.insert(family, style::_fontFamilies.size() - 1);
		}
		init(size, flags, i.value(), 0);
	}

	Font::Font(int size, uint32 flags, int family) {
		init(size, flags, family, 0);
	}

	Font::Font(int size, uint32 flags, int family, Font *modified) {
		init(size, flags, family, modified);
	}

	void Font::init(int size, uint32 flags, int family, Font *modified) {
		uint32 key = fontKey(size, flags, family);
		auto i = fontsMap.constFind(key);
		if (i == fontsMap.cend()) {
			i = fontsMap.insert(key, new FontData(size, flags, family, modified));
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

	namespace {
	inline uint32 colorKey(uchar r, uchar g, uchar b, uchar a) {
		return (((((uint32(r) << 8) | uint32(g)) << 8) | uint32(b)) << 8) | uint32(a);
	}
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

	void startManager() {
		if (cRetina()) {
			cSetRealScale(dbisOne);
		}

		internal::registerFontFamily(qsl("Open Sans"));
		QString spriteFilePostfix;
		if (cRetina() || cScale() == dbisTwo) {
			spriteFilePostfix = qsl("_200x");
		} else if (cScale() == dbisOneAndQuarter) {
			spriteFilePostfix = qsl("_125x");
		} else if (cScale() == dbisOneAndHalf) {
			spriteFilePostfix = qsl("_150x");
		}
		QString spriteFile = qsl(":/gui/art/sprite") + spriteFilePostfix + qsl(".png");
		if (rtl()) {
			spriteData = new QPixmap(QPixmap::fromImage(QImage(spriteFile).mirrored(true, false)));
		} else {
			spriteData = new QPixmap(spriteFile);
		}
		if (cRetina()) spriteData->setDevicePixelRatio(cRetinaFactor());
		spriteWidthValue = spriteData->width();

		if (styleModules) {
			for_const (auto module, *styleModules) {
				module->start();
			}
		}
		_fontFamilies.push_back(qsl("Open Sans"));
	}

	void stopManager() {
		if (styleModules) {
			for_const (auto module, *styleModules) {
				module->stop();
			}
		}

		for (auto fontData : fontsMap) {
			delete fontData;
		}
		fontsMap.clear();

		for (auto colorData : colorsMap) {
			delete colorData;
		}
		colorsMap.clear();
	}

	const QPixmap &spritePixmap() {
		return *spriteData;
	}

};
