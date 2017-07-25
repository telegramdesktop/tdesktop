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

QString IdFromOldKey(uint64 oldKey);

inline EmojiPtr FromOldKey(uint64 oldKey) {
	return Find(IdFromOldKey(oldKey));
}

inline int ColorIndexFromCode(uint32 code) {
	switch (code) {
	case 0xD83CDFFBU: return 1;
	case 0xD83CDFFCU: return 2;
	case 0xD83CDFFDU: return 3;
	case 0xD83CDFFEU: return 4;
	case 0xD83CDFFFU: return 5;
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

void ReplaceInText(TextWithEntities &result);
RecentEmojiPack &GetRecent();
void AddRecent(EmojiPtr emoji);

} // namespace Emoji
} // namespace Ui
