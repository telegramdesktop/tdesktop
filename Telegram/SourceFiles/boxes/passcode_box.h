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
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class InputField;
class PasswordInput;
class LinkButton;
} // namespace Ui

class PasscodeBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	PasscodeBox(QWidget*, bool turningOff);
	PasscodeBox(QWidget*, const QByteArray &newSalt, const QByteArray &curSalt, bool hasRecovery, const QString &hint, bool turningOff = false);

private slots:
	void onSave(bool force = false);
	void onBadOldPasscode();
	void onOldChanged();
	void onNewChanged();
	void onEmailChanged();
	void onRecoverByEmail();
	void onRecoverExpired();
	void onSubmit();

signals:
	void reloadPassword();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void closeReplacedBy();

	void setPasswordDone(const MTPBool &result);
	bool setPasswordFail(const RPCError &error);

	void recoverStarted(const MTPauth_PasswordRecovery &result);
	bool recoverStartFail(const RPCError &error);

	void recover();
	QString _pattern;

	QPointer<BoxContent> _replacedBy;
	bool _turningOff = false;
	bool _cloudPwd = false;
	mtpRequestId _setRequest = 0;

	QByteArray _newSalt, _curSalt;
	bool _hasRecovery = false;
	bool _skipEmailWarning = false;

	int _aboutHeight = 0;

	Text _about, _hintText;

	object_ptr<Ui::PasswordInput> _oldPasscode;
	object_ptr<Ui::PasswordInput> _newPasscode;
	object_ptr<Ui::PasswordInput> _reenterPasscode;
	object_ptr<Ui::InputField> _passwordHint;
	object_ptr<Ui::InputField> _recoverEmail;
	object_ptr<Ui::LinkButton> _recover;

	QString _oldError, _newError, _emailError;

};

class RecoverBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	RecoverBox(QWidget*, const QString &pattern);

public slots:
	void onSubmit();
	void onCodeChanged();

signals:
	void reloadPassword();
	void recoveryExpired();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void codeSubmitDone(bool recover, const MTPauth_Authorization &result);
	bool codeSubmitFail(const RPCError &error);

	mtpRequestId _submitRequest = 0;

	QString _pattern;

	object_ptr<Ui::InputField> _recoverCode;

	QString _error;

};
