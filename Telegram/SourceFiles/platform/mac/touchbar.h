/*
 This file is part of Telegram Desktop,
 the official desktop application for the Telegram messaging service.
 
 For license and copyright information please follow this link:
 https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
 */

#include "platform/platform_specific.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#import <Cocoa/Cocoa.h>

namespace {
enum class TouchBarType {
	None,
	Main,
	AudioPlayer,
	AudioPlayerForce,
};
} // namespace

@interface TouchBar : NSTouchBar {
	rpl::lifetime lifetime;
}
@property TouchBarType touchBarType;
@property TouchBarType touchBarTypeBeforeLock;

@property(retain) NSDictionary * _Nullable touchbarItems;
@property(retain) NSTouchBar * _Nullable touchBarMain;
@property(retain) NSTouchBar * _Nullable touchBarAudioPlayer;
@property(retain) NSView * _Nullable view;
@property(nonatomic, assign) double duration;
@property(nonatomic, assign) double position;

@property(retain) NSMutableArray * _Nullable mainPinnedButtons;

- (id _Nonnull) init:(NSView * _Nonnull)view;
- (void) handleTrackStateChange:(Media::Player::TrackState)property;
- (void) setTouchBar:(TouchBarType)type;

@end
