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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "window/notifications_abstract_manager.h"

#include "platform/platform_notifications_manager.h"
#include "window/notifications_default_manager.h"
#include "lang.h"

namespace Window {
namespace Notifications {
namespace {

NeverFreedPointer<DefaultManager> FallbackManager;

} // namespace

void start() {
	Platform::Notifications::start();
}

AbstractManager *manager() {
	if (auto result = Platform::Notifications::manager()) {
		return result;
	}
	FallbackManager.makeIfNull();
	return FallbackManager.data();
}

void finish() {
	Platform::Notifications::finish();
	FallbackManager.reset();
}

void AbstractManager::showNotification(HistoryItem *item, int forwardedCount) {
	auto hideEverything = (App::passcoded() || Global::ScreenIsLocked());
	auto hideName = hideEverything || (Global::NotifyView() > dbinvShowName);
	auto hidePreview = hideEverything || (Global::NotifyView() > dbinvShowPreview);

	QString title = hideName ? qsl("Telegram Desktop") : item->history()->peer->name;
	QString subtitle = hideName ? QString() : item->notificationHeader();
	bool showUserpic = hideName ? false : true;

	QString msg = hidePreview ? lang(lng_notification_preview) : (forwardedCount < 2 ? item->notificationText() : lng_forward_messages(lt_count, forwardedCount));
	bool showReplyButton = hidePreview ? false : item->history()->peer->canWrite();

	create(item->history()->peer, item->id, title, subtitle, showUserpic, msg, showReplyButton);
}

void AbstractManager::clearAllFast() {
	clear(nullptr, true);
}

void AbstractManager::clearAll() {
	clear(nullptr, false);
}

void AbstractManager::clearFromHistory(History *history) {
	clear(history, false);
}

} // namespace Notifications
} // namespace Window
