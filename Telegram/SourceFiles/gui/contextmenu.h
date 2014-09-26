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

#include <QtWidgets/QWidget>

class FlatButton;

class ContextMenu : public QWidget, public Animated {
	Q_OBJECT

public:

	ContextMenu(QWidget *parent);
	FlatButton *addButton(FlatButton *button);
	void resetButtons();

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

	void fastHide();

	bool animStep(float64 ms);

public slots:

	void hideStart();
	void hideFinish();

	void showStart();

private:

	void adjustButtons();

	typedef QVector<FlatButton*> Buttons;
	Buttons _buttons;

	int32 _width, _height;
	bool _hiding;

	anim::fvalue a_opacity;

};