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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/animation.h"
#include "ui/button.h"

enum class HitTestType {
	None = 0,
	Client,
	SysButton,
	Icon,
	Caption,
	Top,
	TopRight,
	Right,
	BottomRight,
	Bottom,
	BottomLeft,
	Left,
	TopLeft,
};

class SysBtn : public Button {
public:
	SysBtn(QWidget *parent, const style::sysButton &st, const QString &text = QString());

	void setText(const QString &text);
	void setSysBtnStyle(const style::sysButton &st);

	HitTestType hitTest(const QPoint &p) const;

	void setOverLevel(float64 level);

	void step_color(float64 ms, bool timer);

protected:
	void onStateChanged(int oldState, ButtonStateChangeSource source) override;
	void paintEvent(QPaintEvent *e) override;

	const style::sysButton *_st;
	anim::cvalue a_color;
	Animation _a_color;

	float64 _overLevel = 0.;
	QString _text;

};

class MinimizeBtn : public SysBtn {
public:
	MinimizeBtn(QWidget *parent);

};

class MaximizeBtn : public SysBtn {
public:
	MaximizeBtn(QWidget *parent);

};

class RestoreBtn : public SysBtn {
public:
	RestoreBtn(QWidget *parent);

};

class CloseBtn : public SysBtn {
public:
	CloseBtn(QWidget *parent);

};

class UpdateBtn : public SysBtn {
public:
	UpdateBtn(QWidget *parent);

};

class LockBtn : public SysBtn {
public:
	LockBtn(QWidget *parent);

};
