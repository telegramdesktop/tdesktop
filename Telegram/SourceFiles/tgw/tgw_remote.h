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
	Helper,
	Nowhere,
};

void PrepareRemoteEngines();
[[nodiscard]] EnginePlace ChosenEnginePlace();
[[nodiscard]] std::shared_ptr<twidget::SandboxHostedChannel> HostedChannel(
	std::shared_ptr<twidget::SandboxResources> resources);
[[nodiscard]] twidget::SandboxHostedAnswers HostedAnswers();
void RefuseRemoteEngines();
[[nodiscard]] bool StopRemoteEngines(Fn<void()> stopped);

} // namespace Tgw
