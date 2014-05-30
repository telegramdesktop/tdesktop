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
#pragma once

#include <QtWidgets/QScrollArea>
#include "style.h"

enum TouchScrollState {
	TouchScrollManual, // Scrolling manually with the finger on the screen
	TouchScrollAuto, // Scrolling automatically
	TouchScrollAcceleration // Scrolling automatically but a finger is on the screen
};

class ScrollArea;

class ScrollShadow : public QWidget {
	Q_OBJECT

public:

	ScrollShadow(ScrollArea *parent, const style::flatScroll *st);

	void paintEvent(QPaintEvent *e);

public slots:

	void changeVisibility(bool shown);

private:

	const style::flatScroll *_st;

};

class ScrollBar : public QWidget, public Animated {
	Q_OBJECT

public:

	ScrollBar(ScrollArea *parent, bool vertical, const style::flatScroll *st);

	void recountSize();

	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	bool animStep(float64 ms);

	void hideTimeout(int64 dt);

public slots:

	void updateBar();
	void onHideTimer();

signals:

	void topShadowVisibility(bool);
	void bottomShadowVisibility(bool);

private:

	ScrollArea *_area;
	const style::flatScroll *_st;

	bool _vertical;
	bool _over, _overbar, _moving;
	bool _topSh, _bottomSh;

	QPoint _dragStart;
	QScrollBar *_connected;

	int32 _startFrom, _scrollMax;

	int64 _hideIn;
	QTimer _hideTimer;

	anim::cvalue a_bg, a_bar;
	QRect _bar;
};

class ScrollArea : public QScrollArea {
	Q_OBJECT

public:

	ScrollArea(QWidget *parent, const style::flatScroll &st = st::scrollDef, bool handleTouch = true);

	bool viewportEvent(QEvent *e);
	void touchEvent(QTouchEvent *e);

	bool eventFilter(QObject *obj, QEvent *e);

	void resizeEvent(QResizeEvent *e);
	void moveEvent(QMoveEvent *e);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	int scrollWidth() const;
	int scrollHeight() const;
	int scrollLeftMax() const;
	int scrollTopMax() const;
	int scrollLeft() const;
	int scrollTop() const;

	void setWidget(QWidget *widget);

	void rangeChanged(int oldMax, int newMax, bool vertical);

public slots:

	void scrollToY(int toTop, int toBottom = -1);
	void onScrolled();

	void onTouchTimer();
	void onTouchScrollTimer();

signals:

	void scrolled();
	void scrollStarted();
	void scrollFinished();
	void geometryChanged();

private:

	bool touchScroll(const QPoint &delta);

	void touchScrollUpdated(const QPoint &screenPos);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	style::flatScroll _st;
	ScrollBar hor, vert;
	ScrollShadow topSh, bottomSh;
	int32 _horValue, _vertValue;

	bool _touchEnabled;
	QTimer _touchTimer;
	bool _touchScroll, _touchPress, _touchRightButton;
	QPoint _touchStart, _touchPrevPos, _touchPos;

	TouchScrollState _touchScrollState;
	bool _touchPrevPosValid, _touchWaitingAcceleration;
	QPoint _touchSpeed;
	uint64 _touchSpeedTime, _touchAccelerationTime, _touchTime;
	QTimer _touchScrollTimer;

	bool _widgetAcceptsTouch;

};
