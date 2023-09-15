/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

//class Launcher : public Core::Launcher {
//public:
//	Launcher(int argc, char *argv[]);
//
//	...
//
//};

} // namespace Platform

// Platform dependent implementations.

#if defined Q_OS_WINRT || defined Q_OS_WIN
#include "platform/win/launcher_win.h"
#elif defined Q_OS_MAC // Q_OS_WINRT || Q_OS_WIN
#include "platform/mac/launcher_mac.h"
#else // Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC
#include "platform/linux/launcher_linux.h"
#endif // else for Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC
