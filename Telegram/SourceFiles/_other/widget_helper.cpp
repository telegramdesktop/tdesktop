/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "test/test_widget_helper_probe_lockdown.h"
#include "twidget/remote_stream.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr auto kMaxReasonBytes = size_t(4096);

[[nodiscard]] std::string BuildString() {
	return std::to_string(TDESKTOP_APP_VERSION);
}

// Probes inspect the sandbox from inside the helper: never shipped.
#ifdef _DEBUG
[[nodiscard]] bool ArgumentsAllowed(
		const std::vector<std::string> &arguments) {
	return !arguments.empty();
}

[[nodiscard]] std::string Lockdown(
		const std::vector<std::string> &arguments) {
	return Test::WidgetHelperProbeLockdown(arguments, [] {
		return WidgetHelper::Lockdown();
	});
}
#else // _DEBUG
[[nodiscard]] bool ArgumentsAllowed(
		const std::vector<std::string> &arguments) {
	return arguments.size() == 1;
}

[[nodiscard]] std::string Lockdown(const std::vector<std::string> &) {
	return WidgetHelper::Lockdown();
}
#endif // _DEBUG

[[noreturn]] void Exit(const std::string &reason) {
	if (!reason.empty()) {
		const auto size = std::min(reason.size(), kMaxReasonBytes);
		std::fprintf(stderr, "Widget helper: %.*s\n", int(size), reason.data());
	}
	std::_Exit(reason.empty() ? 0 : 1);
}

[[noreturn]] void Run(std::vector<std::string> arguments) {
	if (!ArgumentsAllowed(arguments)) {
		Exit("unexpected arguments");
	}
	const auto stream = WidgetHelper::Adopt(arguments.front());
	if (!stream) {
		Exit("the channel could not be adopted");
	}
	Exit(WidgetHelper::Serve(
		*stream,
		BuildString(),
		[&] { return Lockdown(arguments); }));
}

} // namespace

int main(int argc, char *argv[]) {
#ifdef OS_MAC_STORE
	WidgetHelper::ServeService(Run);
#else // OS_MAC_STORE
	Run(std::vector<std::string>(argv + std::min(argc, 1), argv + argc));
#endif // OS_MAC_STORE
}
