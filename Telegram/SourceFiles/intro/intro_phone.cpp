/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_phone.h"

#include "lang/lang_keys.h"
#include "intro/intro_code.h"
#include "intro/intro_qr.h"
#include "styles/style_intro.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/special_fields.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "data/data_user.h"
#include "boxes/confirm_phone_box.h"
#include "boxes/confirm_box.h"
#include "core/application.h"

namespace Intro {
namespace details {
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
	not_null<Data*> data)
: Step(parent, account, data)
, _country(this, st::introCountry)
, _code(this, st::introCountryCode)
, _phone(this, st::introPhone)
, _checkRequestTimer([=] { checkRequest(); }) {
	connect(_phone, SIGNAL(voidBackspace(QKeyEvent*)), _code, SLOT(startErasing(QKeyEvent*)));
	connect(_country, SIGNAL(codeChanged(const QString &)), _code, SLOT(codeSelected(const QString &)));
	connect(_code, SIGNAL(codeChanged(const QString &)), _country, SLOT(onChooseCode(const QString &)));
	connect(_code, SIGNAL(codeChanged(const QString &)), _phone, SLOT(onChooseCode(const QString &)));
	connect(_country, SIGNAL(codeChanged(const QString &)), _phone, SLOT(onChooseCode(const QString &)));
	connect(_code, SIGNAL(addedToNumber(const QString &)), _phone, SLOT(addedToNumber(const QString &)));
	connect(_phone, &Ui::PhonePartInput::changed, [=] { phoneChanged(); });
	connect(_code, &Ui::CountryCodeInput::changed, [=] { phoneChanged(); });

	setTitleText(tr::lng_phone_title());
	setDescriptionText(tr::lng_phone_desc());
	subscribe(getData()->updated, [=] { countryChanged(); });
	setErrorCentered(true);
	setupQrLogin();

	if (!_country->onChooseCountry(getData()->country)) {
		_country->onChooseCountry(qsl("US"));
	}
	_changed = false;
}

void PhoneWidget::setupQrLogin() {
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		account().appConfig().refreshed()
	) | rpl::map([=] {
		const auto result = account().appConfig().get<QString>(
			"qr_login_code",
			"[not-set]");
		DEBUG_LOG(("PhoneWidget.qr_login_code: %1").arg(result));
		return result;
	}) | rpl::filter([](const QString &value) {
		return (value != "disabled");
	}) | rpl::take(1) | rpl::start_with_next([=] {
		const auto qrLogin = Ui::CreateChild<Ui::LinkButton>(
			this,
			tr::lng_phone_to_qr(tr::now));
		qrLogin->show();

		DEBUG_LOG(("PhoneWidget.qrLogin link created and shown."));

		rpl::combine(
			sizeValue(),
			qrLogin->widthValue()
		) | rpl::start_with_next([=](QSize size, int qrLoginWidth) {
			qrLogin->moveToLeft(
				(size.width() - qrLoginWidth) / 2,
				contentTop() + st::introQrLoginLinkTop);
		}, qrLogin->lifetime());

		qrLogin->setClickedCallback([=] {
			goReplace<QrWidget>(Animate::Forward);
		});
	}, lifetime());
}

void PhoneWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	_country->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto phoneTop = _country->y() + _country->height() + st::introPhoneTop;
	_code->moveToLeft(contentLeft(), phoneTop);
	_phone->moveToLeft(contentLeft() + _country->width() - st::introPhone.width, phoneTop);
}

void PhoneWidget::showPhoneError(rpl::producer<QString> text) {
	_phone->showError();
	showError(std::move(text));
}

void PhoneWidget::hidePhoneError() {
	hideError();
}

void PhoneWidget::countryChanged() {
	if (!_changed) {
		selectCountry(getData()->country);
	}
}

void PhoneWidget::phoneChanged() {
	_changed = true;
	hidePhoneError();
}

void PhoneWidget::submit() {
	if (_sentRequest || isHidden()) {
		return;
	}

	const auto phone = fullNumber();
	if (!AllowPhoneAttempt(phone)) {
		showPhoneError(tr::lng_bad_phone());
		_phone->setFocus();
		return;
	}

	cancelNearestDcRequest();

	// Check if such account is authorized already.
	const auto digitsOnly = [](QString value) {
		return value.replace(QRegularExpression("[^0-9]"), QString());
	};
	const auto phoneDigits = digitsOnly(phone);
	for (const auto &[index, existing] : Core::App().domain().accounts()) {
		const auto raw = existing.get();
		if (const auto session = raw->maybeSession()) {
			if (raw->mtp().environment() == account().mtp().environment()
				&& digitsOnly(session->user()->phone()) == phoneDigits) {
				crl::on_main(raw, [=] {
					Core::App().domain().activate(raw);
				});
				return;
			}
		}
	}

	hidePhoneError();

	_checkRequestTimer.callEach(1000);

	_sentPhone = phone;
	api().instance().setUserPhone(_sentPhone);
	_sentRequest = api().request(MTPauth_SendCode(
		MTP_string(_sentPhone),
		MTP_int(ApiId),
		MTP_string(ApiHash),
		MTP_codeSettings(MTP_flags(0))
	)).done([=](const MTPauth_SentCode &result) {
		phoneSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		phoneSubmitFail(error);
	}).handleFloodErrors().send();
}

void PhoneWidget::stopCheck() {
	_checkRequestTimer.cancel();
}

void PhoneWidget::checkRequest() {
	auto status = api().instance().state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			api().request(base::take(_sentRequest)).cancel();
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
		getData()->callStatus = CallStatus::Waiting;
		getData()->callTimeout = d.vtimeout().value_or(60);
	} else {
		getData()->callStatus = CallStatus::Disabled;
		getData()->callTimeout = 0;
	}
	goNext<CodeWidget>();
}

void PhoneWidget::phoneSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showPhoneError(tr::lng_flood_error());
		return;
	}

	stopCheck();
	_sentRequest = 0;
	auto &err = error.type();
	if (err == qstr("PHONE_NUMBER_FLOOD")) {
		Ui::show(Box<InformBox>(tr::lng_error_phone_flood(tr::now)));
	} else if (err == qstr("PHONE_NUMBER_INVALID")) { // show error
		showPhoneError(tr::lng_bad_phone());
	} else if (err == qstr("PHONE_NUMBER_BANNED")) {
		ShowPhoneBannedError(_sentPhone);
	} else if (Logs::DebugEnabled()) { // internal server error
		showPhoneError(rpl::single(err + ": " + error.description()));
	} else {
		showPhoneError(rpl::single(Lang::Hard::ServerError()));
	}
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
	showChildren();
	setInnerFocus();
}

void PhoneWidget::finished() {
	Step::finished();
	_checkRequestTimer.cancel();
	apiClear();

	cancelled();
}

void PhoneWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

} // namespace details
} // namespace Intro
