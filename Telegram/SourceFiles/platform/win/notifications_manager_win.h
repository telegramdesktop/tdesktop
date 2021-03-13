/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_notifications_manager.h"

namespace Platform {
namespace Notifications {

#ifndef __MINGW32__

class Manager : public Window::Notifications::NativeManager {
public:
	Manager(Window::Notifications::System *system);
	~Manager();

	bool init();
	void clearNotification(NotificationId id);

protected:
	void doShowNativeNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) override;
	void doClearAllFast() override;
	void doClearFromHistory(not_null<History*> history) override;
	void doClearFromSession(not_null<Main::Session*> session) override;
	void onBeforeNotificationActivated(NotificationId id) override;
	void onAfterNotificationActivated(
		NotificationId id,
		not_null<Window::SessionController*> window) override;

private:
	class Private;
	const std::unique_ptr<Private> _private;

};
#endif // !__MINGW32__

} // namespace Notifications
} // namespace Platform
