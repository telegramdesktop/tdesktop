/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/items/mac_pinned_chats_item.h"

#ifndef OS_OSX

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "data/data_changes.h"
#include "data/data_cloud_file.h"
#include "data/data_file_origin.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_layout.h"
#include "history/history.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"
#include "styles/style_dialogs.h"
#include "ui/effects/animations.h"
#include "ui/empty_userpic.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#import <AppKit/NSColor.h>
#import <AppKit/NSGraphicsContext.h>
#import <AppKit/NSPressGestureRecognizer.h>

using TouchBar::kCircleDiameter;

namespace {

constexpr auto kPinnedButtonsSpace = 30;
constexpr auto kPinnedButtonsLeftSkip = kPinnedButtonsSpace / 2;

constexpr auto kOnlineCircleSize = 8;
constexpr auto kOnlineCircleStrokeWidth = 1.5;
constexpr auto kUnreadBadgeSize = 15;

inline bool IsSelfPeer(PeerData *peer) {
	return peer && peer->isSelf();
}

inline bool IsRepliesPeer(PeerData *peer) {
	return peer && peer->isRepliesChat();
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

QImage RepliesMessagesUserpic() {
	auto result = PrepareImage();
	Painter paint(&result);

	const auto s = result.width();
	Ui::EmptyUserpic::PaintRepliesMessages(paint, 0, 0, s, s);
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
	if (peer->isSelf() || peer->isRepliesChat()) {
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

} // namespace

#pragma mark - PinnedDialogsPanel

@interface PinnedDialogsPanel()
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
	Main::Session *_session;

	std::vector<std::unique_ptr<Pin>> _pins;
	QImage _savedMessages;
	QImage _repliesMessages;
	QImage _archive;

	bool _hasArchive;
	bool _selfUnpinned;
	bool _repliesUnpinned;

	rpl::event_stream<not_null<NSEvent*>> _touches;
	rpl::event_stream<not_null<NSPressGestureRecognizer*>> _gestures;

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
		const auto &entry = _pins[index];
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
				st::slideWrapDuration);
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
		const auto &current = _pins[index];
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

	const auto localGuard = _lifetime.make_state<base::has_weak_ptr>();

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

		const auto &current = _pins[index];
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
		base::call_delayed(st::slideWrapDuration, &(*localGuard), [=] {
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

	_gestures.events(
	) | rpl::filter([=] {
		return !(*waitForFinish);
	}) | rpl::start_with_next([=](
			not_null<NSPressGestureRecognizer*> gesture) {
		const auto currentPosition = [gesture locationInView:self].x;

		switch ([gesture state]) {
		case NSGestureRecognizerStateBegan:
			return touchBegan(currentPosition);
		case NSGestureRecognizerStateChanged:
			return touchMoved(currentPosition);
		case NSGestureRecognizerStateCancelled:
		case NSGestureRecognizerStateEnded:
			return touchEnded(currentPosition);
		}
	}, _lifetime);

	_session->data().pinnedDialogsOrderUpdated(
	) | rpl::start_with_next(cancelCurrent, _lifetime);

	_lifetime.add([=] {
		for (const auto &pin : _pins) {
			pin->shiftAnimation.stop();
			pin->onlineAnimation.stop();
		}
	});

}

- (id)init:(not_null<Main::Session*>)session
		destroyEvent:(rpl::producer<>)touchBarSwitches {
	self = [super init];
	_session = session;
	_hasArchive = _selfUnpinned = false;
	_savedMessages = SavedMessagesUserpic();
	_repliesMessages = RepliesMessagesUserpic();

	auto *gesture = [[[NSPressGestureRecognizer alloc]
		initWithTarget:self
		action:@selector(gestureHandler:)] autorelease];
	gesture.allowedTouchTypes = NSTouchTypeMaskDirect;
	gesture.minimumPressDuration = 0;
	gesture.allowableMovement = 0;
	[self addGestureRecognizer:gesture];

	// For some reason, sometimes a parent deallocates not immediately,
	// but only after the user's input (mouse movement, key pressing, etc.).
	// So we have to use a custom event to destroy the current lifetime
	// manually, before it leads to crashes.
	std::move(
		touchBarSwitches
	) | rpl::start_with_next([=] {
		_lifetime.destroy();
	}, _lifetime);

	using UpdateFlag = Data::PeerUpdate::Flag;

	const auto downloadLifetime = _lifetime.make_state<rpl::lifetime>();
	const auto peerChangedLifetime = _lifetime.make_state<rpl::lifetime>();
	const auto lastDialogsCount = _lifetime.make_state<rpl::variable<int>>(0);
	auto &&peers = ranges::views::all(
		_pins
	) | ranges::views::transform(&Pin::peer);

	const auto updatePanelSize = [=] {
		const auto size = lastDialogsCount->current();
		if (self.image) {
			[self.image release];
		}
		// TODO: replace it with NSLayoutConstraint.
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
		} else if (IsRepliesPeer(pin->peer)) {
			pin->userpic = _repliesMessages;
			return;
		}
		auto userpic = PrepareImage();
		Painter p(&userpic);

		pin->peer->paintUserpic(p, pin->userpicView, 0, 0, userpic.width());
		userpic.setDevicePixelRatio(cRetinaFactor());
		pin->userpic = std::move(userpic);
		const auto userpicIndex = pin->index + [self shift];
		[self setNeedsDisplayInRect:PeerRectByIndex(userpicIndex)];
	};
	const auto updateUserpics = [=] {
		ranges::for_each(_pins, singleUserpic);
		*lastDialogsCount = [self shift] + int(std::size(_pins));
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
		_session->downloaderTaskFinished(
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
		const auto peer = pin->peer;
		const auto onlineTimer =
			peerChangedLifetime->make_state<base::Timer>([=] {
				onlineChanges->fire_copy({ peer });
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
			const auto &pin = *it;
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
				const auto index = pin->index;
				Core::Sandbox::Instance().customEnterFromEventLoop([=] {
					_pins[index]->onlineAnimation.start(
						[=] {
							[self setNeedsDisplayInRect:NSMakeRect(
								_pins[index]->x + kCircleDiameter - s,
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
		_pins = ranges::views::zip(
			_session->data().pinnedChatsOrder(nullptr, FilterId()),
			ranges::views::ints(0, ranges::unreachable)
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
			return std::make_unique<Pin>(std::move(pin));
		}) | ranges::to_vector;
		_selfUnpinned = ranges::none_of(peers, &PeerData::isSelf);
		_repliesUnpinned = ranges::none_of(peers, &PeerData::isRepliesChat);

		peerChangedLifetime->destroy();
		for (const auto &pin : _pins) {
			const auto peer = pin->peer;
			const auto index = pin->index;

			_session->changes().peerUpdates(
				peer,
				UpdateFlag::Photo
			) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
				_pins[index]->userpicView = update.peer->createUserpicView();
				listenToDownloaderFinished();
			}, *peerChangedLifetime);

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
				updateBadge(_pins[index]);
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
		_session->data().folderLoaded(ArchiveId)
	) | rpl::then(
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

	const auto localGuard = _lifetime.make_state<base::has_weak_ptr>();

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		crl::on_main(&(*localGuard), [=] {
			updateOnlineColor();
			if (const auto f = _session->data().folderLoaded(ArchiveId)) {
				_archive = ArchiveUserpic(f);
			}
			_savedMessages = SavedMessagesUserpic();
			_repliesMessages = RepliesMessagesUserpic();
			updateUserpics();
		});
	}, _lifetime);

	listenToDownloaderFinished();
	[self processHorizontalReorder];
	return self;
}

- (void)dealloc {
	if (self.image) {
		[self.image release];
	}
	[super dealloc];
}

- (int)shift {
	return (_hasArchive ? 1 : 0) + (_selfUnpinned ? 1 : 0);
}

- (void)gestureHandler:(NSPressGestureRecognizer*)gesture {
	_gestures.fire(std::move(gesture));
}

- (int)indexFromX:(int)position {
	const auto x = position
		- kPinnedButtonsLeftSkip
		+ kPinnedButtonsSpace / 2;
	return x / (kCircleDiameter + kPinnedButtonsSpace) - [self shift];
}

- (void)performAction:(int)xPosition {
	const auto index = [self indexFromX:xPosition];
	const auto peer = (index < 0 || index >= int(std::size(_pins)))
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
	Expects(i < int(std::size(_pins)));
	if (i < 0) {
		if (_hasArchive && (i == -[self shift])) {
			return _archive;
		} else if (_selfUnpinned) {
			return _savedMessages;
		} else if (_repliesUnpinned) {
			return _repliesMessages;
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
	for (auto i = -shift; i < int(std::size(_pins)); i++) {
		if (i >= 0 && _pins[i]->onTop && (indexToTop < 0)) {
			indexToTop = i;
			continue;
		}
		[self drawSinglePin:i rect:dirtyRect];
	}
}

@end // @@implementation PinnedDialogsPanel

#endif // OS_OSX
