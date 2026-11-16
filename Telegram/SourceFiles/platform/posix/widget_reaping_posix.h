/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_widget_helper.h"

#include <sys/types.h>

#include <optional>

namespace Platform::WidgetHelper {

[[nodiscard]] QString DescribeStatus(int status);

[[nodiscard]] Completion CompleteWith(
	crl::time exitWait,
	crl::time killWait,
	Fn<std::optional<Completion>(crl::time timeout)> waitExit,
	Fn<QString()> sendKill);

[[nodiscard]] Completion CompleteChild(
	pid_t pid,
	crl::time exitWait,
	crl::time killWait);

} // namespace Platform::WidgetHelper
