/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_own_card.h"

#include "data/data_changes.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "storage/storage_account.h"
#include "wallet/wallet_address.h"

namespace Wallet {
namespace {

using Data::WalletCardSticker;

constexpr auto kPreserveAppearanceDuration = crl::time(60000);
constexpr auto kCardDecorationHintDonePref = "wallet_card_drag_hint_hidden"_cs;

[[nodiscard]] auto SameNftAs(const QString &address) {
	return [canonical = CanonicalAddress(address)](
			const WalletCardSticker &sticker) {
		return !canonical.isEmpty()
			&& (CanonicalAddress(sticker.address) == canonical);
	};
}

[[nodiscard]] QString SentBackground(const OwnCardState &state) {
	const auto i = ranges::find_if(
		state.stickers,
		SameNftAs(state.background));
	return (i != end(state.stickers)) ? i->address : QString();
}

[[nodiscard]] bool SameWire(const OwnCardState &a, const OwnCardState &b) {
	const auto same = [](
			const WalletCardSticker &x,
			const WalletCardSticker &y) {
		return (x.area == y.area)
			&& (CanonicalAddress(x.address) == CanonicalAddress(y.address));
	};
	return ranges::equal(a.stickers, b.stickers, same)
		&& (CanonicalAddress(SentBackground(a))
			== CanonicalAddress(SentBackground(b)));
}

[[nodiscard]] bool ValidArea(const Data::StoryArea &area) {
	const auto &geometry = area.geometry;
	const auto values = {
		geometry.x(),
		geometry.y(),
		geometry.width(),
		geometry.height(),
		area.rotation,
		area.radius,
	};
	const auto finite = [](float64 value) {
		return std::isfinite(value);
	};
	return ranges::all_of(values, finite)
		&& (geometry.width() > 0.)
		&& (geometry.height() > 0.)
		&& (area.radius >= 0.);
}

[[nodiscard]] bool ValidSticker(const WalletCardSticker &sticker) {
	return sticker.gift && sticker.gift->unique && ValidArea(sticker.area);
}

[[nodiscard]] bool ValidState(const OwnCardState &state) {
	return ranges::all_of(state.stickers, ValidSticker)
		&& Data::UniqueWalletCardStickers(state.stickers)
		&& (state.background.isEmpty() || !SentBackground(state).isEmpty());
}

[[nodiscard]] std::vector<WalletCardSticker> Missing(
		const std::vector<WalletCardSticker> &from,
		const std::vector<WalletCardSticker> &in) {
	auto result = std::vector<WalletCardSticker>();
	for (const auto &sticker : from) {
		if (ranges::none_of(in, SameNftAs(sticker.address))) {
			result.push_back(sticker);
		}
	}
	return result;
}

[[nodiscard]] MTPwallet_SetCardStickers Serialize(
		const OwnCardState &state) {
	using Flag = MTPwallet_setCardStickers::Flag;
	auto stickers = QVector<MTPInputWalletCardSticker>();
	stickers.reserve(state.stickers.size());
	for (const auto &sticker : state.stickers) {
		stickers.push_back(MTP_inputWalletCardSticker(
			Data::SerializeArea(sticker.area),
			MTP_string(sticker.address)));
	}
	const auto background = SentBackground(state);
	return MTPwallet_SetCardStickers(
		MTP_flags(background.isEmpty() ? Flag() : Flag::f_bg_nft_address),
		MTP_vector<MTPInputWalletCardSticker>(std::move(stickers)),
		MTP_string(background));
}

} // namespace

OwnCard::OwnCard(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp()) {
	session->changes().peerFlagsValue(
		session->user(),
		Data::PeerUpdate::Flag::WalletCard
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
}

OwnCard::~OwnCard() = default;

bool OwnCard::ready() const {
	const auto card = _session->user()->walletCard();
	return card && card->stickersComplete;
}

bool OwnCard::contains(const QString &address) const {
	const auto state = shown();
	return ranges::any_of(state.stickers, SameNftAs(address));
}

OwnCardState OwnCard::shown() const {
	return _pending ? *_pending : confirmed();
}

rpl::producer<OwnCardState> OwnCard::shownValue() const {
	return _shown.value();
}

rpl::producer<OwnCardFailure> OwnCard::failures() const {
	return _failures.events();
}

void OwnCard::applyFullUserCard(std::optional<Data::WalletCard> card) {
	const auto self = _session->user();
	const auto current = self->walletCard();
	// Profile reads can lag behind a successful card mutation.
	if (card && current
		&& (_pending || crl::now() < _preserveAppearanceUntil)
		&& (CanonicalAddress(card->address)
			== CanonicalAddress(current->address))) {
		card->stickers = current->stickers;
		card->backgroundAddress = current->backgroundAddress;
		card->stickersComplete = current->stickersComplete;
	}
	self->setWalletCard(std::move(card));
}

OwnCardChange OwnCard::save(OwnCardState desired) {
	if (!ready()) {
		return OwnCardChange::NotReady;
	} else if (int(desired.stickers.size()) > Data::kWalletCardMaxStickers) {
		return OwnCardChange::Limit;
	} else if (!ValidState(desired)) {
		return OwnCardChange::Invalid;
	} else if (SameWire(desired, shown())) {
		return OwnCardChange::Unchanged;
	}
	apply(std::move(desired));
	return OwnCardChange::Applied;
}

bool OwnCard::cardDecorationHintDone() const {
	return _session->local().readPref<bool>(kCardDecorationHintDonePref);
}

void OwnCard::finishCardDecorationHint() {
	if (!cardDecorationHintDone()) {
		_session->local().writePref<bool>(kCardDecorationHintDonePref, true);
	}
}

OwnCardState OwnCard::confirmed() const {
	const auto card = _session->user()->walletCard();
	return card
		? OwnCardState{ card->stickers, card->backgroundAddress }
		: OwnCardState();
}

void OwnCard::refresh() {
	auto state = shown();
	if (!state.stickers.empty()) {
		finishCardDecorationHint();
	}
	_shown = std::move(state);
}

void OwnCard::apply(OwnCardState desired) {
	_pending = std::move(desired);
	refresh();
	if (!_requestId) {
		send();
	}
}

void OwnCard::send() {
	Expects(_pending.has_value());

	_sent = *_pending;
	_preserveAppearanceUntil = crl::now() + kPreserveAppearanceDuration;
	_requestId = _api.request(
		Serialize(_sent)
	).done([=](const MTPwallet_Card &result) {
		_requestId = 0;
		done(result);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		discard({
			.type = error.type(),
			.silent = MTP::IgnoreError(error),
		});
	}).handleAllErrors().send();
}

void OwnCard::done(const MTPwallet_Card &result) {
	const auto self = _session->user();
	_preserveAppearanceUntil = crl::now() + kPreserveAppearanceDuration;
	self->setWalletCard(Data::ParseWalletCard(_session, result));

	if (!_pending
		|| SameWire(*_pending, _sent)
		|| SameWire(*_pending, confirmed())) {
		_pending = std::nullopt;
		refresh();
	} else if (!ready()) {
		discard({ .notReady = true });
	} else {
		send();
	}
}

void OwnCard::discard(OwnCardFailure failure) {
	const auto before = _shown.current();
	_pending = std::nullopt;
	refresh();
	const auto &after = _shown.current();
	failure.left = Missing(before.stickers, after.stickers);
	failure.returned = Missing(after.stickers, before.stickers);
	_failures.fire(std::move(failure));
}

bool RemoveOwnCardSticker(OwnCardState &state, const QString &address) {
	const auto i = ranges::find_if(state.stickers, SameNftAs(address));
	if (i == end(state.stickers)) {
		return false;
	}
	state.stickers.erase(i);
	if (SentBackground(state).isEmpty()) {
		state.background = QString();
	}
	return true;
}

} // namespace Wallet
