/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "wallet/wallet_vault.h"

namespace Main {
class Account;
class SessionShow;
} // namespace Main

namespace Storage {
class Account;
class Domain;
class PasscodeDerivation;
enum class SetPasscodeResult : uchar;
} // namespace Storage

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Wallet {

// The generated wallet_engine::ProtectedSecretHostErrorKind is deliberately
// not reused here: forward-declaring a scoped enum does not make its
// enumerators nameable, so the defaulted error fields below would not
// compile, and the generated engine header does not belong in a UI header.
// The correspondence, for the mapping the gate task writes inside
// wallet_engine.cpp where that enum is in scope, is Cancelled - kCancelled,
// AuthenticationFailed - kAuthenticationFailed, Unavailable - kUnavailable,
// Absent - kNotFound and Corrupt - kOther.
enum class ProtectionError : uchar {
	None,
	Cancelled,
	AuthenticationFailed,
	Unavailable,
	Corrupt,
	Absent,
};

// The Unavailable default fails closed: a provider that answers without
// setting the field cannot be read as a success.
struct ProtectionEnrollResult {
	std::optional<VaultPreparedWrap> wrap;
	ProtectionError error = ProtectionError::Unavailable;
};

struct ProtectionUnwrapResult {
	std::optional<SecureBytes> key;
	ProtectionError error = ProtectionError::Unavailable;
};

// One platform's way of protecting the vault key: it enrolls a wrap, opens
// one back and retires it. Every method is called on the main thread and
// answers asynchronously through its done callback.
//
// available() must emit its current value synchronously on subscription: the
// chooser box takes one value when it opens and unsubscribes at once, so a
// producer that only answers later reads as unavailable and its row simply
// does not appear. base::SystemUnlockStatus() in
// lib_base/base/system_unlock.h is the right source for both this and the
// user-presence prompt, but on Windows it must not be handed back directly:
// there it answers from a static rpl::variable left default-constructed -
// known = false, available = false - until an asynchronous availability
// check writes it, so the first open of a session would read false. Prime it
// instead, for example into the provider's own rpl::variable seeded before
// the box can open; macOS assigns before returning and is unaffected.
//
// enroll() is handed no vault key: only the wrap key it answers with is
// contractual, because every consumer re-seals the vault key under it and
// overwrites the wrap's own blob, so a provider must not seal anything
// against a key it invents itself.
//
// Only enroll(), unwrap() and remove() are new ground - nothing in lib_base
// holds per-vault key material.
class ProtectionProvider {
public:
	virtual ~ProtectionProvider() = default;

	[[nodiscard]] virtual VaultKind kind() const = 0;
	[[nodiscard]] virtual rpl::producer<bool> available() const = 0;
	[[nodiscard]] virtual rpl::producer<QString> title() const = 0;
	[[nodiscard]] virtual rpl::producer<QString> description() const = 0;
	[[nodiscard]] virtual rpl::producer<QString> binding() const = 0;

	virtual void enroll(
		not_null<Storage::Account*> local,
		Fn<void(ProtectionEnrollResult)> done) = 0;
	virtual void unwrap(
		not_null<Storage::Account*> local,
		VaultWrap wrap,
		Fn<void(ProtectionUnwrapResult)> done) = 0;
	virtual void remove(
		not_null<Storage::Account*> local,
		VaultWrap wrap,
		Fn<void(ProtectionError)> done) = 0;

};

// The registry is empty on every platform in this build: nothing registers a
// provider anywhere. The Touch ID and Windows Hello tasks each register one
// from their own platform file, and each of them must also define its kind
// in wallet_vault.cpp's WrapIsWellFormed and lift it out of the
// reserved-kind branch of ParseVaultHeader in the same commit — otherwise a
// header written under that kind afterwards reads as Unsupported.
void RegisterProtectionProvider(std::unique_ptr<ProtectionProvider> provider);

[[nodiscard]] auto ProtectionProviders()
-> const std::vector<std::unique_ptr<ProtectionProvider>> &;

[[nodiscard]] ProtectionProvider *ProtectionProviderFor(VaultKind kind);

[[nodiscard]] rpl::producer<QString> ProtectionLabel(
	VaultKind kind,
	bool appLockEnabled);

// Install: the account has no vault yet and the box hands back an armed
// creation policy. Switch: the key is on this device and the box moves one
// vault from one kind to another. Removal: one choice is applied to every
// dependent vault the chooser lists, as the local passcode goes away.
enum class KeyProtectionMode {
	Install,
	Switch,
	Removal,
};

// Move-only, because VaultGrant is. cancelled = true is the safe default, so
// a caller that never hears back persists nothing. In Install, kind is the
// armed policy's kind and grant scopes the store that consumes it; the policy
// itself goes into the account's VaultRuntime through arm() and is
// deliberately not duplicated here. changed lists the accounts a Removal
// transitioned, in the order they were done, and is weak for the reason
// KeyProtectionArgs::accounts below is: Main::Domain can free one between
// the transition that recorded it and the caller that reads this. A freed
// account leaves a null entry rather than no entry, so size() stays the
// number of vaults the walk really moved, and a reader that wants the
// account takes get() and skips null instead of dereferencing.
struct KeyProtectionResult {
	bool cancelled = true;
	bool failed = false;
	VaultKind kind = VaultKind::Passcode;
	VaultGrant grant;
	std::vector<base::weak_ptr<Main::Account>> changed;
};

// accounts is Removal-only: the initial seed is validated before the gate,
// then refreshed after it and after any accepted exposure warning. The
// displayed chooser freezes that weak list for its walk. Main::Domain can
// free an account while the gate or chooser waits - removeRedundantAccounts()
// runs whenever a session disappears. Every reader skips a gone account, so
// raw pointers from an enumeration never outlive the receiving frame.
struct KeyProtectionArgs {
	KeyProtectionMode mode = KeyProtectionMode::Switch;
	std::vector<base::weak_ptr<Main::Account>> accounts;
	Fn<void(KeyProtectionResult)> done;
};

void ShowKeyProtectionBox(
	std::shared_ptr<Main::SessionShow> show,
	KeyProtectionArgs args);

// Which accounts hold a vault whose committed wrap is of that kind. Three
// invariants a reviewer must be able to check by reading the body: it goes
// through ReadVaultHeader() alone, so it never writes and never opens a
// vault; an account whose header does not read - Absent, Broken or
// Unsupported, which is every reserved and hardware kind in this build - is
// in neither list; and the returned pointers are valid only for the frame
// that receives them, because Main::Domain owns the accounts and one can be
// logged out and dropped. Never keep these vectors in an rpl::variable, a
// state struct or a lambda that outlives the call - re-enumerate instead.
struct VaultDependents {
	std::vector<not_null<Main::Account*>> passcodeWrapped;
	std::vector<not_null<Main::Account*>> open;
};

[[nodiscard]] VaultDependents CollectVaultDependents();

enum class VaultPasscodeChangeResult {
	Done,
	VaultFailed,
	NeedsVerification,
	PasscodeFailed,
	CommitFailed,
};

// Prepare snapshots the current dependents on the main thread and owns all
// typed bytes and key material. Move the job through Storage::DeriveOnWorker
// for exactly one run(), which touches values only, then apply it once on the
// main thread. apply consumes the batch and destroys its keys before returning;
// its synchronous writer is invoked at most once and is never retained.
class VaultPasscodeChange final {
public:
	[[nodiscard]] static std::optional<VaultPasscodeChange> Prepare(
		const Storage::Domain &local,
		SecureBytes oldPasscode,
		const QByteArray &newPasscode);
	VaultPasscodeChange(VaultPasscodeChange &&other) noexcept;
	VaultPasscodeChange &operator=(VaultPasscodeChange &&other) noexcept;
	~VaultPasscodeChange();

	void run();
	[[nodiscard]] VaultPasscodeChangeResult apply(
		Fn<Storage::SetPasscodeResult(Storage::PasscodeDerivation)> writer);

private:
	struct Data;
	explicit VaultPasscodeChange(std::unique_ptr<Data> data);

	std::unique_ptr<Data> _data;

};

} // namespace Wallet
