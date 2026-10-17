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
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/new_badges.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_amount_painter.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_box.h"
#include "wallet/wallet_ton_connect_emulation.h"

#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

using Phase = TonConnectRequestPhase;
using ActionKind = TonConnectActionKind;
using ActionSide = TonConnectActionSide;
using EmulationStatus = TonConnectEmulationStatus;
using SignDataType = TonConnectSignDataType;

constexpr auto kPayloadShown = 1024;
constexpr auto kFieldDepthShown = 8;
constexpr auto kMinus = QChar(0x2212);

struct State {
	std::optional<TonConnectRequestBoxState> built;
	Fn<void(const TonConnectHeaderState &, anim::type)> header;
	rpl::variable<std::optional<int64>> feeNano;
	QPointer<Ui::VerticalLayout> body;
	QPointer<Ui::IconButton> back;
	QPointer<Ui::RoundButton> confirm;
	QPointer<Ui::RoundButton> decline;
	QPointer<Ui::RoundButton> cancel;
	QPointer<Ui::RoundButton> unlock;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> fee;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> error;
	std::shared_ptr<bool> markPlayed = std::make_shared<bool>();
	bool details = false;
};

struct Context {
	not_null<Main::Session*> session;
	Fn<void()> unlock;
	Fn<void()> restore;
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
	return kMinus + Ui::FormatTonAmount(nano).full + ' ' + GramTicker();
}

[[nodiscard]] const TonConnectEmulation *ShownEmulation(
		const TonConnectRequestBoxState &now) {
	const auto emulation = now.emulation.get();
	return (now.transfer
		&& emulation
		&& emulation->status == EmulationStatus::Shown)
		? emulation
		: nullptr;
}

[[nodiscard]] QString ActionTitle(ActionKind kind) {
	switch (kind) {
	case ActionKind::Withdraw:
		return tr::lng_wallet_row_withdrawal(tr::now);
	case ActionKind::Deposit:
		return tr::lng_wallet_row_deposit(tr::now);
	case ActionKind::Transfer:
		return tr::lng_wallet_connect_request_transfer(tr::now);
	case ActionKind::Excess:
		return tr::lng_wallet_connect_request_excess(tr::now);
	case ActionKind::CallContract:
		return tr::lng_wallet_connect_request_call_contract(tr::now);
	case ActionKind::DeployContract:
		return tr::lng_wallet_connect_request_deploy_contract(tr::now);
	case ActionKind::Unknown:
		return tr::lng_wallet_connect_request_unknown_operation(tr::now);
	}
	Unexpected("Kind in TonConnectRequestBox ActionTitle.");
}

[[nodiscard]] ActionRowIcon ActionIcon(ActionKind kind) {
	switch (kind) {
	case ActionKind::Withdraw:
	case ActionKind::Transfer:
		return ActionRowIcon::Outgoing;
	case ActionKind::Deposit:
	case ActionKind::Excess:
		return ActionRowIcon::Incoming;
	case ActionKind::CallContract:
	case ActionKind::DeployContract:
	case ActionKind::Unknown:
		return ActionRowIcon::Gear;
	}
	Unexpected("Kind in TonConnectRequestBox ActionIcon.");
}

[[nodiscard]] ActionRowSign ActionSign(ActionSide side) {
	switch (side) {
	case ActionSide::None: return ActionRowSign::None;
	case ActionSide::Incoming: return ActionRowSign::Plus;
	case ActionSide::Outgoing: return ActionRowSign::Minus;
	}
	Unexpected("Side in TonConnectRequestBox ActionSign.");
}

[[nodiscard]] QString ActionAddress(
		const QString &raw,
		const TonConnectTransfer &transfer) {
	if (raw.isEmpty()) {
		return QString();
	}
	for (const auto &message : transfer.messages) {
		if (CanonicalAddress(message.destination) == raw) {
			return DisplayAddress(message.destination);
		}
	}
	return FormatFriendly(raw, true);
}

[[nodiscard]] ActionRowArgs ActionRowFor(
		const TonConnectAction &action,
		const TonConnectTransfer &transfer) {
	return {
		.kind = ActionTitle(action.kind),
		.address = ActionAddress(action.counterparty, transfer),
		.amountNano = action.amountNano,
		.sign = ActionSign(action.side),
		.icon = ActionIcon(action.kind),
	};
}

[[nodiscard]] bool HasContent(const TonConnectMessage &message) {
	return !message.comment.isEmpty()
		|| !message.payload.isEmpty()
		|| message.deploys;
}

[[nodiscard]] QString HeaderTitle(const TonConnectRequestBoxState &now) {
	switch (now.phase) {
	case Phase::Loading:
		return tr::lng_contacts_loading(tr::now);
	case Phase::Locked:
	case Phase::Restore:
		return now.topic;
	case Phase::Confirm:
		if (now.signData) {
			return tr::lng_wallet_connect_sign_title(tr::now);
		}
		return now.name.isEmpty()
			? tr::lng_wallet_connect_request_title(tr::now)
			: tr::lng_wallet_connect_request_title_app(
				tr::now,
				lt_name,
				now.name);
	case Phase::Notice:
	case Phase::Unhandled:
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
			? tr::lng_wallet_backup_update_fee(
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
	Ui::SetButtonDimmed(state->cancel.data(), now.busy);
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

void FillLoading(not_null<Ui::GenericBox*> box) {
	const auto cancel = SetTonConnectButtons(
		box,
		tr::lng_cancel(),
		nullptr).secondary;
	cancel->setClickedCallback([=] { box->closeBox(); });
}

void FillKeyNeeded(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		rpl::producer<QString> text,
		rpl::producer<QString> primary,
		Fn<void()> pressed) {
	const auto body = state->body.data();
	body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			std::move(text),
			st::walletConnectTextLabel),
		st::walletConnectTextMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	const auto buttons = SetTonConnectButtons(
		box,
		tr::lng_cancel(),
		std::move(primary));
	buttons.secondary->setClickedCallback([=] { box->closeBox(); });
	buttons.primary->setClickedCallback([=] {
		if (!Busy(state)) {
			pressed();
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
	const auto close = SetTonConnectButtons(
		box,
		nullptr,
		tr::lng_close()).primary;
	close->setClickedCallback([=] { box->closeBox(); });
}

void AddErrorLine(not_null<State*> state) {
	const auto body = state->body.data();
	state->error = body->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			body,
			object_ptr<Ui::FlatLabel>(body, st::walletConnectErrorLabel),
			style::margins(0, st::defaultVerticalListSkip, 0, 0)),
		st::boxRowPadding,
		style::al_top);
}

void FillUnhandled(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
	const auto body = state->body.data();
	body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			now.notice,
			st::walletConnectTextLabel),
		st::walletConnectTextMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			tr::lng_wallet_connect_request_unhandled_about(),
			st::walletConnectCaptionLabel),
		st::walletConnectCaptionMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	AddErrorLine(state);
	const auto buttons = SetTonConnectButtons(
		box,
		tr::lng_cancel(),
		tr::lng_wallet_connect_request_decline());
	buttons.secondary->setClickedCallback([=] {
		if (!Busy(state)) {
			box->closeBox();
		}
	});
	buttons.primary->setClickedCallback([=, decline = context.decline] {
		if (!Busy(state)) {
			decline();
		}
	});
	state->cancel = buttons.secondary;
	state->decline = buttons.primary;
}

void AddTraceWarning(
		not_null<Ui::VerticalLayout*> body,
		const TonConnectRequestBoxState &now) {
	const auto emulation = now.emulation.get();
	if (!emulation
		|| (emulation->status != EmulationStatus::Failed
			&& emulation->status != EmulationStatus::Incomplete)) {
		return;
	}
	const auto failed = (emulation->status == EmulationStatus::Failed);
	body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			(failed
				? tr::lng_wallet_connect_request_trace_failed()
				: tr::lng_wallet_connect_request_trace_incomplete()),
			(failed
				? st::walletConnectErrorLabel
				: st::walletConnectCaptionLabel)),
		st::walletConnectCaptionMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
}

void AddFeeLine(
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
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
	AddTraceWarning(body, now);
}

void AddDecisionButtons(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		rpl::producer<QString> secondary,
		rpl::producer<QString> primary) {
	AddErrorLine(state);
	const auto buttons = SetTonConnectButtons(
		box,
		std::move(secondary),
		std::move(primary));
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

void AddConfirmTail(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
	AddFeeLine(state, context, now);
	AddDecisionButtons(
		box,
		state,
		context,
		tr::lng_wallet_connect_request_decline(),
		tr::lng_wallet_connect_request_confirm());
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
	const auto shown = ShownEmulation(now);
	body->add(
		MakeTransferCard(body, context.session, {
			.totalNano = now.transfer ? now.transfer->totalNano : int64(0),
			.netNano = (shown
				? std::make_optional(shown->netNano)
				: std::nullopt),
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
			.markPlayed = state->markPlayed,
		}),
		st::walletConnectCardMargin,
		style::al_top);
	AddConfirmTail(box, state, context, now);
}

[[nodiscard]] style::margins DetailsTitlePadding() {
	return style::margins(
		(st::boxRowPadding.left()
			- st::defaultSubsectionTitlePadding.left()),
		0,
		0,
		0);
}

void FillRequestDetails(
		not_null<Ui::VerticalLayout*> body,
		std::shared_ptr<Ui::Show> show,
		const Context &context,
		const TonConnectTransfer &transfer) {
	const auto padding = DetailsTitlePadding();
	Ui::AddSubsectionTitle(
		body,
		tr::lng_wallet_connect_request_preview(),
		padding);
	const auto preview = AddDetailsTableFrame(body);
	Ui::AddTableRow(
		preview,
		tr::lng_wallet_connect_request_total(),
		MakeTotalValue(preview, context.session, transfer.totalNano));
	const auto destinations = CollectDestinations(transfer);
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
	for (const auto &message : transfer.messages) {
		Ui::AddTableRow(
			list,
			tr::lng_wallet_connect_request_transfer(),
			MakeMessageValue(list, show, message));
	}
}

void FillEmulatedDetails(
		not_null<Ui::VerticalLayout*> body,
		std::shared_ptr<Ui::Show> show,
		const TonConnectTransfer &transfer,
		const TonConnectEmulation &emulation) {
	const auto padding = DetailsTitlePadding();
	Ui::AddSubsectionTitle(
		body,
		tr::lng_wallet_connect_request_preview(),
		padding);
	const auto table = AddDetailsTableFrame(body);
	for (const auto &action : emulation.actions) {
		table->addRow(
			nullptr,
			MakeActionRow(table, ActionRowFor(action, transfer)),
			style::margins(),
			style::margins());
	}
	const auto &messages = transfer.messages;
	if (ranges::none_of(messages, HasContent)) {
		return;
	}
	Ui::AddSubsectionTitle(
		body,
		tr::lng_wallet_connect_request_messages(),
		padding);
	const auto list = AddDetailsTableFrame(body);
	for (const auto &message : messages) {
		if (HasContent(message)) {
			Ui::AddTableRow(
				list,
				tr::lng_wallet_connect_request_message(),
				MakeMessageValue(list, show, message));
		}
	}
}

void AddDetailsSkip(not_null<Ui::VerticalLayout*> body) {
	Ui::AddSkip(
		body,
		(st::walletConnectCardMargin.bottom()
			- st::giveawayGiftCodeTableMargin.bottom()));
}

[[nodiscard]] object_ptr<Ui::FlatLabel> MakeWarningLabel(
		not_null<QWidget*> parent,
		TextWithEntities text,
		const style::FlatLabel &st) {
	auto helper = Ui::Text::CustomEmojiHelper();
	auto marked = helper.paletteDependent(Ui::AttentionMarkEmoji());
	marked.append(QChar(' ')).append(std::move(text));
	auto result = object_ptr<Ui::FlatLabel>(parent, st);
	const auto raw = result.data();
	raw->setMarkedText(marked, helper.context([=] { raw->update(); }));
	return result;
}

[[nodiscard]] TextWithEntities BinaryWarningText() {
	auto result = tr::lng_wallet_connect_sign_binary(tr::now, tr::bold);
	result.append(QChar('\n'));
	result.append(tr::lng_wallet_connect_sign_blind(tr::now));
	return result;
}

[[nodiscard]] object_ptr<Ui::FlatLabel> MakeSignDataLabel(
		not_null<QWidget*> parent,
		const QString &text) {
	return object_ptr<Ui::FlatLabel>(
		parent,
		rpl::single(Ui::Text::Wrapped(tr::marked(text), EntityType::Code)),
		st::walletConnectSignDataLabel);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeSignDataBubble(
		not_null<QWidget*> parent,
		object_ptr<Ui::RpWidget> content,
		Fn<void()> copy) {
	auto result = MakeCommentBubble(
		parent,
		std::move(content),
		st::windowBgOver);
	const auto raw = result.data();
	const auto button = Ui::CreateChild<Ui::AbstractButton>(raw);
	raw->sizeValue() | rpl::on_next([=](QSize size) {
		button->resize(size);
	}, button->lifetime());
	button->setPointerCursor(true);
	button->setClickedCallback(std::move(copy));
	button->raise();
	return result;
}

[[nodiscard]] Fn<void()> CopySignData(
		std::shared_ptr<Ui::Show> show,
		const TonConnectSignData &data) {
	return CopyTextCallback(
		std::move(show),
		data.data,
		tr::lng_text_copied(tr::now));
}

void AddSignDataBubble(
		not_null<Ui::VerticalLayout*> body,
		object_ptr<Ui::RpWidget> content,
		Fn<void()> copy) {
	body->add(
		MakeSignDataBubble(body, std::move(content), std::move(copy)),
		st::giveawayGiftCodeTableMargin,
		style::al_justify);
}

[[nodiscard]] TextWithEntities SignDataFieldText(
		const TonConnectSignDataField &field) {
	auto result = Ui::Text::Colorized(field.name + QChar(':'));
	result.append(QChar(' ')).append(field.value);
	return Ui::Text::Wrapped(std::move(result), EntityType::Code);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeSignDataFields(
		not_null<QWidget*> parent,
		const std::vector<TonConnectSignDataField> &fields) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = result.data();
	for (const auto &field : fields) {
		const auto depth = std::min(field.depth, kFieldDepthShown);
		raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				rpl::single(SignDataFieldText(field)),
				st::walletConnectSignFieldLabel),
			style::margins(depth * st::walletConnectSignFieldIndent, 0, 0, 0));
	}
	return result;
}

void AddSignDataCell(
		not_null<Ui::VerticalLayout*> body,
		std::shared_ptr<Ui::Show> show,
		const TonConnectSignData &data) {
	auto copy = CopySignData(std::move(show), data);
	if (data.fields.empty()) {
		AddSignDataBubble(
			body,
			MakeSignDataLabel(body, data.schema),
			std::move(copy));
		body->add(
			MakeWarningLabel(
				body,
				tr::marked(tr::lng_wallet_connect_sign_blind(tr::now)),
				st::walletCommentCaptionLabel),
			st::walletConnectSignCaptionMargin);
		return;
	}
	AddSignDataBubble(
		body,
		MakeSignDataFields(body, data.fields),
		std::move(copy));
	body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			tr::lng_wallet_connect_sign_text_about(),
			st::walletCommentCaptionLabel),
		st::walletConnectSignCaptionMargin);
}

void AddSignDataContent(
		not_null<Ui::VerticalLayout*> body,
		std::shared_ptr<Ui::Show> show,
		const TonConnectSignData &data) {
	switch (data.type) {
	case SignDataType::Text:
		AddSignDataBubble(
			body,
			MakeSignDataLabel(body, data.data),
			CopySignData(std::move(show), data));
		body->add(
			object_ptr<Ui::FlatLabel>(
				body,
				tr::lng_wallet_connect_sign_text_about(),
				st::walletCommentCaptionLabel),
			st::walletConnectSignCaptionMargin);
		return;
	case SignDataType::Binary:
		AddSignDataBubble(
			body,
			MakeWarningLabel(body, BinaryWarningText(), st::walletCommentLabel),
			CopySignData(std::move(show), data));
		AddDetailsSkip(body);
		return;
	case SignDataType::Cell:
		AddSignDataCell(body, std::move(show), data);
		return;
	}
	Unexpected("Type in TonConnectRequestBox AddSignDataContent.");
}

void FillSignData(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
	const auto body = state->body.data();
	Ui::AddSubsectionTitle(
		body,
		tr::lng_wallet_connect_sign_data(),
		DetailsTitlePadding());
	AddSignDataContent(body, box->uiShow(), *now.signData);
	AddDecisionButtons(
		box,
		state,
		context,
		tr::lng_cancel(),
		tr::lng_wallet_connect_sign_button());

	// WHY: a justified row gets its width only when the layout resizes, and
	// the page is refilled after the box was laid out, so the data bubble
	// would keep its label's natural width without this.
	if (const auto width = body->widthNoMargins(); width > 0) {
		body->resizeToWidth(width);
	}
}

void FillDetails(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const Context &context,
		const TonConnectRequestBoxState &now) {
	const auto body = state->body.data();
	if (const auto transfer = now.transfer.get()) {
		const auto show = box->uiShow();
		if (const auto shown = ShownEmulation(now)) {
			FillEmulatedDetails(body, show, *transfer, *shown);
		} else {
			FillRequestDetails(body, show, context, *transfer);
		}
		AddDetailsSkip(body);
	}
	AddConfirmTail(box, state, context, now);
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
	state->cancel = nullptr;
	state->unlock = nullptr;
	state->fee = nullptr;
	state->error = nullptr;
	state->body->clear();
	if (now.phase != Phase::Confirm) {
		state->details = false;
	}
	switch (now.phase) {
	case Phase::Loading: FillLoading(box); break;
	case Phase::Locked:
		FillKeyNeeded(
			box,
			state,
			tr::lng_wallet_connect_request_locked(),
			tr::lng_wallet_connect_request_unlock(),
			context.unlock);
		break;
	case Phase::Restore:
		FillKeyNeeded(
			box,
			state,
			tr::lng_wallet_connect_restore(),
			tr::lng_wallet_restore_title(),
			context.restore);
		break;
	case Phase::Notice: FillNotice(box, state, now); break;
	case Phase::Unhandled: FillUnhandled(box, state, context, now); break;
	case Phase::Confirm:
		if (now.signData) {
			FillSignData(box, state, context, now);
		} else if (state->details) {
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
		.restore = args.restore,
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
