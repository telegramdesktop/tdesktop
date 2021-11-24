/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_confirm_phone.h"

#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/confirm_phone_box.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "window/window_session_controller.h"

namespace Api {

ConfirmPhone::ConfirmPhone(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

void ConfirmPhone::resolve(
		not_null<Window::SessionController*> controller,
		const QString &phone,
		const QString &hash) {
	if (_sendRequestId) {
		return;
	}
	_sendRequestId = _api.request(MTPaccount_SendConfirmPhoneCode(
		MTP_string(hash),
		MTP_codeSettings(MTP_flags(0), MTP_vector<MTPbytes>())
	)).done([=](const MTPauth_SentCode &result) {
		_sendRequestId = 0;

		result.match([&](const MTPDauth_sentCode &data) {
			const auto sentCodeLength = data.vtype().match([&](
					const MTPDauth_sentCodeTypeApp &data) {
				LOG(("Error: should not be in-app code!"));
				return 0;
			}, [&](const MTPDauth_sentCodeTypeSms &data) {
				return data.vlength().v;
			}, [&](const MTPDauth_sentCodeTypeCall &data) {
				return data.vlength().v;
			}, [&](const MTPDauth_sentCodeTypeFlashCall &data) {
				LOG(("Error: should not be flashcall!"));
				return 0;
			}, [&](const MTPDauth_sentCodeTypeMissedCall &data) {
				LOG(("Error: should not be missedcall!"));
				return 0;
			});
			const auto phoneHash = qs(data.vphone_code_hash());
			const auto timeout = [&]() -> std::optional<int> {
				if (const auto nextType = data.vnext_type()) {
					if (nextType->type() == mtpc_auth_codeTypeCall) {
						return data.vtimeout().value_or(60);
					}
				}
				return std::nullopt;
			}();
			auto box = Box<Ui::ConfirmPhoneBox>(
				phone,
				sentCodeLength,
				timeout);
			const auto boxWeak = Ui::MakeWeak(box.data());
			box->resendRequests(
			) | rpl::start_with_next([=] {
				_api.request(MTPauth_ResendCode(
					MTP_string(phone),
					MTP_string(phoneHash)
				)).done([=](const MTPauth_SentCode &result) {
					if (boxWeak) {
						boxWeak->callDone();
					}
				}).send();
			}, box->lifetime());
			box->checkRequests(
			) | rpl::start_with_next([=](const QString &code) {
				if (_checkRequestId) {
					return;
				}
				_checkRequestId = _api.request(MTPaccount_ConfirmPhone(
					MTP_string(phoneHash),
					MTP_string(code)
				)).done([=](const MTPBool &result) {
					_checkRequestId = 0;
					controller->show(
						Box<Ui::InformBox>(
							tr::lng_confirm_phone_success(
								tr::now,
								lt_phone,
								Ui::FormatPhone(phone))),
						Ui::LayerOption::CloseOther);
				}).fail([=](const MTP::Error &error) {
					_checkRequestId = 0;
					if (!boxWeak) {
						return;
					}

					const auto errorText = MTP::IsFloodError(error)
						? tr::lng_flood_error(tr::now)
						: (error.type() == (u"PHONE_CODE_EMPTY"_q)
							|| error.type() == (u"PHONE_CODE_INVALID"_q))
						? tr::lng_bad_code(tr::now)
						: Lang::Hard::ServerError();
					boxWeak->showServerError(errorText);
				}).handleFloodErrors().send();
			}, box->lifetime());

			controller->show(std::move(box), Ui::LayerOption::CloseOther);
		});
	}).fail([=](const MTP::Error &error) {
		_sendRequestId = 0;
		_checkRequestId = 0;

		const auto errorText = MTP::IsFloodError(error)
			? tr::lng_flood_error(tr::now)
			: (error.code() == 400)
			? tr::lng_confirm_phone_link_invalid(tr::now)
			: Lang::Hard::ServerError();
		controller->show(
			Box<Ui::InformBox>(errorText),
			Ui::LayerOption::CloseOther);
	}).handleFloodErrors().send();
}

} // namespace Api
