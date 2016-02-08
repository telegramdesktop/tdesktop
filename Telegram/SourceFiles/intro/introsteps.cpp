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
#include "stdafx.h"
#include "lang.h"
#include "style.h"

#include "application.h"

#include "intro/introsteps.h"
#include "intro/intro.h"

#include "langloaderplain.h"

IntroSteps::IntroSteps(IntroWidget *parent) : IntroStage(parent),
_intro(this, lang(lng_intro), st::introLabel, st::introLabelTextStyle),
_changeLang(this, QString()),
_next(this, lang(lng_start_msgs), st::btnIntroNext) {

	_changeLang.hide();
	if (cLang() == languageDefault) {
		int32 l = Sandbox::LangSystem();
		if (l != languageDefault) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[l] + qsl(".strings"), LangLoaderRequest(lng_switch_to_this));
			QString text = loader.found().value(lng_switch_to_this);
			if (!text.isEmpty()) {
				_changeLang.setText(text);
				parent->langChangeTo(l);
				_changeLang.show();
			}
		}
	} else {
		_changeLang.setText(langOriginal(lng_switch_to_this));
		parent->langChangeTo(languageDefault);
		_changeLang.show();
	}

	_headerWidth = st::introHeaderFont->width(qsl("Telegram Desktop"));

	setGeometry(parent->innerRect());

	connect(&_next, SIGNAL(stateChanged(int, ButtonStateChangeSource)), parent, SLOT(onDoneStateChanged(int, ButtonStateChangeSource)));
	connect(&_next, SIGNAL(clicked()), parent, SLOT(onIntroNext()));

	connect(&_changeLang, SIGNAL(clicked()), parent, SLOT(onChangeLang()));

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
	p.drawText((width() - _headerWidth) / 2, hy, qsl("Telegram Desktop"));

	p.drawPixmap(QPoint((width() - st::aboutIcon.pxWidth()) / 2, hy - st::introIconSkip - st::aboutIcon.pxHeight()), App::sprite(), st::aboutIcon);
}

void IntroSteps::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		_next.move((width() - _next.width()) / 2, st::introBtnTop);
		_intro.move((width() - _intro.width()) / 2, _next.y() - _intro.height() - st::introSkip);
		_changeLang.move((width() - _changeLang.width()) / 2, _next.y() + _next.height() + _changeLang.height());
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
