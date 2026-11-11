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
#include "boxes/transfer_gift_box.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item_helpers.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/basic_click_handlers.h"
#include "wallet/wallet_panel.h"
#include "window/window_session_controller.h"

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"

namespace Api {
namespace {

constexpr auto kWithdrawnToastDuration = 5 * crl::time(1000);

[[nodiscard]] TextWithEntities MoneyMenuText(const QString &text) {
	return Ui::Text::IconEmoji(
		&st::giftMoveToWalletMoneyEmoji
	).append(' ').append(text);
}

[[nodiscard]] Fn<void()> PrepareFragmentWithdrawal(
		RewardReceiver receiver,
		not_null<Ui::RpWidget*> owner,
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

	const auto state = owner->lifetime().make_state<State>();
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
		) | rpl::on_next([=](const Core::CloudPasswordState &pass) {
			state->loading = false;

			auto fields = PasscodeBox::CloudFields::From(pass);
			fields.customTitle = isChannel
				? tr::lng_channel_earn_balance_password_title()
				: tr::lng_bot_earn_balance_password_title();
			fields.customDescription = isChannel
				? tr::lng_channel_earn_balance_password_description(tr::now)
				: tr::lng_bot_earn_balance_password_description(tr::now);
			fields.customSubmitButton = tr::lng_passcode_submit();
			fields.customCheckCallback = crl::guard(owner, [=](
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
					if (message == u"AMOUNT_TOO_SMALL"_q) {
						if (box) {
							box->closeBox();
						}
						show->showToast(
							tr::lng_channel_earn_out_amount_too_small(
								tr::now));
					} else if (box && !box->handleCustomCheckError(message)) {
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
								? currencyReceiver->input()
								: creditsReceiver->input(),
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
	return [=] {
		if (state->loading) {
			return;
		}
		const auto fail = crl::guard(owner, [=](const MTP::Error &error) {
			if (error.type() == u"AMOUNT_TOO_SMALL"_q) {
				show->showToast(tr::lng_channel_earn_out_amount_too_small(
					tr::now));
				return;
			}
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
		});
		if (currencyReceiver || creditsReceiver) {
			using F = MTPpayments_getStarsRevenueWithdrawalUrl::Flag;
			session->api().request(
				MTPpayments_GetStarsRevenueWithdrawalUrl(
					MTP_flags(currencyReceiver
						? F::f_ton
						: F::f_amount),
					currencyReceiver
						? currencyReceiver->input()
						: creditsReceiver->input(),
					MTP_long(creditsReceiver
						? receiver.creditsAmount()
						: 0),
					MTP_inputCheckPasswordEmpty()
			)).fail(fail).send();
		}
	};
}

void ShowWithdrawnToast(
		std::shared_ptr<Ui::Show> show,
		base::weak_ptr<Main::Session> session,
		int64 nanos) {
	ShowToastWithAction(
		std::move(show),
		Ui::Toast::Config{
			.title = tr::lng_credits_currency_withdraw_sent(
				tr::now,
				lt_amount,
				PrepareGramAmountText(nanos)),
			.text = tr::lng_credits_currency_withdraw_sent_path(
				tr::now,
				lt_menu,
				Ui::Text::IconEmoji(&st::giftMoveToWalletMenuEmoji),
				lt_arrow,
				Ui::Text::IconEmoji(&st::giftMoveToWalletArrowEmoji),
				lt_money,
				MoneyMenuText(tr::lng_wallet_menu(tr::now)),
				tr::marked),
			.icon = &st::toastCheckIcon,
			.duration = kWithdrawnToastDuration,
		},
		tr::lng_credits_currency_withdraw_open(),
		[=] {
			if (const auto strong = session.get()) {
				Wallet::ShowWallet(strong);
			}
		});
}

[[nodiscard]] bool WithdrawalDisabled(const MTP::Error &error) {
	const auto disabled = std::array{
		u"API_WITHDRAWAL_FEATURE_DISABLED"_q,
		u"FEATURE_DISABLED"_q,
	};
	return !MTP::IgnoreError(error)
		&& ranges::contains(disabled, error.type());
}

void ShowWithdrawToWalletError(
		const std::shared_ptr<Ui::Show> &show,
		const MTP::Error &error) {
	if (MTP::IgnoreError(error)) {
		return;
	}
	const auto &type = error.type();
	const auto tooSmall = std::array{
		u"AVAILABLE_BALANCE_TOO_SMALL"_q,
		u"AMOUNT_TOO_SMALL"_q,
	};
	if (ranges::contains(tooSmall, type)) {
		show->showToast(tr::lng_channel_earn_out_amount_too_small(tr::now));
	} else {
		LOG(("API Error: Withdraw to wallet failed with %1.").arg(type));
		show->showToast(tr::lng_credits_currency_withdraw_error(tr::now));
	}
}

void WithdrawToWalletFailed(
		std::shared_ptr<Ui::Show> show,
		const MTP::Error &error,
		Fn<void()> fragment) {
	if (WithdrawalDisabled(error)) {
		fragment();
	} else {
		ShowWithdrawToWalletError(show, error);
	}
}

void SendWithdrawToWallet(
		not_null<Main::Session*> session,
		const QByteArray &tmpPassword,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail) {
	using Flag = MTPpayments_withdrawStarsRevenueToWallet::Flag;
	session->api().request(MTPpayments_WithdrawStarsRevenueToWallet(
		MTP_flags(Flag::f_ton),
		MTP_inputPeerSelf(),
		MTP_long(0), // amount
		MTP_bytes(tmpPassword)
	)).done([=](const MTPBool &result) {
		if (mtpIsTrue(result)) {
			done();
		} else {
			fail(MTP::Error::Local(
				u"STARS_REVENUE_WITHDRAW_FALSE"_q,
				u"payments.withdrawStarsRevenueToWallet returned false"_q));
		}
	}).fail(fail).handleAllErrors().send();
}

void WithdrawToWalletBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		int64 nanos,
		Fn<void()> fragment,
		Fn<void()> withdrawn) {
	struct State {
		rpl::variable<bool> busy;
	};
	const auto state = std::make_shared<State>();
	const auto session = &window->session();
	const auto show = window->uiShow();
	const auto weakSession = base::make_weak(session);
	const auto weakBox = base::make_weak(box);

	box->setTitle(tr::lng_credits_currency_withdraw_title(
		lt_amount,
		rpl::single(PrepareGramAmountText(nanos))));
	const auto menu = Ui::Text::IconEmoji(&st::giftMoveToWalletMenuEmoji);
	const auto arrow = Ui::Text::IconEmoji(&st::giftMoveToWalletArrowEmoji);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_credits_currency_withdraw_text(
			lt_menu,
			rpl::single(menu),
			lt_arrow,
			rpl::single(arrow),
			lt_money,
			tr::lng_wallet_menu() | rpl::map(MoneyMenuText),
			tr::marked),
		st::boxLabel));

	const auto send = [=](
			const QByteArray &tmpPassword,
			Fn<void()> done,
			Fn<void(const MTP::Error &)> fail) {
		SendWithdrawToWallet(
			session,
			tmpPassword,
			std::move(done),
			std::move(fail));
	};
	const auto succeeded = [=] {
		if (const auto strong = weakBox.get()) {
			strong->closeBox();
		}
		withdrawn();
		if (show->valid()) {
			ShowWithdrawnToast(show, weakSession, nanos);
		}
	};
	const auto failed = [=](const MTP::Error &error) {
		if (const auto strong = weakBox.get()) {
			strong->closeBox();
		}
		WithdrawToWalletFailed(show, error, fragment);
	};
	const auto press = [=] {
		if (state->busy.current()) {
			return;
		}
		state->busy = true;
		RequestWithGiftPassword(window, box, {
			.send = send,
			.succeeded = succeeded,
			.failed = failed,
			.dismissed = [=] { state->busy = false; },
			.closeOnFail = true,
		});
	};

	const auto confirm = box->addButton(
		tr::lng_credits_currency_withdraw_button(),
		press).data();
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		confirm,
		confirm->height() / 2,
		&st::editStickerSetNameLoading);
	Info::Statistics::AddChildToWidgetCenter(confirm, loading);
	loading->showOn(state->busy.value());
	state->busy.value() | rpl::on_next([=](bool busy) {
		confirm->setTextFgOverride(busy
			? std::make_optional(QColor(Qt::transparent))
			: std::optional<QColor>());
	}, confirm->lifetime());
}

} // namespace

void RestrictSponsored(
		not_null<ChannelData*> channel,
		bool restricted,
		Fn<void(QString)> failed) {
	channel->session().api().request(MTPchannels_RestrictSponsoredMessages(
		channel->inputChannel(),
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
	button->setClickedCallback(PrepareFragmentWithdrawal(
		std::move(receiver),
		button,
		std::move(show)));
}

void HandleWalletWithdrawalButton(
		not_null<Window::SessionController*> window,
		not_null<Ui::RippleButton*> button,
		Fn<CreditsAmount()> available,
		Fn<void()> withdrawn) {
	const auto session = &window->session();
	const auto fragmentOnly = button->lifetime().make_state<bool>(false);
	const auto fragment = PrepareFragmentWithdrawal(
		{ .currencyReceiver = session->user() },
		button,
		window->uiShow());
	const auto fallback = crl::guard(button, [=] {
		*fragmentOnly = true;
		fragment();
	});
	const auto refresh = crl::guard(button, std::move(withdrawn));
	button->setClickedCallback([=] {
		if (*fragmentOnly || !session->appConfig().walletAvailable()) {
			fragment();
			return;
		}
		const auto amount = available();
		const auto nanos = amount.whole() * Ui::kNanosInOne + amount.nano();
		if (nanos <= 0) {
			return;
		}
		window->show(Box(
			WithdrawToWalletBox,
			window,
			nanos,
			fallback,
			refresh));
	});
}

void WithdrawEarningsToWallet(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		int64 nanos,
		WalletWithdrawal withdrawal) {
	Expects(withdrawal.withdrawn && withdrawal.disabled && withdrawal.failed);

	const auto session = &window->session();
	const auto show = window->uiShow();
	const auto weakSession = base::make_weak(session);
	const auto withdrawn = std::move(withdrawal.withdrawn);
	const auto disabled = std::move(withdrawal.disabled);
	const auto failed = std::move(withdrawal.failed);
	const auto send = [=](
			const QByteArray &tmpPassword,
			Fn<void()> done,
			Fn<void(const MTP::Error &)> fail) {
		const auto reported = [=](const MTP::Error &error) {
			if (WithdrawalDisabled(error)) {
				disabled();
			}
			fail(error);
		};
		SendWithdrawToWallet(session, tmpPassword, std::move(done), reported);
	};
	RequestWithGiftPassword(window, confirmation, {
		.send = send,
		.succeeded = [=] {
			withdrawn();
			if (show->valid()) {
				ShowWithdrawnToast(show, weakSession, nanos);
			}
		},
		.failed = [=](const MTP::Error &error) {
			if (!WithdrawalDisabled(error)) {
				ShowWithdrawToWalletError(show, error);
				failed();
			}
		},
		.dismissed = std::move(withdrawal.dismissed),
		.closeOnFail = true,
	});
}

} // namespace Api
