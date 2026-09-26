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
class QIcon;

namespace Ui {
class DynamicImage;
} // namespace Ui

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

	[[nodiscard]] bool hasIcon() const;

	void createIcon();
	void destroyIcon();

	void updateIcon();

	void createMenu();
	void destroyMenu();

	void addAction(rpl::producer<QString> text, Fn<void()> &&callback);
	void addAction(
		rpl::producer<QString> text,
		Fn<void()> &&callback,
		const QIcon &icon);
	void addAction(
		rpl::producer<QString> text,
		Fn<void()> &&callback,
		std::shared_ptr<Ui::DynamicImage> icon,
		int size);
	void addSeparator();

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

inline bool HasMonochromeSetting() {
	return false;
}

} // namespace Platform
