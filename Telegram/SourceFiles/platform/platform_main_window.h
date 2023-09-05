/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "window/main_window.h"

namespace Platform {

class MainWindow;

} // namespace Platform

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "platform/mac/main_window_mac.h"
#elif defined Q_OS_UNIX // Q_OS_MAC
#include "platform/linux/main_window_linux.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_UNIX
#include "platform/win/main_window_win.h"
#endif // Q_OS_MAC || Q_OS_UNIX || Q_OS_WIN
