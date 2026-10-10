/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Core::Updates {

struct VerifiedUpdate;

[[nodiscard]] std::optional<QByteArray> DecompressUpdatePayload(
	const VerifiedUpdate &verified,
	QString *error = nullptr);

// Extraction requires a fresh caller-controlled directory.
[[nodiscard]] bool ExtractUpdateFiles(
	const QByteArray &uncompressed,
	quint64 expectedVersion,
	const QString &directory,
	QString *error = nullptr);

} // namespace Core::Updates
