/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_earn.h"

#include "api/api_cloud_password.h"
#include "apiwrap.h"
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
		not_null<ChannelData*> channel,
		not_null<Ui::RippleButton*> button,
		std::shared_ptr<Ui::Show> show) {
	struct State {
		rpl::lifetime lifetime;
		bool loading = false;
	};

	const auto state = button->lifetime().make_state<State>();
	const auto session = &channel->session();

	session->api().cloudPassword().reload();
	button->setClickedCallback([=] {
		if (state->loading) {
			return;
		}
		state->loading = true;
		state->lifetime = session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Core::CloudPasswordState &pass) {
			state->loading = false;

			auto fields = PasscodeBox::CloudFields::From(pass);
			fields.customTitle
				= tr::lng_channel_earn_balance_password_title();
			fields.customDescription
				= tr::lng_channel_earn_balance_password_description(tr::now);
			fields.customSubmitButton = tr::lng_passcode_submit();
			fields.customCheckCallback = crl::guard(button, [=](
					const Core::CloudPasswordResult &result,
					QPointer<PasscodeBox> box) {
				const auto done = [=](const QString &result) {
					if (!result.isEmpty()) {
						UrlClickHandler::Open(result);
						if (box) {
							box->closeBox();
						}
					}
				};
				const auto fail = [=](const QString &error) {
					show->showToast(error);
				};
				session->api().request(
					MTPstats_GetBroadcastRevenueWithdrawalUrl(
						channel->inputChannel,
						result.result
				)).done([=](const MTPstats_BroadcastRevenueWithdrawalUrl &r) {
					done(qs(r.data().vurl()));
				}).fail([=](const MTP::Error &error) {
					fail(error.type());
				}).send();
			});
			show->show(Box<PasscodeBox>(session, fields));
		});

	});
}

} // namespace Api
