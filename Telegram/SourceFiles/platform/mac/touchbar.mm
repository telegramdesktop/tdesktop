/*
 This file is part of Telegram Desktop,
 the official desktop application for the Telegram messaging service.
 
 For license and copyright information please follow this link:
 https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
 */

#import "touchbar.h"
#import <QuartzCore/QuartzCore.h>

#include "auth_session.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "history/history.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "observer_peer.h"
#include "styles/style_media_player.h"
#include "window/window_controller.h"
#include "ui/empty_userpic.h"

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

namespace {
//https://developer.apple.com/design/human-interface-guidelines/macos/touch-bar/touch-bar-icons-and-images/
constexpr auto kIdealIconSize = 36;
constexpr auto kMaximumIconSize = 44;

constexpr auto kCommandPlayPause = 0x002;
constexpr auto kCommandPlaylistPrevious = 0x003;
constexpr auto kCommandPlaylistNext = 0x004;
constexpr auto kCommandClosePlayer = 0x005;

constexpr auto kMs = 1000;

constexpr auto kSongType = AudioMsgId::Type::Song;

constexpr auto kSavedMessagesId = 0;
constexpr auto kArchiveId = -1;

const NSString *kCustomizationIdPlayer = @"telegram.touchbar";
const NSString *kCustomizationIdMain = @"telegram.touchbarMain";
const NSTouchBarItemIdentifier kSavedMessagesItemIdentifier = [NSString stringWithFormat:@"%@.savedMessages", kCustomizationIdMain];
const NSTouchBarItemIdentifier kArchiveFolderItemIdentifier = [NSString stringWithFormat:@"%@.archiveFolder", kCustomizationIdMain];
const NSTouchBarItemIdentifier kPinnedPanelItemIdentifier = [NSString stringWithFormat:@"%@.pinnedPanel", kCustomizationIdMain];

const NSTouchBarItemIdentifier kSeekBarItemIdentifier = [NSString stringWithFormat:@"%@.seekbar", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kPlayItemIdentifier = [NSString stringWithFormat:@"%@.play", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kNextItemIdentifier = [NSString stringWithFormat:@"%@.nextItem", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kPreviousItemIdentifier = [NSString stringWithFormat:@"%@.previousItem", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kCommandClosePlayerItemIdentifier = [NSString stringWithFormat:@"%@.closePlayer", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kCurrentPositionItemIdentifier = [NSString stringWithFormat:@"%@.currentPosition", kCustomizationIdPlayer];

NSImage *CreateNSImageFromStyleIcon(const style::icon &icon, int size = kIdealIconSize) {
	const auto instance = icon.instance(QColor(255, 255, 255, 255), 100);
	auto pixmap = QPixmap::fromImage(instance);
	pixmap.setDevicePixelRatio(cRetinaFactor());
	NSImage *image = [qt_mac_create_nsimage(pixmap) autorelease];
	[image setSize:NSMakeSize(size, size)];
	return image;
}

inline bool IsCurrentSongExists() {
	return Media::Player::instance()->current(kSongType).audio() != nullptr;
}

NSString* FormatTime(int time) {
	const auto seconds = time % 60;
	const auto minutes = (time / 60) % 60;
	const auto hours = time / (60 * 60);

	NSString *stringTime = (hours > 0)
		? [NSString stringWithFormat:@"%d:", hours]
		: @"";
	stringTime = [NSString stringWithFormat:@"%@%02d:",
		(stringTime.length > 0 || minutes > 9)
			? stringTime
			: @"",
		minutes];
	stringTime = [NSString stringWithFormat:@"%@%02d", stringTime, seconds];

	return stringTime;
}

} // namespace

@interface PinnedDialogButton : NSCustomTouchBarItem {
	rpl::lifetime lifetime;
	rpl::lifetime userpicChangedLifetime;
}

@property(nonatomic, assign) int number;
@property(nonatomic, assign) bool waiting;
@property(nonatomic, assign) PeerData * peer;
@property(nonatomic, assign) bool isDeletedFromView;

- (id) init:(int)num;
- (NSImage *) getPinImage;
- (void)buttonActionPin:(NSButton *)sender;
- (void)updatePinnedDialog;

@end // @interface PinnedDialogButton

@implementation PinnedDialogButton : NSCustomTouchBarItem

- (id) init:(int)num {
	if (num == kSavedMessagesId) {
		self = [super initWithIdentifier:kSavedMessagesItemIdentifier];
		self.waiting = false;
		self.customizationLabel = [NSString stringWithFormat:@"Pinned Dialog %d", num];
	} else if (num == kArchiveId) {
		self = [super initWithIdentifier:kArchiveFolderItemIdentifier];
		self.waiting = false;
		self.customizationLabel = @"Archive Folder";
	} else {
		NSString *identifier = [NSString stringWithFormat:@"%@.pinnedDialog%d", kCustomizationIdMain, num];
		self = [super initWithIdentifier:identifier];
		self.waiting = true;
		self.customizationLabel = @"Saved Messages";
	}
	if (!self) {
		return nil;
	}
	self.number = num;
	
	NSButton *button = [NSButton buttonWithImage:[self getPinImage] target:self action:@selector(buttonActionPin:)];
	[button setBordered:NO];
	[button sizeToFit];
	self.view = button;

	if (num <= kSavedMessagesId) {
		return self;
	}
	
	base::ObservableViewer(
		Auth().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (self.waiting) {
			[self updatePinnedDialog];
		}
	}, self->lifetime);
	
	return self;
}

// Setter of peer.
- (void) setPeer:(PeerData *)newPeer {
	if (_peer == newPeer) {
		return;
	}
	_peer = newPeer;
	self->userpicChangedLifetime.destroy();
	if (!_peer) {
		return;
	}
	Notify::PeerUpdateViewer(
		_peer,
	Notify::PeerUpdate::Flag::PhotoChanged
	) | rpl::start_with_next([=] {
		self.waiting = true;
		[self updatePinnedDialog];
	}, self->userpicChangedLifetime);
}

- (void) updatePinnedDialog {
	NSButton *button = self.view;
	button.image = [self getPinImage];
}

- (void) buttonActionPin:(NSButton *)sender {
	const auto openFolder = [=] {
		if (!App::wnd()) {
			return;
		}
		if (const auto folder = Auth().data().folderLoaded(Data::Folder::kId)) {
			App::wnd()->controller()->openFolder(folder);
		}
	};
	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		self.number == kArchiveId
			? openFolder()
			: App::main()->choosePeer(self.number == kSavedMessagesId
				? Auth().userPeerId()
				: self.peer->id, ShowAtUnreadMsgId);
	});
}

- (bool) isSelfPeer {
	return !self.peer ? false : self.peer->id == Auth().userPeerId();
}

- (NSImage *) getPinImage {
	// Don't draw self userpic if we pin Saved Messages.
	if (self.number <= kSavedMessagesId || [self isSelfPeer]) {
		const int s = kIdealIconSize * cRetinaFactor();
		auto *pix = new QPixmap(s, s);
		Painter paint(pix);
		paint.fillRect(QRectF(0, 0, s, s), QColor(0, 0, 0, 255));
		
		if (self.number == kArchiveId) {
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

@implementation TouchBar

- (id) init:(NSView *)view {
	self = [super init];
	if (self) {
		const auto iconSize = kIdealIconSize / 3;
		self.view = view;
		self.touchbarItems = @{
			kPinnedPanelItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"pinned",
			}],
			kSeekBarItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type": @"slider",
				@"name": @"Seek Bar"
			}],
			kPlayItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":     @"button",
				@"name":     @"Play Button",
				@"cmd":      [NSNumber numberWithInt:kCommandPlayPause],
				@"image":    CreateNSImageFromStyleIcon(st::touchBarIconPlayerPause, iconSize),
				@"imageAlt": CreateNSImageFromStyleIcon(st::touchBarIconPlayerPlay, iconSize),
			}],
			kPreviousItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"button",
				@"name":  @"Previous Playlist Item",
				@"cmd":   [NSNumber numberWithInt:kCommandPlaylistPrevious],
				@"image": CreateNSImageFromStyleIcon(st::touchBarIconPlayerPrevious, iconSize),
			}],
			kNextItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"button",
				@"name":  @"Next Playlist Item",
				@"cmd":   [NSNumber numberWithInt:kCommandPlaylistNext],
				@"image": CreateNSImageFromStyleIcon(st::touchBarIconPlayerNext, iconSize),
			}],
			kCommandClosePlayerItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
				@"type":  @"button",
				@"name":  @"Close Player",
				@"cmd":   [NSNumber numberWithInt:kCommandClosePlayer],
				@"image": CreateNSImageFromStyleIcon(st::touchBarIconPlayerClose, iconSize),
			}],
			kCurrentPositionItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
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
	}, self->lifetime);
	
	Core::App().passcodeLockChanges(
	) | rpl::start_with_next([=](bool locked) {
		if (locked) {
			self.touchBarTypeBeforeLock = self.touchBarType;
			[self setTouchBar:TouchBarType::None];
		} else {
			[self setTouchBar:self.touchBarTypeBeforeLock];
		}
	}, self->lifetime);
	
	Auth().data().pinnedDialogsOrderUpdated(
	) | rpl::start_with_next([self] {
		[self updatePinnedButtons];
	}, self->lifetime);
	
	Auth().data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return folder && folder->chatsList();
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		[self toggleArchiveButton:folder->chatsList()->empty()];
	}, self->lifetime);
	
	[self updatePinnedButtons];
	
	return self;
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
		button.tag = [self.touchbarItems[identifier][@"cmd"] intValue];
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
		self.mainPinnedButtons = [[NSMutableArray alloc] init];
		NSStackView *stackView = [[NSStackView alloc] init];
		
		for (auto i = kArchiveId; i <= Global::PinnedDialogsCountMax(); i++) {
			PinnedDialogButton *button = [[PinnedDialogButton alloc] init:i];
			[self.mainPinnedButtons addObject:button];
			if (i == kArchiveId) {
				button.isDeletedFromView = true;
				continue;
			}
			[stackView addView:button.view inGravity:NSStackViewGravityCenter];
		}
		
		[stackView setSpacing:-15];
		item.view = stackView;
		[self.touchbarItems[identifier] setObject:item.view forKey:@"view"];
		return item;
	}

	return nil;
}

- (void) createTouchBar {
	_touchBarMain = [[NSTouchBar alloc] init];
	_touchBarMain.delegate = self;
	_touchBarMain.defaultItemIdentifiers = @[kPinnedPanelItemIdentifier];
	
	_touchBarAudioPlayer = [[NSTouchBar alloc] init];
	_touchBarAudioPlayer.delegate = self;
	_touchBarAudioPlayer.customizationIdentifier = kCustomizationIdPlayer.lowercaseString;
	_touchBarAudioPlayer.defaultItemIdentifiers = @[
		kPlayItemIdentifier,
		kPreviousItemIdentifier,
		kNextItemIdentifier,
		kSeekBarItemIdentifier,
		kCommandClosePlayerItemIdentifier];
	_touchBarAudioPlayer.customizationAllowedItemIdentifiers = @[
		kPlayItemIdentifier,
		kPreviousItemIdentifier,
		kNextItemIdentifier,
		kCurrentPositionItemIdentifier,
		kSeekBarItemIdentifier,
		kCommandClosePlayerItemIdentifier];
}

- (void) setTouchBar:(TouchBarType)type {
	if (self.touchBarType == type) {
		return;
	}
	if (type == TouchBarType::Main) {
		[self.view setTouchBar:_touchBarMain];
	} else if (type == TouchBarType::AudioPlayer) {
		if (!IsCurrentSongExists()
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

// Main Touchbar.

- (void) toggleArchiveButton:(bool)hide {
	for (PinnedDialogButton *button in self.mainPinnedButtons) {
		if (button.number == kArchiveId) {
			NSCustomTouchBarItem *item = [self.touchBarMain itemForIdentifier:kPinnedPanelItemIdentifier];
			NSStackView *stack = item.view;
			[button updatePinnedDialog];
			if (hide && !button.isDeletedFromView) {
				button.isDeletedFromView = true;
				[stack removeView:button.view];
			}
			if (!hide && button.isDeletedFromView) {
				button.isDeletedFromView = false;
				[stack insertView:button.view
						  atIndex:(button.number + 1)
						inGravity:NSStackViewGravityLeading];
			}
		}
	}
}

- (void) updatePinnedButtons {
	const auto &order = Auth().data().pinnedChatsOrder(nullptr);
	auto isSelfPeerPinned = false;
	PinnedDialogButton *selfChatButton;
	NSCustomTouchBarItem *item = [self.touchBarMain itemForIdentifier:kPinnedPanelItemIdentifier];
	NSStackView *stack = item.view;
	
	for (PinnedDialogButton *button in self.mainPinnedButtons) {
		const auto num = button.number;
		if (num <= kSavedMessagesId) {
			if (num == kSavedMessagesId) {
				selfChatButton = button;
			}
			continue;
		}
		const auto numIsTooLarge = num > order.size();
		[button.view setHidden:numIsTooLarge];
		if (numIsTooLarge) {
			button.peer = nil;
			continue;
		}
		const auto pinned = order.at(num - 1);
		if (const auto history = pinned.history()) {
			button.peer = history->peer;
			[button updatePinnedDialog];
			if (history->peer->id == Auth().userPeerId()) {
				isSelfPeerPinned = true;
			}
		}
	}

	// If self chat is pinned, delete from view saved messages button.
	if (isSelfPeerPinned && !selfChatButton.isDeletedFromView) {
		selfChatButton.isDeletedFromView = true;
		[stack removeView:selfChatButton.view];
	} else if (!isSelfPeerPinned && selfChatButton.isDeletedFromView) {
		selfChatButton.isDeletedFromView = false;
		[stack insertView:selfChatButton.view atIndex:0 inGravity:NSStackViewGravityLeading];
	}
}

// Audio Player Touchbar.

- (void) handleTrackStateChange:(Media::Player::TrackState)property {
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
	[self updateTouchBarTimeItem];

	NSButton *playButton = self.touchbarItems[kPlayItemIdentifier][@"view"];
	const auto imgButton = (property.state == Media::Player::State::Playing)
		? @"image"
		: @"imageAlt";
	playButton.image = self.touchbarItems[kPlayItemIdentifier][imgButton];
	
	[self.touchbarItems[kNextItemIdentifier][@"view"]
	 setEnabled:Media::Player::instance()->nextAvailable(kSongType)];
	[self.touchbarItems[kPreviousItemIdentifier][@"view"]
	 setEnabled:Media::Player::instance()->previousAvailable(kSongType)];
}

- (void) updateTouchBarTimeItem {
	NSSlider *seekSlider = self.touchbarItems[kSeekBarItemIdentifier][@"view"];
	NSTextField *curPosItem = self.touchbarItems[kCurrentPositionItemIdentifier][@"view"];

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
		return FormatTime((int)floor(t / kMs));
	};
	curPosItem.stringValue = [NSString stringWithFormat:@"%@ / %@",
								timeToString(self.position),
								timeToString(self.duration)];

	NSTextField *field = self.touchbarItems[kCurrentPositionItemIdentifier][@"view"];

	if (!field) {
		return;
	}

	[field removeConstraint:self.touchbarItems[kCurrentPositionItemIdentifier][@"constrain"]];

	NSString *fString = [[curPosItem.stringValue componentsSeparatedByCharactersInSet:
		[NSCharacterSet decimalDigitCharacterSet]] componentsJoinedByString:@"0"];
	NSTextField *tempField = [NSTextField labelWithString:fString];
	NSSize size = [tempField frame].size;

	NSLayoutConstraint *con =
		[NSLayoutConstraint constraintWithItem:field
									 attribute:NSLayoutAttributeWidth
									 relatedBy:NSLayoutRelationEqual
										toItem:nil
									 attribute:NSLayoutAttributeNotAnAttribute
									multiplier:1.0
									  constant:(int)ceil(size.width) * 1.2];
	[field addConstraint:con];
	[self.touchbarItems[kCurrentPositionItemIdentifier] setObject:con forKey:@"constrain"];
}

- (void) buttonAction:(NSButton *)sender {
	const auto command = sender.tag;

	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		if (command == kCommandPlayPause) {
			Media::Player::instance()->playPause(kSongType);
		} else if (command == kCommandPlaylistPrevious) {
			Media::Player::instance()->previous(kSongType);
		} else if (command == kCommandPlaylistNext) {
			Media::Player::instance()->next(kSongType);
		} else if (command == kCommandClosePlayer) {
			App::main()->closeBothPlayers();
		}
	});
}

- (void) seekbarChanged:(NSSliderTouchBarItem *)sender {
	// https://stackoverflow.com/a/45891017
	NSEvent *event = [[NSApplication sharedApplication] currentEvent];
	const auto touchUp = [event touchesMatchingPhase:NSTouchPhaseEnded inView:nil].count > 0;
	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		if (touchUp) {
			Media::Player::instance()->finishSeeking(kSongType, sender.slider.doubleValue);
		} else {
			Media::Player::instance()->startSeeking(kSongType);
		}
	});
}

@end
