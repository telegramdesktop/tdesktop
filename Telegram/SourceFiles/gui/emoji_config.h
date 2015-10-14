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

#include "gui/text.h"

void emojiInit();
EmojiPtr emojiGet(uint32 code);
EmojiPtr emojiGet(uint32 code, uint32 code2);
EmojiPtr emojiGet(EmojiPtr emoji, uint32 color);
EmojiPtr emojiGet(const QChar *from, const QChar *end);
QString emojiGetSequence(int index);

inline QString emojiString(EmojiPtr emoji) {
	if ((emoji->code & 0xFFFF0000U) == 0xFFFF0000U) { // sequence
		return emojiGetSequence(emoji->code & 0xFFFFU);
	}

	QString result;
	result.reserve(emoji->len + (emoji->postfix ? 1 : 0));
	if (!(emoji->code >> 16)) {
		result.append(QChar(emoji->code & 0xFFFF));
	} else {
		result.append(QChar((emoji->code >> 16) & 0xFFFF));
		result.append(QChar(emoji->code & 0xFFFF));
		if (emoji->code2) {
			result.append(QChar((emoji->code2 >> 16) & 0xFFFF));
			result.append(QChar(emoji->code2 & 0xFFFF));
		}
	}
	if (emoji->color && ((emoji->color & 0xFFFF0000U) != 0xFFFF0000U)) {
		result.append(QChar((emoji->color >> 16) & 0xFFFF));
		result.append(QChar(emoji->color & 0xFFFF));
	}
	if (emoji->postfix) result.append(QChar(emoji->postfix));
	return result;
}

inline uint64 emojiKey(EmojiPtr emoji) {
	uint64 key = emoji->code;
	if (emoji->code2) {
		key = (key << 32) | uint64(emoji->code2);
	} else if (emoji->color && ((emoji->color & 0xFFFF0000U) != 0xFFFF0000U)) {
		key = (key << 32) | uint64(emoji->color);
	}
	return key;
}

inline EmojiPtr emojiFromKey(uint64 key) {
	uint32 code = uint32(key >> 32), code2 = uint32(key & 0xFFFFFFFFLLU);
	if (!code && code2) {
		code = code2;
		code2 = 0;
	}
	EmojiPtr emoji = emojiGet(code);
	if (emoji == TwoSymbolEmoji) {
		return emojiGet(code, code2);
	} else if (emoji && emoji->color && code2) {
		return emojiGet(emoji, code2);
	}
	return emoji;
}

inline EmojiPtr emojiFromUrl(const QString &url) {
	return emojiFromKey(url.midRef(10).toULongLong(0, 16)); // skip emoji://e.
}

inline EmojiPtr emojiFromText(const QChar *ch, const QChar *e, int &len) {
	EmojiPtr emoji = 0;
	if (ch + 1 < e && ((ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) || (((ch->unicode() >= 48 && ch->unicode() < 58) || ch->unicode() == 35) && (ch + 1)->unicode() == 0x20E3))) {
		uint32 code = (ch->unicode() << 16) | (ch + 1)->unicode();
		emoji = emojiGet(code);
		if (emoji) {
			if (emoji == TwoSymbolEmoji) { // check two symbol
				if (ch + 3 >= e) {
					emoji = 0;
				} else {
					uint32 code2 = ((uint32((ch + 2)->unicode()) << 16) | uint32((ch + 3)->unicode()));
					emoji = emojiGet(code, code2);
				}
			} else {
				if (ch + 2 < e && (ch + 2)->unicode() == 0x200D) { // check sequence
					EmojiPtr seq = emojiGet(ch, e);
					if (seq) {
						emoji = seq;
					}
				}
			}
		}
	} else if (ch < e) {
		emoji = emojiGet(ch->unicode());
		Q_ASSERT(emoji != TwoSymbolEmoji);
	}

	if (emoji) {
		len = emoji->len + ((ch + emoji->len < e && (ch + emoji->len)->unicode() == 0xFE0F) ? 1 : 0);
		if (emoji->color && (ch + len + 1 < e && (ch + len)->isHighSurrogate() && (ch + len + 1)->isLowSurrogate())) { // color
			uint32 color = ((uint32((ch + len)->unicode()) << 16) | uint32((ch + len + 1)->unicode()));
			EmojiPtr col = emojiGet(emoji, color);
			if (col && col != emoji) {
				len += col->len - emoji->len;
				emoji = col;
				if (ch + len < e && (ch + len)->unicode() == 0xFE0F) {
					++len;
				}
			}
		}
	}
	
	return emoji;
}

extern int EmojiSizes[5], EIndex, ESize;
extern const char *EmojiNames[5], *EName;

void emojiFind(const QChar *ch, const QChar *e, const QChar *&newEmojiEnd, uint32 &emojiCode);

inline bool emojiEdge(const QChar *ch) {
	return true;

	switch (ch->unicode()) {
	case '.': case ',': case ':': case ';': case '!': case '?': case '#': case '@':
	case '(': case ')': case '[': case ']': case '{': case '}': case '<': case '>':
	case '+': case '=': case '-': case '_': case '*': case '/': case '\\': case '^': case '$':
	case '"': case '\'':
	case 8212: case 171: case 187: // --, <<, >>
		return true;
	}
	return false;
}

inline QString replaceEmojis(const QString &text) {
	QString result;
	LinksInText links = textParseLinks(text, TextParseLinks | TextParseMentions | TextParseHashtags);
	int32 currentLink = 0, lnkCount = links.size();
	const QChar *emojiStart = text.unicode(), *emojiEnd = emojiStart, *e = text.cend();
	bool canFindEmoji = true, consumePrevious = false;
	for (const QChar *ch = emojiEnd; ch != e;) {
		uint32 emojiCode = 0;
		const QChar *newEmojiEnd = 0;
		if (canFindEmoji) {
			emojiFind(ch, e, newEmojiEnd, emojiCode);
		}
		
		while (currentLink < lnkCount && ch >= emojiStart + links[currentLink].offset + links[currentLink].length) {
			++currentLink;
		}
		EmojiPtr emoji = emojiCode ? emojiGet(emojiCode) : 0;
		if (emoji && emoji != TwoSymbolEmoji &&
		    (ch == emojiStart || !ch->isLetterOrNumber() || !(ch - 1)->isLetterOrNumber()) &&
		    (newEmojiEnd == e || !newEmojiEnd->isLetterOrNumber() || newEmojiEnd == emojiStart || !(newEmojiEnd - 1)->isLetterOrNumber()) &&
			(currentLink >= lnkCount || (ch < emojiStart + links[currentLink].offset && newEmojiEnd <= emojiStart + links[currentLink].offset) || (ch >= emojiStart + links[currentLink].offset + links[currentLink].length && newEmojiEnd > emojiStart + links[currentLink].offset + links[currentLink].length))
		) {
//			if (newEmojiEnd < e && newEmojiEnd->unicode() == ' ') ++newEmojiEnd;
			if (result.isEmpty()) result.reserve(text.size());
			if (ch > emojiEnd + (consumePrevious ? 1 : 0)) {
				result.append(emojiEnd, ch - emojiEnd - (consumePrevious ? 1 : 0));
			}
			if (emoji->color) {
				EmojiColorVariants::const_iterator it = cEmojiVariants().constFind(emoji->code);
				if (it != cEmojiVariants().cend()) {
					EmojiPtr replace = emojiFromKey(it.value());
					if (replace) {
						if (replace != TwoSymbolEmoji && replace->code == emoji->code && replace->code2 == emoji->code2) {
							emoji = replace;
						}
					}
				}
			}
			result.append(emojiString(emoji));

			ch = emojiEnd = newEmojiEnd;
			canFindEmoji = true;
			consumePrevious = false;
		} else {
			if (false && (ch->unicode() == QChar::Space || ch->unicode() == QChar::Nbsp)) {
				canFindEmoji = true;
				consumePrevious = true;
			} else if (emojiEdge(ch)) {
				canFindEmoji = true;
				consumePrevious = false;
			} else {
				canFindEmoji = false;
			}
			++ch;
		}
	}
	if (result.isEmpty()) return text;

	if (emojiEnd < e) result.append(emojiEnd, e - emojiEnd);
	return result;
}

inline QString prepareSentText(QString result) {
	result = result.replace('\t', qsl(" "));

	result = result.replace(" --", QString::fromUtf8(" \xe2\x80\x94"));
	result = result.replace("-- ", QString::fromUtf8("\xe2\x80\x94 "));
	result = result.replace("<<", QString::fromUtf8("\xc2\xab"));
	result = result.replace(">>", QString::fromUtf8("\xc2\xbb"));

	return (cReplaceEmojis() ? replaceEmojis(result) : result).trimmed();
}

int emojiPackCount(DBIEmojiTab tab);
EmojiPack emojiPack(DBIEmojiTab tab);
