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

inline bool SkipAudio() {
	return false;
}

inline bool SkipToast() {
	return false;
}

inline void FlashBounce() {
}

void Finish();

class Manager : public Window::Notifications::NativeManager {
public:
	Manager(Window::Notifications::System *system);

	void clearNotification(PeerId peerId, MsgId msgId);
	bool hasPoorSupport() const;
	bool hasActionsSupport() const;

	~Manager();

protected:
	void doShowNativeNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) override;
	void doClearAllFast() override;
	void doClearFromHistory(not_null<History*> history) override;

private:
	class Private;
	const std::unique_ptr<Private> _private;

};

} // namespace Notifications
} // namespace Platform
