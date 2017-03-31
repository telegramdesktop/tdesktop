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
#include "emoji.h"

namespace Ui {
namespace Emoji {

constexpr auto kPostfix = static_cast<ushort>(0xFE0F);
constexpr auto kPanelPerRow = 7;
constexpr auto kPanelRowsPerPage = 6;

void Init();

class One {
	struct CreationTag {
	};

public:
	One(One &&other) = default;
	One(const QString &id, uint16 x, uint16 y, bool hasPostfix, bool colorizable, EmojiPtr original, const CreationTag &)
	: _id(id)
	, _x(x)
	, _y(y)
	, _hasPostfix(hasPostfix)
	, _colorizable(colorizable)
	, _original(original) {
		Expects(!_colorizable || !colored());
	}

	QString id() const {
		return _id;
	}
	QString text() const {
		return hasPostfix() ? (_id + QChar(kPostfix)) : _id;
	}

	bool colored() const {
		return (_original != nullptr);
	}
	EmojiPtr original() const {
		return _original ? _original : this;
	}
	QString nonColoredId() const {
		return original()->id();
	}

	bool hasPostfix() const {
		return _hasPostfix;
	}

	bool hasVariants() const {
		return _colorizable || colored();
	}
	int variantsCount() const;
	int variantIndex(EmojiPtr variant) const;
	EmojiPtr variant(int index) const;

	int index() const;
	QString toUrl() const {
		return qsl("emoji://e.") + QString::number(index());
	}

	int x() const {
		return _x;
	}
	int y() const {
		return _y;
	}

private:
	const QString _id;
	const uint16 _x = 0;
	const uint16 _y = 0;
	const bool _hasPostfix = false;
	const bool _colorizable = false;
	const EmojiPtr _original = nullptr;

	friend void internal::Init();

};

inline EmojiPtr FromUrl(const QString &url) {
	auto start = qstr("emoji://e.");
	if (url.startsWith(start)) {
		return internal::ByIndex(url.midRef(start.size()).toInt()); // skip emoji://e.
	}
	return nullptr;
}

inline EmojiPtr Find(const QChar *start, const QChar *end, int *outLength = nullptr) {
	return internal::Find(start, end, outLength);
}

inline EmojiPtr Find(const QString &text, int *outLength = nullptr) {
	return Find(text.constBegin(), text.constEnd(), outLength);
}

inline QString IdFromOldKey(uint64 oldKey) {
	auto code = uint32(oldKey >> 32);
	auto code2 = uint32(oldKey & 0xFFFFFFFFLLU);
	if (!code && code2) {
		code = base::take(code2);
	}
	if ((code & 0xFFFF0000U) != 0xFFFF0000U) { // code and code2 contain the whole id
		auto result = QString();
		result.reserve(4);
		auto addCode = [&result](uint32 code) {
			if (auto high = (code >> 16)) {
				result.append(QChar(static_cast<ushort>(high & 0xFFFFU)));
			}
			result.append(QChar(static_cast<ushort>(code & 0xFFFFU)));
		};
		addCode(code);
		if (code2) addCode(code2);
		return result;
	}

	// old sequence
	auto sequenceIndex = int(code & 0xFFFFU);
	switch (sequenceIndex) {
	case 0: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 1: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 2: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 3: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 4: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 5: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 6: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 7: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 8: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 9: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 10: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 11: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 12: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6");
	case 13: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7");
	case 14: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa9");
	case 15: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa8");
	case 16: return QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa9");
	case 17: return QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa8");
	case 18: return QString::fromUtf8("\xf0\x9f\x91\x81\xe2\x80\x8d\xf0\x9f\x97\xa8");
	}
	return QString();
}

inline EmojiPtr FromOldKey(uint64 oldKey) {
	return Find(IdFromOldKey(oldKey));
}

inline int ColorIndexFromCode(uint32 code) {
	switch (code) {
	case 0xD83CDFFB: return 1;
	case 0xD83CDFFC: return 2;
	case 0xD83CDFFD: return 3;
	case 0xD83CDFFE: return 4;
	case 0xD83CDFFF: return 5;
	}
	return 0;
}

inline int ColorIndexFromOldKey(uint64 oldKey) {
	return ColorIndexFromCode(uint32(oldKey & 0xFFFFFFFFLLU));
}

inline int Size(int index = Index()) {
	int sizes[] = { 18, 22, 27, 36, 45 };
	return sizes[index];
}

inline QString Filename(int index = Index()) {
	const char *EmojiNames[] = {
		":/gui/art/emoji.webp",
		":/gui/art/emoji_125x.webp",
		":/gui/art/emoji_150x.webp",
		":/gui/art/emoji_200x.webp",
		":/gui/art/emoji_250x.webp",
	};
	return QString::fromLatin1(EmojiNames[index]);
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

inline QString ReplaceInText(const QString &text, EntitiesInText *inOutEntities) {
	auto result = QString();
	auto currentEntity = inOutEntities->begin();
	auto entitiesEnd = inOutEntities->end();
	auto emojiStart = text.constData();
	auto emojiEnd = emojiStart;
	auto end = emojiStart + text.size();
	auto canFindEmoji = true;
	for (auto ch = emojiEnd; ch != end;) {
		auto emojiLength = 0;
		auto emoji = canFindEmoji ? internal::FindReplace(ch, end, &emojiLength) : nullptr;
		auto newEmojiEnd = ch + emojiLength;

		while (currentEntity != entitiesEnd && ch >= emojiStart + currentEntity->offset() + currentEntity->length()) {
			++currentEntity;
		}
		if (emoji &&
		    (ch == emojiStart || !ch->isLetterOrNumber() || !(ch - 1)->isLetterOrNumber()) &&
		    (newEmojiEnd == end || !newEmojiEnd->isLetterOrNumber() || newEmojiEnd == emojiStart || !(newEmojiEnd - 1)->isLetterOrNumber()) &&
			(currentEntity == entitiesEnd || (ch < emojiStart + currentEntity->offset() && newEmojiEnd <= emojiStart + currentEntity->offset()) || (ch >= emojiStart + currentEntity->offset() + currentEntity->length() && newEmojiEnd > emojiStart + currentEntity->offset() + currentEntity->length()))
		) {
			if (result.isEmpty()) result.reserve(text.size());

			appendPartToResult(result, emojiStart, emojiEnd, ch, inOutEntities);

			if (emoji->hasVariants()) {
				auto it = cEmojiVariants().constFind(emoji->nonColoredId());
				if (it != cEmojiVariants().cend()) {
					emoji = emoji->variant(it.value());
				}
			}
			result.append(emoji->text());

			ch = emojiEnd = newEmojiEnd;
			canFindEmoji = true;
		} else {
			if (internal::IsReplaceEdge(ch)) {
				canFindEmoji = true;
			} else {
				canFindEmoji = false;
			}
			++ch;
		}
	}
	if (result.isEmpty()) return text;

	appendPartToResult(result, emojiStart, emojiEnd, end, inOutEntities);

	return result;
}

inline RecentEmojiPack &GetRecent() {
	if (cRecentEmoji().isEmpty()) {
		RecentEmojiPack result;
		auto haveAlready = [&result](EmojiPtr emoji) {
			for (auto &row : result) {
				if (row.first->id() == emoji->id()) {
					return true;
				}
			}
			return false;
		};
		if (!cRecentEmojiPreload().isEmpty()) {
			auto preload = cRecentEmojiPreload();
			cSetRecentEmojiPreload(RecentEmojiPreload());
			result.reserve(preload.size());
			for (auto i = preload.cbegin(), e = preload.cend(); i != e; ++i) {
				if (auto emoji = Ui::Emoji::Find(i->first)) {
					if (!haveAlready(emoji)) {
						result.push_back(qMakePair(emoji, i->second));
					}
				}
			}
		}
		auto defaultRecent = {
			0xD83DDE02LLU,
			0xD83DDE18LLU,
			0x2764LLU,
			0xD83DDE0DLLU,
			0xD83DDE0ALLU,
			0xD83DDE01LLU,
			0xD83DDC4DLLU,
			0x263ALLU,
			0xD83DDE14LLU,
			0xD83DDE04LLU,
			0xD83DDE2DLLU,
			0xD83DDC8BLLU,
			0xD83DDE12LLU,
			0xD83DDE33LLU,
			0xD83DDE1CLLU,
			0xD83DDE48LLU,
			0xD83DDE09LLU,
			0xD83DDE03LLU,
			0xD83DDE22LLU,
			0xD83DDE1DLLU,
			0xD83DDE31LLU,
			0xD83DDE21LLU,
			0xD83DDE0FLLU,
			0xD83DDE1ELLU,
			0xD83DDE05LLU,
			0xD83DDE1ALLU,
			0xD83DDE4ALLU,
			0xD83DDE0CLLU,
			0xD83DDE00LLU,
			0xD83DDE0BLLU,
			0xD83DDE06LLU,
			0xD83DDC4CLLU,
			0xD83DDE10LLU,
			0xD83DDE15LLU,
		};
		for (auto oldKey : defaultRecent) {
			if (result.size() >= kPanelPerRow * kPanelRowsPerPage) break;

			if (auto emoji = Ui::Emoji::FromOldKey(oldKey)) {
				if (!haveAlready(emoji)) {
					result.push_back(qMakePair(emoji, 1));
				}
			}
		}
		cSetRecentEmoji(result);
	}
	return cRefRecentEmoji();
}

} // namespace Emoji
} // namespace Ui
