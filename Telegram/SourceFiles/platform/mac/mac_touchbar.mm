/*
 This file is part of Telegram Desktop,
 the official desktop application for the Telegram messaging service.

 For license and copyright information please follow this link:
 https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
 */

#import "mac_touchbar.h"
#import <QuartzCore/QuartzCore.h>

#include "auth_session.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "dialogs/dialogs_layout.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "observer_peer.h"
#include "styles/style_media_player.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "ui/empty_userpic.h"
#include "styles/style_dialogs.h"

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

namespace {
//https://developer.apple.com/design/human-interface-guidelines/macos/touch-bar/touch-bar-icons-and-images/
constexpr auto kIdealIconSize = 36;
constexpr auto kMaximumIconSize = 44;

constexpr auto kCommandPlayPause = 0x002;
constexpr auto kCommandPlaylistPrevious = 0x003;
constexpr auto kCommandPlaylistNext = 0x004;
constexpr auto kCommandClosePlayer = 0x005;

constexpr auto kCommandBold = 0x010;
constexpr auto kCommandItalic = 0x011;
constexpr auto kCommandMonospace = 0x012;
constexpr auto kCommandClear = 0x013;
constexpr auto kCommandLink = 0x014;

constexpr auto kCommandPopoverInput = 0x020;

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

const NSTouchBarItemIdentifier kPopoverInputItemIdentifier = [NSString stringWithFormat:@"%@.popoverInput", kCustomizationIdMain];
const NSTouchBarItemIdentifier kBoldItemIdentifier = [NSString stringWithFormat:@"%@.bold", kCustomizationIdMain];
const NSTouchBarItemIdentifier kItalicItemIdentifier = [NSString stringWithFormat:@"%@.italic", kCustomizationIdMain];
const NSTouchBarItemIdentifier kMonospaceItemIdentifier = [NSString stringWithFormat:@"%@.monospace", kCustomizationIdMain];
const NSTouchBarItemIdentifier kClearItemIdentifier = [NSString stringWithFormat:@"%@.clear", kCustomizationIdMain];
const NSTouchBarItemIdentifier kLinkItemIdentifier = [NSString stringWithFormat:@"%@.link", kCustomizationIdMain];

NSImage *CreateNSImageFromStyleIcon(const style::icon &icon, int size = kIdealIconSize) {
	const auto instance = icon.instance(QColor(255, 255, 255, 255), 100);
	auto pixmap = QPixmap::fromImage(instance);
	pixmap.setDevicePixelRatio(cRetinaFactor());
	NSImage *image = [qt_mac_create_nsimage(pixmap) autorelease];
	[image setSize:NSMakeSize(size, size)];
	return image;
}

NSString *NSStringFromLang(LangKey key) {
	return [NSString stringWithUTF8String:lang(key).toUtf8().constData()];
}

inline bool CurrentSongExists() {
	return Media::Player::instance()->current(kSongType).audio() != nullptr;
}

inline bool UseEmptyUserpic(PeerData *peer) {
	return (peer && (peer->useEmptyUserpic() || peer->isSelf()));
}

inline bool IsSelfPeer(PeerData *peer) {
	return (peer && peer->isSelf());
}

inline int UnreadCount(PeerData *peer) {
	return (peer && peer->owner().history(peer->id)->unreadCountForBadge());
}

NSString *FormatTime(int time) {
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

bool PaintUnreadBadge(Painter &p, PeerData *peer) {
	const auto history = peer->owner().history(peer->id);
	const auto count = history->unreadCountForBadge();
	if (!count) {
		return false;
	}
	const auto unread = history->unreadMark()
		? QString()
		: QString::number(count);
	Dialogs::Layout::UnreadBadgeStyle unreadSt;
	unreadSt.sizeId = Dialogs::Layout::UnreadBadgeInTouchBar;
	unreadSt.muted = history->mute();
	// Use constant values to draw badge regardless of cConfigScale().
	unreadSt.size = 19;
	unreadSt.padding = 5;
	unreadSt.font = style::font(
		12,
		unreadSt.font->flags(),
		unreadSt.font->family());
	Dialogs::Layout::paintUnreadCount(p, unread, kIdealIconSize, kIdealIconSize - unreadSt.size, unreadSt, nullptr, 2);
	return true;
}

void PaintOnlineCircle(Painter &p) {
	PainterHighQualityEnabler hq(p);
	// Use constant values to draw online badge regardless of cConfigScale().
	const auto size = 8;
	const auto paddingSize = 4;
	const auto circleSize = size + paddingSize;
	const auto offset = size + paddingSize / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::black);
	p.drawEllipse(
		kIdealIconSize - circleSize,
		kIdealIconSize - circleSize,
		circleSize,
		circleSize);
	p.setBrush(st::dialogsOnlineBadgeFg);
	p.drawEllipse(
		kIdealIconSize - offset,
		kIdealIconSize - offset,
		size,
		size);
}

void SendKeyEvent(int command) {
	QWidget *focused = QApplication::focusWidget();
	if (!qobject_cast<QTextEdit*>(focused)) {
		return;
	}
	auto key = 0;
	auto modifier = Qt::KeyboardModifiers(0) | Qt::ControlModifier;
	switch (command) {
	case kCommandBold:
		key = Qt::Key_B;
		break;
	case kCommandItalic:
		key = Qt::Key_I;
		break;
	case kCommandMonospace:
		key = Qt::Key_M;
		modifier |= Qt::ShiftModifier;
		break;
	case kCommandClear:
		key = Qt::Key_N;
		modifier |= Qt::ShiftModifier;
		break;
	case kCommandLink:
		key = Qt::Key_K;
		break;
	}
	QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, key, modifier));
	QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, key, modifier));
}

} // namespace

@interface PinnedDialogButton : NSCustomTouchBarItem

@property(nonatomic, assign) int number;
@property(nonatomic, assign) PeerData *peer;
@property(nonatomic, assign) bool isDeletedFromView;
@property(nonatomic, assign) QPixmap userpic;

- (id) init:(int)num;
- (void)buttonActionPin:(NSButton *)sender;
- (void)updateUserpic;

@end // @interface PinnedDialogButton

@implementation PinnedDialogButton {
	rpl::lifetime _lifetime;
	rpl::lifetime _peerChangedLifetime;
	bool isWaitingUserpicLoad;
}

- (id) init:(int)num {
	if (num == kSavedMessagesId) {
		self = [super initWithIdentifier:kSavedMessagesItemIdentifier];
		isWaitingUserpicLoad = false;
		self.customizationLabel = [NSString stringWithFormat:@"Pinned Dialog %d", num];
	} else if (num == kArchiveId) {
		self = [super initWithIdentifier:kArchiveFolderItemIdentifier];
		isWaitingUserpicLoad = false;
		self.customizationLabel = @"Archive Folder";
	} else {
		NSString *identifier = [NSString stringWithFormat:@"%@.pinnedDialog%d", kCustomizationIdMain, num];
		self = [super initWithIdentifier:identifier];
		isWaitingUserpicLoad = true;
		self.customizationLabel = @"Saved Messages";
	}
	if (!self) {
		return nil;
	}
	self.number = num;

	NSButton *button = [NSButton buttonWithImage:[NSImage imageNamed:NSImageNameStopProgressTemplate] target:self action:@selector(buttonActionPin:)];
	[button setBordered:NO];
	[button sizeToFit];
	self.view = button;

	using Update = const Window::Theme::BackgroundUpdate;
	auto themeChanged = base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::start_spawning(_lifetime);

	rpl::duplicate(
		themeChanged
	) | rpl::filter([=](const Update &update) {
		return update.paletteChanged()
			&& (_number <= kSavedMessagesId || UseEmptyUserpic(_peer));
	}) | rpl::start_with_next([=] {
		[self updateUserpic];
	}, _lifetime);

	std::move(
		themeChanged
	) | rpl::filter([=](const Update &update) {
		return update.type == Update::Type::ApplyingTheme
			&& (UnreadCount(_peer) || Data::IsPeerAnOnlineUser(_peer));
	}) | rpl::start_with_next([=] {
		[self updateBadge];
	}, _lifetime);

	if (num <= kSavedMessagesId) {
		[self updateUserpic];
		return self;
	}

	base::ObservableViewer(
		Auth().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (isWaitingUserpicLoad) {
			[self updateUserpic];
		}
	}, _lifetime);

	return self;
}

// Setter of peer.
- (void) setPeer:(PeerData *)newPeer {
	if (_peer == newPeer) {
		return;
	}
	_peer = newPeer;
	_peerChangedLifetime.destroy();
	if (!_peer) {
		return;
	}
	Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::PhotoChanged
	) | rpl::start_with_next([=] {
		isWaitingUserpicLoad = true;
		[self updateUserpic];
	}, _peerChangedLifetime);

	Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::UnreadViewChanged
	) | rpl::start_with_next([=] {
		[self updateBadge];
	}, _peerChangedLifetime);

	Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::UserOnlineChanged
	) | rpl::filter([=] {
		return UnreadCount(_peer) == 0;
	}) | rpl::start_with_next([=] {
		[self updateBadge];
	}, _peerChangedLifetime);
}

- (void) buttonActionPin:(NSButton *)sender {
	const auto openFolder = [=] {
		if (!App::wnd()) {
			return;
		}
		if (const auto folder = Auth().data().folderLoaded(Data::Folder::kId)) {
			App::wnd()->sessionController()->openFolder(folder);
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

- (void) updateUserpic {
	// Don't draw self userpic if we pin Saved Messages.
	if (self.number <= kSavedMessagesId || IsSelfPeer(_peer)) {
		const auto s = kIdealIconSize * cIntRetinaFactor();
		_userpic = QPixmap(s, s);
		Painter paint(&_userpic);
		paint.fillRect(QRectF(0, 0, s, s), QColor(0, 0, 0, 255));

		if (self.number == kArchiveId) {
			if (const auto folder = Auth().data().folderLoaded(Data::Folder::kId)) {
				folder->paintUserpic(paint, 0, 0, s);
			}
		} else {
			Ui::EmptyUserpic::PaintSavedMessages(paint, 0, 0, s, s);
		}
		_userpic.setDevicePixelRatio(cRetinaFactor());
		[self updateImage:_userpic];
		return;
	}
	if (!self.peer) {
		return;
	}
	isWaitingUserpicLoad = !self.peer->userpicLoaded();
	auto pixmap = self.peer->genUserpic(kIdealIconSize);
	pixmap.setDevicePixelRatio(cRetinaFactor());
	_userpic = pixmap;
	[self updateBadge];
}

- (void) updateBadge {
	// Draw unread or online badge.
	auto pixmap = App::pixmapFromImageInPlace(_userpic.toImage());
	Painter p(&pixmap);
	if (!PaintUnreadBadge(p, _peer) && Data::IsPeerAnOnlineUser(_peer)) {
		PaintOnlineCircle(p);
	}
	[self updateImage:pixmap];
}

- (void) updateImage:(QPixmap)pixmap {
	NSButton *button = self.view;
	button.image = [qt_mac_create_nsimage(pixmap) autorelease];
}

@end


@interface TouchBar()<NSTouchBarDelegate>
@end // @interface TouchBar

@implementation TouchBar {
	NSView *_parentView;
	NSMutableArray *_mainPinnedButtons;

	NSTouchBar *_touchBarMain;
	NSTouchBar *_touchBarAudioPlayer;

	Platform::TouchBarType _touchBarType;
	Platform::TouchBarType _touchBarTypeBeforeLock;

	double _duration;
	double _position;

	rpl::lifetime _lifetime;
}

- (id) init:(NSView *)view {
	self = [super init];
	if (!self) {
		return nil;
	}

	const auto iconSize = kIdealIconSize / 3;
	_position = 0;
	_duration = 0;
	_parentView = view;
	self.touchBarItems = @{
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
		}],
		kBoldItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  @"textButton",
			@"name":  NSStringFromLang(lng_menu_formatting_bold),
			@"cmd":   [NSNumber numberWithInt:kCommandBold],
		}],
		kItalicItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  @"textButton",
			@"name":  NSStringFromLang(lng_menu_formatting_italic),
			@"cmd":   [NSNumber numberWithInt:kCommandItalic],
		}],
		kMonospaceItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  @"textButton",
			@"name":  NSStringFromLang(lng_menu_formatting_monospace),
			@"cmd":   [NSNumber numberWithInt:kCommandMonospace],
		}],
		kClearItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  @"textButton",
			@"name":  NSStringFromLang(lng_menu_formatting_clear),
			@"cmd":   [NSNumber numberWithInt:kCommandClear],
		}],
		kLinkItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  @"textButton",
			@"name":  NSStringFromLang(lng_info_link_label),
			@"cmd":   [NSNumber numberWithInt:kCommandLink],
		}],
		kPopoverInputItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  @"popover",
			@"name":  @"Input Field",
			@"cmd":   [NSNumber numberWithInt:kCommandPopoverInput],
			@"image": [NSImage imageNamed:NSImageNameTouchBarTextItalicTemplate],
		}]
	};

	[self createTouchBar];
	[self setTouchBar:Platform::TouchBarType::Main];

	Media::Player::instance()->playerWidgetToggled(
	) | rpl::start_with_next([=](bool toggled) {
		if (!toggled) {
			[self setTouchBar:Platform::TouchBarType::Main];
		} else {
			[self setTouchBar:Platform::TouchBarType::AudioPlayer];
		}
	}, _lifetime);

	Media::Player::instance()->updatedNotifier(
	) | rpl::start_with_next([=](const Media::Player::TrackState &state) {
		[self handleTrackStateChange:state];
	}, _lifetime);

	Core::App().passcodeLockChanges(
	) | rpl::start_with_next([=](bool locked) {
		if (locked) {
			_touchBarTypeBeforeLock = _touchBarType;
			[self setTouchBar:Platform::TouchBarType::None];
		} else {
			[self setTouchBar:_touchBarTypeBeforeLock];
		}
	}, _lifetime);

	Auth().data().pinnedDialogsOrderUpdated(
	) | rpl::start_with_next([self] {
		[self updatePinnedButtons];
	}, _lifetime);

	Auth().data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return folder
			&& folder->chatsList()
			&& folder->id() == Data::Folder::kId;
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		[self toggleArchiveButton:folder->chatsList()->empty()];
	}, _lifetime);

	[self updatePinnedButtons];

	return self;
}

- (nullable NSTouchBarItem *) touchBar:(NSTouchBar *)touchBar
				 makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
	const id dictionaryItem = self.touchBarItems[identifier];
	const id type = dictionaryItem[@"type"];
	if ([type isEqualToString:@"slider"]) {
		NSSliderTouchBarItem *item = [[NSSliderTouchBarItem alloc] initWithIdentifier:identifier];
		item.slider.minValue = 0.0f;
		item.slider.maxValue = 1.0f;
		item.target = self;
		item.action = @selector(seekbarChanged:);
		item.customizationLabel = dictionaryItem[@"name"];
		[dictionaryItem setObject:item.slider forKey:@"view"];
		return item;
	} else if ([type isEqualToString:@"button"]) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSImage *image = dictionaryItem[@"image"];
		NSButton *button = [NSButton buttonWithImage:image target:self action:@selector(buttonAction:)];
		button.tag = [dictionaryItem[@"cmd"] intValue];
		item.view = button;
		item.customizationLabel = dictionaryItem[@"name"];
		[dictionaryItem setObject:button forKey:@"view"];
		return item;
	} else if ([type isEqualToString:@"textButton"]) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSButton *button = [NSButton buttonWithTitle:dictionaryItem[@"name"] target:self action:@selector(buttonAction:)];
		button.tag = [dictionaryItem[@"cmd"] intValue];
		item.view = button;
		item.customizationLabel = dictionaryItem[@"name"];
		[dictionaryItem setObject:button forKey:@"view"];
		return item;
	} else if ([type isEqualToString:@"text"]) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSTextField *text = [NSTextField labelWithString:@"00:00 / 00:00"];
		text.alignment = NSTextAlignmentCenter;
		item.view = text;
		item.customizationLabel = dictionaryItem[@"name"];
		[dictionaryItem setObject:text forKey:@"view"];
		return item;
	} else if ([type isEqualToString:@"popover"]) {
		NSPopoverTouchBarItem *item = [[NSPopoverTouchBarItem alloc] initWithIdentifier:identifier];
		item.collapsedRepresentationImage = dictionaryItem[@"image"];

		NSTouchBar *secondaryTouchBar = [[NSTouchBar alloc] init];
		secondaryTouchBar.delegate = self;
		if ([dictionaryItem[@"cmd"] intValue] == kCommandPopoverInput) {
			secondaryTouchBar.defaultItemIdentifiers = @[
				kBoldItemIdentifier,
				kItalicItemIdentifier,
				kMonospaceItemIdentifier,
				kLinkItemIdentifier,
				kClearItemIdentifier];
		}

		item.pressAndHoldTouchBar = secondaryTouchBar;
		item.popoverTouchBar = secondaryTouchBar;
		return item;
	} else if ([type isEqualToString:@"pinned"]) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		_mainPinnedButtons = [[NSMutableArray alloc] init];
		NSStackView *stackView = [[NSStackView alloc] init];

		for (auto i = kArchiveId; i <= Global::PinnedDialogsCountMax(); i++) {
			PinnedDialogButton *button =
				[[[PinnedDialogButton alloc] init:i] autorelease];
			[_mainPinnedButtons addObject:button];
			if (i == kArchiveId) {
				button.isDeletedFromView = true;
				continue;
			}
			[stackView addView:button.view inGravity:NSStackViewGravityCenter];
		}

		[stackView setSpacing:-15];
		item.view = stackView;
		[dictionaryItem setObject:item.view forKey:@"view"];
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

- (void) setTouchBar:(Platform::TouchBarType)type {
	if (_touchBarType == type) {
		return;
	}
	if (type == Platform::TouchBarType::Main) {
		[_parentView setTouchBar:_touchBarMain];
	} else if (type == Platform::TouchBarType::AudioPlayer) {
		if (!CurrentSongExists()
			|| Media::Player::instance()->getActiveType() != kSongType) {
			return;
		}
		[_parentView setTouchBar:_touchBarAudioPlayer];
	} else if (type == Platform::TouchBarType::AudioPlayerForce) {
		[_parentView setTouchBar:_touchBarAudioPlayer];
		_touchBarType = Platform::TouchBarType::AudioPlayer;
		return;
	} else if (type == Platform::TouchBarType::None) {
		[_parentView setTouchBar:nil];
	}
	_touchBarType = type;
}

- (void) showInputFieldItems:(bool)show {
	NSMutableArray *items = [NSMutableArray arrayWithObject:kPinnedPanelItemIdentifier];
	if (show) {
		[items addObject:kPopoverInputItemIdentifier];
		_touchBarMain.principalItemIdentifier = kPopoverInputItemIdentifier;
	}
	_touchBarMain.defaultItemIdentifiers = items;
}

// Main Touchbar.

- (void) toggleArchiveButton:(bool)hide {
	for (PinnedDialogButton *button in _mainPinnedButtons) {
		if (button.number == kArchiveId) {
			NSCustomTouchBarItem *item = [_touchBarMain itemForIdentifier:kPinnedPanelItemIdentifier];
			NSStackView *stack = item.view;
			[button updateUserpic];
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
	auto isArchivePinned = false;
	PinnedDialogButton *selfChatButton;
	NSCustomTouchBarItem *item = [_touchBarMain itemForIdentifier:kPinnedPanelItemIdentifier];
	NSStackView *stack = item.view;

	for (PinnedDialogButton *button in _mainPinnedButtons) {
		const auto num = button.number;
		if (num <= kSavedMessagesId) {
			if (num == kSavedMessagesId) {
				selfChatButton = button;
			} else if (num == kArchiveId) {
				isArchivePinned = !button.isDeletedFromView;
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
			[button updateUserpic];
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
		[stack insertView:selfChatButton.view
				  atIndex:(isArchivePinned ? 1 : 0)
				inGravity:NSStackViewGravityLeading];
	}
}

// Audio Player Touchbar.

- (void) handleTrackStateChange:(Media::Player::TrackState)state {
	if (state.id.type() == kSongType) {
		[self setTouchBar:Platform::TouchBarType::AudioPlayerForce];
	} else {
		return;
	}
	_position = state.position < 0 ? 0 : state.position;
	_duration = state.length;
	if (Media::Player::IsStoppedOrStopping(state.state)) {
		_position = 0;
		_duration = 0;
	}
	[self updateTouchBarTimeItem];

	NSButton *playButton = self.touchBarItems[kPlayItemIdentifier][@"view"];
	const auto imgButton = (state.state == Media::Player::State::Playing)
		? @"image"
		: @"imageAlt";
	playButton.image = self.touchBarItems[kPlayItemIdentifier][imgButton];

	[self.touchBarItems[kNextItemIdentifier][@"view"]
	 setEnabled:Media::Player::instance()->nextAvailable(kSongType)];
	[self.touchBarItems[kPreviousItemIdentifier][@"view"]
	 setEnabled:Media::Player::instance()->previousAvailable(kSongType)];
}

- (void) updateTouchBarTimeItem {
	const id item = self.touchBarItems[kCurrentPositionItemIdentifier];
	NSSlider *seekSlider = self.touchBarItems[kSeekBarItemIdentifier][@"view"];
	NSTextField *textField = item[@"view"];

	if (_duration <= 0) {
		seekSlider.enabled = NO;
		seekSlider.doubleValue = 0;
	} else {
		seekSlider.enabled = YES;
		if (!seekSlider.highlighted) {
			seekSlider.doubleValue = (_position / _duration) * seekSlider.maxValue;
		}
	}
	const auto timeToString = [&](int t) {
		return FormatTime((int)floor(t / kMs));
	};
	textField.stringValue = [NSString stringWithFormat:@"%@ / %@",
								timeToString(_position),
								timeToString(_duration)];

	NSTextField *field = item[@"view"];

	if (!field) {
		return;
	}

	[field removeConstraint:item[@"constrain"]];

	NSString *fString = [[textField.stringValue componentsSeparatedByCharactersInSet:
		[NSCharacterSet decimalDigitCharacterSet]] componentsJoinedByString:@"0"];
	NSSize size = [[NSTextField labelWithString:fString] frame].size;

	NSLayoutConstraint *con =
		[NSLayoutConstraint constraintWithItem:field
									 attribute:NSLayoutAttributeWidth
									 relatedBy:NSLayoutRelationEqual
										toItem:nil
									 attribute:NSLayoutAttributeNotAnAttribute
									multiplier:1.0
									  constant:(int)ceil(size.width) * 1.2];
	[field addConstraint:con];
	[item setObject:con forKey:@"constrain"];
}

- (void) buttonAction:(NSButton *)sender {
	const auto command = sender.tag;

	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		switch (command) {
		case kCommandPlayPause:
			Media::Player::instance()->playPause(kSongType);
			break;
		case kCommandPlaylistPrevious:
			Media::Player::instance()->previous(kSongType);
			break;
		case kCommandPlaylistNext:
			Media::Player::instance()->next(kSongType);
			break;
		case kCommandClosePlayer:
			App::main()->closeBothPlayers();
			break;
		// Input Field.
		case kCommandBold:
		case kCommandItalic:
		case kCommandMonospace:
		case kCommandClear:
		case kCommandLink:
			SendKeyEvent(command);
			break;
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

-(void)dealloc {
	for (PinnedDialogButton *button in _mainPinnedButtons) {
		[button release];
	}
	[super dealloc];
}

@end
