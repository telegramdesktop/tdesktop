/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_vault.h"

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Wallet {

// One live unlock, shared by every engine call of one logical operation.
// Copying the handle shares the same VaultGrant, so the runtime counts one
// grant for a flow that spans several session calls.
using VaultAuthorization = std::shared_ptr<VaultGrant>;

// What the install ladder answers a CustodyInstallRequest's ready with.
// created is true when the account had no vault and the store that follows
// is the one creating it, which is what lets a failed store drop the header
// again.
struct CustodyInstall {
	VaultAuthorization grant;
	bool created = false;
};

// What a store that ran the custody-install ladder actually did. Cancelled
// is the dismissed protection chooser: it stored nothing and has nothing to
// state. WriteFailed is a custody write that did not reach disk after a
// successful import - the same arm deletes the stored secret and drops a
// header this store created - and it is stated, because nothing the user
// did caused it.
enum class CustodyOutcome {
	Installed,
	Cancelled,
	WriteFailed,
};

enum class CustodyResetResult {
	Done,
	Refused,
	Failed,
};

// What the session hands the install ladder. ready is answered exactly
// once, and an empty answer is a cancellation: the session stores nothing.
// resetUnusableVault is supplied by a restore or an import that may run
// over a vault this process cannot open (Session::vaultKeyUnusable()), and
// it is the one authority for destroying that vault: called after the user
// has confirmed the deletion, it re-resolves the session, re-verifies the
// target identity, rereads the header and the flag, invalidates the runtime
// and resets that account's vault together with the custody secrets it
// names. Done means the vault is gone and the ordinary absent-vault install
// may arm; Refused means there is nothing to reset any more or the target
// has moved, and is silent by contract; Failed means required reset work
// did not reach disk. An installer over a header that reads refuses when
// the request carries no reset.
struct CustodyInstallRequest {
	Fn<void(CustodyInstall)> ready;
	Fn<CustodyResetResult()> resetUnusableVault;
};

// The ladder itself, invoked by the session after the words are in hand and
// immediately before it stores them.
using CustodyInstaller = Fn<void(CustodyInstallRequest request)>;

// What every protected-key entry point on Wallet::Session takes. A call that
// carries neither term fails typed before the engine is reached.
struct KeyAuthorization {
	VaultAuthorization grant;
	CustodyInstaller install;

	[[nodiscard]] bool valid() const;
};

struct VaultUnlockArgs {
	std::shared_ptr<Main::SessionShow> show;
	bool mayInstall = false;
	Fn<void(KeyAuthorization)> done;
};

// Acquires the vault unlock a protected read needs, routed by the committed
// wrap's kind. An answer that is not valid() is the cancelled or unavailable
// outcome, and the caller states nothing: whatever surface was needed - the
// unlock box, the unavailable toast - has already been shown here.
void AcquireVaultUnlock(VaultUnlockArgs args);

[[nodiscard]] CustodyInstaller MakeCustodyInstaller(
	std::shared_ptr<Main::SessionShow> show);

[[nodiscard]] QString VaultLockedText(not_null<Main::Session*> session);

// The one enforcement point of "a passcode exists only while it protects
// something": drops the passcode when, at the moment of acting,
// hasPasscode() is true, appLockEnabled() is false and no signed-in
// account's vault is passcode-wrapped. The removal is
// Storage::Domain::clearPasscodeAfterReset(), which asks for no proof and
// is safe exactly because !appLockEnabled() is the verified reading: the
// committed open wrap was proved to open the local key, so nothing openable
// is lost. That write is checked; hasPasscode() is read back before the
// settings and Application::localPasscodeChanged() follow-ups run, and a
// failed write is simply tried again by the next site. Eligibility is never
// carried from an earlier frame. Called from exactly three places - the end
// of Wallet::Session::notifyKeyProtectionChanged(),
// Main::Domain::removeRedundantAccounts() and Main::Domain::startWith() -
// and never from a localPasscodeChanged() subscriber: a wallet-only
// passcode is created in its role in one step, while the vault that will
// depend on it is only armed afterwards and sealed later by the engine
// store, so a subscriber acting on that step's notification would delete
// the passcode before any vault depends on it. The restore that resets a
// vault this process cannot open defers this reconciliation to the
// install's terminal exit for the same reason.
void DropUnusedPasscode();

// Vault: the typed passcode must open the vault's own passcode wrap.
// KeyData: the typed bytes are verified against key_data only and answered
// as scoped SecureBytes, while no vault is opened, no grant is minted,
// nothing is armed and nothing is retained; whoever takes the bytes opens
// each vault where its key is actually needed.
enum class WalletPasscodeCheck {
	Vault,
	KeyData,
};

// grant is filled for Vault, on the vault the typed passcode unlocked;
// passcode for KeyData alone, bytes that prove key_data and no vault.
struct WalletPasscodeGate {
	VaultGrant grant;
	SecureBytes passcode;
};

struct WalletPasscodeBoxArgs {
	std::shared_ptr<Main::SessionShow> show;
	WalletPasscodeCheck check = WalletPasscodeCheck::Vault;
	Fn<void(WalletPasscodeGate)> passed;
	Fn<void()> cancelled;
};

void WalletPasscodeBox(
	not_null<Ui::GenericBox*> box,
	WalletPasscodeBoxArgs args);

} // namespace Wallet
