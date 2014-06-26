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

#include "intro/introcode.h"
#include "intro/intro.h"

CodeInput::CodeInput(QWidget *parent, const style::flatInput &st, const QString &ph) : FlatInput(parent, st, ph) {
}

void CodeInput::correctValue(QKeyEvent *e, const QString &was) {
	QString oldText(text()), newText;
	int oldPos(cursorPosition()), newPos(-1), oldLen(oldText.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (oldText[i].isDigit()) {
			++digitCount;
		}
	}
	if (digitCount > 5) digitCount = 5;
	bool strict = (digitCount == 5);

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		QChar ch(oldText[i]);
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
	if (newText != oldText) {
		setText(newText);
		if (newPos != oldPos) {
			setCursorPosition(newPos);
		}
	}

	if (strict) emit codeEntered();
}

IntroCode::IntroCode(IntroWidget *parent) : IntroStage(parent), errorAlpha(0),
	next(this, lang(lng_intro_next), st::btnIntroNext),
	back(this, lang(lng_intro_back), st::btnIntroBack),
	code(this, st::inpIntroCode, lang(lng_code_ph)), waitTillCall(intro()->getCallTimeout()) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&next, SIGNAL(stateChanged(int, ButtonStateChangeSource)), parent, SLOT(onDoneStateChanged(int, ButtonStateChangeSource)));
	connect(&next, SIGNAL(clicked()), this, SLOT(onSubmitCode()));
	connect(&back, SIGNAL(clicked()), parent, SLOT(onIntroBack()));
	connect(&code, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(&callTimer, SIGNAL(timeout()), this, SLOT(onSendCall()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));
}

void IntroCode::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (trivial || e->rect().intersects(textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(textRect, intro()->getPhone(), style::al_topleft);
		p.setFont(st::introFont->f);
		p.drawText(textRect, lang(lng_code_desc), style::al_bottomleft);
	}
	QString callText = lang(lng_code_calling);
	if (waitTillCall >= 3600) {
		callText = lang(lng_code_call).arg(QString("%1:%2").arg(waitTillCall / 3600).arg((waitTillCall / 60) % 60, 2, 10, QChar('0'))).arg(waitTillCall % 60, 2, 10, QChar('0'));
	} else if (waitTillCall > 0) {
		callText = lang(lng_code_call).arg(waitTillCall / 60).arg(waitTillCall % 60, 2, 10, QChar('0'));
	} else if (waitTillCall < 0) {
		callText = lang(lng_code_called);
	}
	p.drawText(QRect(textRect.left(), code.y() + code.height() + st::introSkip, st::introTextSize.width(), st::introErrHeight), callText, style::al_left);
	if (animating() || error.length()) {
		p.setOpacity(errorAlpha.current());
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(textRect.left(), next.y() + next.height() + st::introErrTop + st::introErrFont->ascent, error);
	}
}

void IntroCode::resizeEvent(QResizeEvent *e) {
	int sumBack = st::btnIntroNext.width + st::btnIntroBack.width + st::btnIntroSep;
	if (e->oldSize().width() != width()) {
		int sumNext = st::btnIntroNext.width - st::btnIntroBack.width - st::btnIntroSep;
		back.move((width() - sumBack) / 2, st::introBtnTop);
		next.move((width() - sumNext) / 2, st::introBtnTop);
		code.move((width() - sumBack) / 2, st::introTextSize.height() + 24);
	}
	textRect = QRect((width() - sumBack) / 2, 0, st::introTextSize.width(), st::introTextSize.height());
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
	callTimer.start(1000);
	error = "";
	errorAlpha = anim::fvalue(0);
	show();
	code.setDisabled(false);
	code.setFocus();
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
	if (d.vuser.type() != mtpc_userSelf) { // wtf?
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
	}
	if (QRegularExpression("^FLOOD_WAIT_(\\d+)$").match(err).hasMatch()) {
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

void IntroCode::onSubmitCode(bool force) {
	if (!force && (code.text() == sentCode || !code.isEnabled())) return;

	code.setDisabled(true);
	setFocus();

	showError("");

	checkRequest.start(1000);

	sentCode = code.text();
	sentRequest = MTP::send(MTPauth_SignIn(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash()), MTP_string(sentCode)), rpcDone(&IntroCode::codeSubmitDone), rpcFail(&IntroCode::codeSubmitFail));
}

void IntroCode::onNext() {
	onSubmitCode();
}

void IntroCode::onBack() {
	intro()->onIntroBack();
}
