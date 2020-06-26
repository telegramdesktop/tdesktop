/*
 This file is part of Telegram Desktop,
 the official desktop application for the Telegram messaging service.

 For license and copyright information please follow this link:
 https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
 */

#import "mac_touchbar.h"
#import <QuartzCore/QuartzCore.h>

#include "api/api_sending.h"
#include "apiwrap.h"
#include "app.h"
#include "base/call_delayed.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "boxes/confirm_box.h"
#include "chat_helpers/emoji_list_widget.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "data/data_changes.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_stickers.h"
#include "dialogs/dialogs_layout.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "mtproto/mtproto_config.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "styles/style_media_player.h"
#include "styles/style_settings.h"
#include "ui/effects/animations.h"
#include "ui/emoji_config.h"
#include "ui/empty_userpic.h"
#include "ui/widgets/input_fields.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

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

constexpr auto kOnlineCircleSize = 8;
constexpr auto kOnlineCircleStrokeWidth = 1.5;
constexpr auto kUnreadBadgeSize = 15;

constexpr auto kMs = 1000;

constexpr auto kSongType = AudioMsgId::Type::Song;

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

QImage UnreadBadge(not_null<PeerData*> peer) {
	const auto history = peer->owner().history(peer->id);
	const auto count = history->unreadCountForBadge();
	if (!count) {
		return QImage();
	}
	const auto unread = history->unreadMark()
		? QString()
		: QString::number(count);
	Dialogs::Layout::UnreadBadgeStyle unreadSt;
	unreadSt.sizeId = Dialogs::Layout::UnreadBadgeInTouchBar;
	unreadSt.muted = history->mute();
	// Use constant values to draw badge regardless of cConfigScale().
	unreadSt.size = kUnreadBadgeSize * cRetinaFactor();
	unreadSt.padding = 4 * cRetinaFactor();
	unreadSt.font = style::font(
		9.5 * cRetinaFactor(),
		unreadSt.font->flags(),
		unreadSt.font->family());

	auto result = QImage(
		QSize(kCircleDiameter, kUnreadBadgeSize) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	Painter p(&result);

	Dialogs::Layout::paintUnreadCount(
		p,
		unread,
		result.width(),
		result.height() - unreadSt.size,
		unreadSt,
		nullptr,
		2);
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

TimeId CalculateOnlineTill(not_null<PeerData*> peer) {
	if (peer->isSelf()) {
		return 0;
	}
	if (const auto user = peer->asUser()) {
		if (!user->isServiceUser() && !user->isBot()) {
			const auto onlineTill = user->onlineTill;
			return (onlineTill <= -5)
				? -onlineTill
				: (onlineTill <= 0)
				? 0
				: onlineTill;
		}
	}
	return 0;
};

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

inline bool IsSelfPeer(PeerData *peer) {
	return peer && peer->isSelf();
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
		QImage unreadBadge;

		Ui::Animations::Simple shiftAnimation;
		int shift = 0;
		int finalShift = 0;
		int deltaShift = 0;
		int x = 0;
		int horizontalShift = 0;
		bool onTop = false;

		Ui::Animations::Simple onlineAnimation;
		TimeId onlineTill = 0;
	};
	rpl::lifetime _lifetime;
	Main::Session* _session;

	std::vector<std::shared_ptr<Pin>> _pins;
	QImage _savedMessages;
	QImage _archive;
	base::has_weak_ptr _guard;

	bool _hasArchive;
	bool _selfUnpinned;

	rpl::event_stream<not_null<NSEvent*>> _touches;

	double _r, _g, _b, _a; // The online circle color.
}

- (void)processHorizontalReorder {
	// This method is a simplified version of the VerticalLayoutReorder class
	// and is adapatized for horizontal use.
	enum class State : uchar {
		Started,
		Applied,
		Cancelled,
	};

	const auto currentStart = _lifetime.make_state<int>(0);
	const auto currentPeer = _lifetime.make_state<PeerData*>(nullptr);
	const auto currentState = _lifetime.make_state<State>(State::Cancelled);
	const auto currentDesiredIndex = _lifetime.make_state<int>(-1);
	const auto waitForFinish = _lifetime.make_state<bool>(false);
	const auto isDragging = _lifetime.make_state<bool>(false);

	const auto indexOf = [=](PeerData *p) {
		const auto i = ranges::find(_pins, p, &Pin::peer);
		Assert(i != end(_pins));
		return i - begin(_pins);
	};

	const auto setHorizontalShift = [=](const auto &pin, int shift) {
		if (const auto delta = shift - pin->horizontalShift) {
			pin->horizontalShift = shift;
			pin->x += delta;

			// Redraw a rectangle
			// from the beginning point of the pin movement to the end point.
			auto rect = PeerRectByIndex(indexOf(pin->peer) + [self shift]);
			const auto absDelta = std::abs(delta);
			rect.origin.x = pin->x - absDelta;
			rect.size.width += absDelta * 2;
			[self setNeedsDisplayInRect:rect];
		}
	};

	const auto updateShift = [=](not_null<PeerData*> peer, int indexHint) {
		Expects(indexHint >= 0 && indexHint < _pins.size());

		const auto index = (_pins[indexHint]->peer->id == peer->id)
			? indexHint
			: indexOf(peer);
		const auto entry = _pins[index];
		entry->shift = entry->deltaShift
			+ std::round(entry->shiftAnimation.value(entry->finalShift));
		if (entry->deltaShift && !entry->shiftAnimation.animating()) {
			entry->finalShift += entry->deltaShift;
			entry->deltaShift = 0;
		}
		setHorizontalShift(entry, entry->shift);
	};

	const auto moveToShift = [=](int index, int shift) {
		Core::Sandbox::Instance().customEnterFromEventLoop([=] {
			auto &entry = _pins[index];
			if (entry->finalShift + entry->deltaShift == shift) {
				return;
			}
			const auto peer = entry->peer;
			entry->shiftAnimation.start(
				[=] { updateShift(peer, index); },
				entry->finalShift,
				shift - entry->deltaShift,
				st::slideWrapDuration * 1);
			entry->finalShift = shift - entry->deltaShift;
		});
	};

	const auto cancelCurrentByIndex = [=](int index) {
		Expects(*currentPeer != nullptr);

		if (*currentState == State::Started) {
			*currentState = State::Cancelled;
		}
		*currentPeer = nullptr;
		for (auto i = 0, count = int(_pins.size()); i != count; ++i) {
			moveToShift(i, 0);
		}
	};

	const auto cancelCurrent = [=] {
		if (*currentPeer) {
			cancelCurrentByIndex(indexOf(*currentPeer));
		}
	};

	const auto updateOrder = [=](int index, int positionX) {
		const auto shift = positionX - *currentStart;
		const auto current = _pins[index];
		current->shiftAnimation.stop();
		current->shift = current->finalShift = shift;
		setHorizontalShift(current, shift);

		const auto count = _pins.size();
		const auto currentWidth = current->userpic.width();
		const auto currentMiddle = current->x + currentWidth / 2;
		*currentDesiredIndex = index;
		if (shift > 0) {
			auto top = current->x - shift;
			for (auto next = index + 1; next != count; ++next) {
				const auto &entry = _pins[next];
				top += entry->userpic.width();
				if (currentMiddle < top) {
					moveToShift(next, 0);
				} else {
					*currentDesiredIndex = next;
					moveToShift(next, -currentWidth);
				}
			}
			for (auto prev = index - 1; prev >= 0; --prev) {
				moveToShift(prev, 0);
			}
		} else {
			for (auto next = index + 1; next != count; ++next) {
				moveToShift(next, 0);
			}
			for (auto prev = index - 1; prev >= 0; --prev) {
				const auto &entry = _pins[prev];
				if (currentMiddle >= entry->x - entry->shift + currentWidth) {
					moveToShift(prev, 0);
				} else {
					*currentDesiredIndex = prev;
					moveToShift(prev, currentWidth);
				}
			}
		}
	};

	const auto checkForStart = [=](int positionX) {
		const auto shift = positionX - *currentStart;
		const auto delta = QApplication::startDragDistance();
		*isDragging = (std::abs(shift) > delta);
		if (!*isDragging) {
			return;
		}

		*currentState = State::Started;
		*currentStart += (shift > 0) ? delta : -delta;

		const auto index = indexOf(*currentPeer);
		*currentDesiredIndex = index;

		// Raise the pin.
		ranges::for_each(_pins, [=](const auto &pin) {
			pin->onTop = false;
		});
		_pins[index]->onTop = true;

		updateOrder(index, positionX);
	};

	const auto finishCurrent = [=] {
		if (!*currentPeer) {
			return;
		}
		const auto index = indexOf(*currentPeer);
		if (*currentDesiredIndex == index
			|| *currentState != State::Started) {
			cancelCurrentByIndex(index);
			return;
		}
		const auto result = *currentDesiredIndex;
		*currentState = State::Cancelled;
		*currentPeer = nullptr;

		const auto current = _pins[index];
		// Since the width of all elements is the same
		// we can use a single value.
		current->finalShift += (index - result) * current->userpic.width();

		if (!(current->finalShift + current->deltaShift)) {
			current->shift = 0;
			setHorizontalShift(current, 0);
		}
		current->horizontalShift = current->finalShift;
		base::reorder(_pins, index, result);

		*waitForFinish = true;
		// Call on end of an animation.
		base::call_delayed(st::slideWrapDuration * 1, _session, [=] {
			const auto guard = gsl::finally([=] {
				_session->data().notifyPinnedDialogsOrderUpdated();
				*waitForFinish = false;
			});
			if (index == result) {
				return;
			}
			const auto &order = _session->data().pinnedChatsOrder(
				nullptr,
				FilterId());
			const auto d = (index < result) ? 1 : -1; // Direction.
			for (auto i = index; i != result; i += d) {
				_session->data().chatsList()->pinned()->reorder(
					order.at(i).history(),
					order.at(i + d).history());
			}
			_session->api().savePinnedOrder(nullptr);
		});

		moveToShift(result, 0);
	};

	const auto touchBegan = [=](int touchX) {
		*isDragging = false;
		cancelCurrent();
		*currentStart = touchX;
		if (_pins.size() < 2) {
			return;
		}
		const auto index = [self indexFromX:*currentStart];
		if (index < 0) {
			return;
		}
		*currentPeer = _pins[index]->peer;
	};

	const auto touchMoved = [=](int touchX) {
		if (!*currentPeer) {
			return;
		} else if (*currentState != State::Started) {
			checkForStart(touchX);
		} else {
			updateOrder(indexOf(*currentPeer), touchX);
		}
	};

	const auto touchEnded = [=](int touchX) {
		if (*isDragging) {
			finishCurrent();
			return;
		}
		const auto step = QApplication::startDragDistance();
		if (std::abs(*currentStart - touchX) < step) {
			[self performAction:touchX];
		}
	};

	_touches.events(
	) | rpl::filter([=] {
		return !(*waitForFinish);
	}) | rpl::start_with_next([=](not_null<NSEvent*> event) {
		const auto *touches = [(event.get()).allTouches allObjects];
		if (touches.count != 1) {
			cancelCurrent();
			return;
		}
		const auto currentPosition = [touches[0] locationInView:self].x;
		switch (touches[0].phase) {
		case NSTouchPhaseBegan:
			return touchBegan(currentPosition);
		case NSTouchPhaseMoved:
			return touchMoved(currentPosition);
		case NSTouchPhaseEnded:
			return touchEnded(currentPosition);
		}
	}, _lifetime);

	_session->data().pinnedDialogsOrderUpdated(
	) | rpl::start_with_next(cancelCurrent, _lifetime);
}

- (id)init:(not_null<Main::Session*>)session {
	self = [super init];
	_session = session;
	_hasArchive = _selfUnpinned = false;
	_savedMessages = SavedMessagesUserpic();

	using UpdateFlag = Data::PeerUpdate::Flag;

	const auto downloadLifetime = _lifetime.make_state<rpl::lifetime>();
	const auto peerChangedLifetime = _lifetime.make_state<rpl::lifetime>();
	const auto lastDialogsCount = _lifetime.make_state<rpl::variable<int>>(0);
	auto &&peers = ranges::views::all(
		_pins
	) | ranges::views::transform(&Pin::peer);

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
	const auto singleUserpic = [=](const auto &pin) {
		if (IsSelfPeer(pin->peer)) {
			pin->userpic = _savedMessages;
			return;
		}
		auto userpic = PrepareImage();
		Painter p(&userpic);

		pin->peer->paintUserpic(p, pin->userpicView, 0, 0, userpic.width());
		userpic.setDevicePixelRatio(cRetinaFactor());
		pin->userpic = std::move(userpic);
	};
	const auto updateUserpics = [=] {
		ranges::for_each(_pins, singleUserpic);
		*lastDialogsCount = [self shift] + std::ssize(_pins);
		[self display];
	};
	const auto updateBadge = [=](const auto &pin) {
		const auto peer = pin->peer;
		if (IsSelfPeer(peer)) {
			return;
		}
		pin->unreadBadge = UnreadBadge(peer);

		const auto userpicIndex = pin->index + [self shift];
		[self setNeedsDisplayInRect:PeerRectByIndex(userpicIndex)];
	};
	const auto listenToDownloaderFinished = [=] {
		base::ObservableViewer(
			_session->downloaderTaskFinished()
		) | rpl::start_with_next([=] {
			const auto all = ranges::all_of(_pins, [=](const auto &pin) {
				return (!pin->peer->hasUserpic())
					|| (pin->userpicView && pin->userpicView->image());
			});
			if (all) {
				downloadLifetime->destroy();
			}
			updateUserpics();
		}, *downloadLifetime);
	};
	const auto processOnline = [=](const auto &pin) {
		// TODO: this should be replaced
		// with the global application timer for online statuses.
		const auto onlineChanges =
			peerChangedLifetime->make_state<rpl::event_stream<PeerData*>>();
		const auto onlineTimer =
			peerChangedLifetime->make_state<base::Timer>([=] {
				onlineChanges->fire_copy({ pin->peer });
			});

		const auto callTimer = [=](const auto &pin) {
			onlineTimer->cancel();
			if (pin->onlineTill) {
				const auto time = pin->onlineTill - base::unixtime::now();
				if (time > 0) {
					onlineTimer->callOnce(time * crl::time(1000));
				}
			}
		};
		callTimer(pin);

		using PeerUpdate = Data::PeerUpdate;
		auto to_peer = rpl::map([=](const PeerUpdate &update) -> PeerData* {
			return update.peer;
		});
		rpl::merge(
			_session->changes().peerUpdates(
				pin->peer,
				UpdateFlag::OnlineStatus) | to_peer,
			onlineChanges->events()
		) | rpl::start_with_next([=](PeerData *peer) {
			const auto it = ranges::find(_pins, peer, &Pin::peer);
			if (it == end(_pins)) {
				return;
			}
			const auto pin = *it;
			pin->onlineTill = CalculateOnlineTill(pin->peer);

			callTimer(pin);

			if (![NSApplication sharedApplication].active) {
				pin->onlineAnimation.stop();
				return;
			}
			const auto online = Data::OnlineTextActive(
				pin->onlineTill,
				base::unixtime::now());
			if (pin->onlineAnimation.animating()) {
				pin->onlineAnimation.change(
					online ? 1. : 0.,
					st::dialogsOnlineBadgeDuration);
			} else {
				const auto s = kOnlineCircleSize + kOnlineCircleStrokeWidth;
				Core::Sandbox::Instance().customEnterFromEventLoop([=] {
					pin->onlineAnimation.start(
						[=] {
							[self setNeedsDisplayInRect:NSMakeRect(
								pin->x + kCircleDiameter - s,
								0,
								s,
								s)];
						},
						online ? 0. : 1.,
						online ? 1. : 0.,
						st::dialogsOnlineBadgeDuration);
				});
			}
		}, *peerChangedLifetime);
	};

	const auto updatePinnedChats = [=] {
		_pins = ranges::view::zip(
			_session->data().pinnedChatsOrder(nullptr, FilterId()),
			ranges::view::ints(0, ranges::unreachable)
		) | ranges::views::transform([=](const auto &pair) {
			const auto index = pair.second;
			auto peer = pair.first.history()->peer;
			auto view = peer->createUserpicView();
			const auto onlineTill = CalculateOnlineTill(peer);
			Pin pin = {
				.peer = std::move(peer),
				.userpicView = std::move(view),
				.index = index,
				.onlineTill = onlineTill };
			return std::make_shared<Pin>(std::move(pin));
		});
		_selfUnpinned = ranges::none_of(peers, &PeerData::isSelf);

		peerChangedLifetime->destroy();
		for (const auto &pin : _pins) {
			const auto peer = pin->peer;
			_session->changes().peerUpdates(
				peer,
				UpdateFlag::Photo
			) | rpl::start_with_next(
				listenToDownloaderFinished,
				*peerChangedLifetime);

			if (const auto user = peer->asUser()) {
				if (!user->isServiceUser()
					&& !user->isBot()
					&& !peer->isSelf()) {
					processOnline(pin);
				}
			}

			rpl::merge(
				_session->changes().historyUpdates(
					_session->data().history(peer),
					Data::HistoryUpdate::Flag::UnreadView
				) | rpl::to_empty,
				_session->changes().peerFlagsValue(
					peer,
					UpdateFlag::Notifications
				) | rpl::to_empty
			) | rpl::start_with_next([=] {
				updateBadge(pin);
			}, *peerChangedLifetime);
		}

		updateUserpics();
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

	const auto updateOnlineColor = [=] {
		st::dialogsOnlineBadgeFg->c.getRgbF(&_r, &_g, &_b, &_a);
	};
	updateOnlineColor();

	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Window::Theme::BackgroundUpdate &update) {
		return update.paletteChanged();
	}) | rpl::start_with_next([=] {
		crl::on_main(&_guard, [=] {
			updateOnlineColor();
			if (const auto f = _session->data().folderLoaded(ArchiveId)) {
				_archive = ArchiveUserpic(f);
			}
			_savedMessages = SavedMessagesUserpic();
			updateUserpics();
		});
	}, _lifetime);

	listenToDownloaderFinished();
	[self processHorizontalReorder];
	return self;
}

- (int)shift {
	return (_hasArchive ? 1 : 0) + (_selfUnpinned ? 1 : 0);
}

- (void)touchesBeganWithEvent:(NSEvent *)event {
	_touches.fire(std::move(event));
}

- (void)touchesMovedWithEvent:(NSEvent *)event {
	_touches.fire(std::move(event));
}

- (void)touchesEndedWithEvent:(NSEvent *)event {
	_touches.fire(std::move(event));
}

- (int)indexFromX:(int)position {
	const auto x = position
		- kPinnedButtonsLeftSkip
		+ kPinnedButtonsSpace / 2;
	return x / (kCircleDiameter + kPinnedButtonsSpace) - [self shift];
}

- (void)performAction:(int)xPosition {
	const auto index = [self indexFromX:xPosition];
	const auto peer = (index < 0 || index >= std::ssize(_pins))
		? nullptr
		: _pins[index]->peer;
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
	return _pins[i]->userpic;
}

- (void)drawSinglePin:(int)i rect:(NSRect)dirtyRect {
	const auto rect = [&] {
		auto rect = PeerRectByIndex(i + [self shift]);
		if (i < 0) {
			return rect;
		}
		auto &pin = _pins[i];
		// We can have x = 0 when the pin is dragged.
		rect.origin.x = ((!pin->x && !pin->onTop) ? rect.origin.x : pin->x);
		pin->x = rect.origin.x;
		return rect;
	}();
	if (!NSIntersectsRect(rect, dirtyRect)) {
		return;
	}
	CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
	{
		CGImageRef image = ([self imageToDraw:i]).toCGImage();
		CGContextDrawImage(context, rect, image);
		CGImageRelease(image);
	}

	if (i >= 0) {
		const auto &pin = _pins[i];
		const auto rectRight = NSMaxX(rect);
		if (!pin->unreadBadge.isNull()) {
			CGImageRef image = pin->unreadBadge.toCGImage();
			const auto w = CGImageGetWidth(image) / cRetinaFactor();
			const auto borderRect = CGRectMake(
				rectRight - w,
				0,
				w,
				CGImageGetHeight(image) / cRetinaFactor());
			CGContextDrawImage(context, borderRect, image);
			CGImageRelease(image);
			return;
		}
		const auto online = Data::OnlineTextActive(
			pin->onlineTill,
			base::unixtime::now());
		const auto value = pin->onlineAnimation.value(online ? 1. : 0.);
		if (value < 0.05) {
			return;
		}
		const auto lineWidth = kOnlineCircleStrokeWidth;
		const auto circleSize = kOnlineCircleSize;
		const auto progress = value * circleSize;
		const auto diff = (circleSize - progress) / 2;
		const auto borderRect = CGRectMake(
			rectRight - circleSize + diff - lineWidth / 2,
			diff,
			progress,
			progress);

		CGContextSetRGBStrokeColor(context, 0, 0, 0, 1.0);
		CGContextSetRGBFillColor(context, _r, _g, _b, _a);
		CGContextSetLineWidth(context, lineWidth);
		CGContextFillEllipseInRect(context, borderRect);
		CGContextStrokeEllipseInRect(context, borderRect);
	}
}

- (void)drawRect:(NSRect)dirtyRect {
	const auto shift = [self shift];
	if (_pins.empty() && !shift) {
		return;
	}
	auto indexToTop = -1;
	const auto guard = gsl::finally([&] {
		if (indexToTop >= 0) {
			[self drawSinglePin:indexToTop rect:dirtyRect];
		}
	});
	for (auto i = -shift; i < std::ssize(_pins); i++) {
		if (i >= 0 && _pins[i]->onTop && (indexToTop < 0)) {
			indexToTop = i;
			continue;
		}
		[self drawSinglePin:i rect:dirtyRect];
	}
}

@end // @@implementation PinnedDialogsPanel

#pragma mark - End PinnedDialogsPanel


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
	_touchBarTypeBeforeLock = Platform::TouchBarType::Main;

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

// Audio Player Touchbar.

- (void) handleTrackStateChange:(Media::Player::TrackState)state {
	if (state.id.type() == kSongType) {
		if (_touchBarType != Platform::TouchBarType::None) {
			[self setTouchBar:Platform::TouchBarType::AudioPlayerForce];
		}
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
	[super dealloc];
}

@end // @implementation TouchBar
