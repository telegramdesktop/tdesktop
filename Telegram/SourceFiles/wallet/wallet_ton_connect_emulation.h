/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace wallet_engine {
struct SendEmulation;
} // namespace wallet_engine

namespace Wallet {

enum class TonConnectActionKind : uchar {
	Withdraw,
	Deposit,
	Transfer,
	Excess,
	CallContract,
	DeployContract,
	Unknown,
};

enum class TonConnectActionSide : uchar {
	None,
	Incoming,
	Outgoing,
};

struct TonConnectAction {
	TonConnectActionKind kind = TonConnectActionKind::Unknown;
	TonConnectActionSide side = TonConnectActionSide::None;
	std::optional<int64> amountNano;
	QString counterparty;

	friend bool operator==(
		const TonConnectAction &,
		const TonConnectAction &) = default;
};

enum class TonConnectEmulationStatus : uchar {
	Shown,
	Failed,
	Aborted,
	Incomplete,
	Empty,
};

struct TonConnectEmulation {
	std::vector<TonConnectAction> actions;
	int64 netNano = 0;
	TonConnectEmulationStatus status = TonConnectEmulationStatus::Empty;

	friend bool operator==(
		const TonConnectEmulation &,
		const TonConnectEmulation &) = default;
};

[[nodiscard]] TonConnectEmulation ParseTonConnectEmulation(
	const wallet_engine::SendEmulation &emulation,
	const QString &walletAddress,
	int64 requestTotalNano);

} // namespace Wallet
