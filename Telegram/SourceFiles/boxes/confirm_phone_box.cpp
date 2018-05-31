/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/confirm_phone_box.h"

#include "styles/style_boxes.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"

namespace {

object_ptr<ConfirmPhoneBox> CurrentConfirmPhoneBox = { nullptr };

} // namespace

void SentCodeField::fix() {
	if (_fixing) return;

	_fixing = true;
	auto newText = QString();
	auto now = getLastText();
	auto oldPos = textCursor().position();
	auto newPos = -1;
	auto oldLen = now.size();
	auto digitCount = 0;
	for_const (auto ch, now) {
		if (ch.isDigit()) {
			++digitCount;
		}
	}

	if (_autoSubmitLength > 0 && digitCount > _autoSubmitLength) {
		digitCount = _autoSubmitLength;
	}
	auto strict = (_autoSubmitLength > 0 && digitCount == _autoSubmitLength);

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
		setText(now);
		setCursorPosition(newPos);
	}
	_fixing = false;

	if (_changedCallback) {
		_changedCallback();
	}
	if (strict && _submitCallback) {
		_submitCallback();
	}
}

SentCodeCall::SentCodeCall(QObject *parent, base::lambda_once<void()> callCallback, base::lambda<void()> updateCallback)
: _timer(parent)
, _call(std::move(callCallback))
, _update(std::move(updateCallback)) {
	_timer->connect(_timer, &QTimer::timeout, [this] {
		if (_status.state == State::Waiting) {
			if (--_status.timeout <= 0) {
				_status.state = State::Calling;
				_timer->stop();
				if (_call) {
					_call();
				}
			}
		}
		if (_update) {
			_update();
		}
	});
}

void SentCodeCall::setStatus(const Status &status) {
	_status = status;
	if (_status.state == State::Waiting) {
		_timer->start(1000);
	}
}

QString SentCodeCall::getText() const {
	switch (_status.state) {
	case State::Waiting: {
		if (_status.timeout >= 3600) {
			return lng_code_call(lt_minutes, qsl("%1:%2").arg(_status.timeout / 3600).arg((_status.timeout / 60) % 60, 2, 10, QChar('0')), lt_seconds, qsl("%1").arg(_status.timeout % 60, 2, 10, QChar('0')));
		}
		return lng_code_call(lt_minutes, QString::number(_status.timeout / 60), lt_seconds, qsl("%1").arg(_status.timeout % 60, 2, 10, QChar('0')));
	} break;
	case State::Calling: return lang(lng_code_calling);
	case State::Called: return lang(lng_code_called);
	}
	return QString();
}

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
, _call(this, [this] { sendCall(); }, [this] { update(); }) {
}

void ConfirmPhoneBox::sendCall() {
	MTP::send(MTPauth_ResendCode(MTP_string(_phone), MTP_string(_phoneHash)), rpcDone(&ConfirmPhoneBox::callDone));
}

void ConfirmPhoneBox::checkPhoneAndHash() {
	if (_sendCodeRequestId) {
		return;
	}
	_sendCodeRequestId = MTP::send(MTPaccount_SendConfirmPhoneCode(MTP_flags(0), MTP_string(_hash), MTPBool()), rpcDone(&ConfirmPhoneBox::sendCodeDone), rpcFail(&ConfirmPhoneBox::sendCodeFail));
}

void ConfirmPhoneBox::sendCodeDone(const MTPauth_SentCode &result) {
	Expects(result.type() == mtpc_auth_sentCode);
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
		_call.setStatus({ SentCodeCall::State::Waiting, resultInner.has_timeout() ? resultInner.vtimeout.v : 60 });
	}
	launch();
}

bool ConfirmPhoneBox::sendCodeFail(const RPCError &error) {
	auto errorText = Lang::Hard::ServerError();
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

	_code.create(this, st::confirmPhoneCodeField, langFactory(lng_code_ph));
	_code->setAutoSubmit(_sentCodeLength, [=] { sendCode(); });
	_code->setChangedCallback([=] { showError(QString()); });

	setTitle(langFactory(lng_confirm_phone_title));

	addButton(langFactory(lng_confirm_phone_send), [=] { sendCode(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	setDimensions(st::boxWidth, st::usernamePadding.top() + _code->height() + st::usernameSkip + _about->height() + st::usernameSkip);

	connect(_code, &Ui::InputField::submitted, [=] { sendCode(); });

	showChildren();
}

void ConfirmPhoneBox::callDone(const MTPauth_SentCode &result) {
	_call.callDone();
}

void ConfirmPhoneBox::sendCode() {
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
	auto errorText = Lang::Hard::ServerError();
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
	auto callText = _call.getText();
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
