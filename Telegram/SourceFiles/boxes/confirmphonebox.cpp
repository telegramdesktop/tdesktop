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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/confirmphonebox.h"

#include "styles/style_boxes.h"
#include "boxes/confirmbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "mainwidget.h"
#include "lang.h"

namespace {

object_ptr<ConfirmPhoneBox> CurrentConfirmPhoneBox = { nullptr };

} // namespace

void ConfirmPhoneBox::start(const QString &phone, const QString &hash) {
	if (CurrentConfirmPhoneBox && CurrentConfirmPhoneBox->getPhone() != phone) {
		CurrentConfirmPhoneBox.destroyDelayed();
	}
	if (!CurrentConfirmPhoneBox) {
		CurrentConfirmPhoneBox = Box<ConfirmPhoneBox>(phone, hash);
	}
	CurrentConfirmPhoneBox->checkPhoneAndHash();
}

ConfirmPhoneBox::ConfirmPhoneBox(QWidget*, const QString &phone, const QString &hash)
: _phone(phone)
, _hash(hash)
, _callTimer(this) {
}

void ConfirmPhoneBox::checkPhoneAndHash() {
	if (_sendCodeRequestId) {
		return;
	}
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
	Ui::show(Box<InformBox>(errorText));
	if (this == CurrentConfirmPhoneBox) {
		CurrentConfirmPhoneBox.destroyDelayed();
	} else {
		deleteLater();
	}
	return true;
}

void ConfirmPhoneBox::setCallStatus(const CallStatus &status) {
	_callStatus = status;
	if (_callStatus.state == CallState::Waiting) {
		_callTimer->start(1000);
	}
}

void ConfirmPhoneBox::launch() {
	if (!CurrentConfirmPhoneBox) return;
	Ui::show(std::move(CurrentConfirmPhoneBox));
}

void ConfirmPhoneBox::prepare() {

	_about.create(this, st::confirmPhoneAboutLabel);
	TextWithEntities aboutText;
	auto formattedPhone = App::formatPhone(_phone);
	aboutText.text = lng_confirm_phone_about(lt_phone, formattedPhone);
	auto phonePosition = aboutText.text.indexOf(formattedPhone);
	if (phonePosition >= 0) {
		aboutText.entities.push_back(EntityInText(EntityInTextBold, phonePosition, formattedPhone.size()));
	}
	_about->setMarkedText(aboutText);

	_code.create(this, st::confirmPhoneCodeField, lang(lng_code_ph));

	setTitle(lang(lng_confirm_phone_title));

	addButton(lang(lng_confirm_phone_send), [this] { onSendCode(); });
	addButton(lang(lng_cancel), [this] { closeBox(); });

	setDimensions(st::boxWidth, st::usernamePadding.top() + _code->height() + st::usernameSkip + _about->height() + st::usernameSkip);

	connect(_code, SIGNAL(changed()), this, SLOT(onCodeChanged()));
	connect(_code, SIGNAL(submitted(bool)), this, SLOT(onSendCode()));

	connect(_callTimer, SIGNAL(timeout()), this, SLOT(onCallStatusTimer()));

	showChildren();
}

void ConfirmPhoneBox::onCallStatusTimer() {
	if (_callStatus.state == CallState::Waiting) {
		if (--_callStatus.timeout <= 0) {
			_callStatus.state = CallState::Calling;
			_callTimer->stop();
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
	Ui::show(Box<InformBox>(lng_confirm_phone_success(lt_phone, App::formatPhone(_phone))));
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
	BoxContent::paintEvent(e);

	Painter p(this);

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
		p.setPen(st::boxTextFgError);
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
	BoxContent::resizeEvent(e);

	_code->resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _code->height());
	_code->moveToLeft(st::usernamePadding.left(), st::usernamePadding.top());

	_about->moveToLeft(st::usernamePadding.left(), _code->y() + _code->height() + st::usernameSkip);
}

void ConfirmPhoneBox::setInnerFocus() {
	_code->setFocusFast();
}

ConfirmPhoneBox::~ConfirmPhoneBox() {
	if (_sendCodeRequestId) {
		MTP::cancel(_sendCodeRequestId);
	}
}
