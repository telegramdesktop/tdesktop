/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPoint;
class QPainter;
class QPaintEvent;

namespace Ui {
namespace Platform {

[[nodiscard]] bool IsApplicationActive();

[[nodiscard]] bool TranslucentWindowsSupported(QPoint globalPosition);
void StartTranslucentPaint(QPainter &p, QPaintEvent *e);

void InitOnTopPanel(not_null<QWidget*> panel);
void DeInitOnTopPanel(not_null<QWidget*> panel);
void ReInitOnTopPanel(not_null<QWidget*> panel);

void UpdateOverlayed(not_null<QWidget*> widget);
void ShowOverAll(not_null<QWidget*> widget, bool canFocus = true);
void BringToBack(not_null<QWidget*> widget);

[[nodiscard]] constexpr bool UseMainQueueGeneric();
void DrainMainQueue(); // Needed only if UseMainQueueGeneric() is false.

} // namespace Platform
} // namespace Ui

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "ui/platform/mac/ui_platform_utility_mac.h"
#elif defined Q_OS_LINUX // Q_OS_MAC
#include "ui/platform/linux/ui_platform_utility_linux.h"
#elif defined Q_OS_WINRT || defined Q_OS_WIN // Q_OS_MAC || Q_OS_LINUX
#include "ui/platform/win/ui_platform_utility_win.h"
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WINRT || Q_OS_WIN
