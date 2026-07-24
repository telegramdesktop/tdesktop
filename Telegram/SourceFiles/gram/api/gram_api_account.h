/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/api/gram_api_request.h"

#include <optional>

namespace Gram {

enum class AccountStatus {
	NonExisting,
	Uninit,
	Active,
	Frozen,
};

struct AccountState {
	int64 balanceNano = 0;
	AccountStatus status = AccountStatus::NonExisting;
	QByteArray dataBoc;
	QByteArray lastTxHash;
	quint64 lastTxLt = 0;
};

[[nodiscard]] HttpRequest AddressInformationRequest(
	const QString &address);
[[nodiscard]] std::optional<AccountState> ParseAccountState(
	const QByteArray &json);

} // namespace Gram
