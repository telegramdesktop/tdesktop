/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#import <AppKit/NSPopoverTouchBarItem.h>
#import <AppKit/NSTouchBar.h>

namespace Window {
class Controller;
} // namespace Window

API_AVAILABLE(macos(10.12.2))
@interface StickerEmojiPopover : NSPopoverTouchBarItem<NSTouchBarDelegate>
- (id)init:(not_null<Window::Controller*>)controller
	identifier:(NSTouchBarItemIdentifier)identifier;
@end
