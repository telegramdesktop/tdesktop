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

class BackgroundInner : public QWidget, public RPCSender {
	Q_OBJECT

public:

	BackgroundInner();

	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	~BackgroundInner();

signals:

	void backgroundChosen(int index);

private:

	void gotWallpapers(const MTPVector<MTPWallPaper> &result);
	void updateWallpapers();

	int32 _bgCount, _rows;
	int32 _over, _overDown;

};

class BackgroundBox : public ItemListBox {
	Q_OBJECT

public:

	BackgroundBox();
	void paintEvent(QPaintEvent *e);

public slots:

	void onBackgroundChosen(int index);

private:

	BackgroundInner _inner;

};
