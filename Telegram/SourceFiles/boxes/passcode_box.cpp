/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/passcode_box.h"

#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "styles/style_boxes.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"

PasscodeBox::PasscodeBox(QWidget*, bool turningOff)
: _turningOff(turningOff)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _oldPasscode(this, st::defaultInputField, langFactory(lng_passcode_enter_old))
, _newPasscode(this, st::defaultInputField, langFactory(Global::LocalPasscode() ? lng_passcode_enter_new : lng_passcode_enter_first))
, _reenterPasscode(this, st::defaultInputField, langFactory(lng_passcode_confirm_new))
, _passwordHint(this, st::defaultInputField, langFactory(lng_cloud_password_hint))
, _recoverEmail(this, st::defaultInputField, langFactory(lng_cloud_password_email))
, _recover(this, lang(lng_signin_recover)) {
}

PasscodeBox::PasscodeBox(QWidget*, const QByteArray &newSalt, const QByteArray &curSalt, bool hasRecovery, const QString &hint, bool turningOff)
: _turningOff(turningOff)
, _cloudPwd(true)
, _newSalt(newSalt)
, _curSalt(curSalt)
, _hasRecovery(hasRecovery)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _oldPasscode(this, st::defaultInputField, langFactory(lng_cloud_password_enter_old))
, _newPasscode(this, st::defaultInputField, langFactory(curSalt.isEmpty() ? lng_cloud_password_enter_first : lng_cloud_password_enter_new))
, _reenterPasscode(this, st::defaultInputField, langFactory(lng_cloud_password_confirm_new))
, _passwordHint(this, st::defaultInputField, langFactory(curSalt.isEmpty() ? lng_cloud_password_hint : lng_cloud_password_change_hint))
, _recoverEmail(this, st::defaultInputField, langFactory(lng_cloud_password_email))
, _recover(this, lang(lng_signin_recover)) {
	if (!hint.isEmpty()) _hintText.setText(st::passcodeTextStyle, lng_signin_hint(lt_password_hint, hint));
}

void PasscodeBox::prepare() {
	addButton(langFactory(_turningOff ? lng_passcode_remove_button : lng_settings_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_about.setRichText(st::passcodeTextStyle, lang(_cloudPwd ? lng_cloud_password_about : lng_passcode_about));
	_aboutHeight = _about.countHeight(st::boxWidth - st::boxPadding.left() * 1.5);
	if (_turningOff) {
		_oldPasscode->show();
		setTitle(langFactory(_cloudPwd ? lng_cloud_password_remove : lng_passcode_remove));
		setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0) + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
	} else {
		auto has = _cloudPwd ? (!_curSalt.isEmpty()) : Global::LocalPasscode();
		if (has) {
			_oldPasscode->show();
			setTitle(langFactory(_cloudPwd ? lng_cloud_password_change : lng_passcode_change));
			setDimensions(st::boxWidth, st::passcodePadding.top() + _oldPasscode->height() + st::passcodeTextLine + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0) + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeSkip + (_cloudPwd ? _passwordHint->height() + st::passcodeLittleSkip : 0) + st::passcodeAboutSkip + _aboutHeight + st::passcodePadding.bottom());
		} else {
			_oldPasscode->hide();
			setTitle(langFactory(_cloudPwd ? lng_cloud_password_create : lng_passcode_create));
			setDimensions(st::boxWidth, st::passcodePadding.top() + _newPasscode->height() + st::passcodeLittleSkip + _reenterPasscode->height() + st::passcodeSkip + (_cloudPwd ? _passwordHint->height() + st::passcodeLittleSkip : 0) + st::passcodeAboutSkip + _aboutHeight + (_cloudPwd ? (st::passcodeLittleSkip + _recoverEmail->height() + st::passcodeSkip) : st::passcodePadding.bottom()));
		}
	}

	connect(_oldPasscode, SIGNAL(changed()), this, SLOT(onOldChanged()));
	connect(_newPasscode, SIGNAL(changed()), this, SLOT(onNewChanged()));
	connect(_reenterPasscode, SIGNAL(changed()), this, SLOT(onNewChanged()));
	connect(_passwordHint, SIGNAL(changed()), this, SLOT(onNewChanged()));
	connect(_recoverEmail, SIGNAL(changed()), this, SLOT(onEmailChanged()));

	connect(_oldPasscode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_newPasscode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_reenterPasscode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_passwordHint, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_recoverEmail, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	connect(_recover, SIGNAL(clicked()), this, SLOT(onRecoverByEmail()));

	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : Global::LocalPasscode();
	_oldPasscode->setVisible(_turningOff || has);
	_recover->setVisible((_turningOff || has) && _cloudPwd && _hasRecovery);
	_newPasscode->setVisible(!_turningOff);
	_reenterPasscode->setVisible(!_turningOff);
	_passwordHint->setVisible(!_turningOff && _cloudPwd);
	_recoverEmail->setVisible(!_turningOff && _cloudPwd && _curSalt.isEmpty());
}

void PasscodeBox::onSubmit() {
	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : Global::LocalPasscode();
	if (_oldPasscode->hasFocus()) {
		if (_turningOff) {
			onSave();
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
			onSave();
		}
	} else if (_passwordHint->hasFocus()) {
		if (_recoverEmail->isHidden()) {
			onSave();
		} else {
			_recoverEmail->setFocus();
		}
	} else if (_recoverEmail->hasFocus()) {
		onSave();
	}
}

void PasscodeBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	int32 abouty = (_passwordHint->isHidden() ? ((_reenterPasscode->isHidden() ? (_oldPasscode->y() + (_hasRecovery && !_hintText.isEmpty() ? st::passcodeTextLine : 0)) : _reenterPasscode->y()) + st::passcodeSkip) : _passwordHint->y()) + _oldPasscode->height() + st::passcodeLittleSkip + st::passcodeAboutSkip;
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

	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : Global::LocalPasscode();
	int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
	_oldPasscode->resize(w, _oldPasscode->height());
	_oldPasscode->moveToLeft(st::boxPadding.left(), st::passcodePadding.top());
	_newPasscode->resize(w, _newPasscode->height());
	_newPasscode->moveToLeft(st::boxPadding.left(), _oldPasscode->y() + ((_turningOff || has) ? (_oldPasscode->height() + st::passcodeTextLine + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeTextLine : 0)) : 0));
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

void PasscodeBox::setPasswordDone(const MTPBool &result) {
	_setRequest = 0;
	emit reloadPassword();
	auto text = lang(_reenterPasscode->isHidden() ? lng_cloud_password_removed : (_oldPasscode->isHidden() ? lng_cloud_password_was_set : lng_cloud_password_updated));
	Ui::show(Box<InformBox>(text));
}

void PasscodeBox::closeReplacedBy() {
	if (isHidden()) {
		if (_replacedBy && !_replacedBy->isHidden()) {
			_replacedBy->closeBox();
		}
	}
}

bool PasscodeBox::setPasswordFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		if (_oldPasscode->isHidden()) return false;

		closeReplacedBy();
		_setRequest = 0;

		_oldPasscode->selectAll();
		_oldPasscode->setFocus();
		_oldPasscode->showError();
		_oldError = lang(lng_flood_error);
		if (_hasRecovery && _hintText.isEmpty()) {
			_recover->hide();
		}
		update();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	closeReplacedBy();
	_setRequest = 0;
	QString err = error.type();
	if (err == qstr("PASSWORD_HASH_INVALID")) {
		if (_oldPasscode->isHidden()) {
			emit reloadPassword();
			closeBox();
		} else {
			onBadOldPasscode();
		}
	} else if (err == qstr("NEW_PASSWORD_BAD")) {
		_newPasscode->setFocus();
		_newPasscode->showError();
		_newError = lang(lng_cloud_password_bad);
		update();
	} else if (err == qstr("NEW_SALT_INVALID")) {
		emit reloadPassword();
		closeBox();
	} else if (err == qstr("EMAIL_INVALID")) {
		_emailError = lang(lng_cloud_password_bad_email);
		_recoverEmail->setFocus();
		_recoverEmail->showError();
		update();
	} else if (err == qstr("EMAIL_UNCONFIRMED")) {
		Ui::show(Box<InformBox>(lang(lng_cloud_password_almost)));
		emit reloadPassword();
	}
	return true;
}

void PasscodeBox::onSave(bool force) {
	if (_setRequest) return;

	QString old = _oldPasscode->text(), pwd = _newPasscode->text(), conf = _reenterPasscode->text();
	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : Global::LocalPasscode();
	if (!_cloudPwd && (_turningOff || has)) {
		if (!passcodeCanTry()) {
			_oldError = lang(lng_flood_error);
			_oldPasscode->setFocus();
			_oldPasscode->showError();
			update();
			return;
		}

		if (Local::checkPasscode(old.toUtf8())) {
			cSetPasscodeBadTries(0);
			if (_turningOff) pwd = conf = QString();
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			onBadOldPasscode();
			return;
		}
	}
	if (!_turningOff && pwd.isEmpty()) {
		_newPasscode->setFocus();
		_newPasscode->showError();
		closeReplacedBy();
		return;
	}
	if (pwd != conf) {
		_reenterPasscode->selectAll();
		_reenterPasscode->setFocus();
		_reenterPasscode->showError();
		if (!conf.isEmpty()) {
			_newError = lang(_cloudPwd ? lng_cloud_password_differ : lng_passcode_differ);
			update();
		}
		closeReplacedBy();
	} else if (!_turningOff && has && old == pwd) {
		_newPasscode->setFocus();
		_newPasscode->showError();
		_newError = lang(_cloudPwd ? lng_cloud_password_is_same : lng_passcode_is_same);
		update();
		closeReplacedBy();
	} else if (_cloudPwd) {
		QString hint = _passwordHint->getLastText(), email = _recoverEmail->getLastText().trimmed();
		if (_cloudPwd && pwd == hint && !_passwordHint->isHidden() && !_newPasscode->isHidden()) {
			_newPasscode->setFocus();
			_newPasscode->showError();
			_newError = lang(lng_cloud_password_bad);
			update();
			closeReplacedBy();
			return;
		}
		if (!_recoverEmail->isHidden() && email.isEmpty() && !force) {
			_skipEmailWarning = true;
			_replacedBy = Ui::show(Box<ConfirmBox>(lang(lng_cloud_password_about_recover), lang(lng_cloud_password_skip_email), st::attentionBoxButton, base::lambda_guarded(this, [this] {
				onSave(true);
			})), LayerOption::KeepOther);
		} else {
			QByteArray newPasswordData = pwd.isEmpty() ? QByteArray() : (_newSalt + pwd.toUtf8() + _newSalt);
			QByteArray newPasswordHash = pwd.isEmpty() ? QByteArray() : QByteArray(32, Qt::Uninitialized);
			if (pwd.isEmpty()) {
				hint = QString();
				email = QString();
			} else {
				hashSha256(newPasswordData.constData(), newPasswordData.size(), newPasswordHash.data());
			}
			QByteArray oldPasswordData = _oldPasscode->isHidden() ? QByteArray() : (_curSalt + old.toUtf8() + _curSalt);
			QByteArray oldPasswordHash = _oldPasscode->isHidden() ? QByteArray() : QByteArray(32, Qt::Uninitialized);
			if (!_oldPasscode->isHidden()) {
				hashSha256(oldPasswordData.constData(), oldPasswordData.size(), oldPasswordHash.data());
			}
			auto flags = MTPDaccount_passwordInputSettings::Flag::f_new_salt | MTPDaccount_passwordInputSettings::Flag::f_new_password_hash | MTPDaccount_passwordInputSettings::Flag::f_hint;
			if (_oldPasscode->isHidden() || _newPasscode->isHidden()) {
				flags |= MTPDaccount_passwordInputSettings::Flag::f_email;
			}
			MTPaccount_PasswordInputSettings settings(MTP_account_passwordInputSettings(MTP_flags(flags), MTP_bytes(_newSalt), MTP_bytes(newPasswordHash), MTP_string(hint), MTP_string(email)));
			_setRequest = MTP::send(MTPaccount_UpdatePasswordSettings(MTP_bytes(oldPasswordHash), settings), rpcDone(&PasscodeBox::setPasswordDone), rpcFail(&PasscodeBox::setPasswordFail));
		}
	} else {
		cSetPasscodeBadTries(0);
		Local::setPasscode(pwd.toUtf8());
		Auth().checkAutoLock();
		closeBox();
	}
}

void PasscodeBox::onBadOldPasscode() {
	_oldPasscode->selectAll();
	_oldPasscode->setFocus();
	_oldPasscode->showError();
	_oldError = lang(_cloudPwd ? lng_cloud_password_wrong : lng_passcode_wrong);
	if (_hasRecovery && _hintText.isEmpty()) {
		_recover->hide();
	}
	update();
}

void PasscodeBox::onOldChanged() {
	if (!_oldError.isEmpty()) {
		_oldError = QString();
		if (_hasRecovery && _hintText.isEmpty()) {
			_recover->show();
		}
		update();
	}
}

void PasscodeBox::onNewChanged() {
	if (!_newError.isEmpty()) {
		_newError = QString();
		update();
	}
}

void PasscodeBox::onEmailChanged() {
	if (!_emailError.isEmpty()) {
		_emailError = QString();
		update();
	}
}

void PasscodeBox::onRecoverByEmail() {
	if (_pattern.isEmpty()) {
		_pattern = "-";
		MTP::send(MTPauth_RequestPasswordRecovery(), rpcDone(&PasscodeBox::recoverStarted), rpcFail(&PasscodeBox::recoverStartFail));
	} else {
		recover();
	}
}

void PasscodeBox::onRecoverExpired() {
	_pattern = QString();
}

void PasscodeBox::recover() {
	if (_pattern == "-") return;

	_replacedBy = Ui::show(
		Box<RecoverBox>(_pattern),
		LayerOption::KeepOther);
	connect(_replacedBy, SIGNAL(reloadPassword()), this, SIGNAL(reloadPassword()));
	connect(_replacedBy, SIGNAL(recoveryExpired()), this, SLOT(onRecoverExpired()));
}

void PasscodeBox::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_pattern = qs(result.c_auth_passwordRecovery().vemail_pattern);
	recover();
}

bool PasscodeBox::recoverStartFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_pattern = QString();
	closeBox();
	return true;
}

RecoverBox::RecoverBox(QWidget*, const QString &pattern)
: _pattern(st::normalFont->elided(lng_signin_recover_hint(lt_recover_email, pattern), st::boxWidth - st::boxPadding.left() * 1.5))
, _recoverCode(this, st::defaultInputField, langFactory(lng_signin_code)) {
}

void RecoverBox::prepare() {
	setTitle(langFactory(lng_signin_recover_title));

	addButton(langFactory(lng_passcode_submit), [this] { onSubmit(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	setDimensions(st::boxWidth, st::passcodePadding.top() + st::passcodePadding.bottom() + st::passcodeTextLine + _recoverCode->height() + st::passcodeTextLine);

	connect(_recoverCode, SIGNAL(changed()), this, SLOT(onCodeChanged()));
	connect(_recoverCode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
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

void RecoverBox::onSubmit() {
	if (_submitRequest) return;

	QString code = _recoverCode->getLastText().trimmed();
	if (code.isEmpty()) {
		_recoverCode->setFocus();
		_recoverCode->showError();
		return;
	}

	_submitRequest = MTP::send(MTPauth_RecoverPassword(MTP_string(code)), rpcDone(&RecoverBox::codeSubmitDone, true), rpcFail(&RecoverBox::codeSubmitFail));
}

void RecoverBox::onCodeChanged() {
	_error = QString();
	update();
}

void RecoverBox::codeSubmitDone(bool recover, const MTPauth_Authorization &result) {
	_submitRequest = 0;

	emit reloadPassword();
	Ui::show(Box<InformBox>(lang(lng_cloud_password_removed)));
}

bool RecoverBox::codeSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		_submitRequest = 0;
		_error = lang(lng_flood_error);
		update();
		_recoverCode->showError();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	_submitRequest = 0;

	const QString &err = error.type();
	if (err == qstr("PASSWORD_EMPTY")) {
		emit reloadPassword();
		Ui::show(Box<InformBox>(lang(lng_cloud_password_removed)));
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_NA")) {
		closeBox();
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_EXPIRED")) {
		emit recoveryExpired();
		closeBox();
		return true;
	} else if (err == qstr("CODE_INVALID")) {
		_error = lang(lng_signin_wrong_code);
		update();
		_recoverCode->selectAll();
		_recoverCode->setFocus();
		_recoverCode->showError();
		return true;
	}
	if (cDebug()) { // internal server error
		_error =  err + ": " + error.description();
	} else {
		_error = Lang::Hard::ServerError();
	}
	update();
	_recoverCode->setFocus();
	return false;
}
