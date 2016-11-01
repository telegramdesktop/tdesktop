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

namespace style {

class palette;

namespace internal {

void destroyColors();

class ColorData;
class Color {
public:
	Color(Qt::Initialization = Qt::Uninitialized) {
	}
	Color(const Color &c);
	Color(uchar r, uchar g, uchar b, uchar a);

	void set(uchar r, uchar g, uchar b, uchar a) const;

	operator const QBrush &() const;
	operator const QPen &() const;

	ColorData *operator->() const {
		return ptr;
	}
	ColorData *v() const {
		return ptr;
	}

	explicit operator bool() const {
		return !!ptr;
	}

private:
	Color(ColorData *data);
	void init(uchar r, uchar g, uchar b, uchar a);

	ColorData *ptr = nullptr;

	friend class style::palette;

};

class ColorData {
public:
	QColor c;
	QPen p;
	QBrush b;

	QColor transparent() const {
		return QColor(c.red(), c.green(), c.blue(), 0);
	}

private:
	ColorData();
	ColorData(uchar r, uchar g, uchar b, uchar a);
	void set(uchar r, uchar g, uchar b, uchar a);

	friend class Color;
	friend class style::palette;

};

inline bool operator==(const Color &a, const Color &b) {
	return a->c == b->c;
}

inline bool operator!=(const Color &a, const Color &b) {
	return a->c != b->c;
}

inline Color::operator const QBrush &() const {
	return ptr->b;
}
inline Color::operator const QPen &() const {
	return ptr->p;
}

} // namespace internal

inline QColor interpolate(QColor a, QColor b, float64 opacity_b) {
	auto bOpacity = static_cast<int>(opacity_b * 255), aOpacity = (255 - bOpacity);
	return {
		(a.red() * aOpacity + b.red() * bOpacity + 1) >> 8,
		(a.green() * aOpacity + b.green() * bOpacity + 1) >> 8,
		(a.blue() * aOpacity + b.blue() * bOpacity + 1) >> 8,
		(a.alpha() * aOpacity + b.alpha() * bOpacity + 1) >> 8
	};
}

inline QColor interpolate(const style::internal::Color &a, QColor b, float64 opacity_b) {
	return interpolate(a->c, b, opacity_b);
}

inline QColor interpolate(QColor a, const style::internal::Color &b, float64 opacity_b) {
	return interpolate(a, b->c, opacity_b);
}

inline QColor interpolate(const style::internal::Color &a, const style::internal::Color &b, float64 opacity_b) {
	return interpolate(a->c, b->c, opacity_b);
}

} // namespace style
