/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_tray.h"

namespace Core {

class Tray final {
public:
	Tray();

	void create();
	void updateMenuText();
	void updateIconCounters();

	[[nodiscard]] rpl::producer<> aboutToShowRequests() const;
	[[nodiscard]] rpl::producer<> showFromTrayRequests() const;
	[[nodiscard]] rpl::producer<> hideToTrayRequests() const;

	[[nodiscard]] bool has() const;

private:
	void rebuildMenu();
	void toggleSoundNotifications();

	Platform::Tray _tray;

	bool _activeForTrayIconAction = false;
	crl::time _lastTrayClickTime = 0;

	rpl::event_stream<> _textUpdates;
	rpl::event_stream<> _minimizeMenuItemClicks;

};

// The tray icon paints the unread counter, a screen reader reads the
// tooltip instead - so the tooltip carries the counter too, in the
// window title's format: "Telegram Desktop (3)".
[[nodiscard]] QString TrayIconToolTip();

} // namespace Core
