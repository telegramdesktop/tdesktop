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

#include "application.h"

#include "intro/introphone.h"
#include "intro/intro.h"

namespace {
	class SignUpLink : public ITextLink {
	public:

		SignUpLink(IntroPhone *widget) : _widget(widget) {
		}

		void onClick(Qt::MouseButton) const {
			_widget->toSignUp();
		}

	private:
		IntroPhone *_widget;
	};
}

IntroPhone::IntroPhone(IntroWidget *parent) : IntroStage(parent),
    errorAlpha(0), changed(false),
	next(this, lang(lng_intro_next), st::btnIntroStart),
	country(this, st::introCountry),
	phone(this, st::inpIntroPhone, lang(lng_phone_ph)), code(this, st::inpIntroCountryCode),
    _signup(this, lang(lng_phone_notreg).replace(qsl("{signup}"), textcmdStartLink(1)).replace(qsl("{/signup}"), textcmdStopLink()), st::introErrLabel),
    _showSignup(false) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&next, SIGNAL(stateChanged(int, ButtonStateChangeSource)), parent, SLOT(onDoneStateChanged(int, ButtonStateChangeSource)));
	connect(&next, SIGNAL(clicked()), this, SLOT(onSubmitPhone()));
	connect(&phone, SIGNAL(voidBackspace(QKeyEvent*)), &code, SLOT(startErasing(QKeyEvent*)));
	connect(&country, SIGNAL(codeChanged(const QString &)), &code, SLOT(codeSelected(const QString &)));
	connect(&code, SIGNAL(codeChanged(const QString &)), &country, SLOT(onChooseCode(const QString &)));
	connect(&code, SIGNAL(addedToNumber(const QString &)), &phone, SLOT(addedToNumber(const QString &)));
	connect(&country, SIGNAL(selectClosed()), this, SLOT(onSelectClose()));
	connect(&phone, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(&code, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(intro(), SIGNAL(countryChanged()), this, SLOT(countryChanged()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	_signup.setLink(1, TextLinkPtr(new SignUpLink(this)));
	_signup.hide();

	_signupCache = myGrab(&_signup, _signup.rect());

	if (!country.onChooseCountry(intro()->currentCountry())) {
		country.onChooseCountry(qsl("US"));
	}
	changed = false;
}

void IntroPhone::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (trivial || e->rect().intersects(textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(textRect, lang(lng_phone_title), style::al_topleft);
		p.setFont(st::introFont->f);
		p.drawText(textRect, lang(lng_phone_desc), style::al_bottomleft);
	}
	if (animating() || error.length()) {
		p.setOpacity(errorAlpha.current());
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(textRect.x(), next.y() + next.height() + st::introErrTop + st::introErrFont->ascent, error);

		if (_signup.isHidden() && _showSignup) {
			p.drawPixmap(_signup.x(), _signup.y(), _signupCache);
		}
	}
}

void IntroPhone::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		next.move((width() - next.width()) / 2, st::introBtnTop);
		country.move((width() - country.width()) / 2, st::introTextSize.height() + st::introCountry.top);
		int phoneTop = country.y() + country.height() + st::introPhoneTop;
		phone.move((width() + country.width()) / 2 - st::inpIntroPhone.width, phoneTop);
		code.move((width() - country.width()) / 2, phoneTop);
	}
	_signup.move((width() - next.width()) / 2, next.y() + next.height() + st::introErrTop * 2 + st::introErrFont->height);
	textRect = QRect((width() - next.width()) / 2, 0, st::introTextSize.width(), st::introTextSize.height());
}

void IntroPhone::showError(const QString &err, bool signUp) {
	if (!err.isEmpty()) {
		phone.notaBene();
		_showSignup = signUp;
	}

	if (!animating() && err == error) return;

	if (err.length()) {
		error = err;
		errorAlpha.start(1);
	} else {
		errorAlpha.start(0);
	}
	_signup.hide();
	anim::start(this);
}

bool IntroPhone::animStep(float64 ms) {
	float64 dt = ms / st::introErrDuration;

	bool res = true;
	if (dt >= 1) {
		res = false;
		errorAlpha.finish();
		if (!errorAlpha.current()) {
			error = "";
			_signup.hide();
		} else if (!error.isEmpty() && _showSignup) {
			_signup.show();
		}
	} else {
		errorAlpha.update(dt, st::introErrFunc);
	}
	update();
	return res;
}

void IntroPhone::countryChanged() {
	if (!changed) {
		selectCountry(intro()->currentCountry());
	}
}

void IntroPhone::onSelectClose() {
	phone.setFocus();
}

void IntroPhone::onInputChange() {
	changed = true;
	showError("");
}

void IntroPhone::disableAll() {
	next.setDisabled(true);
	phone.setDisabled(true);
	country.setDisabled(true);
	code.setDisabled(true);
	setFocus();
}

void IntroPhone::enableAll(bool failed) {
	next.setDisabled(false);
	phone.setDisabled(false);
	country.setDisabled(false);
	code.setDisabled(false);
	if (failed) phone.setFocus();
}

void IntroPhone::onSubmitPhone(bool force) {
	if (!force && !next.isEnabled()) return;

	if (!App::isValidPhone(fullNumber())) {
		showError(lang(lng_bad_phone));
		phone.setFocus();
		return;
	}

	disableAll();
	showError("");

	checkRequest.start(1000);

	sentPhone = fullNumber();
	sentRequest = MTP::send(MTPauth_CheckPhone(MTP_string(sentPhone)), rpcDone(&IntroPhone::phoneCheckDone), rpcFail(&IntroPhone::phoneSubmitFail));
}

void IntroPhone::stopCheck() {
	checkRequest.stop();
}

void IntroPhone::onCheckRequest() {
	int32 status = MTP::state(sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(sentRequest);
			sentRequest = 0;
			if (!phone.isEnabled()) enableAll(true);
		}
	}
	if (!sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroPhone::phoneCheckDone(const MTPauth_CheckedPhone &result) {
	stopCheck();

	const MTPDauth_checkedPhone &d(result.c_auth_checkedPhone());
	if (d.vphone_registered.v) {
		disableAll();
		showError("");

		checkRequest.start(1000);

		sentRequest = MTP::send(MTPauth_SendCode(MTP_string(sentPhone), MTP_int(0), MTP_int(ApiId), MTP_string(ApiHash), MTP_string(Application::lang())), rpcDone(&IntroPhone::phoneSubmitDone), rpcFail(&IntroPhone::phoneSubmitFail));
	} else {
		showError(lang(lng_bad_phone_noreg), true);
		enableAll(true);
	}
}

void IntroPhone::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	enableAll(false);
	
	const MTPDauth_sentCode &d(result.c_auth_sentCode());
	intro()->setPhone(sentPhone, d.vphone_code_hash.c_string().v.c_str(), d.vphone_registered.v);
	intro()->setCallTimeout(result.c_auth_sentCode().vsend_call_timeout.v);
	intro()->onIntroNext();
}

void IntroPhone::toSignUp() {
	disableAll();
	showError("");

	checkRequest.start(1000);

	sentRequest = MTP::send(MTPauth_SendCode(MTP_string(sentPhone), MTP_int(0), MTP_int(ApiId), MTP_string(ApiHash), MTP_string(Application::lang())), rpcDone(&IntroPhone::phoneSubmitDone), rpcFail(&IntroPhone::phoneSubmitFail));
}

bool IntroPhone::phoneSubmitFail(const RPCError &error) {
	stopCheck();
	const QString &err = error.type();
	if (err == "PHONE_NUMBER_INVALID") { // show error
		showError(lang(lng_bad_phone));
		enableAll(true);
		return true;
	}
	if (QRegularExpression("^FLOOD_WAIT_(\\d+)$").match(err).hasMatch()) {
		showError(lang(lng_flood_error));
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
	return code.text() + phone.text();
}

void IntroPhone::selectCountry(const QString &c) {
	country.onChooseCountry(c);
}

void IntroPhone::activate() {
	error = "";
	errorAlpha = anim::fvalue(0);
	show();
	enableAll(true);
}

void IntroPhone::deactivate() {
	checkRequest.stop();
	hide();
	phone.clearFocus();
}

void IntroPhone::onNext() {
	onSubmitPhone();
}

void IntroPhone::onBack() {
}
