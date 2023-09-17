/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
