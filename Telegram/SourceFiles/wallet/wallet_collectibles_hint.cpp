/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles_hint.h"

#include "data/data_changes.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/tooltip.h"
#include "wallet/wallet_own_card.h"
#include "wallet/wallet_session.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {

CollectiblesHint::CollectiblesHint(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	Fn<std::optional<QRect>()> target)
: _session(session)
, _target(std::move(target)) {
	if (ownCard().cardDecorationHintDone()) {
		_done = true;
		return;
	}
	_tooltip = base::make_unique_q<Ui::ImportantTooltip>(
		parent,
		Ui::MakeTooltipWithClose(
			parent,
			tr::lng_wallet_card_drag_hint(tr::marked),
			st::walletCardHintMaxWidth,
			st::defaultImportantTooltipLabel,
			st::importantTooltipHide,
			st::defaultImportantTooltip.padding,
			[=] { record(); }),
		st::walletCardHintTooltip);
	_tooltip->toggleFast(false);

	const auto wallet = &session->wallet();
	rpl::merge(
		wallet->collectiblesTabValue() | rpl::to_empty,
		wallet->listsGatedValue() | rpl::to_empty,
		wallet->ownCard().shownValue() | rpl::to_empty,
		session->changes().peerFlagsValue(
			session->user(),
			Data::PeerUpdate::Flag::WalletCard
		) | rpl::to_empty
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
}

CollectiblesHint::~CollectiblesHint() = default;

void CollectiblesHint::refresh() {
	if (_done) {
		return;
	}
	const auto &own = ownCard();
	if (own.cardDecorationHintDone()) {
		record();
		return;
	}
	const auto &wallet = _session->wallet();
	const auto area = (own.ready()
		&& own.shown().stickers.empty()
		&& wallet.collectiblesTab()
		&& !wallet.listsGated())
		? _target()
		: std::nullopt;
	if (!area || area->isEmpty()) {
		_shown = false;
		_tooltip->toggleFast(false);
		return;
	}
	_tooltip->pointAt(*area, RectPart::Bottom);
	if (!_shown) {
		_shown = true;
		_tooltip->raise();
		_tooltip->toggleAnimated(true);
	}
}

void CollectiblesHint::record() {
	ownCard().finishCardDecorationHint();
	_done = true;
	_shown = false;
	if (_tooltip) {
		_tooltip->toggleFast(false);
	}
}

OwnCard &CollectiblesHint::ownCard() const {
	return _session->wallet().ownCard();
}

} // namespace Wallet
