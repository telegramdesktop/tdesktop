/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class AuthSession;

namespace Core {

class Changelogs : public base::has_weak_ptr, private base::Subscriber {
public:
	Changelogs(not_null<AuthSession*> session, int oldVersion);

	static std::unique_ptr<Changelogs> Create(
		not_null<AuthSession*> session);

private:
	void requestCloudLogs();
	void addLocalLogs();
	void addLocalLog(const QString &text);
	void addBetaLogs();
	void addBetaLog(int changeVersion, const char *changes);

	const not_null<AuthSession*> _session;
	const int _oldVersion = 0;
	int _chatsSubscription = 0;
	bool _addedSomeLocal = false;

};

} // namespace Core
