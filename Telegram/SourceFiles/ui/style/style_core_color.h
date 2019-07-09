/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {

class palette;

namespace internal {

class Color;
class ColorData {
public:
	QColor c;
	QPen p;
	QBrush b;

	QColor transparent() const {
		return QColor(c.red(), c.green(), c.blue(), 0);
	}

private:
	ColorData(uchar r, uchar g, uchar b, uchar a);
	ColorData(const ColorData &other) = default;
	ColorData &operator=(const ColorData &other) = default;

	void set(uchar r, uchar g, uchar b, uchar a);

	friend class Color;
	friend class style::palette;

};

class Color {
public:
	Color(Qt::Initialization = Qt::Uninitialized) {
	}
	Color(const Color &other) = default;
	Color &operator=(const Color &other) = default;

	void set(uchar r, uchar g, uchar b, uchar a) const {
		_data->set(r, g, b, a);
	}

	operator const QBrush &() const {
		return _data->b;
	}

	operator const QPen &() const {
		return _data->p;
	}

	ColorData *operator->() const {
		return _data;
	}
	ColorData *v() const {
		return _data;
	}

	explicit operator bool() const {
		return !!_data;
	}

	class Proxy;
	Proxy operator[](const style::palette &paletteOverride) const;

private:
	friend class style::palette;
	Color(ColorData *data) : _data(data) {
	}

	ColorData *_data = nullptr;

};

class Color::Proxy {
public:
	Proxy(Color color) : _color(color) {
	}
	Proxy(const Proxy &other) = default;

	operator const QBrush &() const { return _color; }
	operator const QPen &() const { return _color; }
	ColorData *operator->() const { return _color.v(); }
	ColorData *v() const { return _color.v(); }
	explicit operator bool() const { return _color ? true : false; }
	Color clone() const { return _color; }

private:
	Color _color;

};

inline bool operator==(Color a, Color b) {
	return a->c == b->c;
}

inline bool operator!=(Color a, Color b) {
	return a->c != b->c;
}

} // namespace internal
} // namespace style
