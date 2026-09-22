/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_request_box.h"

#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "ui/controls/button_busy.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_ton_connect.h"

#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

using Phase = TonConnectRequestPhase;

constexpr auto kPayloadShown = 1024;

struct State {
	std::optional<TonConnectRequestBoxState> built;
	QPointer<Ui::RoundButton> confirm;
	QPointer<Ui::RoundButton> decline;
	QPointer<Ui::RoundButton> unlock;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> error;
};

[[nodiscard]] bool SameStructure(
		const TonConnectRequestBoxState &built,
		const TonConnectRequestBoxState &now) {
	auto aligned = now;
	aligned.error = built.error;
	aligned.busy = built.busy;
	aligned.declining = built.declining;
	return (aligned == built);
}

[[nodiscard]] bool Busy(not_null<State*> state) {
	return state->built && state->built->busy;
}

[[nodiscard]] bool Confirmable(not_null<State*> state) {
	return state->built && state->built->confirmable;
}

[[nodiscard]] QString PayloadShown(const QString &payload) {
	return (payload.size() <= kPayloadShown)
		? payload
		: (payload.left(kPayloadShown) + Ui::kQEllipsis);
}

not_null<Ui::FlatLabel*> AddLabel(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> text,
		const style::FlatLabel &st) {
	return content->add(
		object_ptr<Ui::FlatLabel>(content, std::move(text), st),
		st::boxRowPadding);
}

void AddCaption(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> text) {
	AddLabel(content, std::move(text), st::walletCommentCaptionLabel);
}

void AddFiat(
		not_null<Ui::VerticalLayout*> content,
		not_null<Main::Session*> session,
		int64 nano) {
	auto text = FiatRateValue(
		session
	) | rpl::map([=](const FiatRate &rate) {
		return rate.available() ? FormatFiat(nano, rate) : QString();
	});
	auto shown = rpl::duplicate(
		text
	) | rpl::map([](const QString &value) {
		return !value.isEmpty();
	});
	content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(text),
				st::walletCommentCaptionLabel),
			st::boxRowPadding)
	)->toggleOn(std::move(shown))->finishAnimating();
}

void AddApp(
		not_null<Ui::VerticalLayout*> content,
		const TonConnectRequestBoxState &now) {
	const auto named = !now.name.isEmpty() && (now.name != now.domain);
	const auto hosted = !now.domain.isEmpty();
	if (named) {
		AddLabel(content, rpl::single(now.name), st::boxLabel);
	}
	if (hosted) {
		AddCaption(content, rpl::single(now.domain));
	}
	if (named || hosted) {
		Ui::AddSkip(content);
	}
}

void AddMessage(
		not_null<Ui::VerticalLayout*> content,
		int index,
		const TonConnectMessage &message) {
	Ui::AddSkip(content, st::walletTonConnectMessageSkip);
	AddCaption(
		content,
		tr::lng_wallet_connect_request_message(
			lt_index,
			rpl::single(QString::number(index + 1))));
	Ui::AddSkip(content);
	AddCaption(content, tr::lng_wallet_details_recipient());
	AddLabel(
		content,
		rpl::single(message.destination),
		st::walletDetailsAddressLabel)->setSelectable(true);
	Ui::AddSkip(content);
	AddCaption(content, tr::lng_wallet_connect_request_amount());
	AddLabel(
		content,
		tr::lng_wallet_send_pill_gram(
			lt_amount,
			rpl::single(Ui::FormatTonAmount(message.amountNano).full)),
		st::boxLabel);
	if (!message.comment.isEmpty()) {
		Ui::AddSkip(content);
		AddCaption(content, tr::lng_wallet_connect_request_comment());
		AddLabel(content, rpl::single(message.comment), st::boxLabel);
	} else if (!message.payload.isEmpty()) {
		Ui::AddSkip(content);
		AddCaption(content, tr::lng_wallet_connect_request_payload());
		AddLabel(
			content,
			rpl::single(PayloadShown(message.payload)),
			st::walletTonConnectPayloadLabel);
	}
	if (message.deploys) {
		Ui::AddSkip(content);
		AddCaption(content, tr::lng_wallet_connect_request_deploy());
	}
}

void Update(
		not_null<State*> state,
		const TonConnectRequestBoxState &now,
		anim::type animated) {
	const auto confirming = now.busy && !now.declining;
	Ui::SetButtonBusy(state->confirm.data(), confirming);
	Ui::SetButtonDimmed(
		state->confirm.data(),
		confirming || !now.confirmable);
	Ui::SetButtonBusy(state->decline.data(), now.busy && now.declining);
	Ui::SetButtonBusy(state->unlock.data(), now.busy);
	if (const auto error = state->error.data()) {
		if (!now.error.isEmpty()) {
			error->entity()->setText(now.error);
		}
		error->toggle(!now.error.isEmpty(), animated);
	}
}

void FillLoading(not_null<Ui::GenericBox*> box) {
	const auto content = box->verticalLayout();
	AddLabel(content, tr::lng_wallet_connect_request_loading(), st::boxLabel);
	const auto &loading = st::walletBusyBoxLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto row = content->add(
		object_ptr<Ui::FixedHeightWidget>(content, side),
		st::walletBusyBoxPadding);
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		row,
		side,
		&loading);
	Info::Statistics::AddChildToWidgetCenter(row, indicator);
	indicator->show();
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void FillLocked(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const TonConnectRequestBoxState &now,
		Fn<void()> unlock) {
	const auto content = box->verticalLayout();
	AddApp(content, now);
	AddLabel(content, rpl::single(now.topic), st::boxLabel);
	Ui::AddSkip(content);
	AddCaption(content, tr::lng_wallet_connect_request_locked());
	Ui::AddSkip(content);
	state->unlock = box->addButton(
		tr::lng_wallet_connect_request_unlock(),
		[=] {
			if (!Busy(state)) {
				unlock();
			}
		});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	Update(state, now, anim::type::instant);
}

void FillConfirm(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		not_null<Main::Session*> session,
		const TonConnectRequestBoxState &now,
		Fn<void()> confirm,
		Fn<void()> decline) {
	const auto content = box->verticalLayout();
	AddApp(content, now);
	const auto total = now.transfer ? now.transfer->totalNano : int64(0);
	AddCaption(content, tr::lng_wallet_connect_request_total());
	AddLabel(
		content,
		tr::lng_wallet_send_pill_gram(
			lt_amount,
			rpl::single(Ui::FormatTonAmount(total).full)),
		st::boxLabel);
	AddFiat(content, session, total);
	Ui::AddSkip(content);
	if (now.feeNano) {
		const auto fee = *now.feeNano;
		AddLabel(
			content,
			tr::lng_wallet_send_network_fee(
				lt_amount,
				rpl::single(Ui::FormatTonAmount(fee).full)),
			st::boxLabel);
		AddFiat(content, session, fee);
	} else if (now.feeLoading) {
		AddLabel(content, tr::lng_wallet_details_fee_loading(), st::boxLabel);
	} else {
		AddLabel(
			content,
			tr::lng_wallet_connect_request_fee_failed(),
			st::boxLabel);
	}
	if (now.transfer) {
		auto index = 0;
		for (const auto &message : now.transfer->messages) {
			AddMessage(content, index++, message);
		}
	}
	state->error = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(content, st::walletCommentErrorLabel),
			style::margins(0, st::defaultVerticalListSkip, 0, 0)),
		st::boxRowPadding);
	Ui::AddSkip(content);
	state->confirm = box->addButton(
		tr::lng_wallet_connect_request_confirm(),
		[=] {
			if (!Busy(state) && Confirmable(state)) {
				confirm();
			}
		});
	state->decline = box->addButton(
		tr::lng_wallet_connect_request_decline(),
		[=] {
			if (!Busy(state)) {
				decline();
			}
		});
	Update(state, now, anim::type::instant);
}

void FillNotice(
		not_null<Ui::GenericBox*> box,
		const TonConnectRequestBoxState &now) {
	const auto content = box->verticalLayout();
	AddApp(content, now);
	AddLabel(content, rpl::single(now.notice), st::boxLabel);
	Ui::AddSkip(content);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void Rebuild(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		not_null<Main::Session*> session,
		const TonConnectRequestBoxState &now,
		Fn<void()> unlock,
		Fn<void()> confirm,
		Fn<void()> decline) {
	state->confirm = nullptr;
	state->decline = nullptr;
	state->unlock = nullptr;
	state->error = nullptr;
	box->verticalLayout()->clear();
	box->clearButtons();
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
	if (now.phase == Phase::Loading) {
		FillLoading(box);
	} else if (now.phase == Phase::Locked) {
		FillLocked(box, state, now, std::move(unlock));
	} else if (now.phase == Phase::Notice) {
		FillNotice(box, now);
	} else {
		FillConfirm(
			box,
			state,
			session,
			now,
			std::move(confirm),
			std::move(decline));
	}
}

} // namespace

void TonConnectRequestBox(
		not_null<Ui::GenericBox*> box,
		TonConnectRequestBoxArgs args) {
	box->setTitle(tr::lng_wallet_connect_request_title());
	box->setWidth(st::boxWidth);
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto state = box->lifetime().make_state<State>();
	const auto reported = std::make_shared<bool>(false);
	const auto dismiss = [=, dismissed = args.dismissed] {
		if (!std::exchange(*reported, true) && dismissed) {
			dismissed();
		}
	};
	box->boxClosing() | rpl::on_next(dismiss, box->lifetime());
	box->lifetime().add(dismiss);

	const auto session = args.session;
	const auto unlock = args.unlock;
	const auto confirm = args.confirm;
	const auto decline = args.decline;
	std::move(
		args.state
	) | rpl::on_next([=](const TonConnectRequestBoxState &now) {
		if (state->built && SameStructure(*state->built, now)) {
			Update(state, now, anim::type::normal);
		} else {
			Rebuild(box, state, session, now, unlock, confirm, decline);
		}
		state->built = now;
	}, box->lifetime());
}

} // namespace Wallet
