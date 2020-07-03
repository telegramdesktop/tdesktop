/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_audio.h"

#include "core/sandbox.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"
#include "styles/style_media_player.h"

#import <AppKit/NSButton.h>
#import <AppKit/NSCustomTouchBarItem.h>
#import <AppKit/NSSlider.h>
#import <AppKit/NSSliderTouchBarItem.h>

#ifndef OS_OSX

NSImage *qt_mac_create_nsimage(const QPixmap &pm);
using TouchBar::kCircleDiameter;
using TouchBar::CreateNSImageFromStyleIcon;

namespace {

constexpr auto kSongType = AudioMsgId::Type::Song;

const auto *kCustomizationIdPlayer = @"telegram.touchbar";

inline NSTouchBarItemIdentifier Format(NSString *s) {
	return [NSString stringWithFormat:@"%@.%@", kCustomizationIdPlayer, s];
}
const auto kSeekBarItemIdentifier = Format(@"seekbar");
const auto kPlayItemIdentifier = Format(@"play");
const auto kNextItemIdentifier = Format(@"nextItem");
const auto kPreviousItemIdentifier = Format(@"previousItem");
const auto kClosePlayerItemIdentifier = Format(@"closePlayer");
const auto kCurrentPositionItemIdentifier = Format(@"currentPosition");

API_AVAILABLE(macos(10.12.2))
NSButton* CreateTouchBarButton(
		const style::icon &icon,
		rpl::lifetime &lifetime,
		Fn<void()> callback) {
	id block = [^{
		Core::Sandbox::Instance().customEnterFromEventLoop(callback);
	} copy];

	NSButton* button = [NSButton
		buttonWithImage:CreateNSImageFromStyleIcon(icon, kCircleDiameter / 2)
		target:block
		action:@selector(invoke)];
	lifetime.add([=] {
		[block release];
	});
	return button;
}

} // namespace

#pragma mark - TouchBarAudioPlayer

@interface TouchBarAudioPlayer()
@end // @interface TouchBarAudioPlayer

@implementation TouchBarAudioPlayer {
	rpl::event_stream<> _closeRequests;
	rpl::producer< Media::Player::TrackState> _trackState;

	rpl::lifetime _lifetime;
}

- (id)init {
	self = [super init];
	if (!self) {
		return self;
	}
	self.delegate = self;
	self.customizationIdentifier = kCustomizationIdPlayer.lowercaseString;
	self.defaultItemIdentifiers = @[
		kPlayItemIdentifier,
		kPreviousItemIdentifier,
		kNextItemIdentifier,
		kSeekBarItemIdentifier,
		kClosePlayerItemIdentifier];
	self.customizationAllowedItemIdentifiers = @[
		kPlayItemIdentifier,
		kPreviousItemIdentifier,
		kNextItemIdentifier,
		// kCurrentPositionItemIdentifier, // TODO.
		kSeekBarItemIdentifier,
		kClosePlayerItemIdentifier];

	_trackState = Media::Player::instance()->updatedNotifier(
	) | rpl::filter([=](const Media::Player::TrackState &state) {
		return state.id.type() == kSongType;
	});

	return self;
}

- (NSTouchBarItem*)touchBar:(NSTouchBar*)touchBar
		makeItemForIdentifier:(NSTouchBarItemIdentifier)itemId {
	if (!touchBar) {
		return nil;
	}
	const auto mediaPlayer = Media::Player::instance();
	const auto isEqual = [&](NSString *string) {
		return [itemId isEqualToString:string];
	};

	if (isEqual(kSeekBarItemIdentifier)) {
		auto *item = [[NSSliderTouchBarItem alloc] initWithIdentifier:itemId];
		item.slider.minValue = 0.0f;
		item.slider.maxValue = 1.0f;
		item.customizationLabel = @"Seek Bar";

		id block = [^{
			// https://stackoverflow.com/a/45891017
			auto *event = [[NSApplication sharedApplication] currentEvent];
			const auto touchUp = [event
				touchesMatchingPhase:NSTouchPhaseEnded
				inView:nil].count > 0;
			Core::Sandbox::Instance().customEnterFromEventLoop([=] {
				if (touchUp) {
					mediaPlayer->finishSeeking(kSongType, item.doubleValue);
				} else {
					mediaPlayer->startSeeking(kSongType);
				}
			});
		} copy];

		rpl::duplicate(
			_trackState
		) | rpl::start_with_next([=](const Media::Player::TrackState &state) {
			const auto stop = Media::Player::IsStoppedOrStopping(state.state);
			const auto duration = double(stop ? 0 : state.length);
			auto slider = item.slider;
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
				}
			}
		}, _lifetime);

		item.target = block;
		item.action = @selector(invoke);
		_lifetime.add([=] {
			[block release];
		});
		return [item autorelease];
	} else if (isEqual(kNextItemIdentifier)
			|| isEqual(kPreviousItemIdentifier)) {
		const auto isNext = isEqual(kNextItemIdentifier);
		auto *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:itemId];

		auto *button = CreateTouchBarButton(
			isNext
				? st::touchBarIconPlayerNext
				: st::touchBarIconPlayerPrevious,
			_lifetime,
			[=] { isNext // TODO
				? mediaPlayer->next(kSongType)
				: mediaPlayer->previous(kSongType); });
		rpl::duplicate(
			_trackState
		) | rpl::start_with_next([=] {
			const auto newValue = isNext
				? mediaPlayer->nextAvailable(kSongType)
				: mediaPlayer->previousAvailable(kSongType);
			if (button.enabled != newValue) {
				button.enabled = newValue;
			}
		}, _lifetime);

		item.view = button;
		item.customizationLabel = [NSString
			stringWithFormat:@"%@ Playlist Item",
			isNext ? @"Next" : @"Previous"];
		return [item autorelease];
	} else if (isEqual(kPlayItemIdentifier)) {
		auto *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:itemId];

		auto *button = CreateTouchBarButton(
			st::touchBarIconPlayerPause,
			_lifetime,
			[=] { mediaPlayer->playPause(kSongType); });

		auto *pause = [button.image retain];
		auto *play = [CreateNSImageFromStyleIcon(
			st::touchBarIconPlayerPlay,
			kCircleDiameter / 2) retain];

		rpl::duplicate(
			_trackState
		) | rpl::start_with_next([=](const auto &state) {
			button.image = (state.state == Media::Player::State::Playing)
				? pause
				: play;
		}, _lifetime);

		_lifetime.add([=] {
			// Avoid a memory leak from retaining of images.
			[pause release];
			[play release];
		});

		item.view = button;
		item.customizationLabel = @"Play/Pause";
		return [item autorelease];
	} else if (isEqual(kClosePlayerItemIdentifier)) {
		auto *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:itemId];
		auto *button = CreateTouchBarButton(
			st::touchBarIconPlayerClose,
			_lifetime,
			[=] { _closeRequests.fire({}); });

		item.view = button;
		item.customizationLabel = @"Close Player";
		return [item autorelease];
	}
	return nil;
}

- (rpl::producer<>)closeRequests {
	return _closeRequests.events();
}

@end // @implementation TouchBarAudioPlayer

#endif // OS_OSX
