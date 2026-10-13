/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace twidget {
class SandboxResources;
} // namespace twidget

namespace Tgw {

void PrepareResources(
	Fn<void(std::shared_ptr<twidget::SandboxResources>)> done);
[[nodiscard]] bool StopResources(Fn<void()> stopped);
[[nodiscard]] bool ResourcesFailed();

} // namespace Tgw
