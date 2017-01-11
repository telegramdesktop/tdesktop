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

#include "window/notifications_manager.h"

namespace Platform {
namespace Notifications {

inline void defaultNotificationShown(QWidget *widget) {
}

class Manager : public Window::Notifications::NativeManager {
public:
	Manager();

	void clearNotification(PeerId peerId, MsgId msgId);

	~Manager();

protected:
	void doShowNativeNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton) override;
	void doClearAllFast() override;
	void doClearFromHistory(History *history) override;
	void onBeforeNotificationActivated(PeerId peerId, MsgId msgId) override;
	void onAfterNotificationActivated(PeerId peerId, MsgId msgId) override;

private:
	class Impl;
	std_::unique_ptr<Impl> _impl;

};

} // namespace Notifications
} // namespace Platform
