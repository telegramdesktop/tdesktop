// This file is part of mokaGram, a Telegram Desktop fork.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace Mokagram::Music {

void InitDesktopIsland(not_null<QWidget*> widget);
void SetDesktopIslandMouseInput(not_null<QWidget*> widget, bool enabled);
[[nodiscard]] QRect DesktopIslandArea();
[[nodiscard]] QSize DesktopIslandNotchSize();

} // namespace Mokagram::Music
