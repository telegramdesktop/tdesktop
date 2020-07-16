/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/items/mac_formatter_item.h"

#ifndef OS_OSX

#include "base/platform/mac/base_utilities_mac.h"
#include "lang/lang_keys.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"

#import <AppKit/NSCustomTouchBarItem.h>
#import <AppKit/NSScrollView.h>
#import <AppKit/NSSegmentedControl.h>

#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

namespace {

constexpr auto kCommandBold = 0x010;
constexpr auto kCommandItalic = 0x011;
constexpr auto kCommandUnderline = 0x012;
constexpr auto kCommandStrikeOut = 0x013;
constexpr auto kCommandMonospace = 0x014;
constexpr auto kCommandClear = 0x015;
constexpr auto kCommandLink = 0x016;

const auto kPopoverFormatter = @"popoverInputFormatter";

void SendKeyEvent(int command) {
	auto *focused = qobject_cast<QTextEdit*>(QApplication::focusWidget());
	if (!focused) {
		return;
	}
	auto key = 0;
	auto modifier = Qt::KeyboardModifiers(0) | Qt::ControlModifier;
	switch (command) {
	case kCommandBold:
		key = Qt::Key_B;
		break;
	case kCommandItalic:
		key = Qt::Key_I;
		break;
	case kCommandMonospace:
		key = Qt::Key_M;
		modifier |= Qt::ShiftModifier;
		break;
	case kCommandClear:
		key = Qt::Key_N;
		modifier |= Qt::ShiftModifier;
		break;
	case kCommandLink:
		key = Qt::Key_K;
		break;
	case kCommandUnderline:
		key = Qt::Key_U;
		break;
	case kCommandStrikeOut:
		key = Qt::Key_X;
		modifier |= Qt::ShiftModifier;
		break;
	}
	QApplication::postEvent(
		focused,
		new QKeyEvent(QEvent::KeyPress, key, modifier));
	QApplication::postEvent(
		focused,
		new QKeyEvent(QEvent::KeyRelease, key, modifier));
}

} // namespace

#pragma mark - TextFormatPopover

@implementation TextFormatPopover {
	rpl::lifetime _lifetime;
}

- (id)init:(NSTouchBarItemIdentifier)identifier {
	self = [super initWithIdentifier:identifier];
	if (!self) {
		return nil;
	}

	self.collapsedRepresentationImage = [NSImage
		imageNamed:NSImageNameTouchBarTextItalicTemplate]; // autorelease];
	auto *secondaryTouchBar = [[[NSTouchBar alloc] init] autorelease];

	auto *popover = [[[NSCustomTouchBarItem alloc]
		initWithIdentifier:kPopoverFormatter] autorelease];
	{
		auto *scroll = [[[NSScrollView alloc] init] autorelease];
		auto *segment = [[[NSSegmentedControl alloc] init] autorelease];
		segment.segmentStyle = NSSegmentStyleRounded;
		segment.target = self;
		segment.action = @selector(segmentClicked:);

		static const auto strings = {
			tr::lng_menu_formatting_bold,
			tr::lng_menu_formatting_italic,
			tr::lng_menu_formatting_underline,
			tr::lng_menu_formatting_strike_out,
			tr::lng_menu_formatting_monospace,
			tr::lng_menu_formatting_clear,
			tr::lng_info_link_label,
		};
		segment.segmentCount = strings.size();
		auto width = 0;
		auto count = 0;
		for (const auto &s : strings) {
			const auto string = Platform::Q2NSString(s(tr::now));
			width += TouchBar::WidthFromString(string) * 1.4;
			[segment setLabel:string forSegment:count++];
		}
		segment.frame = NSMakeRect(0, 0, width, TouchBar::kCircleDiameter);
		[scroll setDocumentView:segment];
		popover.view = scroll;
	}

	secondaryTouchBar.templateItems = [NSSet setWithArray:@[popover]];
	secondaryTouchBar.defaultItemIdentifiers = @[kPopoverFormatter];

	self.popoverTouchBar = secondaryTouchBar;
	return self;
}

- (void)segmentClicked:(NSSegmentedControl*)sender {
	const auto command = int(sender.selectedSegment) + kCommandBold;
	sender.selectedSegment = -1;
	SendKeyEvent(command);

	[self dismissPopover:nil];
}

@end // @implementation TextFormatPopover

#endif // OS_OSX
