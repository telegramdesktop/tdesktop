/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/introphone.h"

#include "lang/lang_keys.h"
#include "application.h"
#include "intro/introcode.h"
#include "styles/style_intro.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "core/click_handler_types.h"
#include "boxes/confirm_box.h"
#include "base/qthelp_url.h"
#include "platform/platform_specific.h"
#include "messenger.h"

namespace Intro {
namespace {

void SendToBannedHelp(const QString &phone) {
	const auto version = QString::fromLatin1(AppVersionStr.c_str())
		+ (cAlphaVersion()
			? qsl(" alpha %1").arg(cAlphaVersion())
			: (AppBetaVersion ? " beta" : ""));

	const auto subject = qsl("Banned phone number: ") + phone;

	const auto body = qsl("\
I'm trying to use my mobile phone number: ") + phone + qsl("\n\
But Telegram says it's banned. Please help.\n\
\n\
App version: ") + version + qsl("\n\
OS version: ") + cPlatformString() + qsl("\n\
Locale: ") + Platform::SystemLanguage();

	const auto url = "mailto:?to="
		+ qthelp::url_encode("login@stel.com")
		+ "&subject="
		+ qthelp::url_encode(subject)
		+ "&body="
		+ qthelp::url_encode(body);

	UrlClickHandler::Open(url);
}

bool AllowPhoneAttempt(const QString &phone) {
	const auto digits = ranges::count_if(
		phone,
		[](QChar ch) { return ch.isNumber(); });
	return (digits > 1);
}

} // namespace

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

	setTitleText(langFactory(lng_phone_title));
	setDescriptionText(langFactory(lng_phone_desc));
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

void PhoneWidget::showPhoneError(Fn<QString()> textFactory) {
	_phone->showError();
	showError(std::move(textFactory));
}

void PhoneWidget::hidePhoneError() {
	hideError();
	if (_signup) {
		_signup->hide(anim::type::instant);
		showDescription();
	}
}

//void PhoneWidget::showSignup() {
//	showPhoneError(langFactory(lng_bad_phone_noreg));
//	if (!_signup) {
//		auto signupText = lng_phone_notreg(lt_link_start, textcmdStartLink(1), lt_link_end, textcmdStopLink(), lt_signup_start, textcmdStartLink(2), lt_signup_end, textcmdStopLink());
//		auto inner = object_ptr<Ui::FlatLabel>(this, signupText, Ui::FlatLabel::InitType::Rich, st::introDescription);
//		_signup.create(this, std::move(inner));
//		_signup->entity()->setLink(1, std::make_shared<UrlClickHandler>(qsl("https://telegram.org"), false));
//		_signup->entity()->setLink(2, std::make_shared<LambdaClickHandler>([this] {
//			toSignUp();
//		}));
//		_signup->hide(anim::type::instant);
//		updateSignupGeometry();
//	}
//	_signup->show(anim::type::normal);
//	hideDescription();
//}

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
		showPhoneError(langFactory(lng_bad_phone));
		_phone->setFocus();
		return;
	}

	hidePhoneError();

	_checkRequest->start(1000);

	_sentPhone = phone;
	Messenger::Instance().mtp()->setUserPhone(_sentPhone);
	//_sentRequest = MTP::send(MTPauth_CheckPhone(MTP_string(_sentPhone)), rpcDone(&PhoneWidget::phoneCheckDone), rpcFail(&PhoneWidget::phoneSubmitFail));
	_sentRequest = MTP::send(
		MTPauth_SendCode(
			MTP_flags(0),
			MTP_string(_sentPhone),
			MTPBool(),
			MTP_int(ApiId),
			MTP_string(ApiHash)),
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
//
//void PhoneWidget::phoneCheckDone(const MTPauth_CheckedPhone &result) {
//	stopCheck();
//
//	auto &d = result.c_auth_checkedPhone();
//	if (mtpIsTrue(d.vphone_registered)) {
//		hidePhoneError();
//
//		_checkRequest->start(1000);
//
//		_sentRequest = MTP::send(MTPauth_SendCode(MTP_flags(0), MTP_string(_sentPhone), MTPBool(), MTP_int(ApiId), MTP_string(ApiHash)), rpcDone(&PhoneWidget::phoneSubmitDone), rpcFail(&PhoneWidget::phoneSubmitFail));
//	} else {
//		showSignup();
//		_sentRequest = 0;
//	}
//}

void PhoneWidget::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	_sentRequest = 0;

	if (result.type() != mtpc_auth_sentCode) {
		showPhoneError(&Lang::Hard::ServerError);
		return;
	}

	const auto &d = result.c_auth_sentCode();
	fillSentCodeData(d);
	getData()->phone = _sentPhone;
	getData()->phoneHash = qba(d.vphone_code_hash);
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

//void PhoneWidget::toSignUp() {
//	hideError(); // Hide error, but leave the signup label visible.
//
//	_checkRequest->start(1000);
//
//	_sentRequest = MTP::send(MTPauth_SendCode(MTP_flags(0), MTP_string(_sentPhone), MTPBool(), MTP_int(ApiId), MTP_string(ApiHash)), rpcDone(&PhoneWidget::phoneSubmitDone), rpcFail(&PhoneWidget::phoneSubmitFail));
//}

bool PhoneWidget::phoneSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showPhoneError(langFactory(lng_flood_error));
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
		showPhoneError(langFactory(lng_bad_phone));
		return true;
	} else if (err == qstr("PHONE_NUMBER_BANNED")) {
		const auto phone = _sentPhone;
		Ui::show(Box<ConfirmBox>(
			lang(lng_signin_banned_text),
			lang(lng_box_ok),
			lang(lng_signin_banned_help),
			[] { Ui::hideLayer(); },
			[phone] { SendToBannedHelp(phone); Ui::hideLayer(); }));
		return true;
	}
	if (Logs::DebugEnabled()) { // internal server error
		auto text = err + ": " + error.description();
		showPhoneError([text] { return text; });
	} else {
		showPhoneError(&Lang::Hard::ServerError);
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
	rpcInvalidate();

	cancelled();
}

void PhoneWidget::cancelled() {
	MTP::cancel(base::take(_sentRequest));
}

} // namespace Intro
