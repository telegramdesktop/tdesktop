/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <chrono>

namespace Platform::WidgetHelper::details {

constexpr auto kRelayEndWait = std::chrono::milliseconds(1000);

struct RequirementText {
	QString text;
	QString check;
	QString error;
};

[[nodiscard]] QString SystemError(const char *call, int error);
[[nodiscard]] QByteArray BundledPath(const char *relative);
[[nodiscard]] RequirementText HelperRequirementText(
	const QString &identifier,
	const QByteArray &path);

} // namespace Platform::WidgetHelper::details
