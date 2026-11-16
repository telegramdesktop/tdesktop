/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform::WidgetHelper::details {

struct RequirementText {
	QString text;
	QString check;
	QString error;
};

[[nodiscard]] QString SystemError(const char *call, int error);
[[nodiscard]] RequirementText HelperRequirementText(const QString &identifier);

} // namespace Platform::WidgetHelper::details
