/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_code.h"

#include "lang/lang_keys.h"
#include "intro/intro_signup.h"
#include "intro/intro_password_check.h"
#include "core/update_checker.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "boxes/confirm_box.h"
#include "main/main_account.h"
#include "mtproto/mtp_instance.h"
#include "app.h"
#include "styles/style_intro.h"

namespace Intro {
namespace details {

CodeInput::CodeInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder)
: Ui::MaskedInputField(parent, st, std::move(placeholder)) {
}

void CodeInput::setDigitsCountMax(int digitsCount) {
	_digitsCountMax = digitsCount;
}

void CodeInput::correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) {
	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (now[i].isDigit()) {
			++digitCount;
		}
	}
	accumulate_min(digitCount, _digitsCountMax);
	auto strict = (digitCount == _digitsCountMax);

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		QChar ch(now[i]);
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
		if (i == oldPos) {
			newPos = newText.length();
		}
	}
	if (newPos < 0 || newPos > newText.size()) {
		newPos = newText.size();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	if (newPos != nowCursor) {
		nowCursor = newPos;
		setCursorPosition(nowCursor);
	}
}

CodeWidget::CodeWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _noTelegramCode(this, tr::lng_code_no_telegram(tr::now), st::introLink)
, _code(this, st::introCode, tr::lng_code_ph())
, _callTimer([=] { sendCall(); })
, _callStatus(getData()->callStatus)
, _callTimeout(getData()->callTimeout)
, _callLabel(this, st::introDescription)
, _checkRequestTimer([=] { checkRequest(); }) {
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	connect(_code, &CodeInput::changed, [=] { codeChanged(); });
	_noTelegramCode->addClickHandler([=] { noTelegramCode(); });

	_code->setDigitsCountMax(getData()->codeLength);

	setTitleText(rpl::single(App::formatPhone(getData()->phone)));
	updateDescText();
}

void CodeWidget::refreshLang() {
	if (_noTelegramCode) {
		_noTelegramCode->setText(tr::lng_code_no_telegram(tr::now));
	}
	updateDescText();
	updateControlsGeometry();
}

int CodeWidget::errorTop() const {
	return contentTop() + st::introErrorBelowLinkTop;
}

void CodeWidget::updateDescText() {
	const auto byTelegram = getData()->codeByTelegram;
	setDescriptionText(
		(byTelegram ? tr::lng_code_from_telegram : tr::lng_code_desc)(
			Ui::Text::RichLangValue));
	if (getData()->codeByTelegram) {
		_noTelegramCode->show();
		_callTimer.cancel();
	} else {
		_noTelegramCode->hide();
		_callStatus = getData()->callStatus;
		_callTimeout = getData()->callTimeout;
		if (_callStatus == CallStatus::Waiting && !_callTimer.isActive()) {
			_callTimer.callEach(1000);
		}
	}
	updateCallText();
}

void CodeWidget::updateCallText() {
	auto text = ([this]() -> QString {
		if (getData()->codeByTelegram) {
			return QString();
		}
		switch (_callStatus) {
		case CallStatus::Waiting: {
			if (_callTimeout >= 3600) {
				return tr::lng_code_call(
					tr::now,
					lt_minutes,
					qsl("%1:%2"
					).arg(_callTimeout / 3600
					).arg((_callTimeout / 60) % 60, 2, 10, QChar('0')),
					lt_seconds,
					qsl("%1").arg(_callTimeout % 60, 2, 10, QChar('0')));
			} else {
				return tr::lng_code_call(
					tr::now,
					lt_minutes,
					QString::number(_callTimeout / 60),
					lt_seconds,
					qsl("%1").arg(_callTimeout % 60, 2, 10, QChar('0')));
			}
		} break;
		case CallStatus::Calling:
			return tr::lng_code_calling(tr::now);
		case CallStatus::Called:
			return tr::lng_code_called(tr::now);
		}
		return QString();
	})();
	_callLabel->setText(text);
	_callLabel->setVisible(!text.isEmpty() && !animating());
}

void CodeWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void CodeWidget::updateControlsGeometry() {
	_code->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto linkTop = _code->y() + _code->height() + st::introLinkTop;
	_noTelegramCode->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
	_callLabel->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
}

void CodeWidget::showCodeError(rpl::producer<QString> text) {
	_code->showError();
	showError(std::move(text));
}

void CodeWidget::setInnerFocus() {
	_code->setFocusFast();
}

void CodeWidget::activate() {
	Step::activate();
	_code->show();
	if (getData()->codeByTelegram) {
		_noTelegramCode->show();
	} else {
		_callLabel->show();
	}
	setInnerFocus();
}

void CodeWidget::finished() {
	Step::finished();
	_checkRequestTimer.cancel();
	_callTimer.cancel();
	apiClear();

	cancelled();
	_sentCode.clear();
	_code->setText(QString());
}

void CodeWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
	api().request(base::take(_callRequestId)).cancel();
	api().request(MTPauth_CancelCode(
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash)
	)).send();
}

void CodeWidget::stopCheck() {
	_checkRequestTimer.cancel();
}

void CodeWidget::checkRequest() {
	auto status = api().instance().state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			if (_sentRequest) {
				api().request(base::take(_sentRequest)).cancel();
				_sentCode.clear();
			}
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void CodeWidget::codeSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	_sentRequest = 0;
	result.match([&](const MTPDauth_authorization &data) {
		if (data.vuser().type() != mtpc_user
			|| !data.vuser().c_user().is_self()) {
			showError(rpl::single(Lang::Hard::ServerError()));
			return;
		}
		finish(data.vuser());
	}, [&](const MTPDauth_authorizationSignUpRequired &data) {
		if (const auto terms = data.vterms_of_service()) {
			terms->match([&](const MTPDhelp_termsOfService &data) {
				getData()->termsLock = Window::TermsLock::FromMTP(
					nullptr,
					data);
			});
		} else {
			getData()->termsLock = Window::TermsLock();
		}
		goReplace<SignupWidget>(Animate::Forward);
	});
}

void CodeWidget::codeSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showCodeError(tr::lng_flood_error());
		return;
	}

	stopCheck();
	_sentRequest = 0;
	auto &err = error.type();
	if (err == qstr("PHONE_NUMBER_INVALID")
		|| err == qstr("PHONE_CODE_EXPIRED")
		|| err == qstr("PHONE_NUMBER_BANNED")) { // show error
		goBack();
	} else if (err == qstr("PHONE_CODE_EMPTY") || err == qstr("PHONE_CODE_INVALID")) {
		showCodeError(tr::lng_bad_code());
	} else if (err == qstr("SESSION_PASSWORD_NEEDED")) {
		_checkRequestTimer.callEach(1000);
		_sentRequest = api().request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			gotPassword(result);
		}).fail([=](const MTP::Error &error) {
			codeSubmitFail(error);
		}).handleFloodErrors().send();
	} else if (Logs::DebugEnabled()) { // internal server error
		showCodeError(rpl::single(err + ": " + error.description()));
	} else {
		showCodeError(rpl::single(Lang::Hard::ServerError()));
	}
}

void CodeWidget::codeChanged() {
	hideError();
	submit();
}

void CodeWidget::sendCall() {
	if (_callStatus == CallStatus::Waiting) {
		if (--_callTimeout <= 0) {
			_callStatus = CallStatus::Calling;
			_callTimer.cancel();
			_callRequestId = api().request(MTPauth_ResendCode(
				MTP_string(getData()->phone),
				MTP_bytes(getData()->phoneHash)
			)).done([=](const MTPauth_SentCode &result) {
				callDone(result);
			}).send();
		} else {
			getData()->callStatus = _callStatus;
			getData()->callTimeout = _callTimeout;
		}
		updateCallText();
	}
}

void CodeWidget::callDone(const MTPauth_SentCode &v) {
	if (v.type() == mtpc_auth_sentCode) {
		fillSentCodeData(v.c_auth_sentCode());
		_code->setDigitsCountMax(getData()->codeLength);
	}
	if (_callStatus == CallStatus::Calling) {
		_callStatus = CallStatus::Called;
		getData()->callStatus = _callStatus;
		getData()->callTimeout = _callTimeout;
		updateCallText();
	}
}

void CodeWidget::gotPassword(const MTPaccount_Password &result) {
	Expects(result.type() == mtpc_account_password);

	stopCheck();
	_sentRequest = 0;
	const auto &d = result.c_account_password();
	getData()->pwdRequest = Core::ParseCloudPasswordCheckRequest(d);
	if (!d.vcurrent_algo() || !d.vsrp_id() || !d.vsrp_B()) {
		LOG(("API Error: No current password received on login."));
		_code->setFocus();
		return;
	} else if (!getData()->pwdRequest) {
		const auto callback = [=](Fn<void()> &&close) {
			Core::UpdateApplication();
			close();
		};
		Ui::show(Box<ConfirmBox>(
			tr::lng_passport_app_out_of_date(tr::now),
			tr::lng_menu_update(tr::now),
			callback));
		return;
	}
	getData()->hasRecovery = d.is_has_recovery();
	getData()->pwdHint = qs(d.vhint().value_or_empty());
	getData()->pwdNotEmptyPassport = d.is_has_secure_values();
	goReplace<PasswordCheckWidget>(Animate::Forward);
}

void CodeWidget::submit() {
	const auto text = QString(
		_code->getLastText()
	).remove(
		QRegularExpression("[^\\d]")
	).mid(0, getData()->codeLength);

	if (_sentRequest
		|| _sentCode == text
		|| text.size() != getData()->codeLength) {
		return;
	}

	hideError();

	_checkRequestTimer.callEach(1000);

	_sentCode = text;
	getData()->pwdRequest = Core::CloudPasswordCheckRequest();
	getData()->hasRecovery = false;
	getData()->pwdHint = QString();
	getData()->pwdNotEmptyPassport = false;
	_sentRequest = api().request(MTPauth_SignIn(
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash),
		MTP_string(_sentCode)
	)).done([=](const MTPauth_Authorization &result) {
		codeSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		codeSubmitFail(error);
	}).handleFloodErrors().send();
}

void CodeWidget::noTelegramCode() {
	if (_noTelegramCodeRequestId) {
		return;
	}
	_noTelegramCodeRequestId = api().request(MTPauth_ResendCode(
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash)
	)).done([=](const MTPauth_SentCode &result) {
		noTelegramCodeDone(result);
	}).fail([=](const MTP::Error &error) {
		noTelegramCodeFail(error);
	}).handleFloodErrors().send();
}

void CodeWidget::noTelegramCodeDone(const MTPauth_SentCode &result) {
	_noTelegramCodeRequestId = 0;

	if (result.type() != mtpc_auth_sentCode) {
		showCodeError(rpl::single(Lang::Hard::ServerError()));
		return;
	}

	const auto &d = result.c_auth_sentCode();
	fillSentCodeData(d);
	_code->setDigitsCountMax(getData()->codeLength);
	const auto next = d.vnext_type();
	if (next && next->type() == mtpc_auth_codeTypeCall) {
		getData()->callStatus = CallStatus::Waiting;
		getData()->callTimeout = d.vtimeout().value_or(60);
	} else {
		getData()->callStatus = CallStatus::Disabled;
		getData()->callTimeout = 0;
	}
	getData()->codeByTelegram = false;
	updateDescText();
}

void CodeWidget::noTelegramCodeFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		_noTelegramCodeRequestId = 0;
		showCodeError(tr::lng_flood_error());
		return;
	}

	_noTelegramCodeRequestId = 0;
	if (Logs::DebugEnabled()) { // internal server error
		showCodeError(rpl::single(error.type() + ": " + error.description()));
	} else {
		showCodeError(rpl::single(Lang::Hard::ServerError()));
	}
}

} // namespace details
} // namespace Intro
