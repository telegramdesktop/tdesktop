/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "base/timer.h"
#include "storage/details/storage_file_utilities.h"

#include <mutex>

namespace Wallet {

struct CustodyStore;
class VaultRuntime;

inline constexpr auto kVaultKeySize = 32;
inline constexpr auto kVaultNonceSize = 12;
inline constexpr auto kVaultTagSize = 16;
inline constexpr auto kVaultSaltSize = 32;
inline constexpr auto kVaultOpenSecretSize = 32;
inline constexpr auto kVaultRetention = 15 * 60 * crl::time(1000);

// Kinds from 3 up are reserved for the hardware providers: a header that
// carries one reads as Unsupported here and is never rewritten.
enum class VaultKind : quint32 {
	Passcode = 1,
	Open = 2,
};

inline constexpr auto kFirstReservedVaultKind = quint32(3);

// A QByteArray shares its storage implicitly, so a copy of key material or
// plaintext would survive any cleanse of the original. This buffer copies on
// construction, is move-only, and wipes its bytes with OPENSSL_cleanse on
// clear(), on destruction and when it is moved from.
class SecureBytes final {
public:
	SecureBytes() = default;
	explicit SecureBytes(int size);
	explicit SecureBytes(bytes::const_span data);
	explicit SecureBytes(const QByteArray &data);
	SecureBytes(SecureBytes &&other) noexcept;
	SecureBytes &operator=(SecureBytes &&other) noexcept;
	~SecureBytes();

	[[nodiscard]] SecureBytes copy() const;
	[[nodiscard]] bytes::const_span span() const;
	[[nodiscard]] bytes::span span();
	[[nodiscard]] int size() const;
	[[nodiscard]] bool empty() const;
	void clear();

private:
	QByteArray _bytes;

};

struct VaultWrap {
	VaultKind kind = VaultKind::Passcode;
	quint32 generation = 0;
	Storage::details::PasscodeKdf kdf;
	QByteArray salt;
	QByteArray openSecret;
	QByteArray blob;
};

struct VaultHeader {
	quint32 committed = 0;
	std::vector<VaultWrap> wraps;

	[[nodiscard]] const VaultWrap *committedWrap() const;
};

struct VaultReading {
	enum class State {
		Absent,
		Broken,
		Unsupported,
		Read,
	};

	State state = State::Absent;
	VaultHeader header;
	bool dirty = false;
};

struct VaultPreparedWrap {
	VaultWrap wrap;
	SecureBytes wrapKey;
};

struct VaultSecretRecord {
	bool requireUserPresence = false;
	SecureBytes bytes;
};

enum class VaultTransitionResult {
	Done,
	Refused,
	WriteFailed,
	VerifyFailed,
};

// One user-confirmed logical operation's authorization: releasing the last
// live grant cleanses the key unless the retention window is open.
class VaultGrant final {
public:
	VaultGrant() = default;
	VaultGrant(VaultGrant &&other) noexcept;
	VaultGrant &operator=(VaultGrant &&other) noexcept;
	~VaultGrant();

	[[nodiscard]] bool valid() const;

private:
	friend class VaultRuntime;
	explicit VaultGrant(std::shared_ptr<VaultRuntime> runtime);

	std::shared_ptr<VaultRuntime> _runtime;

};

// The unlocked state of one account's vault. The main-thread mutators and
// the any-thread readers meet under one mutex that is never held across a
// timer or rpl call; every key copy handed out is a SecureBytes the receiver
// cleanses.
class VaultRuntime final
	: public std::enable_shared_from_this<VaultRuntime> {
public:
	VaultRuntime();
	~VaultRuntime();

	[[nodiscard]] VaultReading reading(Storage::Account &local);
	[[nodiscard]] bool unlockWithPasscode(
		Storage::Account &local,
		const QByteArray &passcode);
	[[nodiscard]] bool unlockOpen(Storage::Account &local);
	void arm(VaultPreparedWrap policy);
	[[nodiscard]] VaultGrant grant();
	void setRetention(bool fifteenMinutes);
	[[nodiscard]] bool retained() const;
	[[nodiscard]] bool unlocked() const;
	void clear();

	struct StoreAuthority {
		std::optional<SecureBytes> key;
		std::optional<VaultPreparedWrap> policy;
	};
	[[nodiscard]] std::optional<SecureBytes> keyForRead();
	[[nodiscard]] StoreAuthority authorityForStore();
	void adoptCreated(SecureBytes key);

private:
	friend class VaultGrant;
	void release();

	mutable std::mutex _mutex;
	std::optional<SecureBytes> _key;
	std::optional<VaultPreparedWrap> _policy;
	int _grants = 0;
	crl::time _retainUntil = 0;
	base::Timer _retention;
	rpl::lifetime _lifetime;

};

[[nodiscard]] QString VaultSecretStorageKey(const QString &secretRef);

[[nodiscard]] VaultReading ReadVaultHeader(Storage::Account &local);
[[nodiscard]] VaultReading ReconcileVaultHeader(Storage::Account &local);
[[nodiscard]] bool WriteVaultHeader(
	Storage::Account &local,
	const VaultHeader &header);
[[nodiscard]] bool RemoveVaultHeader(Storage::Account &local);

[[nodiscard]] std::optional<VaultPreparedWrap> PrepareVaultPasscodeWrap(
	const QByteArray &passcode);
[[nodiscard]] std::optional<VaultPreparedWrap> PrepareVaultOpenWrap();
[[nodiscard]] std::optional<SecureBytes> DeriveVaultWrapKey(
	const VaultWrap &wrap,
	const QByteArray &passcode);
[[nodiscard]] QByteArray WrapVaultKey(
	const SecureBytes &vaultKey,
	const VaultWrap &wrap,
	const SecureBytes &wrapKey);
[[nodiscard]] std::optional<SecureBytes> UnwrapVaultKey(
	const VaultWrap &wrap,
	const SecureBytes &wrapKey);

[[nodiscard]] bool IsVaultRecord(const QByteArray &serialized);
[[nodiscard]] QByteArray SealVaultRecord(
	const SecureBytes &vaultKey,
	const QString &storageKey,
	bool requireUserPresence,
	bytes::const_span secret);
[[nodiscard]] std::optional<VaultSecretRecord> OpenVaultRecord(
	const SecureBytes &vaultKey,
	const QString &storageKey,
	const QByteArray &serialized);

// Write A stages the new wrap beside the committed one, the read-back proves
// it opens the vault key, write B commits it alone at the bumped generation;
// every intermediate crash state leaves the old wrap working and the next
// header read drops the staged one by generation.
[[nodiscard]] VaultTransitionResult TransitionVaultWrap(
	Storage::Account &local,
	VaultHeader &header,
	const SecureBytes &vaultKey,
	VaultPreparedWrap next);

// Pre-vault plain-layout records are development state: every custody record
// whose secret value is not a vault record goes together with that value.
// Returns the count dropped; the store is rewritten when it is not zero.
int DropPreVaultCustody(Storage::Account &local, CustodyStore &store);

} // namespace Wallet
