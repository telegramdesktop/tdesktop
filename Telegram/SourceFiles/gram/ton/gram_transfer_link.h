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

namespace Gram {

struct TransferLink {
	QString address;
	int64 amountNano = 0;
	QString comment;
};

// Parses and formats the de-facto TON transfer link
// `ton://transfer/<address>?amount=<nano>&text=<comment>`. The
// `ton://transfer/` prefix is matched case-insensitively, the address
// is everything between it and the first `?`, and the query is
// everything after that `?`.
// Fatal, yielding std::nullopt: any other prefix, including
// `https://ton.org/transfer/...`; an address that ParseAddress
// rejects, including an empty one; an `amount` that is not a plain
// sequence of decimal digits or does not fit into int64; empty or
// truncated input.
// Ignored, never fatal: unknown query parameters, and every
// repetition of a parameter after the first. An absent or empty
// `amount` means unset (0), an absent or empty `text` means no
// comment.
// The address is validated through ParseAddress but carried exactly
// as written, so a pasted bounceable, non-bounceable or testnet form
// survives. FormatTransferLink writes it back verbatim,
// percent-encodes the comment, omits `amount` unless it is positive
// and omits `text` when the comment is empty.
[[nodiscard]] std::optional<TransferLink> ParseTransferLink(
	const QString &url);
[[nodiscard]] QString FormatTransferLink(const TransferLink &link);

} // namespace Gram
