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

#ifdef Q_OS_MAC
#include "platform/mac/launcher_mac.h"
#elif defined Q_OS_UNIX // Q_OS_MAC
#include "platform/linux/launcher_linux.h"
#elif defined Q_OS_WINRT || defined Q_OS_WIN // Q_OS_MAC || Q_OS_UNIX
#include "platform/win/launcher_win.h"
#endif // Q_OS_MAC || Q_OS_UNIX || Q_OS_WINRT || Q_OS_WIN
