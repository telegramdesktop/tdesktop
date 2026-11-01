/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "crl/crl_time.h"
#include "data/data_user_wallet_card.h"
#include "mtproto/sender.h"
#include "rpl/variable.h"

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

struct OwnCardState {
	std::vector<Data::WalletCardSticker> stickers;
	QString background;

	friend inline bool operator==(
		const OwnCardState &,
		const OwnCardState &) = default;
};

enum class OwnCardChange : uchar {
	Applied,
	Unchanged,
	NotReady,
	Limit,
	Invalid,
};

struct OwnCardFailure {
	QString type;
	std::vector<Data::WalletCardSticker> left;
	std::vector<Data::WalletCardSticker> returned;
	bool silent = false;
	bool notReady = false;
};

class OwnCard final {
public:
	explicit OwnCard(not_null<Main::Session*> session);
	~OwnCard();

	[[nodiscard]] bool ready() const;
	[[nodiscard]] bool contains(const QString &address) const;
	[[nodiscard]] OwnCardState shown() const;
	[[nodiscard]] rpl::producer<OwnCardState> shownValue() const;
	[[nodiscard]] rpl::producer<OwnCardFailure> failures() const;

	void applyFullUserCard(std::optional<Data::WalletCard> card);

	[[nodiscard]] OwnCardChange save(OwnCardState desired);

	[[nodiscard]] bool cardDecorationHintDone() const;
	void finishCardDecorationHint();

private:
	[[nodiscard]] OwnCardState confirmed() const;
	void refresh();
	void apply(OwnCardState desired);
	void send();
	void done(const MTPwallet_Card &result);
	void discard(OwnCardFailure failure);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	std::optional<OwnCardState> _pending;
	OwnCardState _sent;
	mtpRequestId _requestId = 0;
	crl::time _preserveAppearanceUntil = 0;
	rpl::variable<OwnCardState> _shown;
	rpl::event_stream<OwnCardFailure> _failures;
	rpl::lifetime _lifetime;

};

[[nodiscard]] bool RemoveOwnCardSticker(
	OwnCardState &state,
	const QString &address);

} // namespace Wallet
