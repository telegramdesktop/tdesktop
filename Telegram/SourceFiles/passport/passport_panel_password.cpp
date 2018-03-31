/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_password.h"

#include "passport/passport_panel_controller.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/special_buttons.h"
#include "lang/lang_keys.h"
#include "styles/style_passport.h"
#include "styles/style_boxes.h"

namespace Passport {

PanelAskPassword::PanelAskPassword(
	QWidget *parent,
	not_null<PanelController*> controller)
: RpWidget(parent)
, _controller(controller)
, _userpic(
	this,
	_controller->bot(),
	Ui::UserpicButton::Role::Custom,
	st::passportPasswordUserpic)
, _about1(
	this,
	lng_passport_request1(lt_bot, App::peerName(_controller->bot())),
	Ui::FlatLabel::InitType::Simple,
	st::passportPasswordLabelBold)
, _about2(
	this,
	lang(lng_passport_request2),
	Ui::FlatLabel::InitType::Simple,
	st::passportPasswordLabel)
, _password(
	this,
	st::defaultInputField,
	langFactory(lng_passport_password_placeholder))
, _submit(this, langFactory(lng_passport_next), st::passportPasswordSubmit)
, _forgot(this, lang(lng_signin_recover), st::defaultLinkButton) {
	connect(_password, &Ui::PasswordInput::submitted, this, [=] {
		submit();
	});
	connect(_password, &Ui::PasswordInput::changed, this, [=] {
		hideError();
	});
	if (const auto hint = _controller->passwordHint(); !hint.isEmpty()) {
		_hint.create(
			this,
			hint,
			Ui::FlatLabel::InitType::Simple,
			st::passportPasswordHintLabel);
	}
	_controller->passwordError(
	) | rpl::start_with_next([=](const QString &error) {
		showError(error);
	}, lifetime());

	_password->setFocusFast();
	_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);

	_submit->addClickHandler([=] {
		submit();
	});
}

void PanelAskPassword::showError(const QString &error) {
	_password->showError();
	_error.create(
		this,
		error,
		Ui::FlatLabel::InitType::Simple,
		st::passportErrorLabel);
	_error->show();
	updateControlsGeometry();
}

void PanelAskPassword::hideError() {
	_error.destroy();
}

void PanelAskPassword::submit() {
	_controller->submitPassword(_password->getLastText());
}

void PanelAskPassword::setInnerFocus() {
	_password->setFocusFast();
}

void PanelAskPassword::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelAskPassword::focusInEvent(QFocusEvent *e) {
	_password->setFocusFast();
}

void PanelAskPassword::updateControlsGeometry() {
	const auto padding = st::passportPasswordPadding;
	const auto availableWidth = width()
		- st::boxPadding.left()
		- st::boxPadding.right();

	auto top = st::passportPasswordFieldBottom;
	top -= _password->height();
	_password->resize(
		st::passportPasswordSubmit.width,
		_password->height());
	_password->moveToLeft((width() - _password->width()) / 2, top);

	top -= st::passportPasswordFieldSkip + _about2->height();
	_about2->resizeToWidth(availableWidth);
	_about2->moveToLeft(padding.left(), top);

	top -= _about1->height();
	_about1->resizeToWidth(availableWidth);
	_about1->moveToLeft(padding.left(), top);

	top -= st::passportPasswordUserpicSkip + _userpic->height();
	_userpic->moveToLeft((width() - _userpic->width()) / 2, top);

	top = st::passportPasswordFieldBottom;
	if (_hint) {
		top += st::passportPasswordHintSkip;
		_hint->resizeToWidth(availableWidth);
		_hint->moveToLeft(padding.left(), top);
		top += _hint->height();
	}
	if (_error) {
		top += st::passportPasswordHintSkip;
		_error->resizeToWidth(availableWidth);
		_error->moveToLeft(padding.left(), top);
		top += _error->height();
	}

	top = height() - st::passportPasswordSubmitBottom - _submit->height();
	_submit->moveToLeft((width() - _submit->width()) / 2, top);

	top = height() - st::passportPasswordForgotBottom - _forgot->height();
	_forgot->moveToLeft((width() - _forgot->width()) / 2, top);
}

PanelNoPassword::PanelNoPassword(
	QWidget *parent,
	not_null<PanelController*> controller)
: RpWidget(parent)
, _controller(controller) {
}

PanelPasswordUnconfirmed::PanelPasswordUnconfirmed(
	QWidget *parent,
	not_null<PanelController*> controller)
: RpWidget(parent)
, _controller(controller) {
}

} // namespace Passport
