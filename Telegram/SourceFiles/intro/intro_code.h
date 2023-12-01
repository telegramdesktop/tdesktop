/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"
#include "intro/intro_widget.h"
#include "ui/widgets/fields/masked_input_field.h"
#include "base/timer.h"

namespace Ui {
class RoundButton;
class LinkButton;
class FlatLabel;
class CodeInput;
} // namespace Ui

namespace Intro {
namespace details {

enum class CallStatus;

class CodeWidget final : public Step {
public:
	CodeWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	bool hasBack() const override {
		return true;
	}
	void setInnerFocus() override;
	void activate() override;
	void finished() override;
	void cancelled() override;
	void submit() override;
	rpl::producer<QString> nextButtonText() const override;
	rpl::producer<const style::RoundButton*> nextButtonStyle() const override;

	void updateDescText();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void noTelegramCode();
	void sendCall();
	void checkRequest();

	int errorTop() const override;

	void updateCallText();
	void refreshLang();
	void updateControlsGeometry();

	void codeSubmitDone(const MTPauth_Authorization &result);
	void codeSubmitFail(const MTP::Error &error);

	void showCodeError(rpl::producer<QString> text);
	void callDone(const MTPauth_SentCode &result);
	void gotPassword(const MTPaccount_Password &result);

	void noTelegramCodeDone(const MTPauth_SentCode &result);
	void noTelegramCodeFail(const MTP::Error &result);

	void submitCode(const QString &text);

	void stopCheck();

	object_ptr<Ui::LinkButton> _noTelegramCode;
	mtpRequestId _noTelegramCodeRequestId = 0;

	object_ptr<Ui::CodeInput> _code;
	QString _sentCode;
	mtpRequestId _sentRequest = 0;

	rpl::variable<bool> _isFragment = false;

	base::Timer _callTimer;
	CallStatus _callStatus = CallStatus();
	int _callTimeout;
	mtpRequestId _callRequestId = 0;
	object_ptr<Ui::FlatLabel> _callLabel;

	base::Timer _checkRequestTimer;

};

} // namespace details
} // namespace Intro
