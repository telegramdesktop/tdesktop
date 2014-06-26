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

#include "gui/emoji_config.h"

#include "../../../QtStatic/qtbase/src/gui/text/qfontengine_p.h"

enum TextBlockType {
	TextBlockNewline = 0x01,
	TextBlockText    = 0x02,
	TextBlockEmoji   = 0x03,
	TextBlockSkip    = 0x04,
};

enum TextBlockFlags {
	TextBlockBold      = 0x01,
	TextBlockItalic    = 0x02,
	TextBlockUnderline = 0x04,
};

class ITextBlock {
public:

	ITextBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex) : _from(from), _flags((flags & 0xFF) | ((lnkIndex & 0xFFFF) << 12))/*, _color(color)*/, _lpadding(0) {
		if (length) {
			if (str.at(_from + length - 1).unicode() == QChar::Space) {
				_rpadding = font->spacew;
			}
			if (length > 1 && str.at(0).unicode() == QChar::Space) {
				_lpadding = font->spacew;
			}
		}
	}

	uint16 from() const {
		return _from;
	}
	int32 width() const {
		return _width.toInt();
	}
	int32 lpadding() const {
		return _lpadding.toInt();
	}
	int32 rpadding() const {
		return _rpadding.toInt();
	}
	QFixed f_width() const {
		return _width;
	}
	QFixed f_lpadding() const {
		return _lpadding;
	}
	QFixed f_rpadding() const {
		return _rpadding;
	}

	uint16 lnkIndex() const {
		return (_flags >> 12) & 0xFFFF;
	}
	void setLnkIndex(uint16 lnkIndex) {
		_flags = (_flags & ~(0xFFFF << 12)) | (lnkIndex << 12);
	}

	TextBlockType type() const {
		return TextBlockType((_flags >> 8) & 0x0F);
	}
	int32 flags() const {
		return (_flags & 0xFF);
	}
	const style::color &color() const {
		static style::color tmp;
		return tmp;//_color;
	}

	virtual ~ITextBlock() {
	}

protected:

	uint16 _from;

	uint32 _flags; // 4 bits empty, 16 bits lnkIndex, 4 bits type, 8 bits flags

	QFixed _width, _lpadding, _rpadding;

};

class NewlineBlock : public ITextBlock {
public:

	Qt::LayoutDirection nextDirection() const {
		return _nextDir;
	}

private:

	NewlineBlock(const style::font &font, const QString &str, uint16 from, uint16 length) : ITextBlock(font, str, from, length, 0, st::transparent, 0), _nextDir(Qt::LayoutDirectionAuto) {
		_flags |= ((TextBlockNewline & 0x0F) << 8);
	}

	Qt::LayoutDirection _nextDir;

	friend class Text;
	friend class TextParser;

	friend class TextPainter;
};

struct TextWord {
	TextWord() {
	}
	TextWord(uint16 from, QFixed width, QFixed rbearing, QFixed rpadding = 0) : from(from),
		_rbearing(rbearing.value() > 0x7FFF ? 0x7FFF : (rbearing.value() < -0x7FFF ? -0x7FFF : rbearing.value())), width(width), rpadding(rpadding) {
	}
	QFixed f_rbearing() const {
		return QFixed::fromFixed(_rbearing);
	}
	uint16 from;
	int16 _rbearing;
	QFixed width, rpadding;
};

class TextBlock : public ITextBlock {
public:

	QFixed f_rbearing() const {
		return _words.isEmpty() ? 0 : _words.back().f_rbearing();
	}

private:

	TextBlock(const style::font &font, const QString &str, QFixed minResizeWidth, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex);

	typedef QVector<TextWord> TextWords;
	TextWords _words;

	friend class Text;
	friend class TextParser;

	friend class BlockParser;
	friend class TextPainter;
};

class EmojiBlock : public ITextBlock {
public:

private:

	EmojiBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex, const EmojiData *emoji);

	const EmojiData *emoji;

	friend class Text;
	friend class TextParser;

	friend class TextPainter;
};

class SkipBlock : public ITextBlock {
public:

	int32 height() const {
		return _height;
	}

private:

	SkipBlock(const style::font &font, const QString &str, uint16 from, int32 w, int32 h, uint16 lnkIndex);

	int32 _height;

	friend class Text;
	friend class TextParser;

	friend class TextPainter;
};

class ITextLink {
public:

	virtual void onClick(Qt::MouseButton) const = 0;
	virtual const QString &text() const {
		static const QString _tmp;
		return _tmp;
	}
	virtual const QString &readable() const {
		static const QString _tmp;
		return _tmp;
	}
	virtual bool fullDisplayed() const {
		return true;
	}
	virtual QString encoded() const {
		return QString();
	}
	virtual ~ITextLink() {
	}

};
typedef QSharedPointer<ITextLink> TextLinkPtr;

class TextLink : public ITextLink {
public:

	TextLink(const QString &url, bool fullDisplayed = true) : _url(url), _fullDisplayed(fullDisplayed) {
		QUrl u(_url), good(u.isValid() ? u.toEncoded() : QString());
		_readable = good.isValid() ? good.toDisplayString() : _url;
	}

	const QString &text() const {
		return _url;
	}

	void onClick(Qt::MouseButton button) const {
		if (button == Qt::LeftButton || button == Qt::MiddleButton) {
			QDesktopServices::openUrl(TextLink::encoded());
		}
	}

	const QString &readable() const {
		return _readable;
	}

	bool fullDisplayed() const {
		return _fullDisplayed;
	}

	QString encoded() const {
		QUrl u(_url), good(u.isValid() ? u.toEncoded() : QString());
		QString result(good.isValid() ? good.toEncoded() : _url);

		if (!QRegularExpression(qsl("^[a-zA-Z]+://")).match(result).hasMatch()) { // no protocol
			return qsl("http://") + result;
		}
		return result;
	}

private:

	QString _url, _readable;
	bool _fullDisplayed;

};

class EmailLink : public ITextLink {
public:

	EmailLink(const QString &email) : _email(email) {
	}

	const QString &text() const {
		return _email;
	}

	void onClick(Qt::MouseButton button) const {
		if (button == Qt::LeftButton || button == Qt::MiddleButton) {
			QDesktopServices::openUrl(qsl("mailto:") + _email);
		}
	}

	const QString &readable() const {
		return _email;
	}

	QString encoded() const {
		return _email;
	}

private:

	QString _email;

};

static const QChar TextCommand(0x0010);
enum TextCommands {
	TextCommandBold        = 0x01,
	TextCommandNoBold      = 0x02,
	TextCommandItalic      = 0x03,
	TextCommandNoItalic    = 0x04,
	TextCommandUnderline   = 0x05,
	TextCommandNoUnderline = 0x06,
	TextCommandLinkIndex   = 0x07, // 0 - NoLink
	TextCommandLinkText    = 0x08,
	TextCommandColor       = 0x09,
	TextCommandNoColor     = 0x0A,
	TextCommandSkipBlock   = 0x0B,
};

enum {
	TextParseMultiline = 0x01,
	TextParseLinks     = 0x02,
	TextParseRichText  = 0x04,
};

struct TextParseOptions {
	int32 flags;
	int32 maxw;
	int32 maxh;
	Qt::LayoutDirection dir;
};
extern const TextParseOptions _defaultOptions;
extern const TextParseOptions _textPlainOptions;

enum TextSelectType {
	TextSelectLetters    = 0x01,
	TextSelectWords      = 0x02,
	TextSelectParagraphs = 0x03,
};

typedef QPair<QString, QString> TextCustomTag; // open str and close str
typedef QMap<QChar, TextCustomTag> TextCustomTagsMap;

class Text {
public:

	Text(int32 minResizeWidth = QFIXED_MAX);
	Text(style::font font, const QString &text, const TextParseOptions &options = _defaultOptions, int32 minResizeWidth = QFIXED_MAX, bool richText = false);

	int32 countHeight(int32 width) const;
	void setText(style::font font, const QString &text, const TextParseOptions &options = _defaultOptions);
	void setRichText(style::font font, const QString &text, TextParseOptions options = _defaultOptions, const TextCustomTagsMap &custom = TextCustomTagsMap());

	void setLink(uint16 lnkIndex, const TextLinkPtr &lnk);
	bool hasLinks() const;

	int32 maxWidth() const {
		return _maxWidth.ceil().toInt();
	}
	int32 minHeight() const {
		return _minHeight;
	}

	void draw(QPainter &p, int32 left, int32 top, int32 width, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, uint16 selectedFrom = 0, uint16 selectedTo = 0) const;
	void drawElided(QPainter &p, int32 left, int32 top, int32 width, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1) const;

	const TextLinkPtr &link(int32 x, int32 y, int32 width, style::align align = style::al_left) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, int32 width, style::align align = style::al_left) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y, int32 width, style::align align = style::al_left) const;
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType selectType) const;

	QString original(uint16 selectedFrom = 0, uint16 selectedTo = 0xFFFF, bool expandLinks = true) const;

	bool lastDots(int32 dots, int32 maxdots = 3) { // hack for typing animation
		if (_text.size() < maxdots) return false;

		int32 nowDots = 0, from = _text.size() - maxdots, to = _text.size();
		for (int32 i = from; i < to; ++i) {
			if (_text.at(i) == QChar('.')) {
				++nowDots;
			}
		}
		if (nowDots == dots) return false;
		for (int32 j = from; j < from + dots; ++j) {
			_text[j] = QChar('.');
		}
		for (int32 j = from + dots; j < to; ++j) {
			_text[j] = QChar(' ');
		}
		return true;
	}

	void clean();
	~Text() {
		clean();
	}

private:

	QFixed _minResizeWidth, _maxWidth;
	int32 _minHeight;

	QString _text;
	style::font _font;

	typedef QVector<ITextBlock*> TextBlocks;
	TextBlocks _blocks;

	typedef QVector<TextLinkPtr> TextLinks;
	TextLinks _links;

	Qt::LayoutDirection _startDir;

	friend class TextParser;
	friend class TextPainter;

};

// text style
const style::textStyle *textstyleCurrent();
void textstyleSet(const style::textStyle *style);

inline void textstyleRestore() {
	textstyleSet(0);
}

// text preprocess
QString textClean(const QString &text);
QString textRichPrepare(const QString &text);
QString textOneLine(const QString &text, bool trim = true, bool rich = false);
QString textAccentFold(const QString &text);

// textlnk
void textlnkOver(const TextLinkPtr &lnk);
const TextLinkPtr &textlnkOver();

void textlnkDown(const TextLinkPtr &lnk);
const TextLinkPtr &textlnkDown();

// textcmd
QString textcmdSkipBlock(ushort w, ushort h);
QString textcmdStartLink(ushort lnkIndex);
QString textcmdStartLink(const QString &url);
QString textcmdStopLink();
QString textcmdLink(ushort lnkIndex, const QString &text);
QString textcmdLink(const QString &url, const QString &text);
QString textcmdStartColor(const style::color &color);
QString textcmdStopColor();
