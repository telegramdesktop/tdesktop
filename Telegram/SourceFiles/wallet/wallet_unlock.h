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

// The creation handoff identifies only this install's deliberate key_data
// change. Its still-valid owner grant proves no later clear intervened.
// A caller may restamp its unchanged scope from that starting epoch, never
// revive a cancelled scope or skip its identity and record checks.
struct CustodyInstall {
	VaultAuthorization grant;
	std::optional<quint32> passcodeCreatedFromEpoch;
};

// A cancelled installer writes nothing. WriteFailed means the imported
// record could not be kept; cleanup removes only that flow's own secrets.
// A committed shared keyring survives every install failure, including a
// failure of the record write that follows its keyring write.
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

// A confirmed reset revalidates the initiating flow, clears shared private
// authority, waits for every signed-in client to stop and removes every
// account's secrets and custody metadata plus the ring. The ready answer is
// deferred across that reset even if the chooser closes, so the flow settles
// and reconciles its passcode only at the true terminal boundary.
struct CustodyInstallRequest {
	Fn<void(CustodyInstall)> ready;
	Fn<bool(quint32 previousEpoch, quint32 epoch)> passcodeCreated;
	Fn<void(std::optional<quint32>, Fn<void(CustodyResetResult)>)> resetUnusableVault;
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

[[nodiscard]] bool VaultUnlockSilent(not_null<Main::Session*> session);
[[nodiscard]] VaultAuthorization AcquireSilentVaultUnlock(
	not_null<Main::Session*> session);

[[nodiscard]] CustodyInstaller MakeCustodyInstaller(
	std::shared_ptr<Main::SessionShow> show,
	VaultAuthorization authorization = nullptr);

// The app lock and the shared live Passcode policy are the only dependencies.
// Reconciliation runs at startup, logout and completed protection/install
// operations. It never runs from localPasscodeChanged: a wallet-only passcode
// can be created before its chooser arms the first store that will use it.
void DropUnusedPasscode();

// Vault: the typed passcode must open the vault's own passcode wrap.
// KeyData: the typed bytes are verified against key_data only and answered
// as scoped SecureBytes, while no vault is opened, no grant is minted,
// nothing is armed and nothing is retained; whoever takes the bytes opens
// D only when the selected operation needs it.
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
