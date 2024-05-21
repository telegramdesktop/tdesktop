/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct CreditTopupOption final {
	uint64 credits = 0;
	QString product;
	QString currency;
	uint64 amount = 0;
};

using CreditTopupOptions = std::vector<CreditTopupOption>;

struct CreditsHistoryEntry final {
	enum class PeerType {
		Peer,
		AppStore,
		PlayMarket,
		Fragment,
		Unsupported,
		PremiumBot,
	};
	QString id;
	uint64 credits = 0;
	QDateTime date;
	PeerType peerType;
	PeerId peerId = PeerId(0);
};

struct CreditsStatusSlice final {
	using OffsetToken = QString;
	std::vector<CreditsHistoryEntry> list;
	uint64 balance = 0;
	bool allLoaded = false;
	OffsetToken token;
};

} // namespace Data
