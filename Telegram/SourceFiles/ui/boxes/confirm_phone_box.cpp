/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/confirm_phone_box.h"

#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Ui {

ConfirmPhoneBox::ConfirmPhoneBox(
	QWidget*,
	const QString &phone,
	int codeLength,
	std::optional<int> timeout)
: _phone(phone)
, _sentCodeLength(codeLength)
, _call([this] { sendCall(); }, [this] { update(); }) {
	if (timeout) {
		_call.setStatus({ Ui::SentCodeCall::State::Waiting, *timeout });
	}
}

void ConfirmPhoneBox::sendCall() {
	_resendRequests.fire({});
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

	setDimensions(
		st::boxWidth,
		st::usernamePadding.top()
			+ _code->height()
			+ st::usernameSkip
			+ _about->height()
			+ st::usernameSkip);

	connect(_code, &Ui::InputField::submitted, [=] { sendCode(); });

	showChildren();
}

void ConfirmPhoneBox::sendCode() {
	if (_isWaitingCheck) {
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

	_checkRequests.fire_copy(code);
	_isWaitingCheck = true;
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
	const auto callText = _call.getText();
	if (!callText.isEmpty()) {
		p.setPen(st::usernameDefaultFg);
		const auto callTextRect = QRect(
			st::usernamePadding.left(),
			_about->y() + _about->height(),
			width() - 2 * st::usernamePadding.left(),
			st::usernameSkip);
		p.drawText(callTextRect, callText, style::al_left);
	}
	auto errorText = _error;
	if (errorText.isEmpty()) {
		p.setPen(st::usernameDefaultFg);
		errorText = tr::lng_confirm_phone_enter_code(tr::now);
	} else {
		p.setPen(st::boxTextFgError);
	}
	const auto errorTextRect = QRect(
		st::usernamePadding.left(),
		_code->y() + _code->height(),
		width() - 2 * st::usernamePadding.left(),
		st::usernameSkip);
	p.drawText(errorTextRect, errorText, style::al_left);
}

void ConfirmPhoneBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_code->resize(
		width() - st::usernamePadding.left() - st::usernamePadding.right(),
		_code->height());
	_code->moveToLeft(st::usernamePadding.left(), st::usernamePadding.top());

	_about->moveToLeft(
		st::usernamePadding.left(),
		_code->y() + _code->height() + st::usernameSkip);
}

void ConfirmPhoneBox::setInnerFocus() {
	_code->setFocusFast();
}

rpl::producer<QString> ConfirmPhoneBox::checkRequests() const {
	return _checkRequests.events();
}

rpl::producer<> ConfirmPhoneBox::resendRequests() const {
	return _resendRequests.events();
}

void ConfirmPhoneBox::callDone() {
	_call.callDone();
}

void ConfirmPhoneBox::showServerError(const QString &text) {
	_isWaitingCheck = false;
	_code->setDisabled(false);
	_code->setFocus();
	showError(text);
}

QString ConfirmPhoneBox::getPhone() const {
	return _phone;
}

} // namespace Ui
