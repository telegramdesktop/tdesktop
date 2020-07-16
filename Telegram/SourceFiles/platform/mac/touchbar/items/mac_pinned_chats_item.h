/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef OS_OSX

#include <AppKit/NSImageView.h>

namespace Main {
class Session;
} // namespace Main

API_AVAILABLE(macos(10.12.2))
@interface PinnedDialogsPanel : NSImageView
- (id)init:(not_null<Main::Session*>)session
	destroyEvent:(rpl::producer<>)touchBarSwitches;
@end

#endif // OS_OSX
