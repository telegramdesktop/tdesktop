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
#include "gui/twidget.h"

class Switcher : public TWidget, public Animated {
	Q_OBJECT

public:
	Switcher(QWidget *parent, const style::switcher &st);

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

	void paintEvent(QPaintEvent *e);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	void addButton(const QString &btn);

	bool animStep(float64 ms);

	int selected() const;
	void setSelected(int selected);

signals:

	void changed();

private:

	void setOver(int over);

	int _selected;
	int _over, _wasOver, _pressed;

	typedef QVector<QString> Buttons;
	Buttons _buttons;

	style::switcher _st;
	anim::cvalue a_bgOver, a_bgWasOver;

};
