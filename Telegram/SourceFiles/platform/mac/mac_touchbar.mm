/*
 This file is part of Telegram Desktop,
 the official desktop application for the Telegram messaging service.

 For license and copyright information please follow this link:
 https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
 */

#import "mac_touchbar.h"
#import <QuartzCore/QuartzCore.h>

#include "apiwrap.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "api/api_sending.h"
#include "boxes/confirm_box.h"
#include "chat_helpers/emoji_list_widget.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_cloud_file.h"
#include "data/stickers/data_stickers.h"
#include "dialogs/dialogs_layout.h"
#include "ui/emoji_config.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "styles/style_dialogs.h"
#include "styles/style_media_player.h"
#include "styles/style_settings.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "ui/empty_userpic.h"
#include "ui/widgets/input_fields.h"
#include "app.h"
#include "window/window_controller.h"

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

namespace {
//https://developer.apple.com/design/human-interface-guidelines/macos/touch-bar/touch-bar-icons-and-images/
constexpr auto kIdealIconSize = 36;
constexpr auto kMaximumIconSize = 44;
constexpr auto kCircleDiameter = 30;
constexpr auto kPinnedButtonsSpace = 30;
constexpr auto kPinnedButtonsLeftSkip = kPinnedButtonsSpace / 2;

constexpr auto kCommandPlayPause = 0x002;
constexpr auto kCommandPlaylistPrevious = 0x003;
constexpr auto kCommandPlaylistNext = 0x004;
constexpr auto kCommandClosePlayer = 0x005;

constexpr auto kCommandBold = 0x010;
constexpr auto kCommandItalic = 0x011;
constexpr auto kCommandUnderline = 0x012;
constexpr auto kCommandStrikeOut = 0x013;
constexpr auto kCommandMonospace = 0x014;
constexpr auto kCommandClear = 0x015;
constexpr auto kCommandLink = 0x016;

constexpr auto kCommandScrubberStickers = 0x020;
constexpr auto kCommandScrubberEmoji = 0x021;

constexpr auto kMs = 1000;

constexpr auto kSongType = AudioMsgId::Type::Song;

constexpr auto kSavedMessagesId = 0;
constexpr auto kArchiveId = -1;

constexpr auto kMaxStickerSets = 5;

constexpr auto kGestureStateProcessed = {
	NSGestureRecognizerStateChanged,
	NSGestureRecognizerStateBegan,
};

constexpr auto kGestureStateFinished = {
	NSGestureRecognizerStateEnded,
	NSGestureRecognizerStateCancelled,
	NSGestureRecognizerStateFailed,
};

NSString *const kTypePinned = @"pinned";
NSString *const kTypePinnedPanel = @"pinnedPanel";
NSString *const kTypeSlider = @"slider";
NSString *const kTypeButton = @"button";
NSString *const kTypeText = @"text";
NSString *const kTypeTextButton = @"textButton";
NSString *const kTypeScrubber = @"scrubber";
NSString *const kTypePicker = @"picker";
NSString *const kTypeFormatter = @"formatter";
NSString *const kTypeFormatterSegment = @"formatterSegment";

const NSString *kCustomizationIdPlayer = @"telegram.touchbar";
const NSString *kCustomizationIdMain = @"telegram.touchbarMain";
const NSTouchBarItemIdentifier kSavedMessagesItemIdentifier = [NSString stringWithFormat:@"%@.savedMessages", kCustomizationIdMain];
const NSTouchBarItemIdentifier kArchiveFolderItemIdentifier = [NSString stringWithFormat:@"%@.archiveFolder", kCustomizationIdMain];
const NSTouchBarItemIdentifier kPinnedPanelItemIdentifierOld = [NSString stringWithFormat:@"%@.pinnedPanelOld", kCustomizationIdMain];
const NSTouchBarItemIdentifier kPinnedPanelItemIdentifier = [NSString stringWithFormat:@"%@.pinnedPanel", kCustomizationIdMain];

const NSTouchBarItemIdentifier kSeekBarItemIdentifier = [NSString stringWithFormat:@"%@.seekbar", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kPlayItemIdentifier = [NSString stringWithFormat:@"%@.play", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kNextItemIdentifier = [NSString stringWithFormat:@"%@.nextItem", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kPreviousItemIdentifier = [NSString stringWithFormat:@"%@.previousItem", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kCommandClosePlayerItemIdentifier = [NSString stringWithFormat:@"%@.closePlayer", kCustomizationIdPlayer];
const NSTouchBarItemIdentifier kCurrentPositionItemIdentifier = [NSString stringWithFormat:@"%@.currentPosition", kCustomizationIdPlayer];

const NSTouchBarItemIdentifier kPopoverInputItemIdentifier = [NSString stringWithFormat:@"%@.popoverInput", kCustomizationIdMain];
const NSTouchBarItemIdentifier kPopoverInputFormatterItemIdentifier = [NSString stringWithFormat:@"%@.popoverInputFormatter", kCustomizationIdMain];

const NSTouchBarItemIdentifier kPickerPopoverItemIdentifier = [NSString stringWithFormat:@"%@.pickerButtons", kCustomizationIdMain];
const NSTouchBarItemIdentifier kScrubberStickersItemIdentifier = [NSString stringWithFormat:@"%@.scrubberStickers", kCustomizationIdMain];
const NSTouchBarItemIdentifier kStickerItemIdentifier = [NSString stringWithFormat:@"%@.stickerItem", kCustomizationIdMain];
const NSTouchBarItemIdentifier kScrubberEmojiItemIdentifier = [NSString stringWithFormat:@"%@.scrubberEmoji", kCustomizationIdMain];
const NSTouchBarItemIdentifier kEmojiItemIdentifier = [NSString stringWithFormat:@"%@.emojiItem", kCustomizationIdMain];
const NSTouchBarItemIdentifier kPickerTitleItemIdentifier = [NSString stringWithFormat:@"%@.pickerTitleItem", kCustomizationIdMain];

struct PickerScrubberItem {
	PickerScrubberItem(QString title) : title(title) {
	}
	PickerScrubberItem(DocumentData* document) : document(document) {
	}
	PickerScrubberItem(EmojiPtr emoji) : emoji(emoji) {
	}
	QString title = QString();
	DocumentData* document = nullptr;
	EmojiPtr emoji = nullptr;
};

enum ScrubberItemType {
	Emoji,
	Sticker,
};

using Platform::Q2NSString;

NSImage *CreateNSImageFromStyleIcon(const style::icon &icon, int size = kIdealIconSize) {
	const auto instance = icon.instance(QColor(255, 255, 255, 255), 100);
	auto pixmap = QPixmap::fromImage(instance);
	pixmap.setDevicePixelRatio(cRetinaFactor());
	NSImage *image = [qt_mac_create_nsimage(pixmap) autorelease];
	[image setSize:NSMakeSize(size, size)];
	return image;
}

NSImage *CreateNSImageFromEmoji(EmojiPtr emoji) {
	const auto s = kIdealIconSize * cIntRetinaFactor();
	auto pixmap = QPixmap(s, s);
	pixmap.setDevicePixelRatio(cRetinaFactor());
	pixmap.fill(Qt::black);
	Painter paint(&pixmap);
	PainterHighQualityEnabler hq(paint);
#ifndef OS_MAC_OLD
	Ui::Emoji::Draw(
		paint,
		std::move(emoji),
		Ui::Emoji::GetSizeTouchbar(),
		0,
		0);
#endif // OS_MAC_OLD
	return [qt_mac_create_nsimage(pixmap) autorelease];
}

int TouchXPosition(NSEvent *e, NSView *v) {
	return [[[e.allTouches allObjects] objectAtIndex:0] locationInView:v].x;
}

bool IsSingleTouch(NSEvent *e) {
	return [e.allTouches allObjects].count == 1;
}

QImage PrepareImage() {
	const auto s = kCircleDiameter * cIntRetinaFactor();
	auto result = QImage(QSize(s, s), QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	return result;
}

QImage SavedMessagesUserpic() {
	auto result = PrepareImage();
	Painter paint(&result);

	const auto s = result.width();
	Ui::EmptyUserpic::PaintSavedMessages(paint, 0, 0, s, s);
	return result;
}

QImage ArchiveUserpic(not_null<Data::Folder*> folder) {
	auto result = PrepareImage();
	Painter paint(&result);

	auto view = std::shared_ptr<Data::CloudImageView>();
	folder->paintUserpic(paint, view, 0, 0, result.width());
	return result;
}

NSRect PeerRectByIndex(int index) {
	return NSMakeRect(
		index * (kCircleDiameter + kPinnedButtonsSpace)
			+ kPinnedButtonsLeftSkip,
		0,
		kCircleDiameter,
		kCircleDiameter);
}

int WidthFromString(NSString *s) {
	return (int)ceil(
		[[NSTextField labelWithString:s] frame].size.width) * 1.2;
}

inline bool IsSticker(ScrubberItemType type) {
	return type == ScrubberItemType::Sticker;
}

inline bool CurrentSongExists() {
	return Media::Player::instance()->current(kSongType).audio() != nullptr;
}

inline bool UseEmptyUserpic(PeerData *peer, std::shared_ptr<Data::CloudImageView> &userpic) {
	return peer && (peer->useEmptyUserpic(userpic) || peer->isSelf());
}

inline bool IsSelfPeer(PeerData *peer) {
	return peer && peer->isSelf();
}

inline int UnreadCount(not_null<PeerData*> peer) {
	if (const auto history = peer->owner().historyLoaded(peer)) {
		return history->unreadCountForBadge();
	}
	return 0;
}

inline auto GetActiveChat() {
	if (const auto window = App::wnd()) {
		if (const auto controller = window->sessionController()) {
			return controller->activeChatCurrent();
		}
	}
	return Dialogs::Key();
}

inline bool CanWriteToActiveChat() {
	if (const auto history = GetActiveChat().history()) {
		return history->peer->canWrite();
	}
	return false;
}

inline std::optional<QString> RestrictionToSendStickers() {
	if (const auto peer = GetActiveChat().peer()) {
		return Data::RestrictionError(
			peer,
			ChatRestriction::f_send_stickers);
	}
	return std::nullopt;
}

QString TitleRecentlyUsed(const Data::StickersSets &sets) {
	const auto it = sets.find(Data::Stickers::CloudRecentSetId);
	if (it != sets.cend()) {
		return it->second->title;
	}
	return tr::lng_recent_stickers(tr::now);
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

	Dialogs::Layout::paintUnreadCount(
		p,
		unread,
		kCircleDiameter,
		kCircleDiameter - unreadSt.size,
		unreadSt,
		nullptr,
		2);
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
	case kCommandUnderline:
		key = Qt::Key_U;
		break;
	case kCommandStrikeOut:
		key = Qt::Key_X;
		modifier |= Qt::ShiftModifier;
		break;
	}
	QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, key, modifier));
	QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, key, modifier));
}

void AppendStickerSet(
		const Data::StickersSets &sets,
		std::vector<PickerScrubberItem> &to,
		uint64 setId) {
	const auto it = sets.find(setId);
	if (it == sets.cend() || it->second->stickers.isEmpty()) {
		return;
	}
	const auto set = it->second.get();
	if (set->flags & MTPDstickerSet::Flag::f_archived) {
		return;
	}
	if (!(set->flags & MTPDstickerSet::Flag::f_installed_date)) {
		return;
	}

	to.emplace_back(PickerScrubberItem(set->title.isEmpty()
		? set->shortName
		: set->title));
	for (const auto sticker : set->stickers) {
		to.emplace_back(PickerScrubberItem(sticker));
	}
}

void AppendRecentStickers(
		const Data::StickersSets &sets,
		RecentStickerPack &recentPack,
		std::vector<PickerScrubberItem> &to) {
	const auto cloudIt = sets.find(Data::Stickers::CloudRecentSetId);
	const auto cloudCount = (cloudIt != sets.cend())
		? cloudIt->second->stickers.size()
		: 0;
	if (cloudCount > 0) {
		to.emplace_back(PickerScrubberItem(cloudIt->second->title));
		auto count = 0;
		for (const auto document : cloudIt->second->stickers) {
			if (document->owner().stickers().isFaved(document)) {
				continue;
			}
			to.emplace_back(PickerScrubberItem(document));
		}
	}
	for (const auto recent : recentPack) {
		to.emplace_back(PickerScrubberItem(recent.first));
	}
}

void AppendFavedStickers(
		const Data::StickersSets &sets,
		std::vector<PickerScrubberItem> &to) {
	const auto it = sets.find(Data::Stickers::FavedSetId);
	const auto count = (it != sets.cend())
		? it->second->stickers.size()
		: 0;
	if (!count) {
		return;
	}
	to.emplace_back(PickerScrubberItem(
		tr::lng_mac_touchbar_favorite_stickers(tr::now)));
	for (const auto document : it->second->stickers) {
		to.emplace_back(PickerScrubberItem(document));
	}
}

void AppendEmojiPacks(
		const Data::StickersSets &sets,
		std::vector<PickerScrubberItem> &to) {
	for (auto i = 0; i != ChatHelpers::kEmojiSectionCount; ++i) {
		const auto section = static_cast<Ui::Emoji::Section>(i);
		const auto list = (section == Ui::Emoji::Section::Recent)
			? GetRecentEmojiSection()
			: Ui::Emoji::GetSection(section);
		const auto title = (section == Ui::Emoji::Section::Recent)
			? TitleRecentlyUsed(sets)
			: ChatHelpers::EmojiCategoryTitle(i)(tr::now);
		to.emplace_back(title);
		for (const auto &emoji : list) {
			to.emplace_back(PickerScrubberItem(emoji));
		}
	}
}

} // namespace

@interface PinButton : NSButton
@end // @interface PinButton

@implementation PinButton {
	int _startPosition;
	int _tempIndex;
	bool _orderChanged;
	Main::Session *_session;
}

- (void)touchesBeganWithEvent:(NSEvent *)event {
	if ([event.allTouches allObjects].count > 1) {
		return;
	}
	_orderChanged = false;
	_tempIndex = self.tag  - 1;
	_startPosition = [self getTouchX:event];
	[super touchesBeganWithEvent:event];
}

- (void)touchesMovedWithEvent:(NSEvent *)event {
	if (!_session || self.tag <= kSavedMessagesId) {
		return;
	}
	if ([event.allTouches allObjects].count > 1) {
		return;
	}
	const auto currentPosition = [self getTouchX:event];
	const auto step = kPinnedButtonsSpace + kCircleDiameter;
	if (std::abs(_startPosition - currentPosition) > step) {
		const auto delta = (currentPosition > _startPosition) ? 1 : -1;
		const auto newIndex = _tempIndex + delta;
		const auto &order = _session->data().pinnedChatsOrder(nullptr, FilterId());

		// In case the order has been changed from another device
		// while the user is dragging the dialog.
		if (_tempIndex >= order.size()) {
			return;
		}

		if (newIndex >= 0 && newIndex < order.size()) {
			_session->data().reorderTwoPinnedChats(
				FilterId(),
				order.at(_tempIndex).history(),
				order.at(newIndex).history());
			_tempIndex = newIndex;
			_startPosition = currentPosition;
			_orderChanged = true;
		}
	}
}

- (void)setSession:(not_null<Main::Session*>)session {
	_session = session;
}

- (void)touchesEndedWithEvent:(NSEvent *)event {
	if (_orderChanged && _session) {
		_session->api().savePinnedOrder(nullptr);
	}
	[super touchesEndedWithEvent:event];
}

- (int)getTouchX:(NSEvent *)e {
	return [[[e.allTouches allObjects] objectAtIndex:0] locationInView:self].x;
}
@end // @implementation PinButton

#pragma mark - PinnedDialogsPanel

@interface PinnedDialogsPanel : NSImageView
- (id)init:(not_null<Main::Session*>)session;
@end // @interface PinnedDialogsPanel

@implementation PinnedDialogsPanel {
	struct Pin {
		PeerData *peer = nullptr;
		std::shared_ptr<Data::CloudImageView> userpicView = nullptr;
		int index = -1;
		QImage userpic;
	};

	rpl::lifetime _lifetime;
	Main::Session* _session;
	std::vector<Pin> _pins;
	QImage _savedMessages;
	QImage _archive;
	base::has_weak_ptr _guard;

	bool _hasArchive;
	bool _selfUnpinned;
	int _startPosition;
}

- (id)init:(not_null<Main::Session*>)session {
	self = [super init];
	_session = session;
	_startPosition = 0;
	_hasArchive = _selfUnpinned = false;
	_savedMessages = SavedMessagesUserpic();

	const auto downloadLifetime = _lifetime.make_state<rpl::lifetime>();
	const auto peerChangedLifetime = _lifetime.make_state<rpl::lifetime>();
	const auto lastDialogsCount = _lifetime.make_state<rpl::variable<int>>(0);
	auto &&peers = ranges::views::all(
		_pins
	) | ranges::views::transform(&Pin::peer);

	const auto updateBadge = [=](Pin &pin) {
		const auto peer = pin.peer;
		if (IsSelfPeer(peer)
			|| !peer->owner().history(peer->id)->unreadCountForBadge()) {
			return;
		}
		auto pixmap = App::pixmapFromImageInPlace(
			base::take(pin.userpic));
		if (pixmap.isNull()) {
			return;
		}

		Painter p(&pixmap);
		PaintUnreadBadge(p, peer);
		pin.userpic = pixmap.toImage();

		const auto userpicIndex = pin.index + [self shift];
		[self setNeedsDisplayInRect:PeerRectByIndex(userpicIndex)];
	};
	const auto updatePanelSize = [=] {
		const auto size = lastDialogsCount->current();
		self.image = [[NSImage alloc] initWithSize:NSMakeSize(
			size * (kCircleDiameter + kPinnedButtonsSpace)
				+ kPinnedButtonsLeftSkip
				- kPinnedButtonsSpace / 2,
			kCircleDiameter)];
	};
	lastDialogsCount->changes(
	) | rpl::start_with_next(updatePanelSize, _lifetime);
	const auto singleUserpic = [=](Pin &pin) {
		if (IsSelfPeer(pin.peer)) {
			pin.userpic = _savedMessages;
			return;
		}
		auto userpic = pin.peer->genUserpic(
			pin.userpicView,
			kCircleDiameter);

		Painter p(&userpic);
		PaintUnreadBadge(p, pin.peer);
		userpic.setDevicePixelRatio(cRetinaFactor());
		pin.userpic = userpic.toImage();
	};
	const auto updateUserpics = [=] {
		ranges::for_each(_pins, singleUserpic);
		*lastDialogsCount = [self shift] + std::ssize(_pins);
		[self display];
	};
	const auto listenToDownloaderFinished = [=] {
		base::ObservableViewer(
			_session->downloaderTaskFinished()
		) | rpl::start_with_next([=] {
			const auto all = ranges::all_of(_pins, [=](const auto &pin) {
				return (!pin.peer->hasUserpic())
					|| (pin.userpicView && pin.userpicView->image());
			});
			if (all) {
				downloadLifetime->destroy();
			}
			updateUserpics();
		}, *downloadLifetime);
	};
	const auto updatePinnedChats = [=] {
		_pins = ranges::view::zip(
			_session->data().pinnedChatsOrder(nullptr, FilterId()),
			ranges::view::ints(0, ranges::unreachable)
		) | ranges::views::transform([=](const auto &pair) -> Pin {
			const auto index = pair.second;
			auto peer = pair.first.history()->peer;
			if (!_pins.empty() && index < std::ssize(_pins)) {
				if (peer->id == _pins[index].peer->id) {
					// Reuse the existing pin.
					return _pins[index];
				}
			}
			auto view = peer->createUserpicView();
			return { std::move(peer), std::move(view), index, QImage() };
		});
		_selfUnpinned = ranges::none_of(peers, &PeerData::isSelf);

		peerChangedLifetime->destroy();
		for (const auto &pin : _pins) {
			_session->changes().peerUpdates(
				pin.peer,
				Data::PeerUpdate::Flag::Photo
			) | rpl::start_with_next(
					listenToDownloaderFinished,
					*peerChangedLifetime);

			using UpdateFlag = Data::PeerUpdate::Flag;
			auto to_empty = rpl::map([=] { return rpl::empty_value(); });

			rpl::merge(
				_session->changes().historyUpdates(
					_session->data().history(pin.peer),
					Data::HistoryUpdate::Flag::UnreadView
				) | to_empty,
				_session->changes().peerFlagsValue(
					pin.peer,
					UpdateFlag::Notifications
				) | to_empty
			) | rpl::start_with_next([=] {
				updateBadge(_pins[pin.index]);
			}, *peerChangedLifetime);
		}

		updateUserpics();
		// ranges::for_each(peers, updateBadge);
	};

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		_session->data().pinnedDialogsOrderUpdated()
	) | rpl::start_with_next(updatePinnedChats, _lifetime);

	const auto ArchiveId = Data::Folder::kId;
	rpl::single(
		rpl::empty_value()
	) | rpl::map([=] {
		return _session->data().folderLoaded(ArchiveId);
	}) | rpl::then(
		_session->data().chatsListChanges()
	) | rpl::filter([](Data::Folder *folder) {
		return folder && (folder->id() == ArchiveId);
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		_hasArchive = !folder->chatsList()->empty();
		if (_archive.isNull()) {
			_archive = ArchiveUserpic(folder);
		}
		updateUserpics();
	}, _lifetime);

	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Window::Theme::BackgroundUpdate &update) {
		return update.paletteChanged();
	}) | rpl::start_with_next([=] {
		crl::on_main(&_guard, [=] {
			if (const auto f = _session->data().folderLoaded(ArchiveId)) {
				_archive = ArchiveUserpic(f);
			}
			_savedMessages = SavedMessagesUserpic();
			updateUserpics();
		});
	}, _lifetime);

	listenToDownloaderFinished();
	return self;
}

- (int)shift {
	return (_hasArchive ? 1 : 0) + (_selfUnpinned ? 1 : 0);
}

- (void)touchesBeganWithEvent:(NSEvent *)event {
	if (!IsSingleTouch(event)) {
		return;
	}
	_startPosition = TouchXPosition(event, self);
	[super touchesBeganWithEvent:event];
}

- (void)touchesEndedWithEvent:(NSEvent *)event {
	if (!IsSingleTouch(event)) {
		return;
	}
	const auto currentPosition = TouchXPosition(event, self);
	const auto step = kPinnedButtonsSpace;
	if (std::abs(_startPosition - currentPosition) < step) {
		[self performAction:currentPosition];
	}
}

- (void)performAction:(int)xPosition {
	const auto x = xPosition
		- kPinnedButtonsLeftSkip
		+ kPinnedButtonsSpace / 2;
	const auto index = x / (kCircleDiameter + kPinnedButtonsSpace)
		- [self shift];

	const auto peer = (index < 0 || index >= std::ssize(_pins))
		? nullptr
		: _pins[index].peer;
	if (!peer && !_hasArchive && !_selfUnpinned) {
		return;
	}

	const auto active = Core::App().activeWindow();
	const auto controller = active ? active->sessionController() : nullptr;
	const auto openFolder = [=] {
		const auto folder = _session->data().folderLoaded(Data::Folder::kId);
		if (folder && controller) {
			controller->openFolder(folder);
		}
	};
	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		(_hasArchive && (index == (_selfUnpinned ? -2 : -1)))
			? openFolder()
			: controller->content()->choosePeer(
				(_selfUnpinned && index == -1)
					? _session->userPeerId()
					: peer->id,
				ShowAtUnreadMsgId);
	});
}

- (QImage)imageToDraw:(int)i {
	Expects(i < std::ssize(_pins));
	if (i < 0) {
		if (_hasArchive && (i == -[self shift])) {
			return _archive;
		} else if (_selfUnpinned) {
			return _savedMessages;
		}
	}
	return _pins[i].userpic;
}

- (void)drawRect:(NSRect)dirtyRect {
	const auto shift = [self shift];
	if (_pins.empty() && !shift) {
		return;
	}
	for (auto i = -shift; i < std::ssize(_pins); i++) {
		const auto rect = PeerRectByIndex(i + shift);
		if (!NSIntersectsRect(rect, dirtyRect)) {
			continue;
		}
		CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
		CGImageRef image = ([self imageToDraw:i]).toCGImage();
		CGContextDrawImage(context, rect, image);
		CGImageRelease(image);

	}
}

@end // @@implementation PinnedDialogsPanel

#pragma mark - End PinnedDialogsPanel

@interface PinnedDialogButton : NSCustomTouchBarItem

@property(nonatomic, assign) int number;
@property(nonatomic, assign) PeerData *peer;
@property(nonatomic, assign) bool isDeletedFromView;
@property(nonatomic, assign) QPixmap userpic;

- (id) init:(int)num session:(not_null<Main::Session*>)session;
- (void)buttonActionPin:(NSButton *)sender;
- (void)updateUserpic;

@end // @interface PinnedDialogButton

@implementation PinnedDialogButton {
	rpl::lifetime _lifetime;
	rpl::lifetime _peerChangedLifetime;
	base::has_weak_ptr _guard;
	std::shared_ptr<Data::CloudImageView> _userpicView;
	Main::Session* _session;

	bool isWaitingUserpicLoad;
}

- (id) init:(int)num session:(not_null<Main::Session*>)session {
	_session = session;
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

	PinButton *button = [[PinButton alloc] initWithFrame:NSZeroRect];
	[button setSession:_session];
	NSButtonCell *cell = [[NSButtonCell alloc] init];
	[cell setBezelStyle:NSBezelStyleCircular];
	button.cell = cell;
	button.tag = num;
	button.target = self;
	button.action = @selector(buttonActionPin:);
	self.view = button;

	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Window::Theme::BackgroundUpdate &update) {
		return update.paletteChanged();
	}) | rpl::start_with_next([=] {
		crl::on_main(&_guard, [=] {
			if (_number <= kSavedMessagesId || UseEmptyUserpic(_peer, _userpicView)) {
				[self updateUserpic];
			} else if (_peer
				&& (UnreadCount(_peer) || Data::IsPeerAnOnlineUser(_peer))) {
				[self updateBadge];
			}
		});
	}, _lifetime);

	if (num <= kSavedMessagesId) {
		[self updateUserpic];
		return self;
	}

	base::ObservableViewer(
		_session->downloaderTaskFinished()
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
	_peerChangedLifetime.destroy();
	_peer = newPeer;
	if (!_peer || IsSelfPeer(_peer)) {
		_userpicView = nullptr;
		return;
	}
	_userpicView = _peer->createUserpicView();
	_peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::Photo
	) | rpl::start_with_next([=] {
		isWaitingUserpicLoad = true;
		[self updateUserpic];
	}, _peerChangedLifetime);

	_peer->session().changes().historyUpdates(
		_peer->session().data().history(_peer),
		Data::HistoryUpdate::Flag::UnreadView
	) | rpl::start_with_next([=] {
		[self updateBadge];
	}, _peerChangedLifetime);

	_peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::OnlineStatus
	) | rpl::filter([=] {
		return UnreadCount(_peer) == 0;
	}) | rpl::start_with_next([=] {
		[self updateBadge];
	}, _peerChangedLifetime);
}

- (void) buttonActionPin:(NSButton *)sender {
	const auto active = Core::App().activeWindow();
	const auto controller = active ? active->sessionController() : nullptr;
	const auto openFolder = [=] {
		if (!controller) {
			return;
		}
		if (const auto folder = _session->data().folderLoaded(Data::Folder::kId)) {
			controller->openFolder(folder);
		}
	};
	Core::Sandbox::Instance().customEnterFromEventLoop([=] {
		self.number == kArchiveId
			? openFolder()
			: controller->content()->choosePeer(self.number == kSavedMessagesId
				? _session->userPeerId()
				: self.peer->id, ShowAtUnreadMsgId);
	});
}

- (void) updateUserpic {
	// Don't draw self userpic if we pin Saved Messages.
	if (self.number <= kSavedMessagesId || IsSelfPeer(_peer)) {
		const auto s = kIdealIconSize * cIntRetinaFactor();
		_userpic = QPixmap(s, s);
		Painter paint(&_userpic);
		paint.fillRect(QRectF(0, 0, s, s), Qt::black);

		if (self.number != kArchiveId) {
			Ui::EmptyUserpic::PaintSavedMessages(paint, 0, 0, s, s);
		} else if (const auto folder =
				_session->data().folderLoaded(Data::Folder::kId)) {
			// Not used in the folders.
			auto view = std::shared_ptr<Data::CloudImageView>();
			folder->paintUserpic(paint, view, 0, 0, s);
		}
		_userpic.setDevicePixelRatio(cRetinaFactor());
		[self updateImage:_userpic];
		return;
	}
	if (!self.peer) {
		return;
	}
	isWaitingUserpicLoad = _userpicView && !_userpicView->image();
	_userpic = self.peer->genUserpic(_userpicView, kIdealIconSize);
	_userpic.setDevicePixelRatio(cRetinaFactor());
	[self updateBadge];
}

- (void) updateBadge {
	if (IsSelfPeer(_peer)) {
		return;
	}
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
	NSImage *image = [qt_mac_create_nsimage(pixmap) autorelease];
	[image setSize:NSMakeSize(kCircleDiameter, kCircleDiameter)];
	[button.cell setImage:image];
}

@end // @implementation PinnedDialogButton


@interface PickerScrubberItemView : NSScrubberItemView
@end // @interface PickerScrubberItemView
@implementation PickerScrubberItemView {
	rpl::lifetime _lifetime;
	std::shared_ptr<Data::DocumentMedia> _media;
	Image *_image;
	QImage _qimage;
	@public
	DocumentData *documentData;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
	self = [super initWithFrame:frameRect];
	return self;
}

- (void)drawRect:(NSRect)dirtyRect {
	if (_qimage.isNull()) {
		[[NSColor blackColor] setFill];
		NSRectFill(dirtyRect);
	} else {
		CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
		CGImageRef image = _qimage.toCGImage();
		CGContextDrawImage(context, dirtyRect, image);
		CGImageRelease(image);
	}
}

- (void)addDocument:(not_null<DocumentData*>)document {
	if (!document->sticker()) {
		return;
	}
	documentData = document;
	_media = document->createMediaView();
	_media->checkStickerSmall();
	_image = _media->getStickerSmall();
	if (_image) {
		[self updateImage];
		return;
	}
	base::ObservableViewer(
		document->session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		_image = _media->getStickerSmall();
		if (_image) {
			[self updateImage];
			_lifetime.destroy();
		}
	}, _lifetime);
}

- (void)updateImage {
	const auto size = _image->size()
			.scaled(kCircleDiameter, kCircleDiameter, Qt::KeepAspectRatio);
	_qimage = _image->pixSingle(
		size.width(),
		size.height(),
		kCircleDiameter,
		kCircleDiameter,
		ImageRoundRadius::None).toImage();
	[self display];
}
@end // @implementation PickerScrubberItemView


@interface PickerCustomTouchBarItem: NSCustomTouchBarItem
	<NSScrubberDelegate,
	NSScrubberDataSource,
	NSScrubberFlowLayoutDelegate>
@end // @interface PickerCustomTouchBarItem

#pragma mark -

@implementation PickerCustomTouchBarItem {
	std::vector<PickerScrubberItem> _stickers;
	NSPopoverTouchBarItem *_parentPopover;
	DocumentId _lastPreviewedSticker;
	Main::Session* _session;
}

- (id) init:(ScrubberItemType)type
	popover:(NSPopoverTouchBarItem *)popover
	session:(not_null<Main::Session*>)session {
	self = [super initWithIdentifier:IsSticker(type)
		? kScrubberStickersItemIdentifier
		: kScrubberEmojiItemIdentifier];
	if (!self) {
		return self;
	}
	_session = session;
	_parentPopover = popover;
	IsSticker(type) ? [self updateStickers] : [self updateEmoji];
	NSScrubber *scrubber = [[[NSScrubber alloc] initWithFrame:NSZeroRect] autorelease];
	NSScrubberFlowLayout *layout = [[[NSScrubberFlowLayout alloc] init] autorelease];
	layout.itemSpacing = 10;
	scrubber.scrubberLayout = layout;
	scrubber.mode = NSScrubberModeFree;
	scrubber.delegate = self;
	scrubber.dataSource = self;
	scrubber.floatsSelectionViews = true;
	scrubber.showsAdditionalContentIndicators = true;
	scrubber.itemAlignment = NSScrubberAlignmentCenter;
	[scrubber registerClass:[PickerScrubberItemView class] forItemIdentifier:kStickerItemIdentifier];
	[scrubber registerClass:[NSScrubberTextItemView class] forItemIdentifier:kPickerTitleItemIdentifier];
	[scrubber registerClass:[NSScrubberImageItemView class] forItemIdentifier:kEmojiItemIdentifier];

	if (IsSticker(type)) {
		auto *gesture = [[NSPressGestureRecognizer alloc]
			initWithTarget:self
			action:@selector(gesturePreviewHandler:)];
		gesture.allowedTouchTypes = NSTouchTypeMaskDirect;
		gesture.minimumPressDuration = QApplication::startDragTime() / 1000.;
		gesture.allowableMovement = 0;
		[scrubber addGestureRecognizer:gesture];
	}
	_lastPreviewedSticker = 0;

	self.view = scrubber;
	return self;
}

- (void)gesturePreviewHandler:(NSPressGestureRecognizer *)gesture {
	const auto customEnter = [](const auto callback) {
		Core::Sandbox::Instance().customEnterFromEventLoop([=] {
			if (App::wnd()) {
				callback();
			}
		});
	};

	const auto checkState = [&](const auto &states) {
		return ranges::contains(states, gesture.state);
	};

	if (checkState(kGestureStateProcessed)) {
		NSScrollView *scrollView = self.view;
		auto *container = scrollView.documentView.subviews.firstObject;
		if (!container) {
			return;
		}
		const auto point = [gesture locationInView:(std::move(container))];

		for (PickerScrubberItemView *item in container.subviews) {
			const auto &doc = item->documentData;
			if (![item isMemberOfClass:[PickerScrubberItemView class]]
				|| !doc
				|| (doc->id == _lastPreviewedSticker)
				|| !NSPointInRect(point, item.frame)) {
				continue;
			}
			_lastPreviewedSticker = doc->id;
			customEnter([doc = std::move(doc)] {
				App::wnd()->showMediaPreview(Data::FileOrigin(), doc);
			});
			break;
		}
	} else if (checkState(kGestureStateFinished)) {
		customEnter([] { App::wnd()->hideMediaPreview(); });
		_lastPreviewedSticker = 0;
	}
}

- (void)encodeWithCoder:(nonnull NSCoder *)aCoder {
	// Has not been implemented.
}

#pragma mark - NSScrubberDelegate

- (NSInteger)numberOfItemsForScrubber:(NSScrubber *)scrubber {
	return _stickers.size();
}

- (NSScrubberItemView *)scrubber:(NSScrubber *)scrubber viewForItemAtIndex:(NSInteger)index {
	const auto item = _stickers[index];
	if (const auto document = item.document) {
		PickerScrubberItemView *itemView = [scrubber makeItemWithIdentifier:kStickerItemIdentifier owner:nil];
		[itemView addDocument:(std::move(document))];
		return itemView;
	} else if (const auto emoji = item.emoji) {
		NSScrubberImageItemView *itemView = [scrubber makeItemWithIdentifier:kEmojiItemIdentifier owner:nil];
		itemView.imageView.image = CreateNSImageFromEmoji(emoji);
		return itemView;
	} else {
		NSScrubberTextItemView *itemView = [scrubber makeItemWithIdentifier:kPickerTitleItemIdentifier owner:nil];
		itemView.textField.stringValue = Q2NSString(item.title);
		return itemView;
	}
}

- (NSSize)scrubber:(NSScrubber *)scrubber layout:(NSScrubberFlowLayout *)layout sizeForItemAtIndex:(NSInteger)index {
	if (const auto t = _stickers[index].title; !t.isEmpty()) {
		return NSMakeSize(
			WidthFromString(Q2NSString(t)) + kCircleDiameter, kCircleDiameter);
	}
	return NSMakeSize(kCircleDiameter, kCircleDiameter);
}

- (void)scrubber:(NSScrubber *)scrubber didSelectItemAtIndex:(NSInteger)index {
	if (!CanWriteToActiveChat()) {
		return;
	}
	scrubber.selectedIndex = -1;
	const auto chat = GetActiveChat();

	const auto callback = [&]() -> bool {
		if (const auto document = _stickers[index].document) {
			if (const auto error = RestrictionToSendStickers()) {
				Ui::show(Box<InformBox>(*error));
			}
			Api::SendExistingDocument(
				Api::MessageToSend(chat.history()),
				document);
			return true;
		} else if (const auto emoji = _stickers[index].emoji) {
			if (const auto inputField = qobject_cast<QTextEdit*>(
					QApplication::focusWidget())) {
				Ui::InsertEmojiAtCursor(inputField->textCursor(), emoji);
				AddRecentEmoji(emoji);
				return true;
			}
		}
		return false;
	};

	if (!Core::Sandbox::Instance().customEnterFromEventLoop(callback)) {
		return;
	}

	if (_parentPopover) {
		[_parentPopover dismissPopover:nil];
	}
}

- (void)updateStickers {
	auto &stickers = _session->data().stickers();
	std::vector<PickerScrubberItem> temp;
	if (const auto error = RestrictionToSendStickers()) {
		temp.emplace_back(PickerScrubberItem(
			tr::lng_restricted_send_stickers_all(tr::now)));
		_stickers = std::move(temp);
		return;
	}
	AppendFavedStickers(stickers.sets(), temp);
	AppendRecentStickers(stickers.sets(), stickers.getRecentPack(), temp);
	auto count = 0;
	for (const auto setId : stickers.setsOrderRef()) {
		AppendStickerSet(stickers.sets(), temp, setId);
		if (++count == kMaxStickerSets) {
			break;
		}
	}
	if (!temp.size()) {
		temp.emplace_back(PickerScrubberItem(
			tr::lng_stickers_nothing_found(tr::now)));
	}
	_stickers = std::move(temp);
}

- (void)updateEmoji {
	std::vector<PickerScrubberItem> temp;
	AppendEmojiPacks(_session->data().stickers().sets(), temp);
	_stickers = std::move(temp);
}

@end // @implementation PickerCustomTouchBarItem


@interface TouchBar()<NSTouchBarDelegate>
@end // @interface TouchBar

@implementation TouchBar {
	NSView *_parentView;
	NSMutableArray *_mainPinnedButtons;

	NSTouchBar *_touchBarMain;
	NSTouchBar *_touchBarAudioPlayer;

	NSPopoverTouchBarItem *_popoverPicker;

	Platform::TouchBarType _touchBarType;
	Platform::TouchBarType _touchBarTypeBeforeLock;

	Main::Session* _session;

	double _duration;
	double _position;

	rpl::lifetime _lifetime;
	rpl::lifetime _lifetimeSessionControllerChecker;
}

- (id) init:(NSView *)view session:(not_null<Main::Session*>)session {
	self = [super init];
	if (!self) {
		return nil;
	}
	_session = session;

	const auto iconSize = kIdealIconSize / 3;
	_position = 0;
	_duration = 0;
	_parentView = view;
	self.touchBarItems = @{
		kPinnedPanelItemIdentifierOld: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypePinned,
		}],
		kPinnedPanelItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypePinnedPanel,
		}],
		kSeekBarItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type": kTypeSlider,
			@"name": @"Seek Bar"
		}],
		kPlayItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":     kTypeButton,
			@"name":     @"Play Button",
			@"cmd":      [NSNumber numberWithInt:kCommandPlayPause],
			@"image":    CreateNSImageFromStyleIcon(st::touchBarIconPlayerPause, iconSize),
			@"imageAlt": CreateNSImageFromStyleIcon(st::touchBarIconPlayerPlay, iconSize),
		}],
		kPreviousItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypeButton,
			@"name":  @"Previous Playlist Item",
			@"cmd":   [NSNumber numberWithInt:kCommandPlaylistPrevious],
			@"image": CreateNSImageFromStyleIcon(st::touchBarIconPlayerPrevious, iconSize),
		}],
		kNextItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypeButton,
			@"name":  @"Next Playlist Item",
			@"cmd":   [NSNumber numberWithInt:kCommandPlaylistNext],
			@"image": CreateNSImageFromStyleIcon(st::touchBarIconPlayerNext, iconSize),
		}],
		kCommandClosePlayerItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypeButton,
			@"name":  @"Close Player",
			@"cmd":   [NSNumber numberWithInt:kCommandClosePlayer],
			@"image": CreateNSImageFromStyleIcon(st::touchBarIconPlayerClose, iconSize),
		}],
		kCurrentPositionItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type": kTypeText,
			@"name": @"Current Position"
		}],
		kPopoverInputItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypeFormatter,
			@"image": [NSImage imageNamed:NSImageNameTouchBarTextItalicTemplate],
		}],
		kPopoverInputFormatterItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypeFormatterSegment,
		}],
		kScrubberStickersItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypeScrubber,
			@"cmd":   [NSNumber numberWithInt:kCommandScrubberStickers],
		}],
		kScrubberEmojiItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypeScrubber,
			@"cmd":   [NSNumber numberWithInt:kCommandScrubberEmoji],
		}],
		kPickerPopoverItemIdentifier: [NSMutableDictionary dictionaryWithDictionary:@{
			@"type":  kTypePicker,
			@"name":  @"Picker",
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

	_session->data().pinnedDialogsOrderUpdated(
	) | rpl::start_with_next([] {
		// [self updatePinnedButtons];
	}, _lifetime);

	_session->data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return folder
			&& folder->chatsList()
			&& folder->id() == Data::Folder::kId;
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		// [self toggleArchiveButton:folder->chatsList()->empty()];
	}, _lifetime);


	// At the time of this touchbar creation the sessionController does
	// not yet exist. But at the time of chatsListChanges event
	// the sessionController is valid and we can work with it.
	// So _lifetimeSessionControllerChecker is needed only once.
	_session->data().chatsListChanges(
	) | rpl::start_with_next([=] {
		if (const auto window = App::wnd()) {
			if (const auto controller = window->sessionController()) {
				if (_session->data().stickers().setsRef().empty()) {
					_session->api().updateStickers();
				}
				_lifetimeSessionControllerChecker.destroy();
				controller->activeChatChanges(
				) | rpl::start_with_next([=](Dialogs::Key key) {
					const auto show = key.peer()
						&& key.history()
						&& key.peer()->canWrite();
					[self showPickerItem:show];
				}, _lifetime);
			}
		}
	}, _lifetimeSessionControllerChecker);

	rpl::merge(
		_session->data().stickers().updated(),
		_session->data().stickers().recentUpdated()
	) | rpl::start_with_next([=] {
		[self updatePickerPopover:ScrubberItemType::Sticker];
	}, _lifetime);

	rpl::merge(
		UpdatedRecentEmoji(),
		Ui::Emoji::Updated()
	) | rpl::start_with_next([=] {
		[self updatePickerPopover:ScrubberItemType::Emoji];
	}, _lifetime);

	// [self updatePinnedButtons];

	return self;
}

- (nullable NSTouchBarItem *) touchBar:(NSTouchBar *)touchBar
				 makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
	const id dictionaryItem = self.touchBarItems[identifier];
	const id type = dictionaryItem[@"type"];
	const auto isType = [type](NSString *string) {
		return [type isEqualToString:string];
	};
	if (isType(kTypeSlider)) {
		NSSliderTouchBarItem *item = [[NSSliderTouchBarItem alloc] initWithIdentifier:identifier];
		item.slider.minValue = 0.0f;
		item.slider.maxValue = 1.0f;
		item.target = self;
		item.action = @selector(seekbarChanged:);
		item.customizationLabel = dictionaryItem[@"name"];
		[dictionaryItem setObject:item.slider forKey:@"view"];
		return item;
	} else if (isType(kTypeButton) || isType(kTypeTextButton)) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSButton *button = isType(kTypeButton)
			? [NSButton buttonWithImage:dictionaryItem[@"image"]
				target:self action:@selector(buttonAction:)]
			: [NSButton buttonWithTitle:dictionaryItem[@"name"]
				target:self action:@selector(buttonAction:)];
		button.tag = [dictionaryItem[@"cmd"] intValue];
		item.view = button;
		item.customizationLabel = dictionaryItem[@"name"];
		[dictionaryItem setObject:button forKey:@"view"];
		return item;
	} else if (isType(kTypeText)) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSTextField *text = [NSTextField labelWithString:@"00:00 / 00:00"];
		text.alignment = NSTextAlignmentCenter;
		item.view = text;
		item.customizationLabel = dictionaryItem[@"name"];
		[dictionaryItem setObject:text forKey:@"view"];
		return item;
	} else if (isType(kTypeFormatter)) {
		NSPopoverTouchBarItem *item = [[NSPopoverTouchBarItem alloc] initWithIdentifier:identifier];
		item.collapsedRepresentationImage = dictionaryItem[@"image"];
		NSTouchBar *secondaryTouchBar = [[NSTouchBar alloc] init];
		secondaryTouchBar.delegate = self;
		secondaryTouchBar.defaultItemIdentifiers = @[kPopoverInputFormatterItemIdentifier];
		item.popoverTouchBar = secondaryTouchBar;
		return item;
	} else if (isType(kTypeFormatterSegment)) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		NSScrollView *scroll = [[NSScrollView alloc] init];
		NSSegmentedControl *segment = [[NSSegmentedControl alloc] init];
		segment.segmentStyle = NSSegmentStyleRounded;
		segment.target = self;
		segment.action = @selector(formatterClicked:);

		static const auto strings = {
			tr::lng_menu_formatting_bold,
			tr::lng_menu_formatting_italic,
			tr::lng_menu_formatting_underline,
			tr::lng_menu_formatting_strike_out,
			tr::lng_menu_formatting_monospace,
			tr::lng_menu_formatting_clear,
			tr::lng_info_link_label,
		};
		segment.segmentCount = strings.size();
		auto width = 0;
		auto count = 0;
		for (const auto s : strings) {
			const auto string = Q2NSString(s(tr::now));
			width += WidthFromString(string) * 1.4;
			[segment setLabel:string forSegment:count++];
		}
		segment.frame = NSMakeRect(0, 0, width, kCircleDiameter);
		[scroll setDocumentView:segment];
		item.view = scroll;
		return item;
	} else if (isType(kTypeScrubber)) {
		const auto isSticker = ([dictionaryItem[@"cmd"] intValue]
			== kCommandScrubberStickers);
		const auto type = isSticker
			? ScrubberItemType::Sticker
			: ScrubberItemType::Emoji;
		const auto popover = isSticker
			? _popoverPicker
			: nil;
		PickerCustomTouchBarItem *item = [[PickerCustomTouchBarItem alloc]
			init:type popover:popover session:_session];
		return item;
	} else if (isType(kTypePicker)) {
		NSPopoverTouchBarItem *item = [[NSPopoverTouchBarItem alloc] initWithIdentifier:identifier];
		_popoverPicker = item;
		NSSegmentedControl *segment = [[NSSegmentedControl alloc] init];
		[self updatePickerPopover:ScrubberItemType::Sticker];
		[self updatePickerPopover:ScrubberItemType::Emoji];
		const auto imageSize = kIdealIconSize / 3 * 2;
		segment.segmentStyle = NSSegmentStyleSeparated;
		segment.segmentCount = 2;
		[segment setImage:CreateNSImageFromStyleIcon(st::settingsIconStickers, imageSize) forSegment:0];
		[segment setImage:CreateNSImageFromStyleIcon(st::settingsIconEmoji, imageSize) forSegment:1];
		[segment setWidth:92 forSegment:0];
		[segment setWidth:92 forSegment:1];
		segment.target = self;
		segment.action = @selector(segmentClicked:);
		segment.trackingMode = NSSegmentSwitchTrackingMomentary;
		item.visibilityPriority = NSTouchBarItemPriorityHigh;
		item.collapsedRepresentation = segment;
		return item;
	} else if (isType(kTypePinned)) {
		NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		_mainPinnedButtons = [[NSMutableArray alloc] init];
		NSStackView *stackView = [[NSStackView alloc] init];

		const auto &config = _session->serverConfig();
		for (auto i = kArchiveId; i <= config.pinnedDialogsCountMax.current(); i++) {
			PinnedDialogButton *button =
				[[[PinnedDialogButton alloc]
					init:i
					session:_session] autorelease];
			[_mainPinnedButtons addObject:button];
			if (i == kArchiveId) {
				button.isDeletedFromView = true;
				continue;
			}
			[stackView addView:button.view inGravity:NSStackViewGravityTrailing];
		}
		const auto space = kPinnedButtonsSpace;
		[stackView setEdgeInsets:NSEdgeInsetsMake(0, space / 2., 0, space)];
		[stackView setSpacing:space];
		item.view = stackView;
		[dictionaryItem setObject:item.view forKey:@"view"];
		return item;
	} else if (isType(kTypePinnedPanel)) {
		auto *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
		item.customizationLabel = @"Pinned Panel";
		item.view = [[PinnedDialogsPanel alloc] init:_session];
		[dictionaryItem setObject:item.view forKey:@"view"];
		return item;
	}

	return nil;
}

- (void) createTouchBar {
	_touchBarMain = [[NSTouchBar alloc] init];
	_touchBarMain.delegate = self;
	[self showItemInMain:nil];

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

- (void) showInputFieldItem:(bool)show {
	[self showItemInMain: show
		? kPopoverInputItemIdentifier
		: CanWriteToActiveChat()
			? kPickerPopoverItemIdentifier
			: nil];
}

- (void) showPickerItem:(bool)show {
	[self showItemInMain: show
		? kPickerPopoverItemIdentifier
		: nil];
}

- (void) showItemInMain:(NSTouchBarItemIdentifier)item {
	NSMutableArray *items = [NSMutableArray arrayWithArray:@[kPinnedPanelItemIdentifier]];
	if (item) {
		[items addObject:item];
	}
	_touchBarMain.defaultItemIdentifiers = items;
}

- (void) updatePickerPopover:(ScrubberItemType)type {
	NSTouchBar *secondaryTouchBar = [[NSTouchBar alloc] init];
	secondaryTouchBar.delegate = self;
	const auto popover = IsSticker(type)
		? _popoverPicker
		: nil;
	[[PickerCustomTouchBarItem alloc] init:type popover:popover session:_session];
	const auto identifier = IsSticker(type)
		? kScrubberStickersItemIdentifier
		: kScrubberEmojiItemIdentifier;
	secondaryTouchBar.defaultItemIdentifiers = @[identifier];
	_popoverPicker.popoverTouchBar = secondaryTouchBar;
}

// Main Touchbar.

- (void) toggleArchiveButton:(bool)hide {
	for (PinnedDialogButton *button in _mainPinnedButtons) {
		if (button.number == kArchiveId) {
			NSCustomTouchBarItem *item = [_touchBarMain itemForIdentifier:kPinnedPanelItemIdentifierOld];
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
	const auto &order = _session->data().pinnedChatsOrder(nullptr, FilterId());
	auto isSelfPeerPinned = false;
	auto isArchivePinned = false;
	PinnedDialogButton *selfChatButton;
	NSCustomTouchBarItem *item = [_touchBarMain itemForIdentifier:kPinnedPanelItemIdentifierOld];
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
			if (history->peer->id == _session->userPeerId()) {
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

	NSLayoutConstraint *con =
		[NSLayoutConstraint constraintWithItem:field
									 attribute:NSLayoutAttributeWidth
									 relatedBy:NSLayoutRelationEqual
										toItem:nil
									 attribute:NSLayoutAttributeNotAnAttribute
									multiplier:1.0
									  constant:WidthFromString(fString)];
	[field addConstraint:con];
	[item setObject:con forKey:@"constrain"];
}

- (void) buttonAction:(NSButton *)sender {
	const auto command = sender.tag;

	const auto active = Core::App().activeWindow();
	const auto controller = active ? active->sessionController() : nullptr;
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
			controller->content()->closeBothPlayers();
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

- (void) segmentClicked:(NSSegmentedControl *)sender {
	const auto identifier = sender.selectedSegment
		? kScrubberEmojiItemIdentifier
		: kScrubberStickersItemIdentifier;
	_popoverPicker.popoverTouchBar.defaultItemIdentifiers = @[identifier];
	[_popoverPicker showPopover:nil];
}

- (void) formatterClicked:(NSSegmentedControl *)sender {
	[[_touchBarMain itemForIdentifier:kPopoverInputItemIdentifier]
		dismissPopover:nil];
	const auto command = int(sender.selectedSegment) + kCommandBold;
	sender.selectedSegment = -1;
	SendKeyEvent(command);
}

-(void)dealloc {
	for (PinnedDialogButton *button in _mainPinnedButtons) {
		[button release];
	}
	[super dealloc];
}

@end // @implementation TouchBar
