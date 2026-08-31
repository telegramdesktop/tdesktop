/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Storage {
class Account;
} // namespace Storage

namespace Wallet {

inline constexpr auto kCustodyPublicKeySize = 32;

struct CustodyRecord {
	QString recordId;
	QString address;
	QByteArray publicKey;
	int network = 1;
	QString secretRef;
	bool active = false;
};

struct CustodyStore {
	std::vector<CustodyRecord> records;

	[[nodiscard]] const CustodyRecord *matching(
		const QByteArray &publicKey) const;
};

[[nodiscard]] std::optional<CustodyStore> ReadCustodyStore(
	Storage::Account &local);
[[nodiscard]] bool WriteCustodyStore(
	Storage::Account &local,
	const CustodyStore &store);

} // namespace Wallet
