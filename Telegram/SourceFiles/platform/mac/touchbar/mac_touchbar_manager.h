/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
