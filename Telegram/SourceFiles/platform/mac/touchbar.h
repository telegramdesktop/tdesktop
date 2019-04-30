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
};
} // namespace

static NSString * _Nullable BASE_ID = @"telegram.touchbar";
static NSTouchBarCustomizationIdentifier _Nullable customID = @"telegram.touchbar";
static NSTouchBarCustomizationIdentifier _Nullable customIDMain = @"telegram.touchbarMain";
static NSTouchBarItemIdentifier _Nullable savedMessages = [NSString stringWithFormat:@"%@.savedMessages", customIDMain];
static NSTouchBarItemIdentifier _Nullable seekBar = [NSString stringWithFormat:@"%@.seekbar", BASE_ID];
static NSTouchBarItemIdentifier _Nullable play = [NSString stringWithFormat:@"%@.play", BASE_ID];
static NSTouchBarItemIdentifier _Nullable nextItem = [NSString stringWithFormat:@"%@.nextItem", BASE_ID];
static NSTouchBarItemIdentifier _Nullable previousItem = [NSString stringWithFormat:@"%@.previousItem", BASE_ID];
static NSTouchBarItemIdentifier _Nullable closePlayer = [NSString stringWithFormat:@"%@.closePlayer", BASE_ID];
static NSTouchBarItemIdentifier _Nullable currentPosition = [NSString stringWithFormat:@"%@.currentPosition", BASE_ID];

@interface TouchBar : NSTouchBar
@property TouchBarType touchBarType;

@property(retain) NSDictionary * _Nullable touchbarItems;
@property(retain) NSTouchBar * _Nullable touchBarMain;
@property(retain) NSTouchBar * _Nullable touchBarAudioPlayer;
@property(retain) NSView * _Nullable view;
@property(nonatomic, assign) double duration;
@property(nonatomic, assign) double position;

- (id _Nonnull) init:(NSView * _Nonnull)view;
- (void)handlePropertyChange:(Media::Player::TrackState)property;

@end
