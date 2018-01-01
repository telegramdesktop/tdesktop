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
#include "intro/intropwdcheck.h"

#include "styles/style_intro.h"
#include "styles/style_boxes.h"
#include "core/file_utilities.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "intro/introsignup.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"

namespace Intro {

PwdCheckWidget::PwdCheckWidget(QWidget *parent, Widget::Data *data) : Step(parent, data)
, _salt(getData()->pwdSalt)
, _hasRecovery(getData()->hasRecovery)
, _hint(getData()->pwdHint)
, _pwdField(this, st::introPassword, langFactory(lng_signin_password))
, _pwdHint(this, st::introPasswordHint)
, _codeField(this, st::introPassword, langFactory(lng_signin_code))
, _toRecover(this, lang(lng_signin_recover))
, _toPassword(this, lang(lng_signin_try_password))
, _checkRequest(this) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	connect(_checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));
	connect(_toRecover, SIGNAL(clicked()), this, SLOT(onToRecover()));
	connect(_toPassword, SIGNAL(clicked()), this, SLOT(onToPassword()));
	connect(_pwdField, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(_codeField, SIGNAL(changed()), this, SLOT(onInputChange()));

	setTitleText(langFactory(lng_signin_title));
	updateDescriptionText();
	setErrorBelowLink(true);

	if (_hint.isEmpty()) {
		_pwdHint->hide();
	} else {
		_pwdHint->setText(lng_signin_hint(lt_password_hint, _hint));
	}
	_codeField->hide();
	_toPassword->hide();

	setMouseTracking(true);
}

void PwdCheckWidget::refreshLang() {
	if (_toRecover) _toRecover->setText(lang(lng_signin_recover));
	if (_toPassword) _toPassword->setText(lang(lng_signin_try_password));
	if (!_hint.isEmpty()) {
		_pwdHint->setText(lng_signin_hint(lt_password_hint, _hint));
	}
	updateControlsGeometry();
}

void PwdCheckWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void PwdCheckWidget::updateControlsGeometry() {
	_pwdField->moveToLeft(contentLeft(), contentTop() + st::introPasswordTop);
	_pwdHint->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introPasswordHintTop);
	_codeField->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto linkTop = _codeField->y() + _codeField->height() + st::introLinkTop;
	_toRecover->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
	_toPassword->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
}

void PwdCheckWidget::setInnerFocus() {
	if (_pwdField->isHidden()) {
		_codeField->setFocusFast();
	} else {
		_pwdField->setFocusFast();
	}
}

void PwdCheckWidget::activate() {
	if (_pwdField->isHidden() && _codeField->isHidden()) {
		Step::activate();
		_pwdField->show();
		_pwdHint->show();
		_toRecover->show();
	}
	setInnerFocus();
}

void PwdCheckWidget::cancelled() {
	MTP::cancel(base::take(_sentRequest));
}

void PwdCheckWidget::stopCheck() {
	_checkRequest->stop();
}

void PwdCheckWidget::onCheckRequest() {
	auto status = MTP::state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(base::take(_sentRequest));
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void PwdCheckWidget::pwdSubmitDone(bool recover, const MTPauth_Authorization &result) {
	_sentRequest = 0;
	stopCheck();
	if (recover) {
		cSetPasswordRecovered(true);
	}
	auto &d = result.c_auth_authorization();
	if (d.vuser.type() != mtpc_user || !d.vuser.c_user().is_self()) { // wtf?
		showError(&Lang::Hard::ServerError);
		return;
	}
	finish(d.vuser);
}

bool PwdCheckWidget::pwdSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		_sentRequest = 0;
		stopCheck();
		showError(langFactory(lng_flood_error));
		_pwdField->showError();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	_sentRequest = 0;
	stopCheck();
	auto &err = error.type();
	if (err == qstr("PASSWORD_HASH_INVALID")) {
		showError(langFactory(lng_signin_bad_password));
		_pwdField->selectAll();
		_pwdField->showError();
		return true;
	} else if (err == qstr("PASSWORD_EMPTY")) {
		goBack();
	}
	if (cDebug()) { // internal server error
		auto text = err + ": " + error.description();
		showError([text] { return text; });
	} else {
		showError(&Lang::Hard::ServerError);
	}
	_pwdField->setFocus();
	return false;
}

bool PwdCheckWidget::codeSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		showError(langFactory(lng_flood_error));
		_codeField->showError();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	_sentRequest = 0;
	stopCheck();
	const QString &err = error.type();
	if (err == qstr("PASSWORD_EMPTY")) {
		goBack();
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_NA")) {
		recoverStartFail(error);
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_EXPIRED")) {
		_emailPattern = QString();
		onToPassword();
		return true;
	} else if (err == qstr("CODE_INVALID")) {
		showError(langFactory(lng_signin_wrong_code));
		_codeField->selectAll();
		_codeField->showError();
		return true;
	}
	if (cDebug()) { // internal server error
		auto text = err + ": " + error.description();
		showError([text] { return text; });
	} else {
		showError(&Lang::Hard::ServerError);
	}
	_codeField->setFocus();
	return false;
}

void PwdCheckWidget::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_emailPattern = qs(result.c_auth_passwordRecovery().vemail_pattern);
	updateDescriptionText();
}

bool PwdCheckWidget::recoverStartFail(const RPCError &error) {
	stopCheck();
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_pwdField->setFocus();
	updateDescriptionText();
	update();
	hideError();
	return true;
}

void PwdCheckWidget::onToRecover() {
	if (_hasRecovery) {
		if (_sentRequest) {
			MTP::cancel(base::take(_sentRequest));
		}
		hideError();
		_toRecover->hide();
		_toPassword->show();
		_pwdField->hide();
		_pwdHint->hide();
		_pwdField->setText(QString());
		_codeField->show();
		_codeField->setFocus();
		updateDescriptionText();
		if (_emailPattern.isEmpty()) {
			MTP::send(MTPauth_RequestPasswordRecovery(), rpcDone(&PwdCheckWidget::recoverStarted), rpcFail(&PwdCheckWidget::recoverStartFail));
		}
	} else {
		Ui::show(Box<InformBox>(lang(lng_signin_no_email_forgot), [this] { showReset(); }));
	}
}

void PwdCheckWidget::onToPassword() {
	Ui::show(Box<InformBox>(lang(lng_signin_cant_email_forgot), [this] { showReset(); }));
}

void PwdCheckWidget::showReset() {
	if (_sentRequest) {
		MTP::cancel(base::take(_sentRequest));
	}
	_toRecover->show();
	_toPassword->hide();
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_codeField->setText(QString());
	_pwdField->setFocus();
	showResetButton();
	updateDescriptionText();
	update();
}

void PwdCheckWidget::updateDescriptionText() {
	auto pwdHidden = _pwdField->isHidden();
	auto emailPattern = _emailPattern;
	setDescriptionText([pwdHidden, emailPattern] {
		return pwdHidden ? lng_signin_recover_desc(lt_email, emailPattern) : lang(lng_signin_desc);
	});
}

void PwdCheckWidget::onInputChange() {
	hideError();
}

void PwdCheckWidget::submit() {
	if (_sentRequest) return;
	if (_pwdField->isHidden()) {
		auto code = _codeField->getLastText().trimmed();
		if (code.isEmpty()) {
			_codeField->showError();
			return;
		}

		_sentRequest = MTP::send(MTPauth_RecoverPassword(MTP_string(code)), rpcDone(&PwdCheckWidget::pwdSubmitDone, true), rpcFail(&PwdCheckWidget::codeSubmitFail));
	} else {
		hideError();

		QByteArray pwdData = _salt + _pwdField->getLastText().toUtf8() + _salt, pwdHash(32, Qt::Uninitialized);
		hashSha256(pwdData.constData(), pwdData.size(), pwdHash.data());
		_sentRequest = MTP::send(MTPauth_CheckPassword(MTP_bytes(pwdHash)), rpcDone(&PwdCheckWidget::pwdSubmitDone, false), rpcFail(&PwdCheckWidget::pwdSubmitFail));
	}
}

QString PwdCheckWidget::nextButtonText() const {
	return lang(lng_intro_submit);
}

} // namespace Intro
