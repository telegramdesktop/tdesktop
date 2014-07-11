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
		{0xD83DDE0A, ":-)"},
		{0xD83DDE03, ":-D"},
		{0xD83DDE09, ";-)"},
		{0xD83DDE06, "xD"},
		{0xD83DDE1C, ";-P"},
		{0xD83DDE0B, ":-p"},
		{0xD83DDE0D, "8-)"},
		{0xD83DDE0E, "B-)"},
		{0xD83DDE12, ":-("},
		{0xD83DDE0F, ":]"},
		{0xD83DDE14, "3("},
		{0xD83DDE22, ":'("},
		{0xD83DDE2D, ":_("},
		{0xD83DDE29, ":(("},
		{0xD83DDE28, ":o"},
		{0xD83DDE10, ":|"},
		{0xD83DDE0C, "3-)"},
		{0xD83DDE20, ">("},
		{0xD83DDE21, ">(("},
		{0xD83DDE07, "O:)"},
		{0xD83DDE30, ";o"},
		{0xD83DDE33, "8|"},
		{0xD83DDE32, "8o"},
		{0xD83DDE37, ":X"},
		{0xD83DDE1A, ":-*"},
		{0xD83DDE08, "}:)"},
		{0x2764, "<3"},
		{0xD83DDC4D, ":like:"},
		{0xD83DDC4E, ":dislike:"},
		{0x261D, ":up:"},
		{0x270C, ":v:"},
		{0xD83DDC4C, ":ok:"}
	};
	const uint32 replacesCount = sizeof(replaces) / sizeof(EmojiReplace), replacesInRow = 8;
}

EmojiBox::EmojiBox() : _done(this, lang(lng_about_done), st::aboutCloseButton),
    _hiding(false), a_opacity(0, 1) {

	fillBlocks();

	_blockHeight = st::emojiReplaceInnerHeight;
	
	_width = _blocks[0].size() * st::emojiReplaceWidth + (st::emojiReplaceWidth - st::emojiSize);

	_height = st::boxPadding.top() + st::boxFont->height;
	_height += _blocks.size() * st::emojiReplaceHeight + (st::emojiReplaceHeight - _blockHeight);
	_height += _done.height();

	_done.setWidth(_width);
	_header.setText(st::boxFont, lang(lng_settings_emoji_list));

	_done.move(0, _height - _done.height());

	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void EmojiBox::fillBlocks() {
	BlockRow currentRow;
	currentRow.reserve(replacesInRow);
	for (uint32 i = 0; i < replacesCount; ++i) {
		Block block(getEmoji(replaces[i].code), QString::fromUtf8(replaces[i].replace));
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

void EmojiBox::hideAll() {
	_done.hide();
}

void EmojiBox::showAll() {
	_done.show();
}

void EmojiBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onClose();
	} else if (e->key() == Qt::Key_Escape) {
		onClose();
	}
}

void EmojiBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void EmojiBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(0, 0, _width, _height, st::boxBG->b);

			p.setFont(st::boxFont->f);
			p.setPen(st::boxGrayTitle->p);
			_header.draw(p, 0, st::boxPadding.top(), _width, Qt::AlignCenter);

			p.setFont(st::emojiTextFont->f);
			p.setPen(st::black->p);
			int32 top = st::boxPadding.top() + st::boxFont->height + (st::emojiReplaceHeight - _blockHeight) / 2;
			for (Blocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
				int32 rowSize = i->size(), left = (_width - rowSize * st::emojiReplaceWidth) / 2;
				for (BlockRow::const_iterator j = i->cbegin(), en = i->cend(); j != en; ++j) {
					if (j->emoji) {
						QPoint pos(left + (st::emojiReplaceWidth - st::emojiSize) / 2, top + (st::emojiReplaceHeight - _blockHeight) / 2);
						p.drawPixmap(pos, App::emojis(), QRect(j->emoji->x, j->emoji->y, st::emojiImgSize, st::emojiImgSize));
					}
					QRect trect(left, top + (st::emojiReplaceHeight + _blockHeight) / 2 - st::emojiTextFont->height, st::emojiReplaceWidth, st::emojiTextFont->height);
					p.drawText(trect, j->text, QTextOption(Qt::AlignHCenter | Qt::AlignTop));
					left += st::emojiReplaceWidth;
				}
				top += st::emojiReplaceHeight;
			}
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void EmojiBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			setFocus();
		}
	} else {
		a_opacity.update(ms, anim::linear);
	}
	update();
}

void EmojiBox::onClose() {
	emit closed();
}

void EmojiBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

EmojiBox::~EmojiBox() {
}
