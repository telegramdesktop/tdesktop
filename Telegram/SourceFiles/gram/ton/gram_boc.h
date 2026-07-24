/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "gram/ton/gram_cell.h"

#include <QtCore/QByteArray>

#include <optional>

namespace Gram {

[[nodiscard]] QByteArray SerializeBoc(const Cell &root, bool withCrc = true);
[[nodiscard]] std::optional<Cell> DeserializeBoc(const QByteArray &data);

} // namespace Gram
