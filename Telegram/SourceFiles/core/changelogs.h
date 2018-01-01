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
	void addAlphaLogs();
	void addAlphaLog(int changeVersion, const char *changes);

	const not_null<AuthSession*> _session;
	const int _oldVersion = 0;
	int _chatsSubscription = 0;
	bool _addedSomeLocal = false;

};

} // namespace Core
