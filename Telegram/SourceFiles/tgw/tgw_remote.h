/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Tgw {

void PrepareRemoteEngines();
[[nodiscard]] bool RemoteEngines();
[[nodiscard]] bool StopRemoteEngines(Fn<void()> stopped);

#ifdef Q_OS_WIN
[[noreturn]] void RunWidgetHelper(const QStringList &arguments);
#endif // Q_OS_WIN

} // namespace Tgw
