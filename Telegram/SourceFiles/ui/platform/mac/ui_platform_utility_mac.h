/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QPoint>

namespace Ui {
namespace Platform {

inline bool TranslucentWindowsSupported(QPoint globalPosition) {
	return true;
}

inline void UpdateOverlayed(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return false;
}

} // namespace Platform
} // namespace Ui
