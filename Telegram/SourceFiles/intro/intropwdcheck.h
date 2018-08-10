/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/introwidget.h"
#include "mtproto/sender.h"

namespace Ui {
class InputField;
class PasswordInput;
class RoundButton;
class LinkButton;
} // namespace Ui

namespace Intro {

class PwdCheckWidget : public Widget::Step, private MTP::Sender {
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
	void pwdSubmitFail(const RPCError &error);
	void codeSubmitFail(const RPCError &error);
	void recoverStartFail(const RPCError &error);

	void recoverStarted(const MTPauth_PasswordRecovery &result);

	void updateDescriptionText();
	void stopCheck();
	void handleSrpIdInvalid();
	void requestPasswordData();
	void checkPasswordHash();
	void passwordChecked();
	void serverError();

	Core::CloudPasswordCheckRequest _request;
	TimeMs _lastSrpIdInvalidTime = 0;
	bytes::vector _passwordHash;
	bool _hasRecovery = false;
	bool _notEmptyPassport = false;
	QString _hint, _emailPattern;

	object_ptr<Ui::PasswordInput> _pwdField;
	object_ptr<Ui::FlatLabel> _pwdHint;
	object_ptr<Ui::InputField> _codeField;
	object_ptr<Ui::LinkButton> _toRecover;
	object_ptr<Ui::LinkButton> _toPassword;
	mtpRequestId _sentRequest = 0;

	object_ptr<QTimer> _checkRequest;

};

} // namespace Intro
