/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>

namespace Gram {

[[nodiscard]] quint32 Crc32C(const QByteArray &data);
[[nodiscard]] quint16 Crc16(const QByteArray &data);

} // namespace Gram
