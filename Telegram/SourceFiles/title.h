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

#include <QtWidgets/QWidget>
#include "sysbuttons.h"

class Window;

class TitleHider : public QWidget {
public:

	TitleHider(QWidget *parent);
	void paintEvent(QPaintEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void setLevel(float64 level);

private:

	float64 _level;

};

class TitleWidget : public TWidget {
	Q_OBJECT

public:

	TitleWidget(Window *parent);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void updateBackButton();
	void updateWideMode();
	void updateCounter();

	void mousePressEvent(QMouseEvent *e);
	void mouseDoubleClickEvent(QMouseEvent *e);

	void maximizedChanged(bool maximized, bool force = false);

	HitTestType hitTest(const QPoint &p);

	void setHideLevel(float64 level);

	void step_update(float64 ms, bool timer);

	~TitleWidget();

public slots:

	void stateChanged(Qt::WindowState state = Qt::WindowNoState);
	void showUpdateBtn();
	void onContacts();
	void onAbout();

signals:

	void hiderClicked();

private:

	Window *wnd;

	style::color statusColor;

	float64 hideLevel;
	TitleHider *hider;

	float64 _lastUpdateMs;

	MaskedButton _back;
	FlatButton _cancel, _settings, _contacts, _about;

	LockBtn _lock;
	UpdateBtn _update;
	MinimizeBtn _minimize;
	MaximizeBtn _maximize;
	RestoreBtn _restore;
	CloseBtn _close;

	Animation _a_update;

	bool lastMaximized;

	QPixmap _counter;

};
