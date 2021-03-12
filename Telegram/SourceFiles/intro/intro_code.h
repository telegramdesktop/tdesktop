/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"
#include "intro/intro_widget.h"
#include "ui/widgets/input_fields.h"
#include "base/timer.h"

namespace Ui {
class RoundButton;
class LinkButton;
class FlatLabel;
} // namespace Ui

namespace Intro {
namespace details {

enum class CallStatus;

class CodeInput final : public Ui::MaskedInputField {
public:
	CodeInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder);

	void setDigitsCountMax(int digitsCount);

protected:
	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;

private:
	int _digitsCountMax = 5;

};

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

	void updateDescText();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void noTelegramCode();
	void codeChanged();
	void sendCall();
	void checkRequest();

	int errorTop() const override;

	void updateCallText();
	void refreshLang();
	void updateControlsGeometry();

	void codeSubmitDone(const MTPauth_Authorization &result);
	void codeSubmitFail(const MTP::Error &error);

	void showCodeError(rpl::producer<QString> text);
	void callDone(const MTPauth_SentCode &v);
	void gotPassword(const MTPaccount_Password &result);

	void noTelegramCodeDone(const MTPauth_SentCode &result);
	void noTelegramCodeFail(const MTP::Error &result);

	void stopCheck();

	object_ptr<Ui::LinkButton> _noTelegramCode;
	mtpRequestId _noTelegramCodeRequestId = 0;

	object_ptr<CodeInput> _code;
	QString _sentCode;
	mtpRequestId _sentRequest = 0;

	base::Timer _callTimer;
	CallStatus _callStatus = CallStatus();
	int _callTimeout;
	mtpRequestId _callRequestId = 0;
	object_ptr<Ui::FlatLabel> _callLabel;

	base::Timer _checkRequestTimer;

};

} // namespace details
} // namespace Intro
