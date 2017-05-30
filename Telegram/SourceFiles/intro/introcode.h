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
#include "ui/widgets/input_fields.h"

namespace Ui {
class RoundButton;
class LinkButton;
class FlatLabel;
} // namespace Ui

namespace Intro {

class CodeInput final : public Ui::MaskedInputField {
	Q_OBJECT

public:
	CodeInput(QWidget *parent, const style::InputField &st, const QString &ph);

	void setDigitsCountMax(int digitsCount);

signals:
	void codeEntered();

protected:
	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;

private:
	int _digitsCountMax = 5;

};

class CodeWidget : public Widget::Step {
	Q_OBJECT

public:
	CodeWidget(QWidget *parent, Widget::Data *data);

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

private slots:
	void onNoTelegramCode();
	void onInputChange();
	void onSendCall();
	void onCheckRequest();

private:
	void updateCallText();
	void refreshLang();
	void updateControlsGeometry();

	void codeSubmitDone(const MTPauth_Authorization &result);
	bool codeSubmitFail(const RPCError &error);

	void showCodeError(base::lambda<QString()> textFactory);
	void callDone(const MTPauth_SentCode &v);
	void gotPassword(const MTPaccount_Password &result);

	void noTelegramCodeDone(const MTPauth_SentCode &result);
	bool noTelegramCodeFail(const RPCError &result);

	void stopCheck();

	object_ptr<Ui::LinkButton> _noTelegramCode;
	mtpRequestId _noTelegramCodeRequestId = 0;

	object_ptr<CodeInput> _code;
	QString _sentCode;
	mtpRequestId _sentRequest = 0;

	object_ptr<QTimer> _callTimer;
	Widget::Data::CallStatus _callStatus;
	int _callTimeout;
	mtpRequestId _callRequestId = 0;
	object_ptr<Ui::FlatLabel> _callLabel;

	object_ptr<QTimer> _checkRequest;

};

} // namespace Intro
