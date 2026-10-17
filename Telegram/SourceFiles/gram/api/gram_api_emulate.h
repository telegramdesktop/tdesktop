/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/api/gram_api_request.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Gram {

struct EmulatedTrace {
	int64 feeNano = 0;
	QString account;
};

// POST api/emulate/v1/emulateTrace with the body the engine's own
// emulation sends: the signed BOC as the engine serializes it (standard
// padded base64), signature checks ignored, no code/data, address book
// or metadata, actions requested, the latest masterchain block.
[[nodiscard]] HttpRequest EmulateTraceRequest(const QString &bocBase64);

// The root transaction's own fee, read the way the engine reads it:
// trace.tx_hash names the root, transactions[hash] is it, total_fees is
// the fee; the root must not be aborted and both its compute phase
// (exit code 0 or 1) and action phase (result code 0) must succeed.
// Nullopt for anything else - the caller treats it as a failed
// emulation. The account is returned unnormalized for the caller's
// identity check.
[[nodiscard]] std::optional<EmulatedTrace> ParseEmulatedTrace(
	const QByteArray &json);

} // namespace Gram
