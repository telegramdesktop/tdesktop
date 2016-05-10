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
namespace internal {

void destroyColors();

class ColorData;
class Color {
public:
	Color(Qt::Initialization = Qt::Uninitialized) {
	}
	Color(const Color &c);
	explicit Color(const QColor &c);
	Color(uchar r, uchar g, uchar b, uchar a = 255);
	Color &operator=(const Color &c);
	~Color();

	void set(const QColor &newv);
	void set(uchar r, uchar g, uchar b, uchar a = 255);

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
	ColorData *ptr = nullptr;
	bool owner = false;

	void init(uchar r, uchar g, uchar b, uchar a);

	friend void startManager();

	Color(ColorData *p) : ptr(p) {
	}
	friend class ColorData;

};

class ColorData {
public:

	QColor c;
	QPen p;
	QBrush b;

private:

	ColorData(uchar r, uchar g, uchar b, uchar a);
	void set(const QColor &c);

	friend class Color;

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

inline QColor interpolate(const style::internal::Color &a, const style::internal::Color &b, float64 opacity_b) {
	QColor result;
	result.setRedF((a->c.redF() * (1. - opacity_b)) + (b->c.redF() * opacity_b));
	result.setGreenF((a->c.greenF() * (1. - opacity_b)) + (b->c.greenF() * opacity_b));
	result.setBlueF((a->c.blueF() * (1. - opacity_b)) + (b->c.blueF() * opacity_b));
	return result;
}

} // namespace style
