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
#include "auth_session.h"

#include "messenger.h"
#include "storage/file_download.h"
#include "window/notifications_manager.h"

AuthSession::AuthSession(UserId userId)
: _userId(userId)
, _downloader(std::make_unique<Storage::Downloader>())
, _notifications(std::make_unique<Window::Notifications::System>(this)) {
	t_assert(_userId != 0);
}

bool AuthSession::Exists() {
	return (Messenger::Instance().authSession() != nullptr);
}

AuthSession &AuthSession::Current() {
	auto result = Messenger::Instance().authSession();
	t_assert(result != nullptr);
	return *result;
}

UserData *AuthSession::CurrentUser() {
	return App::user(CurrentUserId());
}

base::Observable<void> &AuthSession::CurrentDownloaderTaskFinished() {
	return Current().downloader()->taskFinished();
}

bool AuthSession::validateSelf(const MTPUser &user) {
	if (user.type() != mtpc_user || !user.c_user().is_self() || user.c_user().vid.v != userId()) {
		LOG(("Auth Error: wrong self user received."));
		App::logOutDelayed();
		return false;
	}
	return true;
}

AuthSession::~AuthSession() = default;
