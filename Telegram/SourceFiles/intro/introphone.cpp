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
#include "intro/introphone.h"

#include "lang.h"
#include "application.h"
#include "intro/introcode.h"
#include "styles/style_intro.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

IntroPhone::IntroPhone(IntroWidget *parent) : IntroStep(parent)
, a_errorAlpha(0)
, _a_error(animation(this, &IntroPhone::step_error))
, _next(this, lang(lng_intro_next), st::introNextButton)
, _country(this, st::introCountry)
, _phone(this, st::introPhone)
, _code(this, st::introCountryCode)
, _signup(this, lng_phone_notreg(lt_signup_start, textcmdStartLink(1), lt_signup_end, textcmdStopLink()), Ui::FlatLabel::InitType::Rich, st::introErrorLabel, st::introErrorLabelTextStyle)
, _checkRequest(this) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(_next, SIGNAL(clicked()), this, SLOT(onSubmitPhone()));
	connect(_phone, SIGNAL(voidBackspace(QKeyEvent*)), _code, SLOT(startErasing(QKeyEvent*)));
	connect(_country, SIGNAL(codeChanged(const QString &)), _code, SLOT(codeSelected(const QString &)));
	connect(_code, SIGNAL(codeChanged(const QString &)), _country, SLOT(onChooseCode(const QString &)));
	connect(_code, SIGNAL(codeChanged(const QString &)), _phone, SLOT(onChooseCode(const QString &)));
	connect(_country, SIGNAL(codeChanged(const QString &)), _phone, SLOT(onChooseCode(const QString &)));
	connect(_code, SIGNAL(addedToNumber(const QString &)), _phone, SLOT(addedToNumber(const QString &)));
	connect(_phone, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(_code, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(intro(), SIGNAL(countryChanged()), this, SLOT(countryChanged()));
	connect(_checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	_signup->setLink(1, MakeShared<LambdaClickHandler>([this] {
		toSignUp();
	}));
	_signup->hide();

	_signupCache = myGrab(_signup);

	if (!_country->onChooseCountry(intro()->currentCountry())) {
		_country->onChooseCountry(qsl("US"));
	}
	_changed = false;
}

void IntroPhone::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (trivial || e->rect().intersects(_textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(_textRect, lang(lng_phone_title), style::al_top);
		p.setFont(st::introFont->f);
		p.drawText(_textRect, lang(lng_phone_desc), style::al_bottom);
	}
	if (_a_error.animating() || _error.length()) {
		int32 errorY = _showSignup ? ((_phone->y() + _phone->height() + _next->y() - st::introErrorFont->height) / 2) : (_next->y() + _next->height() + st::introErrorTop);
		p.setOpacity(a_errorAlpha.current());
		p.setFont(st::introErrorFont);
		p.setPen(st::introErrorFg);
		p.drawText(QRect(_textRect.x(), errorY, _textRect.width(), st::introErrorFont->height), _error, style::al_top);

		if (_signup->isHidden() && _showSignup) {
			p.drawPixmap(_signup->x(), _signup->y(), _signupCache);
		}
	}
}

void IntroPhone::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		_next->move((width() - _next->width()) / 2, st::introBtnTop);
		_country->move((width() - _country->width()) / 2, st::introTextTop + st::introTextSize.height() + st::introCountry.top);
		int phoneTop = _country->y() + _country->height() + st::introPhoneTop;
		_phone->move((width() - _country->width()) / 2 + _country->width() - st::introPhone.width, phoneTop);
		_code->move((width() - _country->width()) / 2, phoneTop);
	}
	_signup->move((width() - _signup->width()) / 2, _next->y() + _next->height() + st::introErrorTop - ((st::introErrorLabelTextStyle.lineHeight - st::introErrorFont->height) / 2));
	_textRect = QRect((width() - st::introTextSize.width()) / 2, st::introTextTop, st::introTextSize.width(), st::introTextSize.height());
}

void IntroPhone::showError(const QString &error, bool signUp) {
	if (!error.isEmpty()) {
		_phone->notaBene();
		_showSignup = signUp;
	}

	if (!_a_error.animating() && error == _error) return;

	if (error.length()) {
		_error = error;
		a_errorAlpha.start(1);
	} else {
		a_errorAlpha.start(0);
	}
	_signup->hide();
	_a_error.start();
}

void IntroPhone::step_error(float64 ms, bool timer) {
	float64 dt = ms / st::introErrorDuration;

	if (dt >= 1) {
		_a_error.stop();
		a_errorAlpha.finish();
		if (!a_errorAlpha.current()) {
			_error.clear();
			_signup->hide();
		} else if (!_error.isEmpty() && _showSignup) {
			_signup->show();
		}
	} else {
		a_errorAlpha.update(dt, anim::linear);
	}
	if (timer) update();
}

void IntroPhone::countryChanged() {
	if (!_changed) {
		selectCountry(intro()->currentCountry());
	}
}

void IntroPhone::onInputChange() {
	_changed = true;
	showError(QString());
}

void IntroPhone::disableAll() {
	_next->setDisabled(true);
	_phone->setDisabled(true);
	_country->setDisabled(true);
	_code->setDisabled(true);
	setFocus();
}

void IntroPhone::enableAll(bool failed) {
	_next->setDisabled(false);
	_phone->setDisabled(false);
	_country->setDisabled(false);
	_code->setDisabled(false);
	if (failed) _phone->setFocus();
}

void IntroPhone::onSubmitPhone() {
	if (_sentRequest || isHidden()) return;

	if (!App::isValidPhone(fullNumber())) {
		showError(lang(lng_bad_phone));
		_phone->setFocus();
		return;
	}

	disableAll();
	showError(QString());

	_checkRequest->start(1000);

	_sentPhone = fullNumber();
	_sentRequest = MTP::send(MTPauth_CheckPhone(MTP_string(_sentPhone)), rpcDone(&IntroPhone::phoneCheckDone), rpcFail(&IntroPhone::phoneSubmitFail));
}

void IntroPhone::stopCheck() {
	_checkRequest->stop();
}

void IntroPhone::onCheckRequest() {
	int32 status = MTP::state(_sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(base::take(_sentRequest));
			if (!_phone->isEnabled()) enableAll(true);
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroPhone::phoneCheckDone(const MTPauth_CheckedPhone &result) {
	stopCheck();

	const auto &d(result.c_auth_checkedPhone());
	if (mtpIsTrue(d.vphone_registered)) {
		disableAll();
		showError(QString());

		_checkRequest->start(1000);

		MTPauth_SendCode::Flags flags = 0;
		_sentRequest = MTP::send(MTPauth_SendCode(MTP_flags(flags), MTP_string(_sentPhone), MTPBool(), MTP_int(ApiId), MTP_string(ApiHash)), rpcDone(&IntroPhone::phoneSubmitDone), rpcFail(&IntroPhone::phoneSubmitFail));
	} else {
		showError(lang(lng_bad_phone_noreg), true);
		enableAll(true);
		_sentRequest = 0;
	}
}

void IntroPhone::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	_sentRequest = 0;
	enableAll(true);

	if (result.type() != mtpc_auth_sentCode) {
		showError(lang(lng_server_error));
		return;
	}

	const auto &d(result.c_auth_sentCode());
	switch (d.vtype.type()) {
	case mtpc_auth_sentCodeTypeApp: intro()->setCodeByTelegram(true); break;
	case mtpc_auth_sentCodeTypeSms:
	case mtpc_auth_sentCodeTypeCall: intro()->setCodeByTelegram(false); break;
	case mtpc_auth_sentCodeTypeFlashCall: LOG(("Error: should not be flashcall!")); break;
	}
	intro()->setPhone(_sentPhone, d.vphone_code_hash.c_string().v.c_str(), d.is_phone_registered());
	if (d.has_next_type() && d.vnext_type.type() == mtpc_auth_codeTypeCall) {
		intro()->setCallStatus({ IntroWidget::CallWaiting, d.has_timeout() ? d.vtimeout.v : 60 });
	} else {
		intro()->setCallStatus({ IntroWidget::CallDisabled, 0 });
	}
	intro()->nextStep(new IntroCode(intro()));
}

void IntroPhone::toSignUp() {
	disableAll();
	showError(QString());

	_checkRequest->start(1000);

	MTPauth_SendCode::Flags flags = 0;
	_sentRequest = MTP::send(MTPauth_SendCode(MTP_flags(flags), MTP_string(_sentPhone), MTPBool(), MTP_int(ApiId), MTP_string(ApiHash)), rpcDone(&IntroPhone::phoneSubmitDone), rpcFail(&IntroPhone::phoneSubmitFail));
}

bool IntroPhone::phoneSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showError(lang(lng_flood_error));
		enableAll(true);
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	stopCheck();
	_sentRequest = 0;
	const QString &err = error.type();
	if (err == qstr("PHONE_NUMBER_INVALID")) { // show error
		showError(lang(lng_bad_phone));
		enableAll(true);
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	enableAll(true);
	return false;
}

QString IntroPhone::fullNumber() const {
	return _code->text() + _phone->text();
}

void IntroPhone::selectCountry(const QString &c) {
	_country->onChooseCountry(c);
}

void IntroPhone::activate() {
	IntroStep::activate();
	_phone->setFocus();
}

void IntroPhone::finished() {
	IntroStep::finished();
	_checkRequest->stop();
	rpcClear();

	_error.clear();
	a_errorAlpha = anim::fvalue(0);
	enableAll(true);
}

void IntroPhone::cancelled() {
	if (_sentRequest) {
		MTP::cancel(base::take(_sentRequest));
	}
}

void IntroPhone::onSubmit() {
	onSubmitPhone();
}
