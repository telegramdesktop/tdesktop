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
#include "ui/text/text_utilities.h"
#include "core/click_handler_types.h" // UrlClickHandler
#include "base/qthelp_url.h" // qthelp::url_encode
#include "base/platform/base_platform_info.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "numbers.h"
#include "app.h"
#include "lang/lang_keys.h"
#include "mtproto/facade.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

object_ptr<ConfirmPhoneBox> CurrentConfirmPhoneBox = { nullptr };

void SendToBannedHelp(const QString &phone) {
	const auto version = QString::fromLatin1(AppVersionStr)
		+ (cAlphaVersion()
			? qsl(" alpha %1").arg(cAlphaVersion())
			: (AppBetaVersion ? " beta" : ""));

	const auto subject = qsl("Banned phone number: ") + phone;

	const auto body = qsl("\
I'm trying to use my mobile phone number: ") + phone + qsl("\n\
But Telegram says it's banned. Please help.\n\
\n\
App version: ") + version + qsl("\n\
OS version: ") + Platform::SystemVersionPretty() + qsl("\n\
Locale: ") + Platform::SystemLanguage();

	const auto url = "mailto:?to="
		+ qthelp::url_encode("login@stel.com")
		+ "&subject="
		+ qthelp::url_encode(subject)
		+ "&body="
		+ qthelp::url_encode(body);

	UrlClickHandler::Open(url);
}

} // namespace

void ShowPhoneBannedError(const QString &phone) {
	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto close = [=] {
		if (*box) {
			(*box)->closeBox();
		}
	};
	*box = Ui::show(Box<ConfirmBox>(
		tr::lng_signin_banned_text(tr::now),
		tr::lng_box_ok(tr::now),
		tr::lng_signin_banned_help(tr::now),
		close,
		[=] { SendToBannedHelp(phone); close(); }));
}

SentCodeField::SentCodeField(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: Ui::InputField(parent, st, std::move(placeholder), val) {
	connect(this, &Ui::InputField::changed, [this] { fix(); });
}

void SentCodeField::setAutoSubmit(int length, Fn<void()> submitCallback) {
	_autoSubmitLength = length;
	_submitCallback = std::move(submitCallback);
}

void SentCodeField::setChangedCallback(Fn<void()> changedCallback) {
	_changedCallback = std::move(changedCallback);
}

QString SentCodeField::getDigitsOnly() const {
	return QString(
		getLastText()
	).remove(
		QRegularExpression("[^\\d]")
	);
}

void SentCodeField::fix() {
	if (_fixing) return;

	_fixing = true;
	auto newText = QString();
	const auto now = getLastText();
	auto oldPos = textCursor().position();
	auto newPos = -1;
	auto oldLen = now.size();
	auto digitCount = 0;
	for (const auto ch : now) {
		if (ch.isDigit()) {
			++digitCount;
		}
	}

	if (_autoSubmitLength > 0 && digitCount > _autoSubmitLength) {
		digitCount = _autoSubmitLength;
	}
	auto strict = (_autoSubmitLength > 0)
		&& (digitCount == _autoSubmitLength);

	newText.reserve(oldLen);
	int i = 0;
	for (const auto ch : now) {
		if (i++ == oldPos) {
			newPos = newText.length();
		}
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			newText += ch;
			if (strict && !digitCount) {
				break;
			}
		} else if (ch == '-') {
			newText += ch;
		}
	}
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != now) {
		setText(newText);
		setCursorPosition(newPos);
	}
	_fixing = false;

	if (_changedCallback) {
		_changedCallback();
	}
	if (strict && _submitCallback) {
		_submitCallback();
	}
}

SentCodeCall::SentCodeCall(
	FnMut<void()> callCallback,
	Fn<void()> updateCallback)
: _call(std::move(callCallback))
, _update(std::move(updateCallback)) {
	_timer.setCallback([=] {
		if (_status.state == State::Waiting) {
			if (--_status.timeout <= 0) {
				_status.state = State::Calling;
				_timer.cancel();
				if (_call) {
					_call();
				}
			}
		}
		if (_update) {
			_update();
		}
	});
}

void SentCodeCall::setStatus(const Status &status) {
	_status = status;
	if (_status.state == State::Waiting) {
		_timer.callEach(1000);
	}
}

QString SentCodeCall::getText() const {
	switch (_status.state) {
	case State::Waiting: {
		if (_status.timeout >= 3600) {
			return tr::lng_code_call(tr::now, lt_minutes, qsl("%1:%2").arg(_status.timeout / 3600).arg((_status.timeout / 60) % 60, 2, 10, QChar('0')), lt_seconds, qsl("%1").arg(_status.timeout % 60, 2, 10, QChar('0')));
		}
		return tr::lng_code_call(tr::now, lt_minutes, QString::number(_status.timeout / 60), lt_seconds, qsl("%1").arg(_status.timeout % 60, 2, 10, QChar('0')));
	} break;
	case State::Calling: return tr::lng_code_calling(tr::now);
	case State::Called: return tr::lng_code_called(tr::now);
	}
	return QString();
}

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
				_call.setStatus({ SentCodeCall::State::Waiting, data.vtimeout().value_or(60) });
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
			rpl::single(Ui::Text::Bold(App::formatPhone(_phone))),
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
	Ui::show(Box<InformBox>(tr::lng_confirm_phone_success(tr::now, lt_phone, App::formatPhone(_phone))));
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
