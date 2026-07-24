/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/ton/gram_cell.h"

namespace Gram {

struct StateInitData {
	Cell code;
	Cell data;
};

[[nodiscard]] Cell BuildStateInit(const StateInitData &data);
[[nodiscard]] Cell BuildCommentBody(const QString &text);
[[nodiscard]] Cell BuildInternalMessage(
	const Address &dest,
	bool bounce,
	int64 amountNano,
	const std::optional<Cell> &body,
	const std::optional<Cell> &stateInit);
[[nodiscard]] Cell BuildExternalInMessage(
	const Address &dest,
	const Cell &body,
	const std::optional<Cell> &stateInit);
[[nodiscard]] std::optional<Cell> NormalizeExternalMessage(
	const Cell &externalMessage);
[[nodiscard]] QByteArray NormalizedExternalHash(
	const Cell &externalMessage);

} // namespace Gram
