/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace twidget {
class SandboxRemoteStream;
} // namespace twidget

namespace WidgetHelper {

[[nodiscard]] std::unique_ptr<twidget::SandboxRemoteStream> Adopt(
	std::string_view token);
[[nodiscard]] std::string Lockdown();
[[nodiscard]] std::string Serve(
	twidget::SandboxRemoteStream &stream,
	const std::string &build,
	const std::function<std::string()> &lockdown);

[[noreturn]] void ServeService(
	std::function<void(std::vector<std::string>)> run);

} // namespace WidgetHelper
