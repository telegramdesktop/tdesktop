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
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/special_buttons.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_icon.h"
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

	_forgot->addClickHandler([=] {
		recover();
	});

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
	_controller->submitPassword(_password->getLastText().toUtf8());
}

void PanelAskPassword::recover() {
	_controller->recoverPassword();
}

void PanelAskPassword::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelAskPassword::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		_password->setFocusFast();
	});
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
, _controller(controller)
, _inner(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	setupContent();
}

void PanelNoPassword::setupContent() {
	widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		_inner->resizeToWidth(newWidth);
	}, _inner->lifetime());

	const auto about1 = _inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			_inner,
			object_ptr<Ui::FlatLabel>(
				_inner,
				lng_passport_request1(
					lt_bot,
					App::peerName(_controller->bot())),
				Ui::FlatLabel::InitType::Simple,
				st::passportPasswordLabelBold)),
		st::passportPasswordAbout1Padding)->entity();

	const auto about2 = _inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			_inner,
			object_ptr<Ui::FlatLabel>(
				_inner,
				lang(lng_passport_request2),
				Ui::FlatLabel::InitType::Simple,
				st::passportPasswordLabel)),
		st::passportPasswordAbout2Padding)->entity();

	const auto iconWrap = _inner->add(
		object_ptr<Ui::CenterWrap<Ui::FixedHeightWidget>>(
			_inner,
			object_ptr<Ui::FixedHeightWidget>(
				_inner,
				st::passportPasswordIconHeight)));
	iconWrap->entity()->resizeToWidth(st::passportPasswordIcon.width());
	Ui::CreateChild<Info::Profile::FloatingIcon>(
		iconWrap->entity(),
		st::passportPasswordIcon,
		QPoint(0, 0));

	const auto about3 = _inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			_inner,
			object_ptr<Ui::FlatLabel>(
				_inner,
				lang(lng_passport_create_password),
				Ui::FlatLabel::InitType::Simple,
				st::passportPasswordSetupLabel)),
		st::passportFormAbout2Padding)->entity();

	refreshBottom();
}

void PanelNoPassword::refreshBottom() {
	const auto pattern = _controller->unconfirmedEmailPattern();
	_about.reset(_inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			_inner,
			object_ptr<Ui::FlatLabel>(
				_inner,
				(pattern.isEmpty()
					? lang(lng_passport_about_password)
					: lng_passport_link_sent(lt_email, pattern)),
				Ui::FlatLabel::InitType::Simple,
				st::passportPasswordSetupLabel)),
		st::passportFormAbout2Padding)->entity());
	const auto button = _inner->add(
		object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
			_inner,
			object_ptr<Ui::RoundButton>(
				_inner,
				langFactory(pattern.isEmpty()
					? lng_passport_password_create
					: lng_cancel),
				st::defaultBoxButton)));
	if (pattern.isEmpty()) {
		button->entity()->addClickHandler([=] {
			_controller->setupPassword();
		});
	} else {
		button->entity()->addClickHandler([=] {
			_controller->cancelPasswordSubmit();
		});
	}
	_button.reset(button);
}

} // namespace Passport
