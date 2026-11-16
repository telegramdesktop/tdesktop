/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_helper_probe_lockdown.h"

#ifdef _DEBUG

namespace Test {

std::string WidgetHelperProbeLockdown(
		const std::vector<std::string> &arguments,
		const std::function<std::string()> &lockdown) {
	if (arguments.size() < 2) {
		return lockdown
			? lockdown()
			: std::string("no lockdown callable");
	}
	const auto &mode = arguments[1];
	if (mode != kWidgetHelperProbeMode
		&& mode != kWidgetHelperUnsandboxedMode) {
		return RefuseWidgetHelperMode(mode);
	}
	return RunWidgetHelperProbe(
		mode == kWidgetHelperProbeMode,
		arguments,
		lockdown);
}

} // namespace Test

#endif // _DEBUG
