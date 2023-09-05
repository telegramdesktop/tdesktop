/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#import <AppKit/NSTouchBar.h>

API_AVAILABLE(macos(10.12.2))
@interface TouchBarAudioPlayer : NSTouchBar<NSTouchBarDelegate>
- (rpl::producer<>)closeRequests;
@end
