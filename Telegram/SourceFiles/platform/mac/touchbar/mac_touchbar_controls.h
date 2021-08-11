/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player
} // namespace Media

@class NSButton;
@class NSCustomTouchBarItem;
@class NSImage;
@class NSSliderTouchBarItem;

namespace TouchBar {

[[nodiscard]] API_AVAILABLE(macos(10.12.2))
NSButton *CreateTouchBarButton(
	NSImage *image,
	rpl::lifetime &lifetime,
	Fn<void()> callback);

[[nodiscard]] API_AVAILABLE(macos(10.12.2))
NSButton *CreateTouchBarButton(
	const style::icon &icon,
	rpl::lifetime &lifetime,
	Fn<void()> callback);

[[nodiscard]] API_AVAILABLE(macos(10.12.2))
NSButton *CreateTouchBarButtonWithTwoStates(
	NSImage *icon1,
	NSImage *icon2,
	rpl::lifetime &lifetime,
	Fn<void(bool)> callback,
	bool firstState,
	rpl::producer<bool> stateChanged = rpl::never<bool>());

[[nodiscard]] API_AVAILABLE(macos(10.12.2))
NSButton *CreateTouchBarButtonWithTwoStates(
	const style::icon &icon1,
	const style::icon &icon2,
	rpl::lifetime &lifetime,
	Fn<void(bool)> callback,
	bool firstState,
	rpl::producer<bool> stateChanged = rpl::never<bool>());

[[nodiscard]] API_AVAILABLE(macos(10.12.2))
NSSliderTouchBarItem *CreateTouchBarSlider(
	NSString *itemId,
	rpl::lifetime &lifetime,
	Fn<void(bool, double, double)> callback,
	rpl::producer<Media::Player::TrackState> stateChanged);

[[nodiscard]] API_AVAILABLE(macos(10.12.2))
NSCustomTouchBarItem *CreateTouchBarTrackPosition(
	NSString *itemId,
	rpl::producer<Media::Player::TrackState> stateChanged);

} // namespace TouchBar
