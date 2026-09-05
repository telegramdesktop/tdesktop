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

// What the deferred install ladder answers with. created is true when the
// account had no vault and the store that follows is the one creating it,
// which is what lets a failed store drop the header again.
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

// The ladder itself, invoked by the session after the words are in hand and
// immediately before it stores them. An empty answer is a cancellation and
// the session stores nothing.
using CustodyInstaller = Fn<void(Fn<void(CustodyInstall)> ready)>;

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

// The forgot-passcode path's last step, for the run that could not finish
// it. DropForgottenPasscode() records the removal as owed once every
// dependent vault is gone and before key_data is written, so a checked
// write that did not reach the disk - and a crash in the same window - is
// finished here at the next start instead of leaving a passcode nobody can
// produce standing over nothing it can open. Called once from
// Main::Domain::start(), after the accounts are up. It shows nothing,
// states nothing and logs out nobody, and it destroys no wallet key, vault
// or custody record: that destruction already happened, and this only
// finishes the passcode's own removal.
//
// openedWithoutPasscode is the fact this start established, and the caller
// passes what it did rather than what any stored state says: true only
// when Main::Domain::start() opened the local key with the empty passcode.
// The launch lock's absence is otherwise read from a key_data field that
// nothing validates, so this parameter is what makes that absence a fact
// about this install rather than about a file - see the definition.
void FinishForgottenPasscodeClear(bool openedWithoutPasscode);

// Vault: the typed passcode must open the vault's own passcode wrap.
// KeyDataAndVault: it is checked against key_data and, when the vault is
// passcode-wrapped, must open that wrap too, while nothing is armed,
// nothing is unlocked and nothing is retained.
enum class WalletPasscodeCheck {
	Vault,
	KeyDataAndVault,
};

// grant is filled for Vault, passcode only for KeyDataAndVault.
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
