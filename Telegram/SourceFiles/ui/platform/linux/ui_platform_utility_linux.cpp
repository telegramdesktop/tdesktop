/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/linux/ui_platform_utility_linux.h"

#include "base/flat_set.h"
#include "ui/ui_log.h"

#include <QtCore/QPoint>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <qpa/qplatformnativeinterface.h>

namespace Ui {
namespace Platform {

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
}

bool TranslucentWindowsSupported(QPoint globalPosition) {
	if (const auto native = QGuiApplication::platformNativeInterface()) {
		if (const auto desktop = QApplication::desktop()) {
			const auto index = desktop->screenNumber(globalPosition);
			const auto screens = QGuiApplication::screens();
			if (const auto screen = (index >= 0 && index < screens.size()) ? screens[index] : QGuiApplication::primaryScreen()) {
				if (native->nativeResourceForScreen(QByteArray("compositingEnabled"), screen)) {
					return true;
				}
				static auto WarnedAbout = base::flat_set<int>();
				if (!WarnedAbout.contains(index)) {
					WarnedAbout.emplace(index);
					UI_LOG(("WARNING: Compositing is disabled for screen index %1 (for position %2,%3)").arg(index).arg(globalPosition.x()).arg(globalPosition.y()));
				}
			} else {
				UI_LOG(("WARNING: Could not get screen for index %1 (for position %2,%3)").arg(index).arg(globalPosition.x()).arg(globalPosition.y()));
			}
		}
	}
	return false;
}

} // namespace Platform
} // namespace Ui
