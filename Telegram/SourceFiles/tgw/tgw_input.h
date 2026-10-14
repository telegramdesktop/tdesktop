/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Tgw {

class RendererCore;

[[nodiscard]] int DomModifiers(Qt::KeyboardModifiers modifiers);

void GrantKeys(not_null<RendererCore*> core);

void HoverWheel(not_null<RendererCore*> core);
void UnhoverWheel(not_null<const RendererCore*> core);
[[nodiscard]] bool ConsumeWheel(not_null<QWheelEvent*> e);

} // namespace Tgw
