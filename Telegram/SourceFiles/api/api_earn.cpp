/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_earn.h"

#include "api/api_cloud_password.h"
#include "apiwrap.h"
#include "ui/layers/generic_box.h"
#include "boxes/passcode_box.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/basic_click_handlers.h"
#include "ui/widgets/buttons.h"

namespace Api {

void RestrictSponsored(
		not_null<ChannelData*> channel,
		bool restricted,
		Fn<void(QString)> failed) {
	channel->session().api().request(MTPchannels_RestrictSponsoredMessages(
		channel->inputChannel,
		MTP_bool(restricted))
	).done([=](const MTPUpdates &updates) {
		channel->session().api().applyUpdates(updates);
	}).fail([=](const MTP::Error &error) {
		failed(error.type());
	}).send();
}

void HandleWithdrawalButton(
		RewardReceiver receiver,
		not_null<Ui::RippleButton*> button,
		std::shared_ptr<Ui::Show> show) {
	Expects(receiver.currencyReceiver
		|| (receiver.creditsReceiver && receiver.creditsAmount));

	struct State {
		rpl::lifetime lifetime;
		bool loading = false;
	};

	const auto currencyReceiver = receiver.currencyReceiver;
	const auto creditsReceiver = receiver.creditsReceiver;
	const auto isChannel = receiver.currencyReceiver
		&& receiver.currencyReceiver->isChannel();

	const auto state = button->lifetime().make_state<State>();
	const auto session = (currencyReceiver
		? &currencyReceiver->session()
		: &creditsReceiver->session());

	using CreditsOutUrl = MTPpayments_StarsRevenueWithdrawalUrl;

	session->api().cloudPassword().reload();
	const auto processOut = [=] {
		if (state->loading) {
			return;
		} else if (creditsReceiver && !receiver.creditsAmount()) {
			return;
		}
		state->loading = true;
		state->lifetime = session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Core::CloudPasswordState &pass) {
			state->loading = false;

			auto fields = PasscodeBox::CloudFields::From(pass);
			fields.customTitle = isChannel
				? tr::lng_channel_earn_balance_password_title()
				: tr::lng_bot_earn_balance_password_title();
			fields.customDescription = isChannel
				? tr::lng_channel_earn_balance_password_description(tr::now)
				: tr::lng_bot_earn_balance_password_description(tr::now);
			fields.customSubmitButton = tr::lng_passcode_submit();
			fields.customCheckCallback = crl::guard(button, [=](
					const Core::CloudPasswordResult &result,
					base::weak_qptr<PasscodeBox> box) {
				const auto done = [=](const QString &result) {
					if (!result.isEmpty()) {
						UrlClickHandler::Open(result);
						if (box) {
							box->closeBox();
						}
					}
				};
				const auto fail = [=](const MTP::Error &error) {
					const auto message = error.type();
					if (box && !box->handleCustomCheckError(message)) {
						show->showToast(message);
					}
				};
				if (currencyReceiver || creditsReceiver) {
					using F = MTPpayments_getStarsRevenueWithdrawalUrl::Flag;
					session->api().request(
						MTPpayments_GetStarsRevenueWithdrawalUrl(
							MTP_flags(currencyReceiver
								? F::f_ton
								: F::f_amount),
							currencyReceiver
								? currencyReceiver->input
								: creditsReceiver->input,
							MTP_long(creditsReceiver
								? receiver.creditsAmount()
								: 0),
							result.result
					)).done([=](const CreditsOutUrl &r) {
						done(qs(r.data().vurl()));
					}).fail(fail).send();
				}
			});
			show->show(Box<PasscodeBox>(session, fields));
		});
	};
	button->setClickedCallback([=] {
		if (state->loading) {
			return;
		}
		const auto fail = [=](const MTP::Error &error) {
			auto box = PrePasswordErrorBox(
				error.type(),
				session,
				TextWithEntities{
					tr::lng_channel_earn_out_check_password_about(tr::now),
				});
			if (box) {
				show->show(std::move(box));
				state->loading = false;
			} else {
				processOut();
			}
		};
		if (currencyReceiver || creditsReceiver) {
			using F = MTPpayments_getStarsRevenueWithdrawalUrl::Flag;
			session->api().request(
				MTPpayments_GetStarsRevenueWithdrawalUrl(
					MTP_flags(currencyReceiver
						? F::f_ton
						: F::f_amount),
					currencyReceiver
						? currencyReceiver->input
						: creditsReceiver->input,
					MTP_long(creditsReceiver
						? receiver.creditsAmount()
						: 0),
					MTP_inputCheckPasswordEmpty()
			)).fail(fail).send();
		}
	});
}

} // namespace Api
