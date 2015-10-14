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

#include "passcodebox.h"
#include "confirmbox.h"
#include "window.h"

#include "localstorage.h"

PasscodeBox::PasscodeBox(bool turningOff) : AbstractBox(st::boxWidth)
, _replacedBy(0)
, _turningOff(turningOff)
, _cloudPwd(false)
, _setRequest(0)
, _hasRecovery(false)
, _skipEmailWarning(false)
, _aboutHeight(0)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _saveButton(this, lang(_turningOff ? lng_passcode_remove_button : lng_settings_save), st::defaultBoxButton)
, _cancelButton(this, lang(lng_cancel), st::cancelBoxButton)
, _oldPasscode(this, st::defaultInputField, lang(lng_passcode_enter_old))
, _newPasscode(this, st::defaultInputField, lang(cHasPasscode() ? lng_passcode_enter_new : lng_passcode_enter_first))
, _reenterPasscode(this, st::defaultInputField, lang(lng_passcode_confirm_new))
, _passwordHint(this, st::defaultInputField, lang(lng_cloud_password_hint))
, _recoverEmail(this, st::defaultInputField, lang(lng_cloud_password_email))
, _recover(this, lang(lng_signin_recover)) {
	init();
	prepare();
}

PasscodeBox::PasscodeBox(const QByteArray &newSalt, const QByteArray &curSalt, bool hasRecovery, const QString &hint, bool turningOff) : AbstractBox(st::boxWidth)
, _replacedBy(0)
, _turningOff(turningOff)
, _cloudPwd(true)
, _setRequest(0)
, _newSalt(newSalt)
, _curSalt(curSalt)
, _hasRecovery(hasRecovery)
, _skipEmailWarning(false)
, _aboutHeight(0)
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
, _saveButton(this, lang(_turningOff ? lng_passcode_remove_button : lng_settings_save), st::defaultBoxButton)
, _cancelButton(this, lang(lng_cancel), st::cancelBoxButton)
, _oldPasscode(this, st::defaultInputField, lang(lng_cloud_password_enter_old))
, _newPasscode(this, st::defaultInputField, lang(curSalt.isEmpty() ? lng_cloud_password_enter_first : lng_cloud_password_enter_new))
, _reenterPasscode(this, st::defaultInputField, lang(lng_cloud_password_confirm_new))
, _passwordHint(this, st::defaultInputField, lang(curSalt.isEmpty() ? lng_cloud_password_hint : lng_cloud_password_change_hint))
, _recoverEmail(this, st::defaultInputField, lang(lng_cloud_password_email))
, _recover(this, lang(lng_signin_recover)) {
	textstyleSet(&st::usernameTextStyle);
	if (!hint.isEmpty()) _hintText.setText(st::normalFont, lng_signin_hint(lt_password_hint, hint));
	textstyleRestore();
	init();
	prepare();
}

void PasscodeBox::init() {
	setBlueTitle(true);

	textstyleSet(&st::usernameTextStyle);
	_about.setRichText(st::normalFont, lang(_cloudPwd ? lng_cloud_password_about : lng_passcode_about));
	_aboutHeight = _about.countHeight(st::boxWidth - st::boxPadding.left() * 1.5);
	textstyleRestore();
	if (_turningOff) {
		_oldPasscode.show();
		_boxTitle = lang(_cloudPwd ? lng_cloud_password_remove : lng_passcode_remove);
		setMaxHeight(st::boxTitleHeight + st::passcodePadding.top() + _oldPasscode.height() + st::passcodeSkip + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeSkip : 0) + _aboutHeight + st::passcodePadding.bottom() + st::boxButtonPadding.top() + _saveButton.height() + st::boxButtonPadding.bottom());
	} else {
		bool has = _cloudPwd ? (!_curSalt.isEmpty()) : cHasPasscode();
		if (has) {
			_oldPasscode.show();
			_boxTitle = lang(_cloudPwd ? lng_cloud_password_change : lng_passcode_change);
			setMaxHeight(st::boxTitleHeight + st::passcodePadding.top() + _oldPasscode.height() + st::passcodeSkip + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeSkip : 0) + _newPasscode.height() + st::contactSkip + _reenterPasscode.height() + st::passcodeSkip + (_cloudPwd ? _passwordHint.height() + st::contactSkip : 0) + _aboutHeight + st::passcodePadding.bottom() + st::boxButtonPadding.top() + _saveButton.height() + st::boxButtonPadding.bottom());
		} else {
			_oldPasscode.hide();
			_boxTitle = lang(_cloudPwd ? lng_cloud_password_create : lng_passcode_create);
			setMaxHeight(st::boxTitleHeight + st::passcodePadding.top() + _newPasscode.height() + st::contactSkip + _reenterPasscode.height() + st::passcodeSkip + (_cloudPwd ? _passwordHint.height() + st::contactSkip : 0) + _aboutHeight + (_cloudPwd ? st::contactSkip + _recoverEmail.height() + st::passcodeSkip : st::passcodePadding.bottom()) + st::boxButtonPadding.top() + _saveButton.height() + st::boxButtonPadding.bottom());
		}
	}

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_oldPasscode, SIGNAL(changed()), this, SLOT(onOldChanged()));
	connect(&_newPasscode, SIGNAL(changed()), this, SLOT(onNewChanged()));
	connect(&_reenterPasscode, SIGNAL(changed()), this, SLOT(onNewChanged()));
	connect(&_passwordHint, SIGNAL(changed()), this, SLOT(onNewChanged()));
	connect(&_recoverEmail, SIGNAL(changed()), this, SLOT(onEmailChanged()));

	connect(&_oldPasscode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_newPasscode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_reenterPasscode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_passwordHint, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_recoverEmail, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	connect(&_recover, SIGNAL(clicked()), this, SLOT(onRecoverByEmail()));
}

void PasscodeBox::hideAll() {
	_oldPasscode.hide();
	_newPasscode.hide();
	_reenterPasscode.hide();
	_passwordHint.hide();
	_recoverEmail.hide();
	_recover.hide();
	_saveButton.hide();
	_cancelButton.hide();
	AbstractBox::hideAll();
}

void PasscodeBox::showAll() {
	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : cHasPasscode();
	if (_turningOff) {
		_oldPasscode.show();
		if (_cloudPwd && _hasRecovery) {
			_recover.show();
		} else {
			_recover.hide();
		}
		_passwordHint.hide();
		_newPasscode.hide();
		_reenterPasscode.hide();
	} else {
		if (has) {
			_oldPasscode.show();
			if (_cloudPwd && _hasRecovery) {
				_recover.show();
			} else {
				_recover.hide();
			}
		} else {
			_oldPasscode.hide();
			_recover.hide();
		}
		_newPasscode.show();
		_reenterPasscode.show();
		if (_cloudPwd) {
			_passwordHint.show();
		} else {
			_passwordHint.hide();
		}
		if (_cloudPwd && _curSalt.isEmpty()) {
			_recoverEmail.show();
		} else {
			_recoverEmail.hide();
		}
	}
	_saveButton.show();
	_cancelButton.show();
	AbstractBox::showAll();
}

void PasscodeBox::onSubmit() {
	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : cHasPasscode();
	if (_oldPasscode.hasFocus()) {
		if (_turningOff) {
			onSave();
		} else {
			_newPasscode.setFocus();
		}
	} else if (_newPasscode.hasFocus()) {
		_reenterPasscode.setFocus();
	} else if (_reenterPasscode.hasFocus()) {
		if (has && _oldPasscode.text().isEmpty()) {
			_oldPasscode.setFocus();
			_oldPasscode.showError();
		} else if (_newPasscode.text().isEmpty()) {
			_newPasscode.setFocus();
			_newPasscode.showError();
		} else if (_reenterPasscode.text().isEmpty()) {
			_reenterPasscode.showError();
		} else if (!_passwordHint.isHidden()) {
			_passwordHint.setFocus();
		} else {
			onSave();
		}
	} else if (_passwordHint.hasFocus()) {
		if (_recoverEmail.isHidden()) {
			onSave();
		} else {
			_recoverEmail.setFocus();
		}
	} else if (_recoverEmail.hasFocus()) {
		onSave();
	}
}

void PasscodeBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, _boxTitle);

	textstyleSet(&st::usernameTextStyle);

	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	int32 abouty = (_passwordHint.isHidden() ? (_reenterPasscode.isHidden() ? (_oldPasscode.y() + (_hasRecovery && !_hintText.isEmpty() ? st::passcodeSkip : 0)) : _reenterPasscode.y()) + st::passcodeSkip : _passwordHint.y() + st::contactSkip) + _oldPasscode.height();
	p.setPen(st::black);
	_about.drawLeft(p, st::boxPadding.left(), abouty, w, width());

	if (!_hintText.isEmpty() && _oldError.isEmpty()) {
		p.setPen(st::black->p);
		_hintText.drawLeftElided(p, st::boxPadding.left(), _oldPasscode.y() + _oldPasscode.height() + ((st::passcodeSkip - st::normalFont->height) / 2), w, width(), 1, style::al_topleft);
	}

	if (!_oldError.isEmpty()) {
		p.setPen(st::setErrColor->p);
		p.drawText(QRect(st::boxPadding.left(), _oldPasscode.y() + _oldPasscode.height(), w, st::passcodeSkip), _oldError, style::al_left);
	}

	if (!_newError.isEmpty()) {
		p.setPen(st::setErrColor->p);
		p.drawText(QRect(st::boxPadding.left(), _reenterPasscode.y() + _reenterPasscode.height(), w, st::passcodeSkip), _newError, style::al_left);
	}

	if (!_emailError.isEmpty()) {
		p.setPen(st::setErrColor->p);
		p.drawText(QRect(st::boxPadding.left(), _recoverEmail.y() + _recoverEmail.height(), w, st::passcodeSkip), _emailError, style::al_left);
	}

	textstyleRestore();
}

void PasscodeBox::resizeEvent(QResizeEvent *e) {
	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : cHasPasscode();
	int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
	_oldPasscode.resize(w, _oldPasscode.height());
	_oldPasscode.moveToLeft(st::boxPadding.left(), st::boxTitleHeight + st::passcodePadding.top());
	_newPasscode.resize(w, _newPasscode.height());
	_newPasscode.moveToLeft(st::boxPadding.left(), _oldPasscode.y() + ((_turningOff || has) ? (_oldPasscode.height() + st::passcodeSkip + ((_hasRecovery && !_hintText.isEmpty()) ? st::passcodeSkip : 0)) : 0));
	_reenterPasscode.resize(w, _reenterPasscode.height());
	_reenterPasscode.moveToLeft(st::boxPadding.left(), _newPasscode.y() + _newPasscode.height() + st::contactSkip);
	_passwordHint.resize(w, _passwordHint.height());
	_passwordHint.moveToLeft(st::boxPadding.left(), _reenterPasscode.y() + _reenterPasscode.height() + st::passcodeSkip);
	_recoverEmail.resize(w, _passwordHint.height());
	_recoverEmail.moveToLeft(st::boxPadding.left(), _passwordHint.y() + _passwordHint.height() + st::contactSkip + _aboutHeight + st::contactSkip);

	if (!_recover.isHidden()) {
		_recover.moveToLeft(st::boxPadding.left(), _oldPasscode.y() + _oldPasscode.height() + (_hintText.isEmpty() ? ((st::passcodeSkip - _recover.height()) / 2) : st::passcodeSkip));
	}

	_saveButton.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _saveButton.height());
	_cancelButton.moveToRight(st::boxButtonPadding.right() + _saveButton.width() + st::boxButtonPadding.left(), _saveButton.y());

	AbstractBox::resizeEvent(e);
}

void PasscodeBox::showDone() {
	if (_skipEmailWarning && !_recoverEmail.isHidden()) {
		_recoverEmail.setFocus();
	} else if (_oldPasscode.isHidden()) {
		_newPasscode.setFocus();
	} else {
		_oldPasscode.setFocus();
	}
	_skipEmailWarning = false;
}

void PasscodeBox::setPasswordDone(const MTPBool &result) {
	_setRequest = 0;
	emit reloadPassword();
	ConfirmBox *box = new InformBox(lang(_reenterPasscode.isHidden() ? lng_cloud_password_removed : (_oldPasscode.isHidden() ? lng_cloud_password_was_set : lng_cloud_password_updated)));
	App::wnd()->showLayer(box);
}

bool PasscodeBox::setPasswordFail(const RPCError &error) {
	if (isHidden() && _replacedBy && !_replacedBy->isHidden()) _replacedBy->onClose();
	_setRequest = 0;
	QString err = error.type();
	if (err == "PASSWORD_HASH_INVALID") {
		if (_oldPasscode.isHidden()) {
			emit reloadPassword();
			onClose();
		} else {
			onBadOldPasscode();
		}
	} else if (err == "NEW_PASSWORD_BAD") {
		_newPasscode.setFocus();
		_newPasscode.showError();
		_newError = lang(lng_cloud_password_bad);
		update();
	} else if (err == "NEW_SALT_INVALID") {
		emit reloadPassword();
		onClose();
	} else if (err == "EMAIL_INVALID") {
		_emailError = lang(lng_cloud_password_bad_email);
		_recoverEmail.setFocus();
		_recoverEmail.showError();
		update();
	} else if (err == "EMAIL_UNCONFIRMED") {
		App::wnd()->showLayer(new InformBox(lang(lng_cloud_password_almost)));
		emit reloadPassword();
	} else if (mtpIsFlood(error)) {
		if (_oldPasscode.isHidden()) return false;

		_oldPasscode.selectAll();
		_oldPasscode.setFocus();
		_oldPasscode.showError();
		_oldError = lang(lng_flood_error);
		if (_hasRecovery && _hintText.isEmpty()) {
			_recover.hide();
		}
		update();
	}
	return true;
}

void PasscodeBox::onSave(bool force) {
	if (_setRequest) return;

	QString old = _oldPasscode.text(), pwd = _newPasscode.text(), conf = _reenterPasscode.text();
	bool has = _cloudPwd ? (!_curSalt.isEmpty()) : cHasPasscode();
	if (!_cloudPwd && (_turningOff || has)) {
		if (!passcodeCanTry()) {
			_oldError = lang(lng_flood_error);
			_oldPasscode.setFocus();
			_oldPasscode.showError();
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
		_newPasscode.setFocus();
		_newPasscode.showError();
		if (isHidden() && _replacedBy && !_replacedBy->isHidden()) _replacedBy->onClose();
		return;
	}
	if (pwd != conf) {
		_reenterPasscode.setFocus();
		_reenterPasscode.showError();
		if (!conf.isEmpty()) {
			_newError = lang(_cloudPwd ? lng_cloud_password_differ : lng_passcode_differ);
			update();
		}
		if (isHidden() && _replacedBy && !_replacedBy->isHidden()) _replacedBy->onClose();
	} else if (!_turningOff && has && old == pwd) {
		_newPasscode.setFocus();
		_newPasscode.showError();
		_newError = lang(_cloudPwd ? lng_cloud_password_is_same : lng_passcode_is_same);
		update();
		if (isHidden() && _replacedBy && !_replacedBy->isHidden()) _replacedBy->onClose();
	} else if (_cloudPwd) {
		QString hint = _passwordHint.getLastText(), email = _recoverEmail.getLastText().trimmed();
		if (_cloudPwd && pwd == hint && !_passwordHint.isHidden() && !_newPasscode.isHidden()) {
			_newPasscode.setFocus();
			_newPasscode.showError();
			_newError = lang(lng_cloud_password_bad);
			update();
			if (isHidden() && _replacedBy && !_replacedBy->isHidden()) _replacedBy->onClose();
			return;
		}
		if (!_recoverEmail.isHidden() && email.isEmpty() && !force) {
			_skipEmailWarning = true;
			_replacedBy = new ConfirmBox(lang(lng_cloud_password_about_recover), lang(lng_cloud_password_skip_email), st::attentionBoxButton);
			connect(_replacedBy, SIGNAL(confirmed()), this, SLOT(onForceNoMail()));
			connect(_replacedBy, SIGNAL(destroyed(QObject*)), this, SLOT(onBoxDestroyed(QObject*)));
			App::wnd()->replaceLayer(_replacedBy);
		} else {
			QByteArray newPasswordData = pwd.isEmpty() ? QByteArray() : (_newSalt + pwd.toUtf8() + _newSalt);
			QByteArray newPasswordHash = pwd.isEmpty() ? QByteArray() : QByteArray(32, Qt::Uninitialized);
			if (pwd.isEmpty()) {
				hint = QString();
				email = QString();
			} else {
				hashSha256(newPasswordData.constData(), newPasswordData.size(), newPasswordHash.data());
			}
			QByteArray oldPasswordData = _oldPasscode.isHidden() ? QByteArray() : (_curSalt + old.toUtf8() + _curSalt);
			QByteArray oldPasswordHash = _oldPasscode.isHidden() ? QByteArray() : QByteArray(32, Qt::Uninitialized);
			if (!_oldPasscode.isHidden()) {
				hashSha256(oldPasswordData.constData(), oldPasswordData.size(), oldPasswordHash.data());
			}
			int32 flags = MTPDaccount_passwordInputSettings::flag_new_salt | MTPDaccount_passwordInputSettings::flag_new_password_hash | MTPDaccount_passwordInputSettings::flag_hint;
			if (_oldPasscode.isHidden() || _newPasscode.isHidden()) {
				flags |= MTPDaccount_passwordInputSettings::flag_email;
			}
			MTPaccount_PasswordInputSettings settings(MTP_account_passwordInputSettings(MTP_int(flags), MTP_string(_newSalt), MTP_string(newPasswordHash), MTP_string(hint), MTP_string(email)));
			_setRequest = MTP::send(MTPaccount_UpdatePasswordSettings(MTP_string(oldPasswordHash), settings), rpcDone(&PasscodeBox::setPasswordDone), rpcFail(&PasscodeBox::setPasswordFail));
		}
	} else {
		cSetPasscodeBadTries(0);
		Local::setPasscode(pwd.toUtf8());
		App::wnd()->checkAutoLock();
		App::wnd()->getTitle()->showUpdateBtn();
		emit closed();
	}
}

void PasscodeBox::onBadOldPasscode() {
	_oldPasscode.selectAll();
	_oldPasscode.setFocus();
	_oldPasscode.showError();
	_oldError = lang(_cloudPwd ? lng_cloud_password_wrong : lng_passcode_wrong);
	if (_hasRecovery && _hintText.isEmpty()) {
		_recover.hide();
	}
	update();
}

void PasscodeBox::onOldChanged() {
	if (!_oldError.isEmpty()) {
		_oldError = QString();
		if (_hasRecovery && _hintText.isEmpty()) {
			_recover.show();
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

void PasscodeBox::onForceNoMail() {
	onSave(true);
}

void PasscodeBox::onBoxDestroyed(QObject *obj) {
	if (obj == _replacedBy) {
		_replacedBy = 0;
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

	_replacedBy = new RecoverBox(_pattern);
	connect(_replacedBy, SIGNAL(reloadPassword()), this, SIGNAL(reloadPassword()));
	connect(_replacedBy, SIGNAL(recoveryExpired()), this, SLOT(onRecoverExpired()));
	connect(_replacedBy, SIGNAL(destroyed(QObject*)), this, SLOT(onBoxDestroyed(QObject*)));
	App::wnd()->replaceLayer(_replacedBy);
}

void PasscodeBox::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_pattern = qs(result.c_auth_passwordRecovery().vemail_pattern);
	recover();
}

bool PasscodeBox::recoverStartFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_pattern = QString();
	onClose();
	return true;
}

RecoverBox::RecoverBox(const QString &pattern) : AbstractBox(st::boxWidth)
, _submitRequest(0)
, _pattern(st::normalFont->elided(lng_signin_recover_hint(lt_recover_email, pattern), st::boxWidth - st::boxPadding.left() * 1.5))
, _saveButton(this, lang(lng_passcode_submit), st::defaultBoxButton)
, _cancelButton(this, lang(lng_cancel), st::cancelBoxButton)
, _recoverCode(this, st::defaultInputField, lang(lng_signin_code)) {
	setBlueTitle(true);

	setMaxHeight(st::boxTitleHeight + st::passcodePadding.top() + st::passcodeSkip + _recoverCode.height() + st::passcodeSkip + st::passcodePadding.bottom() + st::boxButtonPadding.top() + _saveButton.height() + st::boxButtonPadding.bottom());

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSubmit()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_recoverCode, SIGNAL(changed()), this, SLOT(onCodeChanged()));
	connect(&_recoverCode, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	prepare();
}

void RecoverBox::hideAll() {
	_recoverCode.hide();
	_saveButton.hide();
	_cancelButton.hide();
	AbstractBox::hideAll();
}

void RecoverBox::showAll() {
	_recoverCode.show();
	_saveButton.show();
	_cancelButton.show();
	AbstractBox::showAll();
}

void RecoverBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_signin_recover_title));

	p.setFont(st::normalFont);
	p.setPen(st::black);
	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	p.drawText(QRect(st::boxPadding.left(), _recoverCode.y() - st::passcodeSkip - st::passcodePadding.top(), w, st::passcodePadding.top() + st::passcodeSkip), _pattern, style::al_left);

	if (!_error.isEmpty()) {
		p.setPen(st::setErrColor->p);
		p.drawText(QRect(st::boxPadding.left(), _recoverCode.y() + _recoverCode.height(), w, st::passcodeSkip), _error, style::al_left);
	}
}

void RecoverBox::resizeEvent(QResizeEvent *e) {
	_recoverCode.resize(st::boxWidth - st::boxPadding.left() - st::boxPadding.right(), _recoverCode.height());
	_recoverCode.moveToLeft(st::boxPadding.left(), st::boxTitleHeight + st::passcodePadding.top() + st::passcodeSkip);

	_saveButton.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _saveButton.height());
	_cancelButton.moveToRight(st::boxButtonPadding.right() + _saveButton.width() + st::boxButtonPadding.left(), _saveButton.y());

	AbstractBox::resizeEvent(e);
}

void RecoverBox::showDone() {
	_recoverCode.setFocus();
}

void RecoverBox::onSubmit() {
	if (_submitRequest) return;

	QString code = _recoverCode.getLastText().trimmed();
	if (code.isEmpty()) {
		_recoverCode.setFocus();
		_recoverCode.showError();
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
	App::wnd()->showLayer(new InformBox(lang(lng_cloud_password_removed)));
}

bool RecoverBox::codeSubmitFail(const RPCError &error) {
	_submitRequest = 0;

	const QString &err = error.type();
	if (err == "PASSWORD_EMPTY") {
		emit reloadPassword();
		App::wnd()->showLayer(new InformBox(lang(lng_cloud_password_removed)));
		return true;
	} else if (err == "PASSWORD_RECOVERY_NA") {
		onClose();
		return true;
	} else if (err == "PASSWORD_RECOVERY_EXPIRED") {
		emit recoveryExpired();
		onClose();
		return true;
	} else if (err == "CODE_INVALID") {
		_error = lang(lng_signin_wrong_code);
		update();
		_recoverCode.showError();
		return true;
	} else if (mtpIsFlood(error)) {
		_error = lang(lng_flood_error);
		update();
		_recoverCode.showError();
		return true;
	}
	if (cDebug()) { // internal server error
		_error =  err + ": " + error.description();
	} else {
		_error = lang(lng_server_error);
	}
	update();
	_recoverCode.setFocus();
	return false;
}
