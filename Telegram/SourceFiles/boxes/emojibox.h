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

#include "abstractbox.h"

class EmojiBox : public AbstractBox {
	Q_OBJECT

public:

	EmojiBox();
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);

private:

	void fillBlocks();

	int32 _esize;

	int32 _blockHeight;
	struct Block {
		Block(const EmojiData *emoji = 0, const QString &text = QString()) : emoji(emoji), text(text) {
		}
		const EmojiData *emoji;
		QString text;
	};
	typedef QVector<Block> BlockRow;
	typedef QVector<BlockRow> Blocks;
	Blocks _blocks;
};
