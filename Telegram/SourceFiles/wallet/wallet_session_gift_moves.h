/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_session.h"

namespace Wallet {

struct SubmittedListEntry {
	std::string operationId;
	const TransferItem *shown = nullptr;
	QString canonicalId;
};

struct SubmittedListFacts {
	const std::vector<TransferItem> &history;
	std::optional<TimeId> boundary;
	GiftMoveHistoryCoverage coverage;
	TimeId skew = 0;
	std::string sendingId;
	std::optional<TransferItem> sending;
	std::vector<SubmittedListEntry> entries;
};

[[nodiscard]] TransferGiftMove SentGiftMove(const SendArgs &args);

[[nodiscard]] bool UpdateGiftMoveCoverage(
	GiftMoveHistoryCoverage &coverage,
	const std::vector<TransferItem> &history,
	const std::vector<TransferItem> &page,
	const QString &next,
	const QString &offset);

[[nodiscard]] std::vector<ListedSubmittedTransfer> ListedSubmittedTransfers(
	const SubmittedListFacts &facts);

} // namespace Wallet
