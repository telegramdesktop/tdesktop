/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/change_phone_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/sent_code_field.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/special_fields.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/phone_banned_box.h"
#include "countries/countries_instance.h" // Countries::ExtractPhoneCode.
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "mtproto/sender.h"
#include "apiwrap.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

void CreateErrorLabel(
		QWidget *parent,
		object_ptr<Ui::FadeWrap<Ui::FlatLabel>> &label,
		const QString &text,
		int x,
		int y) {
	if (label) {
		label->hide(anim::type::normal);

		auto saved = label.data();
		auto destroy = [old = std::move(label)]() mutable {
			old.destroyDelayed();
		};

		using namespace rpl::mappers;
		saved->shownValue()
			| rpl::filter(_1 == false)
			| rpl::take(1)
			| rpl::start_with_done(
				std::move(destroy),
				saved->lifetime());
	}
	if (!text.isEmpty()) {
		label.create(
			parent,
			object_ptr<Ui::FlatLabel>(
				parent,
				text,
				st::changePhoneError));
		label->hide(anim::type::instant);
		label->moveToLeft(x, y);
		label->show(anim::type::normal);
	}
}

} // namespace

class ChangePhoneBox::EnterPhone : public Ui::BoxContent {
public:
	EnterPhone(QWidget*, not_null<Window::SessionController*> controller);

	void setInnerFocus() override {
		_phone->setFocusFast();
	}

protected:
	void prepare() override;

private:
	void submit();
	void sendPhoneDone(
		const MTPauth_SentCode &result,
		const QString &phoneNumber);
	void sendPhoneFail(const MTP::Error &error, const QString &phoneNumber);
	void showError(const QString &text);
	void hideError() {
		showError(QString());
	}

	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;

	object_ptr<Ui::PhoneInput> _phone = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _error = { nullptr };
	mtpRequestId _requestId = 0;

};

class ChangePhoneBox::EnterCode : public Ui::BoxContent {
public:
	EnterCode(
		QWidget*,
		not_null<Main::Session*> session,
		const QString &phone,
		const QString &hash,
		int codeLength,
		int callTimeout);

	void setInnerFocus() override {
		_code->setFocusFast();
	}

protected:
	void prepare() override;

private:
	void submit();
	void sendCall();
	void updateCall();
	void sendCodeFail(const MTP::Error &error);
	void showError(const QString &text);
	void hideError() {
		showError(QString());
	}
	int countHeight();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	QString _phone;
	QString _hash;
	int _codeLength = 0;
	int _callTimeout = 0;
	object_ptr<Ui::SentCodeField> _code = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _error = { nullptr };
	object_ptr<Ui::FlatLabel> _callLabel = { nullptr };
	mtpRequestId _requestId = 0;
	Ui::SentCodeCall _call;

};

ChangePhoneBox::EnterPhone::EnterPhone(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _api(&controller->session().mtp()) {
}

void ChangePhoneBox::EnterPhone::prepare() {
	setTitle(tr::lng_change_phone_title());

	const auto phoneValue = QString();
	_phone.create(
		this,
		st::defaultInputField,
		tr::lng_change_phone_new_title(),
		Countries::ExtractPhoneCode(_controller->session().user()->phone()),
		phoneValue,
		[](const QString &s) { return Countries::Groups(s); });

	_phone->resize(
		st::boxWidth - 2 * st::boxPadding.left(),
		_phone->height());
	_phone->moveToLeft(st::boxPadding.left(), st::boxLittleSkip);
	connect(_phone, &Ui::PhoneInput::submitted, [=] { submit(); });

	const auto description = object_ptr<Ui::FlatLabel>(
		this,
		tr::lng_change_phone_new_description(tr::now),
		st::changePhoneLabel);
	const auto errorSkip = st::boxLittleSkip
		+ st::changePhoneError.style.font->height;
	description->moveToLeft(
		st::boxPadding.left(),
		_phone->y() + _phone->height() + errorSkip + st::boxLittleSkip);

	setDimensions(
		st::boxWidth,
		description->bottomNoMargins() + st::boxLittleSkip);

	addButton(tr::lng_change_phone_new_submit(), [this] { submit(); });
	addButton(tr::lng_cancel(), [this] { closeBox(); });
}

void ChangePhoneBox::EnterPhone::submit() {
	if (_requestId) {
		return;
	}
	hideError();

	const auto phoneNumber = _phone->getLastText().trimmed();
	_requestId = _api.request(MTPaccount_SendChangePhoneCode(
		MTP_string(phoneNumber),
		MTP_codeSettings(MTP_flags(0), MTP_vector<MTPbytes>())
	)).done([=](const MTPauth_SentCode &result) {
		_requestId = 0;
		sendPhoneDone(result, phoneNumber);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		sendPhoneFail(error, phoneNumber);
	}).handleFloodErrors().send();
}

void ChangePhoneBox::EnterPhone::sendPhoneDone(
		const MTPauth_SentCode &result,
		const QString &phoneNumber) {
	using CodeData = const MTPDauth_sentCode&;
	const auto &data = result.match([](const auto &data) -> CodeData {
		return data;
	});

	auto codeLength = 0;
	const auto hasLength = data.vtype().match([&](
			const MTPDauth_sentCodeTypeApp &typeData) {
		LOG(("Error: should not be in-app code!"));
		showError(Lang::Hard::ServerError());
		return false;
	}, [&](const MTPDauth_sentCodeTypeSms &typeData) {
		codeLength = typeData.vlength().v;
		return true;
	}, [&](const MTPDauth_sentCodeTypeCall &typeData) {
		codeLength = typeData.vlength().v;
		return true;
	}, [&](const MTPDauth_sentCodeTypeFlashCall &typeData) {
		LOG(("Error: should not be flashcall!"));
		showError(Lang::Hard::ServerError());
		return false;
	}, [&](const MTPDauth_sentCodeTypeMissedCall &data) {
		LOG(("Error: should not be missedcall!"));
		showError(Lang::Hard::ServerError());
		return false;
	});
	if (!hasLength) {
		return;
	}
	const auto phoneCodeHash = qs(data.vphone_code_hash());
	const auto callTimeout = [&] {
		if (const auto nextType = data.vnext_type()) {
			return nextType->match([&](const MTPDauth_sentCodeTypeCall &) {
				return data.vtimeout().value_or(60);
			}, [](const auto &) {
				return 0;
			});
		}
		return 0;
	}();
	_controller->show(
		Box<EnterCode>(
			&_controller->session(),
			phoneNumber,
			phoneCodeHash,
			codeLength,
			callTimeout),
		Ui::LayerOption::KeepOther);
}

void ChangePhoneBox::EnterPhone::sendPhoneFail(
		const MTP::Error &error,
		const QString &phoneNumber) {
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error(tr::now));
	} else if (error.type() == qstr("PHONE_NUMBER_INVALID")) {
		showError(tr::lng_bad_phone(tr::now));
	} else if (error.type() == qstr("PHONE_NUMBER_BANNED")) {
		Ui::ShowPhoneBannedError(&_controller->window(), phoneNumber);
	} else if (error.type() == qstr("PHONE_NUMBER_OCCUPIED")) {
		_controller->show(
			Box<Ui::InformBox>(
				tr::lng_change_phone_occupied(
					tr::now,
					lt_phone,
					Ui::FormatPhone(phoneNumber)),
				tr::lng_box_ok(tr::now)),
			Ui::LayerOption::CloseOther);
	} else {
		showError(Lang::Hard::ServerError());
	}
}

void ChangePhoneBox::EnterPhone::showError(const QString &text) {
	CreateErrorLabel(
		this,
		_error,
		text,
		st::boxPadding.left(),
		_phone->y() + _phone->height() + st::boxLittleSkip);
	if (!text.isEmpty()) {
		_phone->showError();
	}
}

ChangePhoneBox::EnterCode::EnterCode(
	QWidget*,
	not_null<Main::Session*> session,
	const QString &phone,
	const QString &hash,
	int codeLength,
	int callTimeout)
: _session(session)
, _api(&session->mtp())
, _phone(phone)
, _hash(hash)
, _codeLength(codeLength)
, _callTimeout(callTimeout)
, _call([this] { sendCall(); }, [this] { updateCall(); }) {
}

void ChangePhoneBox::EnterCode::prepare() {
	setTitle(tr::lng_change_phone_title());

	const auto descriptionText = tr::lng_change_phone_code_description(
		tr::now,
		lt_phone,
		Ui::Text::Bold(Ui::FormatPhone(_phone)),
		Ui::Text::WithEntities);
	const auto description = object_ptr<Ui::FlatLabel>(
		this,
		rpl::single(descriptionText),
		st::changePhoneLabel);
	description->moveToLeft(st::boxPadding.left(), 0);

	const auto phoneValue = QString();
	_code.create(
		this,
		st::defaultInputField,
		tr::lng_change_phone_code_title(),
		phoneValue);
	_code->setAutoSubmit(_codeLength, [=] { submit(); });
	_code->setChangedCallback([=] { hideError(); });

	_code->resize(st::boxWidth - 2 * st::boxPadding.left(), _code->height());
	_code->moveToLeft(st::boxPadding.left(), description->bottomNoMargins());
	connect(_code, &Ui::InputField::submitted, [=] { submit(); });

	setDimensions(st::boxWidth, countHeight());

	if (_callTimeout > 0) {
		_call.setStatus({ Ui::SentCodeCall::State::Waiting, _callTimeout });
		updateCall();
	}

	addButton(tr::lng_change_phone_new_submit(), [=] { submit(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

int ChangePhoneBox::EnterCode::countHeight() {
	const auto errorSkip = st::boxLittleSkip
		+ st::changePhoneError.style.font->height;
	return _code->bottomNoMargins() + errorSkip + 3 * st::boxLittleSkip;
}

void ChangePhoneBox::EnterCode::submit() {
	if (_requestId) {
		return;
	}
	hideError();

	const auto session = _session;
	const auto code = _code->getDigitsOnly();
	const auto weak = Ui::MakeWeak(this);
	_requestId = session->api().request(MTPaccount_ChangePhone(
		MTP_string(_phone),
		MTP_string(_hash),
		MTP_string(code)
	)).done([=](const MTPUser &result) {
		_requestId = 0;
		session->data().processUser(result);
		if (weak) {
			Ui::hideLayer();
		}
		Ui::Toast::Show(tr::lng_change_phone_success(tr::now));
	}).fail(crl::guard(this, [=](const MTP::Error &error) {
		_requestId = 0;
		sendCodeFail(error);
	})).handleFloodErrors().send();
}

void ChangePhoneBox::EnterCode::sendCall() {
	_api.request(MTPauth_ResendCode(
		MTP_string(_phone),
		MTP_string(_hash)
	)).done([=](const MTPauth_SentCode &result) {
		_call.callDone();
	}).send();
}

void ChangePhoneBox::EnterCode::updateCall() {
	const auto text = _call.getText();
	if (text.isEmpty()) {
		_callLabel.destroy();
	} else if (!_callLabel) {
		_callLabel.create(this, text, st::changePhoneLabel);
		_callLabel->moveToLeft(
			st::boxPadding.left(),
			countHeight() - _callLabel->height());
		_callLabel->show();
	} else {
		_callLabel->setText(text);
	}
}

void ChangePhoneBox::EnterCode::showError(const QString &text) {
	CreateErrorLabel(
		this,
		_error,
		text,
		st::boxPadding.left(),
		_code->y() + _code->height() + st::boxLittleSkip);
	if (!text.isEmpty()) {
		_code->showError();
	}
}

void ChangePhoneBox::EnterCode::sendCodeFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error(tr::now));
	} else if (error.type() == qstr("PHONE_CODE_EMPTY")
		|| error.type() == qstr("PHONE_CODE_INVALID")) {
		showError(tr::lng_bad_code(tr::now));
	} else if (error.type() == qstr("PHONE_CODE_EXPIRED")
		|| error.type() == qstr("PHONE_NUMBER_BANNED")) {
		closeBox(); // Go back to phone input.
	} else if (error.type() == qstr("PHONE_NUMBER_INVALID")) {
		showError(tr::lng_bad_phone(tr::now));
	} else {
		showError(Lang::Hard::ServerError());
	}
}

ChangePhoneBox::ChangePhoneBox(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

void ChangePhoneBox::prepare() {
	setTitle(tr::lng_change_phone_title());
	addButton(tr::lng_change_phone_button(), [=, controller = _controller] {
		auto callback = [=] {
			controller->show(
				Box<EnterPhone>(controller),
				Ui::LayerOption::CloseOther);
		};
		controller->show(
			Box<Ui::ConfirmBox>(
				tr::lng_change_phone_warning(tr::now),
				std::move(callback)),
			Ui::LayerOption::CloseOther);
	});
	addButton(tr::lng_cancel(), [this] {
		closeBox();
	});

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_change_phone_about(Ui::Text::RichLangValue),
		st::changePhoneDescription);
	label->moveToLeft(
		(st::boxWideWidth - label->width()) / 2,
		st::changePhoneDescriptionTop);

	setDimensions(
		st::boxWideWidth,
		label->bottomNoMargins() + st::boxLittleSkip);
}

void ChangePhoneBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);
	st::changePhoneIcon.paint(
		p,
		(width() - st::changePhoneIcon.width()) / 2,
		st::changePhoneIconTop,
		width());
}
