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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"
#include "style.h"

#include "application.h"

#include "intro/introcode.h"
#include "intro/intro.h"

CodeInput::CodeInput(QWidget *parent, const style::flatInput &st, const QString &ph) : FlatInput(parent, st, ph) {
}

void CodeInput::correctValue(const QString &was, QString &now) {
	QString newText;
	int oldPos(cursorPosition()), newPos(-1), oldLen(now.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (now[i].isDigit()) {
			++digitCount;
		}
	}
	if (digitCount > 5) digitCount = 5;
	bool strict = (digitCount == 5);

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		QChar ch(now[i]);
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			newText += ch;
			if (strict && !digitCount) {
				break;
			}
		}
		if (i == oldPos) {
			newPos = newText.length();
		}
	}
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		updatePlaceholder();
		if (newPos != oldPos) {
			setCursorPosition(newPos);
		}
	}

	if (strict) emit codeEntered();
}

IntroCode::IntroCode(IntroWidget *parent) : IntroStage(parent), errorAlpha(0),
	next(this, lang(lng_intro_next), st::btnIntroNext),
	_desc(st::introTextSize.width()),
	_noTelegramCode(this, lang(lng_code_no_telegram), st::introLink),
	_noTelegramCodeRequestId(0),
	code(this, st::inpIntroCode, lang(lng_code_ph)), waitTillCall(intro()->getCallTimeout()) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&next, SIGNAL(stateChanged(int, ButtonStateChangeSource)), parent, SLOT(onDoneStateChanged(int, ButtonStateChangeSource)));
	connect(&next, SIGNAL(clicked()), this, SLOT(onSubmitCode()));
	connect(&code, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(&callTimer, SIGNAL(timeout()), this, SLOT(onSendCall()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));
	connect(&_noTelegramCode, SIGNAL(clicked()), this, SLOT(onNoTelegramCode()));

	updateDescText();
}

void IntroCode::updateDescText() {
	_desc.setRichText(st::introFont, lang(intro()->codeByTelegram() ? lng_code_telegram : lng_code_desc));
	if (intro()->codeByTelegram()) {
		_noTelegramCode.show();
		callTimer.stop();
	} else {
		_noTelegramCode.hide();
		waitTillCall = intro()->getCallTimeout();
		if (!callTimer.isActive()) {
			callTimer.start(1000);
		}
	}
	update();
}

void IntroCode::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	bool codeByTelegram = intro()->codeByTelegram();
	if (trivial || e->rect().intersects(textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(textRect, intro()->getPhone(), style::al_top);
		p.setFont(st::introFont->f);
		_desc.draw(p, textRect.x(), textRect.y() + textRect.height() - 2 * st::introFont->height, textRect.width(), style::al_top);
	}
	if (codeByTelegram) {
	} else {
		QString callText = lang(lng_code_calling);
		if (waitTillCall >= 3600) {
			callText = lng_code_call(lt_minutes, qsl("%1:%2").arg(waitTillCall / 3600).arg((waitTillCall / 60) % 60, 2, 10, QChar('0')), lt_seconds, qsl("%1").arg(waitTillCall % 60, 2, 10, QChar('0')));
		} else if (waitTillCall > 0) {
			callText = lng_code_call(lt_minutes, QString::number(waitTillCall / 60), lt_seconds, qsl("%1").arg(waitTillCall % 60, 2, 10, QChar('0')));
		} else if (waitTillCall < 0) {
			callText = lang(lng_code_called);
		}
		p.drawText(QRect(textRect.left(), code.y() + code.height() + st::introCallSkip, st::introTextSize.width(), st::introErrHeight), callText, style::al_center);
	}
	if (animating() || error.length()) {
		p.setOpacity(errorAlpha.current());
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(QRect(textRect.left(), next.y() + next.height() + st::introErrTop, st::introTextSize.width(), st::introErrHeight), error, style::al_center);
	}
}

void IntroCode::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		next.move((width() - next.width()) / 2, st::introBtnTop);
		code.move((width() - code.width()) / 2, st::introTextTop + st::introTextSize.height() + st::introCountry.top);
	}
	textRect = QRect((width() - st::introTextSize.width()) / 2, st::introTextTop, st::introTextSize.width(), st::introTextSize.height());
	_noTelegramCode.move(textRect.left() + (st::introTextSize.width() - _noTelegramCode.width()) / 2, code.y() + code.height() + st::introCallSkip + (st::introErrHeight - _noTelegramCode.height()) / 2);
}

void IntroCode::showError(const QString &err) {
	if (!err.isEmpty()) code.notaBene();
	if (!animating() && err == error) return;

	if (err.length()) {
		error = err;
		errorAlpha.start(1);
	} else {
		errorAlpha.start(0);
	}
	anim::start(this);
}

bool IntroCode::animStep(float64 ms) {
	float64 dt = ms / st::introErrDuration;

	bool res = true;
	if (dt >= 1) {
		res = false;
		errorAlpha.finish();
		if (!errorAlpha.current()) {
			error = "";
		}
	} else {
		errorAlpha.update(dt, st::introErrFunc);
	}
	update();
	return res;
}

void IntroCode::activate() {
	waitTillCall = intro()->getCallTimeout();
	if (!intro()->codeByTelegram()) {
		callTimer.start(1000);
	}
	error = "";
	errorAlpha = anim::fvalue(0);
	sentCode = QString();
	show();
	code.setDisabled(false);
	code.setFocus();
}

void IntroCode::prepareShow() {
	code.setText(QString());
	if (sentRequest) {
		MTP::cancel(sentRequest);
		sentRequest = 0;
	}
}

void IntroCode::deactivate() {
	callTimer.stop();
	hide();
	code.clearFocus();
}

void IntroCode::stopCheck() {
	checkRequest.stop();
}

void IntroCode::onCheckRequest() {
	int32 status = MTP::state(sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			if (sentRequest) {
				MTP::cancel(sentRequest);
				sentCode = "";
			}
			sentRequest = 0;
			if (!code.isEnabled()) {
				code.setDisabled(false);
				code.setFocus();
			}
		}
	}
	if (!sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroCode::codeSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	code.setDisabled(false);
	const MTPDauth_authorization &d(result.c_auth_authorization());
	if (d.vuser.type() != mtpc_user || !(d.vuser.c_user().vflags.v & MTPDuser_flag_self)) { // wtf?
		showError(lang(lng_server_error));
		return;
	}
	cSetLoggedPhoneNumber(intro()->getPhone());
	intro()->finish(d.vuser);
}

bool IntroCode::codeSubmitFail(const RPCError &error) {
	stopCheck();
	code.setDisabled(false);
	const QString &err = error.type();
	if (err == "PHONE_NUMBER_INVALID" || err == "PHONE_CODE_EXPIRED") { // show error
		onBack();
		return true;
	} else if (err == "PHONE_CODE_EMPTY" || err == "PHONE_CODE_INVALID") {
		showError(lang(lng_bad_code));
		code.setFocus();
		return true;
	} else if (err == "PHONE_NUMBER_UNOCCUPIED") { // success, need to signUp
		intro()->setCode(sentCode);
		intro()->onIntroNext();
		return true;
	} else if (err == "SESSION_PASSWORD_NEEDED") {
		intro()->setCode(sentCode);
		code.setDisabled(false);
		checkRequest.start(1000);
		sentRequest = MTP::send(MTPaccount_GetPassword(), rpcDone(&IntroCode::gotPassword), rpcFail(&IntroCode::codeSubmitFail));
		return true;
	} else if (mtpIsFlood(error)) {
		showError(lang(lng_flood_error));
		code.setFocus();
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	code.setFocus();
	return false;
}

void IntroCode::onInputChange() {
	showError("");
	if (code.text().length() == 5) onSubmitCode();
}

void IntroCode::onSendCall() {
	if (!--waitTillCall) {
		callTimer.stop();
		MTP::send(MTPauth_SendCall(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash())), rpcDone(&IntroCode::callDone));
	}
	update();
}

void IntroCode::callDone(const MTPBool &v) {
	if (!waitTillCall) {
		waitTillCall = -1;
		update();
	}
}

void IntroCode::gotPassword(const MTPaccount_Password &result) {
	stopCheck();
	code.setDisabled(false);
	switch (result.type()) {
	case mtpc_account_noPassword: // should not happen
		code.setFocus();
	break;

	case mtpc_account_password: {
		const MTPDaccount_password &d(result.c_account_password());
		intro()->setPwdSalt(qba(d.vcurrent_salt));
		intro()->setHasRecovery(d.vhas_recovery.v);
		intro()->setPwdHint(qs(d.vhint));
		intro()->onIntroNext();
	} break;
	}
}

void IntroCode::onSubmitCode(bool force) {
	if (!force && (code.text() == sentCode || !code.isEnabled())) return;

	code.setDisabled(true);
	setFocus();

	showError("");

	checkRequest.start(1000);

	sentCode = code.text();
	intro()->setPwdSalt(QByteArray());
	intro()->setHasRecovery(false);
	intro()->setPwdHint(QString());
	sentRequest = MTP::send(MTPauth_SignIn(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash()), MTP_string(sentCode)), rpcDone(&IntroCode::codeSubmitDone), rpcFail(&IntroCode::codeSubmitFail));
}

void IntroCode::onNoTelegramCode() {
	if (_noTelegramCodeRequestId) return;
	_noTelegramCodeRequestId = MTP::send(MTPauth_SendSms(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash())), rpcDone(&IntroCode::noTelegramCodeDone), rpcFail(&IntroCode::noTelegramCodeFail));
}

void IntroCode::noTelegramCodeDone(const MTPBool &result) {
	intro()->setCodeByTelegram(false);
	updateDescText();
}

bool IntroCode::noTelegramCodeFail(const RPCError &error) {
	if (mtpIsFlood(error)) {
		showError(lang(lng_flood_error));
		code.setFocus();
		return true;
	}
	if (cDebug()) { // internal server error
		showError(error.type() + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	code.setFocus();
	return false;
}

void IntroCode::onNext() {
	onSubmitCode();
}

void IntroCode::onBack() {
	intro()->onIntroBack();
}
