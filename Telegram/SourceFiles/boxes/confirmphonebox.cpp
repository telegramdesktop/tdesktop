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
#include "boxes/confirmphonebox.h"

#include "styles/style_boxes.h"
#include "boxes/confirmbox.h"
#include "mainwidget.h"
#include "lang.h"

namespace {

QPointer<ConfirmPhoneBox> CurrentConfirmPhoneBox = nullptr;

} // namespace

void ConfirmPhoneBox::start(const QString &phone, const QString &hash) {
	if (CurrentConfirmPhoneBox) {
		if (CurrentConfirmPhoneBox->getPhone() == phone) return;
		delete CurrentConfirmPhoneBox;
	}
	if (auto main = App::main()) {
		CurrentConfirmPhoneBox = new ConfirmPhoneBox(main, phone, hash);
	}
}

ConfirmPhoneBox::ConfirmPhoneBox(QWidget *parent, const QString &phone, const QString &hash) : AbstractBox(st::boxWidth)
, _phone(phone)
, _hash(hash) {
	setParent(parent);

	MTPaccount_SendConfirmPhoneCode::Flags flags = 0;
	_sendCodeRequestId = MTP::send(MTPaccount_SendConfirmPhoneCode(MTP_flags(flags), MTP_string(_hash), MTPBool()), rpcDone(&ConfirmPhoneBox::sendCodeDone), rpcFail(&ConfirmPhoneBox::sendCodeFail));
}

void ConfirmPhoneBox::sendCodeDone(const MTPauth_SentCode &result) {
	_sendCodeRequestId = 0;

	auto &resultInner = result.c_auth_sentCode();
	switch (resultInner.vtype.type()) {
	case mtpc_auth_sentCodeTypeApp: LOG(("Error: should not be in-app code!")); break;
	case mtpc_auth_sentCodeTypeSms: _sentCodeLength = resultInner.vtype.c_auth_sentCodeTypeSms().vlength.v; break;
	case mtpc_auth_sentCodeTypeCall: _sentCodeLength = resultInner.vtype.c_auth_sentCodeTypeCall().vlength.v; break;
	case mtpc_auth_sentCodeTypeFlashCall: LOG(("Error: should not be flashcall!")); break;
	}
	_phoneHash = qs(resultInner.vphone_code_hash);
	if (resultInner.has_next_type() && resultInner.vnext_type.type() == mtpc_auth_codeTypeCall) {
		setCallStatus({ CallState::Waiting, resultInner.has_timeout() ? resultInner.vtimeout.v : 60 });
	} else {
		setCallStatus({ CallState::Disabled, 0 });
	}
	launch();
}

bool ConfirmPhoneBox::sendCodeFail(const RPCError &error) {
	auto errorText = lang(lng_server_error);
	if (MTP::isFloodError(error)) {
		errorText = lang(lng_flood_error);
	} else if (MTP::isDefaultHandledError(error)) {
		return false;
	} else if (error.code() == 400) {
		errorText = lang(lng_confirm_phone_link_invalid);
	}
	_sendCodeRequestId = 0;
	Ui::showLayer(new InformBox(errorText));
	deleteLater();
	return true;
}

void ConfirmPhoneBox::setCallStatus(const CallStatus &status) {
	_callStatus = status;
	if (_callStatus.state == CallState::Waiting) {
		_callTimer.start(1000);
	}
}

void ConfirmPhoneBox::launch() {
	setBlueTitle(true);

	_about = new FlatLabel(this, st::confirmPhoneAboutLabel);
	TextWithEntities aboutText;
	auto formattedPhone = App::formatPhone(_phone);
	aboutText.text = lng_confirm_phone_about(lt_phone, formattedPhone);
	auto phonePosition = aboutText.text.indexOf(formattedPhone);
	if (phonePosition >= 0) {
		aboutText.entities.push_back(EntityInText(EntityInTextBold, phonePosition, formattedPhone.size()));
	}
	_about->setMarkedText(aboutText);

	_code = new InputField(this, st::confirmPhoneCodeField, lang(lng_code_ph));

	_send = new BoxButton(this, lang(lng_confirm_phone_send), st::defaultBoxButton);
	_cancel = new BoxButton(this, lang(lng_cancel), st::cancelBoxButton);

	setMaxHeight(st::boxTitleHeight + st::usernamePadding.top() + _code->height() + st::usernameSkip + _about->height() + st::usernameSkip + _send->height() + st::boxButtonPadding.bottom());

	connect(_send, SIGNAL(clicked()), this, SLOT(onSendCode()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(_code, SIGNAL(changed()), this, SLOT(onCodeChanged()));
	connect(_code, SIGNAL(submitted(bool)), this, SLOT(onSendCode()));

	connect(&_callTimer, SIGNAL(timeout()), this, SLOT(onCallStatusTimer()));

	prepare();

	Ui::showLayer(this);
}

void ConfirmPhoneBox::onCallStatusTimer() {
	if (_callStatus.state == CallState::Waiting) {
		if (--_callStatus.timeout <= 0) {
			_callStatus.state = CallState::Calling;
			_callTimer.stop();
			MTP::send(MTPauth_ResendCode(MTP_string(_phone), MTP_string(_phoneHash)), rpcDone(&ConfirmPhoneBox::callDone));
		}
	}
	update();
}

void ConfirmPhoneBox::callDone(const MTPauth_SentCode &result) {
	if (_callStatus.state == CallState::Calling) {
		_callStatus.state = CallState::Called;
		update();
	}
}

void ConfirmPhoneBox::onSendCode() {
	if (_sendCodeRequestId) {
		return;
	}
	auto code = _code->getLastText();
	if (code.isEmpty()) {
		_code->showError();
		return;
	}

	_code->setDisabled(true);
	setFocus();

	showError(QString());

	_sendCodeRequestId = MTP::send(MTPaccount_ConfirmPhone(MTP_string(_phoneHash), MTP_string(_code->getLastText())), rpcDone(&ConfirmPhoneBox::confirmDone), rpcFail(&ConfirmPhoneBox::confirmFail));
}

void ConfirmPhoneBox::confirmDone(const MTPBool &result) {
	_sendCodeRequestId = 0;
	Ui::showLayer(new InformBox(lng_confirm_phone_success(lt_phone, App::formatPhone(_phone))));
}

bool ConfirmPhoneBox::confirmFail(const RPCError &error) {
	auto errorText = lang(lng_server_error);
	if (MTP::isFloodError(error)) {
		errorText = lang(lng_flood_error);
	} else if (MTP::isDefaultHandledError(error)) {
		return false;
	} else {
		auto &errorType = error.type();
		if (errorType == qstr("PHONE_CODE_EMPTY") || errorType == qstr("PHONE_CODE_INVALID")) {
			errorText = lang(lng_bad_code);
		}
	}
	_sendCodeRequestId = 0;
	_code->setDisabled(false);
	_code->setFocus();
	showError(errorText);
	return true;
}

void ConfirmPhoneBox::onCodeChanged() {
	if (_fixing) return;

	_fixing = true;
	QString newText, now = _code->getLastText();
	int oldPos = _code->textCursor().position(), newPos = -1;
	int oldLen = now.size(), digitCount = 0;
	for_const (auto ch, now) {
		if (ch.isDigit()) {
			++digitCount;
		}
	}

	if (_sentCodeLength > 0 && digitCount > _sentCodeLength) {
		digitCount = _sentCodeLength;
	}
	bool strict = (_sentCodeLength > 0 && digitCount == _sentCodeLength);

	newText.reserve(oldLen);
	int i = 0;
	for_const (auto ch, now) {
		if (i++ == oldPos) {
			newPos = newText.length();
		}
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			newText += ch;
			if (strict && !digitCount) {
				break;
			}
		}
	}
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != now) {
		now = newText;
		_code->setText(now);
		_code->setCursorPosition(newPos);
	}
	_fixing = false;

	showError(QString());
	if (strict) {
		onSendCode();
	}
}

void ConfirmPhoneBox::showError(const QString &error) {
	_error = error;
	if (!_error.isEmpty()) {
		_code->showError();
	}
	update();
}

void ConfirmPhoneBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_confirm_phone_title));

	p.setFont(st::boxTextFont);
	auto callText = getCallText();
	if (!callText.isEmpty()) {
		p.setPen(st::usernameDefaultFg);
		auto callTextRectLeft = st::usernamePadding.left();
		auto callTextRectTop = _about->y() + _about->height();
		auto callTextRectWidth = width() - 2 * st::usernamePadding.left();
		auto callTextRect = QRect(callTextRectLeft, callTextRectTop, callTextRectWidth, st::usernameSkip);
		p.drawText(callTextRect, callText, style::al_left);
	}
	auto errorText = _error;
	if (errorText.isEmpty()) {
		p.setPen(st::usernameDefaultFg);
		errorText = lang(lng_confirm_phone_enter_code);
	} else {
		p.setPen(st::setErrColor);
	}
	auto errorTextRectLeft = st::usernamePadding.left();
	auto errorTextRectTop = _code->y() + _code->height();
	auto errorTextRectWidth = width() - 2 * st::usernamePadding.left();
	auto errorTextRect = QRect(errorTextRectLeft, errorTextRectTop, errorTextRectWidth, st::usernameSkip);
	p.drawText(errorTextRect, errorText, style::al_left);
}

QString ConfirmPhoneBox::getCallText() const {
	switch (_callStatus.state) {
	case CallState::Waiting: {
		if (_callStatus.timeout >= 3600) {
			return lng_code_call(lt_minutes, qsl("%1:%2").arg(_callStatus.timeout / 3600).arg((_callStatus.timeout / 60) % 60, 2, 10, QChar('0')), lt_seconds, qsl("%1").arg(_callStatus.timeout % 60, 2, 10, QChar('0')));
		}
		return lng_code_call(lt_minutes, QString::number(_callStatus.timeout / 60), lt_seconds, qsl("%1").arg(_callStatus.timeout % 60, 2, 10, QChar('0')));
	} break;
	case CallState::Calling: return lang(lng_code_calling);
	case CallState::Called: return lang(lng_code_called);
	}
	return QString();
}

void ConfirmPhoneBox::resizeEvent(QResizeEvent *e) {
	_code->resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _code->height());
	_code->moveToLeft(st::usernamePadding.left(), st::boxTitleHeight + st::usernamePadding.top());

	_about->moveToLeft(st::usernamePadding.left(), _code->y() + _code->height() + st::usernameSkip);

	_send->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _send->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _send->width() + st::boxButtonPadding.left(), _send->y());

	AbstractBox::resizeEvent(e);
}

ConfirmPhoneBox::~ConfirmPhoneBox() {
	if (_sendCodeRequestId) {
		MTP::cancel(_sendCodeRequestId);
	}
}
