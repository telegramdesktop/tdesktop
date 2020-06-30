/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef OS_OSX

#include "platform/platform_specific.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#import <Cocoa/Cocoa.h>

namespace Platform {

enum class TouchBarType {
	None,
	Main,
	AudioPlayer,
	AudioPlayerForce,
};

} // namespace Platform

@interface TouchBar : NSTouchBar

@property(retain) NSDictionary * _Nullable touchBarItems;

- (id _Nonnull) init:(NSView * _Nonnull)view
	session:(not_null<Main::Session*>)session;
- (void) handleTrackStateChange:(Media::Player::TrackState)state;
- (void) setTouchBar:(Platform::TouchBarType)type;
- (void) showInputFieldItem:(bool)show;

@end

#endif // OS_OSX
