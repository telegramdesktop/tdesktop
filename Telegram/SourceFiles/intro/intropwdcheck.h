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
#pragma once

#include <QtWidgets/QWidget>
#include "gui/flatbutton.h"
#include "gui/flatinput.h"
#include "intro.h"

class IntroPwdCheck : public IntroStage, public RPCSender {
	Q_OBJECT

public:

	IntroPwdCheck(IntroWidget *parent);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void step_error(float64 ms, bool timer);

	void activate();
	void deactivate();
	void onNext();
	void onBack();

	void pwdSubmitDone(bool recover, const MTPauth_Authorization &result);
	bool pwdSubmitFail(const RPCError &error);
	bool codeSubmitFail(const RPCError &error);
	bool recoverStartFail(const RPCError &error);

	void recoverStarted(const MTPauth_PasswordRecovery &result);

public slots:

	void onSubmitPwd(bool force = false);
	void onToRecover();
	void onToPassword();
	void onInputChange();
	void onCheckRequest();
	void onToReset();
	void onReset();
	void onResetSure();

private:

	void showError(const QString &err);
	void stopCheck();

	void deleteDone(const MTPBool &result);
	bool deleteFail(const RPCError &error);

	QString error;
	anim::fvalue a_errorAlpha;
	Animation _a_error;

	FlatButton _next;

	QRect textRect;

	QByteArray _salt;
	bool _hasRecovery;
	QString _hint, _emailPattern;

	FlatInput _pwdField, _codeField;
	LinkButton _toRecover, _toPassword, _reset;
	mtpRequestId sentRequest;

	Text _hintText;

	QByteArray _pwdSalt;

	QTimer checkRequest;
};
