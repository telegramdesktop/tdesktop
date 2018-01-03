/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
