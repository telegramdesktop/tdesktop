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

#include "gui/text.h"

void initEmoji();
EmojiPtr getEmoji(uint32 code);

void findEmoji(const QChar *ch, const QChar *e, const QChar *&newEmojiEnd, uint32 &emojiCode);

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
	LinkRanges lnkRanges = textParseLinks(text);
	int32 currentLink = 0, lnkCount = lnkRanges.size();
	const QChar *emojiStart = text.unicode(), *emojiEnd = emojiStart, *e = text.cend();
	bool canFindEmoji = true, consumePrevious = false;
	for (const QChar *ch = emojiEnd; ch != e;) {
		uint32 emojiCode = 0;
		const QChar *newEmojiEnd = 0;
		if (canFindEmoji) {
			findEmoji(ch, e, newEmojiEnd, emojiCode);
		}
		
		while (currentLink < lnkCount && ch >= lnkRanges[currentLink].from + lnkRanges[currentLink].len) {
			++currentLink;
		}
		if (emojiCode &&
		    (ch == emojiStart || !ch->isLetterOrNumber() || !(ch - 1)->isLetterOrNumber()) &&
		    (newEmojiEnd == e || !newEmojiEnd->isLetterOrNumber() || newEmojiEnd == emojiStart || !(newEmojiEnd - 1)->isLetterOrNumber()) &&
			(currentLink >= lnkCount || (ch < lnkRanges[currentLink].from && newEmojiEnd <= lnkRanges[currentLink].from) || (ch >= lnkRanges[currentLink].from + lnkRanges[currentLink].len && newEmojiEnd > lnkRanges[currentLink].from + lnkRanges[currentLink].len))
		) {
//			if (newEmojiEnd < e && newEmojiEnd->unicode() == ' ') ++newEmojiEnd;
			if (result.isEmpty()) result.reserve(text.size());
			if (ch > emojiEnd + (consumePrevious ? 1 : 0)) {
				result.append(emojiEnd, ch - emojiEnd - (consumePrevious ? 1 : 0));
			}
			if (emojiCode > 65535) {
				result.append(QChar((emojiCode >> 16) & 0xFFFF));
			}
			result.append(QChar(emojiCode & 0xFFFF));

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

EmojiPack emojiPack(DBIEmojiTab tab);
