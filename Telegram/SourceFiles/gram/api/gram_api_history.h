/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/api/gram_api_request.h"
#include "gram/ton/gram_address.h"

#include <optional>
#include <vector>

namespace Gram {

struct TransferItem {
	enum class Status {
		Success,
		Failure,
		Pending,
	};
	enum class Kind {
		Transfer,
		ContractInteraction,
	};

	Kind kind = Kind::Transfer;
	bool incoming = false;
	Address counterparty;
	int64 amountNano = 0;
	int64 feeNano = 0;
	QString comment;
	bool commentEncrypted = false;
	QByteArray encryptedPayload;
	TimeId date = 0;
	quint64 lt = 0;
	QByteArray traceId;
	QByteArray externalHashNorm;
	Status status = Status::Success;
};

struct HistoryPage {
	std::vector<TransferItem> list;
	bool hasNext = false;
};

[[nodiscard]] HttpRequest TracesRequest(
	const QString &account,
	int limit,
	int offset);
[[nodiscard]] std::optional<HistoryPage> ParseTraces(
	const QByteArray &json,
	const Address &self,
	int limit);
[[nodiscard]] HttpRequest TransactionsRequest(
	const QString &account,
	int limit,
	int offset);
[[nodiscard]] std::optional<HistoryPage> ParseTransactions(
	const QByteArray &json,
	const Address &self,
	int limit);
[[nodiscard]] HttpRequest TransactionsByMessageRequest(
	const QByteArray &msgHashNorm);
[[nodiscard]] std::optional<bool> ParseTransactionsByMessageFound(
	const QByteArray &json);

} // namespace Gram
