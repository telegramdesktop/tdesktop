/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Tgw {

void PostToRenderThread(Fn<void()> task);
[[nodiscard]] bool RenderThreadStarted();
[[nodiscard]] bool OnRenderThread();
[[nodiscard]] bool RenderThreadBarrier(Fn<void()> done);
void FinishRenderThread(Fn<void()> last);

} // namespace Tgw
