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
#include "intro/introwidget.h"

#include "lang.h"
#include "localstorage.h"
#include "intro/introstart.h"
#include "intro/introphone.h"
#include "intro/introcode.h"
#include "intro/introsignup.h"
#include "intro/intropwdcheck.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "ui/text/text.h"

IntroWidget::IntroWidget(QWidget *parent) : TWidget(parent)
, _a_stage(animation(this, &IntroWidget::step_stage))
, _a_show(animation(this, &IntroWidget::step_show))
, _back(this, st::setClose) {
	setGeometry(QRect(0, st::titleHeight, App::wnd()->width(), App::wnd()->height() - st::titleHeight));

	connect(&_back, SIGNAL(clicked()), this, SLOT(onBack()));
	_back.hide();

	_countryForReg = psCurrentCountry();

	MTP::send(MTPhelp_GetNearestDc(), rpcDone(&IntroWidget::gotNearestDC));

	_stepHistory.push_back(new IntroStart(this));
	_back.raise();

	connect(parent, SIGNAL(resized(const QSize&)), this, SLOT(onParentResize(const QSize&)));

	show();
	setFocus();

	cSetPasswordRecovered(false);

	_back.move(st::setClosePos.x(), st::setClosePos.y());

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Sandbox::startUpdateCheck();
#endif
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

void IntroWidget::onStepSubmit() {
	step()->onSubmit();
}

void IntroWidget::onBack() {
	historyMove(MoveBack);
}

void IntroWidget::historyMove(MoveType type) {
	if (_a_stage.animating()) return;

	t_assert(_stepHistory.size() > 1);

	if (App::app()) App::app()->mtpPause();

	switch (type) {
	case MoveBack: {
		_cacheHide = grabStep();

		IntroStep *back = step();
		_backFrom = back->hasBack() ? 1 : 0;
		_stepHistory.pop_back();
		back->cancelled();
		delete back;
	} break;

	case MoveForward: {
		_cacheHide = grabStep(1);
		_backFrom = step(1)->hasBack() ? 1 : 0;
		step(1)->finished();
	} break;

	case MoveReplace: {
		_cacheHide = grabStep(1);
		IntroStep *replaced = step(1);
		_backFrom = replaced->hasBack() ? 1 : 0;
		_stepHistory.removeAt(_stepHistory.size() - 2);
		replaced->finished();
		delete replaced;
	} break;
	}

	_cacheShow = grabStep();
	_backTo = step()->hasBack() ? 1 : 0;

	int32 m = (type == MoveBack) ? -1 : 1;
	a_coordHide = anim::ivalue(0, -m * st::introSlideShift);
	a_opacityHide = anim::fvalue(1, 0);
	a_coordShow = anim::ivalue(m * st::introSlideShift, 0);
	a_opacityShow = anim::fvalue(0, 1);
	_a_stage.start();

	_a_stage.step();
	if (_backFrom > 0 || _backTo > 0) {
		_back.show();
	} else {
		_back.hide();
	}
	step()->hide();
}

void IntroWidget::pushStep(IntroStep *step, MoveType type) {
	_stepHistory.push_back(step);
	_back.raise();
	_stepHistory.back()->hide();

	historyMove(type);
}

void IntroWidget::gotNearestDC(const MTPNearestDc &result) {
	const auto &nearest(result.c_nearestDc());
	DEBUG_LOG(("Got nearest dc, country: %1, nearest: %2, this: %3").arg(nearest.vcountry.c_string().v.c_str()).arg(nearest.vnearest_dc.v).arg(nearest.vthis_dc.v));
	MTP::setdc(result.c_nearestDc().vnearest_dc.v, true);
	if (_countryForReg != nearest.vcountry.c_string().v.c_str()) {
		_countryForReg = nearest.vcountry.c_string().v.c_str();
		emit countryChanged();
	}
}

QPixmap IntroWidget::grabStep(int skip) {
	return myGrab(step(skip), QRect(st::introSlideShift, 0, st::introSize.width(), st::introSize.height()));
}

void IntroWidget::animShow(const QPixmap &bgAnimCache, bool back) {
	if (App::app()) App::app()->mtpPause();

	(back ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.stop();
	step()->show();
	if (step()->hasBack()) {
		_back.setOpacity(1);
		_back.show();
	} else {
		_back.hide();
	}
	(back ? _cacheUnder : _cacheOver) = myGrab(this);

	step()->hide();
	_back.hide();

	a_coordUnder = back ? anim::ivalue(-st::slideShift, 0) : anim::ivalue(0, -st::slideShift);
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
		step()->activate();
		if (step()->hasBack()) {
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

		setFocus();
		step()->activate();
		if (!step()->hasBack()) {
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
			p.fillRect(0, 0, a_coordOver.current(), height(), st::white->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow.rect());
	} else if (_a_stage.animating()) {
		p.setOpacity(a_opacityHide.current());
		p.drawPixmap(step()->x() + st::introSlideShift + a_coordHide.current(), step()->y(), _cacheHide);
		p.setOpacity(a_opacityShow.current());
		p.drawPixmap(step()->x() + st::introSlideShift + a_coordShow.current(), step()->y(), _cacheShow);
	}
}

QRect IntroWidget::innerRect() const {
	int innerWidth = st::introSize.width() + 2 * st::introSlideShift, innerHeight = st::introSize.height();
	return QRect((width() - innerWidth) / 2, (height() - innerHeight) / 2, innerWidth, (height() + innerHeight) / 2);
}

QString IntroWidget::currentCountry() const {
	return _countryForReg;
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
}

void IntroWidget::setHasRecovery(bool has) {
	_hasRecovery = has;
}

void IntroWidget::setPwdHint(const QString &hint) {
	_pwdHint = hint;
}

void IntroWidget::setCodeByTelegram(bool byTelegram) {
	_codeByTelegram = byTelegram;
}

void IntroWidget::setCallStatus(const CallStatus &status) {
	_callStatus = status;
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

const IntroWidget::CallStatus &IntroWidget::getCallStatus() const {
	return _callStatus;
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
	for (IntroStep *step : _stepHistory) {
		step->setGeometry(r);
	}
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
		if (step()->hasBack()) {
			onBack();
		}
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
		onStepSubmit();
	}
}

void IntroWidget::updateAdaptiveLayout() {
}

void IntroWidget::rpcClear() {
	for (IntroStep *step : _stepHistory) {
		step->rpcClear();
	}
}

IntroWidget::~IntroWidget() {
	while (!_stepHistory.isEmpty()) {
		IntroStep *back = _stepHistory.back();
		_stepHistory.pop_back();
		delete back;
	}
	if (App::wnd()) App::wnd()->noIntro(this);
}
