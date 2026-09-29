/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QStringList>

#include <optional>

namespace Platform {

[[nodiscard]] std::optional<int> InstallUpdateIfRequested(
	const QStringList &arguments);

} // namespace Platform
