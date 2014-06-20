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

#include <QtWidgets/QLineEdit>
#include "style.h"
#include "animation.h"

class FlatInput : public QLineEdit, public Animated {
	Q_OBJECT

public:

	FlatInput(QWidget *parent, const style::flatInput &st, const QString &ph = QString(), const QString &val = QString());
	QString val() const;

	bool event(QEvent *e);
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void focusInEvent(QFocusEvent *e);
	void focusOutEvent(QFocusEvent *e);
	void keyPressEvent(QKeyEvent *e);

	void notaBene();

	void updatePlaceholder();

	QRect getTextRect() const;

	bool animStep(float64 ms);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	void customUpDown(bool isCustom);

public slots:

	void onTextChange(const QString &text);
	void onTextEdited();

	void onTouchTimer();

signals:

	void changed();
	void cancelled();
	void accepted();
	void focused();
	void blurred();

protected:

	virtual void correctValue(QKeyEvent *e, const QString &was);

private:

	QString _ph, _oldtext;
	QKeyEvent *_kev;

	bool _customUpDown;

	bool _phVisible;
	anim::ivalue a_phLeft;
	anim::fvalue a_phAlpha;
	anim::cvalue a_phColor, a_borderColor, a_bgColor;

	int _notingBene;
	style::flatInput _st;

	style::font _font;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;
};
