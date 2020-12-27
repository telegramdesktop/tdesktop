/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

typedef struct _GDBusConnection GDBusConnection;

namespace Platform {
namespace internal {

class GSDMediaKeys {
public:
	GSDMediaKeys();

	GSDMediaKeys(const GSDMediaKeys &other) = delete;
	GSDMediaKeys &operator=(const GSDMediaKeys &other) = delete;
	GSDMediaKeys(GSDMediaKeys &&other) = delete;
	GSDMediaKeys &operator=(GSDMediaKeys &&other) = delete;

	~GSDMediaKeys();

private:
	GDBusConnection *_dbusConnection = nullptr;
	QString _service;
	QString _objectPath;
	QString _interface;
	uint _signalId = 0;
	bool _grabbed = false;
};

} // namespace internal
} // namespace Platform
