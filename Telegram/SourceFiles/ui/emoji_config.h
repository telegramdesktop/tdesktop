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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/text/text.h"

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

inline EmojiPtr emojiFromText(const QChar *ch, const QChar *end, int *outLength = nullptr) {
	EmojiPtr emoji = nullptr;
	if (ch + 1 < end && ((ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) || (((ch->unicode() >= 0x30 && ch->unicode() < 0x3A) || ch->unicode() == 0x23 || ch->unicode() == 0x2A) && (ch + 1)->unicode() == 0x20E3))) {
		uint32 code = (ch->unicode() << 16) | (ch + 1)->unicode();
		emoji = emojiGet(code);
		if (emoji) {
			if (emoji == TwoSymbolEmoji) { // check two symbol
				if (ch + 3 >= end) {
					emoji = 0;
				} else {
					uint32 code2 = ((uint32((ch + 2)->unicode()) << 16) | uint32((ch + 3)->unicode()));
					emoji = emojiGet(code, code2);
				}
			} else {
				if (ch + 2 < end && (ch + 2)->unicode() == 0x200D) { // check sequence
					EmojiPtr seq = emojiGet(ch, end);
					if (seq) {
						emoji = seq;
					}
				}
			}
		}
	} else if (ch + 2 < end && ((ch->unicode() >= 0x30 && ch->unicode() < 0x3A) || ch->unicode() == 0x23 || ch->unicode() == 0x2A) && (ch + 1)->unicode() == 0xFE0F && (ch + 2)->unicode() == 0x20E3) {
		uint32 code = (ch->unicode() << 16) | (ch + 2)->unicode();
		emoji = emojiGet(code);
		if (outLength) *outLength = emoji->len + 1;
		return emoji;
	} else if (ch < end) {
		emoji = emojiGet(ch->unicode());
		t_assert(emoji != TwoSymbolEmoji);
	}

	if (emoji) {
		int32 len = emoji->len + ((ch + emoji->len < end && (ch + emoji->len)->unicode() == 0xFE0F) ? 1 : 0);
		if (emoji->color && (ch + len + 1 < end && (ch + len)->isHighSurrogate() && (ch + len + 1)->isLowSurrogate())) { // color
			uint32 color = ((uint32((ch + len)->unicode()) << 16) | uint32((ch + len + 1)->unicode()));
			EmojiPtr col = emojiGet(emoji, color);
			if (col && col != emoji) {
				len += col->len - emoji->len;
				emoji = col;
				if (ch + len < end && (ch + len)->unicode() == 0xFE0F) {
					++len;
				}
			}
		}
		if (outLength) *outLength = len;
	}

	return emoji;
}

inline EmojiPtr emojiFromText(const QString &text, int32 *plen = 0) {
	return text.isEmpty() ? EmojiPtr(0) : emojiFromText(text.constBegin(), text.constEnd(), plen);
}

inline EmojiPtr emojiGetNoColor(EmojiPtr emoji) {
	if (emoji && emoji->color && (emoji->color & 0xFFFF0000U) != 0xFFFF0000U) {
		EmojiPtr result = emojiGet(emoji->code);
		return (result == TwoSymbolEmoji) ? emojiGet(emoji->code, emoji->code2) : result;
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

inline void appendPartToResult(QString &result, const QChar *start, const QChar *from, const QChar *to, EntitiesInText *inOutEntities) {
	if (to > from) {
		for (auto &entity : *inOutEntities) {
			if (entity.offset() >= to - start) break;
			if (entity.offset() + entity.length() < from - start) continue;
			if (entity.offset() >= from - start) {
				entity.extendToLeft(from - start - result.size());
			}
			if (entity.offset() + entity.length() <= to - start) {
				entity.shrinkFromRight(from - start - result.size());
			}
		}
		result.append(from, to - from);
	}
}

inline QString replaceEmojis(const QString &text, EntitiesInText *inOutEntities) {
	QString result;
	auto currentEntity = inOutEntities->begin(), entitiesEnd = inOutEntities->end();
	const QChar *emojiStart = text.constData(), *emojiEnd = emojiStart, *e = text.constData() + text.size();
	bool canFindEmoji = true;
	for (const QChar *ch = emojiEnd; ch != e;) {
		uint32 emojiCode = 0;
		const QChar *newEmojiEnd = 0;
		if (canFindEmoji) {
			emojiFind(ch, e, newEmojiEnd, emojiCode);
		}

		while (currentEntity != entitiesEnd && ch >= emojiStart + currentEntity->offset() + currentEntity->length()) {
			++currentEntity;
		}
		EmojiPtr emoji = emojiCode ? emojiGet(emojiCode) : 0;
		if (emoji && emoji != TwoSymbolEmoji &&
		    (ch == emojiStart || !ch->isLetterOrNumber() || !(ch - 1)->isLetterOrNumber()) &&
		    (newEmojiEnd == e || !newEmojiEnd->isLetterOrNumber() || newEmojiEnd == emojiStart || !(newEmojiEnd - 1)->isLetterOrNumber()) &&
			(currentEntity == entitiesEnd || (ch < emojiStart + currentEntity->offset() && newEmojiEnd <= emojiStart + currentEntity->offset()) || (ch >= emojiStart + currentEntity->offset() + currentEntity->length() && newEmojiEnd > emojiStart + currentEntity->offset() + currentEntity->length()))
		) {
			if (result.isEmpty()) result.reserve(text.size());

			appendPartToResult(result, emojiStart, emojiEnd, ch, inOutEntities);

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
		} else {
			if (emojiEdge(ch)) {
				canFindEmoji = true;
			} else {
				canFindEmoji = false;
			}
			++ch;
		}
	}
	if (result.isEmpty()) return text;

	appendPartToResult(result, emojiStart, emojiEnd, e, inOutEntities);

	return result;
}

int emojiPackCount(DBIEmojiTab tab);
EmojiPack emojiPack(DBIEmojiTab tab);
