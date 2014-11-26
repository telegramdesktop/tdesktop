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
#include "stdafx.h"
#include "lang.h"
#include "style.h"

#include "application.h"

#include "intro/introsteps.h"
#include "intro/intro.h"

IntroSteps::IntroSteps(IntroWidget *parent) : IntroStage(parent),
_intro(this, lang(lng_intro), st::introLabel, st::introLabelTextStyle),
_next(this, lang(lng_start_msgs), st::btnIntroNext) {

	_headerWidth = st::introHeaderFont->m.width(lang(lng_maintitle));

	setGeometry(parent->innerRect());

	connect(&_next, SIGNAL(stateChanged(int, ButtonStateChangeSource)), parent, SLOT(onDoneStateChanged(int, ButtonStateChangeSource)));
	connect(&_next, SIGNAL(clicked()), parent, SLOT(onIntroNext()));

	setMouseTracking(true);
}

void IntroSteps::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	int32 hy = _intro.y() - st::introHeaderFont->height - st::introHeaderSkip + st::introHeaderFont->ascent;

	p.setFont(st::introHeaderFont->f);
	p.setPen(st::introColor->p);
	p.drawText((width() - _headerWidth) / 2, hy, lang(lng_maintitle));

	p.drawPixmap(QPoint((width() - st::aboutIcon.pxWidth()) / 2, hy - st::introIconSkip - st::aboutIcon.pxHeight()), App::sprite(), st::aboutIcon);
}

void IntroSteps::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		_next.move((width() - _next.width()) / 2, st::introBtnTop);
		_intro.move((width() - _intro.width()) / 2, _next.y() - _intro.height() - st::introSkip);
	}
}

void IntroSteps::activate() {
	show();
}

void IntroSteps::deactivate() {
	hide();
}

void IntroSteps::onNext() {
	intro()->onIntroNext();
}

void IntroSteps::onBack() {
}
