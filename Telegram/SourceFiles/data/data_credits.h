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
	bool extended = false;
};

using CreditTopupOptions = std::vector<CreditTopupOption>;

enum class CreditsHistoryMediaType {
	Photo,
	Video,
};

struct CreditsHistoryMedia {
	CreditsHistoryMediaType type = CreditsHistoryMediaType::Photo;
	uint64 id = 0;
};

struct CreditsHistoryEntry final {
	using PhotoId = uint64;
	enum class PeerType {
		Peer,
		AppStore,
		PlayMarket,
		Fragment,
		Unsupported,
		PremiumBot,
		Ads,
	};

	QString id;
	QString title;
	QString description;
	QDateTime date;
	PhotoId photoId = 0;
	std::vector<CreditsHistoryMedia> extended;
	uint64 credits = 0;
	uint64 bareMsgId = 0;
	uint64 barePeerId = 0;
	PeerType peerType;
	bool refunded = false;
	bool pending = false;
	bool failed = false;
	QDateTime successDate;
	QString successLink;
	bool in = false;

};

struct CreditsStatusSlice final {
	using OffsetToken = QString;
	std::vector<CreditsHistoryEntry> list;
	uint64 balance = 0;
	bool allLoaded = false;
	OffsetToken token;
};

} // namespace Data
