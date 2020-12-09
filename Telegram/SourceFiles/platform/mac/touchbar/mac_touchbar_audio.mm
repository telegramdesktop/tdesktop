/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_audio.h"

#ifndef OS_OSX

#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"
#include "platform/mac/touchbar/mac_touchbar_controls.h"
#include "styles/style_media_player.h"

#import <AppKit/NSButton.h>
#import <AppKit/NSCustomTouchBarItem.h>
#import <AppKit/NSSlider.h>
#import <AppKit/NSSliderTouchBarItem.h>

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
		kCurrentPositionItemIdentifier,
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
		auto *item = TouchBar::CreateTouchBarSlider(
			itemId,
			_lifetime,
			[=](bool touchUp, double value, double duration) {
				if (touchUp) {
					mediaPlayer->finishSeeking(kSongType, value);
				} else {
					mediaPlayer->startSeeking(kSongType);
				}
			},
			rpl::duplicate(_trackState));
		return [item autorelease];
	} else if (isEqual(kNextItemIdentifier)
			|| isEqual(kPreviousItemIdentifier)) {
		const auto isNext = isEqual(kNextItemIdentifier);
		auto *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:itemId];

		auto *button = TouchBar::CreateTouchBarButton(
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

		auto *button = TouchBar::CreateTouchBarButtonWithTwoStates(
			st::touchBarIconPlayerPause,
			st::touchBarIconPlayerPlay,
			_lifetime,
			[=](bool value) { mediaPlayer->playPause(kSongType); },
			false,
			rpl::duplicate(
				_trackState
			) | rpl::map([](const auto &state) {
				return (state.state == Media::Player::State::Playing);
			}) | rpl::distinct_until_changed());

		item.view = button;
		item.customizationLabel = @"Play/Pause";
		return [item autorelease];
	} else if (isEqual(kClosePlayerItemIdentifier)) {
		auto *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:itemId];
		auto *button = TouchBar::CreateTouchBarButton(
			st::touchBarIconPlayerClose,
			_lifetime,
			[=] { _closeRequests.fire({}); });

		item.view = button;
		item.customizationLabel = @"Close Player";
		return [item autorelease];
	} else if (isEqual(kCurrentPositionItemIdentifier)) {
		auto *item = TouchBar::CreateTouchBarTrackPosition(
			itemId,
			rpl::duplicate(_trackState));
		return [item autorelease];
	}
	return nil;
}

- (rpl::producer<>)closeRequests {
	return _closeRequests.events();
}

@end // @implementation TouchBarAudioPlayer

#endif // OS_OSX
