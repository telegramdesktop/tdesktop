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

struct AddressFunds {
	int64 balanceNano = 0;
	bool neverUsed = false;
};

// The same read the engine refreshes its own account with.
[[nodiscard]] HttpRequest AddressInformationRequest(const QString &address);
[[nodiscard]] std::optional<AddressFunds> ParseAddressFunds(
	const QByteArray &json);

} // namespace Gram
