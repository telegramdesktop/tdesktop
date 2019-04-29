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

static NSString * _Nullable BASE_ID = @"telegram.touchbar";
static NSTouchBarCustomizationIdentifier _Nullable customID = @"telegram.touchbar";
static NSTouchBarItemIdentifier _Nullable seekBar = [NSString stringWithFormat:@"%@.seekbar", BASE_ID];
static NSTouchBarItemIdentifier _Nullable play = [NSString stringWithFormat:@"%@.play", BASE_ID];
static NSTouchBarItemIdentifier _Nullable nextItem = [NSString stringWithFormat:@"%@.nextItem", BASE_ID];
static NSTouchBarItemIdentifier _Nullable previousItem = [NSString stringWithFormat:@"%@.previousItem", BASE_ID];
static NSTouchBarItemIdentifier _Nullable nextChapter = [NSString stringWithFormat:@"%@.nextChapter", BASE_ID];
static NSTouchBarItemIdentifier _Nullable previousChapter = [NSString stringWithFormat:@"%@.previousChapter", BASE_ID];
static NSTouchBarItemIdentifier _Nullable cycleAudio = [NSString stringWithFormat:@"%@.cycleAudio", BASE_ID];
static NSTouchBarItemIdentifier _Nullable cycleSubtitle = [NSString stringWithFormat:@"%@.cycleSubtitle", BASE_ID];
static NSTouchBarItemIdentifier _Nullable currentPosition = [NSString stringWithFormat:@"%@.currentPosition", BASE_ID];
static NSTouchBarItemIdentifier _Nullable timeLeft = [NSString stringWithFormat:@"%@.timeLeft", BASE_ID];

@interface TouchBar : NSTouchBar
@property Platform::TouchBarType touchBarType;

@property(retain) NSDictionary * _Nullable touchbarItems;
@property(nonatomic, assign) double duration;
@property(nonatomic, assign) double position;

- (nullable NSTouchBar *) makeTouchBar;
- (void)handlePropertyChange:(Media::Player::TrackState)property;

@end
