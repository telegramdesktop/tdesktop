/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_media_view.h"

#ifndef OS_OSX

#include "media/audio/media_audio.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"
#include "platform/mac/touchbar/mac_touchbar_controls.h"
#include "styles/style_media_player.h"
#include "styles/style_media_view.h"

#import <AppKit/NSButton.h>
#import <AppKit/NSCustomTouchBarItem.h>
#import <AppKit/NSTouchBar.h>

using namespace TouchBar;
using Delegate = Media::View::PlaybackControls::Delegate;
using ItemType = Media::View::OverlayWidget::TouchBarItemType;

namespace {

inline NSTouchBarItemIdentifier Format(NSString *s) {
	return [NSString stringWithFormat:@"button.%@", s];
}

const auto kPlayItemIdentifier = Format(@"playPause");
const auto kRotateItemIdentifier = Format(@"rotate");
const auto kFullscreenItemIdentifier = Format(@"fullscreen");
const auto kPipItemIdentifier = Format(@"pip");
const auto kTrackItemIdentifier = @"trackPosition";
const auto kSeekItemIdentifier = @"seekBar";

}

#pragma mark - MediaViewTouchBar

@interface MediaViewTouchBar : NSTouchBar
- (id)init:(not_null<Delegate*>)controlsDelegate
	trackState:(rpl::producer<Media::Player::TrackState>)trackState
	display:(rpl::producer<ItemType>)display
	fullscreenToggled:(rpl::producer<bool>)fullscreenToggled;
@end

@implementation MediaViewTouchBar {
	rpl::lifetime _lifetime;
}

- (id)init:(not_null<Delegate*>)controlsDelegate
		trackState:(rpl::producer<Media::Player::TrackState>)trackState
		display:(rpl::producer<ItemType>)display
		fullscreenToggled:(rpl::producer<bool>)fullscreenToggled {
	self = [super init];
	if (!self) {
		return self;
	}
	const auto allocate = [](NSTouchBarItemIdentifier i) {
		return [[NSCustomTouchBarItem alloc] initWithIdentifier:i];
	};

	auto *playPause = allocate(kPlayItemIdentifier);
	{
		auto *button = CreateTouchBarButtonWithTwoStates(
			st::touchBarIconPlayerPause,
			st::touchBarIconPlayerPlay,
			_lifetime,
			[=](bool value) {
				value
					? controlsDelegate->playbackControlsPlay()
					: controlsDelegate->playbackControlsPause();
			},
			false,
			rpl::duplicate(
				trackState
			) | rpl::map([](const auto &state) {
				return (state.state == Media::Player::State::Playing);
			}) | rpl::distinct_until_changed());
		playPause.view = button;
		playPause.customizationLabel = @"Play/Pause";
	}

	auto *rotate = allocate(kRotateItemIdentifier);
	{
		auto *button = CreateTouchBarButton(
			[NSImage imageNamed:NSImageNameTouchBarRotateLeftTemplate],
			_lifetime,
			[=] { controlsDelegate->playbackControlsRotate(); });
		rotate.view = button;
		rotate.customizationLabel = @"Rotate";
	}

	auto *fullscreen = allocate(kFullscreenItemIdentifier);
	{
		auto *button = CreateTouchBarButtonWithTwoStates(
			[NSImage imageNamed:NSImageNameTouchBarExitFullScreenTemplate],
			[NSImage imageNamed:NSImageNameTouchBarEnterFullScreenTemplate],
			_lifetime,
			[=](bool value) {
				value
					? controlsDelegate->playbackControlsFromFullScreen()
					: controlsDelegate->playbackControlsToFullScreen();
			},
			true,
			std::move(fullscreenToggled));
		fullscreen.view = button;
		fullscreen.customizationLabel = @"Fullscreen";
	}

	auto *pip = allocate(kPipItemIdentifier);
	{
		auto *button = TouchBar::CreateTouchBarButton(
			CreateNSImageFromStyleIcon(
				st::mediaviewPipButton.icon,
				kCircleDiameter / 4 * 3),
			_lifetime,
			[=] { controlsDelegate->playbackControlsToPictureInPicture(); });
		pip.view = button;
		pip.customizationLabel = @"Picture-in-Picture";
	}

	auto *trackPosition = CreateTouchBarTrackPosition(
		kTrackItemIdentifier,
		rpl::duplicate(trackState));

	auto *seekBar = TouchBar::CreateTouchBarSlider(
		kSeekItemIdentifier,
		_lifetime,
		[=](bool touchUp, double value, double duration) {
			const auto progress = value * duration;
			touchUp
				? controlsDelegate->playbackControlsSeekFinished(progress)
				: controlsDelegate->playbackControlsSeekProgress(progress);
		},
		std::move(trackState));

	self.templateItems = [NSSet setWithArray:@[
		playPause,
		rotate,
		fullscreen,
		pip,
		seekBar,
		trackPosition]];

	const auto items = [](ItemType type) {
		switch (type) {
		case ItemType::Photo: return @[kRotateItemIdentifier];
		case ItemType::Video: return @[
			kRotateItemIdentifier,
			kFullscreenItemIdentifier,
			kPipItemIdentifier,
			kPlayItemIdentifier,
			kSeekItemIdentifier,
			kTrackItemIdentifier];
		default: return @[];
		};
	};

	std::move(
		display
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](ItemType type) {
		TouchBar::CustomEnterToCocoaEventLoop([=] {
			self.defaultItemIdentifiers = items(type);
		});
	}, _lifetime);

	return self;
}

@end // @implementation MediaViewTouchBar

namespace TouchBar {

void SetupMediaViewTouchBar(
		WId winId,
		not_null<Delegate*> controlsDelegate,
		rpl::producer<Media::Player::TrackState> trackState,
		rpl::producer<ItemType> display,
		rpl::producer<bool> fullscreenToggled) {
	auto *window = [reinterpret_cast<NSView*>(winId) window];
	CustomEnterToCocoaEventLoop([=] {
		[window setTouchBar:[[[MediaViewTouchBar alloc]
			init:std::move(controlsDelegate)
			trackState:std::move(trackState)
			display:std::move(display)
			fullscreenToggled:std::move(fullscreenToggled)
		] autorelease]];
	});
}

} // namespace TouchBar

#endif // OS_OSX
