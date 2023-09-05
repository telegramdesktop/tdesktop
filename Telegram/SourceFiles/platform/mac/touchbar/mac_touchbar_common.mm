/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_common.h"

#include "base/platform/mac/base_utilities_mac.h"

#import <AppKit/NSTextField.h>

namespace TouchBar {

int WidthFromString(NSString *s) {
	return (int)ceil(
		[[NSTextField labelWithString:s] frame].size.width) * 1.2;
}

NSImage *CreateNSImageFromStyleIcon(const style::icon &icon, int size) {
	auto instance = icon.instance(QColor(255, 255, 255, 255), 100);
	instance.setDevicePixelRatio(cRetinaFactor());
	NSImage *image = Platform::Q2NSImage(instance);
	[image setSize:NSMakeSize(size, size)];
	return image;
}

} // namespace TouchBar
