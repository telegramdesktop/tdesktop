/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#import <AppKit/NSPopoverTouchBarItem.h>
#import <AppKit/NSTouchBar.h>

API_AVAILABLE(macos(10.12.2))
@interface TextFormatPopover : NSPopoverTouchBarItem
- (id)init:(NSTouchBarItemIdentifier)identifier;
@end
