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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "layerwidget.h"

class EmojiBox : public LayeredWidget {
	Q_OBJECT

public:

	EmojiBox();
	void parentResized();
	void animStep(float64 ms);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void startHide();
	~EmojiBox();

public slots:

	void onClose();

private:

	void hideAll();
	void showAll();

	void fillBlocks();

	int32 _width, _height;
	BottomButton _done;

	Text _header;

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

	bool _hiding;
	QPixmap _cache;

	anim::fvalue a_opacity;
};
