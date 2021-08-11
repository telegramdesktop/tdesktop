/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#import <AppKit/NSTouchBar.h>

namespace Main {
class Domain;
} // namespace Main

namespace Window {
class Controller;
} // namespace Window

API_AVAILABLE(macos(10.12.2))
@interface RootTouchBar : NSTouchBar<NSTouchBarDelegate>
- (id)init:(rpl::producer<bool>)canApplyMarkdown
	controller:(not_null<Window::Controller*>)controller
	domain:(not_null<Main::Domain*>)domain;
@end
