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
#include "passcodewidget.h"

#include "lang.h"
#include "localstorage.h"
#include "mainwindow.h"
#include "application.h"
#include "ui/text/text.h"

PasscodeWidget::PasscodeWidget(QWidget *parent) : TWidget(parent)
, _a_show(animation(this, &PasscodeWidget::step_show))
, _passcode(this, st::passcodeInput)
, _submit(this, lang(lng_passcode_submit), st::passcodeSubmit)
, _logout(this, lang(lng_passcode_logout)) {
	setGeometry(QRect(0, st::titleHeight, App::wnd()->width(), App::wnd()->height() - st::titleHeight));
	connect(App::wnd(), SIGNAL(resized(const QSize&)), this, SLOT(onParentResize(const QSize&)));

	_passcode.setEchoMode(QLineEdit::Password);
	connect(&_submit, SIGNAL(clicked()), this, SLOT(onSubmit()));

	connect(&_passcode, SIGNAL(changed()), this, SLOT(onChanged()));
	connect(&_passcode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	connect(&_logout, SIGNAL(clicked()), App::wnd(), SLOT(onLogout()));

	show();
	_passcode.setFocus();
}

void PasscodeWidget::onParentResize(const QSize &newSize) {
	resize(newSize);
}

void PasscodeWidget::onSubmit() {
	if (_passcode.text().isEmpty()) {
		_passcode.notaBene();
		return;
	}
	if (!passcodeCanTry()) {
		_error = lang(lng_flood_error);
		_passcode.notaBene();
		update();
		return;
	}

	if (App::main()) {
		if (Local::checkPasscode(_passcode.text().toUtf8())) {
			cSetPasscodeBadTries(0);
			App::wnd()->clearPasscode();
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			onError();
			return;
		}
	} else {
		if (Local::readMap(_passcode.text().toUtf8()) != Local::ReadMapPassNeeded) {
			cSetPasscodeBadTries(0);

			MTP::start();
			if (MTP::authedId()) {
				App::wnd()->setupMain(true);
			} else {
				App::wnd()->setupIntro(true);
			}

			App::app()->checkMapVersion();
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			onError();
			return;
		}
	}
}

void PasscodeWidget::onError() {
	_error = lang(lng_passcode_wrong);
	_passcode.selectAll();
	_passcode.notaBene();
	update();
}

void PasscodeWidget::onChanged() {
	if (!_error.isEmpty()) {
		_error = QString();
		update();
	}
}

void PasscodeWidget::animShow(const QPixmap &bgAnimCache, bool back) {
	if (App::app()) App::app()->mtpPause();

	(back ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.stop();

	showAll();
	(back ? _cacheUnder : _cacheOver) = myGrab(this);
	hideAll();

	a_coordUnder = back ? anim::ivalue(-st::slideShift, 0) : anim::ivalue(0, -st::slideShift);
	a_coordOver = back ? anim::ivalue(0, width()) : anim::ivalue(width(), 0);
	a_shadow = back ? anim::fvalue(1, 0) : anim::fvalue(0, 1);
	_a_show.start();

	show();
}

void PasscodeWidget::step_show(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		_a_show.stop();

		a_coordUnder.finish();
		a_coordOver.finish();
		a_shadow.finish();

		_cacheUnder = _cacheOver = QPixmap();

		showAll();
		if (App::wnd()) App::wnd()->setInnerFocus();

		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordUnder.update(dt, st::slideFunction);
		a_coordOver.update(dt, st::slideFunction);
		a_shadow.update(dt, st::slideFunction);
	}
	if (timer) update();
}

void PasscodeWidget::stop_show() {
	_a_show.stop();
}

void PasscodeWidget::showAll() {
	_passcode.show();
	_submit.show();
	_logout.show();
}

void PasscodeWidget::hideAll() {
	_passcode.hide();
	_submit.hide();
	_logout.hide();
}

void PasscodeWidget::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());
	setMouseTracking(true);

	Painter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}

	if (_a_show.animating()) {
		if (a_coordOver.current() > 0) {
			p.drawPixmap(QRect(0, 0, a_coordOver.current(), height()), _cacheUnder, QRect(-a_coordUnder.current() * cRetinaFactor(), 0, a_coordOver.current() * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(a_shadow.current() * st::slideFadeOut);
			p.fillRect(0, 0, a_coordOver.current(), height(), st::white->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow.rect());
	} else {
		p.fillRect(rect(), st::setBG->b);

		p.setFont(st::passcodeHeaderFont->f);
		p.drawText(QRect(0, _passcode.y() - st::passcodeHeaderHeight, width(), st::passcodeHeaderHeight), lang(lng_passcode_enter), style::al_center);

		if (!_error.isEmpty()) {
			p.setFont(st::boxTextFont->f);
			p.setPen(st::setErrColor->p);
			p.drawText(QRect(0, _passcode.y() + _passcode.height(), width(), st::passcodeSubmitSkip), _error, style::al_center);
		}
	}
}

void PasscodeWidget::resizeEvent(QResizeEvent *e) {
	_passcode.move((width() - _passcode.width()) / 2, (height() / 3));
	_submit.move(_passcode.x(), _passcode.y() + _passcode.height() + st::passcodeSubmitSkip);
	_logout.move(_passcode.x() + (_passcode.width() - _logout.width()) / 2, _submit.y() + _submit.height() + st::linkFont->ascent);
}

void PasscodeWidget::mousePressEvent(QMouseEvent *e) {

}

void PasscodeWidget::keyPressEvent(QKeyEvent *e) {
}

void PasscodeWidget::setInnerFocus() {
	_passcode.setFocus();
}

PasscodeWidget::~PasscodeWidget() {
}
