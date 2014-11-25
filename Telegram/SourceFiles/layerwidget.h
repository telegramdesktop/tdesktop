/*
This file is part of Telegram Desktop,
an official desktop messaging app, see https://telegram.org

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

#include "gui/boxshadow.h"

class LayeredWidget : public QWidget {
	Q_OBJECT

public:

	virtual void animStep(float64 ms) {
	}
	virtual void parentResized() = 0;
	virtual void startHide() {
	}

	virtual void resizeEvent(QResizeEvent *e) {
		emit resized();
	}

	virtual QRect boxRect() const {
		QRect res(rect());
		res.moveTopLeft(geometry().topLeft());
		return res;
	}

signals:

	void closed();
	void resized();

};

class BackgroundWidget : public QWidget, public Animated {
	Q_OBJECT

public:

	BackgroundWidget(QWidget *parent, LayeredWidget *w);

	void paintEvent(QPaintEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	void replaceInner(LayeredWidget *n);

	bool animStep(float64 ms);

	~BackgroundWidget();

public slots:

	void onClose();
	bool onInnerClose();

private:

	void startHide();

	LayeredWidget *w, *_hidden;
	anim::fvalue aBackground;
	anim::transition aBackgroundFunc;
	bool hiding;

	BoxShadow shadow;
};
