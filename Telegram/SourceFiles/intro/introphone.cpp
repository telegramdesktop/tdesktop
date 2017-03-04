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
#include "intro/introphone.h"

#include "lang.h"
#include "application.h"
#include "intro/introcode.h"
#include "styles/style_intro.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/effects/widget_fade_wrap.h"
#include "core/click_handler_types.h"
#include "boxes/confirmbox.h"
#include "messenger.h"

namespace Intro {

PhoneWidget::PhoneWidget(QWidget *parent, Widget::Data *data) : Step(parent, data)
, _country(this, st::introCountry)
, _code(this, st::introCountryCode)
, _phone(this, st::introPhone)
, _checkRequest(this) {
	connect(_phone, SIGNAL(voidBackspace(QKeyEvent*)), _code, SLOT(startErasing(QKeyEvent*)));
	connect(_country, SIGNAL(codeChanged(const QString &)), _code, SLOT(codeSelected(const QString &)));
	connect(_code, SIGNAL(codeChanged(const QString &)), _country, SLOT(onChooseCode(const QString &)));
	connect(_code, SIGNAL(codeChanged(const QString &)), _phone, SLOT(onChooseCode(const QString &)));
	connect(_country, SIGNAL(codeChanged(const QString &)), _phone, SLOT(onChooseCode(const QString &)));
	connect(_code, SIGNAL(addedToNumber(const QString &)), _phone, SLOT(addedToNumber(const QString &)));
	connect(_phone, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(_code, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(_checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	setTitleText(lang(lng_phone_title));
	setDescriptionText(lang(lng_phone_desc));
	subscribe(getData()->updated, [this] { countryChanged(); });
	setErrorCentered(true);

	if (!_country->onChooseCountry(getData()->country)) {
		_country->onChooseCountry(qsl("US"));
	}
	_changed = false;

	Messenger::Instance().destroyStaleAuthorizationKeys();
}

void PhoneWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	_country->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto phoneTop = _country->y() + _country->height() + st::introPhoneTop;
	_code->moveToLeft(contentLeft(), phoneTop);
	_phone->moveToLeft(contentLeft() + _country->width() - st::introPhone.width, phoneTop);
	updateSignupGeometry();
}

void PhoneWidget::updateSignupGeometry() {
	if (_signup) {
		_signup->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introDescriptionTop);
	}
}

void PhoneWidget::showPhoneError(const QString &text) {
	_phone->showError();
	showError(text);
}

void PhoneWidget::hidePhoneError() {
	hideError();
	if (_signup) {
		_signup->hideAnimated();
		showDescription();
	}
}

void PhoneWidget::showSignup() {
	showPhoneError(lang(lng_bad_phone_noreg));
	if (!_signup) {
		auto signupText = lng_phone_notreg(lt_link_start, textcmdStartLink(1), lt_link_end, textcmdStopLink(), lt_signup_start, textcmdStartLink(2), lt_signup_end, textcmdStopLink());
		auto inner = object_ptr<Ui::FlatLabel>(this, signupText, Ui::FlatLabel::InitType::Rich, st::introDescription);
		_signup.create(this, std::move(inner), st::introErrorDuration);
		_signup->entity()->setLink(1, MakeShared<UrlClickHandler>(qsl("https://telegram.org"), false));
		_signup->entity()->setLink(2, MakeShared<LambdaClickHandler>([this] {
			toSignUp();
		}));
		_signup->hideFast();
		updateSignupGeometry();
	}
	_signup->showAnimated();
	hideDescription();
}

void PhoneWidget::countryChanged() {
	if (!_changed) {
		selectCountry(getData()->country);
	}
}

void PhoneWidget::onInputChange() {
	_changed = true;
	hidePhoneError();
}

void PhoneWidget::submit() {
	if (_sentRequest || isHidden()) return;

	if (!App::isValidPhone(fullNumber())) {
		showPhoneError(lang(lng_bad_phone));
		_phone->setFocus();
		return;
	}

	hidePhoneError();

	_checkRequest->start(1000);

	_sentPhone = fullNumber();
	_sentRequest = MTP::send(MTPauth_CheckPhone(MTP_string(_sentPhone)), rpcDone(&PhoneWidget::phoneCheckDone), rpcFail(&PhoneWidget::phoneSubmitFail));
}

void PhoneWidget::stopCheck() {
	_checkRequest->stop();
}

void PhoneWidget::onCheckRequest() {
	auto status = MTP::state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(base::take(_sentRequest));
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void PhoneWidget::phoneCheckDone(const MTPauth_CheckedPhone &result) {
	stopCheck();

	auto &d = result.c_auth_checkedPhone();
	if (mtpIsTrue(d.vphone_registered)) {
		hidePhoneError();

		_checkRequest->start(1000);

		MTPauth_SendCode::Flags flags = 0;
		_sentRequest = MTP::send(MTPauth_SendCode(MTP_flags(flags), MTP_string(_sentPhone), MTPBool(), MTP_int(ApiId), MTP_string(ApiHash)), rpcDone(&PhoneWidget::phoneSubmitDone), rpcFail(&PhoneWidget::phoneSubmitFail));
	} else {
		showSignup();
		_sentRequest = 0;
	}
}

void PhoneWidget::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	_sentRequest = 0;

	if (result.type() != mtpc_auth_sentCode) {
		showPhoneError(lang(lng_server_error));
		return;
	}

	auto &d = result.c_auth_sentCode();
	fillSentCodeData(d.vtype);
	getData()->phone = _sentPhone;
	getData()->phoneHash = d.vphone_code_hash.c_string().v.c_str();
	getData()->phoneIsRegistered = d.is_phone_registered();
	if (d.has_next_type() && d.vnext_type.type() == mtpc_auth_codeTypeCall) {
		getData()->callStatus = Widget::Data::CallStatus::Waiting;
		getData()->callTimeout = d.has_timeout() ? d.vtimeout.v : 60;
	} else {
		getData()->callStatus = Widget::Data::CallStatus::Disabled;
		getData()->callTimeout = 0;
	}
	goNext(new Intro::CodeWidget(parentWidget(), getData()));
}

void PhoneWidget::toSignUp() {
	hideError(); // Hide error, but leave the signup label visible.

	_checkRequest->start(1000);

	MTPauth_SendCode::Flags flags = 0;
	_sentRequest = MTP::send(MTPauth_SendCode(MTP_flags(flags), MTP_string(_sentPhone), MTPBool(), MTP_int(ApiId), MTP_string(ApiHash)), rpcDone(&PhoneWidget::phoneSubmitDone), rpcFail(&PhoneWidget::phoneSubmitFail));
}

bool PhoneWidget::phoneSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showPhoneError(lang(lng_flood_error));
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	stopCheck();
	_sentRequest = 0;
	auto &err = error.type();
	if (err == qstr("PHONE_NUMBER_FLOOD")) {
		Ui::show(Box<InformBox>(lang(lng_error_phone_flood)));
		return true;
	} else if (err == qstr("PHONE_NUMBER_INVALID")) { // show error
		showPhoneError(lang(lng_bad_phone));
		return true;
	}
	if (cDebug()) { // internal server error
		showPhoneError(err + ": " + error.description());
	} else {
		showPhoneError(lang(lng_server_error));
	}
	return false;
}

QString PhoneWidget::fullNumber() const {
	return _code->getLastText() + _phone->getLastText();
}

void PhoneWidget::selectCountry(const QString &c) {
	_country->onChooseCountry(c);
}

void PhoneWidget::setInnerFocus() {
	_phone->setFocusFast();
}

void PhoneWidget::activate() {
	Step::activate();
	_country->show();
	_phone->show();
	_code->show();
	setInnerFocus();
}

void PhoneWidget::finished() {
	Step::finished();
	_checkRequest->stop();
	rpcClear();

	cancelled();
}

void PhoneWidget::cancelled() {
	MTP::cancel(base::take(_sentRequest));
}

} // namespace Intro
