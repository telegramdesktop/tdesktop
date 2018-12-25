/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/introsignup.h"

#include "styles/style_intro.h"
#include "styles/style_boxes.h"
#include "core/file_utilities.h"
#include "boxes/photo_crop_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/special_buttons.h"

namespace Intro {

SignupWidget::SignupWidget(QWidget *parent, Widget::Data *data) : Step(parent, data)
, _photo(
	this,
	lang(lng_settings_crop_profile),
	Ui::UserpicButton::Role::ChangePhoto,
	st::defaultUserpicButton)
, _first(this, st::introName, langFactory(lng_signup_firstname))
, _last(this, st::introName, langFactory(lng_signup_lastname))
, _invertOrder(langFirstNameGoesSecond())
, _checkRequest(this) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });
	if (_invertOrder) {
		setTabOrder(_last, _first);
	} else {
		setTabOrder(_first, _last);
	}

	connect(_checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	setErrorCentered(true);

	setTitleText(langFactory(lng_signup_title));
	setDescriptionText(langFactory(lng_signup_desc));
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
	MTP::cancel(base::take(_sentRequest));
}

void SignupWidget::stopCheck() {
	_checkRequest->stop();
}

void SignupWidget::onCheckRequest() {
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

void SignupWidget::nameSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	auto &d = result.c_auth_authorization();
	if (d.vuser.type() != mtpc_user || !d.vuser.c_user().is_self()) { // wtf?
		showError(&Lang::Hard::ServerError);
		return;
	}
	finish(d.vuser, _photo->takeResultImage());
}

bool SignupWidget::nameSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		showError(langFactory(lng_flood_error));
		if (_invertOrder) {
			_first->setFocus();
		} else {
			_last->setFocus();
		}
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	stopCheck();
	auto &err = error.type();
	if (err == qstr("PHONE_NUMBER_FLOOD")) {
		Ui::show(Box<InformBox>(lang(lng_error_phone_flood)));
		return true;
	} else if (err == qstr("PHONE_NUMBER_INVALID") || err == qstr("PHONE_CODE_EXPIRED") ||
		err == qstr("PHONE_CODE_EMPTY") || err == qstr("PHONE_CODE_INVALID") ||
		err == qstr("PHONE_NUMBER_OCCUPIED")) {
		goBack();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		showError(langFactory(lng_bad_name));
		_first->setFocus();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		showError(langFactory(lng_bad_name));
		_last->setFocus();
		return true;
	}
	if (Logs::DebugEnabled()) { // internal server error
		auto text = err + ": " + error.description();
		showError([text] { return text; });
	} else {
		showError(&Lang::Hard::ServerError);
	}
	if (_invertOrder) {
		_last->setFocus();
	} else {
		_first->setFocus();
	}
	return false;
}

void SignupWidget::onInputChange() {
	hideError();
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
		_sentRequest = MTP::send(
			MTPauth_SignUp(
				MTP_string(getData()->phone),
				MTP_bytes(getData()->phoneHash),
				MTP_string(getData()->code),
				MTP_string(_firstName),
				MTP_string(_lastName)),
			rpcDone(&SignupWidget::nameSubmitDone),
			rpcFail(&SignupWidget::nameSubmitFail));
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

QString SignupWidget::nextButtonText() const {
	return lang(lng_intro_finish);
}

} // namespace Intro
