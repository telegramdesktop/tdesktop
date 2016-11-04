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
#include "intro/introcode.h"

#include "lang.h"
#include "application.h"
#include "intro/introsignup.h"
#include "intro/intropwdcheck.h"
#include "ui/buttons/round_button.h"
#include "styles/style_intro.h"

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

IntroCode::IntroCode(IntroWidget *parent) : IntroStep(parent)
, a_errorAlpha(0)
, _a_error(animation(this, &IntroCode::step_error))
, _next(this, lang(lng_intro_next), st::introNextButton)
, _desc(st::introTextSize.width())
, _noTelegramCode(this, lang(lng_code_no_telegram), st::introLink)
, _noTelegramCodeRequestId(0)
, _code(this, st::inpIntroCode, lang(lng_code_ph))
, _callTimer(this)
, _callStatus(intro()->getCallStatus())
, _checkRequest(this) {
	setGeometry(parent->innerRect());

	_next->setTextTransform(Ui::RoundButton::TextTransform::ToUpper);
	connect(_next, SIGNAL(clicked()), this, SLOT(onSubmitCode()));
	connect(_code, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(_callTimer, SIGNAL(timeout()), this, SLOT(onSendCall()));
	connect(_checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));
	connect(_noTelegramCode, SIGNAL(clicked()), this, SLOT(onNoTelegramCode()));

	updateDescText();

	if (!intro()->codeByTelegram()) {
		if (_callStatus.type == IntroWidget::CallWaiting) {
			_callTimer->start(1000);
		}
	}
}

void IntroCode::updateDescText() {
	_desc.setRichText(st::introFont, lang(intro()->codeByTelegram() ? lng_code_telegram : lng_code_desc));
	if (intro()->codeByTelegram()) {
		_noTelegramCode->show();
		_callTimer->stop();
	} else {
		_noTelegramCode->hide();
		_callStatus = intro()->getCallStatus();
		if (_callStatus.type == IntroWidget::CallWaiting && !_callTimer->isActive()) {
			_callTimer->start(1000);
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
	if (trivial || e->rect().intersects(_textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(_textRect, intro()->getPhone(), style::al_top);
		p.setFont(st::introFont->f);
		_desc.draw(p, _textRect.x(), _textRect.y() + _textRect.height() - 2 * st::introFont->height, _textRect.width(), style::al_top);
	}
	if (codeByTelegram) {
	} else {
		QString callText;
		switch (_callStatus.type) {
		case IntroWidget::CallWaiting: {
			if (_callStatus.timeout >= 3600) {
				callText = lng_code_call(lt_minutes, qsl("%1:%2").arg(_callStatus.timeout / 3600).arg((_callStatus.timeout / 60) % 60, 2, 10, QChar('0')), lt_seconds, qsl("%1").arg(_callStatus.timeout % 60, 2, 10, QChar('0')));
			} else {
				callText = lng_code_call(lt_minutes, QString::number(_callStatus.timeout / 60), lt_seconds, qsl("%1").arg(_callStatus.timeout % 60, 2, 10, QChar('0')));
			}
		} break;

		case IntroWidget::CallCalling: {
			callText = lang(lng_code_calling);
		} break;

		case IntroWidget::CallCalled: {
			callText = lang(lng_code_called);
		} break;
		}
		if (!callText.isEmpty()) {
			p.drawText(QRect(_textRect.left(), _code->y() + _code->height() + st::introCallSkip, st::introTextSize.width(), st::introErrorHeight), callText, style::al_center);
		}
	}
	if (_a_error.animating() || _error.length()) {
		p.setOpacity(a_errorAlpha.current());
		p.setFont(st::introErrorFont);
		p.setPen(st::introErrorFg);
		p.drawText(QRect(_textRect.left(), _next->y() + _next->height() + st::introErrorTop, st::introTextSize.width(), st::introErrorHeight), _error, style::al_center);
	}
}

void IntroCode::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		_next->move((width() - _next->width()) / 2, st::introBtnTop);
		_code->move((width() - _code->width()) / 2, st::introTextTop + st::introTextSize.height() + st::introCountry.top);
	}
	_textRect = QRect((width() - st::introTextSize.width()) / 2, st::introTextTop, st::introTextSize.width(), st::introTextSize.height());
	_noTelegramCode->move(_textRect.left() + (st::introTextSize.width() - _noTelegramCode->width()) / 2, _code->y() + _code->height() + st::introCallSkip + (st::introErrorHeight - _noTelegramCode->height()) / 2);
}

void IntroCode::showError(const QString &error) {
	if (!error.isEmpty()) _code->notaBene();
	if (!_a_error.animating() && error == _error) return;

	if (error.length()) {
		_error = error;
		a_errorAlpha.start(1);
	} else {
		a_errorAlpha.start(0);
	}
	_a_error.start();
}

void IntroCode::step_error(float64 ms, bool timer) {
	float64 dt = ms / st::introErrorDuration;

	if (dt >= 1) {
		_a_error.stop();
		a_errorAlpha.finish();
		if (!a_errorAlpha.current()) {
			_error.clear();
		}
	} else {
		a_errorAlpha.update(dt, anim::linear);
	}
	if (timer) update();
}

void IntroCode::activate() {
	IntroStep::activate();
	_code->setFocus();
}

void IntroCode::finished() {
	IntroStep::finished();
	_error.clear();
	a_errorAlpha = anim::fvalue(0);

	_sentCode.clear();
	_code->setDisabled(false);

	_callTimer->stop();
	_code->setText(QString());
	rpcClear();
}

void IntroCode::cancelled() {
	if (_sentRequest) {
		MTP::cancel(base::take(_sentRequest));
	}
	MTP::send(MTPauth_CancelCode(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash())));
}

void IntroCode::stopCheck() {
	_checkRequest->stop();
}

void IntroCode::onCheckRequest() {
	int32 status = MTP::state(_sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			if (_sentRequest) {
				MTP::cancel(base::take(_sentRequest));
				_sentCode.clear();
			}
			if (!_code->isEnabled()) {
				_code->setDisabled(false);
				_code->setFocus();
			}
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroCode::codeSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	_sentRequest = 0;
	_code->setDisabled(false);
	const auto &d(result.c_auth_authorization());
	if (d.vuser.type() != mtpc_user || !d.vuser.c_user().is_self()) { // wtf?
		showError(lang(lng_server_error));
		return;
	}
	cSetLoggedPhoneNumber(intro()->getPhone());
	intro()->finish(d.vuser);
}

bool IntroCode::codeSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showError(lang(lng_flood_error));
		_code->setDisabled(false);
		_code->setFocus();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	stopCheck();
	_sentRequest = 0;
	_code->setDisabled(false);
	const QString &err = error.type();
	if (err == qstr("PHONE_NUMBER_INVALID") || err == qstr("PHONE_CODE_EXPIRED")) { // show error
		intro()->onBack();
		return true;
	} else if (err == qstr("PHONE_CODE_EMPTY") || err == qstr("PHONE_CODE_INVALID")) {
		showError(lang(lng_bad_code));
		_code->notaBene();
		return true;
	} else if (err == qstr("PHONE_NUMBER_UNOCCUPIED")) { // success, need to signUp
		intro()->setCode(_sentCode);
		intro()->replaceStep(new IntroSignup(intro()));
		return true;
	} else if (err == qstr("SESSION_PASSWORD_NEEDED")) {
		intro()->setCode(_sentCode);
		_code->setDisabled(false);
		_checkRequest->start(1000);
		_sentRequest = MTP::send(MTPaccount_GetPassword(), rpcDone(&IntroCode::gotPassword), rpcFail(&IntroCode::codeSubmitFail));
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	_code->setFocus();
	return false;
}

void IntroCode::onInputChange() {
	showError(QString());
	if (_code->text().length() == 5) onSubmitCode();
}

void IntroCode::onSendCall() {
	if (_callStatus.type == IntroWidget::CallWaiting) {
		if (--_callStatus.timeout <= 0) {
			_callStatus.type = IntroWidget::CallCalling;
			_callTimer->stop();
			MTP::send(MTPauth_ResendCode(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash())), rpcDone(&IntroCode::callDone));
		} else {
			intro()->setCallStatus(_callStatus);
		}
	}
	update();
}

void IntroCode::callDone(const MTPauth_SentCode &v) {
	if (_callStatus.type == IntroWidget::CallCalling) {
		_callStatus.type = IntroWidget::CallCalled;
		intro()->setCallStatus(_callStatus);
		update();
	}
}

void IntroCode::gotPassword(const MTPaccount_Password &result) {
	stopCheck();
	_sentRequest = 0;
	_code->setDisabled(false);
	switch (result.type()) {
	case mtpc_account_noPassword: // should not happen
		_code->setFocus();
	break;

	case mtpc_account_password: {
		const auto &d(result.c_account_password());
		intro()->setPwdSalt(qba(d.vcurrent_salt));
		intro()->setHasRecovery(mtpIsTrue(d.vhas_recovery));
		intro()->setPwdHint(qs(d.vhint));
		intro()->replaceStep(new IntroPwdCheck(intro()));
	} break;
	}
}

void IntroCode::onSubmitCode() {
	if (_sentRequest) return;

	_code->setDisabled(true);
	setFocus();

	showError(QString());

	_checkRequest->start(1000);

	_sentCode = _code->text();
	intro()->setPwdSalt(QByteArray());
	intro()->setHasRecovery(false);
	intro()->setPwdHint(QString());
	_sentRequest = MTP::send(MTPauth_SignIn(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash()), MTP_string(_sentCode)), rpcDone(&IntroCode::codeSubmitDone), rpcFail(&IntroCode::codeSubmitFail));
}

void IntroCode::onNoTelegramCode() {
	if (_noTelegramCodeRequestId) return;
	_noTelegramCodeRequestId = MTP::send(MTPauth_ResendCode(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash())), rpcDone(&IntroCode::noTelegramCodeDone), rpcFail(&IntroCode::noTelegramCodeFail));
}

void IntroCode::noTelegramCodeDone(const MTPauth_SentCode &result) {
	if (result.type() != mtpc_auth_sentCode) {
		showError(lang(lng_server_error));
		return;
	}

	const auto &d(result.c_auth_sentCode());
	switch (d.vtype.type()) {
	case mtpc_auth_sentCodeTypeApp: intro()->setCodeByTelegram(true);
	case mtpc_auth_sentCodeTypeSms:
	case mtpc_auth_sentCodeTypeCall: intro()->setCodeByTelegram(false);
	case mtpc_auth_sentCodeTypeFlashCall: LOG(("Error: should not be flashcall!")); break;
	}
	if (d.has_next_type() && d.vnext_type.type() == mtpc_auth_codeTypeCall) {
		intro()->setCallStatus({ IntroWidget::CallWaiting, d.has_timeout() ? d.vtimeout.v : 60 });
	} else {
		intro()->setCallStatus({ IntroWidget::CallDisabled, 0 });
	}
	intro()->setCodeByTelegram(false);
	updateDescText();
}

bool IntroCode::noTelegramCodeFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		showError(lang(lng_flood_error));
		_code->setFocus();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	if (cDebug()) { // internal server error
		showError(error.type() + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	_code->setFocus();
	return false;
}

void IntroCode::onSubmit() {
	onSubmitCode();
}
