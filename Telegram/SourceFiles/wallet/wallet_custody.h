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
	bool rotatedSinceBackup = false;
};

struct PendingRotation {
	QString recordId;
	QString secretRef;
	QString operationId;
};

struct CustodyStore {
	std::vector<CustodyRecord> records;
	QByteArray lastSeenServerKey;
	std::optional<PendingRotation> pendingRotation;

	[[nodiscard]] const CustodyRecord *matching(
		const QByteArray &publicKey) const;
};

[[nodiscard]] std::optional<CustodyStore> ReadCustodyStore(
	Storage::Account &local);
[[nodiscard]] bool WriteCustodyStore(
	Storage::Account &local,
	const CustodyStore &store);

// The custody fields that name a secret, in store order: every record's
// secretRef and then the pending rotation's, each offered once; an empty
// reference names nothing and is skipped. A callback answering true
// detaches the entry that carries the reference - the record is erased,
// the pending rotation is cleared. Every pass that acts on the values the
// store names walks this one definition, so a field that names a secret
// is added in exactly one place.
void ForEachCustodySecretRef(
	CustodyStore &store,
	Fn<bool(const QString &secretRef)> drop);

} // namespace Wallet
