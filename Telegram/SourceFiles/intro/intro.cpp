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
#include "stdafx.h"
#include "lang.h"
#include "style.h"

#include "intro/intro.h"
#include "intro/introsteps.h"
#include "intro/introphone.h"
#include "intro/introcode.h"
#include "intro/introsignup.h"
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
		if (App::app()) App::app()->startUpdateCheck();
	}
}

IntroWidget::IntroWidget(Window *window) : QWidget(window),
	cacheForHideInd(0), cacheForShowInd(0), wnd(window), steps(new IntroSteps(this)),
    phone(0), code(0), signup(0), current(0), moving(0), visibilityChanging(0), _callTimeout(60) {
	setGeometry(QRect(0, st::titleHeight, wnd->width(), wnd->height() - st::titleHeight));

	countryForReg = psCurrentCountry();

	MTP::send(MTPhelp_GetNearestDc(), rpcDone(gotNearestDC));
	signalEmitOn = this;

	stages[0] = steps;
	memset(stages + 1, 0, sizeof(QWidget*) * 3);

	connect(window, SIGNAL(resized(const QSize &)), this, SLOT(onParentResize(const QSize &)));

	show();
	setFocus();
}

void IntroWidget::onParentResize(const QSize &newSize) {
	resize(newSize);
}

void IntroWidget::onIntroBack() {
	if (!current) return;
	moving = -1;
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
		case 2: stages[current + 1] = signup = new IntroSignup(this); break;
		}
	}
	return true;
}

void IntroWidget::prepareMove() {
	if (cacheForHide.isNull() || cacheForHideInd != current) makeHideCache();
	if (cacheForShow.isNull() || cacheForShowInd != current + moving) makeShowCache();

	xCoordHide = anim::ivalue(0, -moving * st::introSlideShift);
	cAlphaHide = anim::fvalue(1, 0);
	xCoordShow = anim::ivalue(moving * st::introSlideShift, 0);
	cAlphaShow = anim::fvalue(0, 1);
	anim::start(this);

	stages[current]->deactivate();
	stages[current + moving]->hide();
}

void IntroWidget::onDoneStateChanged(int oldState, ButtonStateChangeSource source) {
	if (animating()) return;
	if (source == ButtonByPress) {
		if (oldState & Button::StateDown) {
			cacheForHide = QPixmap();
		} else {
			makeHideCache();
		}
	} else if (source == ButtonByHover) {
		if (!createNext()) return;
		if (!cacheForShow) makeShowCache(current + 1);
	}
}

void IntroWidget::makeHideCache(int stage) {
	if (stage < 0) stage = current;
	int w = st::introSize.width(), h = st::introSize.height();
	cacheForHide = myGrab(stages[stage], QRect(st::introSlideShift, 0, w, h));
	cacheForHideInd = stage;
}

void IntroWidget::makeShowCache(int stage) {
	if (stage < 0) stage = current + moving;
	int w = st::introSize.width(), h = st::introSize.height();
	cacheForShow = myGrab(stages[stage], QRect(st::introSlideShift, 0, w, h));
	cacheForShowInd = stage;
}

void IntroWidget::animShow(const QPixmap &bgAnimCache, bool back) {
	_bgAnimCache = bgAnimCache;

	anim::stop(this);
	stages[current]->show();
	_animCache = myGrab(this, rect());

	visibilityChanging = 1;
	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);

	stages[current]->deactivate();
	stages[current]->hide();
	anim::start(this);
	show();
}

bool IntroWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (visibilityChanging) {
		if (dt2 >= 1) {
			res = false;
			a_bgCoord.finish();
			a_bgAlpha.finish();
			a_coord.finish();
			a_alpha.finish();

			_animCache = _bgAnimCache = QPixmap();

			visibilityChanging = 0;
			setFocus();
			stages[current]->show();
			stages[current]->activate();
		} else {
			a_bgCoord.update(dt1, st::introHideFunc);
			a_bgAlpha.update(dt1, st::introAlphaHideFunc);
			a_coord.update(dt2, st::introShowFunc);
			a_alpha.update(dt2, st::introAlphaShowFunc);
		}
	} else if (dt >= 1) {
		res = false;
		xCoordShow.finish();
		cAlphaShow.finish();

		cacheForHide = cacheForShow = QPixmap();

		current += moving;
		moving = 0;
		setFocus();
		stages[current]->activate();
	} else {
		xCoordShow.update(dt2, st::introShowFunc);
		cAlphaShow.update(dt2, st::introAlphaShowFunc);
		xCoordHide.update(dt1, st::introHideFunc);
		cAlphaHide.update(dt1, st::introAlphaHideFunc);
	}
	update();
	return res;
}

void IntroWidget::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());
	setMouseTracking(true);

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (animating()) {
		if (visibilityChanging) {
			p.setOpacity(a_bgAlpha.current());
			p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
			p.setOpacity(a_alpha.current());
			p.drawPixmap(a_coord.current(), 0, _animCache);
		} else {
			p.setOpacity(cAlphaHide.current());
			p.drawPixmap(stages[current]->x() + st::introSlideShift + xCoordHide.current(), stages[current]->y(), cacheForHide);
			p.setOpacity(cAlphaShow.current());
			p.drawPixmap(stages[current + moving]->x() + st::introSlideShift + xCoordShow.current(), stages[current + moving]->y(), cacheForShow);
		}
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

void IntroWidget::resizeEvent(QResizeEvent *e) {
	QRect r(innerRect());
	if (steps) steps->setGeometry(r);
	if (phone) phone->setGeometry(r);
	if (code) code->setGeometry(r);
	if (signup) signup->setGeometry(r);
}

void IntroWidget::mousePressEvent(QMouseEvent *e) {

}

void IntroWidget::finish(const MTPUser &user, const QImage &photo) {
	wnd->setupMain(true);
	wnd->startMain(user);
	if (!photo.isNull()) {
		App::app()->uploadProfilePhoto(photo, MTP::authedId());
	}
}

void IntroWidget::keyPressEvent(QKeyEvent *e) {
	if (animating()) return;
	if (e->key() == Qt::Key_Escape) {
		stages[current]->onBack();
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
		stages[current]->onNext();
	}
}

IntroWidget::~IntroWidget() {
	delete steps;
	delete phone;
	delete code;
	delete signup;
	if (App::wnd()) App::wnd()->noIntro(this);
}
