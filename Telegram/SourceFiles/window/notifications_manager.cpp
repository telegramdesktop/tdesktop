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
#include "stdafx.h"
#include "window/notifications_manager.h"

#include "platform/platform_notifications_manager.h"
#include "window/notifications_manager_default.h"
#include "lang.h"
#include "mainwindow.h"
#include "mainwidget.h"

namespace Window {
namespace Notifications {

void Start() {
	Default::Start();
	Platform::Notifications::Start();
}

Manager *GetManager() {
	if (auto result = Platform::Notifications::GetManager()) {
		return result;
	}
	return Default::GetManager();
}

void Finish() {
	Platform::Notifications::Finish();
	Default::Finish();
}

Manager::DisplayOptions Manager::getNotificationOptions(HistoryItem *item) {
	auto hideEverything = (App::passcoded() || Global::ScreenIsLocked());

	DisplayOptions result;
	result.hideNameAndPhoto = hideEverything || (Global::NotifyView() > dbinvShowName);
	result.hideMessageText = hideEverything || (Global::NotifyView() > dbinvShowPreview);
	result.hideReplyButton = result.hideMessageText || !item || !item->history()->peer->canWrite();
	return result;
}

void Manager::notificationActivated(PeerId peerId, MsgId msgId) {
	onBeforeNotificationActivated(peerId, msgId);
	if (auto window = App::wnd()) {
		auto history = App::history(peerId);
		window->showFromTray();
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
		window->reActivateWindow();
#endif
		if (App::passcoded()) {
			window->setInnerFocus();
			window->notifyClear();
		} else {
			auto tomsg = !history->peer->isUser() && (msgId > 0);
			if (tomsg) {
				auto item = App::histItemById(peerToChannel(peerId), msgId);
				if (!item || !item->mentionsMe()) {
					tomsg = false;
				}
			}
			Ui::showPeerHistory(history, tomsg ? msgId : ShowAtUnreadMsgId);
			window->notifyClear(history);
		}
	}
	onAfterNotificationActivated(peerId, msgId);
}

void Manager::notificationReplied(PeerId peerId, MsgId msgId, const QString &reply) {
	if (!peerId) return;

	auto history = App::history(peerId);

	MainWidget::MessageToSend message;
	message.history = history;
	message.textWithTags = { reply, TextWithTags::Tags() };
	message.replyTo = (msgId > 0 && !history->peer->isUser()) ? msgId : 0;
	message.silent = false;
	message.clearDraft = false;
	if (auto main = App::main()) {
		main->sendMessage(message);
	}
}

void NativeManager::doShowNotification(HistoryItem *item, int forwardedCount) {
	auto options = getNotificationOptions(item);

	QString title = options.hideNameAndPhoto ? qsl("Telegram Desktop") : item->history()->peer->name;
	QString subtitle = options.hideNameAndPhoto ? QString() : item->notificationHeader();
	QString text = options.hideMessageText ? lang(lng_notification_preview) : (forwardedCount < 2 ? item->notificationText() : lng_forward_messages(lt_count, forwardedCount));

	doShowNativeNotification(item->history()->peer, item->id, title, subtitle, text, options.hideNameAndPhoto, options.hideReplyButton);
}

} // namespace Notifications
} // namespace Window
