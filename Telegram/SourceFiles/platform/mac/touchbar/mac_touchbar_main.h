/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#import <AppKit/NSTouchBar.h>

namespace Window {
class Controller;
} // namespace Window

namespace TouchBar::Main {

const auto kPinnedPanelItemIdentifier = @"pinnedPanel";
const auto kPopoverInputItemIdentifier = @"popoverInput";
const auto kPopoverPickerItemIdentifier = @"pickerButtons";

} // namespace TouchBar::Main

API_AVAILABLE(macos(10.12.2))
@interface TouchBarMain : NSTouchBar
- (id)init:(not_null<Window::Controller*>)controller
	touchBarSwitches:(rpl::producer<>)touchBarSwitches;
@end
