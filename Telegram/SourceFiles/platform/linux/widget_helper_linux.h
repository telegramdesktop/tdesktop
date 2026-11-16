/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform::WidgetHelper {

#ifdef Q_OS_LINUX
void Hold();
#else // Q_OS_LINUX
inline void Hold() {
}
#endif // Q_OS_LINUX

} // namespace Platform::WidgetHelper
