/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

class Tray;

} // namespace Platform

// Platform dependent implementations.

#ifdef Q_OS_WIN
#include "platform/win/tray_win.h"
#elif defined Q_OS_MAC // Q_OS_WIN
#include "platform/mac/tray_mac.h"
#else // Q_OS_WIN || Q_OS_MAC
#include "platform/linux/tray_linux.h"
#endif // else for Q_OS_WIN || Q_OS_MAC
