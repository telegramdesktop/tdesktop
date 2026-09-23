/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_request_box.h"

#include "lang/lang_keys.h"
#include "ui/controls/button_busy.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_box.h"

#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

using Phase = TonConnectRequestPhase;

constexpr auto kPayloadShown = 1024;
constexpr auto kMinus = QChar(0x2212);

struct State {
	std::optional<TonConnectRequestBoxState> built;
	Fn<void(const TonConnectHeaderState &, anim::type)> header;
	rpl::variable<std::optional<int64>> feeNano;
	QPointer<Ui::VerticalLayout> body;
	QPointer<Ui::IconButton> back;
	QPointer<Ui::RoundButton> confirm;
	QPointer<Ui::RoundButton> decline;
	QPointer<Ui::RoundButton> unlock;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> fee;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> error;
	bool details = false;
};

struct Context {
	not_null<Main::Session*> session;
	Fn<void()> unlock;
	Fn<void()> confirm;
	Fn<void()> decline;
};

struct Destinations {
	QString first;
	int distinct = 0;
};

[[nodiscard]] bool SameStructure(
		const TonConnectRequestBoxState &built,
		const TonConnectRequestBoxState &now) {
	auto aligned = now;
	aligned.feeNano = built.feeNano;
	aligned.error = built.error;
	aligned.feeLoading = built.feeLoading;
	aligned.confirmable = built.confirmable;
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

[[nodiscard]] QString DisplayAddress(const QString &destination) {
	const auto parsed = ParseAddress(destination);
	if (!parsed || parsed->friendly) {
		return destination;
	}
	const auto friendly = FormatFriendly(
		parsed->raw,
		false,
		parsed->testnet);
	return friendly.isEmpty() ? destination : friendly;
}

[[nodiscard]] Destinations CollectDestinations(
		const TonConnectTransfer &transfer) {
	auto keys = base::flat_set<QString>();
	for (const auto &message : transfer.messages) {
		const auto canonical = CanonicalAddress(message.destination);
		keys.emplace(canonical.isEmpty() ? message.destination : canonical);
	}
	return {
		.first = (transfer.messages.empty()
			? QString()
			: DisplayAddress(transfer.messages.front().destination)),
		.distinct = int(keys.size()),
	};
}

[[nodiscard]] QString OutgoingGram(int64 nano) {
	return kMinus + tr::lng_wallet_send_pill_gram(
		tr::now,
		lt_amount,
		Ui::FormatTonAmount(nano).full);
}

[[nodiscard]] QString HeaderTitle(const TonConnectRequestBoxState &now) {
	switch (now.phase) {
	case Phase::Loading:
		return tr::lng_wallet_connect_request_loading(tr::now);
	case Phase::Locked:
		return now.topic;
	case Phase::Confirm:
		return now.name.isEmpty()
			? tr::lng_wallet_connect_request_title(tr::now)
			: tr::lng_wallet_connect_request_title_app(
				tr::now,
				lt_name,
				now.name);
	case Phase::Notice:
		return now.name.isEmpty()
			? tr::lng_wallet_connect_request_title(tr::now)
			: now.name;
	}
	Unexpected("Phase in TonConnectRequestBox HeaderTitle.");
}

[[nodiscard]] TonConnectHeaderState HeaderStateFor(
		const TonConnectRequestBoxState &now) {
	return {
		.title = HeaderTitle(now),
		.domain = now.domain,
		.icon = now.icon,
		.loading = (now.phase == Phase::Loading),
	};
}

[[nodiscard]] rpl::producer<QString> FeeText(
		not_null<State*> state,
		not_null<Main::Session*> session) {
	return rpl::combine(
		state->feeNano.value(),
		FiatRateValue(session)
	) | rpl::map([](std::optional<int64> fee, const FiatRate &rate) {
		return fee
			? tr::lng_wallet_connect_request_fee(
				tr::now,
				lt_amount,
				Ui::FormatTonAmount(*fee).full,
				lt_fiat,
				FormatFiat(*fee, rate, kFeeFiatDecimals, true))
			: tr::lng_wallet_connect_request_fee_loading(tr::now);
	});
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeTotalValue(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		int64 total) {
	auto text = FiatRateValue(
		session
	) | rpl::map([=](const FiatRate &rate) {
		auto result = tr::marked(OutgoingGram(total));
		result.append(QChar(' '));
		result.append(Ui::Text::Colorized(
			FormatFiat(total, rate, kFiatCurrencyDecimals, true)));
		return result;
	});
	return object_ptr<Ui::FlatLabel>(
		parent,
		std::move(text),
		st::walletTonConnectAmountLabel);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeMessageValue(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const TonConnectMessage &message) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto cell = result.data();
	cell->add(object_ptr<Ui::FlatLabel>(
		cell,
		OutgoingGram(message.amountNano),
		st::walletTonConnectAmountLabel));
	Ui::AddSkip(cell, st::walletRowSkip);
	cell->add(AddressValueLabel(
		cell,
		std::move(show),
		DisplayAddress(message.destination)));
	if (!message.comment.isEmpty()) {
		Ui::AddSkip(cell, st::walletRowSkip);
		cell->add(MakeCommentBubble(
			cell,
			object_ptr<Ui::FlatLabel>(
				cell,
				message.comment,
				st::walletCommentLabel),
			st::windowBgOver));
	} else if (!message.payload.isEmpty()) {
		Ui::AddSkip(cell, st::walletRowSkip);
		cell->add(object_ptr<Ui::FlatLabel>(
			cell,
			tr::lng_wallet_connect_request_payload(),
			st::walletTonConnectNoteLabel));
		Ui::AddSkip(cell, st::walletRowSkip);
		auto payload = object_ptr<Ui::FlatLabel>(
			cell,
			rpl::single(Ui::Text::Wrapped(
				tr::marked(PayloadShown(message.payload)),
				EntityType::Code)),
			st::walletTonConnectPayloadLabel);
		payload->setSelectable(true);
		cell->add(MakeCommentBubble(
			cell,
			std::move(payload),
			st::windowBgOver));
	}
	if (message.deploys) {
		Ui::AddSkip(cell, st::walletRowSkip);
		cell->add(object_ptr<Ui::FlatLabel>(
			cell,
			tr::lng_wallet_connect_request_deploy(),
			st::walletTonConnectNoteLabel));
	}
	return result;
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
	state->feeNano = now.feeNano;
	if (const auto fee = state->fee.data()) {
		fee->toggle(now.feeNano || now.feeLoading, animated);
	}
	if (const auto error = state->error.data()) {
		if (!now.error.isEmpty()) {
			error->entity()->setText(now.error);
		}
		error->toggle(!now.error.isEmpty(), animated);
	}
}

void FillLoading(not_null<Ui::GenericBox*> box, not_null<State*> state) {
	const auto cancel = AddTonConnectButtons(
		state->body.data(),
		tr::lng_cancel(),
		nullptr).secondary;
	cancel->setClickedCallback([=] { box->closeBox(); });
}

void FillLocked(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context) {
	const auto body = state->body.data();
	body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			tr::lng_wallet_connect_request_locked(),
			st::walletConnectTextLabel),
		st::walletConnectTextMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	const auto buttons = AddTonConnectButtons(
		body,
		tr::lng_cancel(),
		tr::lng_wallet_connect_request_unlock());
	buttons.secondary->setClickedCallback([=] { box->closeBox(); });
	buttons.primary->setClickedCallback([=, unlock = context.unlock] {
		if (!Busy(state)) {
			unlock();
		}
	});
	state->unlock = buttons.primary;
}

void FillNotice(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const TonConnectRequestBoxState &now) {
	const auto body = state->body.data();
	body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			now.notice,
			st::walletConnectTextLabel),
		st::walletConnectTextMargin,
		style::al_top);
	const auto close = AddTonConnectButtons(
		body,
		nullptr,
		tr::lng_close()).primary;
	close->setClickedCallback([=] { box->closeBox(); });
}

void AddConfirmTail(not_null<State*> state, const Context &context) {
	const auto body = state->body.data();
	state->fee = body->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			body,
			object_ptr<Ui::FlatLabel>(
				body,
				FeeText(state, context.session),
				st::walletConnectCaptionLabel),
			st::walletConnectCaptionMargin),
		style::margins(),
		style::al_top);
	state->fee->entity()->setTryMakeSimilarLines(true);
	state->error = body->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			body,
			object_ptr<Ui::FlatLabel>(body, st::walletConnectErrorLabel),
			style::margins(0, st::defaultVerticalListSkip, 0, 0)),
		st::boxRowPadding,
		style::al_top);
	const auto buttons = AddTonConnectButtons(
		body,
		tr::lng_wallet_connect_request_decline(),
		tr::lng_wallet_connect_request_confirm());
	buttons.secondary->setClickedCallback([=, decline = context.decline] {
		if (!Busy(state)) {
			decline();
		}
	});
	buttons.primary->setClickedCallback([=, confirm = context.confirm] {
		if (!Busy(state) && Confirmable(state)) {
			confirm();
		}
	});
	state->decline = buttons.secondary;
	state->confirm = buttons.primary;
}

void SetDetails(
	not_null<Ui::GenericBox*> box,
	not_null<State*> state,
	const Context &context,
	bool details);

void FillSheet(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
	const auto body = state->body.data();
	const auto destinations = now.transfer
		? CollectDestinations(*now.transfer)
		: Destinations();
	body->add(
		MakeTransferCard(body, context.session, {
			.totalNano = now.transfer ? now.transfer->totalNano : int64(0),
			.destination = ((destinations.distinct == 1)
				? destinations.first
				: QString()),
			.recipients = ((destinations.distinct >= 2)
				? destinations.distinct
				: 0),
			.info = [=] {
				// The rebuild destroys the card this click is still inside.
				Ui::PostponeCall(box, [=] {
					SetDetails(box, state, context, true);
				});
			},
		}),
		st::walletConnectCardMargin,
		style::al_top);
	AddConfirmTail(state, context);
}

void FillDetails(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
	const auto body = state->body.data();
	if (const auto transfer = now.transfer.get()) {
		const auto show = box->uiShow();
		const auto padding = style::margins(
			(st::boxRowPadding.left()
				- st::defaultSubsectionTitlePadding.left()),
			0,
			0,
			0);
		Ui::AddSubsectionTitle(
			body,
			tr::lng_wallet_connect_request_preview(),
			padding);
		const auto preview = AddDetailsTableFrame(body);
		Ui::AddTableRow(
			preview,
			tr::lng_wallet_connect_request_total(),
			MakeTotalValue(preview, context.session, transfer->totalNano));
		const auto destinations = CollectDestinations(*transfer);
		if (destinations.distinct == 1) {
			Ui::AddTableRow(
				preview,
				tr::lng_wallet_details_recipient(),
				AddressValueLabel(preview, show, destinations.first));
		} else if (destinations.distinct >= 2) {
			Ui::AddTableRow(
				preview,
				tr::lng_wallet_connect_request_recipients_title(),
				rpl::single(tr::marked(
					QString::number(destinations.distinct))));
		}
		Ui::AddSubsectionTitle(
			body,
			tr::lng_wallet_connect_request_actions(),
			padding);
		const auto list = AddDetailsTableFrame(body);
		for (const auto &message : transfer->messages) {
			Ui::AddTableRow(
				list,
				tr::lng_wallet_connect_request_transfer(),
				MakeMessageValue(list, show, message));
		}
		Ui::AddSkip(
			body,
			(st::walletConnectCardMargin.bottom()
				- st::giveawayGiftCodeTableMargin.bottom()));
	}
	AddConfirmTail(state, context);
}

void ToggleBack(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context) {
	if (state->details && !state->back) {
		const auto back = Ui::CreateChild<Ui::IconButton>(
			box.get(),
			st::walletConnectBack);
		back->setClickedCallback([=] {
			Ui::PostponeCall(box, [=] {
				SetDetails(box, state, context, false);
			});
		});
		back->moveToLeft(0, 0);
		back->raise();
		state->back = back;
	}
	if (const auto back = state->back.data()) {
		back->setVisible(state->details);
	}
}

void Rebuild(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
	state->confirm = nullptr;
	state->decline = nullptr;
	state->unlock = nullptr;
	state->fee = nullptr;
	state->error = nullptr;
	state->body->clear();
	if (now.phase != Phase::Confirm) {
		state->details = false;
	}
	switch (now.phase) {
	case Phase::Loading: FillLoading(box, state); break;
	case Phase::Locked: FillLocked(box, state, context); break;
	case Phase::Notice: FillNotice(box, state, now); break;
	case Phase::Confirm:
		if (state->details) {
			FillDetails(box, state, context, now);
		} else {
			FillSheet(box, state, context, now);
		}
		break;
	}
	ToggleBack(box, state, context);
	Update(state, now, anim::type::instant);
}

// WHY: the details view is a page of the same box, not a box over it, so
// the flow's close, expiry and dismissal reach it with no wiring of their
// own, and both pages build Decline / Confirm in AddConfirmTail.
void SetDetails(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		bool details) {
	if (!state->built
		|| state->built->phase != Phase::Confirm
		|| state->details == details) {
		return;
	}
	state->details = details;
	Rebuild(box, state, context, *state->built);
	box->scrollToY(0);
}

} // namespace

void TonConnectRequestBox(
		not_null<Ui::GenericBox*> box,
		TonConnectRequestBoxArgs args) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletConnectBox);
	box->setNoContentMargin(true);
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

	state->header = AddTonConnectHeader(box->verticalLayout(), args.session);
	state->body = box->verticalLayout()->add(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);

	const auto context = Context{
		.session = args.session,
		.unlock = args.unlock,
		.confirm = args.confirm,
		.decline = args.decline,
	};
	std::move(
		args.state
	) | rpl::on_next([=](const TonConnectRequestBoxState &now) {
		const auto first = !state->built;
		state->header(
			HeaderStateFor(now),
			first ? anim::type::instant : anim::type::normal);
		if (!first && SameStructure(*state->built, now)) {
			Update(state, now, anim::type::normal);
		} else {
			Rebuild(box, state, context, now);
		}
		state->built = now;
	}, box->lifetime());
}

} // namespace Wallet
