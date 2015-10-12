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
#include "stdafx.h"
#include "lang.h"

#include "emojibox.h"
#include "mainwidget.h"
#include "window.h"

namespace {
	// copied from genemoji.cpp
	struct EmojiReplace {
		uint32 code;
		const char *replace;
	};
	EmojiReplace replaces[] = {
		{ 0xD83DDE0AU, ":-)" },
		{ 0xD83DDE0DU, "8-)" },
		{ 0x2764U, "<3" },
		{ 0xD83DDC8BU, ":kiss:" },
		{ 0xD83DDE01U, ":grin:" },
		{ 0xD83DDE02U, ":joy:" },
		{ 0xD83DDE1AU, ":-*" },
		{ 0xD83DDE06U, "xD" },
		{ 0xD83DDC4DU, ":like:" },
		{ 0xD83DDC4EU, ":dislike:" },
		{ 0x261DU, ":up:" },
		{ 0x270CU, ":v:" },
		{ 0xD83DDC4CU, ":ok:" },
		{ 0xD83DDE0EU, "B-)" },
		{ 0xD83DDE03U, ":-D" },
		{ 0xD83DDE09U, ";-)" },
		{ 0xD83DDE1CU, ";-P" },
		{ 0xD83DDE0BU, ":-p" },
		{ 0xD83DDE14U, "3(" },
		{ 0xD83DDE1EU, ":-(" },
		{ 0xD83DDE0FU, ":]" },
		{ 0xD83DDE22U, ":'(" },
		{ 0xD83DDE2DU, ":_(" },
		{ 0xD83DDE29U, ":((" },
		{ 0xD83DDE28U, ":o" },
		{ 0xD83DDE10U, ":|" },
		{ 0xD83DDE0CU, "3-)" },
		{ 0xD83DDE20U, ">(" },
		{ 0xD83DDE21U, ">((" },
		{ 0xD83DDE07U, "O:)" },
		{ 0xD83DDE30U, ";o" },
		{ 0xD83DDE33U, "8|" },
		{ 0xD83DDE32U, "8o" },
		{ 0xD83DDE37U, ":X" },
		{ 0xD83DDE08U, "}:)" },
	};
	const uint32 replacesCount = sizeof(replaces) / sizeof(EmojiReplace), replacesInRow = 7;
}

EmojiBox::EmojiBox() : _esize(EmojiSizes[EIndex + 1]) {
	setBlueTitle(true);

	fillBlocks();

	_blockHeight = st::emojiReplaceInnerHeight;
	
	resizeMaxHeight(_blocks[0].size() * st::emojiReplaceWidth + 2 * st::emojiReplacePadding, st::boxTitleHeight + st::emojiReplacePadding + _blocks.size() * st::emojiReplaceHeight + (st::emojiReplaceHeight - _blockHeight) + st::emojiReplacePadding);

	prepare();
}

void EmojiBox::fillBlocks() {
	BlockRow currentRow;
	currentRow.reserve(replacesInRow);
	for (uint32 i = 0; i < replacesCount; ++i) {
		EmojiPtr emoji = emojiGet(replaces[i].code);
		if (!emoji || emoji == TwoSymbolEmoji) continue;
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

		Block block(emoji, QString::fromUtf8(replaces[i].replace));
		currentRow.push_back(block);
        if (uint32(currentRow.size()) == replacesInRow) {
			_blocks.push_back(currentRow);
			currentRow.resize(0);
		}
	}
	if (currentRow.size()) {
		_blocks.push_back(currentRow);
	}
}

void EmojiBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onClose();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void EmojiBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_settings_emoji_list));

	p.setFont(st::emojiTextFont->f);
	p.setPen(st::black->p);
	int32 top = st::boxTitleHeight + st::emojiReplacePadding + (st::emojiReplaceHeight - _blockHeight) / 2;
	for (Blocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		int32 rowSize = i->size(), left = (width() - rowSize * st::emojiReplaceWidth) / 2;
		for (BlockRow::const_iterator j = i->cbegin(), en = i->cend(); j != en; ++j) {
			if (j->emoji) {
				p.drawPixmap(QPoint(left + (st::emojiReplaceWidth - _esize) / 2, top + (st::emojiReplaceHeight - _blockHeight) / 2), App::emojiLarge(), QRect(j->emoji->x * _esize, j->emoji->y * _esize, _esize, _esize));
			}
			QRect trect(left, top + (st::emojiReplaceHeight + _blockHeight) / 2 - st::emojiTextFont->height, st::emojiReplaceWidth, st::emojiTextFont->height);
			p.drawText(trect, j->text, QTextOption(Qt::AlignHCenter | Qt::AlignTop));
			left += st::emojiReplaceWidth;
		}
		top += st::emojiReplaceHeight;
	}
}
