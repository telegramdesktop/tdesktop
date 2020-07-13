/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef OS_OSX

#import <AppKit/NSImage.h>
#import <Foundation/Foundation.h>

namespace TouchBar {

constexpr auto kCircleDiameter = 30;

template <typename Callable>
void CustomEnterToCocoaEventLoop(Callable callable) {
	id block = [^{ callable(); } copy]; // Don't forget to -release.
	[block
		performSelectorOnMainThread:@selector(invoke)
		withObject:nil
		waitUntilDone:true];
	// [block performSelector:@selector(invoke) withObject:nil afterDelay:d];
	[block release];
}

int WidthFromString(NSString *s);

NSImage *CreateNSImageFromStyleIcon(const style::icon &icon, int size);

} // namespace TouchBar

#endif // OS_OSX
