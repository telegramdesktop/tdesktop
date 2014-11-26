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

#include <QtWidgets/QWidget>
#include "gui/flatbutton.h"
#include "intro.h"

class IntroSteps : public IntroStage {
public:

	IntroSteps(IntroWidget *parent);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void activate();
	void deactivate();
	void onNext();
	void onBack();

private:

	FlatLabel _intro;

	FlatButton _next;
	int32 _headerWidth;
};
