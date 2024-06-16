/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_code.h"

#include "lang/lang_keys.h"
#include "intro/intro_code_input.h"
#include "intro/intro_signup.h"
#include "intro/intro_password_check.h"
#include "boxes/abstract_box.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/masked_input_field.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/boxes/confirm_box.h"
#include "main/main_account.h"
#include "mtproto/mtp_instance.h"
#include "styles/style_intro.h"

namespace Intro {
namespace details {

CodeWidget::CodeWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _noTelegramCode(this, tr::lng_code_no_telegram(tr::now), st::introLink)
, _code(this)
, _callTimer([=] { sendCall(); })
, _callStatus(getData()->callStatus)
, _callTimeout(getData()->callTimeout)
, _callLabel(this, st::introDescription)
, _checkRequestTimer([=] { checkRequest(); }) {
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_noTelegramCode->addClickHandler([=] { noTelegramCode(); });

	_code->setDigitsCountMax(getData()->codeLength);

	updateDescText();
	setTitleText(_isFragment.value(
	) | rpl::map([=](bool isFragment) {
		return !isFragment
			? rpl::single(Ui::FormatPhone(getData()->phone))
			: tr::lng_intro_fragment_title();
	}) | rpl::flatten_latest());

	account->setHandleLoginCode([=](const QString &code) {
		_code->setCode(code);
		_code->requestCode();
	});

	_code->codeCollected(
	) | rpl::start_with_next([=](const QString &code) {
		hideError();
		submitCode(code);
	}, lifetime());
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
	const auto isFragment = !getData()->codeByFragmentUrl.isEmpty();
	_isFragment = isFragment;
	setDescriptionText(
		isFragment
			? tr::lng_intro_fragment_about(
				lt_phone_number,
				rpl::single(TextWithEntities{
					.text = Ui::FormatPhone(getData()->phone)
				}),
				Ui::Text::RichLangValue)
			: (byTelegram ? tr::lng_code_from_telegram : tr::lng_code_desc)(
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
					(u"%1:%2"_q
					).arg(_callTimeout / 3600
					).arg((_callTimeout / 60) % 60, 2, 10, QChar('0')),
					lt_seconds,
					u"%1"_q.arg(_callTimeout % 60, 2, 10, QChar('0')));
			} else {
				return tr::lng_code_call(
					tr::now,
					lt_minutes,
					QString::number(_callTimeout / 60),
					lt_seconds,
					u"%1"_q.arg(_callTimeout % 60, 2, 10, QChar('0')));
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
	_code->setFocus();
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
	account().setHandleLoginCode(nullptr);
	_checkRequestTimer.cancel();
	_callTimer.cancel();
	apiClear();

	cancelled();
	_sentCode.clear();
	_code->clear();
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
	_code->setEnabled(true);
	_sentRequest = 0;
	finish(result);
}

void CodeWidget::codeSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		stopCheck();
		_code->setEnabled(true);
		_code->setFocus();
		_sentRequest = 0;
		showCodeError(tr::lng_flood_error());
		return;
	}

	stopCheck();
	_code->setEnabled(true);
	_code->setFocus();
	_sentRequest = 0;
	auto &err = error.type();
	if (err == u"PHONE_NUMBER_INVALID"_q
		|| err == u"PHONE_CODE_EXPIRED"_q
		|| err == u"PHONE_NUMBER_BANNED"_q) { // show error
		goBack();
	} else if (err == u"PHONE_CODE_EMPTY"_q || err == u"PHONE_CODE_INVALID"_q) {
		showCodeError(tr::lng_bad_code());
	} else if (err == u"SESSION_PASSWORD_NEEDED"_q) {
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

void CodeWidget::sendCall() {
	if (_callStatus == CallStatus::Waiting) {
		if (--_callTimeout <= 0) {
			_callStatus = CallStatus::Calling;
			_callTimer.cancel();
			_callRequestId = api().request(MTPauth_ResendCode(
				MTP_flags(0),
				MTP_string(getData()->phone),
				MTP_bytes(getData()->phoneHash),
				MTPstring() // reason
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

void CodeWidget::callDone(const MTPauth_SentCode &result) {
	result.match([&](const MTPDauth_sentCode &data) {
		fillSentCodeData(data);
		_code->setDigitsCountMax(getData()->codeLength);
		if (_callStatus == CallStatus::Calling) {
			_callStatus = CallStatus::Called;
			getData()->callStatus = _callStatus;
			getData()->callTimeout = _callTimeout;
			updateCallText();
		}
	}, [&](const MTPDauth_sentCodeSuccess &data) {
		finish(data.vauthorization());
	});
}

void CodeWidget::gotPassword(const MTPaccount_Password &result) {
	Expects(result.type() == mtpc_account_password);

	stopCheck();
	_sentRequest = 0;
	const auto &d = result.c_account_password();
	getData()->pwdState = Core::ParseCloudPasswordState(d);
	if (!d.vcurrent_algo() || !d.vsrp_id() || !d.vsrp_B()) {
		LOG(("API Error: No current password received on login."));
		_code->setFocus();
		return;
	} else if (!getData()->pwdState.hasPassword) {
		const auto callback = [=](Fn<void()> &&close) {
			Core::UpdateApplication();
			close();
		};
		Ui::show(Ui::MakeConfirmBox({
			.text = tr::lng_passport_app_out_of_date(),
			.confirmed = callback,
			.confirmText = tr::lng_menu_update(),
		}));
		return;
	}
	goReplace<PasswordCheckWidget>(Animate::Forward);
}

void CodeWidget::submit() {
	if (getData()->codeByFragmentUrl.isEmpty()) {
		_code->requestCode();
	} else {
		File::OpenUrl(getData()->codeByFragmentUrl);
	}
}

void CodeWidget::submitCode(const QString &text) {
	if (_sentRequest
		|| _sentCode == text
		|| text.size() != getData()->codeLength) {
		return;
	}

	hideError();

	_checkRequestTimer.callEach(1000);

	_sentCode = text;
	_code->setEnabled(false);
	getData()->pwdState = Core::CloudPasswordState();
	_sentRequest = api().request(MTPauth_SignIn(
		MTP_flags(MTPauth_SignIn::Flag::f_phone_code),
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash),
		MTP_string(_sentCode),
		MTPEmailVerification()
	)).done([=](const MTPauth_Authorization &result) {
		codeSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		codeSubmitFail(error);
	}).handleFloodErrors().send();
}

rpl::producer<QString> CodeWidget::nextButtonText() const {
	return _isFragment.value(
	) | rpl::map([=](bool isFragment) {
		return isFragment
			? tr::lng_intro_fragment_button()
			: Step::nextButtonText();
	}) | rpl::flatten_latest();
}

rpl::producer<const style::RoundButton*> CodeWidget::nextButtonStyle() const {
	return _isFragment.value(
	) | rpl::map([](bool isFragment) {
		return isFragment ? &st::introFragmentButton : nullptr;
	});
}

void CodeWidget::noTelegramCode() {
	if (_noTelegramCodeRequestId) {
		return;
	}
	_noTelegramCodeRequestId = api().request(MTPauth_ResendCode(
		MTP_flags(0),
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash),
		MTPstring() // reason
	)).done([=](const MTPauth_SentCode &result) {
		noTelegramCodeDone(result);
	}).fail([=](const MTP::Error &error) {
		noTelegramCodeFail(error);
	}).handleFloodErrors().send();
}

void CodeWidget::noTelegramCodeDone(const MTPauth_SentCode &result) {
	_noTelegramCodeRequestId = 0;

	result.match([&](const MTPDauth_sentCode &data) {
		const auto &d = result.c_auth_sentCode();
		fillSentCodeData(data);
		_code->setDigitsCountMax(getData()->codeLength);
		const auto next = data.vnext_type();
		if (next && next->type() == mtpc_auth_codeTypeCall) {
			getData()->callStatus = CallStatus::Waiting;
			getData()->callTimeout = d.vtimeout().value_or(60);
		} else {
			getData()->callStatus = CallStatus::Disabled;
			getData()->callTimeout = 0;
		}
		getData()->codeByTelegram = false;
		updateDescText();
	}, [&](const MTPDauth_sentCodeSuccess &data) {
		finish(data.vauthorization());
	});
}

void CodeWidget::noTelegramCodeFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		_noTelegramCodeRequestId = 0;
		showCodeError(tr::lng_flood_error());
		return;
	} else if (error.type() == u"SEND_CODE_UNAVAILABLE"_q) {
		_noTelegramCodeRequestId = 0;
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
