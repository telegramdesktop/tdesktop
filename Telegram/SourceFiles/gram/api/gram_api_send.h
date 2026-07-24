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

struct SentInfo {
	QByteArray messageHash;
	QByteArray messageHashNorm;
};

[[nodiscard]] HttpRequest SendMessageRequest(const QByteArray &bocBase64);
[[nodiscard]] std::optional<SentInfo> ParseSendResult(const QByteArray &json);

struct EmulationResult {
	bool success = false;
	int64 totalFeeNano = 0;
	int64 sentNano = 0;
	QString error;
};

[[nodiscard]] HttpRequest EmulateTraceRequest(const QByteArray &bocBase64);
[[nodiscard]] std::optional<EmulationResult> ParseEmulateTrace(
	const QByteArray &json);

} // namespace Gram
