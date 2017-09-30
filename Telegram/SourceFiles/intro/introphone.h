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

#include "ui/countryinput.h"
#include "intro/introwidget.h"

namespace Ui {
class PhonePartInput;
class CountryCodeInput;
class RoundButton;
class FlatLabel;
} // namespace Ui

namespace Intro {

class PhoneWidget : public Widget::Step {
	Q_OBJECT

public:
	PhoneWidget(QWidget *parent, Widget::Data *data);

	void selectCountry(const QString &country);

	void setInnerFocus() override;
	void activate() override;
	void finished() override;
	void cancelled() override;
	void submit() override;

	bool hasBack() const override {
		return true;
	}

protected:
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onInputChange();
	void onCheckRequest();

private:
	void updateSignupGeometry();
	void countryChanged();

	void phoneCheckDone(const MTPauth_CheckedPhone &result);
	void phoneSubmitDone(const MTPauth_SentCode &result);
	bool phoneSubmitFail(const RPCError &error);

	void toSignUp();

	QString fullNumber() const;
	void stopCheck();

	void showPhoneError(base::lambda<QString()> textFactory);
	void hidePhoneError();
	void showSignup();

	bool _changed = false;

	object_ptr<CountryInput> _country;
	object_ptr<Ui::CountryCodeInput> _code;
	object_ptr<Ui::PhonePartInput> _phone;

	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _signup = { nullptr };

	QString _sentPhone;
	mtpRequestId _sentRequest = 0;

	object_ptr<QTimer> _checkRequest;

};

} // namespace Intro
