/*
 This file is part of Telegram Desktop,
 the official desktop application for the Telegram messaging service.
 
 For license and copyright information please follow this link:
 https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
 */

#import "touchbar.h"
#import <QuartzCore/QuartzCore.h>

#include "mainwindow.h"
#include "mainwidget.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/crash_reports.h"
#include "storage/localstorage.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_playback_progress.h"
#include "media/audio/media_audio.h"
#include "platform/mac/mac_utilities.h"
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "base/timer.h"
#include "styles/style_window.h"

namespace {
constexpr auto kSavedMessages = 0x001;

constexpr auto kPlayPause = 0x002;
constexpr auto kPlaylistPrevious = 0x003;
constexpr auto kPlaylistNext = 0x004;
constexpr auto kClosePlayer = 0x005;
	
constexpr auto kMs = 1000;
	
constexpr auto kSongType = AudioMsgId::Type::Song;
} // namespace

@interface TouchBar()<NSTouchBarDelegate>
@end

@implementation TouchBar

- (id)init:(NSView *)view {
	self = [super init];
	if (self) {
		self.view = view;
		self.touchbarItems = @{
			savedMessages: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"button",
				@"name":  @"Saved Messages",
				@"cmd":   [NSNumber numberWithInt:kSavedMessages],
                @"image": [NSImage imageNamed:NSImageNameTouchBarBookmarksTemplate],
			}],
			seekBar: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type": @"slider",
				@"name": @"Seek Bar"
			}],
			play: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":     @"button",
				@"name":     @"Play Button",
				@"cmd":      [NSNumber numberWithInt:kPlayPause],
				@"image":    [NSImage imageNamed:NSImageNameTouchBarPauseTemplate],
				@"imageAlt": [NSImage imageNamed:NSImageNameTouchBarPlayTemplate]
			}],
			previousItem: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"button",
				@"name":  @"Previous Playlist Item",
				@"cmd":   [NSNumber numberWithInt:kPlaylistPrevious],
				@"image": [NSImage imageNamed:NSImageNameTouchBarGoBackTemplate]
			}],
			nextItem: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"button",
				@"name":  @"Next Playlist Item",
				@"cmd":   [NSNumber numberWithInt:kPlaylistNext],
				@"image": [NSImage imageNamed:NSImageNameTouchBarGoForwardTemplate]
			}],
			closePlayer: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"button",
				@"name":  @"Close Player",
				@"cmd":   [NSNumber numberWithInt:kClosePlayer],
				@"image": [NSImage imageNamed:NSImageNameTouchBarExitFullScreenTemplate]
			}],
			currentPosition: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type": @"text",
				@"name": @"Current Position"
			}]
		};
	}
    [self createTouchBar];
    [self setTouchBar:TouchBarType::Main];
	
	return self;
}

- (void) createTouchBar{
	_touchBarMain = [[NSTouchBar alloc] init];
	_touchBarMain.delegate = self;
	
	_touchBarMain.customizationIdentifier = customIDMain;
	_touchBarMain.defaultItemIdentifiers = @[savedMessages];
	_touchBarMain.customizationAllowedItemIdentifiers = @[savedMessages];
	
    _touchBarAudioPlayer = [[NSTouchBar alloc] init];
    _touchBarAudioPlayer.delegate = self;

    _touchBarAudioPlayer.customizationIdentifier = customID;
    _touchBarAudioPlayer.defaultItemIdentifiers = @[play, previousItem, nextItem, seekBar, closePlayer];
    _touchBarAudioPlayer.customizationAllowedItemIdentifiers = @[play, previousItem,
                                                                nextItem, currentPosition, seekBar, closePlayer];
}

- (nullable NSTouchBarItem *)touchBar:(NSTouchBar *)touchBar
				makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
	if ([self.touchbarItems[identifier][@"type"] isEqualToString:@"slider"]) {
		NSSliderTouchBarItem *item = [[NSSliderTouchBarItem alloc] initWithIdentifier:identifier];
		item.slider.minValue = 0.0f;
		item.slider.maxValue = 1.0f;
		item.target = self;
		item.action = @selector(seekbarChanged:);
		item.customizationLabel = self.touchbarItems[identifier][@"name"];
		[self.touchbarItems[identifier] setObject:item.slider forKey:@"view"];
		return item;
	} else if ([self.touchbarItems[identifier][@"type"] isEqualToString:@"button"]) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSImage *image = self.touchbarItems[identifier][@"image"];
		NSButton *button = [NSButton buttonWithImage:image target:self action:@selector(buttonAction:)];
		item.view = button;
		item.customizationLabel = self.touchbarItems[identifier][@"name"];
		[self.touchbarItems[identifier] setObject:button forKey:@"view"];
		return item;
	} else if ([self.touchbarItems[identifier][@"type"] isEqualToString:@"text"]) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSTextField *text = [NSTextField labelWithString:@"0:00"];
		text.alignment = NSTextAlignmentCenter;
		item.view = text;
		item.customizationLabel = self.touchbarItems[identifier][@"name"];
		[self.touchbarItems[identifier] setObject:text forKey:@"view"];
		return item;
	}

	return nil;
}

- (void)setTouchBar:(TouchBarType)type {
	if (self.touchBarType == type) {
		return;
	}
	self.touchBarType = type;
	if (type == TouchBarType::Main) {
		[self.view setTouchBar:_touchBarMain];
	} else if (type == TouchBarType::AudioPlayer) {
		[self.view setTouchBar:_touchBarAudioPlayer];
	}
}

- (void)handlePropertyChange:(Media::Player::TrackState)property {
	// #TODO: fix hiding of touch bar when last track is ended.
	if (property.state == Media::Player::State::Stopped) {
		[self setTouchBar:TouchBarType::Main];
		return;
	} else if (property.state == Media::Player::State::StoppedAtEnd) {
		[self setTouchBar:TouchBarType::AudioPlayer];
	} else {
		[self setTouchBar:TouchBarType::AudioPlayer];
	}
	
	self.position = property.position < 0 ? 0 : property.position;
	self.duration = property.length;
	[self updateTouchBarTimeItems];
	NSButton *playButton = self.touchbarItems[play][@"view"];
	if (property.state == Media::Player::State::Playing) {
		playButton.image = self.touchbarItems[play][@"image"];
	} else {
		playButton.image = self.touchbarItems[play][@"imageAlt"];
	}
	
	[self.touchbarItems[nextItem][@"view"]
	 setEnabled:Media::Player::instance()->nextAvailable(kSongType)];
	[self.touchbarItems[previousItem][@"view"]
	 setEnabled:Media::Player::instance()->previousAvailable(kSongType)];
}

- (NSString *)formatTime:(int)time {
	const int seconds = time % 60;
	const int minutes = (time / 60) % 60;
	const int hours = time / (60 * 60);

	NSString *stime = hours > 0 ? [NSString stringWithFormat:@"%d:", hours] : @"";
	stime = (stime.length > 0 || minutes > 9) ?
		[NSString stringWithFormat:@"%@%02d:", stime, minutes] :
		[NSString stringWithFormat:@"%d:", minutes];
	stime = [NSString stringWithFormat:@"%@%02d", stime, seconds];

	return stime;
}

- (void)removeConstraintForIdentifier:(NSTouchBarItemIdentifier)identifier {
	NSTextField *field = self.touchbarItems[identifier][@"view"];
	[field removeConstraint:self.touchbarItems[identifier][@"constrain"]];
}

- (void)applyConstraintFromString:(NSString *)string
					forIdentifier:(NSTouchBarItemIdentifier)identifier {
	NSTextField *field = self.touchbarItems[identifier][@"view"];
	if (field) {
		NSString *fString = [[string componentsSeparatedByCharactersInSet:
			[NSCharacterSet decimalDigitCharacterSet]] componentsJoinedByString:@"0"];
		NSTextField *textField = [NSTextField labelWithString:fString];
		NSSize size = [textField frame].size;

		NSLayoutConstraint *con =
			[NSLayoutConstraint constraintWithItem:field
										 attribute:NSLayoutAttributeWidth
										 relatedBy:NSLayoutRelationEqual
											toItem:nil
										 attribute:NSLayoutAttributeNotAnAttribute
										multiplier:1.0
										  constant:(int)ceil(size.width * 1.5)];
		[field addConstraint:con];
		[self.touchbarItems[identifier] setObject:con forKey:@"constrain"];
	}
}

- (void)updateTouchBarTimeItemConstrains {
	[self removeConstraintForIdentifier:currentPosition];

	if (self.duration <= 0) {
		[self applyConstraintFromString:[self formatTime:self.position]
						  forIdentifier:currentPosition];
	} else {
		NSString *durFormat = [self formatTime:self.duration];
		[self applyConstraintFromString:durFormat forIdentifier:currentPosition];
	}
}

- (void)updateTouchBarTimeItems {
	NSSlider *seekSlider = self.touchbarItems[seekBar][@"view"];
	NSTextField *curPosItem = self.touchbarItems[currentPosition][@"view"];

	if (self.duration <= 0) {
		seekSlider.enabled = NO;
		seekSlider.doubleValue = 0;
	} else {
		seekSlider.enabled = YES;
		if (!seekSlider.highlighted) {
			seekSlider.doubleValue = (self.position / self.duration) * seekSlider.maxValue;
		}
	}
	const auto timeToString = [&](int t) {
		return [self formatTime:(int)floor(t / kMs)];
	};
	curPosItem.stringValue = [NSString stringWithFormat:@"%@ / %@",
							  timeToString(self.position),
							  timeToString(self.duration)];

	[self updateTouchBarTimeItemConstrains];
}

- (NSString *)getIdentifierFromView:(id)view {
	NSString *identifier;
	for (identifier in self.touchbarItems)
		if([self.touchbarItems[identifier][@"view"] isEqual:view])
			break;
	return identifier;
}

- (void)buttonAction:(NSButton *)sender {
	NSString *identifier = [self getIdentifierFromView:sender];
	const auto command = [self.touchbarItems[identifier][@"cmd"] intValue];

	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		if (command == kSavedMessages) {
			App::main()->choosePeer(Auth().userPeerId(), ShowAtUnreadMsgId);
		} else if (command == kPlayPause) {
			Media::Player::instance()->playPause();
		} else if (command == kPlaylistPrevious) {
			Media::Player::instance()->previous();
		} else if (command == kPlaylistNext) {
			Media::Player::instance()->next();
		} else if (command == kClosePlayer) {
			App::main()->closeBothPlayers();
		}
	});
}

- (void)seekbarChanged:(NSSliderTouchBarItem *)sender {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		Media::Player::instance()->finishSeeking(kSongType, sender.slider.doubleValue);
	});
}

@end
