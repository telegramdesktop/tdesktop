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
 Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
 */
#pragma once

#include <QtWidgets/QWidget>
#include "gui/boxshadow.h"

class ContextMenu : public TWidget, public Animated {
	Q_OBJECT

public:

	ContextMenu(QWidget *parent, const style::dropdown &st = st::dropdownDef, const style::iconedButton &btnst = st::btnContext);
	QAction *addAction(const QString &text, const QObject *receiver, const char* member);
	void resetActions();

	typedef QVector<QAction*> Actions;
	Actions &actions();

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);
	void keyPressEvent(QKeyEvent *e);

	void focusOutEvent(QFocusEvent *e);

	void fastHide();

	bool animStep(float64 ms);

	void deleteOnHide();
	void popup(const QPoint &p);

	~ContextMenu();

public slots:

	void hideStart();
	void hideFinish();

	void showStart();

	void actionChanged();

	void onActiveChanged();
	void buttonStateChanged(int oldState, ButtonStateChangeSource source);

private:

	void clearActions();
	void adjustButtons();

	typedef QVector<IconedButton*> Buttons;
	Buttons _buttons;

	Actions _actions;

	int32 _width, _height;
	bool _hiding;

	const style::dropdown &_st;
	const style::iconedButton &_btnst;

	BoxShadow _shadow;
	int32 _selected;

	anim::fvalue a_opacity;

	bool _deleteOnHide;

};