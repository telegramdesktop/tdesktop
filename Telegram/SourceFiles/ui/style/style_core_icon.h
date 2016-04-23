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

#include "ui/style/style_core_color.h"

namespace style {
namespace internal {

class IconMask {
public:
	template <int N>
	IconMask(const uchar (&data)[N]) : _data(data), _size(N) {
		static_assert(N > 0, "invalid image data");
	}

	const uchar *data() const {
		return _data;
	}
	int size() const {
		return _size;
	}

private:
	const uchar *_data;
	const int _size;

};

class MonoIcon {
public:
	MonoIcon() = default;
	MonoIcon(const IconMask *mask, const Color &color, QPoint offset);

	int width() const;
	int height() const;

	QPoint offset() const;
	void paint(QPainter &p, const QPoint &pos, int outerw) const;

	MonoIcon clone(const Color &color) const {
		return MonoIcon(_mask, color ? color : _color, _offset, OwningPixmapTag());
	}

	~MonoIcon() {
	}

private:
	struct OwningPixmapTag {
	};
	MonoIcon(const IconMask *mask, const Color &color, QPoint offset, OwningPixmapTag);
	void ensureLoaded() const;

	const IconMask *_mask = nullptr;
	Color _color;
	QPoint _offset = { 0, 0 };
	mutable QPixmap _pixmap; // for pixmaps
	mutable QSize _size; // for rects
	bool _owningPixmap = false;

};

class Icon {
	struct ColoredCopy;

public:
	Icon(Qt::Initialization) {
	}
	Icon(const ColoredCopy &makeCopy) {
		_parts.reserve(makeCopy.copyFrom._parts.size());
		auto colorIt = makeCopy.colors.cbegin(), colorsEnd = makeCopy.colors.cend();
		for_const (const auto &part, makeCopy.copyFrom._parts) {
			const auto &newPart = part.clone((colorIt == colorsEnd) ? Color(Qt::Uninitialized) : *(colorIt++));
			_parts.push_back(newPart);
		}
	}

	template <typename ... MonoIcons>
	Icon(const MonoIcons&... icons) {
		_parts.reserve(sizeof...(MonoIcons));
		addIcons(icons...);
	}

	std_::unique_ptr<Icon> clone(const QVector<Color> &colors) {
		return std_::make_unique<Icon>(ColoredCopy { *this, colors });
	}

	void paint(QPainter &p, const QPoint &pos, int outerw) const;
	int width() const;
	int height() const;

private:
	struct ColoredCopy {
		const Icon &copyFrom;
		const QVector<Color> &colors;
	};

	template <typename ... MonoIcons>
	void addIcons() {
	}
	template <typename ... MonoIcons>
	void addIcons(const MonoIcon &icon, const MonoIcons&... icons) {
		_parts.push_back(icon);
		addIcons(icons...);
	}

	QVector<MonoIcon> _parts;
	mutable int _width = -1;
	mutable int _height = -1;

};

} // namespace internal
} // namespace style
