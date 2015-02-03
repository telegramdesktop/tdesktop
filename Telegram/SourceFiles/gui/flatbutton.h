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

#include "gui/button.h"
#include "gui/flatcheckbox.h"
#include "gui/animation.h"
#include "style.h"

class FlatButton : public Button, public Animated {
	Q_OBJECT

public:

	FlatButton(QWidget *parent, const QString &text, const style::flatButton &st);

	bool animStep(float64 ms);
	void paintEvent(QPaintEvent *e);
	void setOpacity(float64 o);

	void setText(const QString &text);
	void setWidth(int32 w);

	int32 textWidth() const;

	~FlatButton() {
	}

public slots:

	void onStateChange(int oldState, ButtonStateChangeSource source);

private:

	QString _text;
	int32 _textWidth;

	style::flatButton _st;
	anim::cvalue a_bg, a_text;

	float64 _opacity;
};

class BottomButton : public FlatButton {
public:

	BottomButton(QWidget *parent, const QString &text, const style::flatButton &st);
	void paintEvent(QPaintEvent *e);

};

class LinkButton : public Button {
	Q_OBJECT

public:

	LinkButton(QWidget *parent, const QString &text, const style::linkButton &st = st::btnDefLink);

	void paintEvent(QPaintEvent *e);

	void setText(const QString &text);

	~LinkButton();

public slots:

	void onStateChange(int oldState, ButtonStateChangeSource source);

private:

	QString _text;
	style::linkButton _st;
};

class IconedButton : public Button, public Animated {
	Q_OBJECT

public:

	IconedButton(QWidget *parent, const style::iconedButton &st, const QString &text = QString());

	bool animStep(float64 ms);
	void paintEvent(QPaintEvent *e);

	void setOpacity(float64 o);

	void setText(const QString &text);

public slots:

	void onStateChange(int oldState, ButtonStateChangeSource source);

protected:

	QString _text;

	style::iconedButton _st;
	int32 _width;

	anim::fvalue a_opacity;
	anim::cvalue a_bg;

	float64 _opacity;
};

class MaskedButton : public IconedButton {
	Q_OBJECT

public:

	MaskedButton(QWidget *parent, const style::iconedButton &st, const QString &text = QString());

	void paintEvent(QPaintEvent *e);

};
