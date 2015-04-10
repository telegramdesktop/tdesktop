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
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

class PasscodeWidget : public QWidget, public Animated {
	Q_OBJECT

public:

	PasscodeWidget(QWidget *parent);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void setInnerFocus();

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	bool animStep(float64 ms);

	~PasscodeWidget();

public slots:

	void onParentResize(const QSize &newSize);
	void onError();
	void onChanged();
	void onSubmit();

signals:

private:

	void showAll();
	void hideAll();

	QPixmap _animCache, _bgAnimCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

	FlatInput _passcode;
	FlatButton _submit;
	LinkButton _logout;
	QString _error;

};
