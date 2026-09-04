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

} // namespace Wallet
