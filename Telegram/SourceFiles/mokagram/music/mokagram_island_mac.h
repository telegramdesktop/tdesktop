// This file is part of mokaGram, a Telegram Desktop fork.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace Mokagram::Music {

void InitDesktopIsland(not_null<QWidget*> widget);
[[nodiscard]] QRect DesktopIslandArea();
[[nodiscard]] int DesktopIslandNotchWidth();

} // namespace Mokagram::Music
