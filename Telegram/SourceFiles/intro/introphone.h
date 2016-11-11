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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/countryinput.h"
#include "ui/flatlabel.h"
#include "intro/introwidget.h"

namespace Ui {
class RoundButton;
} // namespace Ui

class IntroPhone final : public IntroStep {
	Q_OBJECT

public:
	IntroPhone(IntroWidget *parent);

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void step_error(float64 ms, bool timer);

	void selectCountry(const QString &country);

	void activate() override;
	void finished() override;
	void cancelled() override;
	void onSubmit() override;

	void phoneCheckDone(const MTPauth_CheckedPhone &result);
	void phoneSubmitDone(const MTPauth_SentCode &result);
	bool phoneSubmitFail(const RPCError &error);

	void toSignUp();

public slots:
	void countryChanged();
	void onInputChange();
	void onSubmitPhone();
	void onCheckRequest();

private:
	QString fullNumber() const;
	void disableAll();
	void enableAll(bool failed);
	void stopCheck();

	void showError(const QString &err, bool signUp = false);

	QString _error;
	anim::fvalue a_errorAlpha;
	Animation _a_error;

	bool _changed = false;
	ChildWidget<Ui::RoundButton> _next;

	QRect _textRect;

	ChildWidget<CountryInput> _country;
	ChildWidget<PhonePartInput> _phone;
	ChildWidget<CountryCodeInput> _code;

	ChildWidget<FlatLabel> _signup;
	QPixmap _signupCache;
	bool _showSignup = false;

	QString _sentPhone;
	mtpRequestId _sentRequest = 0;

	ChildObject<QTimer> _checkRequest;

};
