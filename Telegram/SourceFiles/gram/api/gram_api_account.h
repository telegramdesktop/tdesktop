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

// The same read the engine refreshes its own account with.
[[nodiscard]] HttpRequest AddressInformationRequest(const QString &address);
[[nodiscard]] std::optional<int64> ParseAddressBalance(
	const QByteArray &json);

} // namespace Gram
