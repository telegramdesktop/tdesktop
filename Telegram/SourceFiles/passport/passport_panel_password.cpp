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
#include "boxes/passcode_box.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_icon.h"
#include "styles/style_passport.h"
#include "styles/style_layers.h"

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
	tr::lng_passport_request1(
		tr::now,
		lt_bot,
		_controller->bot()->name),
	st::passportPasswordLabelBold)
, _about2(
	this,
	tr::lng_passport_request2(tr::now),
	st::passportPasswordLabel)
, _password(
	this,
	st::defaultInputField,
	tr::lng_passport_password_placeholder())
, _submit(this, tr::lng_passport_next(), st::passportPasswordSubmit)
, _forgot(this, tr::lng_signin_recover(tr::now), st::defaultLinkButton) {
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

	_inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			_inner,
			object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_passport_request1(
					tr::now,
					lt_bot,
					_controller->bot()->name),
				st::passportPasswordLabelBold)),
		st::passportPasswordAbout1Padding)->entity();

	_inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			_inner,
			object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_passport_request2(tr::now),
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

	_inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			_inner,
			object_ptr<Ui::FlatLabel>(
				_inner,
				tr::lng_passport_create_password(tr::now),
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
					? tr::lng_passport_about_password(tr::now)
					: tr::lng_passport_code_sent(tr::now, lt_email, pattern)),
				st::passportPasswordSetupLabel)),
		st::passportFormAbout2Padding)->entity());
	if (pattern.isEmpty()) {
		const auto button = _inner->add(
			object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
				_inner,
				object_ptr<Ui::RoundButton>(
					_inner,
					tr::lng_passport_password_create(),
					st::defaultBoxButton)));
		button->entity()->addClickHandler([=] {
			_controller->setupPassword();
		});
	} else {
		const auto container = _inner->add(
			object_ptr<Ui::FixedHeightWidget>(
				_inner,
				st::defaultBoxButton.height));
		const auto cancel = Ui::CreateChild<Ui::RoundButton>(
			container,
			tr::lng_cancel(),
			st::defaultBoxButton);
		cancel->addClickHandler([=] {
			_controller->cancelPasswordSubmit();
		});
		const auto validate = Ui::CreateChild<Ui::RoundButton>(
			container,
			tr::lng_passport_email_validate(),
			st::defaultBoxButton);
		validate->addClickHandler([=] {
			_controller->validateRecoveryEmail();
		});
		container->widthValue(
		) | rpl::start_with_next([=](int width) {
			const auto both = cancel->width()
				+ validate->width()
				+ st::boxLittleSkip;
			cancel->moveToLeft((width - both) / 2, 0, width);
			validate->moveToRight((width - both) / 2, 0, width);
		}, container->lifetime());
	}
}

} // namespace Passport
