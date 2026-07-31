/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

[[nodiscard]] float64 TonUsdRate(float64 raw);
[[nodiscard]] float64 TonUsdRate(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<float64> TonUsdRateValue(
	not_null<Main::Session*> session);
[[nodiscard]] QString FormatUsd(
	int64 nanoAmount,
	float64 rate,
	int decimals = 2,
	bool approximate = false);

} // namespace Wallet
