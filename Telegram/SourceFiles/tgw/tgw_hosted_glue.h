/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <string>

namespace twidget {
class Renderer;
class SandboxHostedPlatform;
} // namespace twidget

namespace Tgw {

void PrepareHostedRenderers();

[[nodiscard]] std::unique_ptr<twidget::Renderer> MakeHostedRenderer(
	const std::string &scheme,
	double width,
	std::shared_ptr<twidget::SandboxHostedPlatform> platform);

} // namespace Tgw
