/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace twidget {
class SandboxHostedChannel;
class SandboxResources;
struct SandboxHostedAnswers;
} // namespace twidget

namespace Tgw {

enum class EnginePlace : uchar {
	InProcess,
	Helper,
	Nowhere,
};

[[nodiscard]] constexpr bool EnginesInTelegram() {
#ifdef TDESKTOP_WIDGETS_HELPER_ONLY
	return false;
#else // TDESKTOP_WIDGETS_HELPER_ONLY
	return true;
#endif // TDESKTOP_WIDGETS_HELPER_ONLY
}

void PrepareRemoteEngines();
[[nodiscard]] EnginePlace ChosenEnginePlace();
[[nodiscard]] bool WidgetsHosted();
[[nodiscard]] std::shared_ptr<twidget::SandboxHostedChannel> HostedChannel(
	std::shared_ptr<twidget::SandboxResources> resources);
[[nodiscard]] twidget::SandboxHostedAnswers HostedAnswers();
[[nodiscard]] bool StopRemoteEngines(Fn<void()> stopped);

} // namespace Tgw
