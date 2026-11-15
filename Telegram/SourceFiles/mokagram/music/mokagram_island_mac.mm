// This file is part of mokaGram, a Telegram Desktop fork.
// SPDX-License-Identifier: GPL-3.0-only
#include "mokagram/music/mokagram_island_mac.h"

#include "ui/platform/ui_platform_utility.h"

#include <Cocoa/Cocoa.h>

namespace Mokagram::Music {

void InitDesktopIsland(not_null<QWidget*> widget) {
	Ui::Platform::InitOnTopPanel(widget);
	const auto window = [reinterpret_cast<NSView*>(widget->winId()) window];
	[window setHasShadow:NO];
	[window setLevel:NSMainMenuWindowLevel + 3];
	[window setAnimationBehavior:NSWindowAnimationBehaviorNone];
	[window setMovable:NO];
}

QRect DesktopIslandArea() {
	const auto screens = [NSScreen screens];
	const auto primary = [screens firstObject];
	if (!primary) {
		return QRect();
	}
	auto selected = primary;
	for (NSScreen *screen in screens) {
		if (@available(macOS 12.0, *)) {
			if (screen.safeAreaInsets.top > 0.) {
				selected = screen;
				break;
			}
		}
	}
	const auto frame = selected.frame;
	// AppKit uses a bottom-left origin, Qt uses the primary screen's top-left.
	return QRect(
		int(NSMinX(frame)),
		int(NSMaxY(primary.frame) - NSMaxY(frame)),
		int(NSWidth(frame)),
		int(NSHeight(frame)));
}

int DesktopIslandNotchWidth() {
	if (@available(macOS 12.0, *)) {
		for (NSScreen *screen in [NSScreen screens]) {
			if (screen.safeAreaInsets.top > 0.) {
				return int(NSWidth(screen.frame)
					- NSWidth(screen.auxiliaryTopLeftArea)
					- NSWidth(screen.auxiliaryTopRightArea));
			}
		}
	}
	return 0;
}

} // namespace Mokagram::Music
