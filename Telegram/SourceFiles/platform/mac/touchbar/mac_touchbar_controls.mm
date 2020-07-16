/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_controls.h"

#ifndef OS_OSX

#include "base/platform/mac/base_utilities_mac.h" // Q2NSString()
#include "core/sandbox.h" // Sandbox::customEnterFromEventLoop()
#include "layout.h" // formatDurationText()
#include "media/audio/media_audio.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"

#import <AppKit/NSButton.h>
#import <AppKit/NSCustomTouchBarItem.h>
#import <AppKit/NSImage.h>
#import <AppKit/NSImageView.h>
#import <AppKit/NSSlider.h>
#import <AppKit/NSSliderTouchBarItem.h>

using namespace TouchBar;

namespace {

constexpr auto kPadding = 7;

inline NSImage *Icon(const style::icon &icon) {
	return CreateNSImageFromStyleIcon(icon, kCircleDiameter / 2);
}

inline NSDictionary *Attributes() {
	return @{
		NSFontAttributeName: [NSFont systemFontOfSize:14],
		NSParagraphStyleAttributeName:
			[NSMutableParagraphStyle defaultParagraphStyle],
		NSForegroundColorAttributeName: [NSColor whiteColor]
	};
}

inline NSString *FormatTime(TimeId time) {
	return Platform::Q2NSString(formatDurationText(time));
}

} // namespace

#pragma mark - TrackPosition

@interface TrackPosition : NSImageView
@end // @interface TrackPosition

@implementation TrackPosition {
	NSMutableString *_text;

	double _width;
	double _height;

	rpl::lifetime _lifetime;
}

- (id)init:(rpl::producer< Media::Player::TrackState>)trackState {
	self = [super init];
	const auto textLength = _lifetime.make_state<rpl::variable<int>>(0);
	_width = _height = 0;
	_text = [[NSMutableString alloc] initWithCapacity:13];

	rpl::combine(
		rpl::duplicate(
			trackState
		) | rpl::map([](const auto &state) {
			return state.position / 1000;
		}) | rpl::distinct_until_changed(),
		std::move(
			trackState
		) | rpl::map([](const auto &state) {
			return state.length / 1000;
		}) | rpl::distinct_until_changed()
	) | rpl::start_with_next([=](int position, int length) {
		[_text setString:[NSString stringWithFormat:@"%@ / %@",
			FormatTime(position),
			FormatTime(length)]];
		*textLength = _text.length;

		[self display];
	}, _lifetime);

	textLength->changes(
	) | rpl::start_with_next([=] {
		const auto size = [_text sizeWithAttributes:Attributes()];
		_width = size.width + kPadding * 2;
		_height = size.height;

		if (self.image) {
			[self.image release];
		}
		self.image = [[NSImage alloc] initWithSize:NSMakeSize(
			_width,
			kCircleDiameter)];
	}, _lifetime);

	return self;
}

- (void)drawRect:(NSRect)dirtyRect {
	if (!(_text && _text.length && _width && _height)) {
		return;
	}
	const auto size = [_text sizeWithAttributes:Attributes()];
	const auto rect = CGRectMake(
		(_width - size.width) / 2,
		-(kCircleDiameter - _height) / 2,
		_width,
		kCircleDiameter);
	[_text drawInRect:rect withAttributes:Attributes()];
}

- (void)dealloc {
	if (self.image) {
		[self.image release];
	}
	if (_text) {
		[_text release];
	}
	[super dealloc];
}

@end // @implementation TrackPosition

namespace TouchBar {

NSButton *CreateTouchBarButton(
		// const style::icon &icon,
		NSImage *image,
		rpl::lifetime &lifetime,
		Fn<void()> callback) {
	id block = [^{
		Core::Sandbox::Instance().customEnterFromEventLoop(callback);
	} copy];

	NSButton* button = [NSButton
		buttonWithImage:image
		target:block
		action:@selector(invoke)];
	lifetime.add([=] {
		[block release];
	});
	return button;
}

NSButton *CreateTouchBarButton(
	const style::icon &icon,
	rpl::lifetime &lifetime,
	Fn<void()> callback) {
	return CreateTouchBarButton(Icon(icon), lifetime, std::move(callback));
}

NSButton *CreateTouchBarButtonWithTwoStates(
		NSImage *icon1,
		NSImage *icon2,
		rpl::lifetime &lifetime,
		Fn<void(bool)> callback,
		bool firstState,
		rpl::producer<bool> stateChanged) {
	NSButton* button = [NSButton
		buttonWithImage:(firstState ? icon2 : icon1)
		target:nil
		action:nil];

	const auto isFirstState = lifetime.make_state<bool>(firstState);
	id block = [^{
		const auto state = *isFirstState;
		button.image = state ? icon1 : icon2;
		*isFirstState = !state;
		Core::Sandbox::Instance().customEnterFromEventLoop([=] {
			callback(state);
		});
	} copy];

	button.target = block;
	button.action = @selector(invoke);

	std::move(
		stateChanged
	) | rpl::start_with_next([=](bool isChangedToFirstState) {
		button.image = isChangedToFirstState ? icon1 : icon2;
	}, lifetime);

	lifetime.add([=] {
		[block release];
	});
	return button;
}

NSButton *CreateTouchBarButtonWithTwoStates(
		const style::icon &icon1,
		const style::icon &icon2,
		rpl::lifetime &lifetime,
		Fn<void(bool)> callback,
		bool firstState,
		rpl::producer<bool> stateChanged) {
	return CreateTouchBarButtonWithTwoStates(
		Icon(icon1),
		Icon(icon2),
		lifetime,
		std::move(callback),
		firstState,
		std::move(stateChanged));
}

NSSliderTouchBarItem *CreateTouchBarSlider(
		NSString *itemId,
		rpl::lifetime &lifetime,
		Fn<void(bool, double, double)> callback,
		rpl::producer<Media::Player::TrackState> stateChanged) {
	const auto lastDurationMs = lifetime.make_state<crl::time>(0);

	auto *seekBar = [[NSSliderTouchBarItem alloc] initWithIdentifier:itemId];
	seekBar.slider.minValue = 0.0f;
	seekBar.slider.maxValue = 1.0f;
	seekBar.customizationLabel = @"Seek Bar";

	id block = [^{
		// https://stackoverflow.com/a/45891017
		auto *event = [[NSApplication sharedApplication] currentEvent];
		const auto touchUp = [event
			touchesMatchingPhase:NSTouchPhaseEnded
			inView:nil].count > 0;
		Core::Sandbox::Instance().customEnterFromEventLoop([=] {
			callback(touchUp, seekBar.doubleValue, *lastDurationMs);
		});
	} copy];

	std::move(
		stateChanged
	) | rpl::start_with_next([=](const Media::Player::TrackState &state) {
		const auto stop = Media::Player::IsStoppedOrStopping(state.state);
		const auto duration = double(stop ? 0 : state.length);
		auto slider = seekBar.slider;
		if (duration <= 0) {
			slider.enabled = false;
			slider.doubleValue = 0;
		} else {
			slider.enabled = true;
			if (!slider.highlighted) {
				const auto pos = stop
					? 0
					: std::max(state.position, int64(0));
				slider.doubleValue = (pos / duration) * slider.maxValue;
				*lastDurationMs = duration;
			}
		}
	}, lifetime);

	seekBar.target = block;
	seekBar.action = @selector(invoke);
	lifetime.add([=] {
		[block release];
	});

	return seekBar;
}

NSCustomTouchBarItem *CreateTouchBarTrackPosition(
		NSString *itemId,
		rpl::producer<Media::Player::TrackState> stateChanged) {
	auto *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:itemId];
	auto *trackPosition = [[[TrackPosition alloc]
		init:std::move(stateChanged)] autorelease];

	item.view = trackPosition;
	item.customizationLabel = @"Track Position";
	return item;
}

} // namespace TouchBar

#endif // OS_OSX
