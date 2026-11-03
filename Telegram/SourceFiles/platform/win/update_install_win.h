/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QStringList>

#include <optional>

namespace Platform {

enum class UpdateInstallMode {
	Writable,
	Protected,
	Refused,
};

[[nodiscard]] UpdateInstallMode GetUpdateInstallMode(
	const QString &executablePath,
	QString &error);
[[nodiscard]] QString ReadyUpdatePackagePath(const QString &workingDirectory);
[[nodiscard]] bool ReadyUpdatePackagePresent(const QString &workingDirectory);
[[nodiscard]] std::optional<QByteArray> ReadUpdatePackage(
	const QString &path,
	QString &error);
[[nodiscard]] bool ValidateProtectedUpdate(
	const QByteArray &bytes,
	bool beta,
	QString &error);
[[nodiscard]] QString UpdateInstallLogPath(const QString &executableDirectory);

[[nodiscard]] std::optional<int> InstallUpdateIfRequested(
	const QStringList &arguments);

} // namespace Platform
