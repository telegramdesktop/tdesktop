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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

// text preprocess
QString textClean(const QString &text);
QString textRichPrepare(const QString &text);
QString textOneLine(const QString &text, bool trim = true, bool rich = false);
QString textAccentFold(const QString &text);
QString textSearchKey(const QString &text);
bool textSplit(QString &sendingText, QString &leftText, int32 limit);

enum {
	TextParseMultiline = 0x001,
	TextParseLinks = 0x002,
	TextParseRichText = 0x004,
	TextParseMentions = 0x008,
	TextParseHashtags = 0x010,
	TextParseBotCommands = 0x020,

	TextTwitterMentions = 0x040,
	TextTwitterHashtags = 0x080,
	TextInstagramMentions = 0x100,
	TextInstagramHashtags = 0x200,
};

enum LinkInTextType {
	LinkInTextUrl,
	LinkInTextCustomUrl,
	LinkInTextEmail,
	LinkInTextHashtag,
	LinkInTextMention,
	LinkInTextBotCommand,
};
struct LinkInText {
	LinkInText(LinkInTextType type, int32 offset, int32 length, const QString &text = QString()) : type(type), offset(offset), length(length), text(text) {
	}
	LinkInTextType type;
	int32 offset, length;
	QString text;
};
typedef QList<LinkInText> LinksInText;
inline LinksInText linksFromMTP(const QVector<MTPMessageEntity> &entities) {
	LinksInText result;
	if (!entities.isEmpty()) {
		result.reserve(entities.size());
		for (int32 i = 0, l = entities.size(); i != l; ++i) {
			const MTPMessageEntity &e(entities.at(i));
			switch (e.type()) {
			case mtpc_messageEntityUrl: { const MTPDmessageEntityUrl &d(e.c_messageEntityUrl()); result.push_back(LinkInText(LinkInTextUrl, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityTextUrl: { const MTPDmessageEntityTextUrl &d(e.c_messageEntityTextUrl()); result.push_back(LinkInText(LinkInTextCustomUrl, d.voffset.v, d.vlength.v, textClean(qs(d.vurl)))); } break;
			case mtpc_messageEntityEmail: { const MTPDmessageEntityEmail &d(e.c_messageEntityEmail()); result.push_back(LinkInText(LinkInTextEmail, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityHashtag: { const MTPDmessageEntityHashtag &d(e.c_messageEntityHashtag()); result.push_back(LinkInText(LinkInTextHashtag, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityMention: { const MTPDmessageEntityMention &d(e.c_messageEntityMention()); result.push_back(LinkInText(LinkInTextMention, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityBotCommand: { const MTPDmessageEntityBotCommand &d(e.c_messageEntityBotCommand()); result.push_back(LinkInText(LinkInTextBotCommand, d.voffset.v, d.vlength.v)); } break;
			}
		}
	}
	return result;
}
inline MTPVector<MTPMessageEntity> linksToMTP(const LinksInText &links) {
	MTPVector<MTPMessageEntity> result(MTP_vector<MTPMessageEntity>(0));
	QVector<MTPMessageEntity> &v(result._vector().v);
	for (int32 i = 0, s = links.size(); i != s; ++i) {
		const LinkInText &l(links.at(i));
		switch (l.type) {
		case LinkInTextUrl: v.push_back(MTP_messageEntityUrl(MTP_int(l.offset), MTP_int(l.length))); break;
		case LinkInTextCustomUrl: v.push_back(MTP_messageEntityTextUrl(MTP_int(l.offset), MTP_int(l.length), MTP_string(l.text))); break;
		case LinkInTextEmail: v.push_back(MTP_messageEntityEmail(MTP_int(l.offset), MTP_int(l.length))); break;
		case LinkInTextHashtag: v.push_back(MTP_messageEntityHashtag(MTP_int(l.offset), MTP_int(l.length))); break;
		case LinkInTextMention: v.push_back(MTP_messageEntityMention(MTP_int(l.offset), MTP_int(l.length))); break;
		case LinkInTextBotCommand: v.push_back(MTP_messageEntityBotCommand(MTP_int(l.offset), MTP_int(l.length))); break;
		}
	}
	return result;
}
LinksInText textParseLinks(const QString &text, int32 flags, bool rich = false);

#include "gui/emoji_config.h"

void emojiDraw(QPainter &p, EmojiPtr e, int x, int y);

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

	virtual ITextBlock *clone() const = 0;
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

	ITextBlock *clone() const {
		return new NewlineBlock(*this);
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

	ITextBlock *clone() const {
		return new TextBlock(*this);
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

	ITextBlock *clone() const {
		return new EmojiBlock(*this);
	}

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

	ITextBlock *clone() const {
		return new SkipBlock(*this);
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
	virtual const QLatin1String &type() const = 0;
	virtual ~ITextLink() {
	}

};

#define TEXT_LINK_CLASS(ClassName) public: \
const QLatin1String &type() const { \
	static const QLatin1String _type(qstr(#ClassName)); \
	return _type; \
}

typedef QSharedPointer<ITextLink> TextLinkPtr;

class TextLink : public ITextLink {
	TEXT_LINK_CLASS(TextLink)

public:

	TextLink(const QString &url, bool fullDisplayed = true) : _url(url), _fullDisplayed(fullDisplayed) {
		QUrl u(_url), good(u.isValid() ? u.toEncoded() : QString());
		_readable = good.isValid() ? good.toDisplayString() : _url;
	}

	const QString &text() const {
		return _url;
	}

	void onClick(Qt::MouseButton button) const;

	const QString &readable() const {
		return _readable;
	}

	bool fullDisplayed() const {
		return _fullDisplayed;
	}

	QString encoded() const {
		QUrl u(_url), good(u.isValid() ? u.toEncoded() : QString());
		QString result(good.isValid() ? QString::fromUtf8(good.toEncoded()) : _url);

		if (!QRegularExpression(qsl("^[a-zA-Z]+://")).match(result).hasMatch()) { // no protocol
			return qsl("http://") + result;
		}
		return result;
	}

private:

	QString _url, _readable;
	bool _fullDisplayed;

};

class CustomTextLink : public TextLink {
public:

	CustomTextLink(const QString &url) : TextLink(url, false) {
	}
	void onClick(Qt::MouseButton button) const;
};

class EmailLink : public ITextLink {
	TEXT_LINK_CLASS(EmailLink)

public:

	EmailLink(const QString &email) : _email(email) {
	}

	const QString &text() const {
		return _email;
	}

	void onClick(Qt::MouseButton button) const;

	const QString &readable() const {
		return _email;
	}

	QString encoded() const {
		return _email;
	}

private:

	QString _email;

};

class MentionLink : public ITextLink {
	TEXT_LINK_CLASS(MentionLink)

public:

	MentionLink(const QString &tag) : _tag(tag) {
	}

	const QString &text() const {
		return _tag;
	}

	void onClick(Qt::MouseButton button) const;

	const QString &readable() const {
		return _tag;
	}

	QString encoded() const {
		return _tag;
	}

private:

	QString _tag;

};

class HashtagLink : public ITextLink {
	TEXT_LINK_CLASS(HashtagLink)

public:

	HashtagLink(const QString &tag) : _tag(tag) {
	}

	const QString &text() const {
		return _tag;
	}

	void onClick(Qt::MouseButton button) const;

	const QString &readable() const {
		return _tag;
	}

	QString encoded() const {
		return _tag;
	}

private:

	QString _tag;

};

class BotCommandLink : public ITextLink {
	TEXT_LINK_CLASS(BotCommandLink)

public:

	BotCommandLink(const QString &cmd) : _cmd(cmd) {
	}

	const QString &text() const {
		return _cmd;
	}

	void onClick(Qt::MouseButton button) const;

	const QString &readable() const {
		return _cmd;
	}

	QString encoded() const {
		return _cmd;
	}

private:

	QString _cmd;

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

	TextCommandLangTag     = 0x20,
};

struct TextParseOptions {
	int32 flags;
	int32 maxw;
	int32 maxh;
	Qt::LayoutDirection dir;
};
extern const TextParseOptions _defaultOptions, _textPlainOptions;

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
	Text(const Text &other);
	Text &operator=(const Text &other);

	int32 countHeight(int32 width) const;
	void setText(style::font font, const QString &text, const TextParseOptions &options = _defaultOptions);
	void setRichText(style::font font, const QString &text, TextParseOptions options = _defaultOptions, const TextCustomTagsMap &custom = TextCustomTagsMap());
	void setMarkedText(style::font font, const QString &text, const LinksInText &links, const TextParseOptions &options = _defaultOptions);

	void setLink(uint16 lnkIndex, const TextLinkPtr &lnk);
	bool hasLinks() const;

	bool hasSkipBlock() const {
		return _blocks.isEmpty() ? false : _blocks.back()->type() == TextBlockSkip;
	}
	void setSkipBlock(int32 width, int32 height);
	void removeSkipBlock();
	LinksInText calcLinksInText() const;

	int32 maxWidth() const {
		return _maxWidth.ceil().toInt();
	}
	int32 minHeight() const {
		return _minHeight;
	}

	void replaceFont(style::font f); // does not recount anything, use at your own risk!

	void draw(QPainter &p, int32 left, int32 top, int32 width, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, uint16 selectedFrom = 0, uint16 selectedTo = 0) const;
	void drawElided(QPainter &p, int32 left, int32 top, int32 width, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0) const;
	void drawLeft(QPainter &p, int32 left, int32 top, int32 width, int32 outerw, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, uint16 selectedFrom = 0, uint16 selectedTo = 0) const {
		draw(p, rtl() ? (outerw - left - width) : left, top, width, align, yFrom, yTo, selectedFrom, selectedTo);
	}
	void drawLeftElided(QPainter &p, int32 left, int32 top, int32 width, int32 outerw, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0) const {
		drawElided(p, rtl() ? (outerw - left - width) : left, top, width, lines, align, yFrom, yTo, removeFromEnd);
	}
	void drawRight(QPainter &p, int32 right, int32 top, int32 width, int32 outerw, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, uint16 selectedFrom = 0, uint16 selectedTo = 0) const {
		draw(p, rtl() ? right : (outerw - right - width), top, width, align, yFrom, yTo, selectedFrom, selectedTo);
	}
	void drawRightElided(QPainter &p, int32 right, int32 top, int32 width, int32 outerw, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0) const {
		drawElided(p, rtl() ? right : (outerw - right - width), top, width, lines, align, yFrom, yTo, removeFromEnd);
	}

	const TextLinkPtr &link(int32 x, int32 y, int32 width, style::align align = style::al_left) const;
	const TextLinkPtr &linkLeft(int32 x, int32 y, int32 width, int32 outerw, style::align align = style::al_left) const {
		return link(rtl() ? (outerw - x - width) : x, y, width, align);
	}
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, int32 width, style::align align = style::al_left) const;
	void getStateLeft(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, int32 width, int32 outerw, style::align align = style::al_left) const {
		return getState(lnk, inText, rtl() ? (outerw - x - width) : x, y, width, align);
	}
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y, int32 width, style::align align = style::al_left) const;
	void getSymbolLeft(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y, int32 width, int32 outerw, style::align align = style::al_left) const {
		return getSymbol(symbol, after, upon, rtl() ? (outerw - x - width) : x, y, width, align);
	}
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType selectType) const;

	bool isEmpty() const {
		return _text.isEmpty();
	}
	bool isNull() const {
		return !_font;
	}
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

	void recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir = Qt::LayoutDirectionAuto);

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

void initLinkSets();
const QSet<int32> &validProtocols();
const QSet<int32> &validTopDomains();
const QRegularExpression &reDomain();
const QRegularExpression &reMailName();
const QRegularExpression &reMailStart();
const QRegularExpression &reHashtag();
const QRegularExpression &reBotCommand();

// text style
const style::textStyle *textstyleCurrent();
void textstyleSet(const style::textStyle *style);

inline void textstyleRestore() {
	textstyleSet(0);
}

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
const QChar *textSkipCommand(const QChar *from, const QChar *end, bool canLink = true);

inline bool chIsSpace(QChar ch, bool rich = false) {
	return ch.isSpace() || (ch < 32 && !(rich && ch == TextCommand)) || (ch == QChar::ParagraphSeparator) || (ch == QChar::LineSeparator) || (ch == QChar::ObjectReplacementCharacter) || (ch == QChar::SoftHyphen) || (ch == QChar::CarriageReturn) || (ch == QChar::Tabulation);
}
inline bool chIsBad(QChar ch) {
	return (ch == 0) || (ch >= 8232 && ch < 8237) || (ch >= 65024 && ch < 65040 && ch != 65039) || (ch >= 127 && ch < 160 && ch != 156);
}
inline bool chIsTrimmed(QChar ch, bool rich = false) {
	return (!rich || ch != TextCommand) && (chIsSpace(ch) || chIsBad(ch));
}
inline bool chIsDiac(QChar ch) { // diac and variation selectors
	QChar::Category c = ch.category();
	return (c == QChar::Mark_NonSpacing);
}
inline int32 chMaxDiacAfterSymbol() {
	return 2;
}
inline bool chIsNewline(QChar ch) {
	return (ch == QChar::LineFeed || ch == 156);
}
inline bool chIsLinkEnd(QChar ch) {
	return ch == TextCommand || chIsBad(ch) || chIsSpace(ch) || chIsNewline(ch) || ch.isLowSurrogate() || ch.isHighSurrogate();
}
inline bool chIsAlmostLinkEnd(QChar ch) {
	switch (ch.unicode()) {
	case '?':
	case ',':
	case '.':
	case '"':
	case ':':
	case '!':
	case '\'':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsWordSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::Space:
	case QChar::LineFeed:
	case '.':
	case ',':
	case '?':
	case '!':
	case '@':
	case '#':
	case '$':
	case ':':
	case ';':
	case '-':
	case '<':
	case '>':
	case '[':
	case ']':
	case '(':
	case ')':
	case '{':
	case '}':
	case '=':
	case '/':
	case '+':
	case '%':
	case '&':
	case '^':
	case '*':
	case '\'':
	case '"':
	case '`':
	case '~':
	case '|':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsSentenceEnd(QChar ch) {
	switch (ch.unicode()) {
	case '.':
	case '?':
	case '!':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsSentencePartEnd(QChar ch) {
	switch (ch.unicode()) {
	case ',':
	case ':':
	case ';':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsParagraphSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::LineFeed:
		return true;
	default:
		break;
	}
	return false;
}
