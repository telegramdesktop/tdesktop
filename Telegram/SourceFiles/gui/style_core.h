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
#pragma once

#include "gui/animation.h"

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QCursor>
#include <QtGui/QFont>

namespace style {
	
	class FontData;
	class Font {
	public:
		Font(Qt::Initialization = Qt::Uninitialized) : ptr(0) {
		}
		Font(uint32 size, uint32 flags, const QString &family);
		Font(uint32 size, uint32 flags = 0, uint32 family = 0);

		Font &operator=(const Font &other) {
			ptr = other.ptr;
			return (*this);
		}

		FontData *operator->() const {
			return ptr;
		}
		FontData *v() const {
			return ptr;
		}

		operator bool() const {
			return !!ptr;
		}

	private:
		FontData *ptr;

		void init(uint32 size, uint32 flags, uint32 family, Font *modified);
		friend void startManager();

		Font(FontData *p) : ptr(p) {
		}
		Font(uint32 size, uint32 flags, uint32 family, Font *modified);
		friend class FontData;

	};

	enum FontFlagBits {
		FontBoldBit,
		FontItalicBit,
		FontUnderlineBit,

		FontFlagsBits
	};

	enum FontFlags {
		FontBold = (1 << FontBoldBit),
		FontItalic = (1 << FontItalicBit),
		FontUnderline = (1 << FontUnderlineBit),

		FontDifferentFlags = (1 << FontFlagsBits)
	};

	inline uint32 _fontKey(uint32 size, uint32 flags, uint32 family) {
		return (((family << 10) | size) << FontFlagsBits) | flags;
	}

	class FontData {
	public:

		int32 width(const QString &str, int32 from, int32 to) {
			return m.width(str.mid(from, to));
		}

		Font bold(bool set = true) const;
		Font italic(bool set = true) const;
		Font underline(bool set = true) const;

		uint32 flags() const;

		QFont f;
		QFontMetrics m;
		int32 height, ascent, descent, spacew, elidew;

	private:
		mutable Font modified[FontDifferentFlags];

		Font otherFlagsFont(uint32 flag, bool set) const;
		FontData(uint32 size, uint32 flags, uint32 family, Font *other);

		friend class Font;
		uint32 _size, _flags, _family;

	};

inline bool operator==(const Font &a, const Font &b) {
	return a.v() == b.v();
}
inline bool operator!=(const Font &a, const Font &b) {
	return a.v() != b.v();
}

	class ColorData;
	class Color {
	public:
		Color(Qt::Initialization = Qt::Uninitialized) : ptr(0), owner(false) {
		}
		Color(const Color &c);
		Color(const QColor &c);
		Color(uchar r, uchar g, uchar b, uchar a = 255);
		Color &operator=(const Color &c);
		~Color();

		void set(const QColor &newv);
		void set(uchar r, uchar g, uchar b, uchar a = 255);

		ColorData *operator->() const {
			return ptr;
		}
		ColorData *v() const {
			return ptr;
		}

		operator bool() const {
			return !!ptr;
		}

	private:
		ColorData *ptr;
		bool owner;

		void init(uchar r, uchar g, uchar b, uchar a);
		
		friend void startManager();

		Color(ColorData *p) : ptr(p) {
		}
		friend class ColorData;

	};

	inline uint32 _colorKey(uchar r, uchar g, uchar b, uchar a) {
		return (((((uint32(r) << 8) | uint32(g)) << 8) | uint32(b)) << 8) | uint32(a);
	}

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


	typedef QVector<QString> FontFamilies;
	extern FontFamilies _fontFamilies;

	typedef QMap<uint32, FontData*> FontDatas;
	extern FontDatas _fontsMap;

	typedef QMap<uint32, ColorData*> ColorDatas;
	extern ColorDatas _colorsMap;

	typedef float64 number;
	typedef QString string;
	typedef QRect rect;
    class sprite : public rect {
    public:
        sprite() {
        }
        sprite(int left, int top, int width, int height) : rect(left, top, width, height) {
        }
        inline int pxWidth() const {
            return rect::width() / cIntRetinaFactor();
        }
        inline int pxHeight() const {
            return rect::height() / cIntRetinaFactor();
        }
        inline QSize pxSize() const {
            return rect::size() / cIntRetinaFactor();
        }
    private:
        inline int width() const {
            return rect::width();
        }
        inline int height() const {
            return rect::height();
        }
        inline QSize size() const {
            return rect::size();
        }
    };
	typedef QPoint point;
	typedef QSize size;
	typedef anim::transition transition;

	typedef Qt::CursorShape cursor;
	static const cursor cur_default(Qt::ArrowCursor);
	static const cursor cur_pointer(Qt::PointingHandCursor);
	static const cursor cur_text(Qt::IBeamCursor);
	static const cursor cur_cross(Qt::CrossCursor);
    static const cursor cur_sizever(Qt::SizeVerCursor);
    static const cursor cur_sizehor(Qt::SizeHorCursor);
    static const cursor cur_sizebdiag(Qt::SizeBDiagCursor);
    static const cursor cur_sizefdiag(Qt::SizeFDiagCursor);
    static const cursor cur_sizeall(Qt::SizeAllCursor);

	typedef Qt::Alignment align;
	static const align al_topleft(Qt::AlignTop | Qt::AlignLeft);
	static const align al_top(Qt::AlignTop | Qt::AlignHCenter);
	static const align al_topright(Qt::AlignTop | Qt::AlignRight);
	static const align al_right(Qt::AlignVCenter | Qt::AlignRight);
	static const align al_bottomright(Qt::AlignBottom | Qt::AlignRight);
	static const align al_bottom(Qt::AlignBottom | Qt::AlignHCenter);
	static const align al_bottomleft(Qt::AlignBottom | Qt::AlignLeft);
	static const align al_left(Qt::AlignVCenter | Qt::AlignLeft);
	static const align al_center(Qt::AlignVCenter | Qt::AlignHCenter);

	typedef QMargins margins;
	typedef Font font;
	typedef Color color;

	void startManager();
	void stopManager();

};
