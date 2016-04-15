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

#include <QtWidgets/QWidget>
#include "ui/flatbutton.h"
#include "ui/countryinput.h"
#include "intro/introwidget.h"

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

	QString error;
	anim::fvalue a_errorAlpha;
	Animation _a_error;

	bool changed;
	FlatButton next;

	QRect textRect;

	CountryInput country;
	PhonePartInput phone;
	CountryCodeInput code;

	FlatLabel _signup;
	QPixmap _signupCache;
	bool _showSignup;

	QString sentPhone;
	mtpRequestId sentRequest;

	QTimer checkRequest;
};
