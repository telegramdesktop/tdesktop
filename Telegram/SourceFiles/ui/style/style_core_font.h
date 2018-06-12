/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
namespace internal {

void destroyFonts();
int registerFontFamily(const QString &family);

class FontData;
class Font {
public:
	Font(Qt::Initialization = Qt::Uninitialized) : ptr(0) {
	}
	Font(int size, uint32 flags, const QString &family);
	Font(int size, uint32 flags, int family);

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

	operator const QFont &() const;

private:
	FontData *ptr;

	void init(int size, uint32 flags, int family, Font *modified);
	friend void startManager();

	Font(FontData *p) : ptr(p) {
	}
	Font(int size, uint32 flags, int family, Font *modified);
	friend class FontData;

};

enum FontFlags {
	FontBold = 0x01,
	FontItalic = 0x02,
	FontUnderline = 0x04,

	FontDifferentFlags = 0x08,
};

class FontData {
public:

	int32 width(const QString &str) const {
		return m.width(str);
	}
	int32 width(const QString &str, int32 from, int32 to) const {
		return width(str.mid(from, to));
	}
	int32 width(QChar ch) const {
		return m.width(ch);
	}
	QString elided(const QString &str, int32 width, Qt::TextElideMode mode = Qt::ElideRight) const {
		return m.elidedText(str, mode, width);
	}

	Font bold(bool set = true) const;
	Font italic(bool set = true) const;
	Font underline(bool set = true) const;

	int size() const;
	uint32 flags() const;
	int family() const;

	QFont f;
	QFontMetrics m;
	int32 height, ascent, descent, spacew, elidew;

private:
	mutable Font modified[FontDifferentFlags];

	Font otherFlagsFont(uint32 flag, bool set) const;
	FontData(int size, uint32 flags, int family, Font *other);

	friend class Font;
	int _size;
	uint32 _flags;
	int _family;

};

inline bool operator==(const Font &a, const Font &b) {
	return a.v() == b.v();
}
inline bool operator!=(const Font &a, const Font &b) {
	return a.v() != b.v();
}

inline Font::operator const QFont &() const {
	return ptr->f;
}

} // namespace internal
} // namespace style
