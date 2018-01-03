/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
