/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/passcode_box.h"

#include "base/bytes.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/confirm_phone_box.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "core/application.h"
#include "storage/storage_domain.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "passport/passport_encryption.h"
#include "passport/passport_panel_edit_contact.h"
#include "settings/settings_privacy_security.h"
#include "styles/style_layers.h"
#include "styles/style_passport.h"
#include "styles/style_boxes.h"

namespace {

enum class PasswordErrorType {
	None,
	NoPassword,
	Later,
};

void SetCloudPassword(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	session->api().passwordState(
	) | rpl::start_with_next([=] {
		using namespace Settings;
		const auto weak = Ui::MakeWeak(box);
		if (CheckEditCloudPassword(session)) {
			box->getDelegate()->show(
				EditCloudPasswordBox(session));
		} else {
			box->getDelegate()->show(CloudPasswordAppOutdatedBox());
		}
		if (weak) {
			weak->closeBox();
		}
	}, box->lifetime());
}

void TransferPasswordError(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		TextWithEntities &&about,
		PasswordErrorType error) {
	box->setTitle(tr::lng_rights_transfer_check());
	box->setWidth(st::transferCheckWidth);

	auto text = std::move(about).append('\n').append('\n').append(
		tr::lng_rights_transfer_check_password(
			tr::now,
			Ui::Text::RichLangValue)
	).append('\n').append('\n').append(
		tr::lng_rights_transfer_check_session(
			tr::now,
			Ui::Text::RichLangValue)
	);
	if (error == PasswordErrorType::Later) {
		text.append('\n').append('\n').append(
			tr::lng_rights_transfer_check_later(
				tr::now,
				Ui::Text::RichLangValue));
	}
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(text),
		st::boxLabel));
	if (error == PasswordErrorType::Later) {
		box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	} else {
		box->addButton(tr::lng_rights_transfer_set_password(), [=] {
			SetCloudPassword(box, session);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}
}

} // namespace

PasscodeBox::CloudFields PasscodeBox::CloudFields::From(
		const Core::CloudPasswordState &current) {
	auto result = CloudFields();
	result.curRequest = current.request;
	result.newAlgo = current.newPassword;
	result.newSecureSecretAlgo = current.newSecureSecret;
	result.hasRecovery = current.hasRecovery;
	result.notEmptyPassport = current.notEmptyPassport;
	result.hint = current.hint;
	return result;
}

PasscodeBox::PasscodeBox(
	QWidget*,
	not_null<Main::Session*> session,
	bool turningOff)
: _session(session)
, _api(&_session->mtp())
, _turningOff(turningOff)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _oldPasscode(this, st::defaultInputField, tr::lng_passcode_enter_old())
, _newPasscode(
	this,
	st::defaultInputField,
	session->domain().local().hasLocalPasscode()
		? tr::lng_passcode_enter_new()
		: tr::lng_passcode_enter_first())
, _reenterPasscode(this, st::defaultInputField, tr::lng_passcode_confirm_new())
, _passwordHint(this, st::defaultInputField, tr::lng_cloud_password_hint())
, _recoverEmail(this, st::defaultInputField, tr::lng_cloud_password_email())
, _recover(this, tr::lng_signin_recover(tr::now)) {
}

PasscodeBox::PasscodeBox(
	QWidget*,
	not_null<Main::Session*> session,
	const CloudFields &fields)
: _session(session)
, _api(&_session->mtp())
, _turningOff(fields.turningOff)
, _cloudPwd(true)
, _cloudFields(fields)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _oldPasscode(this, st::defaultInputField, tr::lng_cloud_password_enter_old())
, _newPasscode(this, st::defaultInputField, fields.curRequest ? tr::lng_cloud_password_enter_new() : tr::lng_cloud_password_enter_first())
, _reenterPasscode(this, st::defaultInputField, tr::lng_cloud_password_confirm_new())
, _passwordHint(this, st::defaultInputField, fields.curRequest ? tr::lng_cloud_password_change_hint() : tr::lng_cloud_password_hint())
, _recoverEmail(this, st::defaultInputField, tr::lng_cloud_password_email())
, _recover(this, tr::lng_signin_recover(tr::now)) {
	Expects(!_turningOff || _cloudFields.curRequest);

	if (!_cloudFields.hint.isEmpty()) {
		_hintText.setText(
			st::passcodeTextStyle,
			tr::lng_signin_hint(tr::now, lt_password_hint, _cloudFields.hint));
	}
}

rpl::producer<QByteArray> PasscodeBox::newPasswordSet() const {
	return _newPasswordSet.events();
}

rpl::producer<> PasscodeBox::passwordReloadNeeded() const {
	return _passwordReloadNeeded.events();
}

rpl::producer<> PasscodeBox::clearUnconfirmedPassword() const {
	return _clearUnconfirmedPassword.events();
}

bool PasscodeBox::currentlyHave() const {
	return _cloudPwd
		? (!!_cloudFields.curRequest)
		: _session->domain().local().hasLocalPasscode();
}

bool PasscodeBox::onlyCheckCurrent() const {
	return _turningOff || _cloudFields.customCheckCallback;
}

void PasscodeBox::prepare() {
	addButton(
		(_cloudFields.customSubmitButton
			? std::move(_cloudFields.customSubmitButton)
			: _turningOff
			? tr::lng_passcode_remove_button()
			: tr::lng_settings_save()),
		[=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	_about.setText(
		st::passcodeTextStyle,
		(_cloudFields.customDescription
			? *_cloudFields.customDescription
			: _cloudPwd
			? tr::lng_cloud_password_about(tr::now)
			: tr::lng_passcode_about(tr::now)));
	_aboutHeight = _about.countHeight(st::boxWidth - st::boxPadding.left() * 1.5);
	const auto onlyCheck = onlyCheckCurrent();
	if (onlyCheck) {
		_oldPasscode->show();
		setTitle(_cloudFields.customTitle
			? std::move(_cloudFields.customTitle)
			: _cloudPwd
			? tr::lng_cloud_password_remove()
			: tr::lng_passcode_remove());
		setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + ((_cloudFields.hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0) + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
	} else {
		if (currentlyHave()) {
			_oldPasscode->show();
			setTitle(_cloudPwd
				? tr::lng_cloud_password_change()
				: tr::lng_passcode_change());
			setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + ((_cloudFields.hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0) + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeSkip + (_cloudPwd ? _passwordHint->height() + st::passcodeLittleSkip : 0) + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
		} else {
			_oldPasscode->hide();
			setTitle(_cloudPwd
				? tr::lng_cloud_password_create()
				: tr::lng_passcode_create());
			setDimensions(st::boxWidth, st::passcodePadding.top() + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeSkip + (_cloudPwd ? _passwordHint->height() + st::passcodeLittleSkip : 0) + st::passcodeAboutSkip + _aboutHeight + (_cloudPwd ? (st::passcodeLittleSkip + _recoverEmail->height() + st::passcodeSkip) : st::passcodePadding.bottom()));
		}
	}

	connect(_oldPasscode, &Ui::MaskedInputField::changed, [=] { oldChanged(); });
	connect(_newPasscode, &Ui::MaskedInputField::changed, [=] { newChanged(); });
	connect(_reenterPasscode, &Ui::MaskedInputField::changed, [=] { newChanged(); });
	connect(_passwordHint, &Ui::InputField::changed, [=] { newChanged(); });
	connect(_recoverEmail, &Ui::InputField::changed, [=] { emailChanged(); });

	const auto fieldSubmit = [=] { submit(); };
	connect(_oldPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
	connect(_newPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
	connect(_reenterPasscode, &Ui::MaskedInputField::submitted, fieldSubmit);
	connect(_passwordHint, &Ui::InputField::submitted, fieldSubmit);
	connect(_recoverEmail, &Ui::InputField::submitted, fieldSubmit);

	_recover->addClickHandler([=] { recoverByEmail(); });

	const auto has = currentlyHave();
	_oldPasscode->setVisible(onlyCheck || has);
	_recover->setVisible((onlyCheck || has) && _cloudPwd && _cloudFields.hasRecovery);
	_newPasscode->setVisible(!onlyCheck);
	_reenterPasscode->setVisible(!onlyCheck);
	_passwordHint->setVisible(!onlyCheck && _cloudPwd);
	_recoverEmail->setVisible(!onlyCheck && _cloudPwd && !has);
}

void PasscodeBox::submit() {
	const auto has = currentlyHave();
	if (_oldPasscode->hasFocus()) {
		if (onlyCheckCurrent()) {
			save();
		} else {
			_newPasscode->setFocus();
		}
	} else if (_newPasscode->hasFocus()) {
		_reenterPasscode->setFocus();
	} else if (_reenterPasscode->hasFocus()) {
		if (has && _oldPasscode->text().isEmpty()) {
			_oldPasscode->setFocus();
			_oldPasscode->showError();
		} else if (_newPasscode->text().isEmpty()) {
			_newPasscode->setFocus();
			_newPasscode->showError();
		} else if (_reenterPasscode->text().isEmpty()) {
			_reenterPasscode->showError();
		} else if (!_passwordHint->isHidden()) {
			_passwordHint->setFocus();
		} else {
			save();
		}
	} else if (_passwordHint->hasFocus()) {
		if (_recoverEmail->isHidden()) {
			save();
		} else {
			_recoverEmail->setFocus();
		}
	} else if (_recoverEmail->hasFocus()) {
		save();
	}
}

void PasscodeBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	int32 abouty = (_passwordHint->isHidden() ? ((_reenterPasscode->isHidden() ? (_oldPasscode->y() + (_cloudFields.hasRecovery && !_hintText.isEmpty() ? st::passcodeTextLine : 0)) : _reenterPasscode->y()) + st::passcodeSkip) : _passwordHint->y()) + _oldPasscode->height() + st::passcodeLittleSkip + st::passcodeAboutSkip;
	p.setPen(st::boxTextFg);
	_about.drawLeft(p, st::boxPadding.left(), abouty, w, width());

	if (!_hintText.isEmpty() && _oldError.isEmpty()) {
		_hintText.drawLeftElided(p, st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height() + ((st::passcodeTextLine - st::normalFont->height) / 2), w, width(), 1, style::al_topleft);
	}

	if (!_oldError.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height(), w, st::passcodeTextLine), _oldError, style::al_left);
	}

	if (!_newError.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _reenterPasscode->y() + _reenterPasscode->height(), w, st::passcodeTextLine), _newError, style::al_left);
	}

	if (!_emailError.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _recoverEmail->y() + _recoverEmail->height(), w, st::passcodeTextLine), _emailError, style::al_left);
	}
}

void PasscodeBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	const auto has = currentlyHave();
	int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
	_oldPasscode->resize(w, _oldPasscode->height());
	_oldPasscode->moveToLeft(st::boxPadding.left(), st::passcodePadding.top());
	_newPasscode->resize(w, _newPasscode->height());
	_newPasscode->moveToLeft(st::boxPadding.left(), _oldPasscode->y() + ((_turningOff || has) ? (_oldPasscode->height() + st::passcodeTextLine + ((_cloudFields.hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0)) : 0));
	_reenterPasscode->resize(w, _reenterPasscode->height());
	_reenterPasscode->moveToLeft(st::boxPadding.left(), _newPasscode->y() + _newPasscode->height() + st::passcodeLittleSkip);
	_passwordHint->resize(w, _passwordHint->height());
	_passwordHint->moveToLeft(st::boxPadding.left(), _reenterPasscode->y() + _reenterPasscode->height() + st::passcodeSkip);
	_recoverEmail->resize(w, _passwordHint->height());
	_recoverEmail->moveToLeft(st::boxPadding.left(), _passwordHint->y() + _passwordHint->height() + st::passcodeLittleSkip + _aboutHeight + st::passcodeLittleSkip);

	if (!_recover->isHidden()) {
		_recover->moveToLeft(st::boxPadding.left(), _oldPasscode->y() + _oldPasscode->height() + (_hintText.isEmpty() ? ((st::passcodeTextLine - _recover->height()) / 2) : st::passcodeTextLine));
	}
}

void PasscodeBox::setInnerFocus() {
	if (_skipEmailWarning && !_recoverEmail->isHidden()) {
		_recoverEmail->setFocusFast();
	} else if (_oldPasscode->isHidden()) {
		_newPasscode->setFocusFast();
	} else {
		_oldPasscode->setFocusFast();
	}
}

void PasscodeBox::setPasswordDone(const QByteArray &newPasswordBytes) {
	if (_replacedBy) {
		_replacedBy->closeBox();
	}
	_setRequest = 0;
	_newPasswordSet.fire_copy(newPasswordBytes);
	const auto weak = Ui::MakeWeak(this);
	const auto text = _reenterPasscode->isHidden()
		? tr::lng_cloud_password_removed(tr::now)
		: _oldPasscode->isHidden()
		? tr::lng_cloud_password_was_set(tr::now)
		: tr::lng_cloud_password_updated(tr::now);
	getDelegate()->show(Box<InformBox>(text));
	if (weak) {
		closeBox();
	}
}

void PasscodeBox::closeReplacedBy() {
	if (isHidden()) {
		if (_replacedBy && !_replacedBy->isHidden()) {
			_replacedBy->closeBox();
		}
	}
}

void PasscodeBox::setPasswordFail(const MTP::Error &error) {
	setPasswordFail(error.type());
}

void PasscodeBox::setPasswordFail(const QString &type) {
	if (MTP::IsFloodError(type)) {
		closeReplacedBy();
		_setRequest = 0;

		_oldPasscode->selectAll();
		_oldPasscode->setFocus();
		_oldPasscode->showError();
		_oldError = tr::lng_flood_error(tr::now);
		if (_cloudFields.hasRecovery && _hintText.isEmpty()) {
			_recover->hide();
		}
		update();
		return;
	}

	closeReplacedBy();
	_setRequest = 0;
	if (type == qstr("PASSWORD_HASH_INVALID")
		|| type == qstr("SRP_PASSWORD_CHANGED")) {
		if (_oldPasscode->isHidden()) {
			_passwordReloadNeeded.fire({});
			closeBox();
		} else {
			badOldPasscode();
		}
	} else if (type == qstr("SRP_ID_INVALID")) {
		handleSrpIdInvalid();
	//} else if (type == qstr("NEW_PASSWORD_BAD")) {
	//} else if (type == qstr("NEW_SALT_INVALID")) {
	} else if (type == qstr("EMAIL_INVALID")) {
		_emailError = tr::lng_cloud_password_bad_email(tr::now);
		_recoverEmail->setFocus();
		_recoverEmail->showError();
		update();
	}
}

void PasscodeBox::setPasswordFail(
		const QByteArray &newPasswordBytes,
		const QString &email,
		const MTP::Error &error) {
	const auto prefix = qstr("EMAIL_UNCONFIRMED_");
	if (error.type().startsWith(prefix)) {
		const auto codeLength = error.type().midRef(prefix.size()).toInt();

		closeReplacedBy();
		_setRequest = 0;

		validateEmail(email, codeLength, newPasswordBytes);
	} else {
		setPasswordFail(error);
	}
}

void PasscodeBox::validateEmail(
		const QString &email,
		int codeLength,
		const QByteArray &newPasswordBytes) {
	const auto errors = std::make_shared<rpl::event_stream<QString>>();
	const auto resent = std::make_shared<rpl::event_stream<QString>>();
	const auto set = std::make_shared<bool>(false);
	const auto submit = crl::guard(this, [=](QString code) {
		if (_setRequest) {
			return;
		}
		_setRequest = _api.request(MTPaccount_ConfirmPasswordEmail(
			MTP_string(code)
		)).done([=](const MTPBool &result) {
			*set = true;
			setPasswordDone(newPasswordBytes);
		}).fail([=](const MTP::Error &error) {
			_setRequest = 0;
			if (MTP::IsFloodError(error)) {
				errors->fire(tr::lng_flood_error(tr::now));
			} else if (error.type() == qstr("CODE_INVALID")) {
				errors->fire(tr::lng_signin_wrong_code(tr::now));
			} else if (error.type() == qstr("EMAIL_HASH_EXPIRED")) {
				const auto weak = Ui::MakeWeak(this);
				_clearUnconfirmedPassword.fire({});
				if (weak) {
					auto box = Box<InformBox>(
						Lang::Hard::EmailConfirmationExpired());
					weak->getDelegate()->show(
						std::move(box),
						Ui::LayerOption::CloseOther);
				}
			} else {
				errors->fire(Lang::Hard::ServerError());
			}
		}).handleFloodErrors().send();
	});
	const auto resend = crl::guard(this, [=] {
		if (_setRequest) {
			return;
		}
		_setRequest = _api.request(MTPaccount_ResendPasswordEmail(
		)).done([=](const MTPBool &result) {
			_setRequest = 0;
			resent->fire(tr::lng_cloud_password_resent(tr::now));
		}).fail([=](const MTP::Error &error) {
			_setRequest = 0;
			errors->fire(Lang::Hard::ServerError());
		}).send();
	});
	const auto box = _replacedBy = getDelegate()->show(
		Passport::VerifyEmailBox(
			email,
			codeLength,
			submit,
			resend,
			errors->events(),
			resent->events()));

	box->setCloseByOutsideClick(false);
	box->setCloseByEscape(false);
	box->boxClosing(
	) | rpl::filter([=] {
		return !*set;
	}) | start_with_next([=, weak = Ui::MakeWeak(this)] {
		if (weak) {
			weak->_clearUnconfirmedPassword.fire({});
		}
		if (weak) {
			weak->closeBox();
		}
	}, box->lifetime());
}

void PasscodeBox::handleSrpIdInvalid() {
	const auto now = crl::now();
	if (_lastSrpIdInvalidTime > 0
		&& now - _lastSrpIdInvalidTime < Core::kHandleSrpIdInvalidTimeout) {
		_cloudFields.curRequest.id = 0;
		_oldError = Lang::Hard::ServerError();
		update();
	} else {
		_lastSrpIdInvalidTime = now;
		requestPasswordData();
	}
}

void PasscodeBox::save(bool force) {
	if (_setRequest) return;

	QString old = _oldPasscode->text(), pwd = _newPasscode->text(), conf = _reenterPasscode->text();
	const auto has = currentlyHave();
	if (!_cloudPwd && (_turningOff || has)) {
		if (!passcodeCanTry()) {
			_oldError = tr::lng_flood_error(tr::now);
			_oldPasscode->setFocus();
			_oldPasscode->showError();
			update();
			return;
		}

		if (_session->domain().local().checkPasscode(old.toUtf8())) {
			cSetPasscodeBadTries(0);
			if (_turningOff) pwd = conf = QString();
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(crl::now());
			badOldPasscode();
			return;
		}
	}
	const auto onlyCheck = onlyCheckCurrent();
	if (!onlyCheck && pwd.isEmpty()) {
		_newPasscode->setFocus();
		_newPasscode->showError();
		closeReplacedBy();
		return;
	}
	if (!onlyCheck && pwd != conf) {
		_reenterPasscode->selectAll();
		_reenterPasscode->setFocus();
		_reenterPasscode->showError();
		if (!conf.isEmpty()) {
			_newError = _cloudPwd
				? tr::lng_cloud_password_differ(tr::now)
				: tr::lng_passcode_differ(tr::now);
			update();
		}
		closeReplacedBy();
	} else if (!onlyCheck && has && old == pwd) {
		_newPasscode->setFocus();
		_newPasscode->showError();
		_newError = _cloudPwd
			? tr::lng_cloud_password_is_same(tr::now)
			: tr::lng_passcode_is_same(tr::now);
		update();
		closeReplacedBy();
	} else if (_cloudPwd) {
		QString hint = _passwordHint->getLastText(), email = _recoverEmail->getLastText().trimmed();
		if (!onlyCheck
			&& !_passwordHint->isHidden()
			&& !_newPasscode->isHidden()
			&& pwd == hint) {
			_newPasscode->setFocus();
			_newPasscode->showError();
			_newError = tr::lng_cloud_password_bad(tr::now);
			update();
			closeReplacedBy();
			return;
		}
		if (!onlyCheck && !_recoverEmail->isHidden() && email.isEmpty() && !force) {
			_skipEmailWarning = true;
			_replacedBy = getDelegate()->show(
				Box<ConfirmBox>(
					tr::lng_cloud_password_about_recover(tr::now),
					tr::lng_cloud_password_skip_email(tr::now),
					st::attentionBoxButton,
					crl::guard(this, [this] { save(true); })));
		} else if (onlyCheck) {
			submitOnlyCheckCloudPassword(old);
		} else if (_oldPasscode->isHidden()) {
			setNewCloudPassword(pwd);
		} else {
			changeCloudPassword(old, pwd);
		}
	} else {
		closeReplacedBy();
		const auto weak = Ui::MakeWeak(this);
		cSetPasscodeBadTries(0);
		_session->domain().local().setPasscode(pwd.toUtf8());
		Core::App().localPasscodeChanged();
		if (weak) {
			closeBox();
		}
	}
}

void PasscodeBox::submitOnlyCheckCloudPassword(const QString &oldPassword) {
	Expects(!_oldPasscode->isHidden());

	const auto send = [=] {
		sendOnlyCheckCloudPassword(oldPassword);
	};
	if (_cloudFields.turningOff && _cloudFields.notEmptyPassport) {
		Assert(!_cloudFields.customCheckCallback);

		const auto confirmed = [=](Fn<void()> &&close) {
			send();
			close();
		};
		getDelegate()->show(Box<ConfirmBox>(
			tr::lng_cloud_password_passport_losing(tr::now),
			tr::lng_continue(tr::now),
			confirmed));
	} else {
		send();
	}
}

void PasscodeBox::sendOnlyCheckCloudPassword(const QString &oldPassword) {
	checkPassword(oldPassword, [=](const Core::CloudPasswordResult &check) {
		if (const auto onstack = _cloudFields.customCheckCallback) {
			onstack(check);
		} else {
			Assert(_cloudFields.turningOff);
			sendClearCloudPassword(check);
		}
	});
}

void PasscodeBox::checkPassword(
		const QString &oldPassword,
		CheckPasswordCallback callback) {
	const auto passwordUtf = oldPassword.toUtf8();
	_checkPasswordHash = Core::ComputeCloudPasswordHash(
		_cloudFields.curRequest.algo,
		bytes::make_span(passwordUtf));
	checkPasswordHash(std::move(callback));
}

void PasscodeBox::checkPasswordHash(CheckPasswordCallback callback) {
	_checkPasswordCallback = std::move(callback);
	if (_cloudFields.curRequest.id) {
		passwordChecked();
	} else {
		requestPasswordData();
	}
}

void PasscodeBox::passwordChecked() {
	if (!_cloudFields.curRequest || !_cloudFields.curRequest.id || !_checkPasswordCallback) {
		return serverError();
	}
	const auto check = Core::ComputeCloudPasswordCheck(
		_cloudFields.curRequest,
		_checkPasswordHash);
	if (!check) {
		return serverError();
	}
	_cloudFields.curRequest.id = 0;
	_checkPasswordCallback(check);
}

void PasscodeBox::requestPasswordData() {
	if (!_checkPasswordCallback) {
		return serverError();
	}

	_api.request(base::take(_setRequest)).cancel();
	_setRequest = _api.request(
		MTPaccount_GetPassword()
	).done([=](const MTPaccount_Password &result) {
		_setRequest = 0;
		result.match([&](const MTPDaccount_password &data) {
			_cloudFields.curRequest = Core::ParseCloudPasswordCheckRequest(data);
			passwordChecked();
		});
	}).send();
}

void PasscodeBox::serverError() {
	getDelegate()->show(Box<InformBox>(Lang::Hard::ServerError()));
	closeBox();
}

bool PasscodeBox::handleCustomCheckError(const MTP::Error &error) {
	return handleCustomCheckError(error.type());
}

bool PasscodeBox::handleCustomCheckError(const QString &type) {
	if (MTP::IsFloodError(type)
		|| type == qstr("PASSWORD_HASH_INVALID")
		|| type == qstr("SRP_PASSWORD_CHANGED")
		|| type == qstr("SRP_ID_INVALID")) {
		setPasswordFail(type);
		return true;
	}
	return false;
}

void PasscodeBox::sendClearCloudPassword(
		const Core::CloudPasswordResult &check) {
	const auto hint = QString();
	const auto email = QString();
	const auto flags = MTPDaccount_passwordInputSettings::Flag::f_new_algo
		| MTPDaccount_passwordInputSettings::Flag::f_new_password_hash
		| MTPDaccount_passwordInputSettings::Flag::f_hint
		| MTPDaccount_passwordInputSettings::Flag::f_email;
	_setRequest = _api.request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(_cloudFields.newAlgo),
			MTP_bytes(), // new_password_hash
			MTP_string(hint),
			MTP_string(email),
			MTPSecureSecretSettings())
	)).done([=](const MTPBool &result) {
		setPasswordDone({});
	}).fail([=](const MTP::Error &error) mutable {
		setPasswordFail({}, QString(), error);
	}).handleFloodErrors().send();
}

void PasscodeBox::setNewCloudPassword(const QString &newPassword) {
	const auto newPasswordBytes = newPassword.toUtf8();
	const auto newPasswordHash = Core::ComputeCloudPasswordDigest(
		_cloudFields.newAlgo,
		bytes::make_span(newPasswordBytes));
	if (newPasswordHash.modpow.empty()) {
		return serverError();
	}
	const auto hint = _passwordHint->getLastText();
	const auto email = _recoverEmail->getLastText().trimmed();
	const auto flags = MTPDaccount_passwordInputSettings::Flag::f_new_algo
		| MTPDaccount_passwordInputSettings::Flag::f_new_password_hash
		| MTPDaccount_passwordInputSettings::Flag::f_hint
		| MTPDaccount_passwordInputSettings::Flag::f_email;
	_checkPasswordCallback = nullptr;
	_setRequest = _api.request(MTPaccount_UpdatePasswordSettings(
		MTP_inputCheckPasswordEmpty(),
		MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(_cloudFields.newAlgo),
			MTP_bytes(newPasswordHash.modpow),
			MTP_string(hint),
			MTP_string(email),
			MTPSecureSecretSettings())
	)).done([=](const MTPBool &result) {
		setPasswordDone(newPasswordBytes);
	}).fail([=](const MTP::Error &error) {
		setPasswordFail(newPasswordBytes, email, error);
	}).handleFloodErrors().send();
}

void PasscodeBox::changeCloudPassword(
		const QString &oldPassword,
		const QString &newPassword) {
	checkPassword(oldPassword, [=](const Core::CloudPasswordResult &check) {
		changeCloudPassword(oldPassword, check, newPassword);
	});
}

void PasscodeBox::changeCloudPassword(
		const QString &oldPassword,
		const Core::CloudPasswordResult &check,
		const QString &newPassword) {
	_setRequest = _api.request(MTPaccount_GetPasswordSettings(
		check.result
	)).done([=](const MTPaccount_PasswordSettings &result) {
		_setRequest = 0;

		Expects(result.type() == mtpc_account_passwordSettings);
		const auto &data = result.c_account_passwordSettings();

		const auto wrapped = data.vsecure_settings();
		if (!wrapped) {
			checkPasswordHash([=](const Core::CloudPasswordResult &check) {
				const auto empty = QByteArray();
				sendChangeCloudPassword(check, newPassword, empty);
			});
			return;
		}
		const auto &settings = wrapped->c_secureSecretSettings();
		const auto passwordUtf = oldPassword.toUtf8();
		const auto secret = Passport::DecryptSecureSecret(
			bytes::make_span(settings.vsecure_secret().v),
			Core::ComputeSecureSecretHash(
				Core::ParseSecureSecretAlgo(settings.vsecure_algo()),
				bytes::make_span(passwordUtf)));
		if (secret.empty()) {
			LOG(("API Error: Failed to decrypt secure secret."));
			suggestSecretReset(newPassword);
		} else if (Passport::CountSecureSecretId(secret)
				!= settings.vsecure_secret_id().v) {
			LOG(("API Error: Wrong secure secret id."));
			suggestSecretReset(newPassword);
		} else {
			const auto secureSecret = QByteArray(
				reinterpret_cast<const char*>(secret.data()),
				secret.size());
			checkPasswordHash([=](const Core::CloudPasswordResult &check) {
				sendChangeCloudPassword(check, newPassword, secureSecret);
			});
		}
	}).fail([=](const MTP::Error &error) {
		setPasswordFail(error);
	}).handleFloodErrors().send();
}

void PasscodeBox::suggestSecretReset(const QString &newPassword) {
	auto resetSecretAndSave = [=](Fn<void()> &&close) {
		checkPasswordHash([=, close = std::move(close)](
				const Core::CloudPasswordResult &check) {
			resetSecret(check, newPassword, std::move(close));
		});
	};
	getDelegate()->show(Box<ConfirmBox>(
		Lang::Hard::PassportCorruptedChange(),
		Lang::Hard::PassportCorruptedReset(),
		std::move(resetSecretAndSave)));
}

void PasscodeBox::resetSecret(
		const Core::CloudPasswordResult &check,
		const QString &newPassword,
		Fn<void()> callback) {
	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_setRequest = _api.request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_settings),
			MTPPasswordKdfAlgo(), // new_algo
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_secureSecretSettings(
				MTP_securePasswordKdfAlgoUnknown(), // secure_algo
				MTP_bytes(), // secure_secret
				MTP_long(0))) // secure_secret_id
	)).done([=](const MTPBool &result) {
		_setRequest = 0;
		callback();
		checkPasswordHash([=](const Core::CloudPasswordResult &check) {
			const auto empty = QByteArray();
			sendChangeCloudPassword(check, newPassword, empty);
		});
	}).fail([=](const MTP::Error &error) {
		_setRequest = 0;
		if (error.type() == qstr("SRP_ID_INVALID")) {
			handleSrpIdInvalid();
		}
	}).send();
}

void PasscodeBox::sendChangeCloudPassword(
		const Core::CloudPasswordResult &check,
		const QString &newPassword,
		const QByteArray &secureSecret) {
	const auto newPasswordBytes = newPassword.toUtf8();
	const auto newPasswordHash = Core::ComputeCloudPasswordDigest(
		_cloudFields.newAlgo,
		bytes::make_span(newPasswordBytes));
	if (newPasswordHash.modpow.empty()) {
		return serverError();
	}
	const auto hint = _passwordHint->getLastText();
	auto flags = MTPDaccount_passwordInputSettings::Flag::f_new_algo
		| MTPDaccount_passwordInputSettings::Flag::f_new_password_hash
		| MTPDaccount_passwordInputSettings::Flag::f_hint;
	auto newSecureSecret = bytes::vector();
	auto newSecureSecretId = 0ULL;
	if (!secureSecret.isEmpty()) {
		flags |= MTPDaccount_passwordInputSettings::Flag::f_new_secure_settings;
		newSecureSecretId = Passport::CountSecureSecretId(
			bytes::make_span(secureSecret));
		newSecureSecret = Passport::EncryptSecureSecret(
			bytes::make_span(secureSecret),
			Core::ComputeSecureSecretHash(
				_cloudFields.newSecureSecretAlgo,
				bytes::make_span(newPasswordBytes)));
	}
	_setRequest = _api.request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(_cloudFields.newAlgo),
			MTP_bytes(newPasswordHash.modpow),
			MTP_string(hint),
			MTPstring(), // email is not changing
			MTP_secureSecretSettings(
				Core::PrepareSecureSecretAlgo(_cloudFields.newSecureSecretAlgo),
				MTP_bytes(newSecureSecret),
				MTP_long(newSecureSecretId)))
	)).done([=](const MTPBool &result) {
		setPasswordDone(newPasswordBytes);
	}).fail([=](const MTP::Error &error) {
		setPasswordFail(newPasswordBytes, QString(), error);
	}).handleFloodErrors().send();
}

void PasscodeBox::badOldPasscode() {
	_oldPasscode->selectAll();
	_oldPasscode->setFocus();
	_oldPasscode->showError();
	_oldError = _cloudPwd
		? tr::lng_cloud_password_wrong(tr::now)
		: tr::lng_passcode_wrong(tr::now);
	if (_cloudFields.hasRecovery && _hintText.isEmpty()) {
		_recover->hide();
	}
	update();
}

void PasscodeBox::oldChanged() {
	if (!_oldError.isEmpty()) {
		_oldError = QString();
		if (_cloudFields.hasRecovery && _hintText.isEmpty()) {
			_recover->show();
		}
		update();
	}
}

void PasscodeBox::newChanged() {
	if (!_newError.isEmpty()) {
		_newError = QString();
		update();
	}
}

void PasscodeBox::emailChanged() {
	if (!_emailError.isEmpty()) {
		_emailError = QString();
		update();
	}
}

void PasscodeBox::recoverByEmail() {
	if (_pattern.isEmpty()) {
		_pattern = "-";
		_api.request(MTPauth_RequestPasswordRecovery(
		)).done([=](const MTPauth_PasswordRecovery &result) {
			recoverStarted(result);
		}).fail([=](const MTP::Error &error) {
			recoverStartFail(error);
		}).send();
	} else {
		recover();
	}
}

void PasscodeBox::recoverExpired() {
	_pattern = QString();
}

void PasscodeBox::recover() {
	if (_pattern == "-") return;

	const auto box = getDelegate()->show(Box<RecoverBox>(
		_session,
		_pattern,
		_cloudFields.notEmptyPassport));

	box->passwordCleared(
	) | rpl::map_to(
		QByteArray()
	) | rpl::start_to_stream(_newPasswordSet, lifetime());

	box->recoveryExpired(
	) | rpl::start_with_next([=] {
		recoverExpired();
	}, lifetime());

	_replacedBy = box;
}

void PasscodeBox::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_pattern = qs(result.c_auth_passwordRecovery().vemail_pattern());
	recover();
}

void PasscodeBox::recoverStartFail(const MTP::Error &error) {
	_pattern = QString();
	closeBox();
}

RecoverBox::RecoverBox(
	QWidget*,
	not_null<Main::Session*> session,
	const QString &pattern,
	bool notEmptyPassport)
: _api(&session->mtp())
, _pattern(st::normalFont->elided(tr::lng_signin_recover_hint(tr::now, lt_recover_email, pattern), st::boxWidth - st::boxPadding.left() * 1.5))
, _notEmptyPassport(notEmptyPassport)
, _recoverCode(this, st::defaultInputField, tr::lng_signin_code()) {
}

rpl::producer<> RecoverBox::passwordCleared() const {
	return _passwordCleared.events();
}

rpl::producer<> RecoverBox::recoveryExpired() const {
	return _recoveryExpired.events();
}

void RecoverBox::prepare() {
	setTitle(tr::lng_signin_recover_title());

	addButton(tr::lng_passcode_submit(), [=] { submit(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensions(st::boxWidth, st::passcodePadding.top() + st::passcodePadding.bottom() + st::passcodeTextLine + _recoverCode->height() + st::passcodeTextLine);

	connect(_recoverCode, &Ui::InputField::changed, [=] { codeChanged(); });
	connect(_recoverCode, &Ui::InputField::submitted, [=] { submit(); });
}

void RecoverBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::normalFont);
	p.setPen(st::boxTextFg);
	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	p.drawText(QRect(st::boxPadding.left(), _recoverCode->y() - st::passcodeTextLine - st::passcodePadding.top(), w, st::passcodePadding.top() + st::passcodeTextLine), _pattern, style::al_left);

	if (!_error.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(st::boxPadding.left(), _recoverCode->y() + _recoverCode->height(), w, st::passcodeTextLine), _error, style::al_left);
	}
}

void RecoverBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_recoverCode->resize(st::boxWidth - st::boxPadding.left() - st::boxPadding.right(), _recoverCode->height());
	_recoverCode->moveToLeft(st::boxPadding.left(), st::passcodePadding.top() + st::passcodePadding.bottom() + st::passcodeTextLine);
}

void RecoverBox::setInnerFocus() {
	_recoverCode->setFocusFast();
}

void RecoverBox::submit() {
	if (_submitRequest) return;

	QString code = _recoverCode->getLastText().trimmed();
	if (code.isEmpty()) {
		_recoverCode->setFocus();
		_recoverCode->showError();
		return;
	}

	const auto send = crl::guard(this, [=] {
		_submitRequest = _api.request(MTPauth_RecoverPassword(
			MTP_flags(0),
			MTP_string(code),
			MTPaccount_PasswordInputSettings()
		)).done([=](const MTPauth_Authorization &result) {
			codeSubmitDone(result);
		}).fail([=](const MTP::Error &error) {
			codeSubmitFail(error);
		}).handleFloodErrors().send();
	});
	if (_notEmptyPassport) {
		const auto confirmed = [=](Fn<void()> &&close) {
			send();
			close();
		};
		getDelegate()->show(Box<ConfirmBox>(
			tr::lng_cloud_password_passport_losing(tr::now),
			tr::lng_continue(tr::now),
			confirmed));
	} else {
		send();
	}
}

void RecoverBox::codeChanged() {
	_error = QString();
	update();
}

void RecoverBox::codeSubmitDone(const MTPauth_Authorization &result) {
	_submitRequest = 0;

	_passwordCleared.fire({});
	getDelegate()->show(
		Box<InformBox>(tr::lng_cloud_password_removed(tr::now)),
		Ui::LayerOption::CloseOther);
}

void RecoverBox::codeSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		_submitRequest = 0;
		_error = tr::lng_flood_error(tr::now);
		update();
		_recoverCode->showError();
		return;
	}
	_submitRequest = 0;

	const QString &err = error.type();
	if (err == qstr("PASSWORD_EMPTY")) {
		_passwordCleared.fire({});
		getDelegate()->show(
			Box<InformBox>(tr::lng_cloud_password_removed(tr::now)),
			Ui::LayerOption::CloseOther);
	} else if (err == qstr("PASSWORD_RECOVERY_NA")) {
		closeBox();
	} else if (err == qstr("PASSWORD_RECOVERY_EXPIRED")) {
		_recoveryExpired.fire({});
		closeBox();
	} else if (err == qstr("CODE_INVALID")) {
		_error = tr::lng_signin_wrong_code(tr::now);
		update();
		_recoverCode->selectAll();
		_recoverCode->setFocus();
		_recoverCode->showError();
	} else {
		if (Logs::DebugEnabled()) { // internal server error
			_error = err + ": " + error.description();
		} else {
			_error = Lang::Hard::ServerError();
		}
		update();
		_recoverCode->setFocus();
	}
}

RecoveryEmailValidation ConfirmRecoveryEmail(
		not_null<Main::Session*> session,
		const QString &pattern) {
	const auto errors = std::make_shared<rpl::event_stream<QString>>();
	const auto resent = std::make_shared<rpl::event_stream<QString>>();
	const auto requestId = std::make_shared<mtpRequestId>(0);
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto reloads = std::make_shared<rpl::event_stream<>>();
	const auto cancels = std::make_shared<rpl::event_stream<>>();

	const auto submit = [=](QString code) {
		if (*requestId) {
			return;
		}
		*requestId = session->api().request(MTPaccount_ConfirmPasswordEmail(
			MTP_string(code)
		)).done([=](const MTPBool &result) {
			*requestId = 0;
			reloads->fire({});
			if (*weak) {
				(*weak)->getDelegate()->show(
					Box<InformBox>(tr::lng_cloud_password_was_set(tr::now)),
					Ui::LayerOption::CloseOther);
			}
		}).fail([=](const MTP::Error &error) {
			*requestId = 0;
			if (MTP::IsFloodError(error)) {
				errors->fire(tr::lng_flood_error(tr::now));
			} else if (error.type() == qstr("CODE_INVALID")) {
				errors->fire(tr::lng_signin_wrong_code(tr::now));
			} else if (error.type() == qstr("EMAIL_HASH_EXPIRED")) {
				cancels->fire({});
				if (*weak) {
					auto box = Box<InformBox>(
						Lang::Hard::EmailConfirmationExpired());
					(*weak)->getDelegate()->show(
						std::move(box),
						Ui::LayerOption::CloseOther);
				}
			} else {
				errors->fire(Lang::Hard::ServerError());
			}
		}).handleFloodErrors().send();
	};
	const auto resend = [=] {
		if (*requestId) {
			return;
		}
		*requestId = session->api().request(MTPaccount_ResendPasswordEmail(
		)).done([=](const MTPBool &result) {
			*requestId = 0;
			resent->fire(tr::lng_cloud_password_resent(tr::now));
		}).fail([=](const MTP::Error &error) {
			*requestId = 0;
			errors->fire(Lang::Hard::ServerError());
		}).send();
	};

	auto box = Passport::VerifyEmailBox(
		pattern,
		0,
		submit,
		resend,
		errors->events(),
		resent->events());

	*weak = box.data();
	return { std::move(box), reloads->events(), cancels->events() };
}

[[nodiscard]] object_ptr<Ui::GenericBox> PrePasswordErrorBox(
		const MTP::Error &error,
		not_null<Main::Session*> session,
		TextWithEntities &&about) {
	const auto type = [&] {
		const auto &type = error.type();
		if (type == qstr("PASSWORD_MISSING")) {
			return PasswordErrorType::NoPassword;
		} else if (type.startsWith(qstr("PASSWORD_TOO_FRESH_"))
			|| type.startsWith(qstr("SESSION_TOO_FRESH_"))) {
			return PasswordErrorType::Later;
		}
		return PasswordErrorType::None;
	}();
	if (type == PasswordErrorType::None) {
		return nullptr;
	}

	return Box(
		TransferPasswordError,
		session,
		std::move(about),
		type);
}
