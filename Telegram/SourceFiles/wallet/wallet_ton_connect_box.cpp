/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_box.h"

#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "ui/controls/button_busy.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"

#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

using Phase = TonConnectBoxPhase;

struct State {
	std::optional<TonConnectBoxState> built;
	QPointer<Ui::RoundButton> connect;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> error;
	bool busy = false;
};

[[nodiscard]] bool Confirming(Phase phase) {
	return (phase == Phase::Confirm) || (phase == Phase::Connecting);
}

[[nodiscard]] bool SameStructure(
		const TonConnectBoxState &built,
		const TonConnectBoxState &now) {
	auto aligned = now;
	aligned.phase = built.phase;
	aligned.error = built.error;
	return (aligned == built)
		&& ((built.phase == now.phase)
			|| (Confirming(built.phase) && Confirming(now.phase)));
}

void AddLabel(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> text,
		const style::FlatLabel &st) {
	content->add(
		object_ptr<Ui::FlatLabel>(content, std::move(text), st),
		st::boxRowPadding);
}

void AddApp(
		not_null<Ui::VerticalLayout*> content,
		const TonConnectBoxState &now) {
	const auto named = !now.name.isEmpty() && (now.name != now.domain);
	const auto hosted = !now.domain.isEmpty();
	if (named) {
		AddLabel(content, rpl::single(now.name), st::boxLabel);
	}
	if (hosted) {
		AddLabel(
			content,
			rpl::single(now.domain),
			st::walletCommentCaptionLabel);
	}
	if (named || hosted) {
		Ui::AddSkip(content);
	}
}

void UpdateConfirm(
		not_null<State*> state,
		const TonConnectBoxState &now,
		anim::type animated) {
	state->busy = (now.phase == Phase::Connecting);
	Ui::SetButtonBusy(state->connect.data(), state->busy);
	if (const auto error = state->error.data()) {
		if (!now.error.isEmpty()) {
			error->entity()->setText(now.error);
		}
		error->toggle(!now.error.isEmpty(), animated);
	}
}

void FillLoading(not_null<Ui::GenericBox*> box) {
	const auto content = box->verticalLayout();
	AddLabel(content, tr::lng_wallet_connect_loading(), st::boxLabel);
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

void FillConfirm(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const TonConnectBoxState &now,
		Fn<void()> connect) {
	const auto content = box->verticalLayout();
	AddApp(content, now);
	AddLabel(content, tr::lng_wallet_connect_permission(), st::boxLabel);
	Ui::AddSkip(content);
	AddLabel(
		content,
		tr::lng_wallet_connect_address(),
		st::walletCommentCaptionLabel);
	AddLabel(
		content,
		rpl::single(now.address),
		st::walletDetailsAddressLabel);
	if (now.proof) {
		Ui::AddSkip(content);
		AddLabel(content, tr::lng_wallet_connect_proof(), st::boxLabel);
	}
	Ui::AddSkip(content);
	AddLabel(
		content,
		tr::lng_wallet_connect_reassurance(),
		st::walletCommentCaptionLabel);
	state->error = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(content, st::walletCommentErrorLabel),
			style::margins(0, st::defaultVerticalListSkip, 0, 0)),
		st::boxRowPadding);
	Ui::AddSkip(content);
	state->connect = box->addButton(tr::lng_wallet_connect_button(), [=] {
		if (!state->busy) {
			connect();
		}
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	UpdateConfirm(state, now, anim::type::instant);
}

void FillNotice(
		not_null<Ui::GenericBox*> box,
		const TonConnectBoxState &now) {
	const auto content = box->verticalLayout();
	AddApp(content, now);
	AddLabel(content, rpl::single(now.notice), st::boxLabel);
	Ui::AddSkip(content);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void Rebuild(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const TonConnectBoxState &now,
		Fn<void()> connect) {
	state->connect = nullptr;
	state->error = nullptr;
	state->busy = false;
	box->verticalLayout()->clear();
	box->clearButtons();
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
	if (now.phase == Phase::Loading) {
		FillLoading(box);
	} else if (now.phase == Phase::Notice) {
		FillNotice(box, now);
	} else {
		FillConfirm(box, state, now, std::move(connect));
	}
}

} // namespace

void TonConnectBox(not_null<Ui::GenericBox*> box, TonConnectBoxArgs args) {
	box->setTitle(tr::lng_wallet_connect_title());
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

	std::move(
		args.state
	) | rpl::on_next([=, connect = args.connect](
			const TonConnectBoxState &now) {
		if (state->built && SameStructure(*state->built, now)) {
			UpdateConfirm(state, now, anim::type::normal);
		} else {
			Rebuild(box, state, now, connect);
		}
		state->built = now;
	}, box->lifetime());
}

} // namespace Wallet
