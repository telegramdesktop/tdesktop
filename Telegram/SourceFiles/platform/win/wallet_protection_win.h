/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

// Registers the Windows Hello Wallet::ProtectionProvider, whose wrap key
// derives from a TPM-resident Hello credential's signature; called once from
// WindowsIntegration::init(), the first platform hook with the main queue
// installed, because the availability check completes asynchronously and
// crl::on_main drops a callable queued before crl::init_main_queue.
void RegisterWalletProtectionProvider();

} // namespace Platform
