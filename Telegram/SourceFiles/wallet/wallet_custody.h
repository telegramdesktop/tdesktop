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

// publicKey is the anchor: words 1-12 of the phrase, it derives the
// address and never changes. signingKey is the current signing key this
// device established for the wallet; empty means not established (a
// pre-v4 store). awaitingServerKey marks a journal-confirmed promotion
// the server has not served yet: it is set only by
// Session::promotePendingRotation, maintained only by
// Session::reconcileCustody and ignored while signingKey is empty.
struct CustodyRecord {
	QString recordId;
	QString address;
	QByteArray publicKey;
	QByteArray signingKey;
	int network = 1;
	QString secretRef;
	bool active = false;
	bool rotatedSinceBackup = false;
	bool awaitingServerKey = false;

	[[nodiscard]] bool signsWith(const QByteArray &servedKey) const;

	// A pre-v4 record whose anchor is not the served key: the wallet
	// rotated, and nothing but the phrase itself says whether this record
	// holds the current signing key or an obsolete one. Session keeps such
	// a record reachable through the parked reveal, and the identity of
	// the revealed words is what establishes its signing key.
	[[nodiscard]] bool unresolved(const QByteArray &servedKey) const;

	friend bool operator==(
		const CustodyRecord &,
		const CustodyRecord &) = default;
};

struct PendingRotation {
	QString recordId;
	QString secretRef;
	QString operationId;

	// The replacement signing key from PreparedKeyRotation::new_public_key,
	// empty on a pre-v4 pending, which can resolve only through the journal.
	QByteArray newPublicKey;
};

struct CustodyStore {
	std::vector<CustodyRecord> records;
	QByteArray lastSeenServerKey;
	std::optional<PendingRotation> pendingRotation;

	[[nodiscard]] const CustodyRecord *byAnchor(
		const QByteArray &publicKey) const;

	// The record of the same wallet whether or not it signs for it; an
	// empty address answers nothing.
	[[nodiscard]] const CustodyRecord *forAddress(
		const QString &canonicalAddress) const;

	// The record of the same wallet that signs with the served key.
	[[nodiscard]] const CustodyRecord *current(
		const QString &canonicalAddress,
		const QByteArray &servedKey) const;

	[[nodiscard]] bool anyAwaitingServerKey() const;
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
