/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

// Registers the Touch ID Wallet::ProtectionProvider, which wraps the vault
// key to a Secure Enclave key; called once from Platform::start().
void RegisterWalletProtectionProvider();

} // namespace Platform
