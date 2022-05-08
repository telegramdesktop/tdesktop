/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_tray.h"

#include "base/unique_qptr.h"

class QMenu;

namespace Platform {

class NativeIcon;

class Tray final {
public:
	Tray();
	~Tray();

	[[nodiscard]] rpl::producer<> aboutToShowRequests() const;
	[[nodiscard]] rpl::producer<> showFromTrayRequests() const;
	[[nodiscard]] rpl::producer<> hideToTrayRequests() const;
	[[nodiscard]] rpl::producer<> iconClicks() const;

	void createIcon();
	void destroyIcon();

	void updateIcon();

	void createMenu();
	void destroyMenu();

	void addAction(rpl::producer<QString> text, Fn<void()> &&callback);

	void showTrayMessage() const;
	[[nodiscard]] bool hasTrayMessageSupport() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	std::unique_ptr<NativeIcon> _nativeIcon;
	base::unique_qptr<QMenu> _menu;

	rpl::event_stream<> _showFromTrayRequests;

	rpl::lifetime _actionsLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Platform
