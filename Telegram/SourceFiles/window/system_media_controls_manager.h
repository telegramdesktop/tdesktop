/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace base::Platform {
class SystemMediaControls;
} // namespace base::Platform

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Window {
class Controller;
} // namespace Window

namespace Window {

class SystemMediaControlsManager {
public:
	SystemMediaControlsManager(not_null<Window::Controller*> controller);
	~SystemMediaControlsManager();

	static bool Supported();

private:
	const std::unique_ptr<base::Platform::SystemMediaControls> _controls;

	std::vector<std::shared_ptr<Data::DocumentMedia>> _cachedMediaView;

	rpl::lifetime _lifetimeDownload;
	rpl::lifetime _lifetime;
};

}  // namespace Window
