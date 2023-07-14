/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#import <AppKit/NSPopoverTouchBarItem.h>
#import <AppKit/NSTouchBar.h>

API_AVAILABLE(macos(10.12.2))
@interface TextFormatPopover : NSPopoverTouchBarItem
- (id)init:(NSTouchBarItemIdentifier)identifier;
@end
