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

class BlueTitleShadow : public TWidget {
public:
	BlueTitleShadow(QWidget *parent) : TWidget(parent) {
	}
	void paintEvent(QPaintEvent *e);
};

class BlueTitleClose : public Button {
	Q_OBJECT

public:
	BlueTitleClose(QWidget *parent);
	void paintEvent(QPaintEvent *e);

public slots:

	void onStateChange(int oldState, ButtonStateChangeSource source);

private:
	bool animStep_over(float64 ms);
	anim::cvalue a_iconFg;
	Animation _a_over;

};

class AbstractBox : public LayeredWidget {
	Q_OBJECT

public:

	AbstractBox(int32 w = st::boxWideWidth);
	void parentResized();
	void animStep(float64 ms);
	void keyPressEvent(QKeyEvent *e);
	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);
	void startHide();

	void setBlueTitle(bool blue);
	void raiseShadow();
	
public slots:

	void onClose();

protected:

	void prepare();
	bool paint(QPainter &p);
	void paintTitle(Painter &p, const QString &title, const QString &additional = QString());
	void setMaxHeight(int32 maxHeight);
	void resizeMaxHeight(int32 newWidth, int32 maxHeight);

	virtual void closePressed() {
	}
	virtual void hideAll() {
		if (_blueClose) _blueClose->hide();
		if (_blueShadow) _blueShadow->hide();
	}
	virtual void showAll() {
		if (_blueClose) _blueClose->show();
		if (_blueShadow) _blueShadow->show();
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

	bool _blueTitle;
	BlueTitleClose *_blueClose;
	BlueTitleShadow *_blueShadow;
};

class ScrollableBoxShadow : public TWidget {
public:
	ScrollableBoxShadow(QWidget *parent) : TWidget(parent) {
	}
	void paintEvent(QPaintEvent *e);
};

class ScrollableBox : public AbstractBox {
public:

	ScrollableBox(const style::flatScroll &scroll, int32 w = st::boxWideWidth);
	void resizeEvent(QResizeEvent *e);
	
protected:

	void init(QWidget *inner, int32 bottomSkip = st::boxScrollSkip, int32 topSkip = st::boxTitleHeight);

	virtual void hideAll();
	virtual void showAll();

	ScrollArea _scroll;

private:

	QWidget *_innerPtr;
	int32 _topSkip, _bottomSkip;

};

class ItemListBox : public ScrollableBox {
public:

	ItemListBox(const style::flatScroll &scroll, int32 w = st::boxWideWidth);

};

enum CreatingGroupType {
	CreatingGroupNone,
	CreatingGroupGroup,
	CreatingGroupChannel,
};
