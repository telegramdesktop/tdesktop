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
#include "stdafx.h"
#include "lang.h"
#include "style.h"

#include "localstorage.h"

#include "passcodewidget.h"
#include "window.h"
#include "application.h"
#include "gui/text.h"

class LogOutLink : public ITextLink {
public:

	void onClick(Qt::MouseButton) const {
		App::wnd()->onLogout();
	}

};

PasscodeWidget::PasscodeWidget(QWidget *parent) : QWidget(parent),
_passcode(this, st::passcodeInput),
_submit(this, lang(lng_passcode_submit), st::passcodeSubmit),
_logout(this, lng_passcode_logout(lt_link_start, textcmdStartLink(1), lt_link_end, textcmdStopLink())) {
	setGeometry(QRect(0, st::titleHeight, App::wnd()->width(), App::wnd()->height() - st::titleHeight));
	connect(App::wnd(), SIGNAL(resized(const QSize &)), this, SLOT(onParentResize(const QSize &)));

	_passcode.setEchoMode(QLineEdit::Password);
	connect(&_submit, SIGNAL(clicked()), this, SLOT(onSubmit()));

	_errorTimer.setSingleShot(true);
	connect(&_errorTimer, SIGNAL(timeout()), this, SLOT(onError()));

	_logout.setLink(1, TextLinkPtr(new LogOutLink()));

	connect(&_passcode, SIGNAL(changed()), this, SLOT(onChanged()));
	connect(&_passcode, SIGNAL(accepted()), this, SLOT(onSubmit()));

	show();
	_passcode.setFocus();
}

void PasscodeWidget::onParentResize(const QSize &newSize) {
	resize(newSize);
}

void PasscodeWidget::onSubmit() {
	if (_passcode.text().isEmpty()) {
		_passcode.setFocus();
		_passcode.notaBene();
		return;
	}

	if (App::main()) {
		if (Local::checkPasscode(_passcode.text().toUtf8())) {
			App::wnd()->clearPasscode();
		} else {
			_error = QString();
			_passcode.setDisabled(true);
			_errorTimer.start(WrongPasscodeTimeout);
			return;
		}
	} else {
		if (Local::readMap(_passcode.text().toUtf8()) != Local::ReadMapPassNeeded) {
			App::app()->checkMapVersion();

			MTP::start();
			if (MTP::authedId()) {
				App::wnd()->setupMain(true);
			} else {
				App::wnd()->setupIntro(true);
			}
		} else {
			_error = QString();
			_passcode.setDisabled(true);
			_errorTimer.start(WrongPasscodeTimeout);
			update();
			return;
		}
	}
}

void PasscodeWidget::onError() {
	_error = lang(lng_passcode_wrong);
	_passcode.setDisabled(false);
	_passcode.selectAll();
	_passcode.setFocus();
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
	_bgAnimCache = bgAnimCache;

	anim::stop(this);
	showAll();
	_animCache = myGrab(this, rect());

	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);

	hideAll();
	anim::start(this);
	show();
}

bool PasscodeWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		res = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();

		_animCache = _bgAnimCache = QPixmap();

		showAll();
		setInnerFocus();
	} else {
		a_bgCoord.update(dt1, st::introHideFunc);
		a_bgAlpha.update(dt1, st::introAlphaHideFunc);
		a_coord.update(dt2, st::introShowFunc);
		a_alpha.update(dt2, st::introAlphaShowFunc);
	}
	update();
	return res;
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

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}

	if (animating()) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
	} else {
		p.fillRect(rect(), st::setBG->b);

		p.setFont(st::passcodeHeaderFont->f);
		p.drawText(QRect(0, _passcode.y() - st::passcodeHeaderHeight, width(), st::passcodeHeaderHeight), lang(lng_passcode_enter), style::al_center);

		if (!_error.isEmpty()) {
			p.setFont(st::boxFont->f);
			p.setPen(st::setErrColor->p);
			p.drawText(QRect(0, _passcode.y() + _passcode.height(), width(), st::usernameSkip), _error, style::al_center);
		}
	}
}

void PasscodeWidget::resizeEvent(QResizeEvent *e) {
	_passcode.move((width() - _passcode.width()) / 2, (height() / 3));
	_submit.move(_passcode.x(), _passcode.y() + _passcode.height() + st::passcodeSkip);
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
