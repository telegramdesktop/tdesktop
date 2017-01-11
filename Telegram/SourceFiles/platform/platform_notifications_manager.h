/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

// Platform module must define a Platform::Notifications::Manager class.
// It should be Window::Notifications::Manager or its derivative.
#ifdef Q_OS_MAC
#include "platform/mac/notifications_manager_mac.h"
#elif defined Q_OS_LINUX // Q_OS_MAC
#include "platform/linux/notifications_manager_linux.h"
#elif defined Q_OS_WINRT // Q_OS_MAC || Q_OS_LINUX
#include "platform/winrt/notifications_manager_winrt.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_LINUX || Q_OS_WINRT
#include "platform/win/notifications_manager_win.h"
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WINRT || Q_OS_WIN

// Platform-independent API.
namespace Platform {
namespace Notifications {

void defaultNotificationShown(QWidget *widget);
bool skipAudio();
bool skipToast();

void start();
Manager *manager();
bool supported();
void finish();

} // namespace Notifications
} // namespace Platform
