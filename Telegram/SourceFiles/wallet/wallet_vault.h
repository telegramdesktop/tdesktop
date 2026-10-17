/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "base/flat_map.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "storage/details/storage_file_utilities.h"

#include <mutex>

namespace Main {
class Account;
class Domain;
class Session;
} // namespace Main

namespace Storage {
class Domain;
} // namespace Storage

namespace Wallet {

struct CustodyStore;
class VaultRuntime;

inline constexpr auto kVaultKeySize = 32;
inline constexpr auto kVaultKeyIdSize = 16;
inline constexpr auto kVaultNonceSize = 12;
inline constexpr auto kVaultTagSize = 16;
inline constexpr auto kVaultSaltSize = 32;
inline constexpr auto kVaultOpenSecretSize = 32;
inline constexpr auto kVaultRetention = 15 * 60 * crl::time(1000);

// Kinds from kFirstReservedVaultKind up are reserved for the hardware
// providers. TouchId and WindowsHello are the two this build defines. macOS
// registers the TouchId provider from platform/mac/wallet_protection_mac.mm:
// the Secure Enclave key blob together with the ECIES-sealed wrap key ride
// in the wrap's openSecret. Windows registers the WindowsHello provider from
// platform/win/wallet_protection_win.cpp: the Hello credential id and the
// signed challenge ride in the wrap's openSecret, and the wrap key is HKDF
// over the credential's signature. A reserved kind this build does not
// define reads as Unsupported.
enum class VaultKind : quint32 {
	Passcode = 1,
	Open = 2,
	TouchId = 3,
	WindowsHello = 4,
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
	Storage::details::PasscodeKdf kdf;
	QByteArray salt;
	QByteArray openSecret;
	QByteArray blob;
};

struct DeviceKeyringEntry {
	QByteArray keyId;
	uint64 accountId = 0;
	QByteArray nonce;
	QByteArray sealed;
};

struct DeviceKeyring {
	VaultWrap wrap;
	std::vector<DeviceKeyringEntry> entries;
};

struct KeyringReading {
	enum class State {
		Absent,
		Broken,
		Unsupported,
		Read,
	};

	State state = State::Absent;
	DeviceKeyring keyring;
};

// Never serialized - the keyring carries the VaultWrap alone - and an armed
// one is retired by VaultRuntime's clear() on localPasscodeChanged().
struct VaultPreparedWrap {
	VaultWrap wrap;
	SecureBytes wrapKey;
};

struct VaultSecretRecord {
	bool requireUserPresence = false;
	SecureBytes bytes;
};

struct DeviceKeyringRotation {
	DeviceKeyring keyring;
	SecureBytes key;
};

// A grant belongs to one account and one logical operation. Clear removes all
// grant ids, so old handles stay inert even after a later unlock. An install
// rotation preserves only its owning id under the new epoch; keeping that
// handle usable never revives the unrelated grants the rotation retired.
// Releasing the last live grant cleanses D unless retention is still active.
class VaultGrant final {
public:
	VaultGrant() = default;
	VaultGrant(VaultGrant &&other) noexcept;
	VaultGrant &operator=(VaultGrant &&other) noexcept;
	~VaultGrant();

	[[nodiscard]] bool valid() const;

private:
	friend class VaultRuntime;
	VaultGrant(std::shared_ptr<VaultRuntime> runtime, uint64 id);

	std::shared_ptr<VaultRuntime> _runtime;
	uint64 _id = 0;

};

// The domain's unlocked D, grants and retention share one mutex. Storage and
// liveness queries run on main; key and epoch readers may run on any thread.
// Timer and rpl operations stay outside the mutex. Keys handed to a host
// operation are move-only SecureBytes; no unwrapped K is retained here.
// Membership comes from actual record headers, independently of custody
// metadata and of orphan entries left by a failed record write.
class VaultRuntime final
	: public std::enable_shared_from_this<VaultRuntime> {
public:
	explicit VaultRuntime(Main::Domain &domain);
	~VaultRuntime();

	[[nodiscard]] KeyringReading reading() const;
	[[nodiscard]] bool unlockOpen();
	void registerAccount(Main::Session &session);
	void unregisterAccount(uint64 accountId);
	void recordStored(
		uint64 accountId,
		const QString &storageKey,
		const QByteArray &keyId);
	void recordRemoved(uint64 accountId, const QString &storageKey);
	[[nodiscard]] bool hasLiveEntries() const;
	[[nodiscard]] bool hasLiveEntries(const DeviceKeyring &keyring) const;
	[[nodiscard]] DeviceKeyring liveKeyring(DeviceKeyring keyring) const;

	// Async factor acquisition captures the epoch before it starts. The
	// caller also checks that the current wrap is the one it opened before
	// handing over D, never its same-sized wrap key. The runtime rejects a
	// result whose clear happened while the provider or derivation ran.
	[[nodiscard]] quint32 clearEpoch() const;
	[[nodiscard]] bool current(uint64 accountId, quint32 epoch) const;
	[[nodiscard]] bool unlockWith(SecureBytes key, quint32 epoch);

	[[nodiscard]] VaultGrant arm(uint64 accountId, VaultPreparedWrap policy);
	[[nodiscard]] VaultGrant grant(uint64 accountId);
	void setRetention(bool fifteenMinutes);
	// Also drops the key at once when no grant still holds it.
	void endRetention();
	[[nodiscard]] bool retained() const;
	[[nodiscard]] bool unlocked() const;
	void clear();

	[[nodiscard]] bool unusable() const;
	void setUnusable(bool unusable);
	[[nodiscard]] rpl::producer<> protectionChanges() const;
	[[nodiscard]] rpl::producer<> granted() const;
	void notifyProtectionChanged(bool stillUnusable = false);

	struct StoreAuthority {
		std::optional<SecureBytes> key;
		std::optional<VaultPreparedWrap> policy;
		uint64 owner = 0;
		quint32 epoch = 0;
	};
	[[nodiscard]] std::optional<SecureBytes> keyForRead(
		uint64 accountId,
		quint32 epoch);
	[[nodiscard]] StoreAuthority authorityForStore(
		uint64 accountId,
		quint32 epoch);

	// Called inside the serialized store after the ring commits, including
	// when the subsequent record write fails. The new D belongs only to
	// the still-valid installer; all other grants and retention are retired.
	// A clear that already ended that ownership cannot be undone here.
	[[nodiscard]] std::optional<quint32> adoptCommitted(
		SecureBytes key,
		quint32 epoch,
		uint64 owner);

private:
	friend class VaultGrant;
	struct AccountRecords {
		base::weak_ptr<Main::Account> account;
		base::flat_map<QString, QByteArray> records;
	};
	struct ArmedPolicy {
		VaultPreparedWrap prepared;
		base::weak_ptr<Main::Account> account;
		uint64 accountId = 0;
		uint64 owner = 0;
	};

	[[nodiscard]] bool grantValid(uint64 id) const;
	[[nodiscard]] bool hasGrantLocked(uint64 accountId) const;
	[[nodiscard]] bool entryLiveLocked(const DeviceKeyringEntry &entry) const;
	void release(uint64 id);
	void discard(std::optional<ArmedPolicy> policy);

	const base::weak_ptr<Main::Domain> _domain;
	mutable std::mutex _mutex;
	base::flat_map<uint64, AccountRecords> _accounts;
	base::flat_map<uint64, uint64> _grants;
	std::optional<SecureBytes> _key;
	std::optional<ArmedPolicy> _policy;
	uint64 _nextGrantId = 0;
	quint32 _clearEpoch = 0;
	crl::time _retainUntil = 0;
	bool _unusable = false;
	base::Timer _retention;
	rpl::event_stream<> _protectionChanges;
	rpl::event_stream<> _granted;
	rpl::lifetime _lifetime;

};

[[nodiscard]] QString VaultSecretStorageKey(const QString &secretRef);

[[nodiscard]] KeyringReading ParseDeviceKeyring(const QByteArray &serialized);
[[nodiscard]] QByteArray SerializeDeviceKeyring(const DeviceKeyring &keyring);
[[nodiscard]] KeyringReading ReadDeviceKeyring(Storage::Domain &local);
[[nodiscard]] bool WriteDeviceKeyring(
	Storage::Domain &local,
	const DeviceKeyring &keyring);

[[nodiscard]] std::optional<VaultPreparedWrap> PrepareVaultPasscodeWrap(
	const QByteArray &passcode);
[[nodiscard]] std::optional<VaultPreparedWrap> PrepareVaultOpenWrap();
[[nodiscard]] std::optional<SecureBytes> DeriveVaultWrapKey(
	const VaultWrap &wrap,
	const QByteArray &passcode);
// HKDF-SHA256 over OpenSSL, empty on failure; the vault's own wrap keys and
// the Windows Hello provider's derive through it.
[[nodiscard]] SecureBytes HkdfSha256(
	bytes::const_span ikm,
	bytes::const_span salt,
	bytes::const_span info,
	int size);
[[nodiscard]] QByteArray WrapVaultKey(
	const SecureBytes &vaultKey,
	const VaultWrap &wrap,
	const SecureBytes &wrapKey);
[[nodiscard]] std::optional<SecureBytes> UnwrapVaultKey(
	const VaultWrap &wrap,
	const SecureBytes &wrapKey);

[[nodiscard]] std::optional<DeviceKeyringEntry> SealDeviceKeyringEntry(
	const SecureBytes &deviceKey,
	const QByteArray &keyId,
	uint64 accountId,
	const SecureBytes &key);
[[nodiscard]] std::optional<SecureBytes> OpenDeviceKeyringEntry(
	const SecureBytes &deviceKey,
	const DeviceKeyringEntry &entry);
[[nodiscard]] std::optional<DeviceKeyringRotation> RotateDeviceKeyring(
	const DeviceKeyring &current,
	const SecureBytes &deviceKey,
	const VaultPreparedWrap &next);

[[nodiscard]] bool IsVaultRecord(const QByteArray &serialized);
[[nodiscard]] std::optional<QByteArray> ReadVaultRecordKeyId(
	const QByteArray &serialized);
[[nodiscard]] QByteArray SealVaultRecord(
	const SecureBytes &key,
	const QString &storageKey,
	const QByteArray &keyId,
	bool requireUserPresence,
	bytes::const_span secret);
[[nodiscard]] std::optional<VaultSecretRecord> OpenVaultRecord(
	const SecureBytes &key,
	const QString &storageKey,
	const QByteArray &keyId,
	const QByteArray &serialized);

// Records from previous development formats go together with their custody
// metadata. The supported keyId record format is the only retained shape.
// Returns the count dropped; the store is rewritten when it is not zero.
int DropPreVaultCustody(Storage::Account &local, CustodyStore &store);

// The confirmed domain reset removes every signed-in account's actual
// secret records and custody metadata, then the ring and its credential.
// The caller invalidates private authority and awaits every client stop
// before invoking this synchronous storage step. Missing records are inert;
// false reports a keyring file which could not be removed.
[[nodiscard]] bool ResetVaultAndCustody(Main::Domain &domain);

} // namespace Wallet
