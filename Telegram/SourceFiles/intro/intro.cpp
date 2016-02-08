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

#include "localstorage.h"

#include "intro/intro.h"
#include "intro/introsteps.h"
#include "intro/introphone.h"
#include "intro/introcode.h"
#include "intro/introsignup.h"
#include "intro/intropwdcheck.h"
#include "mainwidget.h"
#include "window.h"
#include "application.h"
#include "gui/text.h"

namespace {
	IntroWidget *signalEmitOn = 0;
	QString countryForReg;
	void gotNearestDC(const MTPNearestDc &result) {
		const MTPDnearestDc &nearest(result.c_nearestDc());
		DEBUG_LOG(("Got nearest dc, country: %1, nearest: %2, this: %3").arg(nearest.vcountry.c_string().v.c_str()).arg(nearest.vnearest_dc.v).arg(nearest.vthis_dc.v));
		MTP::setdc(result.c_nearestDc().vnearest_dc.v, true);
		if (countryForReg != nearest.vcountry.c_string().v.c_str()) {
			countryForReg = nearest.vcountry.c_string().v.c_str();
			emit signalEmitOn->countryChanged();
		}
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
		Sandbox::startUpdateCheck();
#endif
	}
}

IntroWidget::IntroWidget(Window *window) : TWidget(window)
, _langChangeTo(0)
, _a_stage(animation(this, &IntroWidget::step_stage))
, _cacheHideIndex(0)
, _cacheShowIndex(0)
, _a_show(animation(this, &IntroWidget::step_show))
, steps(new IntroSteps(this))
, phone(0)
, code(0)
, signup(0)
, pwdcheck(0)
, current(0)
, moving(0)
, _callTimeout(60)
, _registered(false)
, _hasRecovery(false)
, _codeByTelegram(false)
, _back(this, st::setClose)
, _backFrom(0)
, _backTo(0) {
	setGeometry(QRect(0, st::titleHeight, App::wnd()->width(), App::wnd()->height() - st::titleHeight));

	connect(&_back, SIGNAL(clicked()), this, SLOT(onIntroBack()));
	_back.hide();

	countryForReg = psCurrentCountry();

	MTP::send(MTPhelp_GetNearestDc(), rpcDone(gotNearestDC));
	signalEmitOn = this;

	stages[0] = steps;
	memset(stages + 1, 0, sizeof(QWidget*) * 3);
	_back.raise();

	connect(window, SIGNAL(resized(const QSize&)), this, SLOT(onParentResize(const QSize&)));

	show();
	setFocus();

	cSetPasswordRecovered(false);

	_back.move(st::setClosePos.x(), st::setClosePos.y());
}

void IntroWidget::langChangeTo(int32 langId) {
	_langChangeTo = langId;
}

void IntroWidget::onChangeLang() {
	cSetLang(_langChangeTo);
	Local::writeSettings();
	cSetRestarting(true);
	cSetRestartingToSettings(false);
	App::quit();
}

void IntroWidget::onParentResize(const QSize &newSize) {
	resize(newSize);
}

void IntroWidget::onIntroBack() {
	if (!current) return;
	moving = (current == 4) ? -2 : -1;
	prepareMove();
}

void IntroWidget::onIntroNext() {
	if (!createNext()) return;
	moving = 1;
	prepareMove();
}

bool IntroWidget::createNext() {
	if (current == sizeof(stages) / sizeof(stages[0]) - 1) return false;
	if (!stages[current + 1]) {
		switch (current) {
		case 0: stages[current + 1] = phone = new IntroPhone(this); break;
		case 1: stages[current + 1] = code = new IntroCode(this); break;
		case 2:
			if (_pwdSalt.isEmpty()) {
				if (signup) delete signup;
				stages[current + 1] = signup = new IntroSignup(this);
			} else {
				stages[current + 1] = pwdcheck = new IntroPwdCheck(this);
			}
		break;
		case 3: stages[current + 1] = signup = new IntroSignup(this); break;
		}
	}
	_back.raise();
	return true;
}

void IntroWidget::prepareMove() {
	if (App::app()) App::app()->mtpPause();

	if (_cacheHide.isNull() || _cacheHideIndex != current) makeHideCache();

	stages[current + moving]->prepareShow();
	if (_cacheShow.isNull() || _cacheShowIndex != current + moving) makeShowCache();

	int32 m = (moving > 0) ? 1 : -1;
	a_coordHide = anim::ivalue(0, -m * st::introSlideShift);
	a_opacityHide = anim::fvalue(1, 0);
	a_coordShow = anim::ivalue(m * st::introSlideShift, 0);
	a_opacityShow = anim::fvalue(0, 1);
	_a_stage.start();

	_backTo = stages[current + moving]->hasBack() ? 1 : 0;
	_backFrom = stages[current]->hasBack() ? 1 : 0;
	_a_stage.step();
	if (_backFrom > 0 || _backTo > 0) {
		_back.show();
	} else {
		_back.hide();
	}
	stages[current]->deactivate();
	stages[current + moving]->hide();
}

void IntroWidget::onDoneStateChanged(int oldState, ButtonStateChangeSource source) {
	if (_a_stage.animating()) return;
	if (source == ButtonByPress) {
		if (oldState & Button::StateDown) {
			_cacheHide = QPixmap();
		} else {
			makeHideCache();
		}
	} else if (source == ButtonByHover && current != 2) {
		if (!createNext()) return;
		if (!_cacheShow) makeShowCache(current + 1);
	}
}

void IntroWidget::makeHideCache(int stage) {
	if (stage < 0) stage = current;
	int w = st::introSize.width(), h = st::introSize.height();
	_cacheHide = myGrab(stages[stage], QRect(st::introSlideShift, 0, w, h));
	_cacheHideIndex = stage;
}

void IntroWidget::makeShowCache(int stage) {
	if (stage < 0) stage = current + moving;
	int w = st::introSize.width(), h = st::introSize.height();
	_cacheShow = myGrab(stages[stage], QRect(st::introSlideShift, 0, w, h));
	_cacheShowIndex = stage;
}

void IntroWidget::animShow(const QPixmap &bgAnimCache, bool back) {
	if (App::app()) App::app()->mtpPause();

	(back ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.stop();
	stages[current]->show();
	if (stages[current]->hasBack()) {
		_back.setOpacity(1);
		_back.show();
	} else {
		_back.hide();
	}
	(back ? _cacheUnder : _cacheOver) = myGrab(this);

	stages[current]->deactivate();
	stages[current]->hide();
	_back.hide();

	a_coordUnder = back ? anim::ivalue(-qFloor(st::slideShift * width()), 0) : anim::ivalue(0, -qFloor(st::slideShift * width()));
	a_coordOver = back ? anim::ivalue(0, width()) : anim::ivalue(width(), 0);
	a_shadow = back ? anim::fvalue(1, 0) : anim::fvalue(0, 1);
	_a_show.start();

	show();
}

void IntroWidget::step_show(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		_a_show.stop();

		a_coordUnder.finish();
		a_coordOver.finish();
		a_shadow.finish();

		_cacheUnder = _cacheOver = QPixmap();

		setFocus();
		stages[current]->show();
		stages[current]->activate();
		if (stages[current]->hasBack()) {
			_back.setOpacity(1);
			_back.show();
		}
		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordUnder.update(dt, st::slideFunction);
		a_coordOver.update(dt, st::slideFunction);
		a_shadow.update(dt, st::slideFunction);
	}
	if (timer) update();
}

void IntroWidget::stop_show() {
	_a_show.stop();
}

void IntroWidget::step_stage(float64 ms, bool timer) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	if (dt >= 1) {
		_a_stage.stop();

		a_coordShow.finish();
		a_opacityShow.finish();

		_cacheHide = _cacheShow = QPixmap();

		current += moving;
		moving = 0;
		setFocus();
		stages[current]->activate();
		if (!stages[current]->hasBack()) {
			_back.hide();
		}
		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordShow.update(dt2, st::introShowFunc);
		a_opacityShow.update(dt2, st::introAlphaShowFunc);
		a_coordHide.update(dt1, st::introHideFunc);
		a_opacityHide.update(dt1, st::introAlphaHideFunc);
		if (_backFrom != _backTo) {
			_back.setOpacity((_backFrom > _backTo) ? a_opacityHide.current() : a_opacityShow.current());
		} else {
			_back.setOpacity(1);
		}
	}
	if (timer) update();
}

void IntroWidget::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());
	setMouseTracking(true);

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	p.fillRect(e->rect(), st::white->b);
	if (_a_show.animating()) {
		if (a_coordOver.current() > 0) {
			p.drawPixmap(QRect(0, 0, a_coordOver.current(), height()), _cacheUnder, QRect(-a_coordUnder.current() * cRetinaFactor(), 0, a_coordOver.current() * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(a_shadow.current() * st::slideFadeOut);
			p.fillRect(0, 0, a_coordOver.current(), height(), st::black->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow);
	} else if (_a_stage.animating()) {
		p.setOpacity(a_opacityHide.current());
		p.drawPixmap(stages[current]->x() + st::introSlideShift + a_coordHide.current(), stages[current]->y(), _cacheHide);
		p.setOpacity(a_opacityShow.current());
		p.drawPixmap(stages[current + moving]->x() + st::introSlideShift + a_coordShow.current(), stages[current + moving]->y(), _cacheShow);
	}
}

QRect IntroWidget::innerRect() const {
	int innerWidth = st::introSize.width() + 2 * st::introSlideShift, innerHeight = st::introSize.height();
	return QRect((width() - innerWidth) / 2, (height() - innerHeight) / 2, innerWidth, (height() + innerHeight) / 2);
}

QString IntroWidget::currentCountry() const {
	return countryForReg;
}

void IntroWidget::setPhone(const QString &phone, const QString &phone_hash, bool registered) {
	_phone = phone;
	_phone_hash = phone_hash;
	_registered = registered;
}

void IntroWidget::setCode(const QString &code) {
	_code = code;
}

void IntroWidget::setPwdSalt(const QByteArray &salt) {
	_pwdSalt = salt;
	delete signup;
	delete pwdcheck;
	stages[3] = stages[4] = 0;
	signup = 0;
	pwdcheck = 0;
}

void IntroWidget::setHasRecovery(bool has) {
	_hasRecovery = has;
}

void IntroWidget::setPwdHint(const QString &hint) {
	_pwdHint = hint;
}

void IntroWidget::setCodeByTelegram(bool byTelegram) {
	_codeByTelegram = byTelegram;
	if (code) code->updateDescText();
}

void IntroWidget::setCallTimeout(int32 callTimeout) {
	_callTimeout = callTimeout;
}

const QString &IntroWidget::getPhone() const {
	return _phone;
}

const QString &IntroWidget::getPhoneHash() const {
	return _phone_hash;
}

const QString &IntroWidget::getCode() const {
	return _code;
}

int32 IntroWidget::getCallTimeout() const {
	return _callTimeout;
}

const QByteArray &IntroWidget::getPwdSalt() const {
	return _pwdSalt;
}

bool IntroWidget::getHasRecovery() const {
	return _hasRecovery;
}

const QString &IntroWidget::getPwdHint() const {
	return _pwdHint;
}

bool IntroWidget::codeByTelegram() const {
	return _codeByTelegram;
}

void IntroWidget::resizeEvent(QResizeEvent *e) {
	QRect r(innerRect());
	if (steps) steps->setGeometry(r);
	if (phone) phone->setGeometry(r);
	if (code) code->setGeometry(r);
	if (signup) signup->setGeometry(r);
	if (pwdcheck) pwdcheck->setGeometry(r);
}

void IntroWidget::mousePressEvent(QMouseEvent *e) {

}

void IntroWidget::finish(const MTPUser &user, const QImage &photo) {
	App::wnd()->setupMain(true, &user);
	if (!photo.isNull()) {
		App::app()->uploadProfilePhoto(photo, MTP::authedId());
	}
}

void IntroWidget::keyPressEvent(QKeyEvent *e) {
	if (_a_show.animating() || _a_stage.animating()) return;

	if (e->key() == Qt::Key_Escape) {
		stages[current]->onBack();
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
		stages[current]->onNext();
	}
}

void IntroWidget::updateAdaptiveLayout() {
}

void IntroWidget::rpcInvalidate() {
	if (phone) phone->rpcInvalidate();
	if (code) code->rpcInvalidate();
	if (signup) signup->rpcInvalidate();
	if (pwdcheck) pwdcheck->rpcInvalidate();
}

IntroWidget::~IntroWidget() {
	delete steps;
	delete phone;
	delete code;
	delete signup;
	delete pwdcheck;
	if (App::wnd()) App::wnd()->noIntro(this);
}
