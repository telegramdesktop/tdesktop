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
#include "text.h"

#include "lang.h"

#include <private/qharfbuzz_p.h>

namespace {

	inline bool chIsSpace(QChar ch, bool rich = false) {
		return ch.isSpace() || (ch < 32 && !(rich && ch == TextCommand)) || (ch == QChar::ParagraphSeparator) || (ch == QChar::LineSeparator) || (ch == QChar::ObjectReplacementCharacter) || (ch == QChar::SoftHyphen) || (ch == QChar::CarriageReturn) || (ch == QChar::Tabulation);
	}
	inline bool chIsBad(QChar ch) {
		return (ch == 0) || (ch >= 8232 && ch < 8239) || (ch >= 65024 && ch < 65040) || (ch >= 127 && ch < 160 && ch != 156);
	}
	inline bool chIsTrimmed(QChar ch, bool rich = false) {
		return (!rich || ch != TextCommand) && (chIsSpace(ch) || chIsBad(ch));
	}
	inline bool chIsDiac(QChar ch) { // diac and variation selectors
		return (ch >= 768 && ch < 880) || (ch >= 7616 && ch < 7680) || (ch >= 8400 && ch < 8448) || (ch >= 65056 && ch < 65072);
	}
	inline int32 chMaxDiacAfterSymbol() {
		return 4;
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
	inline bool chIsParagraphSeparator(QChar ch) {
		switch (ch.unicode()) {
		case QChar::LineFeed:
			return true;
		default:
			break;
		}
		return false;
	}

	const QRegularExpression reDomain(QString::fromUtf8("(?<![A-Za-z\\$0-9А-Яа-яёЁ\\-\\_%=])(?:([a-zA-Z]+)://)?((?:[A-Za-zА-яА-ЯёЁ0-9\\-\\_]+\\.){1,5}([A-Za-zрф\\-\\d]{2,22}))"));
	const QRegularExpression reMailName(QString::fromUtf8("[a-zA-Z\\-_\\.0-9]{1,256}$"));
	const QRegularExpression reMailStart(QString::fromUtf8("^[a-zA-Z\\-_\\.0-9]{1,256}\\@"));
	QSet<int32> validProtocols, validTopDomains;
	void initLinkSets();

	const style::textStyle *_textStyle = 0;

	TextLinkPtr _overLnk, _downLnk, _zeroLnk;

	void _initDefault() {
		_textStyle = &st::defaultTextStyle;
	}

	inline int32 _blockHeight(const ITextBlock *b, const style::font &font) {
		return (b->type() == TextBlockSkip) ? static_cast<const SkipBlock*>(b)->height() : (_textStyle->lineHeight > font->height) ? _textStyle->lineHeight : font->height;
	}

	inline QFixed _blockRBearing(const ITextBlock *b) {
		return (b->type() == TextBlockText) ? static_cast<const TextBlock*>(b)->f_rbearing() : 0;
	}
}

const style::textStyle *textstyleCurrent() {
	return _textStyle;
}

void textstyleSet(const style::textStyle *style) {
	_textStyle = style ? style : &st::defaultTextStyle;
}

void textlnkOver(const TextLinkPtr &lnk) {
	_overLnk = lnk;
}

const TextLinkPtr &textlnkOver() {
	return _overLnk;
}

void textlnkDown(const TextLinkPtr &lnk) {
	_downLnk = lnk;
}

const TextLinkPtr &textlnkDown() {
	return _downLnk;
}

QString textOneLine(const QString &text, bool trim, bool rich) {
	QString result(text);
	const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size();
	if (trim) {
		while (s < e && chIsTrimmed(*s)) {
			++s;
		}
		while (s < e && chIsTrimmed(*(e - 1))) {
			--e;
		}
		if (e - s != text.size()) {
			result = text.mid(s - text.unicode(), e - s);
		}
	}
	for (const QChar *ch = s; ch != e; ++ch) {
		if (chIsNewline(*ch)) {
            result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString textClean(const QString &text) {
	QString result(text);
	for (const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size(); ch != e; ++ch) {
		if (*ch == TextCommand) {
            result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString textRichPrepare(const QString &text) {
	QString result;
	result.reserve(text.size());
	const QChar *s = text.constData(), *ch = s;
	for (const QChar *e = s + text.size(); ch != e; ++ch) {
		if (*ch == TextCommand) {
			if (ch > s) result.append(s, ch - s);
			result.append(QChar::Space);
			s = ch + 1;
			continue;
		}
		if (ch->unicode() == '\\' || ch->unicode() == '[') {
			if (ch > s) result.append(s, ch - s);
			result.append('\\');
			s = ch;
			continue;
		}
	}
	if (ch > s) result.append(s, ch - s);
	return result;
}

QString textcmdSkipBlock(ushort w, ushort h) {
	static QString cmd(5, TextCommand);
	cmd[1] = QChar(TextCommandSkipBlock);
	cmd[2] = QChar(w);
	cmd[3] = QChar(h);
	return cmd;
}

QString textcmdStartLink(ushort lnkIndex) {
	static QString cmd(4, TextCommand);
	cmd[1] = QChar(TextCommandLinkIndex);
	cmd[2] = QChar(lnkIndex);
	return cmd;
}

QString textcmdStartLink(const QString &url) {
	if (url.size() >= 4096) return QString();

	QString result;
	result.reserve(url.size() + 4);
	return result.append(TextCommand).append(QChar(TextCommandLinkText)).append(QChar(url.size())).append(url).append(TextCommand);
}

QString textcmdStopLink() {
	return textcmdStartLink(0);
}

QString textcmdLink(ushort lnkIndex, const QString &text) {
	QString result;
	result.reserve(4 + text.size() + 4);
	return result.append(textcmdStartLink(lnkIndex)).append(text).append(textcmdStopLink());
}

QString textcmdLink(const QString &url, const QString &text) {
	QString result;
	result.reserve(4 + url.size() + text.size() + 4);
	return result.append(textcmdStartLink(url)).append(text).append(textcmdStopLink());
}

QString textcmdStartColor(const style::color &color) {
	QString result;
	result.reserve(7);
	return result.append(TextCommand).append(QChar(TextCommandColor)).append(QChar(color->c.red())).append(QChar(color->c.green())).append(QChar(color->c.blue())).append(QChar(color->c.alpha())).append(TextCommand);
}

QString textcmdStopColor() {
	QString result;
	result.reserve(3);
	return result.append(TextCommand).append(QChar(TextCommandNoColor)).append(TextCommand);
}

class TextParser {
	struct LinkRange {
		LinkRange() : from(0), len(0) {
		}
		const QChar *from;
		int32 len;
	};

public:
	
	static Qt::LayoutDirection stringDirection(const QString &str, int32 from, int32 to) {
		const ushort *p = reinterpret_cast<const ushort*>(str.unicode()) + from;
		const ushort *end = p + (to - from);
		while (p < end) {
			uint ucs4 = *p;
			if (QChar::isHighSurrogate(ucs4) && p < end - 1) {
				ushort low = p[1];
				if (QChar::isLowSurrogate(low)) {
					ucs4 = QChar::surrogateToUcs4(ucs4, low);
					++p;
				}
			}
			switch (QChar::direction(ucs4)) {
			case QChar::DirL:
				return Qt::LeftToRight;
			case QChar::DirR:
			case QChar::DirAL:
				return Qt::RightToLeft;
			default:
				break;
			}
			++p;
		}
		return Qt::LayoutDirectionAuto;
	}

	void prepareLinks() { // support emails!
		if (validProtocols.empty()) {
			initLinkSets();
		}
		int32 len = src.size(), nextCmd = rich ? 0 : len;
		const QChar *srcData = src.unicode();
		for (int32 offset = 0; offset < len; ) {
			if (nextCmd <= offset) {
				for (nextCmd = offset; nextCmd < len; ++nextCmd) {
					if (*(srcData + nextCmd) == TextCommand) {
						break;
					}
				}
			}
			QRegularExpressionMatch mDomain = reDomain.match(src, offset);
			if (!mDomain.hasMatch()) break;

			int32 domainOffset = mDomain.capturedStart(), domainEnd = mDomain.capturedEnd();
			if (domainOffset > nextCmd) {
				const QChar *after = skipCommand(srcData + nextCmd, srcData + len);
				if (after > srcData + nextCmd && domainOffset < (after - srcData)) {
					nextCmd = offset = after - srcData;
					continue;
				}
			}

			QString protocol = mDomain.captured(1).toLower();
			QString topDomain = mDomain.captured(3).toLower();
				
			bool isProtocolValid = protocol.isEmpty() || validProtocols.contains(hashCrc32(protocol.constData(), protocol.size() * sizeof(QChar)));
			bool isTopDomainValid = validTopDomains.contains(hashCrc32(topDomain.constData(), topDomain.size() * sizeof(QChar)));

			if (!isProtocolValid || !isTopDomainValid) {
				offset = domainEnd;
				continue;
			}

			LinkRange link;
			if (protocol.isEmpty() && domainOffset > offset + 1 && *(start + domainOffset - 1) == QChar('@')) {
				QString forMailName = src.mid(offset, domainOffset - offset - 1);
				QRegularExpressionMatch mMailName = reMailName.match(forMailName);
				if (mMailName.hasMatch()) {
					int32 mailOffset = offset + mMailName.capturedStart();
					if (mailOffset < offset) {
						mailOffset = offset;
					}
					link.from = start + mailOffset;
					link.len = domainEnd - mailOffset;
				}
			}
			if (!link.from || !link.len) {
				link.from = start + domainOffset;

				QStack<const QChar*> parenth;
				const QChar *p = start + mDomain.capturedEnd();
				for (; p < end; ++p) {
					QChar ch(*p);
					if (chIsLinkEnd(ch)) break; // link finished
					if (chIsAlmostLinkEnd(ch)) {
						const QChar *endTest = p + 1;
						while (endTest < end && chIsAlmostLinkEnd(*endTest)) {
							++endTest;
						}
						if (endTest >= end || chIsLinkEnd(*endTest)) {
							break; // link finished at p
						}
						p = endTest;
						ch = *p;
					}
					if (ch == '(' || ch == '[' || ch == '{' || ch == '<') {
						parenth.push(p);
					} else if (ch == ')' || ch == ']' || ch == '}' || ch == '>') {
						if (parenth.isEmpty()) break;
						const QChar *q = parenth.pop(), open(*q);
						if ((ch == ')' && open != '(') || (ch == ']' && open != '[') || (ch == '}' && open != '{') || (ch == '>' && open != '<')) {
							p = q;
							break;
						}
					}
				}

				link.len = p - link.from;
			}
			lnkRanges.push_back(link);

			offset = (link.from - start) + link.len;
		}
	}

	void blockCreated() {
		sumWidth += _t->_blocks.back()->f_width();
		if (sumWidth.floor().toInt() > stopAfterWidth) {
			sumFinished = true;
		}
	}

	void createBlock(int32 skipBack = 0) {
		if (lnkIndex < 0x8000 && lnkIndex > maxLnkIndex) maxLnkIndex = lnkIndex;
		int32 len = int32(_t->_text.size()) + skipBack - blockStart;
		if (len > 0) {
			lastSkipped = lastSpace = false;
			if (emoji) {
				_t->_blocks.push_back(new EmojiBlock(_t->_font, _t->_text, blockStart, len, flags, color, lnkIndex, emoji));
				emoji = 0;
				lastSkipped = true;
			} else if (len == 1 && _t->_text.at(blockStart) == QChar::LineFeed) {
				_t->_blocks.push_back(new NewlineBlock(_t->_font, _t->_text, blockStart, len));
			} else {
				_t->_blocks.push_back(new TextBlock(_t->_font, _t->_text, _t->_minResizeWidth, blockStart, len, flags, color, lnkIndex));
			}
			blockStart += len;
			blockCreated();
		}
	}

	void createSkipBlock(int32 w, int32 h) {
		createBlock();
		_t->_text.push_back('_');
		_t->_blocks.push_back(new SkipBlock(_t->_font, _t->_text, blockStart++, w, h, lnkIndex));
		blockCreated();
	}

	void getLinkData(const QString &original, QString &result, int32 &fullDisplayed) {
		if (reMailStart.match(original).hasMatch()) {
			result = original;
			fullDisplayed = -1;
		} else {
			QUrl url(original), good(url.isValid() ? url.toEncoded() : "");
			QString readable = good.isValid() ? good.toDisplayString() : original;
			result = _t->_font->m.elidedText(readable, Qt::ElideRight, LinkCropLimit);
			fullDisplayed = (result == readable) ? 1 : 0;
		}
	}

	bool checkWaitedLink() {
		if (waitingLink == linksEnd || ptr < waitingLink->from || links.size() >= 0x7FFF) {
			return true;
		}

		createBlock();

		QString lnkUrl = QString(waitingLink->from, waitingLink->len), lnkText;
		int32 fullDisplayed;
		getLinkData(lnkUrl, lnkText, fullDisplayed);

		links.push_back(TextLinkData(lnkUrl, fullDisplayed));
		lnkIndex = 0x8000 + links.size();

		_t->_text += lnkText;
		ptr = waitingLink->from + waitingLink->len;

		createBlock();
		++waitingLink;
		lnkIndex = 0;

		return true;
	}
	
	const QChar *skipCommand(const QChar *from, const QChar *end) {
		const QChar *result = from + 1;
		if (*from != TextCommand || result >= end) return from;

		ushort cmd = result->unicode();
		++result;
		if (result >= end) return from;

		switch (cmd) {
		case TextCommandBold:
		case TextCommandNoBold:
		case TextCommandItalic:
		case TextCommandNoItalic:
		case TextCommandUnderline:
		case TextCommandNoUnderline:
		case TextCommandNoColor:
		break;

		case TextCommandLinkIndex:
			if (result->unicode() > 0x7FFF) return from;
			++result;
		break;

		case TextCommandLinkText: {
			ushort len = result->unicode();
			if (len >= 4096 || links.size() >= 0x7FFF) return from;
			result += len + 1;
		} break;

		case TextCommandColor: {
			const QChar *e = result + 4;
			if (e >= end) return from;
			
			for (; result < e; ++result) {
				if (result->unicode() >= 256) return from;
			}
		} break;

		case TextCommandSkipBlock:
			result += 2;
		break;
		}
		return (result < end && *result == TextCommand) ? (result + 1) : from;
	}

	bool readCommand() {
		const QChar *afterCmd = skipCommand(ptr, end);
		if (afterCmd == ptr) {
			return false;
		}

		ushort cmd = (++ptr)->unicode();
		++ptr;

		switch (cmd) {
		case TextCommandBold:
			if (!(flags & TextBlockBold)) {
				createBlock();
				flags |= TextBlockBold;
			}
		break;

		case TextCommandNoBold:
			if (flags & TextBlockBold) {
				createBlock();
				flags &= ~TextBlockBold;
			}
		break;

		case TextCommandItalic:
			if (!(flags & TextBlockItalic)) {
				createBlock();
				flags |= TextBlockItalic;
			}
		break;

		case TextCommandNoItalic:
			if (flags & TextBlockItalic) {
				createBlock();
				flags &= ~TextBlockItalic;
			}
		break;

		case TextCommandUnderline:
			if (!(flags & TextBlockUnderline)) {
				createBlock();
				flags |= TextBlockUnderline;
			}
		break;

		case TextCommandNoUnderline:
			if (flags & TextBlockUnderline) {
				createBlock();
				flags &= ~TextBlockUnderline;
			}
		break;

		case TextCommandLinkIndex:
			if (ptr->unicode() != lnkIndex) {
				createBlock();
				lnkIndex = ptr->unicode();
			}
		break;

		case TextCommandLinkText: {
			createBlock();
			int32 len = ptr->unicode();
			links.push_back(TextLinkData(QString(++ptr, len), false));
			lnkIndex = 0x8000 + links.size();
		} break;

		case TextCommandColor: {
			style::color c(ptr->unicode(), (ptr + 1)->unicode(), (ptr + 2)->unicode(), (ptr + 3)->unicode());
			if (color != c) {
				createBlock();
				color = c;
			}
		} break;

		case TextCommandSkipBlock:
			createBlock();
			createSkipBlock(ptr->unicode(), (ptr + 1)->unicode());
		break;

		case TextCommandNoColor:
			if (color) {
				createBlock();
				color = style::color();
			}
		break;
		}

		ptr = afterCmd;
		return true;
	}

	void parseCurrentChar() {
		ch = ((ptr < end) ? *ptr : 0);
		while (rich && ch == TextCommand) {
			if (readCommand()) {
				ch = ((ptr < end) ? *ptr : 0);
			} else {
				ch = QChar::Space;
			}
		}

		int skipBack = 0;
		chInt = ch.unicode();
		bool skip = false, isNewLine = multiline && chIsNewline(ch), isSpace = chIsSpace(ch, rich), isDiac = chIsDiac(ch);
		if (chIsBad(ch) || ch.isLowSurrogate()) {
			skip = true;
		} else if (isDiac) {
			if (lastSkipped || lastSpace || emoji || ++diacs > chMaxDiacAfterSymbol()) {
				skip = true;
			}
		} else if (isSpace && lastSpace && !isNewLine) {
			skip = true;
		} else if (ch.isHighSurrogate()) {
			if (ptr + 1 >= end || !(ptr + 1)->isLowSurrogate()) {
				skip = true;
			} else {
				_t->_text.push_back(ch);
				skipBack = -1;
				++ptr;
				ch = *ptr;
				chInt = (chInt << 16) | ch.unicode();
			}
		} else if ((ch >= 48 && ch < 58) || ch == 35) { // check for digit emoji
			if (ptr + 1 < end && (ptr + 1)->unicode() == 0x20E3) {
				_t->_text.push_back(ch);
				skipBack = -1;
				++ptr;
				ch = *ptr;
				chInt = (chInt << 16) | 0x20E3;
			}
		}

		lastSkipped = skip;
		lastSpace = isSpace;
		if (skip) {
			ch = 0;
		} else {
			if (isNewLine) {
				createBlock();
				_t->_text.push_back(QChar::LineFeed);
				createBlock();
			} else if (isSpace) {
				_t->_text.push_back(QChar::Space);
			} else {
				if (emoji) createBlock(skipBack);
				_t->_text.push_back(ch);
			}
			if (!isDiac) diacs = 0;
		}
	}

	void parseEmojiFromCurrent() {
		const EmojiData *e = getEmoji(chInt);
		if (!e) return;

		if (e->len > 2) {
			if (ptr + 2 >= end || e->code2 != ((uint32((ptr + 1)->unicode()) << 16) | uint32((ptr + 2)->unicode()))) {
				return;
			} else {
				_t->_text.push_back(*++ptr);
				_t->_text.push_back(*++ptr);
			}
		}

		createBlock(-e->len);
		emoji = e;
	}

	TextParser(Text *t, const QString &text, const TextParseOptions &options) : _t(t), src(text),
		rich(options.flags & TextParseRichText), multiline(options.flags & TextParseMultiline), maxLnkIndex(0), flags(0), lnkIndex(0), stopAfterWidth(QFIXED_MAX) {
		int flags = options.flags;
		if (options.maxw > 0 && options.maxh > 0) {
			stopAfterWidth = ((options.maxh / _t->_font->height) + 1) * options.maxw;
		}

		start = src.constData();
		end = start + src.size();

		if (options.flags & TextParseLinks) {
			prepareLinks();
		}

		while (start != end && chIsTrimmed(*start, rich)) {
			++start;
		}
		while (start != end && chIsTrimmed(*(end - 1), rich)) {
			--end;
		}

		_t->_text.resize(0);
		_t->_text.reserve(end - start);

		diacs = 0;
		sumWidth = 0;
		sumFinished = false;
		blockStart = 0;
		emoji = 0;

		ch = chInt = 0;
		lastSkipped = false;
		lastSpace = true;
		waitingLink = lnkRanges.isEmpty() ? 0 : lnkRanges.constData();
		linksEnd = lnkRanges.isEmpty() ? 0 : waitingLink + lnkRanges.size();
		for (ptr = start; ptr <= end; ++ptr) {
			if (!checkWaitedLink()) {
				break;
			}
			parseCurrentChar();

			parseEmojiFromCurrent();

			if (sumFinished || _t->_text.size() >= 0x8000) break; // 32k max
		}
		createBlock();

		_t->_links.resize(maxLnkIndex);
		for (Text::TextBlocks::const_iterator i = _t->_blocks.cbegin(), e = _t->_blocks.cend(); i != e; ++i) {
			ITextBlock *b = *i;
			if (b->lnkIndex() > 0x8000) {
				lnkIndex = maxLnkIndex + (b->lnkIndex() - 0x8000);
				if (_t->_links.size() < lnkIndex) {
					_t->_links.resize(lnkIndex);
					const TextLinkData &data(links[lnkIndex - maxLnkIndex - 1]);
					TextLinkPtr lnk;
					if (data.fullDisplayed < 0) { // email
						lnk = TextLinkPtr(new EmailLink(data.url));
					} else {
						lnk = TextLinkPtr(new TextLink(data.url, data.fullDisplayed > 0));
					}
					_t->setLink(lnkIndex, lnk);
				}
				b->setLnkIndex(lnkIndex);
			}
		}
		_t->_links.squeeze();
		_t->_blocks.squeeze();
		_t->_text.squeeze();
	}

private:

	Text *_t;
	const QString &src;
	const QChar *start, *end, *ptr;
	bool rich, multiline;

	typedef QVector<LinkRange> LinkRanges;
	LinkRanges lnkRanges;
	const LinkRange *waitingLink, *linksEnd;

	struct TextLinkData {
		TextLinkData(const QString &url = QString(), int32 fullDisplayed = 1) : url(url), fullDisplayed(fullDisplayed) {
		}
		QString url;
		int32 fullDisplayed; // < 0 - email
	};
	typedef QVector<TextLinkData> TextLinks;
	TextLinks links;

	uint16 maxLnkIndex;

	// current state
	int32 flags;
	uint16 lnkIndex;
	const EmojiData *emoji; // current emoji, if current word is an emoji, or zero
	int32 blockStart; // offset in result, from which current parsed block is started
	int32 diacs; // diac chars skipped without good char
	QFixed sumWidth, stopAfterWidth; // summary width of all added words
	bool sumFinished;
	style::color color; // current color, could be invalid

	// current char data
	QChar ch; // current char (low surrogate, if current char is surrogate pair)
	uint32 chInt; // full ch, could be surrogate pair
	bool lastSkipped; // did we skip current char
	bool lastSpace; // was last char a space character
};

namespace {
	// COPIED FROM qtextengine.cpp AND MODIFIED

	struct BidiStatus {
		BidiStatus() {
			eor = QChar::DirON;
			lastStrong = QChar::DirON;
			last = QChar:: DirON;
			dir = QChar::DirON;
		}
		QChar::Direction eor;
		QChar::Direction lastStrong;
		QChar::Direction last;
		QChar::Direction dir;
	};

	enum { _MaxBidiLevel = 61 };
	enum { _MaxItemLength = 4096 };

	struct BidiControl {
		inline BidiControl(bool rtl)
			: cCtx(0), base(rtl ? 1 : 0), level(rtl ? 1 : 0), override(false) {}

		inline void embed(bool rtl, bool o = false) {
			unsigned int toAdd = 1;
			if((level%2 != 0) == rtl ) {
				++toAdd;
			}
			if (level + toAdd <= _MaxBidiLevel) {
				ctx[cCtx].level = level;
				ctx[cCtx].override = override;
				cCtx++;
				override = o;
				level += toAdd;
			}
		}
		inline bool canPop() const { return cCtx != 0; }
		inline void pdf() {
			Q_ASSERT(cCtx);
			--cCtx;
			level = ctx[cCtx].level;
			override = ctx[cCtx].override;
		}

		inline QChar::Direction basicDirection() const {
			return (base ? QChar::DirR : QChar:: DirL);
		}
		inline unsigned int baseLevel() const {
			return base;
		}
		inline QChar::Direction direction() const {
			return ((level%2) ? QChar::DirR : QChar:: DirL);
		}

		struct {
			unsigned int level;
			bool override;
		} ctx[_MaxBidiLevel];
		unsigned int cCtx;
		const unsigned int base;
		unsigned int level;
		bool override;
	};

	static void eAppendItems(QScriptAnalysis *analysis, int &start, int &stop, const BidiControl &control, QChar::Direction dir) {
		if (start > stop)
			return;

		int level = control.level;

		if(dir != QChar::DirON && !control.override) {
			// add level of run (cases I1 & I2)
			if(level % 2) {
				if(dir == QChar::DirL || dir == QChar::DirAN || dir == QChar::DirEN)
					level++;
			} else {
				if(dir == QChar::DirR)
					level++;
				else if(dir == QChar::DirAN || dir == QChar::DirEN)
					level += 2;
			}
		}

		QScriptAnalysis *s = analysis + start;
		const QScriptAnalysis *e = analysis + stop;
		while (s <= e) {
			s->bidiLevel = level;
			++s;
		}
		++stop;
		start = stop;
	}

}

class TextPainter {
public:

	static inline uint16 _blockEnd(const Text *t, const Text::TextBlocks::const_iterator &i, const Text::TextBlocks::const_iterator &e) {
		return (i + 1 == e) ? t->_text.size() : (*(i + 1))->from();
	}
	static inline uint16 _blockLength(const Text *t, const Text::TextBlocks::const_iterator &i, const Text::TextBlocks::const_iterator &e) {
		return _blockEnd(t, i, e) - (*i)->from();
	}

	TextPainter(QPainter *p, const Text *t) : _p(p), _t(t), _elideLast(false), _str(0), _elideSavedBlock(0), _lnkResult(0), _inTextFlag(0), _getSymbol(0), _getSymbolAfter(0), _getSymbolUpon(0) {
	}

	void initNextParagraph(Text::TextBlocks::const_iterator i) {
		_parStartBlock = i;
		Text::TextBlocks::const_iterator e = _t->_blocks.cend();
		if (i == e) {
			_parStart = _t->_text.size();
			_parLength = 0;
		} else {
			_parStart = (*i)->from();
			for (; i != e; ++i) {
				if ((*i)->type() == TextBlockNewline) {
					break;
				}
			}
			_parLength = ((i == e) ? _t->_text.size() : (*i)->from()) - _parStart;
		}
		_parAnalysis.resize(0);
	}

	void initParagraphBidi() {
		if (!_parLength || !_parAnalysis.isEmpty()) return;
		
		Text::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		bool ignore = false;
		bool rtl = (_parDirection == Qt::RightToLeft);
		if (!ignore && !rtl) {
			ignore = true;
			const ushort *start = reinterpret_cast<const ushort*>(_str) + _parStart;
			const ushort *curr = start;
			const ushort *end = start + _parLength;
			while (curr < end) {
				while (n != e && (*n)->from() <= _parStart + (curr - start)) {
					i = n;
					++n;
				}
				if ((*i)->type() != TextBlockEmoji && *curr >= 0x590) {
					ignore = false;
					break;
				}
				++curr;
			}
		}

		_parAnalysis.resize(_parLength);
		QScriptAnalysis *analysis = _parAnalysis.data();

		BidiControl control(rtl);

		_parHasBidi = false;
		if (ignore) {
			memset(analysis, 0, _parLength * sizeof(QScriptAnalysis));
			if (rtl) {
				for (int i = 0; i < _parLength; ++i)
					analysis[i].bidiLevel = 1;
				_parHasBidi = true;
			}
		} else {
			_parHasBidi = eBidiItemize(analysis, control);
		}
	}

	void draw(int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, uint16 selectedFrom = 0, uint16 selectedTo = 0) {
		if (_t->_blocks.isEmpty()) return;

		_blocksSize = _t->_blocks.size();
		if (!_textStyle) _initDefault();

		if (_p) {
			_p->setFont(_t->_font->f);
			_originalPen = _p->pen();
		}

		_x = left;
		_y = top;
		_yFrom = yFrom + top;
		_yTo = (yTo < 0) ? -1 : (yTo + top);
		_selectedFrom = selectedFrom;
		_selectedTo = selectedTo;
		_wLeft = _w = w;
		_str = _t->_text.unicode();

		if (_p) {
			QRectF clip = _p->clipBoundingRect();
			if (clip.width() > 0 || clip.height() > 0) {
				if (_yFrom < clip.y()) _yFrom = clip.y();
				if (_yTo < 0 || _yTo > clip.y() + clip.height()) _yTo = clip.y() + clip.height();
			}
		}

		_align = align;

		_parDirection = _t->_startDir;
		if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = langDir();
		if ((*_t->_blocks.cbegin())->type() != TextBlockNewline) {
			initNextParagraph(_t->_blocks.cbegin());
		}

		_lineStart = 0;
		_lineStartBlock = 0;

		_lineHeight = 0;
		_fontHeight = _t->_font->height;
		QFixed last_rBearing = 0, last_rPadding = 0;

		int32 blockIndex = 0;
		bool longWordLine = true;
		Text::TextBlocks::const_iterator e = _t->_blocks.cend();
		for (Text::TextBlocks::const_iterator i = _t->_blocks.cbegin(); i != e; ++i, ++blockIndex) {
			ITextBlock *b = *i;
			TextBlockType _btype = b->type();
			int32 blockHeight = _blockHeight(b, _t->_font);
			QFixed _rb = _blockRBearing(b);

			if (_btype == TextBlockNewline) {
				if (!_lineHeight) _lineHeight = blockHeight;
				ushort nextStart = _blockEnd(_t, i, e);
				if (!drawLine(nextStart, i + 1, e)) return;

				_y += _lineHeight;
				_lineHeight = 0;
				_lineStart = nextStart;
				_lineStartBlock = blockIndex + 1;

				last_rBearing = _rb;
				last_rPadding = b->f_rpadding();
				_wLeft = _w - (b->f_width() - last_rBearing);

				_parDirection = static_cast<NewlineBlock*>(b)->nextDirection();
				if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = langDir();
				initNextParagraph(i + 1);

				longWordLine = true;
				continue;
			}

			QFixed lpadding = b->f_lpadding();
			QFixed newWidthLeft = _wLeft - lpadding - last_rBearing - (last_rPadding + b->f_width() - _rb);
			if (newWidthLeft >= 0) {
				last_rBearing = _rb;
				last_rPadding = b->f_rpadding();
				_wLeft = newWidthLeft;

				_lineHeight = qMax(_lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			if (_btype == TextBlockText) {
				TextBlock *t = static_cast<TextBlock*>(b);
				QFixed f_wLeft = _wLeft;
				int32 f_lineHeight = _lineHeight;
				for (TextBlock::TextWords::const_iterator j = t->_words.cbegin(), en = t->_words.cend(), f = j; j != en; ++j) {
					bool wordEndsHere = (j->width >= 0);
					QFixed j_width = wordEndsHere ? j->width : -j->width;

					QFixed newWidthLeft = _wLeft - lpadding - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
					lpadding = 0;
					if (newWidthLeft >= 0) {
						last_rBearing = j->f_rbearing();
						last_rPadding = j->rpadding;
						_wLeft = newWidthLeft;

						_lineHeight = qMax(_lineHeight, blockHeight);

						if (wordEndsHere) {
							longWordLine = false;
						}
						if (wordEndsHere || longWordLine) {
							f_wLeft = _wLeft;
							f_lineHeight = _lineHeight;
							f = j + 1;
						}
						continue;
					}

					int32 elidedLineHeight = qMax(_lineHeight, blockHeight);
					bool elidedLine = _elideLast && (_y + elidedLineHeight >= _yTo);
					if (elidedLine) {
						_lineHeight = elidedLineHeight;
					} else if (f != j) {
						j = f;
						_wLeft = f_wLeft;
						_lineHeight = f_lineHeight;
						j_width = (j->width >= 0) ? j->width : -j->width;
					}
					if (!drawLine(elidedLine ? ((j + 1 == en) ? _blockEnd(_t, i, e) : (j + 1)->from) : j->from, i, e)) return;
					_y += _lineHeight;
					_lineHeight = qMax(0, blockHeight);
					_lineStart = j->from;
					_lineStartBlock = blockIndex;

					last_rBearing = j->f_rbearing();
					last_rPadding = j->rpadding;
					_wLeft = _w - (j_width - last_rBearing);

					longWordLine = true;
					f = j + 1;
					f_wLeft = _wLeft;
					f_lineHeight = _lineHeight;
				}
				continue;
			}

			int32 elidedLineHeight = qMax(_lineHeight, blockHeight);
			bool elidedLine = _elideLast && (_y + elidedLineHeight >= _yTo);
			if (elidedLine) {
				_lineHeight = elidedLineHeight;
			}
			if (!drawLine(elidedLine ? _blockEnd(_t, i, e) : b->from(), i, e)) return;
			_y += _lineHeight;
			_lineHeight = qMax(0, blockHeight);
			_lineStart = b->from();
			_lineStartBlock = blockIndex;

			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			_wLeft = _w - (b->f_width() - last_rBearing);

			longWordLine = true;
			continue;
		}
		if (_lineStart < _t->_text.size()) {
			if (!drawLine(_t->_text.size(), e, e)) return;
		}
		if (_getSymbol) {
			*_getSymbol = _t->_text.size();
			*_getSymbolAfter = false;
			*_getSymbolUpon = false;
		}
	}

	void drawElided(int32 left, int32 top, int32 w, style::align align, int32 lines, int32 yFrom, int32 yTo) {
		if (lines <= 0) return;

		if (yTo < 0 || (lines - 1) * _t->_font->height < yTo) {
			yTo = lines * _t->_font->height;
			_elideLast = true;
		}
		draw(left, top, w, align, yFrom, yTo);
	}

	const TextLinkPtr &link(int32 x, int32 y, int32 w, style::align align) {
		_lnkX = x;
		_lnkY = y;
		_lnkResult = &_zeroLnk;
		if (_lnkX >= 0 && _lnkX < w && _lnkY >= 0) {
			draw(0, 0, w, align, _lnkY, _lnkY + 1);
		}
		return *_lnkResult;
	}

	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, int32 w, style::align align) {
		lnk = TextLinkPtr();
		inText = false;

		if (x >= 0 && x < w && y >= 0) {
			_lnkX = x;
			_lnkY = y;
			_lnkResult = &lnk;
			_inTextFlag = &inText;
			draw(0, 0, w, align, _lnkY, _lnkY + 1);
			lnk = *_lnkResult;
		}
	}

	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y, int32 w, style::align align) {
		symbol = 0;
		after = false;
		upon = false;

		if (y >= 0) {
			_lnkX = x;
			_lnkY = y;
			_getSymbol = &symbol;
			_getSymbolAfter = &after;
			_getSymbolUpon = &upon;
			draw(0, 0, w, align, _lnkY, _lnkY + 1);
		}
	}

	const QPen &blockPen(ITextBlock *block) {
		if (block->color()) {
			return block->color()->p;
		}
		if (block->lnkIndex()) {
			const TextLinkPtr &l(_t->_links.at(block->lnkIndex() - 1));
			if (l == _overLnk) {
				if (l == _downLnk) {
					return _textStyle->lnkDownColor->p;
				}
			}
			return _textStyle->lnkColor->p;
		}
		return _originalPen;
	}

	bool drawLine(uint16 _lineEnd, const Text::TextBlocks::const_iterator &_endBlockIter, const Text::TextBlocks::const_iterator &_end) {
		_yDelta = (_lineHeight - _fontHeight) / 2;
		if (_yTo >= 0 && _y + _yDelta >= _yTo) return false;
		if (_y + _yDelta + _fontHeight <= _yFrom) return true;

		ITextBlock *_endBlock = (_endBlockIter == _end) ? 0 : (*_endBlockIter);
		bool elidedLine = _elideLast && _endBlock && (_y + _lineHeight >= _yTo);

		QFixed x = _x;
		if (_align & Qt::AlignHCenter) {
			x += (_wLeft / 2).toInt();
		} else if (((_align & Qt::AlignLeft) && _parDirection == Qt::RightToLeft) || ((_align & Qt::AlignRight) && _parDirection == Qt::LeftToRight)) {
			x += _wLeft;
		}

		if (_getSymbol) {
			if (_lnkX < x) {
				if (_parDirection == Qt::RightToLeft) {
					*_getSymbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
					*_getSymbolAfter = (_lineEnd > _lineStart) ? true : false;
					*_getSymbolUpon = ((_lnkX >= _x) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockSkip)) ? true : false;
				} else {
					*_getSymbol = _lineStart;
					*_getSymbolAfter = false;
					*_getSymbolUpon = ((_lnkX >= _x) && (_lineStart > 0)) ? true : false;
				}
				return false;  
			} else if (_lnkX >= x + (_w - _wLeft)) {
				if (_parDirection == Qt::RightToLeft) {
					*_getSymbol = _lineStart;
					*_getSymbolAfter = false;
					*_getSymbolUpon = ((_lnkX < _x + _w) && (_lineStart > 0)) ? true : false;
				} else {
					*_getSymbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
					*_getSymbolAfter = (_lineEnd > _lineStart) ? true : false;
					*_getSymbolUpon = ((_lnkX < _x + _w) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockSkip)) ? true : false;
				}
				return false;
			}
		}

		bool selectFromStart = (_selectedTo > _lineStart) && (_lineStart > 0) && (_selectedFrom <= _lineStart);
		bool selectTillEnd = (_selectedTo >= _lineEnd) && (_lineEnd < _t->_text.size()) && (_selectedFrom < _lineEnd) && (!_endBlock || _endBlock->type() != TextBlockSkip);

		if ((selectFromStart && _parDirection == Qt::LeftToRight) || (selectTillEnd && _parDirection == Qt::RightToLeft)) {
			if (x > _x) {
				_p->fillRect(QRectF(_x.toReal(), _y + _yDelta, (x - _x).toReal(), _fontHeight), _textStyle->selectBG->b);
			}
		}
		if ((selectTillEnd && _parDirection == Qt::LeftToRight) || (selectFromStart && _parDirection == Qt::RightToLeft)) {
			if (x < _x + _wLeft) {
				_p->fillRect(QRectF((x + _w - _wLeft).toReal(), _y + _yDelta, (_x + _wLeft - x).toReal(), _fontHeight), _textStyle->selectBG->b);
			}
		}

		/* // lpadding is counted to _wLeft
		for (; _lineStart < _lineEnd; ++_lineStart) {
			if (_t->_text.at(_lineStart) != QChar::Space) {
				break;
			}
		}/**/
        for (; _lineEnd > _lineStart; --_lineEnd) {
			QChar ch = _t->_text.at(_lineEnd - 1);
            if ((ch != QChar::Space || _lineEnd == _lineStart + 1) && ch != QChar::LineFeed) {
				break;
			}
		}/**/
		if (_lineEnd == _lineStart && !elidedLine) return true;

		initParagraphBidi(); // if was not inited

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		int32 delta = (currentBlock->from() < _lineStart ? qMin(_lineStart - currentBlock->from(), 2) : 0);
		_localFrom = _lineStart - delta;
		int32 lineEnd = (_endBlock && _endBlock->from() < _lineEnd && !elidedLine) ? qMin(uint16(_lineEnd + 2), _blockEnd(_t, _endBlockIter, _end)) : _lineEnd;

		QString lineText = _t->_text.mid(_localFrom, lineEnd - _localFrom);
		int32 lineStart = delta, lineLength = _lineEnd - _lineStart;

		if (elidedLine) prepareElidedLine(lineText, lineStart, lineLength, _endBlock);

		_f = _t->_font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	    int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0;
		if (!nItems) {
			if (elidedLine) restoreAfterElided();
			return true;
		}

		QVarLengthArray<int> visualOrder(nItems);
		QVarLengthArray<uchar> levels(nItems);
		for (int i = 0; i < nItems; ++i) {
			QScriptItem &si(engine.layoutData->items[firstItem + i]);
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			}
			TextBlockType _type = currentBlock->type();
			if (_type == TextBlockSkip) {
				levels[i] = si.analysis.bidiLevel = 0;
			} else {
				levels[i] = si.analysis.bidiLevel;
			}
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockEmoji || _type == TextBlockSkip) {
					si.width = currentBlock->f_width() + (nextBlock == _endBlock && (!nextBlock || nextBlock->from() >= _lineEnd) ? 0 : currentBlock->f_rpadding());
				}
			}
		}
	    QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex];
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		int32 textY = _y + _yDelta + _t->_font->ascent, emojiY = (_t->_font->height - st::emojiSize) / 2;

		eSetFont(currentBlock);
		if (_p) _p->setPen(blockPen(currentBlock));
		for (int i = 0; i < nItems; ++i) {
			int item = firstItem + visualOrder[i];
			const QScriptItem &si = engine.layoutData->items.at(item);
			bool rtl = (si.analysis.bidiLevel % 2);

			while (blockIndex > _lineStartBlock + 1 && _t->_blocks[blockIndex - 1]->from() > _localFrom + si.position) {
				nextBlock = currentBlock;
				currentBlock = _t->_blocks[--blockIndex - 1];
				if (_p) _p->setPen(blockPen(currentBlock));
				eSetFont(currentBlock);
			}
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
				if (_p) _p->setPen(blockPen(currentBlock));
				eSetFont(currentBlock);
			}
			if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
				TextBlockType _type = currentBlock->type();
				if (_lnkResult && _lnkX >= x && _lnkX < x + si.width) {
					if (currentBlock->lnkIndex() && _lnkY >= _y + _yDelta && _lnkY < _y + _yDelta + _fontHeight) {
						_lnkResult = &_t->_links.at(currentBlock->lnkIndex() - 1);
					}
					if (_inTextFlag && _type != TextBlockSkip) {
						*_inTextFlag = true;
					}
					return false;
				} else if (_getSymbol && _lnkX >= x && _lnkX < x + si.width) {
					if (_type == TextBlockSkip) {
						if (_parDirection == Qt::RightToLeft) {
							*_getSymbol = _lineStart;
							*_getSymbolAfter = false;
							*_getSymbolUpon = false;
						} else {
							*_getSymbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
							*_getSymbolAfter = (_lineEnd > _lineStart) ? true : false;
							*_getSymbolUpon = false;
						}
						return false;
					}
					const QChar *chFrom = _str + currentBlock->from(), *chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
					if (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space) {
						if (rtl) {
							if (_lnkX < x + (si.width - currentBlock->f_width())) {
								*_getSymbol = (chTo - 1 - _str); // up to ending space, included, rtl
								*_getSymbolAfter = (_lnkX < x + (si.width - currentBlock->f_width()) / 2) ? true : false;
								*_getSymbolUpon = true;
								return false;
							}
						} else if (_lnkX >= x + currentBlock->f_width()) {
							*_getSymbol = (chTo - 1 - _str); // up to ending space, inclided, ltr
							*_getSymbolAfter = (_lnkX >= x + currentBlock->f_width() + (currentBlock->f_rpadding() / 2)) ? true : false;
							*_getSymbolUpon = true;
							return false;
						}
						--chTo;
					}
					if (_lnkX < x + (rtl ? (si.width - currentBlock->f_width()) : 0) + (currentBlock->f_width() / 2)) {
						*_getSymbol = ((rtl && chTo > chFrom) ? (chTo - 1) : chFrom) - _str;
						*_getSymbolAfter = (rtl && chTo > chFrom) ? true : false;
						*_getSymbolUpon = true;
					} else {
						*_getSymbol = ((rtl || chTo <= chFrom) ? chFrom : (chTo - 1)) - _str;
						*_getSymbolAfter = (rtl || chTo <= chFrom) ? false : true;
						*_getSymbolUpon = true;
					}
					return false;
				} else if (_p && _type == TextBlockEmoji) {
					QFixed glyphX = x;
					if (rtl) {
						glyphX += (si.width - currentBlock->f_width());
					}
					if (_localFrom + si.position < _selectedTo) {
						const QChar *chFrom = _str + currentBlock->from(), *chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
						if (_localFrom + si.position >= _selectedFrom) { // could be without space
							if (chTo == chFrom || (chTo - 1)->unicode() != QChar::Space || _selectedTo >= (chTo - _str)) {
								_p->fillRect(QRectF(x.toReal(), _y + _yDelta, si.width.toReal(), _fontHeight), _textStyle->selectBG->b);
							} else { // or with space
								_p->fillRect(QRectF(glyphX.toReal(), _y + _yDelta, currentBlock->f_width().toReal(), _fontHeight), _textStyle->selectBG->b);
							}
						} else if (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space && (chTo - 1 - _str) >= _selectedFrom) {
							if (rtl) { // rtl space only
								_p->fillRect(QRectF(x.toReal(), _y + _yDelta, (glyphX - x).toReal(), _fontHeight), _textStyle->selectBG->b);
							} else { // ltr space only
								_p->fillRect(QRectF((x + currentBlock->f_width()).toReal(), _y + _yDelta, (si.width - currentBlock->f_width()).toReal(), _fontHeight), _textStyle->selectBG->b);
							}
						}
					}
					_p->drawPixmap(QPoint((glyphX + int(st::emojiPadding)).toInt(), _y + _yDelta + emojiY), App::emojis(), QRect(static_cast<EmojiBlock*>(currentBlock)->emoji->x, static_cast<EmojiBlock*>(currentBlock)->emoji->y, st::emojiImgSize, st::emojiImgSize));
//				} else if (_p && currentBlock->type() == TextBlockSkip) { // debug
//					_p->fillRect(QRect(x.toInt(), _y, currentBlock->width(), static_cast<SkipBlock*>(currentBlock)->height()), QColor(0, 0, 0, 32));
				}
				x += si.width;
				continue;
			}

			unsigned short *logClusters = engine.logClusters(&si);
			QGlyphLayout glyphs = engine.shapedGlyphs(&si);

			int itemStart = qMax(line.from, si.position), itemEnd;
			int itemLength = engine.length(item);
			int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
			if (line.from + line.length < si.position + itemLength) {
				itemEnd = line.from + line.length;
				glyphsEnd = logClusters[itemEnd - si.position];
			} else {
				itemEnd = si.position + itemLength;
				glyphsEnd = si.num_glyphs;
			}

			QFixed itemWidth = 0;
			for (int g = glyphsStart; g < glyphsEnd; ++g)
				itemWidth += glyphs.effectiveAdvance(g);

			if (_lnkResult && _lnkX >= x && _lnkX < x + itemWidth) {
				if (currentBlock->lnkIndex() && _lnkY >= _y + _yDelta && _lnkY < _y + _yDelta + _fontHeight) {
					_lnkResult = &_t->_links.at(currentBlock->lnkIndex() - 1);
				}
				if (_inTextFlag) {
					*_inTextFlag = true;
				}
				return false;
			} else if (_getSymbol && _lnkX >= x && _lnkX < x + itemWidth) {
				QFixed tmpx = rtl ? (x + itemWidth) : x;
				for (int ch = 0, g, itemL = itemEnd - itemStart; ch < itemL;) {
					g = logClusters[itemStart - si.position + ch];
					QFixed gwidth = glyphs.effectiveAdvance(g);
					 // ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
					int ch2 = ch + 1;
					while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
						++ch2;
					}
					for (int charsCount = (ch2 - ch); ch < ch2; ++ch) {
						QFixed shift1 = QFixed(2 * (charsCount - (ch2 - ch)) + 2) * gwidth / QFixed(2 * charsCount),
						       shift2 = QFixed(2 * (charsCount - (ch2 - ch)) + 1) * gwidth / QFixed(2 * charsCount);
						if ((rtl && _lnkX >= tmpx - shift1) ||
							(!rtl && _lnkX < tmpx + shift1)) {
							*_getSymbol = _localFrom + itemStart + ch;
							if ((rtl && _lnkX >= tmpx - shift2) ||
								(!rtl && _lnkX < tmpx + shift2)) {
								*_getSymbolAfter = false;
							} else {
								*_getSymbolAfter = true;
							}
							*_getSymbolUpon = true;
							return false;
						}
					}
					if (rtl) {
						tmpx -= gwidth;
					} else {
						tmpx += gwidth;
					}
				}
				if (itemEnd > itemStart) {
					*_getSymbol = _localFrom + itemEnd - 1;
					*_getSymbolAfter = true;
				} else {
					*_getSymbol = _localFrom + itemStart;
					*_getSymbolAfter = false;
				}
				*_getSymbolUpon = true;
				return false;
			} else if (_p) {
				QTextCharFormat format;
				QTextItemInt gf(glyphs.mid(glyphsStart, glyphsEnd - glyphsStart),
								&_e->fnt, engine.layoutData->string.unicode() + itemStart,
								itemEnd - itemStart, engine.fontEngine(si), format);
				gf.logClusters = logClusters + itemStart - si.position;
				gf.width = itemWidth;
				gf.justified = false;
				gf.initWithScriptItem(si);

				if (_localFrom + itemStart < _selectedTo && _localFrom + itemEnd > _selectedFrom) {
					QFixed selX = x, selWidth = itemWidth;
					if (_localFrom + itemEnd > _selectedTo || _localFrom + itemStart < _selectedFrom) {
						selWidth = 0;
						int itemL = itemEnd - itemStart;
						int selStart = _selectedFrom - (_localFrom + itemStart), selEnd = _selectedTo - (_localFrom + itemStart);
						if (selStart < 0) selStart = 0;
						if (selEnd > itemL) selEnd = itemL;
						for (int ch = 0, g; ch < selEnd;) {
							g = logClusters[itemStart - si.position + ch];
							QFixed gwidth = glyphs.effectiveAdvance(g);
							// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
							int ch2 = ch + 1;
							while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
								++ch2;
							}
							if (ch2 <= selStart) {
								selX += gwidth;
							} else if (ch >= selStart && ch2 <= selEnd) {
								selWidth += gwidth;
							} else {
								int sStart = ch, sEnd = ch2;
								if (ch < selStart) {
									sStart = selStart;
									selX += QFixed(sStart - ch) * gwidth / QFixed(ch2 - ch);
								}
								if (ch2 >= selEnd) {
									sEnd = selEnd;
									selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
									break;
								}
								selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
							}
							ch = ch2;
						}
					}
					if (rtl) selX = x + itemWidth - (selX - x) - selWidth;
					_p->fillRect(QRectF(selX.toReal(), _y + _yDelta, selWidth.toReal(), _fontHeight), _textStyle->selectBG->b);
				}

				_p->drawTextItem(QPointF(x.toReal(), textY), gf);
			}

			x += itemWidth;
		}

		if (elidedLine) restoreAfterElided();
		return true;
	}

	void elideSaveBlock(int32 blockIndex, ITextBlock *&_endBlock, int32 elideStart, int32 elideWidth) {
		_elideSavedIndex = blockIndex;
		_elideSavedBlock = _t->_blocks[blockIndex];
		const_cast<Text*>(_t)->_blocks[blockIndex] = new TextBlock(_t->_font, _t->_text, QFIXED_MAX, elideStart, 0, _elideSavedBlock->flags(), _elideSavedBlock->color(), _elideSavedBlock->lnkIndex());
		_blocksSize = blockIndex + 1;
		_endBlock = (blockIndex + 1 < _t->_blocks.size() ? _t->_blocks[blockIndex + 1] : 0);
	}

	void setElideBidi(int32 elideStart, int32 elideLen) {
		int32 newParLength = elideStart + elideLen - _parStart;
		if (newParLength > _parAnalysis.size()) {
			_parAnalysis.resize(newParLength);
		}
		for (int32 i = elideLen; i > 0; --i) {
			_parAnalysis[newParLength - i].bidiLevel = (_parDirection == Qt::RightToLeft) ? 1 : 0;
		}
	}

	void prepareElidedLine(QString &lineText, int32 lineStart, int32 &lineLength, ITextBlock *&_endBlock, int repeat = 0) {
		static const QString _Elide = qsl("...");

		_f = _t->_font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		int32 elideWidth = _f->m.width(_Elide);
		_wLeft = _w - elideWidth;

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	    int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0, i;

		for (i = 0; i < nItems; ++i) {
			QScriptItem &si(engine.layoutData->items[firstItem + i]);
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			}
			TextBlockType _type = currentBlock->type();
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockEmoji || _type == TextBlockSkip) {
					si.width = currentBlock->f_width() + currentBlock->f_rpadding();
				}
			}
			if (_type == TextBlockEmoji || _type == TextBlockSkip || _type == TextBlockNewline) {
				if (_wLeft < si.width) {
					lineText = lineText.mid(0, currentBlock->from() - _localFrom) + _Elide;
					lineLength = currentBlock->from() + _Elide.size() - _lineStart;
					setElideBidi(currentBlock->from(), _Elide.size());
					elideSaveBlock(blockIndex - 1, _endBlock, currentBlock->from(), elideWidth);
					return;
				}
				_wLeft -= si.width;
			} else if (_type == TextBlockText) {
				unsigned short *logClusters = engine.logClusters(&si);
				QGlyphLayout glyphs = engine.shapedGlyphs(&si);

				int itemStart = qMax(line.from, si.position), itemEnd;
				int itemLength = engine.length(firstItem + i);
				int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
				if (line.from + line.length < si.position + itemLength) {
					itemEnd = line.from + line.length;
					glyphsEnd = logClusters[itemEnd - si.position];
				} else {
					itemEnd = si.position + itemLength;
					glyphsEnd = si.num_glyphs;
				}

				for (int g = glyphsStart; g < glyphsEnd; ++g) {
					QFixed adv = glyphs.effectiveAdvance(g);
					if (_wLeft < adv) {
						int pos = itemStart;
						while (pos < itemEnd && logClusters[pos - si.position] < g) {
							++pos;
						}

						if (lineText.size() <= pos || repeat > 3) {
							lineText += _Elide;
							lineLength = _localFrom + pos + _Elide.size() - _lineStart;
							setElideBidi(_localFrom + pos, _Elide.size());
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
						} else {
							lineText = lineText.mid(0, pos);
							lineLength = _localFrom + pos - _lineStart;
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
							prepareElidedLine(lineText, lineStart, lineLength, _endBlock, repeat + 1);
						}
						return;
					} else {
						_wLeft -= adv;
					}
				}
			}
		}

		int32 elideStart = _lineStart + lineText.length();
		setElideBidi(elideStart, _Elide.size());

		lineText += _Elide;
		lineLength += _Elide.size();

		if (!repeat) {
			for (; blockIndex < _blocksSize && _t->_blocks[blockIndex] != _endBlock && _t->_blocks[blockIndex]->from() < elideStart; ++blockIndex) {
			}
			if (blockIndex < _blocksSize) {
				elideSaveBlock(blockIndex, _endBlock, elideStart, elideWidth);
			}
		}
	}

	void restoreAfterElided() {
		if (_elideSavedBlock) {
			delete _t->_blocks[_elideSavedIndex];
			const_cast<Text*>(_t)->_blocks[_elideSavedIndex] = _elideSavedBlock;
			_elideSavedBlock = 0;
		}
	}

	// COPIED FROM qtextengine.cpp AND MODIFIED
	void eShapeLine(const QScriptLine &line) {
		int item = _e->findItem(line.from), end = _e->findItem(line.from + line.length - 1);
		if (item == -1)
			return;

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
		eSetFont(currentBlock);
		for (item = _e->findItem(line.from); item <= end; ++item) {
			QScriptItem &si = _e->layoutData->items[item];
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
				eSetFont(currentBlock);
			}
			_e->shape(item);
		}
	}

	void eSetFont(ITextBlock *block) {
		style::font newFont = _t->_font;
		int flags = block->flags();
		if (!flags && block->lnkIndex()) {
			const TextLinkPtr &l(_t->_links.at(block->lnkIndex() - 1));
			if (l == _overLnk) {
				if (l == _downLnk || !_downLnk) {
					flags = _textStyle->lnkOverFlags->flags();
				} else {
					flags = _textStyle->lnkFlags->flags();
				}
			} else {
				flags = _textStyle->lnkFlags->flags();
			}
		}
		if (flags & TextBlockBold) newFont = newFont->bold();
		if (flags & TextBlockItalic) newFont = newFont->italic();
		if (flags & TextBlockUnderline) newFont = newFont->underline();
		if (newFont != _f) {
			_f = newFont;
			_e->fnt = _f->f;
			_e->resetFontEngineCache();
		}
	}

	void eItemize() {
		_e->validate();
		if (_e->layoutData->items.size())
			return;

		int length = _e->layoutData->string.length();
		if (!length)
			return;

		const ushort *string = reinterpret_cast<const ushort*>(_e->layoutData->string.unicode());

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		_e->layoutData->hasBidi = _parHasBidi;
		QScriptAnalysis *analysis = _parAnalysis.data() + (_localFrom - _parStart);

		{
			QVarLengthArray<uchar> scripts(length);
			QUnicodeTools::initScripts(string, length, scripts.data());
			for (int i = 0; i < length; ++i)
				analysis[i].script = scripts.at(i);
		}

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex];
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		const ushort *start = string;
		const ushort *end = start + length;
		while (start < end) {
			while (nextBlock && nextBlock->from() <= _localFrom + (start - string)) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			}
			TextBlockType _type = currentBlock->type();
			if (_type == TextBlockEmoji || _type == TextBlockSkip) {
				analysis->script = QChar::Script_Common;
				analysis->flags = QScriptAnalysis::Object;
			} else {
				analysis->flags = QScriptAnalysis::None;
			}
			analysis->script = hbscript_to_script(script_to_hbscript(analysis->script)); // retain the old behavior
			++start;
			++analysis;
		}

		{
			const QString *i_string = &_e->layoutData->string;
			const QScriptAnalysis *i_analysis = _parAnalysis.data() + (_localFrom - _parStart);
			QScriptItemArray *i_items = &_e->layoutData->items;

			blockIndex = _lineStartBlock;
			currentBlock = _t->_blocks[blockIndex];
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			ITextBlock *startBlock = currentBlock;

			if (!length)
				return;
			int start = 0, end = start + length;
			for (int i = start + 1; i < end; ++i) {
				while (nextBlock && nextBlock->from() <= _localFrom + i) {
					currentBlock = nextBlock;
					nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
				}
				// According to the unicode spec we should be treating characters in the Common script
				// (punctuation, spaces, etc) as being the same script as the surrounding text for the
				// purpose of splitting up text. This is important because, for example, a fullstop
				// (0x2E) can be used to indicate an abbreviation and so must be treated as part of a
				// word.  Thus it must be passed along with the word in languages that have to calculate
				// word breaks.  For example the thai word "ครม." has no word breaks but the word "ครม"
				// does.
				// Unfortuntely because we split up the strings for both wordwrapping and for setting
				// the font and because Japanese and Chinese are also aliases of the script "Common",
				// doing this would break too many things.  So instead we only pass the full stop
				// along, and nothing else.
				if (currentBlock == startBlock
					&& i_analysis[i].bidiLevel == i_analysis[start].bidiLevel
					&& i_analysis[i].flags == i_analysis[start].flags
					&& (i_analysis[i].script == i_analysis[start].script || i_string->at(i) == QLatin1Char('.'))
//					&& i_analysis[i].flags < QScriptAnalysis::SpaceTabOrObject // only emojis are objects here, no tabs
					&& i - start < _MaxItemLength)
					continue;
				i_items->append(QScriptItem(start, i_analysis[start]));
				start = i;
				startBlock = currentBlock;
			}
			i_items->append(QScriptItem(start, i_analysis[start]));
		}
	}

	QChar::Direction eSkipBoundryNeutrals(QScriptAnalysis *analysis,
											const ushort *unicode,
											int &sor, int &eor, BidiControl &control,
											Text::TextBlocks::const_iterator i) {
		Text::TextBlocks::const_iterator e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction dir = control.basicDirection();
		int level = sor > 0 ? analysis[sor - 1].bidiLevel : control.level;
		while (sor <= _parLength) {
			while (i != _parStartBlock && (*i)->from() > _parStart + sor) {
				n = i;
				--i;
			}
			while (n != e && (*n)->from() <= _parStart + sor) {
				i = n;
				++n;
			}

			TextBlockType _itype = (*i)->type();
			if (eor == _parLength)
				dir = control.basicDirection();
			else if (_itype == TextBlockEmoji)
				dir = QChar::DirCS;
			else if (_itype == TextBlockSkip)
				dir = QChar::DirCS;
			else
				dir = QChar::direction(unicode[sor]);
			// Keep skipping DirBN as if it doesn't exist
			if (dir != QChar::DirBN)
				break;
			analysis[sor++].bidiLevel = level;
		}

		eor = sor;

		return dir;
	}

	// creates the next QScript items.
	bool eBidiItemize(QScriptAnalysis *analysis, BidiControl &control) {
		bool rightToLeft = (control.basicDirection() == 1);
		bool hasBidi = rightToLeft;

		int sor = 0;
		int eor = -1;

		const ushort *unicode = reinterpret_cast<const ushort*>(_t->_text.unicode()) + _parStart;
		int current = 0;

		QChar::Direction dir = rightToLeft ? QChar::DirR : QChar::DirL;
		BidiStatus status;

		Text::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction sdir;
		TextBlockType _stype = (*_parStartBlock)->type();
		if (_stype == TextBlockEmoji)
			sdir = QChar::DirCS;
		else if (_stype == TextBlockSkip)
			sdir = QChar::DirCS;
		else
			sdir = QChar::direction(*unicode);
		if (sdir != QChar::DirL && sdir != QChar::DirR && sdir != QChar::DirEN && sdir != QChar::DirAN)
			sdir = QChar::DirON;
		else
			dir = QChar::DirON;

		status.eor = sdir;
		status.lastStrong = rightToLeft ? QChar::DirR : QChar::DirL;
		status.last = status.lastStrong;
		status.dir = sdir;

		while (current <= _parLength) {
			while (n != e && (*n)->from() <= _parStart + current) {
				i = n;
				++n;
			}

			QChar::Direction dirCurrent;
			TextBlockType _itype = (*i)->type();
			if (current == (int)_parLength)
				dirCurrent = control.basicDirection();
			else if (_itype == TextBlockEmoji)
				dirCurrent = QChar::DirCS;
			else if (_itype == TextBlockSkip)
				dirCurrent = QChar::DirCS;
			else
				dirCurrent = QChar::direction(unicode[current]);

			switch (dirCurrent) {

				// embedding and overrides (X1-X9 in the BiDi specs)
			case QChar::DirRLE:
			case QChar::DirRLO:
			case QChar::DirLRE:
			case QChar::DirLRO:
				{
					bool rtl = (dirCurrent == QChar::DirRLE || dirCurrent == QChar::DirRLO);
					hasBidi |= rtl;
					bool override = (dirCurrent == QChar::DirLRO || dirCurrent == QChar::DirRLO);

					unsigned int level = control.level+1;
					if ((level%2 != 0) == rtl) ++level;
					if (level < _MaxBidiLevel) {
						eor = current-1;
						eAppendItems(analysis, sor, eor, control, dir);
						eor = current;
						control.embed(rtl, override);
						QChar::Direction edir = (rtl ? QChar::DirR : QChar::DirL);
						dir = status.eor = edir;
						status.lastStrong = edir;
					}
					break;
				}
			case QChar::DirPDF:
				{
					if (control.canPop()) {
						if (dir != control.direction()) {
							eor = current-1;
							eAppendItems(analysis, sor, eor, control, dir);
							dir = control.direction();
						}
						eor = current;
						eAppendItems(analysis, sor, eor, control, dir);
						control.pdf();
						dir = QChar::DirON; status.eor = QChar::DirON;
						status.last = control.direction();
						if (control.override)
							dir = control.direction();
						else
							dir = QChar::DirON;
						status.lastStrong = control.direction();
					}
					break;
				}

				// strong types
			case QChar::DirL:
				if(dir == QChar::DirON)
					dir = QChar::DirL;
				switch(status.last)
					{
					case QChar::DirL:
						eor = current; status.eor = QChar::DirL; break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0) {
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
						} else {
							eor = current; status.eor = dir;
						}
						break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(dir != QChar::DirL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR) {
								if(status.eor != QChar::DirR) {
									// AN or EN
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirR;
								}
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
							} else {
								if(status.eor != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirL;
								} else {
									eor = current; status.eor = QChar::DirL; break;
								}
							}
						} else {
							eor = current; status.eor = QChar::DirL;
						}
					default:
						break;
					}
				status.lastStrong = QChar::DirL;
				break;
			case QChar::DirAL:
			case QChar::DirR:
				hasBidi = true;
				if(dir == QChar::DirON) dir = QChar::DirR;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0)
							eAppendItems(analysis, sor, eor, control, dir);
						// fall through
					case QChar::DirR:
					case QChar::DirAL:
						dir = QChar::DirR; eor = current; status.eor = QChar::DirR; break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor != QChar::DirR && status.eor != QChar::DirAL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR
							   || status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
								eor = current;
							} else {
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
							}
						} else {
							eor = current; status.eor = QChar::DirR;
						}
					default:
						break;
					}
				status.lastStrong = dirCurrent;
				break;

				// weak types:

			case QChar::DirNSM:
				if (eor == current-1)
					eor = current;
				break;
			case QChar::DirEN:
				// if last strong was AL change EN to AN
				if(status.lastStrong != QChar::DirAL) {
					if(dir == QChar::DirON) {
						if(status.lastStrong == QChar::DirL)
							dir = QChar::DirL;
						else
							dir = QChar::DirEN;
					}
					switch(status.last)
						{
						case QChar::DirET:
							if (status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								dir = QChar::DirAN;
							}
							// fall through
						case QChar::DirEN:
						case QChar::DirL:
							eor = current;
							status.eor = dirCurrent;
							break;
						case QChar::DirR:
						case QChar::DirAL:
						case QChar::DirAN:
							if (eor >= 0)
								eAppendItems(analysis, sor, eor, control, dir);
							else
								eor = current;
							status.eor = QChar::DirEN;
							dir = QChar::DirAN; break;
						case QChar::DirES:
						case QChar::DirCS:
							if(status.eor == QChar::DirEN || dir == QChar::DirAN) {
								eor = current; break;
							}
						case QChar::DirBN:
						case QChar::DirB:
						case QChar::DirS:
						case QChar::DirWS:
						case QChar::DirON:
							if(status.eor == QChar::DirR) {
								// neutrals go to R
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirON; status.eor = QChar::DirEN;
								dir = QChar::DirAN;
							}
							else if(status.eor == QChar::DirL ||
									 (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
								eor = current; status.eor = dirCurrent;
							} else {
								// numbers on both sides, neutrals get right to left direction
								if(dir != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									dir = QChar::DirON; status.eor = QChar::DirON;
									eor = current - 1;
									dir = QChar::DirR;
									eAppendItems(analysis, sor, eor, control, dir);
									dir = QChar::DirON; status.eor = QChar::DirON;
									dir = QChar::DirAN;
								} else {
									eor = current; status.eor = dirCurrent;
								}
							}
						default:
							break;
						}
					break;
				}
			case QChar::DirAN:
				hasBidi = true;
				dirCurrent = QChar::DirAN;
				if(dir == QChar::DirON) dir = QChar::DirAN;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirAN:
						eor = current; status.eor = QChar::DirAN; break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
						if (eor >= 0){
							eAppendItems(analysis, sor, eor, control, dir);
						} else {
							eor = current;
						}
						dir = QChar::DirAN; status.eor = QChar::DirAN;
						break;
					case QChar::DirCS:
						if(status.eor == QChar::DirAN) {
							eor = current; break;
						}
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor == QChar::DirR) {
							// neutrals go to R
							eor = current - 1;
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirAN;
							dir = QChar::DirAN;
						} else if(status.eor == QChar::DirL ||
								   (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
							eor = current; status.eor = dirCurrent;
						} else {
							// numbers on both sides, neutrals get right to left direction
							if(dir != QChar::DirL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								eor = current - 1;
								dir = QChar::DirR;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirAN;
								dir = QChar::DirAN;
							} else {
								eor = current; status.eor = dirCurrent;
							}
						}
					default:
						break;
					}
				break;
			case QChar::DirES:
			case QChar::DirCS:
				break;
			case QChar::DirET:
				if(status.last == QChar::DirEN) {
					dirCurrent = QChar::DirEN;
					eor = current; status.eor = dirCurrent;
				}
				break;

				// boundary neutrals should be ignored
			case QChar::DirBN:
				break;
				// neutrals
			case QChar::DirB:
				// ### what do we do with newline and paragraph separators that come to here?
				break;
			case QChar::DirS:
				// ### implement rule L1
				break;
			case QChar::DirWS:
			case QChar::DirON:
				break;
			default:
				break;
			}

			if(current >= (int)_parLength) break;

			// set status.last as needed.
			switch(dirCurrent) {
			case QChar::DirET:
			case QChar::DirES:
			case QChar::DirCS:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
				switch(status.last)
				{
				case QChar::DirL:
				case QChar::DirR:
				case QChar::DirAL:
				case QChar::DirEN:
				case QChar::DirAN:
					status.last = dirCurrent;
					break;
				default:
					status.last = QChar::DirON;
				}
				break;
			case QChar::DirNSM:
			case QChar::DirBN:
				// ignore these
				break;
			case QChar::DirLRO:
			case QChar::DirLRE:
				status.last = QChar::DirL;
				break;
			case QChar::DirRLO:
			case QChar::DirRLE:
				status.last = QChar::DirR;
				break;
			case QChar::DirEN:
				if (status.last == QChar::DirL) {
					status.last = QChar::DirL;
					break;
				}
				// fall through
			default:
				status.last = dirCurrent;
			}

			++current;
		}

		eor = current - 1; // remove dummy char

		if (sor <= eor)
			eAppendItems(analysis, sor, eor, control, dir);

		return hasBidi;
	}

private:

	QPainter *_p;
	const Text *_t;
	bool _elideLast;
	style::align _align;
	QPen _originalPen;
	int32 _yFrom, _yTo;
	uint16 _selectedFrom, _selectedTo;
	const QChar *_str;

	// current paragraph data
	Text::TextBlocks::const_iterator _parStartBlock;
	Qt::LayoutDirection _parDirection;
	int32 _parStart, _parLength;
	bool _parHasBidi;
	QVarLengthArray<QScriptAnalysis, 4096> _parAnalysis;

	// current line data
	QTextEngine *_e;
	style::font _f;
	QFixed _x, _w, _wLeft;
	int32 _y, _yDelta, _lineHeight, _fontHeight;
	
	// elided hack support
	int32 _blocksSize;
	int32 _elideSavedIndex;
	ITextBlock *_elideSavedBlock;

	int32 _lineStart, _localFrom;
	int32 _lineStartBlock;

	// link and symbol resolve
	QFixed _lnkX;
	int32 _lnkY;
	const TextLinkPtr *_lnkResult;
	bool *_inTextFlag;
	uint16 *_getSymbol;
	bool *_getSymbolAfter, *_getSymbolUpon;

};

const TextParseOptions _defaultOptions = {
	TextParseLinks | TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions _textPlainOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

Text::Text(int32 minResizeWidth) : _minResizeWidth(minResizeWidth), _maxWidth(0), _minHeight(0), _startDir(Qt::LayoutDirectionAuto) {
}

Text::Text(style::font font, const QString &text, const TextParseOptions &options, int32 minResizeWidth, bool richText) : _minResizeWidth(minResizeWidth) {
	if (richText) {
		setRichText(font, text, options);
	} else {
		setText(font, text, options);
	}
}

void Text::setText(style::font font, const QString &text, const TextParseOptions &options) {
	if (!_textStyle) _initDefault();
	_font = font;
	clean();
	
	{
		TextParser parser(this, text, options);
	}

	NewlineBlock *lastNewline = 0;

	int32 lineHeight = 0;
	int32 result = 0, lastNewlineStart = 0;
	QFixed _width = 0, last_rBearing = 0, last_rPadding = 0;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		ITextBlock *b = *i;
		TextBlockType _btype = b->type();
		int32 blockHeight = _blockHeight(b, _font);
		QFixed _rb = _blockRBearing(b);

		if (_btype == TextBlockNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			Qt::LayoutDirection dir = options.dir;
			if (dir == Qt::LayoutDirectionAuto) {
				dir = TextParser::stringDirection(_text, lastNewlineStart, b->from());
			}
			if (lastNewline) {
				lastNewline->_nextDir = dir;
			} else {
				_startDir = dir;
			}
			lastNewlineStart = b->from();
			lastNewline = static_cast<NewlineBlock*>(b);

			_minHeight += lineHeight;
			lineHeight = 0;
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			if (_maxWidth < _width) {
				_maxWidth = _width;
			}
			_width = (b->f_width() - last_rBearing);
			continue;
		}

		_width += b->f_lpadding();
		_width += last_rBearing + (last_rPadding + b->f_width() - _rb);
		lineHeight = qMax(lineHeight, blockHeight);

		last_rBearing = _rb;
		last_rPadding = b->f_rpadding();
		continue;
	}
	Qt::LayoutDirection dir = options.dir;
	if (dir == Qt::LayoutDirectionAuto) {
		dir = TextParser::stringDirection(_text, lastNewlineStart, _text.size());
	}
	if (lastNewline) {
		lastNewline->_nextDir = dir;
	} else {
		_startDir = dir;
	}
	if (_width > 0) {
		if (!lineHeight) lineHeight = _blockHeight(_blocks.back(), _font);
		_minHeight += lineHeight;
		if (_maxWidth < _width) {
			_maxWidth = _width;
		}
	}
}

void Text::setRichText(style::font font, const QString &text, TextParseOptions options, const TextCustomTagsMap &custom) {
	QString parsed;
	parsed.reserve(text.size());
	const QChar *s = text.constData(), *ch = s;
	for (const QChar *b = s, *e = b + text.size(); ch != e; ++ch) {
		if (ch->unicode() == '\\') {
			if (ch > s) parsed.append(s, ch - s);
			s = ch + 1;

			if (s < e) ++ch;
			continue;
		}
		if (ch->unicode() == '[') {
			if (ch > s) parsed.append(s, ch - s);
			s = ch;

			const QChar *tag = ch + 1;
			if (tag >= e) continue;

			bool closing = false, other = false;
			if (tag->unicode() == '/') {
				closing = true;
				if (++tag >= e) continue;
			}

			TextCommands cmd;
			switch (tag->unicode()) {
			case 'b': cmd = closing ? TextCommandNoBold : TextCommandBold; break;
			case 'i': cmd = closing ? TextCommandNoItalic : TextCommandItalic; break;
			case 'u': cmd = closing ? TextCommandNoUnderline : TextCommandUnderline; break;
			default : other = true; break;
			}

			if (!other) {
				if (++tag >= e || tag->unicode() != ']') continue;
				parsed.append(TextCommand).append(QChar(cmd)).append(TextCommand);
				ch = tag;
				s = ch + 1;
				continue;
			}

			if (tag->unicode() != 'a') {
				TextCustomTagsMap::const_iterator i = custom.constFind(*tag);
				if (++tag >= e || tag->unicode() != ']' || i == custom.cend()) continue;
				parsed.append(closing ? i->second : i->first);
				ch = tag;
				s = ch + 1;
				continue;
			}

			if (closing) {
				if (++tag >= e || tag->unicode() != ']') continue;
				parsed.append(textcmdStopLink());
				ch = tag;
				s = ch + 1;
				continue;
			}
			if (++tag >= e || tag->unicode() != ' ') continue;
			while (tag < e && tag->unicode() == ' ') ++tag;
			if (tag + 5 < e && text.midRef(tag - b, 6) == qsl("href=\"")) {
				tag += 6;
				const QChar *tagend = tag;
				while (tagend < e && tagend->unicode() != '"') ++tagend;
				if (++tagend >= e || tagend->unicode() != ']') continue;
				parsed.append(textcmdStartLink(QString(tag, tagend - 1 - tag)));
				ch = tagend;
				s = ch + 1;
				continue;
			}
		}
	}
	if (ch > s) parsed.append(s, ch - s);
	s = ch;

	options.flags |= TextParseRichText;
	setText(font, parsed, options);
}

void Text::setLink(uint16 lnkIndex, const TextLinkPtr &lnk) {
	if (!lnkIndex || lnkIndex > _links.size()) return;
	_links[lnkIndex - 1] = lnk;
}

bool Text::hasLinks() const {
	return !_links.isEmpty();
}

int32 Text::countHeight(int32 w) const {
	QFixed width = w;
	if (width < _minResizeWidth) width = _minResizeWidth;
	if (width >= _maxWidth) {
		return _minHeight;
	}

	int32 result = 0, lineHeight = 0;
	QFixed widthLeft = width, last_rBearing = 0, last_rPadding = 0;
	bool longWordLine = true;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		ITextBlock *b = *i;
		TextBlockType _btype = b->type();
		int32 blockHeight = _blockHeight(b, _font);
		QFixed _rb = _blockRBearing(b);

		if (_btype == TextBlockNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			result += lineHeight;
			lineHeight = 0;
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			widthLeft = width - (b->f_width() - last_rBearing);

			longWordLine = true;
			continue;
		}
		widthLeft -= b->f_lpadding();
		QFixed newWidthLeft = widthLeft - last_rBearing - (last_rPadding + b->f_width() - _rb);
		if (newWidthLeft >= 0) {
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			widthLeft = newWidthLeft;

			lineHeight = qMax(lineHeight, blockHeight);

			longWordLine = false;
			continue;
		}

		if (_btype == TextBlockText) {
			TextBlock *t = static_cast<TextBlock*>(b);
			QFixed f_wLeft = widthLeft;
			int32 f_lineHeight = lineHeight;
			for (TextBlock::TextWords::const_iterator j = t->_words.cbegin(), e = t->_words.cend(), f = j; j != e; ++j) {
				bool wordEndsHere = (j->width >= 0);
				QFixed j_width = wordEndsHere ? j->width : -j->width;

				QFixed newWidthLeft = widthLeft - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
				if (newWidthLeft >= 0) {
					last_rBearing = j->f_rbearing();
					last_rPadding = j->rpadding;
					widthLeft = newWidthLeft;

					lineHeight = qMax(lineHeight, blockHeight);

					if (wordEndsHere) {
						longWordLine = false;
					}
					if (wordEndsHere || longWordLine) {
						f_wLeft = widthLeft;
						f_lineHeight = lineHeight;
						f = j + 1;
					}
					continue;
				}

				if (f != j) {
					j = f;
					widthLeft = f_wLeft;
					lineHeight = f_lineHeight;
					j_width = (j->width >= 0) ? j->width : -j->width;
				}

				result += lineHeight;
				lineHeight = qMax(0, blockHeight);
				last_rBearing = j->f_rbearing();
				last_rPadding = j->rpadding;
				widthLeft = width - (j_width - last_rBearing);

				longWordLine = true;
				f = j + 1;
				f_wLeft = widthLeft;
				f_lineHeight = lineHeight;
			}
			continue;
		}

		result += lineHeight;
		lineHeight = qMax(0, blockHeight);
		last_rBearing = _rb;
		last_rPadding = b->f_rpadding();
		widthLeft = width - (b->f_width() - last_rBearing);

		longWordLine = true;
		continue;
	}
	if (widthLeft < width) {
		result += lineHeight;
	}

	return result;
}

void Text::draw(QPainter &painter, int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, uint16 selectedFrom, uint16 selectedTo) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	TextPainter p(&painter, this);
	p.draw(left, top, w, align, yFrom, yTo, selectedFrom, selectedTo);
}

void Text::drawElided(QPainter &painter, int32 left, int32 top, int32 w, int32 lines, style::align align, int32 yFrom, int32 yTo) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	TextPainter p(&painter, this);
	p.drawElided(left, top, w, align, lines, yFrom, yTo);
}

const TextLinkPtr &Text::link(int32 x, int32 y, int32 width, style::align align) const {
	TextPainter p(0, this);
	return p.link(x, y, width, align);
}

void Text::getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, int32 width, style::align align) const {
	TextPainter p(0, this);
	p.getState(lnk, inText, x, y, width, align);
}

void Text::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y, int32 width, style::align align) const {
	TextPainter p(0, this);
	p.getSymbol(symbol, after, upon, x, y, width, align);
}

uint32 Text::adjustSelection(uint16 from, uint16 to, TextSelectType selectType) const {
	if (from < _text.size() && from <= to) {
		if (to > _text.size()) to = _text.size() - 1;
		if (selectType == TextSelectParagraphs) {
			if (!chIsParagraphSeparator(_text.at(from))) {
				while (from > 0 && !chIsParagraphSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (chIsParagraphSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !chIsParagraphSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		} else if (selectType == TextSelectWords) {
			if (!chIsWordSeparator(_text.at(from))) {
				while (from > 0 && !chIsWordSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (chIsWordSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !chIsWordSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		}
	}
	return (from << 16) | to;
}

QString Text::original(uint16 selectedFrom, uint16 selectedTo, bool expandLinks) const {
	QString result;
	result.reserve(_text.size());

	int32 lnkFrom = 0, lnkIndex = 0;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); true; ++i) {		
		int32 blockLnkIndex = (i == e) ? 0 : (*i)->lnkIndex();
		int32 blockFrom = (i == e) ? _text.size() : (*i)->from();
		if (blockLnkIndex != lnkIndex) {
			if (lnkIndex) { // write link
				const TextLinkPtr &lnk(_links.at(lnkIndex - 1));
				const QString &url(lnk ? lnk->text() : QString());

				int32 rangeFrom = qMax(int32(selectedFrom), lnkFrom), rangeTo = qMin(blockFrom, int32(selectedTo));

				if (rangeTo > rangeFrom) {
					QStringRef r = _text.midRef(rangeFrom, rangeTo - rangeFrom);
					if (url.isEmpty() || !expandLinks || lnkFrom != rangeFrom || blockFrom != rangeTo) {
						result += r;
					} else {
						if (r.size() > 3 && _text.midRef(lnkFrom, r.size() - 3) == url.midRef(0, r.size() - 3)) { // same link
							result += url;
						} else {
							result.append(r).append(qsl(" ( ")).append(url).append(qsl(" )"));
						}
					}
				}
			}
			lnkIndex = blockLnkIndex;
			lnkFrom = blockFrom;
		}
		if (i == e) break;

		TextBlockType type = (*i)->type();
		if (type == TextBlockSkip) continue;

		if (!blockLnkIndex) {
			int32 rangeFrom = qMax(selectedFrom, (*i)->from()), rangeTo = qMin(selectedTo, uint16((*i)->from() + TextPainter::_blockLength(this, i, e)));
			if (rangeTo > rangeFrom) {
				result += _text.midRef(rangeFrom, rangeTo - rangeFrom);
			}
		}
	}
	return result;
}

void Text::clean() {
	for (TextBlocks::iterator i = _blocks.begin(), e = _blocks.end(); i != e; ++i) {
		delete *i;
	}
	_blocks.clear();
	_links.clear();
	_maxWidth = _minHeight = 0;
	_startDir = Qt::LayoutDirectionAuto;
}

// COPIED FROM qtextlayout.cpp AND MODIFIED
namespace {

	struct ScriptLine {
        ScriptLine() : length(0), textWidth(0) {
        }

		int32 length;
		QFixed textWidth;
	};

    struct LineBreakHelper
    {
        LineBreakHelper()
            : glyphCount(0), maxGlyphs(0), currentPosition(0), fontEngine(0), logClusters(0)
        {
        }


        ScriptLine tmpData;
        ScriptLine spaceData;

        QGlyphLayout glyphs;

        int glyphCount;
        int maxGlyphs;
        int currentPosition;
        glyph_t previousGlyph;

        QFixed rightBearing;

        QFontEngine *fontEngine;
        const unsigned short *logClusters;

        inline glyph_t currentGlyph() const
        {
            Q_ASSERT(currentPosition > 0);
            Q_ASSERT(logClusters[currentPosition - 1] < glyphs.numGlyphs);

            return glyphs.glyphs[logClusters[currentPosition - 1]];
        }

        inline void saveCurrentGlyph()
        {
            previousGlyph = 0;
            if (currentPosition > 0 &&
                logClusters[currentPosition - 1] < glyphs.numGlyphs) {
                previousGlyph = currentGlyph(); // needed to calculate right bearing later
            }
        }

        inline void adjustRightBearing(glyph_t glyph)
        {
            qreal rb;
            fontEngine->getGlyphBearings(glyph, 0, &rb);
            rightBearing = qMin(QFixed(), QFixed::fromReal(rb));
        }

        inline void adjustRightBearing()
        {
            if (currentPosition <= 0)
                return;
            adjustRightBearing(currentGlyph());
        }

        inline void adjustPreviousRightBearing()
        {
            if (previousGlyph > 0)
                adjustRightBearing(previousGlyph);
        }

    };

	static inline void addNextCluster(int &pos, int end, ScriptLine &line, int &glyphCount,
									  const QScriptItem &current, const unsigned short *logClusters,
									  const QGlyphLayout &glyphs)
	{
		int glyphPosition = logClusters[pos];
		do { // got to the first next cluster
			++pos;
			++line.length;
		} while (pos < end && logClusters[pos] == glyphPosition);
		do { // calculate the textWidth for the rest of the current cluster.
			if (!glyphs.attributes[glyphPosition].dontPrint)
				line.textWidth += glyphs.advances[glyphPosition];
			++glyphPosition;
		} while (glyphPosition < current.num_glyphs && !glyphs.attributes[glyphPosition].clusterStart);

		Q_ASSERT((pos == end && glyphPosition == current.num_glyphs) || logClusters[pos] == glyphPosition);

		++glyphCount;
	}

} // anonymous namespace

class BlockParser {
public:

	BlockParser(QTextEngine *e, TextBlock *b, QFixed minResizeWidth, int32 blockFrom) : block(b), eng(e) {
		parseWords(minResizeWidth, blockFrom);
	}

	void parseWords(QFixed minResizeWidth, int32 blockFrom) {
		LineBreakHelper lbh;

		lbh.maxGlyphs = INT_MAX;

		int item = -1;
		int newItem = eng->findItem(0);

		style::align alignment = eng->option.alignment();

		const QCharAttributes *attributes = eng->attributes();
		if (!attributes)
			return;
		lbh.currentPosition = 0;
		int end = 0;
		lbh.logClusters = eng->layoutData->logClustersPtr;
		lbh.previousGlyph = 0;

		block->_lpadding = 0;
		block->_words.clear();

		int wordStart = lbh.currentPosition;

		bool addingEachGrapheme = false;
		int lastGraphemeBoundaryPosition = -1;
		ScriptLine lastGraphemeBoundaryLine;

		while (newItem < eng->layoutData->items.size()) {
			if (newItem != item) {
				item = newItem;
				const QScriptItem &current = eng->layoutData->items[item];
				if (!current.num_glyphs) {
					eng->shape(item);
					attributes = eng->attributes();
					if (!attributes)
						return;
					lbh.logClusters = eng->layoutData->logClustersPtr;
				}
				lbh.currentPosition = current.position;
				end = current.position + eng->length(item);
				lbh.glyphs = eng->shapedGlyphs(&current);
				QFontEngine *fontEngine = eng->fontEngine(current);
				if (lbh.fontEngine != fontEngine) {
					lbh.fontEngine = fontEngine;
				}
			}
			const QScriptItem &current = eng->layoutData->items[item];

			if (attributes[lbh.currentPosition].whiteSpace) {
				while (lbh.currentPosition < end && attributes[lbh.currentPosition].whiteSpace)
					addNextCluster(lbh.currentPosition, end, lbh.spaceData, lbh.glyphCount,
								   current, lbh.logClusters, lbh.glyphs);

				if (block->_words.isEmpty()) {
					block->_lpadding = lbh.spaceData.textWidth;
				} else {
					block->_words.back().rpadding += lbh.spaceData.textWidth;
					block->_width += lbh.spaceData.textWidth;
				}
				lbh.spaceData.length = 0;
				lbh.spaceData.textWidth = 0;

				wordStart = lbh.currentPosition;

				addingEachGrapheme = false;
				lastGraphemeBoundaryPosition = -1;
				lastGraphemeBoundaryLine = ScriptLine();
			} else {
				do {
					addNextCluster(lbh.currentPosition, end, lbh.tmpData, lbh.glyphCount,
								   current, lbh.logClusters, lbh.glyphs);

					if (lbh.currentPosition >= eng->layoutData->string.length()
						|| attributes[lbh.currentPosition].whiteSpace
						|| attributes[lbh.currentPosition].lineBreak) {
						lbh.adjustRightBearing();
						block->_words.push_back(TextWord(wordStart + blockFrom, lbh.tmpData.textWidth, qMin(QFixed(), lbh.rightBearing)));
						block->_width += lbh.tmpData.textWidth;
						lbh.tmpData.textWidth = 0;
						lbh.tmpData.length = 0;
						wordStart = lbh.currentPosition;
						break;
					} else if (attributes[lbh.currentPosition].graphemeBoundary) {
						if (!addingEachGrapheme && lbh.tmpData.textWidth > minResizeWidth) {
							if (lastGraphemeBoundaryPosition >= 0) {
								lbh.adjustPreviousRightBearing();
								block->_words.push_back(TextWord(wordStart + blockFrom, -lastGraphemeBoundaryLine.textWidth, qMin(QFixed(), lbh.rightBearing)));
								block->_width += lastGraphemeBoundaryLine.textWidth;
								lbh.tmpData.textWidth -= lastGraphemeBoundaryLine.textWidth;
								lbh.tmpData.length -= lastGraphemeBoundaryLine.length;
								wordStart = lastGraphemeBoundaryPosition;
							}
							addingEachGrapheme = true;
						}
						if (addingEachGrapheme) {
							lbh.adjustRightBearing();
							block->_words.push_back(TextWord(wordStart + blockFrom, -lbh.tmpData.textWidth, qMin(QFixed(), lbh.rightBearing)));
							block->_width += lbh.tmpData.textWidth;
							lbh.tmpData.textWidth = 0;
							lbh.tmpData.length = 0;
							wordStart = lbh.currentPosition;
						} else {
							lastGraphemeBoundaryPosition = lbh.currentPosition;
							lastGraphemeBoundaryLine = lbh.tmpData;
							lbh.saveCurrentGlyph();
						}
					}
				} while (lbh.currentPosition < end);
			}
			if (lbh.currentPosition == end)
				newItem = item + 1;
		}
		if (block->_words.isEmpty()) {
			block->_rpadding = 0;
		} else {
			block->_rpadding = block->_words.back().rpadding;
			block->_width -= block->_rpadding;
			block->_words.squeeze();
		}
	}

private:

	TextBlock *block;
	QTextEngine *eng;

};

TextBlock::TextBlock(const style::font &font, const QString &str, QFixed minResizeWidth, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex) : ITextBlock(font, str, from, length, flags, color, lnkIndex) {
	_flags |= ((TextBlockText & 0x0F) << 8);
	if (length) {
		style::font blockFont = font;
		if (!flags && lnkIndex) {
			// should use textStyle lnkFlags somehow.. not supported
		}
		if (flags & TextBlockBold) blockFont = blockFont->bold();
		if (flags & TextBlockItalic) blockFont = blockFont->italic();
		if (flags & TextBlockUnderline) blockFont = blockFont->underline();

		QStackTextEngine engine(str.mid(_from, length), blockFont->f);
		engine.itemize();

		QTextLayout layout(&engine);
		layout.beginLayout();
		layout.createLine();

		BlockParser parser(&engine, this, minResizeWidth, _from);

		layout.endLayout();
	}
}

EmojiBlock::EmojiBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex, const EmojiData *emoji) : ITextBlock(font, str, from, length, flags, color, lnkIndex), emoji(emoji) {
	_flags |= ((TextBlockEmoji & 0x0F) << 8);
	_width = int(st::emojiSize + 2 * st::emojiPadding);
}

SkipBlock::SkipBlock(const style::font &font, const QString &str, uint16 from, int32 w, int32 h, uint16 lnkIndex) : ITextBlock(font, str, from, 1, 0, style::color(), lnkIndex), _height(h) {
	_flags |= ((TextBlockSkip & 0x0F) << 8);
	_width = w;
}

namespace {
	void regOneProtocol(const QString &protocol) {
		validProtocols.insert(hashCrc32(protocol.constData(), protocol.size() * sizeof(QChar)));
	}
	void regOneTopDomain(const QString &domain) {
		validTopDomains.insert(hashCrc32(domain.constData(), domain.size() * sizeof(QChar)));
	}
	void initLinkSets() {
		regOneProtocol(qsl("itmss")); // itunes
		regOneProtocol(qsl("http"));
		regOneProtocol(qsl("https"));
		regOneProtocol(qsl("ftp"));

		regOneTopDomain(qsl("ac"));
		regOneTopDomain(qsl("ad"));
		regOneTopDomain(qsl("ae"));
		regOneTopDomain(qsl("af"));
		regOneTopDomain(qsl("ag"));
		regOneTopDomain(qsl("ai"));
		regOneTopDomain(qsl("al"));
		regOneTopDomain(qsl("am"));
		regOneTopDomain(qsl("an"));
		regOneTopDomain(qsl("ao"));
		regOneTopDomain(qsl("aq"));
		regOneTopDomain(qsl("ar"));
		regOneTopDomain(qsl("as"));
		regOneTopDomain(qsl("at"));
		regOneTopDomain(qsl("au"));
		regOneTopDomain(qsl("aw"));
		regOneTopDomain(qsl("ax"));
		regOneTopDomain(qsl("az"));
		regOneTopDomain(qsl("ba"));
		regOneTopDomain(qsl("bb"));
		regOneTopDomain(qsl("bd"));
		regOneTopDomain(qsl("be"));
		regOneTopDomain(qsl("bf"));
		regOneTopDomain(qsl("bg"));
		regOneTopDomain(qsl("bh"));
		regOneTopDomain(qsl("bi"));
		regOneTopDomain(qsl("bj"));
		regOneTopDomain(qsl("bm"));
		regOneTopDomain(qsl("bn"));
		regOneTopDomain(qsl("bo"));
		regOneTopDomain(qsl("br"));
		regOneTopDomain(qsl("bs"));
		regOneTopDomain(qsl("bt"));
		regOneTopDomain(qsl("bv"));
		regOneTopDomain(qsl("bw"));
		regOneTopDomain(qsl("by"));
		regOneTopDomain(qsl("bz"));
		regOneTopDomain(qsl("ca"));
		regOneTopDomain(qsl("cc"));
		regOneTopDomain(qsl("cd"));
		regOneTopDomain(qsl("cf"));
		regOneTopDomain(qsl("cg"));
		regOneTopDomain(qsl("ch"));
		regOneTopDomain(qsl("ci"));
		regOneTopDomain(qsl("ck"));
		regOneTopDomain(qsl("cl"));
		regOneTopDomain(qsl("cm"));
		regOneTopDomain(qsl("cn"));
		regOneTopDomain(qsl("co"));
		regOneTopDomain(qsl("cr"));
		regOneTopDomain(qsl("cu"));
		regOneTopDomain(qsl("cv"));
		regOneTopDomain(qsl("cx"));
		regOneTopDomain(qsl("cy"));
		regOneTopDomain(qsl("cz"));
		regOneTopDomain(qsl("de"));
		regOneTopDomain(qsl("dj"));
		regOneTopDomain(qsl("dk"));
		regOneTopDomain(qsl("dm"));
		regOneTopDomain(qsl("do"));
		regOneTopDomain(qsl("dz"));
		regOneTopDomain(qsl("ec"));
		regOneTopDomain(qsl("ee"));
		regOneTopDomain(qsl("eg"));
		regOneTopDomain(qsl("eh"));
		regOneTopDomain(qsl("er"));
		regOneTopDomain(qsl("es"));
		regOneTopDomain(qsl("et"));
		regOneTopDomain(qsl("eu"));
		regOneTopDomain(qsl("fi"));
		regOneTopDomain(qsl("fj"));
		regOneTopDomain(qsl("fk"));
		regOneTopDomain(qsl("fm"));
		regOneTopDomain(qsl("fo"));
		regOneTopDomain(qsl("fr"));
		regOneTopDomain(qsl("ga"));
		regOneTopDomain(qsl("gd"));
		regOneTopDomain(qsl("ge"));
		regOneTopDomain(qsl("gf"));
		regOneTopDomain(qsl("gg"));
		regOneTopDomain(qsl("gh"));
		regOneTopDomain(qsl("gi"));
		regOneTopDomain(qsl("gl"));
		regOneTopDomain(qsl("gm"));
		regOneTopDomain(qsl("gn"));
		regOneTopDomain(qsl("gp"));
		regOneTopDomain(qsl("gq"));
		regOneTopDomain(qsl("gr"));
		regOneTopDomain(qsl("gs"));
		regOneTopDomain(qsl("gt"));
		regOneTopDomain(qsl("gu"));
		regOneTopDomain(qsl("gw"));
		regOneTopDomain(qsl("gy"));
		regOneTopDomain(qsl("hk"));
		regOneTopDomain(qsl("hm"));
		regOneTopDomain(qsl("hn"));
		regOneTopDomain(qsl("hr"));
		regOneTopDomain(qsl("ht"));
		regOneTopDomain(qsl("hu"));
		regOneTopDomain(qsl("id"));
		regOneTopDomain(qsl("ie"));
		regOneTopDomain(qsl("il"));
		regOneTopDomain(qsl("im"));
		regOneTopDomain(qsl("in"));
		regOneTopDomain(qsl("io"));
		regOneTopDomain(qsl("iq"));
		regOneTopDomain(qsl("ir"));
		regOneTopDomain(qsl("is"));
		regOneTopDomain(qsl("it"));
		regOneTopDomain(qsl("je"));
		regOneTopDomain(qsl("jm"));
		regOneTopDomain(qsl("jo"));
		regOneTopDomain(qsl("jp"));
		regOneTopDomain(qsl("ke"));
		regOneTopDomain(qsl("kg"));
		regOneTopDomain(qsl("kh"));
		regOneTopDomain(qsl("ki"));
		regOneTopDomain(qsl("km"));
		regOneTopDomain(qsl("kn"));
		regOneTopDomain(qsl("kp"));
		regOneTopDomain(qsl("kr"));
		regOneTopDomain(qsl("kw"));
		regOneTopDomain(qsl("ky"));
		regOneTopDomain(qsl("kz"));
		regOneTopDomain(qsl("la"));
		regOneTopDomain(qsl("lb"));
		regOneTopDomain(qsl("lc"));
		regOneTopDomain(qsl("li"));
		regOneTopDomain(qsl("lk"));
		regOneTopDomain(qsl("lr"));
		regOneTopDomain(qsl("ls"));
		regOneTopDomain(qsl("lt"));
		regOneTopDomain(qsl("lu"));
		regOneTopDomain(qsl("lv"));
		regOneTopDomain(qsl("ly"));
		regOneTopDomain(qsl("ma"));
		regOneTopDomain(qsl("mc"));
		regOneTopDomain(qsl("md"));
		regOneTopDomain(qsl("me"));
		regOneTopDomain(qsl("mg"));
		regOneTopDomain(qsl("mh"));
		regOneTopDomain(qsl("mk"));
		regOneTopDomain(qsl("ml"));
		regOneTopDomain(qsl("mm"));
		regOneTopDomain(qsl("mn"));
		regOneTopDomain(qsl("mo"));
		regOneTopDomain(qsl("mp"));
		regOneTopDomain(qsl("mq"));
		regOneTopDomain(qsl("mr"));
		regOneTopDomain(qsl("ms"));
		regOneTopDomain(qsl("mt"));
		regOneTopDomain(qsl("mu"));
		regOneTopDomain(qsl("mv"));
		regOneTopDomain(qsl("mw"));
		regOneTopDomain(qsl("mx"));
		regOneTopDomain(qsl("my"));
		regOneTopDomain(qsl("mz"));
		regOneTopDomain(qsl("na"));
		regOneTopDomain(qsl("nc"));
		regOneTopDomain(qsl("ne"));
		regOneTopDomain(qsl("nf"));
		regOneTopDomain(qsl("ng"));
		regOneTopDomain(qsl("ni"));
		regOneTopDomain(qsl("nl"));
		regOneTopDomain(qsl("no"));
		regOneTopDomain(qsl("np"));
		regOneTopDomain(qsl("nr"));
		regOneTopDomain(qsl("nu"));
		regOneTopDomain(qsl("nz"));
		regOneTopDomain(qsl("om"));
		regOneTopDomain(qsl("pa"));
		regOneTopDomain(qsl("pe"));
		regOneTopDomain(qsl("pf"));
		regOneTopDomain(qsl("pg"));
		regOneTopDomain(qsl("ph"));
		regOneTopDomain(qsl("pk"));
		regOneTopDomain(qsl("pl"));
		regOneTopDomain(qsl("pm"));
		regOneTopDomain(qsl("pn"));
		regOneTopDomain(qsl("pr"));
		regOneTopDomain(qsl("ps"));
		regOneTopDomain(qsl("pt"));
		regOneTopDomain(qsl("pw"));
		regOneTopDomain(qsl("py"));
		regOneTopDomain(qsl("qa"));
		regOneTopDomain(qsl("re"));
		regOneTopDomain(qsl("ro"));
		regOneTopDomain(qsl("ru"));
		regOneTopDomain(qsl("rs"));
		regOneTopDomain(qsl("rw"));
		regOneTopDomain(qsl("sa"));
		regOneTopDomain(qsl("sb"));
		regOneTopDomain(qsl("sc"));
		regOneTopDomain(qsl("sd"));
		regOneTopDomain(qsl("se"));
		regOneTopDomain(qsl("sg"));
		regOneTopDomain(qsl("sh"));
		regOneTopDomain(qsl("si"));
		regOneTopDomain(qsl("sj"));
		regOneTopDomain(qsl("sk"));
		regOneTopDomain(qsl("sl"));
		regOneTopDomain(qsl("sm"));
		regOneTopDomain(qsl("sn"));
		regOneTopDomain(qsl("so"));
		regOneTopDomain(qsl("sr"));
		regOneTopDomain(qsl("ss"));
		regOneTopDomain(qsl("st"));
		regOneTopDomain(qsl("su"));
		regOneTopDomain(qsl("sv"));
		regOneTopDomain(qsl("sx"));
		regOneTopDomain(qsl("sy"));
		regOneTopDomain(qsl("sz"));
		regOneTopDomain(qsl("tc"));
		regOneTopDomain(qsl("td"));
		regOneTopDomain(qsl("tf"));
		regOneTopDomain(qsl("tg"));
		regOneTopDomain(qsl("th"));
		regOneTopDomain(qsl("tj"));
		regOneTopDomain(qsl("tk"));
		regOneTopDomain(qsl("tl"));
		regOneTopDomain(qsl("tm"));
		regOneTopDomain(qsl("tn"));
		regOneTopDomain(qsl("to"));
		regOneTopDomain(qsl("tp"));
		regOneTopDomain(qsl("tr"));
		regOneTopDomain(qsl("tt"));
		regOneTopDomain(qsl("tv"));
		regOneTopDomain(qsl("tw"));
		regOneTopDomain(qsl("tz"));
		regOneTopDomain(qsl("ua"));
		regOneTopDomain(qsl("ug"));
		regOneTopDomain(qsl("uk"));
		regOneTopDomain(qsl("um"));
		regOneTopDomain(qsl("us"));
		regOneTopDomain(qsl("uy"));
		regOneTopDomain(qsl("uz"));
		regOneTopDomain(qsl("va"));
		regOneTopDomain(qsl("vc"));
		regOneTopDomain(qsl("ve"));
		regOneTopDomain(qsl("vg"));
		regOneTopDomain(qsl("vi"));
		regOneTopDomain(qsl("vn"));
		regOneTopDomain(qsl("vu"));
		regOneTopDomain(qsl("wf"));
		regOneTopDomain(qsl("ws"));
		regOneTopDomain(qsl("ye"));
		regOneTopDomain(qsl("yt"));
		regOneTopDomain(qsl("yu"));
		regOneTopDomain(qsl("za"));
		regOneTopDomain(qsl("zm"));
		regOneTopDomain(qsl("zw"));
		regOneTopDomain(qsl("arpa"));
		regOneTopDomain(qsl("aero"));
		regOneTopDomain(qsl("asia"));
		regOneTopDomain(qsl("biz"));
		regOneTopDomain(qsl("cat"));
		regOneTopDomain(qsl("com"));
		regOneTopDomain(qsl("coop"));
		regOneTopDomain(qsl("info"));
		regOneTopDomain(qsl("int"));
		regOneTopDomain(qsl("jobs"));
		regOneTopDomain(qsl("mobi"));
		regOneTopDomain(qsl("museum"));
		regOneTopDomain(qsl("name"));
		regOneTopDomain(qsl("net"));
		regOneTopDomain(qsl("org"));
		regOneTopDomain(qsl("post"));
		regOneTopDomain(qsl("pro"));
		regOneTopDomain(qsl("tel"));
		regOneTopDomain(qsl("travel"));
		regOneTopDomain(qsl("xxx"));
		regOneTopDomain(qsl("edu"));
		regOneTopDomain(qsl("gov"));
		regOneTopDomain(qsl("mil"));
		regOneTopDomain(qsl("local"));
		regOneTopDomain(qsl("xn--lgbbat1ad8j"));
		regOneTopDomain(qsl("xn--54b7fta0cc"));
		regOneTopDomain(qsl("xn--fiqs8s"));
		regOneTopDomain(qsl("xn--fiqz9s"));
		regOneTopDomain(qsl("xn--wgbh1c"));
		regOneTopDomain(qsl("xn--node"));
		regOneTopDomain(qsl("xn--j6w193g"));
		regOneTopDomain(qsl("xn--h2brj9c"));
		regOneTopDomain(qsl("xn--mgbbh1a71e"));
		regOneTopDomain(qsl("xn--fpcrj9c3d"));
		regOneTopDomain(qsl("xn--gecrj9c"));
		regOneTopDomain(qsl("xn--s9brj9c"));
		regOneTopDomain(qsl("xn--xkc2dl3a5ee0h"));
		regOneTopDomain(qsl("xn--45brj9c"));
		regOneTopDomain(qsl("xn--mgba3a4f16a"));
		regOneTopDomain(qsl("xn--mgbayh7gpa"));
		regOneTopDomain(qsl("xn--80ao21a"));
		regOneTopDomain(qsl("xn--mgbx4cd0ab"));
		regOneTopDomain(qsl("xn--l1acc"));
		regOneTopDomain(qsl("xn--mgbc0a9azcg"));
		regOneTopDomain(qsl("xn--mgb9awbf"));
		regOneTopDomain(qsl("xn--mgbai9azgqp6j"));
		regOneTopDomain(qsl("xn--ygbi2ammx"));
		regOneTopDomain(qsl("xn--wgbl6a"));
		regOneTopDomain(qsl("xn--p1ai"));
		regOneTopDomain(qsl("xn--mgberp4a5d4ar"));
		regOneTopDomain(qsl("xn--90a3ac"));
		regOneTopDomain(qsl("xn--yfro4i67o"));
		regOneTopDomain(qsl("xn--clchc0ea0b2g2a9gcd"));
		regOneTopDomain(qsl("xn--3e0b707e"));
		regOneTopDomain(qsl("xn--fzc2c9e2c"));
		regOneTopDomain(qsl("xn--xkc2al3hye2a"));
		regOneTopDomain(qsl("xn--mgbtf8fl"));
		regOneTopDomain(qsl("xn--kprw13d"));
		regOneTopDomain(qsl("xn--kpry57d"));
		regOneTopDomain(qsl("xn--o3cw4h"));
		regOneTopDomain(qsl("xn--pgbs0dh"));
		regOneTopDomain(qsl("xn--j1amh"));
		regOneTopDomain(qsl("xn--mgbaam7a8h"));
		regOneTopDomain(qsl("xn--mgb2ddes"));
		regOneTopDomain(qsl("xn--ogbpf8fl"));
		regOneTopDomain(QString::fromUtf8("рф"));
	}

	// accent char list taken from https://github.com/aristus/accent-folding
	inline QChar chNoAccent(int32 code) { 
		switch (code) {
		case 7834: return QChar(97);
		case 193: return QChar(97);
		case 225: return QChar(97);
		case 192: return QChar(97);
		case 224: return QChar(97);
		case 258: return QChar(97);
		case 259: return QChar(97);
		case 7854: return QChar(97);
		case 7855: return QChar(97);
		case 7856: return QChar(97);
		case 7857: return QChar(97);
		case 7860: return QChar(97);
		case 7861: return QChar(97);
		case 7858: return QChar(97);
		case 7859: return QChar(97);
		case 194: return QChar(97);
		case 226: return QChar(97);
		case 7844: return QChar(97);
		case 7845: return QChar(97);
		case 7846: return QChar(97);
		case 7847: return QChar(97);
		case 7850: return QChar(97);
		case 7851: return QChar(97);
		case 7848: return QChar(97);
		case 7849: return QChar(97);
		case 461: return QChar(97);
		case 462: return QChar(97);
		case 197: return QChar(97);
		case 229: return QChar(97);
		case 506: return QChar(97);
		case 507: return QChar(97);
		case 196: return QChar(97);
		case 228: return QChar(97);
		case 478: return QChar(97);
		case 479: return QChar(97);
		case 195: return QChar(97);
		case 227: return QChar(97);
		case 550: return QChar(97);
		case 551: return QChar(97);
		case 480: return QChar(97);
		case 481: return QChar(97);
		case 260: return QChar(97);
		case 261: return QChar(97);
		case 256: return QChar(97);
		case 257: return QChar(97);
		case 7842: return QChar(97);
		case 7843: return QChar(97);
		case 512: return QChar(97);
		case 513: return QChar(97);
		case 514: return QChar(97);
		case 515: return QChar(97);
		case 7840: return QChar(97);
		case 7841: return QChar(97);
		case 7862: return QChar(97);
		case 7863: return QChar(97);
		case 7852: return QChar(97);
		case 7853: return QChar(97);
		case 7680: return QChar(97);
		case 7681: return QChar(97);
		case 570: return QChar(97);
		case 11365: return QChar(97);
		case 508: return QChar(97);
		case 509: return QChar(97);
		case 482: return QChar(97);
		case 483: return QChar(97);
		case 7682: return QChar(98);
		case 7683: return QChar(98);
		case 7684: return QChar(98);
		case 7685: return QChar(98);
		case 7686: return QChar(98);
		case 7687: return QChar(98);
		case 579: return QChar(98);
		case 384: return QChar(98);
		case 7532: return QChar(98);
		case 385: return QChar(98);
		case 595: return QChar(98);
		case 386: return QChar(98);
		case 387: return QChar(98);
		case 262: return QChar(99);
		case 263: return QChar(99);
		case 264: return QChar(99);
		case 265: return QChar(99);
		case 268: return QChar(99);
		case 269: return QChar(99);
		case 266: return QChar(99);
		case 267: return QChar(99);
		case 199: return QChar(99);
		case 231: return QChar(99);
		case 7688: return QChar(99);
		case 7689: return QChar(99);
		case 571: return QChar(99);
		case 572: return QChar(99);
		case 391: return QChar(99);
		case 392: return QChar(99);
		case 597: return QChar(99);
		case 270: return QChar(100);
		case 271: return QChar(100);
		case 7690: return QChar(100);
		case 7691: return QChar(100);
		case 7696: return QChar(100);
		case 7697: return QChar(100);
		case 7692: return QChar(100);
		case 7693: return QChar(100);
		case 7698: return QChar(100);
		case 7699: return QChar(100);
		case 7694: return QChar(100);
		case 7695: return QChar(100);
		case 272: return QChar(100);
		case 273: return QChar(100);
		case 7533: return QChar(100);
		case 393: return QChar(100);
		case 598: return QChar(100);
		case 394: return QChar(100);
		case 599: return QChar(100);
		case 395: return QChar(100);
		case 396: return QChar(100);
		case 545: return QChar(100);
		case 240: return QChar(100);
		case 201: return QChar(101);
		case 399: return QChar(101);
		case 398: return QChar(101);
		case 477: return QChar(101);
		case 233: return QChar(101);
		case 200: return QChar(101);
		case 232: return QChar(101);
		case 276: return QChar(101);
		case 277: return QChar(101);
		case 202: return QChar(101);
		case 234: return QChar(101);
		case 7870: return QChar(101);
		case 7871: return QChar(101);
		case 7872: return QChar(101);
		case 7873: return QChar(101);
		case 7876: return QChar(101);
		case 7877: return QChar(101);
		case 7874: return QChar(101);
		case 7875: return QChar(101);
		case 282: return QChar(101);
		case 283: return QChar(101);
		case 203: return QChar(101);
		case 235: return QChar(101);
		case 7868: return QChar(101);
		case 7869: return QChar(101);
		case 278: return QChar(101);
		case 279: return QChar(101);
		case 552: return QChar(101);
		case 553: return QChar(101);
		case 7708: return QChar(101);
		case 7709: return QChar(101);
		case 280: return QChar(101);
		case 281: return QChar(101);
		case 274: return QChar(101);
		case 275: return QChar(101);
		case 7702: return QChar(101);
		case 7703: return QChar(101);
		case 7700: return QChar(101);
		case 7701: return QChar(101);
		case 7866: return QChar(101);
		case 7867: return QChar(101);
		case 516: return QChar(101);
		case 517: return QChar(101);
		case 518: return QChar(101);
		case 519: return QChar(101);
		case 7864: return QChar(101);
		case 7865: return QChar(101);
		case 7878: return QChar(101);
		case 7879: return QChar(101);
		case 7704: return QChar(101);
		case 7705: return QChar(101);
		case 7706: return QChar(101);
		case 7707: return QChar(101);
		case 582: return QChar(101);
		case 583: return QChar(101);
		case 602: return QChar(101);
		case 605: return QChar(101);
		case 7710: return QChar(102);
		case 7711: return QChar(102);
		case 7534: return QChar(102);
		case 401: return QChar(102);
		case 402: return QChar(102);
		case 500: return QChar(103);
		case 501: return QChar(103);
		case 286: return QChar(103);
		case 287: return QChar(103);
		case 284: return QChar(103);
		case 285: return QChar(103);
		case 486: return QChar(103);
		case 487: return QChar(103);
		case 288: return QChar(103);
		case 289: return QChar(103);
		case 290: return QChar(103);
		case 291: return QChar(103);
		case 7712: return QChar(103);
		case 7713: return QChar(103);
		case 484: return QChar(103);
		case 485: return QChar(103);
		case 403: return QChar(103);
		case 608: return QChar(103);
		case 292: return QChar(104);
		case 293: return QChar(104);
		case 542: return QChar(104);
		case 543: return QChar(104);
		case 7718: return QChar(104);
		case 7719: return QChar(104);
		case 7714: return QChar(104);
		case 7715: return QChar(104);
		case 7720: return QChar(104);
		case 7721: return QChar(104);
		case 7716: return QChar(104);
		case 7717: return QChar(104);
		case 7722: return QChar(104);
		case 7723: return QChar(104);
		case 817: return QChar(104);
		case 7830: return QChar(104);
		case 294: return QChar(104);
		case 295: return QChar(104);
		case 11367: return QChar(104);
		case 11368: return QChar(104);
		case 205: return QChar(105);
		case 237: return QChar(105);
		case 204: return QChar(105);
		case 236: return QChar(105);
		case 300: return QChar(105);
		case 301: return QChar(105);
		case 206: return QChar(105);
		case 238: return QChar(105);
		case 463: return QChar(105);
		case 464: return QChar(105);
		case 207: return QChar(105);
		case 239: return QChar(105);
		case 7726: return QChar(105);
		case 7727: return QChar(105);
		case 296: return QChar(105);
		case 297: return QChar(105);
		case 304: return QChar(105);
		case 302: return QChar(105);
		case 303: return QChar(105);
		case 298: return QChar(105);
		case 299: return QChar(105);
		case 7880: return QChar(105);
		case 7881: return QChar(105);
		case 520: return QChar(105);
		case 521: return QChar(105);
		case 522: return QChar(105);
		case 523: return QChar(105);
		case 7882: return QChar(105);
		case 7883: return QChar(105);
		case 7724: return QChar(105);
		case 7725: return QChar(105);
		case 305: return QChar(105);
		case 407: return QChar(105);
		case 616: return QChar(105);
		case 308: return QChar(106);
		case 309: return QChar(106);
		case 780: return QChar(106);
		case 496: return QChar(106);
		case 567: return QChar(106);
		case 584: return QChar(106);
		case 585: return QChar(106);
		case 669: return QChar(106);
		case 607: return QChar(106);
		case 644: return QChar(106);
		case 7728: return QChar(107);
		case 7729: return QChar(107);
		case 488: return QChar(107);
		case 489: return QChar(107);
		case 310: return QChar(107);
		case 311: return QChar(107);
		case 7730: return QChar(107);
		case 7731: return QChar(107);
		case 7732: return QChar(107);
		case 7733: return QChar(107);
		case 408: return QChar(107);
		case 409: return QChar(107);
		case 11369: return QChar(107);
		case 11370: return QChar(107);
		case 313: return QChar(97);
		case 314: return QChar(108);
		case 317: return QChar(108);
		case 318: return QChar(108);
		case 315: return QChar(108);
		case 316: return QChar(108);
		case 7734: return QChar(108);
		case 7735: return QChar(108);
		case 7736: return QChar(108);
		case 7737: return QChar(108);
		case 7740: return QChar(108);
		case 7741: return QChar(108);
		case 7738: return QChar(108);
		case 7739: return QChar(108);
		case 321: return QChar(108);
		case 322: return QChar(108);
		case 803: return QChar(108);
		case 319: return QChar(108);
		case 320: return QChar(108);
		case 573: return QChar(108);
		case 410: return QChar(108);
		case 11360: return QChar(108);
		case 11361: return QChar(108);
		case 11362: return QChar(108);
		case 619: return QChar(108);
		case 620: return QChar(108);
		case 621: return QChar(108);
		case 564: return QChar(108);
		case 7742: return QChar(109);
		case 7743: return QChar(109);
		case 7744: return QChar(109);
		case 7745: return QChar(109);
		case 7746: return QChar(109);
		case 7747: return QChar(109);
		case 625: return QChar(109);
		case 323: return QChar(110);
		case 324: return QChar(110);
		case 504: return QChar(110);
		case 505: return QChar(110);
		case 327: return QChar(110);
		case 328: return QChar(110);
		case 209: return QChar(110);
		case 241: return QChar(110);
		case 7748: return QChar(110);
		case 7749: return QChar(110);
		case 325: return QChar(110);
		case 326: return QChar(110);
		case 7750: return QChar(110);
		case 7751: return QChar(110);
		case 7754: return QChar(110);
		case 7755: return QChar(110);
		case 7752: return QChar(110);
		case 7753: return QChar(110);
		case 413: return QChar(110);
		case 626: return QChar(110);
		case 544: return QChar(110);
		case 414: return QChar(110);
		case 627: return QChar(110);
		case 565: return QChar(110);
		case 776: return QChar(116);
		case 211: return QChar(111);
		case 243: return QChar(111);
		case 210: return QChar(111);
		case 242: return QChar(111);
		case 334: return QChar(111);
		case 335: return QChar(111);
		case 212: return QChar(111);
		case 244: return QChar(111);
		case 7888: return QChar(111);
		case 7889: return QChar(111);
		case 7890: return QChar(111);
		case 7891: return QChar(111);
		case 7894: return QChar(111);
		case 7895: return QChar(111);
		case 7892: return QChar(111);
		case 7893: return QChar(111);
		case 465: return QChar(111);
		case 466: return QChar(111);
		case 214: return QChar(111);
		case 246: return QChar(111);
		case 554: return QChar(111);
		case 555: return QChar(111);
		case 336: return QChar(111);
		case 337: return QChar(111);
		case 213: return QChar(111);
		case 245: return QChar(111);
		case 7756: return QChar(111);
		case 7757: return QChar(111);
		case 7758: return QChar(111);
		case 7759: return QChar(111);
		case 556: return QChar(111);
		case 557: return QChar(111);
		case 558: return QChar(111);
		case 559: return QChar(111);
		case 560: return QChar(111);
		case 561: return QChar(111);
		case 216: return QChar(111);
		case 248: return QChar(111);
		case 510: return QChar(111);
		case 511: return QChar(111);
		case 490: return QChar(111);
		case 491: return QChar(111);
		case 492: return QChar(111);
		case 493: return QChar(111);
		case 332: return QChar(111);
		case 333: return QChar(111);
		case 7762: return QChar(111);
		case 7763: return QChar(111);
		case 7760: return QChar(111);
		case 7761: return QChar(111);
		case 7886: return QChar(111);
		case 7887: return QChar(111);
		case 524: return QChar(111);
		case 525: return QChar(111);
		case 526: return QChar(111);
		case 527: return QChar(111);
		case 416: return QChar(111);
		case 417: return QChar(111);
		case 7898: return QChar(111);
		case 7899: return QChar(111);
		case 7900: return QChar(111);
		case 7901: return QChar(111);
		case 7904: return QChar(111);
		case 7905: return QChar(111);
		case 7902: return QChar(111);
		case 7903: return QChar(111);
		case 7906: return QChar(111);
		case 7907: return QChar(111);
		case 7884: return QChar(111);
		case 7885: return QChar(111);
		case 7896: return QChar(111);
		case 7897: return QChar(111);
		case 415: return QChar(111);
		case 629: return QChar(111);
		case 7764: return QChar(112);
		case 7765: return QChar(112);
		case 7766: return QChar(112);
		case 7767: return QChar(112);
		case 11363: return QChar(112);
		case 420: return QChar(112);
		case 421: return QChar(112);
		case 771: return QChar(112);
		case 672: return QChar(113);
		case 586: return QChar(113);
		case 587: return QChar(113);
		case 340: return QChar(114);
		case 341: return QChar(114);
		case 344: return QChar(114);
		case 345: return QChar(114);
		case 7768: return QChar(114);
		case 7769: return QChar(114);
		case 342: return QChar(114);
		case 343: return QChar(114);
		case 528: return QChar(114);
		case 529: return QChar(114);
		case 530: return QChar(114);
		case 531: return QChar(114);
		case 7770: return QChar(114);
		case 7771: return QChar(114);
		case 7772: return QChar(114);
		case 7773: return QChar(114);
		case 7774: return QChar(114);
		case 7775: return QChar(114);
		case 588: return QChar(114);
		case 589: return QChar(114);
		case 7538: return QChar(114);
		case 636: return QChar(114);
		case 11364: return QChar(114);
		case 637: return QChar(114);
		case 638: return QChar(114);
		case 7539: return QChar(114);
		case 223: return QChar(115);
		case 346: return QChar(115);
		case 347: return QChar(115);
		case 7780: return QChar(115);
		case 7781: return QChar(115);
		case 348: return QChar(115);
		case 349: return QChar(115);
		case 352: return QChar(115);
		case 353: return QChar(115);
		case 7782: return QChar(115);
		case 7783: return QChar(115);
		case 7776: return QChar(115);
		case 7777: return QChar(115);
		case 7835: return QChar(115);
		case 350: return QChar(115);
		case 351: return QChar(115);
		case 7778: return QChar(115);
		case 7779: return QChar(115);
		case 7784: return QChar(115);
		case 7785: return QChar(115);
		case 536: return QChar(115);
		case 537: return QChar(115);
		case 642: return QChar(115);
		case 809: return QChar(115);
		case 222: return QChar(116);
		case 254: return QChar(116);
		case 356: return QChar(116);
		case 357: return QChar(116);
		case 7831: return QChar(116);
		case 7786: return QChar(116);
		case 7787: return QChar(116);
		case 354: return QChar(116);
		case 355: return QChar(116);
		case 7788: return QChar(116);
		case 7789: return QChar(116);
		case 538: return QChar(116);
		case 539: return QChar(116);
		case 7792: return QChar(116);
		case 7793: return QChar(116);
		case 7790: return QChar(116);
		case 7791: return QChar(116);
		case 358: return QChar(116);
		case 359: return QChar(116);
		case 574: return QChar(116);
		case 11366: return QChar(116);
		case 7541: return QChar(116);
		case 427: return QChar(116);
		case 428: return QChar(116);
		case 429: return QChar(116);
		case 430: return QChar(116);
		case 648: return QChar(116);
		case 566: return QChar(116);
		case 218: return QChar(117);
		case 250: return QChar(117);
		case 217: return QChar(117);
		case 249: return QChar(117);
		case 364: return QChar(117);
		case 365: return QChar(117);
		case 219: return QChar(117);
		case 251: return QChar(117);
		case 467: return QChar(117);
		case 468: return QChar(117);
		case 366: return QChar(117);
		case 367: return QChar(117);
		case 220: return QChar(117);
		case 252: return QChar(117);
		case 471: return QChar(117);
		case 472: return QChar(117);
		case 475: return QChar(117);
		case 476: return QChar(117);
		case 473: return QChar(117);
		case 474: return QChar(117);
		case 469: return QChar(117);
		case 470: return QChar(117);
		case 368: return QChar(117);
		case 369: return QChar(117);
		case 360: return QChar(117);
		case 361: return QChar(117);
		case 7800: return QChar(117);
		case 7801: return QChar(117);
		case 370: return QChar(117);
		case 371: return QChar(117);
		case 362: return QChar(117);
		case 363: return QChar(117);
		case 7802: return QChar(117);
		case 7803: return QChar(117);
		case 7910: return QChar(117);
		case 7911: return QChar(117);
		case 532: return QChar(117);
		case 533: return QChar(117);
		case 534: return QChar(117);
		case 535: return QChar(117);
		case 431: return QChar(117);
		case 432: return QChar(117);
		case 7912: return QChar(117);
		case 7913: return QChar(117);
		case 7914: return QChar(117);
		case 7915: return QChar(117);
		case 7918: return QChar(117);
		case 7919: return QChar(117);
		case 7916: return QChar(117);
		case 7917: return QChar(117);
		case 7920: return QChar(117);
		case 7921: return QChar(117);
		case 7908: return QChar(117);
		case 7909: return QChar(117);
		case 7794: return QChar(117);
		case 7795: return QChar(117);
		case 7798: return QChar(117);
		case 7799: return QChar(117);
		case 7796: return QChar(117);
		case 7797: return QChar(117);
		case 580: return QChar(117);
		case 649: return QChar(117);
		case 7804: return QChar(118);
		case 7805: return QChar(118);
		case 7806: return QChar(118);
		case 7807: return QChar(118);
		case 434: return QChar(118);
		case 651: return QChar(118);
		case 7810: return QChar(119);
		case 7811: return QChar(119);
		case 7808: return QChar(119);
		case 7809: return QChar(119);
		case 372: return QChar(119);
		case 373: return QChar(119);
		case 778: return QChar(121);
		case 7832: return QChar(119);
		case 7812: return QChar(119);
		case 7813: return QChar(119);
		case 7814: return QChar(119);
		case 7815: return QChar(119);
		case 7816: return QChar(119);
		case 7817: return QChar(119);
		case 7820: return QChar(120);
		case 7821: return QChar(120);
		case 7818: return QChar(120);
		case 7819: return QChar(120);
		case 221: return QChar(121);
		case 253: return QChar(121);
		case 7922: return QChar(121);
		case 7923: return QChar(121);
		case 374: return QChar(121);
		case 375: return QChar(121);
		case 7833: return QChar(121);
		case 376: return QChar(121);
		case 255: return QChar(121);
		case 7928: return QChar(121);
		case 7929: return QChar(121);
		case 7822: return QChar(121);
		case 7823: return QChar(121);
		case 562: return QChar(121);
		case 563: return QChar(121);
		case 7926: return QChar(121);
		case 7927: return QChar(121);
		case 7924: return QChar(121);
		case 7925: return QChar(121);
		case 655: return QChar(121);
		case 590: return QChar(121);
		case 591: return QChar(121);
		case 435: return QChar(121);
		case 436: return QChar(121);
		case 377: return QChar(122);
		case 378: return QChar(122);
		case 7824: return QChar(122);
		case 7825: return QChar(122);
		case 381: return QChar(122);
		case 382: return QChar(122);
		case 379: return QChar(122);
		case 380: return QChar(122);
		case 7826: return QChar(122);
		case 7827: return QChar(122);
		case 7828: return QChar(122);
		case 7829: return QChar(122);
		case 437: return QChar(122);
		case 438: return QChar(122);
		case 548: return QChar(122);
		case 549: return QChar(122);
		case 656: return QChar(122);
		case 657: return QChar(122);
		case 11371: return QChar(122);
		case 11372: return QChar(122);
		case 494: return QChar(122);
		case 495: return QChar(122);
		case 442: return QChar(122);
		case 65298: return QChar(50);
		case 65302: return QChar(54);
		case 65314: return QChar(66);
		case 65318: return QChar(70);
		case 65322: return QChar(74);
		case 65326: return QChar(78);
		case 65330: return QChar(82);
		case 65334: return QChar(86);
		case 65338: return QChar(90);
		case 65346: return QChar(98);
		case 65350: return QChar(102);
		case 65354: return QChar(106);
		case 65358: return QChar(110);
		case 65362: return QChar(114);
		case 65366: return QChar(118);
		case 65370: return QChar(122);
		case 65297: return QChar(49);
		case 65301: return QChar(53);
		case 65305: return QChar(57);
		case 65313: return QChar(65);
		case 65317: return QChar(69);
		case 65321: return QChar(73);
		case 65325: return QChar(77);
		case 65329: return QChar(81);
		case 65333: return QChar(85);
		case 65337: return QChar(89);
		case 65345: return QChar(97);
		case 65349: return QChar(101);
		case 65353: return QChar(105);
		case 65357: return QChar(109);
		case 65361: return QChar(113);
		case 65365: return QChar(117);
		case 65369: return QChar(121);
		case 65296: return QChar(48);
		case 65300: return QChar(52);
		case 65304: return QChar(56);
		case 65316: return QChar(68);
		case 65320: return QChar(72);
		case 65324: return QChar(76);
		case 65328: return QChar(80);
		case 65332: return QChar(84);
		case 65336: return QChar(88);
		case 65348: return QChar(100);
		case 65352: return QChar(104);
		case 65356: return QChar(108);
		case 65360: return QChar(112);
		case 65364: return QChar(116);
		case 65368: return QChar(120);
		case 65299: return QChar(51);
		case 65303: return QChar(55);
		case 65315: return QChar(67);
		case 65319: return QChar(71);
		case 65323: return QChar(75);
		case 65327: return QChar(79);
		case 65331: return QChar(83);
		case 65335: return QChar(87);
		case 65347: return QChar(99);
		case 65351: return QChar(103);
		case 65355: return QChar(107);
		case 65359: return QChar(111);
		case 65363: return QChar(115);
		case 65367: return QChar(119);
		default:
			break;
		}
		return QChar(0);
	}
}

QString textAccentFold(const QString &text) {
	QString result(text);
	bool copying = false;
	int32 i = 0;
	for (const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size(); ch != e; ++ch, ++i) {
		if (ch->unicode() < 128) {
			if (copying) result[i] = *ch;
			continue;
		}
		if (chIsDiac(*ch)) {
			copying = true;
			--i;
			continue;
		}
		if (ch->isHighSurrogate() && ch + 1 < e && (ch + 1)->isLowSurrogate()) {
			QChar noAccent = QChar::surrogateToUcs4(*ch, *(ch + 1));
			if (noAccent.unicode() > 0) {
				copying = true;
				result[i] = noAccent;
			} else {
				if (copying) result[i] = *ch;
				++ch, ++i; 
				if (copying) result[i] = *ch;
			}
		} else {
			QChar noAccent = chNoAccent(ch->unicode());
			if (noAccent.unicode() > 0 && noAccent != *ch) {
				result[i] = noAccent;
			} else if (copying) {
				result[i] = *ch;
			}
		}
	}
	return (i < result.size()) ? result.mid(0, i) : result;
}
