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
// live grant cleanses the key unless the retention window is open. A grant
// carries the clear count it was minted under, and a clear retires every
// earlier grant - valid() is false, it is not counted and its release is a
// no-op - so a flow's grant is inert after a clear wherever it travels. The
// runtime's _grants tally therefore means "live grants minted since the last
// clear", which keeps release()'s last-release cleanse and its exactly-once
// contract without an assertion that a count zeroed by the clear could trip.
class VaultGrant final {
public:
	VaultGrant() = default;
	VaultGrant(VaultGrant &&other) noexcept;
	VaultGrant &operator=(VaultGrant &&other) noexcept;
	~VaultGrant();

	[[nodiscard]] bool valid() const;

private:
	friend class VaultRuntime;
	VaultGrant(std::shared_ptr<VaultRuntime> runtime, quint32 epoch);

	std::shared_ptr<VaultRuntime> _runtime;
	quint32 _epoch = 0;

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

	// A hardware provider's unwrap() answers with the vault key itself,
	// which the two unlocks above never take: they derive it. This is the
	// only way to install one that was opened elsewhere. That answer arrives
	// many main-thread turns after the ask, so a clear trigger can land
	// inside the provider's prompt; the caller reads the epoch before it
	// asks and hands it back here, and a key opened before that clear is
	// refused instead of quietly unlocking a vault the user has just locked.
	[[nodiscard]] quint32 clearEpoch() const;
	[[nodiscard]] bool unlockWith(SecureBytes key, quint32 epoch);

	void arm(VaultPreparedWrap policy);
	[[nodiscard]] VaultGrant grant();
	void setRetention(bool fifteenMinutes);
	[[nodiscard]] bool retained() const;
	[[nodiscard]] bool unlocked() const;
	void clear();

	struct StoreAuthority {
		std::optional<SecureBytes> key;
		std::optional<VaultPreparedWrap> policy;
		quint32 epoch = 0;
	};
	[[nodiscard]] std::optional<SecureBytes> keyForRead();
	[[nodiscard]] StoreAuthority authorityForStore();

	// The store that creates the vault writes the header and the record on
	// the main thread while the worker waits, so a clear trigger or the
	// last grant's release can land inside that window. The created key is
	// installed only while the authority's epoch is still current and the
	// key would still have an owner; otherwise it is dropped and cleansed -
	// the header is already on disk, so the next reveal unlocks through the
	// box.
	void adoptCreated(SecureBytes key, quint32 epoch);

private:
	friend class VaultGrant;
	void release(quint32 epoch);

	mutable std::mutex _mutex;
	std::optional<SecureBytes> _key;
	std::optional<VaultPreparedWrap> _policy;
	int _grants = 0;
	quint32 _clearEpoch = 0;
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
	quint32 generation,
	bool requireUserPresence,
	bytes::const_span secret);
[[nodiscard]] std::optional<VaultSecretRecord> OpenVaultRecord(
	const SecureBytes &vaultKey,
	const QString &storageKey,
	const QByteArray &serialized);

// A wrap change mints a fresh vault key and re-seals every custody-named
// record under it, so the retired wrap stops opening the data and not only
// the header. The invariant every write boundary keeps: the header on disk
// holds a wrap at its committed generation, and every custody-named record
// holds an entry sealed under the key that wrap opens. Hence (i) an entry is
// written under a key only after a wrap opening that key is on disk - write
// A stages the new wrap beside the committed one and proves it, then each
// record gains a second entry under the new key beside its old one; (ii)
// committed moves only after every record carries an entry at the new
// generation - write B, which keeps the retiring wrap beside the new one;
// (iii) an entry outside the committed generation is removed only while
// committed already points elsewhere - the strip after B; (iv) a wrap leaves
// the header only after no record depends on it alone - write C, after the
// strip. A crash anywhere leaves a header with a wrap outside its committed
// generation, and the next reconciling read strips the records to that
// generation and rewrites the header alone: a rollback before B, a completion
// after it. While the custody store does not read, or while a custody-named
// record carries no entry at the committed generation, the header stays dirty
// and every read keeps using its committed wrap: the wrap such a record still
// needs stays in the header until a repair, rather than being written away
// with the record left unopenable. The commit half clears the runtime right
// after write B, because the key it holds is the one just retired; an account
// without a session has no runtime and passes nullptr. The commit half also
// reads the raw header back before write B and refuses, writing nothing,
// unless the disk still carries the staged wrap beside the unchanged committed
// generation, and asks the same records the strip will ask, refusing when one
// carries no entry at the staged generation, so both halves of the rule hold
// by the primitive's own checks when something reconciled between the halves.
// The two halves are public so that a caller changing one passcode across
// several stores can hold every vault staged while another store's write runs
// and commit them only once it succeeded; TransitionVaultWrap is exactly
// their composition.
//
// Refused writes nothing. Every stage failure leaves the caller header
// unchanged: WriteFailed means sealing or a checked write failed, VerifyFailed
// means read-back proof failed. A failure after write A attempts to strip the
// new record entries and restore the prior header; failed rollback can leave
// a staged header on disk, with the old committed wrap and records usable.
// Done installs both wraps in the caller header at the old committed generation
// after the new wrap and both entries of every re-sealed record are proved.
[[nodiscard]] VaultTransitionResult StageVaultWrap(
	Storage::Account &local,
	VaultHeader &header,
	const SecureBytes &vaultKey,
	VaultPreparedWrap next);

// Invalid input, a disk header without the same stage and committed
// generation, or a custody-named record with no entry at the staged
// generation returns false before write B. A failed checked write B also
// returns false; these exits leave the caller header and runtime unchanged.
// After B, committed has advanced while both wraps remain on disk, and the
// runtime's retired key is cleared.
// Stripping old record entries and writing C are best-effort: true means B
// succeeded, even if the next reconciling read must finish that cleanup. The
// caller header holds only the new wrap at the advanced generation either way.
// ReadVaultHeader filters other generations in its copy; ReconcileVaultHeader
// also tries to strip their record entries and persist the settled header,
// leaving it dirty while custody is unreadable or cleanup fails.
[[nodiscard]] bool CommitStagedVaultWrap(
	Storage::Account &local,
	VaultHeader &header,
	VaultRuntime *runtime);

// Composes StageVaultWrap and CommitStagedVaultWrap, forwarding stage failures
// and reporting a failed commit as WriteFailed. That result does not promise
// the pre-stage single-wrap caller header: a failed commit retains both wraps
// at the old committed generation, with the old wrap still opening the records.
[[nodiscard]] VaultTransitionResult TransitionVaultWrap(
	Storage::Account &local,
	VaultHeader &header,
	const SecureBytes &vaultKey,
	VaultPreparedWrap next,
	VaultRuntime *runtime);

// Pre-vault plain-layout records are development state: every custody record
// whose secret value is not a vault record goes together with that value.
// Returns the count dropped; the store is rewritten when it is not zero.
int DropPreVaultCustody(Storage::Account &local, CustodyStore &store);

} // namespace Wallet
