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
#include "boxes/emoji_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"

namespace {

struct EmojiReplace {
	uint32 code;
	const char *replace;
};

// copied from codegen_emoji
EmojiReplace Replaces[] = {
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

constexpr auto kReplacesCount = base::array_size(Replaces);
constexpr auto kReplacesInRow = 7;

} // namespace

EmojiBox::EmojiBox(QWidget*) : _esize(Ui::Emoji::Size(Ui::Emoji::Index() + 1)) {
}

void EmojiBox::prepare() {
	setTitle(lang(lng_settings_emoji_list));
	fillBlocks();

	addButton(lang(lng_close), [this] { closeBox(); });

	_blockHeight = st::emojiReplaceInnerHeight;

	setDimensions(_blocks[0].size() * st::emojiReplaceWidth + 2 * st::emojiReplacePadding, st::emojiReplacePadding + _blocks.size() * st::emojiReplaceHeight + (st::emojiReplaceHeight - _blockHeight) + st::emojiReplacePadding);
}

void EmojiBox::fillBlocks() {
	BlockRow currentRow;
	currentRow.reserve(kReplacesInRow);
	for (uint32 i = 0; i < kReplacesCount; ++i) {
		auto emoji = Ui::Emoji::FromOldKey(Replaces[i].code);
		if (!emoji) continue;

		if (emoji->hasVariants()) {
			auto it = cEmojiVariants().constFind(emoji->nonColoredId());
			if (it != cEmojiVariants().cend()) {
				emoji = emoji->variant(it.value());
			}
		}

		Block block(emoji, QString::fromUtf8(Replaces[i].replace));
		currentRow.push_back(block);
        if (uint32(currentRow.size()) == kReplacesInRow) {
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
		closeBox();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void EmojiBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::emojiTextFont);
	p.setPen(st::boxTextFg);
	auto top = st::emojiReplacePadding + (st::emojiReplaceHeight - _blockHeight) / 2;
	for (Blocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		int32 rowSize = i->size(), left = (width() - rowSize * st::emojiReplaceWidth) / 2;
		for (BlockRow::const_iterator j = i->cbegin(), en = i->cend(); j != en; ++j) {
			if (j->emoji) {
				p.drawPixmap(QPoint(left + (st::emojiReplaceWidth - (_esize / cIntRetinaFactor())) / 2, top + (st::emojiReplaceHeight - _blockHeight) / 2), App::emojiLarge(), QRect(j->emoji->x() * _esize, j->emoji->y() * _esize, _esize, _esize));
			}
			QRect trect(left, top + (st::emojiReplaceHeight + _blockHeight) / 2 - st::emojiTextFont->height, st::emojiReplaceWidth, st::emojiTextFont->height);
			p.drawText(trect, j->text, QTextOption(Qt::AlignHCenter | Qt::AlignTop));
			left += st::emojiReplaceWidth;
		}
		top += st::emojiReplaceHeight;
	}
}
