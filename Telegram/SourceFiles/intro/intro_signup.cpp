/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_signup.h"

#include "intro/intro_widget.h"
#include "core/file_utilities.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/special_buttons.h"
#include "styles/style_intro.h"
#include "styles/style_boxes.h"

namespace Intro {
namespace details {

SignupWidget::SignupWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _photo(
	this,
	data->controller,
	tr::lng_settings_crop_profile(tr::now),
	Ui::UserpicButton::Role::ChangePhoto,
	st::defaultUserpicButton)
, _first(this, st::introName, tr::lng_signup_firstname())
, _last(this, st::introName, tr::lng_signup_lastname())
, _invertOrder(langFirstNameGoesSecond()) {
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	if (_invertOrder) {
		setTabOrder(_last, _first);
	} else {
		setTabOrder(_first, _last);
	}

	setErrorCentered(true);

	setTitleText(tr::lng_signup_title());
	setDescriptionText(tr::lng_signup_desc());
	setMouseTracking(true);
}

void SignupWidget::finishInit() {
	showTerms();
}

void SignupWidget::refreshLang() {
	_invertOrder = langFirstNameGoesSecond();
	if (_invertOrder) {
		setTabOrder(_last, _first);
	} else {
		setTabOrder(_first, _last);
	}
	updateControlsGeometry();
}

void SignupWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void SignupWidget::updateControlsGeometry() {
	auto photoRight = contentLeft() + st::introNextButton.width;
	auto photoTop = contentTop() + st::introPhotoTop;
	_photo->moveToLeft(photoRight - _photo->width(), photoTop);

	auto firstTop = contentTop() + st::introStepFieldTop;
	auto secondTop = firstTop + st::introName.heightMin + st::introPhoneTop;
	if (_invertOrder) {
		_last->moveToLeft(contentLeft(), firstTop);
		_first->moveToLeft(contentLeft(), secondTop);
	} else {
		_first->moveToLeft(contentLeft(), firstTop);
		_last->moveToLeft(contentLeft(), secondTop);
	}
}

void SignupWidget::setInnerFocus() {
	if (_invertOrder || _last->hasFocus()) {
		_last->setFocusFast();
	} else {
		_first->setFocusFast();
	}
}

void SignupWidget::activate() {
	Step::activate();
	_first->show();
	_last->show();
	_photo->show();
	setInnerFocus();
}

void SignupWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

void SignupWidget::nameSubmitDone(const MTPauth_Authorization &result) {
	auto &d = result.c_auth_authorization();
	if (d.vuser().type() != mtpc_user || !d.vuser().c_user().is_self()) { // wtf?
		showError(rpl::single(Lang::Hard::ServerError()));
		return;
	}
	finish(d.vuser(), _photo->takeResultImage());
}

void SignupWidget::nameSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error());
		if (_invertOrder) {
			_first->setFocus();
		} else {
			_last->setFocus();
		}
		return;
	}

	auto &err = error.type();
	if (err == qstr("PHONE_NUMBER_FLOOD")) {
		Ui::show(Box<InformBox>(tr::lng_error_phone_flood(tr::now)));
	} else if (err == qstr("PHONE_NUMBER_INVALID")
		|| err == qstr("PHONE_NUMBER_BANNED")
		|| err == qstr("PHONE_CODE_EXPIRED")
		|| err == qstr("PHONE_CODE_EMPTY")
		|| err == qstr("PHONE_CODE_INVALID")
		|| err == qstr("PHONE_NUMBER_OCCUPIED")) {
		goBack();
	} else if (err == "FIRSTNAME_INVALID") {
		showError(tr::lng_bad_name());
		_first->setFocus();
	} else if (err == "LASTNAME_INVALID") {
		showError(tr::lng_bad_name());
		_last->setFocus();
	} else {
		if (Logs::DebugEnabled()) { // internal server error
			showError(rpl::single(err + ": " + error.description()));
		} else {
			showError(rpl::single(Lang::Hard::ServerError()));
		}
		if (_invertOrder) {
			_last->setFocus();
		} else {
			_first->setFocus();
		}
	}
}

void SignupWidget::submit() {
	if (_sentRequest) {
		return;
	}
	if (_invertOrder) {
		if ((_last->hasFocus() || _last->getLastText().trimmed().length()) && !_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		} else if (!_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		}
	} else {
		if ((_first->hasFocus() || _first->getLastText().trimmed().length()) && !_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		} else if (!_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		}
	}

	const auto send = [&] {
		hideError();

		_firstName = _first->getLastText().trimmed();
		_lastName = _last->getLastText().trimmed();
		_sentRequest = api().request(MTPauth_SignUp(
			MTP_string(getData()->phone),
			MTP_bytes(getData()->phoneHash),
			MTP_string(_firstName),
			MTP_string(_lastName)
		)).done([=](const MTPauth_Authorization &result) {
			nameSubmitDone(result);
		}).fail([=](const MTP::Error &error) {
			nameSubmitFail(error);
		}).handleFloodErrors().send();
	};
	if (_termsAccepted
		|| getData()->termsLock.text.text.isEmpty()
		|| !getData()->termsLock.popup) {
		send();
	} else {
		acceptTerms(crl::guard(this, [=] {
			_termsAccepted = true;
			send();
		}));
	}
}

rpl::producer<QString> SignupWidget::nextButtonText() const {
	return tr::lng_intro_finish();
}

} // namespace details
} // namespace Intro
