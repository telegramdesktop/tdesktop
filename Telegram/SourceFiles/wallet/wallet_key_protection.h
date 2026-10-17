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
class PasscodeVerification;
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
// holds the device keyring's key material.
class ProtectionProvider {
public:
	virtual ~ProtectionProvider() = default;

	[[nodiscard]] virtual VaultKind kind() const = 0;
	[[nodiscard]] virtual rpl::producer<bool> available() const = 0;
	[[nodiscard]] virtual rpl::producer<QString> title() const = 0;
	[[nodiscard]] virtual rpl::producer<QString> description() const = 0;

	// The noun phrase the key-location line composes after "protected by".
	// Defaults to title().
	[[nodiscard]] virtual rpl::producer<QString> label() const;

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

// macOS registers the Touch ID provider from Platform::start() and Windows
// the Windows Hello one from WindowsIntegration::init(): its availability
// check completes asynchronously and needs the main queue, which
// Platform::start() precedes. Both run before any chooser can open. A
// provider's kind must be defined in wallet_vault.cpp - named in
// WrapIsWellFormed and in IsDefinedVaultKind, which is what lifts it out of
// ParseDeviceKeyring's Unsupported verdict - in the same commit that registers
// the provider, otherwise a keyring written under that kind afterwards reads
// as Unsupported.
void RegisterProtectionProvider(std::unique_ptr<ProtectionProvider> provider);

[[nodiscard]] auto ProtectionProviders()
-> const std::vector<std::unique_ptr<ProtectionProvider>> &;

[[nodiscard]] ProtectionProvider *ProtectionProviderFor(VaultKind kind);

// Install arms the first store's policy when no live secured ring exists.
// Change applies to the whole device. Removal keeps the Passcode row's
// wallet-only meaning while the app passcode is being disabled.
enum class KeyProtectionMode {
	Install,
	Change,
	Removal,
};

// Install carries the owner grant for its first store. A passcode created
// by that chooser may have cleared the caller's epoch before arm(); only
// that explicit creation's starting epoch is returned for a scoped handoff.
// An unrelated clear invalidates the grant and cannot use this handoff.
struct KeyProtectionResult {
	bool cancelled = true;
	bool failed = false;
	VaultKind kind = VaultKind::Passcode;
	VaultGrant grant;
	std::optional<quint32> passcodeCreatedFromEpoch;
};

// The weak account list supplies display names in Removal only.
struct KeyProtectionArgs {
	KeyProtectionMode mode = KeyProtectionMode::Change;
	std::vector<base::weak_ptr<Main::Account>> accounts;
	Fn<bool(quint32 previousEpoch, quint32 epoch)> passcodeCreated;
	// Called only after confirmation. Completion waits for the whole domain
	// reset; false closes without arming. The optional epoch identifies this
	// chooser's deliberate passcode creation, already revalidated by it.
	Fn<void(std::optional<quint32>, Fn<void(bool)>)> resetUnusableVault;
	Fn<void(KeyProtectionResult)> done;
};

// WHY: a key still held when the previous box closed repeats into this one,
// so an auto-repeated Enter is not the deliberate press this acts on.
void SubmitBoxOnEnter(not_null<Ui::GenericBox*> box, Fn<void()> submit);

// The layer a provider's system sheet is raised behind: the provider's own
// title and mark, a spinner while the sheet is up, and Retry beside Cancel
// once the user dismissed it. The caller drives `asking`, adds any further
// rows after this call and owns what the answers mean.
void SetupSystemPromptBox(
	not_null<Ui::GenericBox*> box,
	not_null<ProtectionProvider*> provider,
	rpl::producer<bool> asking,
	Fn<void()> retry);

// Optional bytes already verified against key_data. Otherwise the chooser
// acquires them lazily, only for a passcode choice or passcode change.
void ShowKeyProtectionBox(
	std::shared_ptr<Main::SessionShow> show,
	KeyProtectionArgs args,
	SecureBytes verified = SecureBytes());

[[nodiscard]] bool CurrentVaultWrap(
	const VaultRuntime &vault,
	const VaultWrap &wrap);

// Mints the proof a key_data write asks for from the bytes the caller holds,
// with the derivation on the worker. The guard sits in front of the mint, so
// a caller that is gone by the time its derivation answers mints nothing and
// is never called back; every caller spends the proof in the same callback
// that receives it, so no token waits across turns for a later write. The
// typed copy taken here is cleansed before this returns; the derivation
// carries its own and cleanses it on the worker.
void MintVerificationOnWorker(
	not_null<QObject*> guard,
	const SecureBytes &passcode,
	Fn<void(std::optional<Storage::PasscodeVerification>)> done);

// Only entries named by actual records of signed-in accounts participate.
// Stale entries and an empty keyring do not keep a passcode dependency alive.
[[nodiscard]] std::optional<VaultKind> LiveKeyProtection();

// CommitFailed changed key_data but could not replace the keyring. All old
// grants remain cleared; the old factor can still open the persisted ring.
// Other failures leave both stores unchanged.
enum class LocalPasscodeChangeResult {
	Done,
	CommitFailed,
	Stale,
	VaultFailed,
	PasscodeFailed,
};

// Derives the checked key_data inputs and, for live Passcode protection,
// D and its new wrap on the worker. The main-thread commit revalidates the
// factor and epoch, rotates the current live map, then writes key_data and
// the ring synchronously. done is skipped if the guard was destroyed, but
// that cannot interrupt a write pair which already committed key_data.
void ChangeLocalPasscode(
	not_null<QObject*> guard,
	const SecureBytes &current,
	const QByteArray &updated,
	Fn<void(LocalPasscodeChangeResult)> done);

} // namespace Wallet
