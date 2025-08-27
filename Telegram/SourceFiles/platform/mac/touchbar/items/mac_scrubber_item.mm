/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/items/mac_scrubber_item.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "base/call_delayed.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "chat_helpers/emoji_list_widget.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"
#include "styles/style_basic.h"
#include "styles/style_settings.h"
#include "ui/widgets/fields/input_field.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#import <AppKit/NSCustomTouchBarItem.h>
#import <AppKit/NSGestureRecognizer.h>
#import <AppKit/NSImage.h>
#import <AppKit/NSImageView.h>
#import <AppKit/NSPressGestureRecognizer.h>
#import <AppKit/NSScrollView.h>
#import <AppKit/NSScrubber.h>
#import <AppKit/NSScrubberItemView.h>
#import <AppKit/NSScrubberLayout.h>
#import <AppKit/NSSegmentedControl.h>
#import <AppKit/NSTextField.h>

#include <QtWidgets/QTextEdit>

using TouchBar::kCircleDiameter;
using TouchBar::CreateNSImageFromStyleIcon;

namespace {

//https://developer.apple.com/design/human-interface-guidelines/macos/touch-bar/touch-bar-icons-and-images/
constexpr auto kIdealIconSize = 36;
constexpr auto kSegmentIconSize = 25;
constexpr auto kSegmentSize = 92;

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

const auto kStickersScrubber = @"scrubberStickers";
const auto kEmojiScrubber = @"scrubberEmoji";

const auto kStickerItemIdentifier = @"stickerItem";
const auto kEmojiItemIdentifier = @"emojiItem";
const auto kPickerTitleItemIdentifier = @"pickerTitleItem";

enum ScrubberItemType {
	Emoji,
	Sticker,
	None,
};

inline bool IsSticker(ScrubberItemType type) {
	return type == ScrubberItemType::Sticker;
}

struct PickerScrubberItem {
	PickerScrubberItem(QString title) : title(title) {
	}
	PickerScrubberItem(DocumentData *document) : document(document) {
		mediaView = document->createMediaView();
		mediaView->checkStickerSmall();
		updateThumbnail();
	}
	PickerScrubberItem(EmojiPtr emoji) : emoji(emoji) {
	}

	void updateThumbnail() {
		if (!document || !image.isNull()) {
			return;
		}
		const auto sticker = mediaView->getStickerSmall();
		if (!sticker) {
			return;
		}
		const auto size = sticker->size()
			.scaled(kCircleDiameter, kCircleDiameter, Qt::KeepAspectRatio);
		image = sticker->pixSingle(
			size,
			{ .outer = { kCircleDiameter, kCircleDiameter } }).toImage();
	}

	bool isStickerLoaded() const {
		return !image.isNull();
	}

	QString title = QString();

	DocumentData *document = nullptr;
	std::shared_ptr<Data::DocumentMedia> mediaView = nullptr;
	QImage image;

	EmojiPtr emoji = nullptr;
};

struct PickerScrubberItemsHolder {
	std::vector<PickerScrubberItem> stickers;
	std::vector<PickerScrubberItem> emoji;

	int size(ScrubberItemType type) {
		return IsSticker(type) ? stickers.size() : emoji.size();
	}

	auto at(int index, ScrubberItemType type) {
		return IsSticker(type) ? stickers[index] : emoji[index];
	}
};

using Platform::Q2NSString;
using Platform::Q2NSImage;

NSImage *CreateNSImageFromEmoji(EmojiPtr emoji) {
	auto image = QImage(
		QSize(kIdealIconSize, kIdealIconSize) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::black);
	{
		Painter paint(&image);
		PainterHighQualityEnabler hq(paint);
		Ui::Emoji::Draw(
			paint,
			emoji,
			Ui::Emoji::GetSizeTouchbar(),
			0,
			0);
	}
	return Q2NSImage(image);
}

auto ActiveChat(not_null<Window::Controller*> controller) {
	if (const auto sessionController = controller->sessionController()) {
		return sessionController->activeChatCurrent();
	}
	return Dialogs::Key();
}

bool CanSendToActiveChat(
		not_null<Window::Controller*> controller,
			ChatRestriction right) {
	if (const auto topic = ActiveChat(controller).topic()) {
		return Data::CanSend(topic, right);
	} else if (const auto history = ActiveChat(controller).history()) {
		return Data::CanSend(history->peer, right);
	}
	return false;
}

std::optional<QString> RestrictionToSend(
		not_null<Window::Controller*> controller,
		ChatRestriction right) {
	if (const auto peer = ActiveChat(controller).peer()) {
		if (const auto error = Data::RestrictionError(peer, right)) {
			return *error;
		}
	}
	return std::nullopt;
}

QString TitleRecentlyUsed(const Data::StickersSets &sets) {
	const auto it = sets.find(Data::Stickers::CloudRecentSetId);
	return (it != sets.cend())
		? it->second->title
		: tr::lng_recent_stickers(tr::now);
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
	if (set->flags & Data::StickersSetFlag::Archived) {
		return;
	}
	if (!(set->flags & Data::StickersSetFlag::Installed)) {
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
	for (const auto &recent : recentPack) {
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

[[nodiscard]] EmojiPack RecentEmojiSection() {
	const auto list = Core::App().settings().recentEmoji();
	auto result = EmojiPack();
	result.reserve(list.size());
	for (const auto &emoji : list) {
		if (const auto one = std::get_if<EmojiPtr>(&emoji.id.data)) {
			result.push_back(*one);
		}
	}
	return result;
}

void AppendEmojiPacks(
		const Data::StickersSets &sets,
		std::vector<PickerScrubberItem> &to) {
	for (auto i = 0; i != ChatHelpers::kEmojiSectionCount; ++i) {
		const auto section = static_cast<Ui::Emoji::Section>(i);
		const auto list = (section == Ui::Emoji::Section::Recent)
			? RecentEmojiSection()
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

@interface PickerScrubberItemView : NSScrubberImageItemView {
	@public
	DocumentId documentId;
}
@end // @interface PickerScrubberItemView
@implementation PickerScrubberItemView
@end // @implementation PickerScrubberItemView

#pragma mark - PickerCustomTouchBarItem

@interface PickerCustomTouchBarItem : NSCustomTouchBarItem
	<NSScrubberDelegate,
	NSScrubberDataSource,
	NSScrubberFlowLayoutDelegate>
@end // @interface PickerCustomTouchBarItem

@implementation PickerCustomTouchBarItem {
	ScrubberItemType _type;
	std::shared_ptr<PickerScrubberItemsHolder> _itemsDataSource;
	std::unique_ptr<PickerScrubberItem> _error;
	DocumentId _lastPreviewedSticker;
	Window::Controller *_controller;
	History *_history;

	rpl::event_stream<> _closeRequests;
	rpl::lifetime _lifetime;
}

- (id)init:(ScrubberItemType)type
		controller:(not_null<Window::Controller*>)controller
		items:(std::shared_ptr<PickerScrubberItemsHolder>)items {
	Expects(controller->sessionController() != nullptr);
	self = [super initWithIdentifier:IsSticker(type)
		? kStickersScrubber
		: kEmojiScrubber];
	if (!self) {
		return self;
	}
	_type = type;
	_controller = controller;
	_itemsDataSource = items;

	auto *scrubber = [[[NSScrubber alloc] initWithFrame:NSZeroRect]
		autorelease];
	auto *layout = [[[NSScrubberFlowLayout alloc] init] autorelease];
	layout.itemSpacing = 10;
	scrubber.scrubberLayout = layout;
	scrubber.mode = NSScrubberModeFree;
	scrubber.delegate = self;
	scrubber.dataSource = self;
	scrubber.floatsSelectionViews = true;
	scrubber.showsAdditionalContentIndicators = true;
	scrubber.itemAlignment = NSScrubberAlignmentCenter;

	[scrubber registerClass:[PickerScrubberItemView class]
		forItemIdentifier:kStickerItemIdentifier];
	[scrubber registerClass:[NSScrubberTextItemView class]
		forItemIdentifier:kPickerTitleItemIdentifier];
	[scrubber registerClass:[NSScrubberImageItemView class]
		forItemIdentifier:kEmojiItemIdentifier];

	if (IsSticker(type)) {
		auto *gesture = [[[NSPressGestureRecognizer alloc]
			initWithTarget:self
			action:@selector(gesturePreviewHandler:)] autorelease];
		gesture.allowedTouchTypes = NSTouchTypeMaskDirect;
		gesture.minimumPressDuration = QApplication::startDragTime() / 1000.;
		gesture.allowableMovement = 0;
		[scrubber addGestureRecognizer:gesture];

		const auto kRight = ChatRestriction::SendStickers;
		if (const auto error = RestrictionToSend(_controller, kRight)) {
			_error = std::make_unique<PickerScrubberItem>(
				tr::lng_restricted_send_stickers_all(tr::now));
		}
	} else {
		const auto kRight = ChatRestriction::SendOther;
		if (const auto error = RestrictionToSend(_controller, kRight)) {
			_error = std::make_unique<PickerScrubberItem>(
				tr::lng_restricted_send_message_all(tr::now));
		}
	}
	_lastPreviewedSticker = 0;

	self.view = scrubber;
	return self;
}

- (PickerScrubberItem)itemAt:(int)index {
	return _error ? *_error : _itemsDataSource->at(index, _type);
}

- (void)gesturePreviewHandler:(NSPressGestureRecognizer*)gesture {
	const auto customEnter = [=](auto &&callback) {
		Core::Sandbox::Instance().customEnterFromEventLoop([=] {
			if (_controller) {
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
		const auto point = [gesture locationInView:container];

		for (PickerScrubberItemView *item in container.subviews) {
			if (![item isMemberOfClass:[PickerScrubberItemView class]]
				|| (item->documentId == _lastPreviewedSticker)
				|| !NSPointInRect(point, item.frame)) {
				continue;
			}
			_lastPreviewedSticker = item->documentId;
			auto &owner = _controller->sessionController()->session().data();
			const auto doc = owner.document(item->documentId);
			customEnter([=] {
				_controller->widget()->showMediaPreview(
					Data::FileOrigin(),
					doc);
			});
			break;
		}
	} else if (checkState(kGestureStateFinished)) {
		customEnter([=] { _controller->widget()->hideMediaPreview(); });
		_lastPreviewedSticker = 0;
	}
}

- (void)encodeWithCoder:(nonnull NSCoder*)aCoder {
	// Has not been implemented.
}

#pragma mark - NSScrubberDelegate

- (NSInteger)numberOfItemsForScrubber:(NSScrubber*)scrubber {
	return _error ? 1 : _itemsDataSource->size(_type);
}

- (NSScrubberItemView*)scrubber:(NSScrubber*)scrubber
		viewForItemAtIndex:(NSInteger)index {
	const auto item = [self itemAt:index];
	if (const auto document = item.document) {
		PickerScrubberItemView *itemView = [scrubber
			makeItemWithIdentifier:kStickerItemIdentifier
			owner:self];
		itemView.imageView.image = Q2NSImage(item.image);
		itemView->documentId = document->id;
		return itemView;
	} else if (const auto emoji = item.emoji) {
		NSScrubberImageItemView *itemView = [scrubber
			makeItemWithIdentifier:kEmojiItemIdentifier
			owner:self];
		itemView.imageView.image = CreateNSImageFromEmoji(emoji);
		return itemView;
	} else {
		NSScrubberTextItemView *itemView = [scrubber
			makeItemWithIdentifier:kPickerTitleItemIdentifier
			owner:self];
		itemView.textField.stringValue = Q2NSString(item.title);
		return itemView;
	}
}

- (NSSize)scrubber:(NSScrubber*)scrubber
		layout:(NSScrubberFlowLayout*)layout
		sizeForItemAtIndex:(NSInteger)index {
	const auto t = [self itemAt:index].title;
	const auto w = t.isEmpty() ? 0 : TouchBar::WidthFromString(Q2NSString(t));
	return NSMakeSize(kCircleDiameter + w, kCircleDiameter);
}

- (void)scrubber:(NSScrubber*)scrubber
		didSelectItemAtIndex:(NSInteger)index {
	scrubber.selectedIndex = -1;
	const auto sticker = _itemsDataSource->at(index, _type);
	const auto document = sticker.document;
	const auto emoji = sticker.emoji;
	const auto kRight = document
		? ChatRestriction::SendStickers
		: ChatRestriction::SendOther;
	if (!CanSendToActiveChat(_controller, kRight) || _error) {
		return;
	}
	auto callback = [=] {
		if (document) {
			if (const auto error = RestrictionToSend(_controller, kRight)) {
				_controller->show(Ui::MakeInformBox(*error));
				return true;
			} else if (Window::ShowSendPremiumError(_controller->sessionController(), document)) {
				return true;
			}
			Api::SendExistingDocument(
				Api::MessageToSend(
					Api::SendAction(ActiveChat(_controller).history())),
				document);
			return true;
		} else if (emoji) {
			if (const auto error = RestrictionToSend(_controller, kRight)) {
				_controller->show(Ui::MakeInformBox(*error));
				return true;
			} else if (const auto inputField = qobject_cast<QTextEdit*>(
					QApplication::focusWidget())) {
				Ui::InsertEmojiAtCursor(inputField->textCursor(), emoji);
				Core::App().settings().incrementRecentEmoji({ emoji });
				return true;
			}
		}
		return false;
	};

	if (!Core::Sandbox::Instance().customEnterFromEventLoop(
			std::move(callback))) {
		return;
	}

	_closeRequests.fire({});
}

- (rpl::producer<>)closeRequests {
	return _closeRequests.events();
}

- (rpl::lifetime &)lifetime {
	return _lifetime;
}

@end // @implementation PickerCustomTouchBarItem

#pragma mark - StickerEmojiPopover

@implementation StickerEmojiPopover {
	Window::Controller *_controller;
	Main::Session *_session;
	std::shared_ptr<PickerScrubberItemsHolder> _itemsDataSource;
	ScrubberItemType _waitingForUpdate;

	rpl::lifetime _lifetime;
}

- (id)init:(not_null<Window::Controller*>)controller
		identifier:(NSTouchBarItemIdentifier)identifier {
	self = [super initWithIdentifier:identifier];
	if (!self) {
		return nil;
	}
	_controller = controller;
	_session = &controller->sessionController()->session();
	_waitingForUpdate = ScrubberItemType::None;

	auto *segment = [[[NSSegmentedControl alloc] init] autorelease];
	const auto size = kSegmentIconSize;
	segment.segmentStyle = NSSegmentStyleSeparated;
	segment.segmentCount = 2;
	[segment
		setImage:CreateNSImageFromStyleIcon(st::settingsIconStickers, size)
		forSegment:0];
	[segment
		setImage:CreateNSImageFromStyleIcon(st::settingsIconEmoji, size)
		forSegment:1];
	[segment setWidth:kSegmentSize forSegment:0];
	[segment setWidth:kSegmentSize forSegment:1];
	segment.target = self;
	segment.action = @selector(segmentClicked:);
	segment.trackingMode = NSSegmentSwitchTrackingMomentary;
	self.visibilityPriority = NSTouchBarItemPriorityHigh;
	self.collapsedRepresentation = segment;

	self.popoverTouchBar = [[[NSTouchBar alloc] init] autorelease];
	self.popoverTouchBar.delegate = self;

	controller->sessionController()->activeChatValue(
	) | rpl::map([](Dialogs::Key k) {
		const auto topic = k.topic();
		const auto peer = k.peer();
		const auto right = ChatRestriction::SendStickers;
		return peer
			&& (topic
				? Data::CanSend(topic, right)
				: Data::CanSend(peer, right));
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool value) {
		[self dismissPopover:nil];
	}, _lifetime);


	_itemsDataSource = std::make_shared<PickerScrubberItemsHolder>();
	const auto localGuard = _lifetime.make_state<base::has_weak_ptr>();
	// Workaround.
	// A little waiting for the sticker sets and the ending animation.
	base::call_delayed(st::slideDuration, &(*localGuard), [=] {
		[self updateStickers];
		[self updateEmoji];
	});

	rpl::merge(
		rpl::merge(
			_session->data().stickers().updated(
				Data::StickersType::Stickers),
			_session->data().stickers().recentUpdated(
				Data::StickersType::Stickers)
		) | rpl::map_to(ScrubberItemType::Sticker),
		rpl::merge(
			Core::App().settings().recentEmojiUpdated(),
			Ui::Emoji::Updated()
		) | rpl::map_to(ScrubberItemType::Emoji)
	) | rpl::start_with_next([=](ScrubberItemType type) {
		_waitingForUpdate = type;
	}, _lifetime);

	return self;
}

- (NSTouchBarItem*)touchBar:(NSTouchBar*)touchBar
		makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
	if (!touchBar) {
		return nil;
	}
	const auto isEqual = [&](NSString *string) {
		return [identifier isEqualToString:string];
	};

	if (isEqual(kStickersScrubber)) {
		auto *item = [[[PickerCustomTouchBarItem alloc]
			init:(ScrubberItemType::Sticker)
			controller:_controller
			items:_itemsDataSource] autorelease];
		auto &lifetime = [item lifetime];
		[item closeRequests] | rpl::start_with_next([=] {
			[self dismissPopover:nil];
			[self updateStickers];
		}, lifetime);
		return item;
	} else if (isEqual(kEmojiScrubber)) {
		return [[[PickerCustomTouchBarItem alloc]
			init:(ScrubberItemType::Emoji)
			controller:_controller
			items:_itemsDataSource] autorelease];
	}
	return nil;
}

- (void)segmentClicked:(NSSegmentedControl*)sender {
	self.popoverTouchBar.defaultItemIdentifiers = @[];
	const auto identifier = sender.selectedSegment
		? kEmojiScrubber
		: kStickersScrubber;

	if (sender.selectedSegment
			&& _waitingForUpdate == ScrubberItemType::Emoji) {
		[self updateEmoji];
	} else if (!sender.selectedSegment
			&& _waitingForUpdate == ScrubberItemType::Sticker) {
		[self updateStickers];
	}

	self.popoverTouchBar.defaultItemIdentifiers = @[identifier];
	[self showPopover:nil];
}

- (void)addDownloadHandler {
	const auto loadingLifetime = _lifetime.make_state<rpl::lifetime>();
	const auto checkLoaded = [=](const auto &sticker) {
		return !sticker.document || sticker.isStickerLoaded();
	};
	const auto isPerformedOnMain = loadingLifetime->make_state<bool>(true);
	const auto localGuard = loadingLifetime->make_state<base::has_weak_ptr>();
	_session->downloaderTaskFinished(
	) | rpl::start_with_next(crl::guard(&(*localGuard), [=] {
		if (*isPerformedOnMain) {
			crl::on_main(&(*localGuard), [=] {
				for (auto &sticker : _itemsDataSource->stickers) {
					sticker.updateThumbnail();
				}
				if (ranges::all_of(_itemsDataSource->stickers, checkLoaded)) {
					loadingLifetime->destroy();
					return;
				}
				*isPerformedOnMain = true;
			});
		}
		*isPerformedOnMain = false;
	}), *loadingLifetime);
}

- (void)updateStickers {
	auto &stickers = _session->data().stickers();
	std::vector<PickerScrubberItem> temp;
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
	_itemsDataSource->stickers = std::move(temp);
	_waitingForUpdate = ScrubberItemType::None;
	[self addDownloadHandler];
}

- (void)updateEmoji {
	std::vector<PickerScrubberItem> temp;
	AppendEmojiPacks(_session->data().stickers().sets(), temp);
	_itemsDataSource->emoji = std::move(temp);
	_waitingForUpdate = ScrubberItemType::None;
}

@end // @implementation StickerEmojiPopover
