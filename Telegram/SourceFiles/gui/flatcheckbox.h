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

#include "gui/button.h"

class FlatCheckbox : public Button, public Animated {
	Q_OBJECT

public:

	FlatCheckbox(QWidget *parent, const QString &text, bool checked = false, const style::flatCheckbox &st = st::cbDefFlat);

	bool checked() const;
	void setChecked(bool checked);

	bool animStep(float64 ms);
	void paintEvent(QPaintEvent *e);

	void setOpacity(float64 o);

public slots:

	void onClicked();
	void onStateChange(int oldState, ButtonStateChangeSource source);

signals:

	void changed();

private:

	style::flatCheckbox _st;
	anim::fvalue a_over;
	QString _text;
	style::font _font;

	float64 _opacity;

	bool _checked;

};

class RadiobuttonsGroup;
class FlatRadiobutton : public FlatCheckbox {
	Q_OBJECT

public:

	FlatRadiobutton(QWidget *parent, const QString &group, int32 value, const QString &text, bool checked = false, const style::flatCheckbox &st = st::rbDefFlat);
	int32 val() const {
		return _value;
	}
	~FlatRadiobutton();

public slots:

	void onChanged();

private:

	RadiobuttonsGroup *_group;
	int32 _value;

};
