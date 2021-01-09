/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtDBus/QDBusServiceWatcher>

namespace Platform {
namespace internal {

class NotificationServiceWatcher {
public:
	NotificationServiceWatcher();

private:
	QDBusServiceWatcher _dbusWatcher;
};

} // namespace internal
} // namespace Platform
