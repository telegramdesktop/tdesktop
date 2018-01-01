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

#include "intro/introwidget.h"

namespace Ui {
class InputField;
class PasswordInput;
class RoundButton;
class LinkButton;
} // namespace Ui

namespace Intro {

class PwdCheckWidget : public Widget::Step {
	Q_OBJECT

public:
	PwdCheckWidget(QWidget *parent, Widget::Data *data);

	void setInnerFocus() override;
	void activate() override;
	void cancelled() override;
	void submit() override;
	QString nextButtonText() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onToRecover();
	void onToPassword();
	void onInputChange();
	void onCheckRequest();

private:
	void showReset();
	void refreshLang();
	void updateControlsGeometry();

	void pwdSubmitDone(bool recover, const MTPauth_Authorization &result);
	bool pwdSubmitFail(const RPCError &error);
	bool codeSubmitFail(const RPCError &error);
	bool recoverStartFail(const RPCError &error);

	void recoverStarted(const MTPauth_PasswordRecovery &result);

	void updateDescriptionText();
	void stopCheck();

	QByteArray _salt;
	bool _hasRecovery;
	QString _hint, _emailPattern;

	object_ptr<Ui::PasswordInput> _pwdField;
	object_ptr<Ui::FlatLabel> _pwdHint;
	object_ptr<Ui::InputField> _codeField;
	object_ptr<Ui::LinkButton> _toRecover;
	object_ptr<Ui::LinkButton> _toPassword;
	mtpRequestId _sentRequest = 0;

	QByteArray _pwdSalt;

	object_ptr<QTimer> _checkRequest;

};

} // namespace Intro
