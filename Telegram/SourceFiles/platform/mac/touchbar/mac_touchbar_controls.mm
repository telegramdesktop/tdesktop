/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_controls.h"

#include "core/sandbox.h" // Sandbox::customEnterFromEventLoop()
#include "media/audio/media_audio.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"

#import <AppKit/NSButton.h>
#import <AppKit/NSImage.h>
#import <AppKit/NSSlider.h>
#import <AppKit/NSSliderTouchBarItem.h>

namespace {

inline NSImage *Icon(const style::icon &icon) {
	using namespace TouchBar;
	return CreateNSImageFromStyleIcon(icon, kCircleDiameter / 2);
}

} // namespace

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

} // namespace TouchBar
