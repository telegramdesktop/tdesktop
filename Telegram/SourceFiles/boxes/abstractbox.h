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

#include "layerwidget.h"

class AbstractBox : public LayeredWidget {
	Q_OBJECT

public:

	AbstractBox(int32 w = st::boxWideWidth);
	void parentResized();
	void animStep(float64 ms);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void startHide();
	
public slots:

	void onClose();

protected:

	void prepare();
	bool paint(QPainter &p);
	void paintTitle(Painter &p, const QString &title);
	void paintBlueTitle(Painter &p, const QString &title, const QString &additional = QString());
	void paintOldTitle(Painter &p, const QString &title, bool withShadow);
	void paintGrayTitle(QPainter &p, const QString &title);
	void setMaxHeight(int32 maxHeight);
	void resizeMaxHeight(int32 newWidth, int32 maxHeight);

	virtual void closePressed() {
	}
	virtual void hideAll() {
	}
	virtual void showAll() {
	}
	virtual void showDone() {
		setFocus();
	}

private:

	int32 _maxHeight;
	int32 countHeight() const;

	bool _hiding;
	QPixmap _cache;

	anim::fvalue a_opacity;
};

class ScrollableBox : public AbstractBox {
public:

	ScrollableBox(const style::flatScroll &scroll);
	void resizeEvent(QResizeEvent *e);
	
protected:

	void init(QWidget *inner, int32 bottomSkip = 0, int32 topSkip = st::old_boxTitleHeight);

	virtual void hideAll();
	virtual void showAll();

	ScrollArea _scroll;

private:

	QWidget *_innerPtr;
	int32 _topSkip, _bottomSkip;

};

class ItemListBox : public ScrollableBox {
public:

	ItemListBox(const style::flatScroll &scroll);

};
