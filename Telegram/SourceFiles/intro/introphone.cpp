/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/introphone.h"

#include "lang/lang_keys.h"
#include "intro/introcode.h"
#include "styles/style_intro.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "main/main_account.h"
#include "boxes/confirm_phone_box.h"
#include "boxes/confirm_box.h"
#include "core/application.h"

namespace Intro {
namespace {

bool AllowPhoneAttempt(const QString &phone) {
	const auto digits = ranges::count_if(
		phone,
		[](QChar ch) { return ch.isNumber(); });
	return (digits > 1);
}

} // namespace

PhoneWidget::PhoneWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Widget::Data*> data)
: Step(parent, account, data)
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

	setTitleText(tr::lng_phone_title());
	setDescriptionText(tr::lng_phone_desc());
	subscribe(getData()->updated, [this] { countryChanged(); });
	setErrorCentered(true);

	if (!_country->onChooseCountry(getData()->country)) {
		_country->onChooseCountry(qsl("US"));
	}
	_changed = false;

	account->destroyStaleAuthorizationKeys();
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

void PhoneWidget::showPhoneError(rpl::producer<QString> text) {
	_phone->showError();
	showError(std::move(text));
}

void PhoneWidget::hidePhoneError() {
	hideError();
	if (_signup) {
		_signup->hide(anim::type::instant);
		showDescription();
	}
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

	const auto phone = fullNumber();
	if (!AllowPhoneAttempt(phone)) {
		showPhoneError(tr::lng_bad_phone());
		_phone->setFocus();
		return;
	}

	hidePhoneError();

	_checkRequest->start(1000);

	_sentPhone = phone;
	account().mtp()->setUserPhone(_sentPhone);
	_sentRequest = MTP::send(
		MTPauth_SendCode(
			MTP_string(_sentPhone),
			MTP_int(ApiId),
			MTP_string(ApiHash),
			MTP_codeSettings(MTP_flags(0))),
		rpcDone(&PhoneWidget::phoneSubmitDone),
		rpcFail(&PhoneWidget::phoneSubmitFail));
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

void PhoneWidget::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	_sentRequest = 0;

	if (result.type() != mtpc_auth_sentCode) {
		showPhoneError(rpl::single(Lang::Hard::ServerError()));
		return;
	}

	const auto &d = result.c_auth_sentCode();
	fillSentCodeData(d);
	getData()->phone = _sentPhone;
	getData()->phoneHash = qba(d.vphone_code_hash());
	const auto next = d.vnext_type();
	if (next && next->type() == mtpc_auth_codeTypeCall) {
		getData()->callStatus = Widget::Data::CallStatus::Waiting;
		getData()->callTimeout = d.vtimeout().value_or(60);
	} else {
		getData()->callStatus = Widget::Data::CallStatus::Disabled;
		getData()->callTimeout = 0;
	}
	goNext<CodeWidget>();
}

bool PhoneWidget::phoneSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showPhoneError(tr::lng_flood_error());
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	stopCheck();
	_sentRequest = 0;
	auto &err = error.type();
	if (err == qstr("PHONE_NUMBER_FLOOD")) {
		Ui::show(Box<InformBox>(tr::lng_error_phone_flood(tr::now)));
		return true;
	} else if (err == qstr("PHONE_NUMBER_INVALID")) { // show error
		showPhoneError(tr::lng_bad_phone());
		return true;
	} else if (err == qstr("PHONE_NUMBER_BANNED")) {
		ShowPhoneBannedError(_sentPhone);
		return true;
	}
	if (Logs::DebugEnabled()) { // internal server error
		showPhoneError(rpl::single(err + ": " + error.description()));
	} else {
		showPhoneError(rpl::single(Lang::Hard::ServerError()));
	}
	return false;
}

QString PhoneWidget::fullNumber() const {
	return _code->getLastText() + _phone->getLastText();
}

void PhoneWidget::selectCountry(const QString &country) {
	_country->onChooseCountry(country);
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
	rpcInvalidate();

	cancelled();
}

void PhoneWidget::cancelled() {
	MTP::cancel(base::take(_sentRequest));
}

} // namespace Intro
