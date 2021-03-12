/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_password_check.h"

#include "intro/intro_widget.h"
#include "core/file_utilities.h"
#include "core/core_cloud_password.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "intro/intro_signup.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "main/main_account.h"
#include "base/openssl_help.h"
#include "styles/style_intro.h"
#include "styles/style_boxes.h"

namespace Intro {
namespace details {

PasswordCheckWidget::PasswordCheckWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _request(getData()->pwdRequest)
, _hasRecovery(getData()->hasRecovery)
, _notEmptyPassport(getData()->pwdNotEmptyPassport)
, _hint(getData()->pwdHint)
, _pwdField(this, st::introPassword, tr::lng_signin_password())
, _pwdHint(this, st::introPasswordHint)
, _codeField(this, st::introPassword, tr::lng_signin_code())
, _toRecover(this, tr::lng_signin_recover(tr::now))
, _toPassword(this, tr::lng_signin_try_password(tr::now)) {
	Expects(!!_request);

	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_toRecover->addClickHandler([=] { toRecover(); });
	_toPassword->addClickHandler([=] { toPassword(); });
	connect(_pwdField, &Ui::PasswordInput::changed, [=] { hideError(); });
	connect(_codeField, &Ui::InputField::changed, [=] { hideError(); });

	setTitleText(tr::lng_signin_title());
	updateDescriptionText();

	if (_hint.isEmpty()) {
		_pwdHint->hide();
	} else {
		_pwdHint->setText(
			tr::lng_signin_hint(tr::now, lt_password_hint, _hint));
	}
	_codeField->hide();
	_toPassword->hide();

	setMouseTracking(true);
}

void PasswordCheckWidget::refreshLang() {
	if (_toRecover) {
		_toRecover->setText(tr::lng_signin_recover(tr::now));
	}
	if (_toPassword) {
		_toPassword->setText(
			tr::lng_signin_try_password(tr::now));
	}
	if (!_hint.isEmpty()) {
		_pwdHint->setText(
			tr::lng_signin_hint(tr::now, lt_password_hint, _hint));
	}
	updateControlsGeometry();
}

int PasswordCheckWidget::errorTop() const {
	return contentTop() + st::introErrorBelowLinkTop;
}

void PasswordCheckWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void PasswordCheckWidget::updateControlsGeometry() {
	_pwdField->moveToLeft(contentLeft(), contentTop() + st::introPasswordTop);
	_pwdHint->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introPasswordHintTop);
	_codeField->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto linkTop = _codeField->y() + _codeField->height() + st::introLinkTop;
	_toRecover->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
	_toPassword->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
}

void PasswordCheckWidget::setInnerFocus() {
	if (_pwdField->isHidden()) {
		_codeField->setFocusFast();
	} else {
		_pwdField->setFocusFast();
	}
}

void PasswordCheckWidget::activate() {
	if (_pwdField->isHidden() && _codeField->isHidden()) {
		Step::activate();
		_pwdField->show();
		_pwdHint->show();
		_toRecover->show();
	}
	setInnerFocus();
}

void PasswordCheckWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

void PasswordCheckWidget::pwdSubmitDone(bool recover, const MTPauth_Authorization &result) {
	_sentRequest = 0;
	if (recover) {
		cSetPasswordRecovered(true);
	}
	auto &d = result.c_auth_authorization();
	if (d.vuser().type() != mtpc_user || !d.vuser().c_user().is_self()) { // wtf?
		showError(rpl::single(Lang::Hard::ServerError()));
		return;
	}
	finish(d.vuser());
}

void PasswordCheckWidget::pwdSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		_sentRequest = 0;
		showError(tr::lng_flood_error());
		_pwdField->showError();
		return;
	}

	_sentRequest = 0;
	const auto &type = error.type();
	if (type == qstr("PASSWORD_HASH_INVALID")
		|| type == qstr("SRP_PASSWORD_CHANGED")) {
		showError(tr::lng_signin_bad_password());
		_pwdField->selectAll();
		_pwdField->showError();
	} else if (type == qstr("PASSWORD_EMPTY")
		|| type == qstr("AUTH_KEY_UNREGISTERED")) {
		goBack();
	} else if (type == qstr("SRP_ID_INVALID")) {
		handleSrpIdInvalid();
	} else {
		if (Logs::DebugEnabled()) { // internal server error
			showError(rpl::single(type + ": " + error.description()));
		} else {
			showError(rpl::single(Lang::Hard::ServerError()));
		}
		_pwdField->setFocus();
	}
}

void PasswordCheckWidget::handleSrpIdInvalid() {
	const auto now = crl::now();
	if (_lastSrpIdInvalidTime > 0
		&& now - _lastSrpIdInvalidTime < Core::kHandleSrpIdInvalidTimeout) {
		_request.id = 0;
		showError(rpl::single(Lang::Hard::ServerError()));
	} else {
		_lastSrpIdInvalidTime = now;
		requestPasswordData();
	}
}

void PasswordCheckWidget::checkPasswordHash() {
	if (_request.id) {
		passwordChecked();
	} else {
		requestPasswordData();
	}
}

void PasswordCheckWidget::requestPasswordData() {
	api().request(base::take(_sentRequest)).cancel();
	_sentRequest = api().request(
		MTPaccount_GetPassword()
	).done([=](const MTPaccount_Password &result) {
		_sentRequest = 0;
		result.match([&](const MTPDaccount_password &data) {
			auto request = Core::ParseCloudPasswordCheckRequest(data);
			if (request && request.id) {
				_request = std::move(request);
			} else {
				// Maybe the password was removed? Just submit it once again.
			}
			passwordChecked();
		});
	}).send();
}

void PasswordCheckWidget::passwordChecked() {
	const auto check = Core::ComputeCloudPasswordCheck(
		_request,
		_passwordHash);
	if (!check) {
		return serverError();
	}
	_request.id = 0;
	_sentRequest = api().request(
		MTPauth_CheckPassword(check.result)
	).done([=](const MTPauth_Authorization &result) {
		pwdSubmitDone(false, result);
	}).fail([=](const MTP::Error &error) {
		pwdSubmitFail(error);
	}).handleFloodErrors().send();
}

void PasswordCheckWidget::serverError() {
	showError(rpl::single(Lang::Hard::ServerError()));
}

void PasswordCheckWidget::codeSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error());
		_codeField->showError();
		return;
	}

	_sentRequest = 0;
	const auto &type = error.type();
	if (type == qstr("PASSWORD_EMPTY")
		|| type == qstr("AUTH_KEY_UNREGISTERED")) {
		goBack();
	} else if (type == qstr("PASSWORD_RECOVERY_NA")) {
		recoverStartFail(error);
	} else if (type == qstr("PASSWORD_RECOVERY_EXPIRED")) {
		_emailPattern = QString();
		toPassword();
	} else if (type == qstr("CODE_INVALID")) {
		showError(tr::lng_signin_wrong_code());
		_codeField->selectAll();
		_codeField->showError();
	} else {
		if (Logs::DebugEnabled()) { // internal server error
			showError(rpl::single(type + ": " + error.description()));
		} else {
			showError(rpl::single(Lang::Hard::ServerError()));
		}
		_codeField->setFocus();
	}
}

void PasswordCheckWidget::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_emailPattern = qs(result.c_auth_passwordRecovery().vemail_pattern());
	updateDescriptionText();
}

void PasswordCheckWidget::recoverStartFail(const MTP::Error &error) {
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_pwdField->setFocus();
	updateDescriptionText();
	update();
	hideError();
}

void PasswordCheckWidget::toRecover() {
	if (_hasRecovery) {
		if (_sentRequest) {
			api().request(base::take(_sentRequest)).cancel();
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
			api().request(
				MTPauth_RequestPasswordRecovery()
			).done([=](const MTPauth_PasswordRecovery &result) {
				recoverStarted(result);
			}).fail([=](const MTP::Error &error) {
				recoverStartFail(error);
			}).send();
		}
	} else {
		Ui::show(Box<InformBox>(
			tr::lng_signin_no_email_forgot(tr::now),
			[=] { showReset(); }));
	}
}

void PasswordCheckWidget::toPassword() {
	Ui::show(Box<InformBox>(
		tr::lng_signin_cant_email_forgot(tr::now),
		[=] { showReset(); }));
}

void PasswordCheckWidget::showReset() {
	if (_sentRequest) {
		api().request(base::take(_sentRequest)).cancel();
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

void PasswordCheckWidget::updateDescriptionText() {
	auto pwdHidden = _pwdField->isHidden();
	auto emailPattern = _emailPattern;
	setDescriptionText(pwdHidden
		? tr::lng_signin_recover_desc(lt_email, rpl::single(emailPattern))
		: tr::lng_signin_desc());
}

void PasswordCheckWidget::submit() {
	if (_sentRequest) {
		return;
	}
	if (_pwdField->isHidden()) {
		auto code = _codeField->getLastText().trimmed();
		if (code.isEmpty()) {
			_codeField->showError();
			return;
		}
		const auto send = crl::guard(this, [=] {
			_sentRequest = api().request(
				MTPauth_RecoverPassword(MTP_string(code))
			).done([=](const MTPauth_Authorization &result) {
				pwdSubmitDone(true, result);
			}).fail([=](const MTP::Error &error) {
				codeSubmitFail(error);
			}).handleFloodErrors().send();
		});

		if (_notEmptyPassport) {
			const auto confirmed = [=](Fn<void()> &&close) {
				send();
				close();
			};
			Ui::show(Box<ConfirmBox>(
				tr::lng_cloud_password_passport_losing(tr::now),
				tr::lng_continue(tr::now),
				confirmed));
		} else {
			send();
		}
	} else {
		hideError();

		const auto password = _pwdField->getLastText().toUtf8();
		_passwordHash = Core::ComputeCloudPasswordHash(
			_request.algo,
			bytes::make_span(password));
		checkPasswordHash();
	}
}

rpl::producer<QString> PasswordCheckWidget::nextButtonText() const {
	return tr::lng_intro_submit();
}

} // namespace details
} // namespace Intro
