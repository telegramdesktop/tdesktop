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
#include "auth_session.h"
#include "data/data_session.h"
#include "history/history.h"
#include "ui/empty_userpic.h"
#include "observer_peer.h"
#include "styles/style_dialogs.h"
#include "styles/style_media_player.h"
#include "data/data_folder.h"

namespace {
//https://developer.apple.com/design/human-interface-guidelines/macos/touch-bar/touch-bar-icons-and-images/
constexpr auto kIdealIconSize = 36;
constexpr auto kMaximumIconSize = 44;

constexpr auto kPlayPause = 0x002;
constexpr auto kPlaylistPrevious = 0x003;
constexpr auto kPlaylistNext = 0x004;
constexpr auto kClosePlayer = 0x005;
	
constexpr auto kMs = 1000;
	
constexpr auto kSongType = AudioMsgId::Type::Song;
	
constexpr auto kSavedMessagesId = 0;
constexpr auto kArchiveId = -1;
} // namespace

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

@interface PinnedDialogButton : NSCustomTouchBarItem {
}

@property(nonatomic, assign) int number;
@property(nonatomic, assign) bool waiting;
@property(nonatomic, assign) PeerData * peer;

- (id) init:(int)num;
- (id) initSavedMessages;
- (NSImage *) getPinImage;
- (void)buttonActionPin:(NSButton *)sender;
- (void)updatePeerData;
- (void)updatePinnedDialog;

@end // @interface PinnedDialogButton

@implementation PinnedDialogButton : NSCustomTouchBarItem

auto lifetime = rpl::lifetime();

- (id) init:(int)num {
	if (num == kSavedMessagesId) {
		return [self initSavedMessages];
	} else if (num == kArchiveId) {
		return [self initArchive];
	}
	NSString *identifier = [NSString stringWithFormat:@"%@.pinnedDialog%d", customIDMain, num];
	self = [super initWithIdentifier:identifier];
	if (!self) {
		return nil;
	}
	self.number = num;
	self.waiting = true;
	[self updatePeerData];
	
	NSButton *button = [NSButton buttonWithImage:[self getPinImage] target:self action:@selector(buttonActionPin:)];
	[button setBordered:NO];
	[button sizeToFit];
	[button setHidden:(num > Auth().data().pinnedChatsOrder(nullptr).size())];
	self.view = button;
	self.customizationLabel = [NSString stringWithFormat:@"Pinned Dialog %d", num];
	
	const auto updateImage = [self]() {
		NSButton *button = self.view;
		button.image = [self getPinImage];
	};
	
	if (self.peer) {
		Notify::PeerUpdateViewer(
			self.peer,
			Notify::PeerUpdate::Flag::PhotoChanged
		) | rpl::start_with_next([=] {
			self.waiting = true;
			updateImage();
		}, lifetime);
	}
	
	Auth().data().pinnedDialogsOrderUpdated(
	) | rpl::start_with_next([=] {
		[self updatePinnedDialog];
	}, lifetime);
	
	base::ObservableViewer(
	   Auth().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (self.waiting) {
			updateImage();
		}
	}, lifetime);
	
	return self;
}

- (void) updatePinnedDialog {
	[self updatePeerData];
	NSButton *button = self.view;
	button.image = [self getPinImage];
	[button setHidden:(self.number > Auth().data().pinnedChatsOrder(nullptr).size())];
}

- (id) initSavedMessages {
	self = [super initWithIdentifier:savedMessages];
	if (!self) {
		return nil;
	}
	self.number = kSavedMessagesId;
	self.waiting = false;
	
	NSButton *button = [NSButton buttonWithImage:[self getPinImage] target:self action:@selector(buttonActionPin:)];
	[button setBordered:NO];
	[button sizeToFit];
	self.view = button;
	self.customizationLabel = @"Saved Messages";

	return self;
}

- (id) initArchive {
	self = [super initWithIdentifier:archiveFolder];
	if (!self) {
		return nil;
	}
	self.number = kArchiveId;
	self.waiting = false;
	
	NSButton *button = [NSButton buttonWithImage:[self getPinImage] target:self action:@selector(buttonActionPin:)];
	[button setBordered:NO];
	[button sizeToFit];
	self.view = button;
	self.customizationLabel = @"Archive Folder";
	
	return self;
}

- (void) updatePeerData {
	const auto &order = Auth().data().pinnedChatsOrder(nullptr);
	if (self.number > order.size()) {
		self.peer = nil;
		return;
	}
	const auto pinned = order.at(self.number - 1);
	if (const auto history = pinned.history()) {
		self.peer = history->peer;
	}
}

- (void) buttonActionPin:(NSButton *)sender {
	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		App::main()->choosePeer(self.number == kSavedMessagesId || self.number == kArchiveId
			? Auth().userPeerId()
			: self.peer->id, ShowAtUnreadMsgId);
	});
}


- (NSImage *) getPinImage {
	if (self.number <= kSavedMessagesId) {
		const int s = kIdealIconSize * cRetinaFactor();
		auto *pix = new QPixmap(s, s);
		Painter paint(pix);
		paint.fillRect(QRectF(0, 0, s, s), QColor(0, 0, 0, 255));
		
		if (self.number == kArchiveId) {
			paint.fillRect(QRectF(0, 0, s, s), QColor(0, 0, 0, 255));
			if (const auto folder = Auth().data().folderLoaded(Data::Folder::kId)) {
				folder->paintUserpic(paint, 0, 0, s);
			}
		} else {
			Ui::EmptyUserpic::PaintSavedMessages(paint, 0, 0, s, s);
		}
		pix->setDevicePixelRatio(cRetinaFactor());
		return [qt_mac_create_nsimage(*pix) autorelease];
	}
	if (!self.peer) {
		// Random picture.
		return [NSImage imageNamed:NSImageNameTouchBarAddTemplate];
	}
	self.waiting = !self.peer->userpicLoaded();
	auto pixmap = self.peer->genUserpic(kIdealIconSize);
	pixmap.setDevicePixelRatio(cRetinaFactor());
	return [qt_mac_create_nsimage(pixmap) autorelease];
}


@end


@interface TouchBar()<NSTouchBarDelegate>
@end // @interface TouchBar

@interface TouchBar()<NSTouchBarDelegate>
@end

@implementation TouchBar

- (id) init:(NSView *)view {
	self = [super init];
	if (self) {
		self.view = view;
		self.touchbarItems = @{
			pinnedPanel: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"pinned",
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
				@"image": createImageFromStyleIcon(st::iconPlayerClose, NSMakeSize(kIdealIconSize / 3, kIdealIconSize / 3))
			}],
			currentPosition: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type": @"text",
				@"name": @"Current Position"
			}]
		};
	}
	[self createTouchBar];
	[self setTouchBar:TouchBarType::Main];
	
	Media::Player::instance()->playerWidgetToggled(
	) | rpl::start_with_next([=](bool toggled) {
		if (!toggled) {
			[self setTouchBar:TouchBarType::Main];
		} else {
			[self setTouchBar:TouchBarType::AudioPlayer];
		}
	}, lifetime);
	
	Core::App().passcodeLockChanges(
	) | rpl::start_with_next([=](bool locked) {
		if (locked) {
			self.touchBarTypeBeforeLock = self.touchBarType;
			[self setTouchBar:TouchBarType::None];
		} else {
			[self setTouchBar:self.touchBarTypeBeforeLock];
		}
	}, lifetime);
	
	return self;
}

NSImage *createImageFromStyleIcon(const style::icon &icon, NSSize size) {
	const auto pixmap = icon.instance(QColor(255, 255, 255, 255), 100);
	NSImage *image = [qt_mac_create_nsimage(QPixmap::fromImage(pixmap)) autorelease];
	[image setSize:size];
	return image;
}

- (void) createTouchBar {
	_touchBarMain = [[NSTouchBar alloc] init];
	_touchBarMain.delegate = self;
	
	_touchBarMain.customizationIdentifier = customIDMain;
	_touchBarMain.defaultItemIdentifiers = @[pinnedPanel];
	
	_touchBarAudioPlayer = [[NSTouchBar alloc] init];
	_touchBarAudioPlayer.delegate = self;

	_touchBarAudioPlayer.customizationIdentifier = customID;
	_touchBarAudioPlayer.defaultItemIdentifiers = @[play, previousItem, nextItem, seekBar, closePlayer];
	_touchBarAudioPlayer.customizationAllowedItemIdentifiers = @[play, previousItem,
																nextItem, currentPosition, seekBar, closePlayer];
}

- (nullable NSTouchBarItem *) touchBar:(NSTouchBar *)touchBar
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
		NSTextField *text = [NSTextField labelWithString:@"00:00 / 00:00"];
		text.alignment = NSTextAlignmentCenter;
		item.view = text;
		item.customizationLabel = self.touchbarItems[identifier][@"name"];
		[self.touchbarItems[identifier] setObject:text forKey:@"view"];
		return item;
	} else if ([self.touchbarItems[identifier][@"type"] isEqualToString:@"pinned"]) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSMutableArray *pins = [[NSMutableArray alloc] init];
		
		for (auto i = kArchiveId; i <= Global::PinnedDialogsCountMax(); i++) {
			[pins addObject:[[PinnedDialogButton alloc] init:i].view];
		}
		NSStackView *stackView = [NSStackView stackViewWithViews:[pins copy]];
		[stackView setSpacing:-15];
		item.view = stackView;
		[self.touchbarItems[identifier] setObject:item.view forKey:@"view"];
		return item;
	}

	return nil;
}

- (void) setTouchBar:(TouchBarType)type {
	if (self.touchBarType == type) {
		return;
	}
	if (type == TouchBarType::Main) {
		[self.view setTouchBar:_touchBarMain];
	} else if (type == TouchBarType::AudioPlayer) {
		if (!isCurrentSongExists()
			|| Media::Player::instance()->getActiveType() != kSongType) {
			return;
		}
		[self.view setTouchBar:_touchBarAudioPlayer];
	} else if (type == TouchBarType::AudioPlayerForce) {
		[self.view setTouchBar:_touchBarAudioPlayer];
		self.touchBarType = TouchBarType::AudioPlayer;
		return;
	} else if (type == TouchBarType::None) {
		[self.view setTouchBar:nil];
	}
	self.touchBarType = type;
}

inline bool isCurrentSongExists() {
	return Media::Player::instance()->current(kSongType).audio() != nullptr;
}

- (void) handlePropertyChange:(Media::Player::TrackState)property {
	if (property.id.type() == kSongType) {
		[self setTouchBar:TouchBarType::AudioPlayerForce];
	} else {
		return;
	}
	self.position = property.position < 0 ? 0 : property.position;
	self.duration = property.length;
	if (Media::Player::IsStoppedOrStopping(property.state)) {
		self.position = 0;
		self.duration = 0;
	}
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

- (NSString *) formatTime:(int)time {
	const int seconds = time % 60;
	const int minutes = (time / 60) % 60;
	const int hours = time / (60 * 60);

	NSString *stime = hours > 0 ? [NSString stringWithFormat:@"%d:", hours] : @"";
	stime = (stime.length > 0 || minutes > 9) ?
		[NSString stringWithFormat:@"%@%02d:", stime, minutes] :
		[NSString stringWithFormat:@"%02d:", minutes];
	stime = [NSString stringWithFormat:@"%@%02d", stime, seconds];

	return stime;
}

- (void) removeConstraintForIdentifier:(NSTouchBarItemIdentifier)identifier {
	NSTextField *field = self.touchbarItems[identifier][@"view"];
	[field removeConstraint:self.touchbarItems[identifier][@"constrain"]];
}

- (void) applyConstraintFromString:(NSString *)string
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
										  constant:(int)ceil(size.width) * 1.2];
		[field addConstraint:con];
		[self.touchbarItems[identifier] setObject:con forKey:@"constrain"];
	}
}

- (void) updateTouchBarTimeItems {
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

	[self removeConstraintForIdentifier:currentPosition];
	[self applyConstraintFromString:curPosItem.stringValue forIdentifier:currentPosition];
}

- (NSString *) getIdentifierFromView:(id)view {
	NSString *identifier;
	for (identifier in self.touchbarItems)
		if([self.touchbarItems[identifier][@"view"] isEqual:view])
			break;
	return identifier;
}

- (void) buttonAction:(NSButton *)sender {
	NSString *identifier = [self getIdentifierFromView:sender];
	const auto command = [self.touchbarItems[identifier][@"cmd"] intValue];

	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		if (command == kPlayPause) {
			Media::Player::instance()->playPause(kSongType);
		} else if (command == kPlaylistPrevious) {
			Media::Player::instance()->previous(kSongType);
		} else if (command == kPlaylistNext) {
			Media::Player::instance()->next(kSongType);
		} else if (command == kClosePlayer) {
			App::main()->closeBothPlayers();
		}
	});
}

- (void) seekbarChanged:(NSSliderTouchBarItem *)sender {
	// https://stackoverflow.com/a/45891017
	NSEvent *event = [[NSApplication sharedApplication] currentEvent];
	bool touchUp = [event touchesMatchingPhase:NSTouchPhaseEnded inView:nil].count > 0;
	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		if (touchUp) {
			Media::Player::instance()->finishSeeking(kSongType, sender.slider.doubleValue);
		} else {
			Media::Player::instance()->startSeeking(kSongType);
		}
	});
}

@end
