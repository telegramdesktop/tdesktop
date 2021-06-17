/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"
#include "core/core_cloud_password.h"
#include "mtproto/sender.h"
#include "base/timer.h"

namespace Ui {
class InputField;
class PasswordInput;
class RoundButton;
class LinkButton;
} // namespace Ui

namespace Intro {
namespace details {

class PasswordCheckWidget final : public Step {
public:
	PasswordCheckWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void setInnerFocus() override;
	void activate() override;
	void cancelled() override;
	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void toRecover();
	void toPassword();

	int errorTop() const override;

	void showReset();
	void refreshLang();
	void updateControlsGeometry();

	void pwdSubmitDone(bool recover, const MTPauth_Authorization &result);
	void pwdSubmitFail(const MTP::Error &error);
	void codeSubmitFail(const MTP::Error &error);
	void recoverStartFail(const MTP::Error &error);

	void recoverStarted(const MTPauth_PasswordRecovery &result);

	void updateDescriptionText();
	void handleSrpIdInvalid();
	void requestPasswordData();
	void checkPasswordHash();
	void passwordChecked();
	void serverError();

	Core::CloudPasswordCheckRequest _request;
	crl::time _lastSrpIdInvalidTime = 0;
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

};

} // namespace details
} // namespace Intro
