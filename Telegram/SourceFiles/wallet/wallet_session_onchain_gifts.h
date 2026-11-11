/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Wallet {

struct TransferItem;

void ReadOnchainGiftAction(
	TransferItem &result,
	const MTPWalletOnchainGiftAction &action);

} // namespace Wallet
