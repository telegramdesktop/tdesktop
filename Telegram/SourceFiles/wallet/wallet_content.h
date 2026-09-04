/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "wallet/wallet_vault.h"

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class GenericBox;
class RpWidget;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Wallet {

[[nodiscard]] base::unique_qptr<Ui::RpWidget> CreateContent(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Main::SessionShow> show);

void FillMenu(
	std::shared_ptr<Main::SessionShow> show,
	const Ui::Menu::MenuCallback &addAction);

[[nodiscard]] bool TransferLinkValid(const QString &url);

void ShowTransferLink(
	std::shared_ptr<Main::SessionShow> show,
	const QString &url);

[[nodiscard]] rpl::producer<bool> TransactionsShownValue(
	not_null<Main::Session*> session);

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
