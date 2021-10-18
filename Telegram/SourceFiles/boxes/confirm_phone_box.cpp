/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/confirm_phone_box.h"

#include "boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "core/click_handler_types.h" // UrlClickHandler
#include "base/qthelp_url.h" // qthelp::url_encode
#include "base/platform/base_platform_info.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "mtproto/facade.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

object_ptr<ConfirmPhoneBox> CurrentConfirmPhoneBox = { nullptr };

} // namespace

void ConfirmPhoneBox::Start(
		not_null<Main::Session*> session,
		const QString &phone,
		const QString &hash) {
	if (CurrentConfirmPhoneBox
		&& (CurrentConfirmPhoneBox->getPhone() != phone
			|| &CurrentConfirmPhoneBox->session() != session)) {
		CurrentConfirmPhoneBox.destroyDelayed();
	}
	if (!CurrentConfirmPhoneBox) {
		CurrentConfirmPhoneBox = Box<ConfirmPhoneBox>(session, phone, hash);
	}
	CurrentConfirmPhoneBox->checkPhoneAndHash();
}

ConfirmPhoneBox::ConfirmPhoneBox(
	QWidget*,
	not_null<Main::Session*> session,
	const QString &phone,
	const QString &hash)
: _session(session)
, _api(&session->mtp())
, _phone(phone)
, _hash(hash)
, _call([this] { sendCall(); }, [this] { update(); }) {
}

void ConfirmPhoneBox::sendCall() {
	_api.request(MTPauth_ResendCode(
		MTP_string(_phone),
		MTP_string(_phoneHash)
	)).done([=](const MTPauth_SentCode &result) {
		callDone(result);
	}).send();
}

void ConfirmPhoneBox::checkPhoneAndHash() {
	if (_sendCodeRequestId) {
		return;
	}
	_sendCodeRequestId = _api.request(MTPaccount_SendConfirmPhoneCode(
		MTP_string(_hash),
		MTP_codeSettings(MTP_flags(0))
	)).done([=](const MTPauth_SentCode &result) {
		sendCodeDone(result);
	}).fail([=](const MTP::Error &error) {
		sendCodeFail(error);
	}).handleFloodErrors().send();
}

void ConfirmPhoneBox::sendCodeDone(const MTPauth_SentCode &result) {
	result.match([&](const MTPDauth_sentCode &data) {
		_sendCodeRequestId = 0;
		_sentCodeLength = data.vtype().match([&](const MTPDauth_sentCodeTypeApp &data) {
			LOG(("Error: should not be in-app code!"));
			return 0;
		}, [&](const MTPDauth_sentCodeTypeSms &data) {
			return data.vlength().v;
		}, [&](const MTPDauth_sentCodeTypeCall &data) {
			return data.vlength().v;
		}, [&](const MTPDauth_sentCodeTypeFlashCall &data) {
			LOG(("Error: should not be flashcall!"));
			return 0;
		});
		_phoneHash = qs(data.vphone_code_hash());
		if (const auto nextType = data.vnext_type()) {
			if (nextType->type() == mtpc_auth_codeTypeCall) {
				_call.setStatus({ Ui::SentCodeCall::State::Waiting, data.vtimeout().value_or(60) });
			}
		}
		launch();
	});
}

void ConfirmPhoneBox::sendCodeFail(const MTP::Error &error) {
	auto errorText = Lang::Hard::ServerError();
	if (MTP::IsFloodError(error)) {
		errorText = tr::lng_flood_error(tr::now);
	} else if (error.code() == 400) {
		errorText = tr::lng_confirm_phone_link_invalid(tr::now);
	}
	_sendCodeRequestId = 0;
	Ui::show(Box<InformBox>(errorText));
	if (this == CurrentConfirmPhoneBox) {
		CurrentConfirmPhoneBox.destroyDelayed();
	} else {
		deleteLater();
	}
}

void ConfirmPhoneBox::launch() {
	if (!CurrentConfirmPhoneBox) return;
	Ui::show(std::move(CurrentConfirmPhoneBox));
}

void ConfirmPhoneBox::prepare() {
	_about.create(
		this,
		tr::lng_confirm_phone_about(
			lt_phone,
			rpl::single(Ui::Text::Bold(Ui::FormatPhone(_phone))),
			Ui::Text::WithEntities),
		st::confirmPhoneAboutLabel);

	_code.create(this, st::confirmPhoneCodeField, tr::lng_code_ph());
	_code->setAutoSubmit(_sentCodeLength, [=] { sendCode(); });
	_code->setChangedCallback([=] { showError(QString()); });

	setTitle(tr::lng_confirm_phone_title());

	addButton(tr::lng_confirm_phone_send(), [=] { sendCode(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensions(st::boxWidth, st::usernamePadding.top() + _code->height() + st::usernameSkip + _about->height() + st::usernameSkip);

	connect(_code, &Ui::InputField::submitted, [=] { sendCode(); });

	showChildren();
}

void ConfirmPhoneBox::callDone(const MTPauth_SentCode &result) {
	_call.callDone();
}

void ConfirmPhoneBox::sendCode() {
	if (_sendCodeRequestId) {
		return;
	}
	const auto code = _code->getDigitsOnly();
	if (code.isEmpty()) {
		_code->showError();
		return;
	}

	_code->setDisabled(true);
	setFocus();

	showError(QString());

	_sendCodeRequestId = _api.request(MTPaccount_ConfirmPhone(
		MTP_string(_phoneHash),
		MTP_string(code)
	)).done([=](const MTPBool &result) {
		confirmDone(result);
	}).fail([=](const MTP::Error &error) {
		confirmFail(error);
	}).handleFloodErrors().send();
}

void ConfirmPhoneBox::confirmDone(const MTPBool &result) {
	_sendCodeRequestId = 0;
	Ui::show(Box<InformBox>(tr::lng_confirm_phone_success(tr::now, lt_phone, Ui::FormatPhone(_phone))));
}

void ConfirmPhoneBox::confirmFail(const MTP::Error &error) {
	auto errorText = Lang::Hard::ServerError();
	if (MTP::IsFloodError(error)) {
		errorText = tr::lng_flood_error(tr::now);
	} else {
		auto &errorType = error.type();
		if (errorType == qstr("PHONE_CODE_EMPTY") || errorType == qstr("PHONE_CODE_INVALID")) {
			errorText = tr::lng_bad_code(tr::now);
		}
	}
	_sendCodeRequestId = 0;
	_code->setDisabled(false);
	_code->setFocus();
	showError(errorText);
}

void ConfirmPhoneBox::showError(const QString &error) {
	_error = error;
	if (!_error.isEmpty()) {
		_code->showError();
	}
	update();
}

void ConfirmPhoneBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::boxTextFont);
	auto callText = _call.getText();
	if (!callText.isEmpty()) {
		p.setPen(st::usernameDefaultFg);
		auto callTextRectLeft = st::usernamePadding.left();
		auto callTextRectTop = _about->y() + _about->height();
		auto callTextRectWidth = width() - 2 * st::usernamePadding.left();
		auto callTextRect = QRect(callTextRectLeft, callTextRectTop, callTextRectWidth, st::usernameSkip);
		p.drawText(callTextRect, callText, style::al_left);
	}
	auto errorText = _error;
	if (errorText.isEmpty()) {
		p.setPen(st::usernameDefaultFg);
		errorText = tr::lng_confirm_phone_enter_code(tr::now);
	} else {
		p.setPen(st::boxTextFgError);
	}
	auto errorTextRectLeft = st::usernamePadding.left();
	auto errorTextRectTop = _code->y() + _code->height();
	auto errorTextRectWidth = width() - 2 * st::usernamePadding.left();
	auto errorTextRect = QRect(errorTextRectLeft, errorTextRectTop, errorTextRectWidth, st::usernameSkip);
	p.drawText(errorTextRect, errorText, style::al_left);
}

void ConfirmPhoneBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_code->resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _code->height());
	_code->moveToLeft(st::usernamePadding.left(), st::usernamePadding.top());

	_about->moveToLeft(st::usernamePadding.left(), _code->y() + _code->height() + st::usernameSkip);
}

void ConfirmPhoneBox::setInnerFocus() {
	_code->setFocusFast();
}
