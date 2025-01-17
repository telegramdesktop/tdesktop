/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_notifications_manager.h"

struct ToastActivation;

namespace Platform {
namespace Notifications {

class Manager : public Window::Notifications::NativeManager {
public:
	Manager(Window::Notifications::System *system);
	~Manager();

	bool init();
	void clearNotification(NotificationId id);

	void handleActivation(const ToastActivation &activation);

protected:
	void doShowNativeNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) override;
	void doClearAllFast() override;
	void doClearFromItem(not_null<HistoryItem*> item) override;
	void doClearFromTopic(not_null<Data::ForumTopic*> topic) override;
	void doClearFromHistory(not_null<History*> history) override;
	void doClearFromSession(not_null<Main::Session*> session) override;
	void onBeforeNotificationActivated(NotificationId id) override;
	void onAfterNotificationActivated(
		NotificationId id,
		not_null<Window::SessionController*> window) override;
	bool doSkipToast() const override;
	void doMaybePlaySound(Fn<void()> playSound) override;
	void doMaybeFlashBounce(Fn<void()> flashBounce) override;

private:
	class Private;
	const std::unique_ptr<Private> _private;

};

} // namespace Notifications
} // namespace Platform
