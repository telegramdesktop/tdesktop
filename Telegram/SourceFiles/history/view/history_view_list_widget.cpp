/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_list_widget.h"

#include "base/unixtime.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/qt/qt_common_adapters.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/history_item_text.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_emoji_interactions.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_translate_tracker.h"
#include "history/view/history_view_quick_action.h"
#include "chat_helpers/message_field.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/click_handler_types.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "apiwrap.h"
#include "api/api_who_reacted.h"
#include "api/api_views.h"
#include "layout/layout_selection.h"
#include "payments/payments_reaction_process.h"
#include "window/section_widget.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "main/main_session.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/toast/toast.h"
#include "ui/inactive_press.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "boxes/delete_messages_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/peers/edit_participant_box.h"
#include "core/crash_reports.h"
#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_folder.h"
#include "data/data_media_types.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_file_click_handler.h"
#include "data/data_message_reactions.h"
#include "data/data_peer_values.h"
#include "styles/style_chat.h"

#include <QtWidgets/QApplication>
#include <QtCore/QMimeData>

namespace HistoryView {
namespace {

constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadIfLessThanScreens = 2;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;
constexpr auto kClearUserpicsAfter = 50;

[[nodiscard]] std::unique_ptr<TranslateTracker> MaybeTranslateTracker(
		History *history) {
	return history ? std::make_unique<TranslateTracker>(history) : nullptr;
}

} // namespace

const crl::time ListWidget::kItemRevealDuration = crl::time(150);

WindowListDelegate::WindowListDelegate(
	not_null<Window::SessionController*> window)
: _window(window) {
}

not_null<Window::SessionController*> WindowListDelegate::listWindow() {
	return _window;
}

not_null<QWidget*> WindowListDelegate::listEmojiInteractionsParent() {
	return _window->content();
}

not_null<const Ui::ChatStyle*> WindowListDelegate::listChatStyle() {
	return _window->chatStyle();
}

rpl::producer<bool> WindowListDelegate::listChatWideValue() {
	return _window->adaptive().chatWideValue();
}

auto WindowListDelegate::listMakeReactionsManager(
	QWidget *wheelEventsTarget,
	Fn<void(QRect)> update)
-> std::unique_ptr<Reactions::Manager> {
	return std::make_unique<Reactions::Manager>(
		wheelEventsTarget,
		std::move(update));
}

void WindowListDelegate::listVisibleAreaUpdated() {
	_window->floatPlayerAreaUpdated();
}

std::shared_ptr<Ui::Show> WindowListDelegate::listUiShow() {
	return _window->uiShow();
}

void WindowListDelegate::listShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) {
	_window->showPollResults(poll, context);
}

void WindowListDelegate::listCancelUploadLayer(not_null<HistoryItem*> item) {
	_window->cancelUploadLayer(item);
}

bool WindowListDelegate::listAnimationsPaused() {
	return _window->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

auto WindowListDelegate::listSendingAnimation()
-> Ui::MessageSendingAnimationController * {
	return &_window->sendingAnimation();
}

Ui::ChatPaintContext WindowListDelegate::listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) {
	return _window->preparePaintContext(std::move(args));
}

bool WindowListDelegate::listMarkingContentRead() {
	return _window->widget()->markingAsRead();
}

bool WindowListDelegate::listIgnorePaintEvent(QWidget *w, QPaintEvent *e) {
	return _window->contentOverlapped(w, e);
}

bool WindowListDelegate::listShowReactPremiumError(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	return Window::ShowReactPremiumError(_window, item, id);
}

void WindowListDelegate::listWindowSetInnerFocus() {
	_window->widget()->setInnerFocus();
}

bool WindowListDelegate::listAllowsDragForward() {
	return _window->adaptive().isOneColumn();
}

void WindowListDelegate::listLaunchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> finished) {
	_window->widget()->launchDrag(std::move(data), std::move(finished));
}

ListWidget::MouseState::MouseState() : pointState(PointState::Outside) {
}

ListWidget::MouseState::MouseState(
	FullMsgId itemId,
	int height,
	QPoint point,
	PointState pointState)
: itemId(itemId)
, height(height)
, point(point)
, pointState(pointState) {
}

template <ListWidget::EnumItemsDirection direction, typename Method>
void ListWidget::enumerateItems(Method method) {
	constexpr auto TopToBottom = (direction == EnumItemsDirection::TopToBottom);

	// No displayed messages in this history.
	if (_items.empty()) {
		return;
	}
	if (_visibleBottom <= _itemsTop || _itemsTop + _itemsHeight <= _visibleTop) {
		return;
	}

	const auto beginning = begin(_items);
	const auto ending = end(_items);
	auto from = TopToBottom
		? std::lower_bound(
			beginning,
			ending,
			_visibleTop,
			[this](auto &elem, int top) {
				return this->itemTop(elem) + elem->height() <= top;
			})
		: std::upper_bound(
			beginning,
			ending,
			_visibleBottom,
			[this](int bottom, auto &elem) {
				return this->itemTop(elem) + elem->height() >= bottom;
			});
	auto wasEnd = (from == ending);
	if (wasEnd) {
		--from;
	}
	if (TopToBottom) {
		Assert(itemTop(from->get()) + from->get()->height() > _visibleTop);
	} else {
		if (itemTop(from->get()) >= _visibleBottom) {
			setGeometryCrashAnnotations(*from);
			Unexpected("itemTop(from->get()) >= _visibleBottom");
		}
	}

	while (true) {
		auto view = from->get();
		auto itemtop = itemTop(view);
		auto itembottom = itemtop + view->height();

		// Binary search should've skipped all the items that are above / below the visible area.
		if (TopToBottom) {
			Assert(itembottom > _visibleTop);
		} else {
			if (itemtop >= _visibleBottom) {
				setGeometryCrashAnnotations(view);
				Unexpected("itemtop >= _visibleBottom");
			}
		}

		if (!method(view, itemtop, itembottom)) {
			return;
		}

		// Skip all the items that are below / above the visible area.
		if (TopToBottom) {
			if (itembottom >= _visibleBottom) {
				return;
			}
		} else {
			if (itemtop <= _visibleTop) {
				return;
			}
		}

		if (TopToBottom) {
			if (++from == ending) {
				break;
			}
		} else {
			if (from == beginning) {
				break;
			}
			--from;
		}
	}
}

template <typename Method>
void ListWidget::enumerateUserpics(Method method) {
	// Find and remember the top of an attached messages pack
	// -1 means we didn't find an attached to next message yet.
	int lowestAttachedItemTop = -1;

	auto userpicCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		// Skip all service messages.
		if (view->data()->isService()) {
			return true;
		}

		if (lowestAttachedItemTop < 0 && view->isAttachedToNext()) {
			lowestAttachedItemTop = itemtop + view->marginTop();
		}

		// Call method on a userpic for all messages that have it and for those who are not showing it
		// because of their attachment to the next message if they are bottom-most visible.
		if (view->displayFromPhoto() || (view->hasFromPhoto() && itembottom >= _visibleBottom)) {
			if (lowestAttachedItemTop < 0) {
				lowestAttachedItemTop = itemtop + view->marginTop();
			}
			// Attach userpic to the bottom of the visible area with the same margin as the last message.
			auto userpicMinBottomSkip = st::historyPaddingBottom + st::msgMargin.bottom();
			auto userpicBottom = qMin(itembottom - view->marginBottom(), _visibleBottom - userpicMinBottomSkip);

			// Do not let the userpic go above the attached messages pack top line.
			userpicBottom = qMax(userpicBottom, lowestAttachedItemTop + st::msgPhotoSize);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, userpicBottom - st::msgPhotoSize)) {
				return false;
			}
		}

		// Forget the found top of the pack, search for the next one from scratch.
		if (!view->isAttachedToNext()) {
			lowestAttachedItemTop = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::TopToBottom>(userpicCallback);
}

template <typename Method>
void ListWidget::enumerateDates(Method method) {
	// Find and remember the bottom of an single-day messages pack
	// -1 means we didn't find a same-day with previous message yet.
	auto lowestInOneDayItemBottom = -1;

	auto dateCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		const auto item = view->data();
		if (lowestInOneDayItemBottom < 0 && view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = itembottom - view->marginBottom();
		}

		// Call method on a date for all messages that have it and for those who are not showing it
		// because they are in a one day together with the previous message if they are top-most visible.
		if (view->displayDate() || (!item->isEmpty() && itemtop <= _visibleTop)) {
			if (lowestInOneDayItemBottom < 0) {
				lowestInOneDayItemBottom = itembottom - view->marginBottom();
			}
			// Attach date to the top of the visible area with the same margin as it has in service message.
			auto dateTop = qMax(itemtop, _visibleTop) + st::msgServiceMargin.top();

			// Do not let the date go below the single-day messages pack bottom line.
			auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			dateTop = qMin(dateTop, lowestInOneDayItemBottom - dateHeight);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, itemtop, dateTop)) {
				return false;
			}
		}

		// Forget the found bottom of the pack, search for the next one from scratch.
		if (!view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::BottomToTop>(dateCallback);
}

ListWidget::ListWidget(
	QWidget *parent,
	not_null<Main::Session*> session,
	not_null<ListDelegate*> delegate)
: RpWidget(parent)
, _delegate(delegate)
, _session(session)
, _emojiInteractions(std::make_unique<EmojiInteractions>(
	this,
	_delegate->listEmojiInteractionsParent(),
	session,
	[=](not_null<const Element*> view) { return itemTop(view); }))
, _context(_delegate->listContext())
, _itemAverageHeight(itemMinimalHeight())
, _pathGradient(
	MakePathShiftGradient(
		_delegate->listChatStyle(),
		[=] { update(); }))
, _reactionsManager(_delegate->listMakeReactionsManager(
	this,
	[=](QRect updated) { update(updated); }))
, _translateTracker(MaybeTranslateTracker(_delegate->listTranslateHistory()))
, _scrollDateCheck([this] { scrollDateCheck(); })
, _applyUpdatedScrollState([this] { applyUpdatedScrollState(); })
, _selectEnabled(_delegate->listAllowsMultiSelect())
, _highlighter(
	&_session->data(),
	[=](const HistoryItem *item) { return viewForItem(item); },
	[=](const Element *view) { repaintItem(view); })
, _touchSelectTimer([=] { onTouchSelect(); })
, _touchScrollTimer([=] { onTouchScrollTimer(); }) {
	setAttribute(Qt::WA_AcceptTouchEvents);
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([this] { scrollDateHideByTimer(); });
	_session->data().viewRepaintRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			repaintItem(view);
		}
	}, lifetime());
	_session->data().viewResizeRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			resizeItem(view);
		}
	}, lifetime());
	_session->data().itemViewRefreshRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			refreshItem(view);
		}
	}, lifetime());
	_session->data().viewLayoutChanged(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			if (view->isUnderCursor()) {
				mouseActionUpdate();
			}
		}
	}, lifetime());
	_session->data().itemDataChanges(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		if (const auto view = viewForItem(item)) {
			view->itemDataChanged();
		}
	}, lifetime());

	_session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	_session->data().peerDecorationsUpdated(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	_session->data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		itemRemoved(item);
	}, lifetime());

	using MessageUpdateFlag = Data::MessageUpdate::Flag;
	_session->changes().realtimeMessageUpdates(
		MessageUpdateFlag::NewUnreadReaction
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		maybeMarkReactionsRead(update.item);
	}, lifetime());

	if (const auto history = _delegate->listTranslateHistory()) {
		_session->changes().historyUpdates(
			history,
			Data::HistoryUpdate::Flag::TranslatedTo
		) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	}

	_session->data().itemVisibilityQueries(
	) | rpl::start_with_next([=](
			const Data::Session::ItemVisibilityQuery &query) {
		if (const auto view = viewForItem(query.item)) {
			const auto top = itemTop(view);
			if (top >= 0
				&& top + view->height() > _visibleTop
				&& top < _visibleBottom) {
				*query.isVisible = true;
			}
		}
	}, lifetime());

	if (_reactionsManager) {
		_reactionsManager->chosen(
		) | rpl::start_with_next([=](ChosenReaction reaction) {
			_reactionsManager->updateButton({});
			reactionChosen(reaction);
		}, lifetime());

		Reactions::SetupManagerList(
			_reactionsManager.get(),
			_reactionsItem.value());

		Core::App().settings().cornerReactionValue(
		) | rpl::start_with_next([=](bool value) {
			_useCornerReaction = value;
			if (!value) {
				_reactionsManager->updateButton({});
			}
		}, lifetime());
	}

	_delegate->listChatWideValue(
	) | rpl::start_with_next([=](bool wide) {
		_isChatWide = wide;
	}, lifetime());

	_selectScroll.scrolls(
	) | rpl::start_with_next([=](int d) {
		delegate->listScrollTo(_visibleTop + d, false);
	}, lifetime());
}

Main::Session &ListWidget::session() const {
	return *_session;
}

not_null<Window::SessionController*> ListWidget::controller() const {
	return _delegate->listWindow();
}

not_null<ListDelegate*> ListWidget::delegate() const {
	return _delegate;
}

void ListWidget::refreshViewer() {
	_viewerLifetime.destroy();
	_refreshingViewer = true;
	_delegate->listSource(
		_aroundPosition,
		_idsLimit,
		_idsLimit
	) | rpl::start_with_next([=](Data::MessagesSlice &&slice) {
		_refreshingViewer = false;
		std::swap(_slice, slice);
		refreshRows(slice);
	}, _viewerLifetime);
}

void ListWidget::setGeometryCrashAnnotations(not_null<Element*> view) {
	CrashReports::SetAnnotation(
		"Geometry",
		u"size: %1x%2, visibleTop: %3, visibleBottom: %4, top: %5"_q
		.arg(width())
		.arg(height())
		.arg(_visibleTop)
		.arg(_visibleBottom)
		.arg(_itemsTop));
	const auto logItems = [&] {
		auto items = QStringList();
		auto top = _itemsTop;
		auto index = 0;
		for (const auto &some : _items) {
			items.push_back(u"(%1)%2=%3,%4,%5"_q
				.arg(index++)
				.arg(top)
				.arg(itemTop(some))
				.arg(some->y())
				.arg(some->height()));
			top += some->height();
		}
		return items.join(';');
	};
	CrashReports::SetAnnotation("Chosen", u"%1,%2,%3"_q
		.arg(itemTop(view))
		.arg(view->y())
		.arg(view->height()));
	CrashReports::SetAnnotation("Before", logItems());
	updateSize();
	CrashReports::SetAnnotation("After", logItems());
}

void ListWidget::refreshRows(const Data::MessagesSlice &old) {
	Expects(_viewsCapacity.empty());

	saveScrollState();

	const auto addedToEndFrom = (old.skippedAfter == 0
		&& (_slice.skippedAfter == 0)
		&& !old.ids.empty())
		? ranges::find(_slice.ids, old.ids.back())
		: end(_slice.ids);
	const auto addedToEndCount = std::max(
		int(end(_slice.ids) - addedToEndFrom),
		1
	) - 1;

	auto destroyingBarElement = _bar.element;
	auto clearingOverElement = _overElement;
	_itemsKnownTillEnd = (_slice.skippedAfter == 0);
	_resizePending = true;
	_items.clear();
	_items.reserve(_slice.ids.size());
	std::swap(_views, _viewsCapacity);
	auto nearestIndex = -1;
	for (const auto &fullId : _slice.ids) {
		if (const auto item = session().data().message(fullId)) {
			if (_slice.nearestToAround == fullId) {
				nearestIndex = int(_items.size());
			}
			const auto view = enforceViewForItem(item, _viewsCapacity);
			_items.push_back(view);
			if (destroyingBarElement == view) {
				destroyingBarElement = nullptr;
			}
			if (clearingOverElement == view) {
				clearingOverElement = nullptr;
			}
		}
	}
	if (_translateTracker) {
		_translateTracker->addBunchFrom(_items);
	}
	for (auto e = end(_items), i = e - addedToEndCount; i != e; ++i) {
		_itemRevealPending.emplace(*i);
	}
	updateAroundPositionFromNearest(nearestIndex);

	updateItemsGeometry();

	if (clearingOverElement) {
		_overElement = nullptr;
	}
	if (destroyingBarElement) {
		destroyingBarElement->destroyUnreadBar();
		_bar = {};
	}

	for (const auto &[item, view] : _viewsCapacity) {
		if (const auto raw = view.get()) {
			viewReplaced(raw, nullptr);
		}
	}
	_viewsCapacity.clear();

	checkUnreadBarCreation();
	restoreScrollState();
	if (!_itemsRevealHeight) {
		mouseActionUpdate(QCursor::pos());
	}
	if (_emptyInfo) {
		_emptyInfo->setVisible(isEmpty());
	}
	checkActivation();
}

std::optional<int> ListWidget::scrollTopForPosition(
		Data::MessagePosition position) const {
	auto messageUnknown = !position.date && position.fullId;
	if (messageUnknown) {
		if (const auto item = session().data().message(position.fullId)) {
			position = item->position();
			messageUnknown = false;
		}
	}
	if (position == Data::UnreadMessagePosition) {
		if (_bar.element && !_bar.hidden && _bar.focus) {
			const auto shift = st::lineWidth + st::historyUnreadBarMargin;
			return itemTop(_bar.element) + shift;
		}
		position = Data::MaxMessagePosition;
	}
	if (_visibleTop >= _visibleBottom) {
		return std::nullopt;
	} else if (position == Data::MaxMessagePosition) {
		if (loadedAtBottom()) {
			return height() - (_visibleBottom - _visibleTop);
		}
		return std::nullopt;
	} else if (!_items.empty()
		&& (_aroundPosition == position
			|| _initialAroundPosition == position)
		&& messageUnknown) {
		if (_refreshingViewer) {
			return std::nullopt;
		}
		const auto available = _visibleBottom - _visibleTop;
		return std::max((height() / 2) - available / 2, 0);
	} else if (_items.empty()
		|| isBelowPosition(position)
		|| isAbovePosition(position)) {
		return std::nullopt;
	}
	const auto index = findNearestItem(position);
	const auto view = _items[index];
	return scrollTopForView(view);
}

std::optional<int> ListWidget::scrollTopForView(
		not_null<Element*> view) const {
	if (view->isHiddenByGroup()) {
		if (const auto group = session().data().groups().find(view->data())) {
			if (const auto leader = viewForItem(group->items.front())) {
				if (!leader->isHiddenByGroup()) {
					return scrollTopForView(leader);
				}
			}
		}
	}
	const auto top = view->y();
	const auto height = view->height();
	const auto available = _visibleBottom - _visibleTop;
	return top - std::max((available - height) / 2, 0);
}

void ListWidget::scrollTo(
		int scrollTop,
		Data::MessagePosition attachPosition,
		int delta,
		AnimatedScroll type) {
	_scrollToAnimation.stop();
	if (!delta || _items.empty() || type == AnimatedScroll::None) {
		_delegate->listScrollTo(scrollTop);
		return;
	}
	const auto transition = (type == AnimatedScroll::Full)
		? anim::sineInOut
		: anim::easeOutCubic;
	if (delta > 0 && scrollTop == height() - (_visibleBottom - _visibleTop)) {
		// Animated scroll to bottom.
		_scrollToAnimation.start(
			[=] { scrollToAnimationCallback(FullMsgId(), 0); },
			-delta,
			0,
			st::slideDuration,
			transition);
		return;
	}
	const auto index = findNearestItem(attachPosition);
	Assert(index >= 0 && index < int(_items.size()));
	const auto attachTo = _items[index];
	const auto attachToId = attachTo->data()->fullId();
	const auto initial = scrollTop - delta;
	_delegate->listScrollTo(initial);

	const auto attachToTop = itemTop(attachTo);
	const auto relativeStart = initial - attachToTop;
	const auto relativeFinish = scrollTop - attachToTop;
	_scrollToAnimation.start(
		[=] { scrollToAnimationCallback(attachToId, relativeFinish); },
		relativeStart,
		relativeFinish,
		st::slideDuration,
		transition);
}

bool ListWidget::animatedScrolling() const {
	return _scrollToAnimation.animating();
}

void ListWidget::scrollToAnimationCallback(
		FullMsgId attachToId,
		int relativeTo) {
	if (!attachToId) {
		// Animated scroll to bottom.
		const auto current = int(base::SafeRound(
			_scrollToAnimation.value(0)));
		_delegate->listScrollTo(height()
			- (_visibleBottom - _visibleTop)
			+ current);
		return;
	}
	const auto attachTo = session().data().message(attachToId);
	const auto attachToView = viewForItem(attachTo);
	if (!attachToView) {
		_scrollToAnimation.stop();
	} else {
		const auto current = int(base::SafeRound(_scrollToAnimation.value(
			relativeTo)));
		_delegate->listScrollTo(itemTop(attachToView) + current);
	}
}

bool ListWidget::isAbovePosition(Data::MessagePosition position) const {
	if (_items.empty() || loadedAtBottom()) {
		return false;
	}
	return _items.back()->data()->position() < position;
}

bool ListWidget::isBelowPosition(Data::MessagePosition position) const {
	if (_items.empty() || loadedAtTop()) {
		return false;
	}
	return _items.front()->data()->position() > position;
}

void ListWidget::highlightMessage(
		FullMsgId itemId,
		const TextWithEntities &part,
		int partOffsetHint) {
	if (const auto view = viewForItem(itemId)) {
		_highlighter.highlight({ view->data(), part, partOffsetHint });
	}
}

void ListWidget::showAroundPosition(
		Data::MessagePosition position,
		Fn<bool()> overrideInitialScroll) {
	_aroundPosition = position;
	_aroundIndex = -1;
	_overrideInitialScroll = std::move(overrideInitialScroll);
	refreshViewer();
}

bool ListWidget::jumpToBottomInsteadOfUnread() const {
	// If we want to jump to unread, but we're at the unread already,
	// then jump to the end of the list.
	//
	// That means there is no read inbox messages below us.
	const auto firstReadMessage = [&]() -> Element* {
		for (const auto &view : ranges::views::reverse(_items)) {
			const auto item = view->data();
			if (item->isRegular()
				&& (item->out()
					|| !_delegate->listElementShownUnread(view))) {
				return view;
			}
		}
		return nullptr;
	}();
	return !firstReadMessage || (itemTop(firstReadMessage) < _visibleBottom);
}

void ListWidget::showAtPosition(
		Data::MessagePosition position,
		const Window::SectionShow &params,
		Fn<void(bool found)> done) {
	const auto showAtUnread = (position == Data::UnreadMessagePosition);

	if (showAtUnread && jumpToBottomInsteadOfUnread()) {
		showAtPosition(Data::MaxMessagePosition, params, std::move(done));
		return;
	}

	if (position.fullId.peer && position.fullId.msg) {
		if (const auto item = session().data().message(position.fullId)) {
			position = item->position();
		}
	}

	if (showAtUnread) {
		showAroundPosition(position, [=] {
			if (_bar.element) {
				_bar.element->destroyUnreadBar();
				const auto i = ranges::find(_items, not_null{ _bar.element });
				Assert(i != end(_items));
				refreshAttachmentsAtIndex(i - begin(_items));
				_bar = {};
			}
			checkUnreadBarCreation();
			return showAtPositionNow(position, params, done);
		});
	} else if (!showAtPositionNow(position, params, done)) {
		showAroundPosition(position, [=] {
			return showAtPositionNow(position, params, done);
		});
	}
}

bool ListWidget::showAtPositionNow(
		Data::MessagePosition position,
		const Window::SectionShow &params,
		Fn<void(bool found)> done) {
	if (const auto scrollTop = scrollTopForPosition(position)) {
		computeScrollTo(*scrollTop, position, params.animated);
		if (position != Data::MaxMessagePosition
			&& position != Data::UnreadMessagePosition) {
			highlightMessage(
				position.fullId,
				params.highlightPart,
				params.highlightPartOffsetHint);
		}
		if (done) {
			const auto found = !position.fullId.peer
				|| !IsServerMsgId(position.fullId.msg)
				|| viewForItem(position.fullId);
			done(found);
		}
		return true;
	}
	return false;
}

void ListWidget::computeScrollTo(
		int to,
		Data::MessagePosition position,
		anim::type animated) {
	const auto currentScrollHeight = (_visibleBottom - _visibleTop);
	const auto currentScrollTop = _visibleTop;
	const auto wanted = std::max(
		std::min(to, height() - currentScrollHeight),
		0);
	const auto fullDelta = (wanted - currentScrollTop);
	const auto limit = currentScrollHeight;
	const auto scrollDelta = std::clamp(fullDelta, -limit, limit);
	const auto type = (animated == anim::type::instant)
		? AnimatedScroll::None
		: (std::abs(fullDelta) > limit)
		? AnimatedScroll::Part
		: AnimatedScroll::Full;
	scrollTo(wanted, position, scrollDelta, type);
}

void ListWidget::checkUnreadBarCreation() {
	if (!_bar.element) {
		if (auto data = _delegate->listMessagesBar(_items); data.bar.element) {
			_bar = std::move(data.bar);
			_barText = std::move(data.text);
			if (!_bar.hidden) {
				_bar.element->createUnreadBar(_barText.value());
				const auto i = ranges::find(_items, not_null{ _bar.element });
				Assert(i != end(_items));
				refreshAttachmentsAtIndex(i - begin(_items));
			}
		}
	}
}

void ListWidget::saveScrollState() {
	if (!_scrollTopState.item) {
		_scrollTopState = countScrollState();
	}
}

void ListWidget::restoreScrollState() {
	if (_items.empty()) {
		return;
	} else if (_overrideInitialScroll
		&& base::take(_overrideInitialScroll)()) {
		_scrollTopState = ScrollTopState();
		_scrollInited = true;
		return;
	}
	if (!_scrollTopState.item) {
		if (!_bar.element || _bar.hidden || !_bar.focus || _scrollInited) {
			return;
		}
		_scrollInited = true;
		_scrollTopState.item = _bar.element->data()->position();
		_scrollTopState.shift = st::lineWidth
			+ st::historyUnreadBarMargin
			+ _bar.element->displayedDateHeight();
	}
	const auto index = findNearestItem(_scrollTopState.item);
	if (index >= 0) {
		const auto view = _items[index];
		auto newVisibleTop = itemTop(view) + _scrollTopState.shift;
		if (_visibleTop != newVisibleTop) {
			_delegate->listScrollTo(newVisibleTop);
		}
	}
	_scrollTopState = ScrollTopState();
}

Element *ListWidget::viewForItem(FullMsgId itemId) const {
	if (const auto item = session().data().message(itemId)) {
		return viewForItem(item);
	}
	return nullptr;
}

Element *ListWidget::viewForItem(const HistoryItem *item) const {
	if (item) {
		if (const auto i = _views.find(item); i != _views.end()) {
			return i->second.get();
		}
	}
	return nullptr;
}

not_null<Element*> ListWidget::enforceViewForItem(
		not_null<HistoryItem*> item,
		ViewsMap &old) {
	if (const auto i = old.find(item); i != end(old)) {
		if (i->second) {
			return _views.emplace(
				item,
				base::take(i->second)).first->second.get();
		} else if (const auto j = _views.find(item); j != end(_views)) {
			return j->second.get();
		}
	}
	const auto &[i, ok] = _views.emplace(
		item,
		item->createView(this));
	return i->second.get();
}

void ListWidget::updateAroundPositionFromNearest(int nearestIndex) {
	if (nearestIndex < 0) {
		_aroundIndex = -1;
		return;
	}
	const auto isGoodIndex = [&](int index) {
		Expects(index >= 0 && index < _items.size());

		return _delegate->listIsGoodForAroundPosition(_items[index]);
	};
	_aroundIndex = [&] {
		for (auto index = nearestIndex; index < _items.size(); ++index) {
			if (isGoodIndex(index)) {
				return index;
			}
		}
		for (auto index = nearestIndex; index != 0;) {
			if (isGoodIndex(--index)) {
				return index;
			}
		}
		return -1;
	}();
	if (_aroundIndex < 0) {
		return;
	}
	const auto newPosition = _items[_aroundIndex]->data()->position();
	if (_aroundPosition != newPosition) {
		_initialAroundPosition = _aroundPosition;
		_aroundPosition = newPosition;
		crl::on_main(this, [=] { refreshViewer(); });
	}
}

Element *ListWidget::viewByPosition(Data::MessagePosition position) const {
	const auto index = findNearestItem(position);
	const auto result = (index < 0) ? nullptr : _items[index].get();
	return (position == Data::MinMessagePosition
		|| position == Data::MaxMessagePosition
		|| (result && result->data()->position() == position))
		? result
		: nullptr;
}

int ListWidget::findNearestItem(Data::MessagePosition position) const {
	if (_items.empty()) {
		return -1;
	}
	const auto after = ranges::find_if(
		_items,
		[&](not_null<Element*> view) {
			return (view->data()->position() >= position);
		});
	return (after == end(_items))
		? int(_items.size() - 1)
		: int(after - begin(_items));
}

HistoryItemsList ListWidget::collectVisibleItems() const {
	auto result = HistoryItemsList();
	const auto from = std::lower_bound(
		begin(_items),
		end(_items),
		_visibleTop,
		[this](auto &elem, int top) {
			return this->itemTop(elem) + elem->height() <= top;
		});
	const auto to = std::lower_bound(
		begin(_items),
		end(_items),
		_visibleBottom,
		[this](auto &elem, int bottom) {
			return this->itemTop(elem) < bottom;
		});
	result.reserve(to - from);
	for (auto i = from; i != to; ++i) {
		result.push_back((*i)->data());
	}
	return result;
}

void ListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	if (!(visibleTop < visibleBottom)) {
		return;
	}

	const auto initializing = !(_visibleTop < _visibleBottom);
	const auto scrolledUp = (visibleTop < _visibleTop);
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	// Unload userpics.
	if (_userpics.size() > kClearUserpicsAfter) {
		_userpicsCache = std::move(_userpics);
	}

	if (initializing) {
		checkUnreadBarCreation();
	}
	updateVisibleTopItem();
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		scrollDateHideByTimer();
	}
	_delegate->listVisibleAreaUpdated();
	session().data().itemVisibilitiesUpdated();
	_applyUpdatedScrollState.call();

	_emojiInteractions->visibleAreaUpdated(_visibleTop, _visibleBottom);
}

void ListWidget::applyUpdatedScrollState() {
	checkMoveToOtherViewer();
}

void ListWidget::updateVisibleTopItem() {
	if (_itemsKnownTillEnd && _visibleBottom == height()) {
		_visibleTopItem = nullptr;
	} else if (_items.empty()) {
		_visibleTopItem = nullptr;
		_visibleTopFromItem = _visibleTop;
	} else {
		_visibleTopItem = findItemByY(_visibleTop);
		_visibleTopFromItem = _visibleTop - itemTop(_visibleTopItem);
	}
}

bool ListWidget::displayScrollDate() const {
	return (_visibleTop <= height() - 2 * (_visibleBottom - _visibleTop));
}

void ListWidget::scrollDateCheck() {
	if (!_visibleTopItem) {
		_scrollDateLastItem = nullptr;
		_scrollDateLastItemTop = 0;
		scrollDateHide();
	} else if (_visibleTopItem != _scrollDateLastItem || _visibleTopFromItem != _scrollDateLastItemTop) {
		// Show scroll date only if it is not the initial onScroll() event (with empty _scrollDateLastItem).
		if (_scrollDateLastItem && !_scrollDateShown) {
			toggleScrollDateShown();
		}
		_scrollDateLastItem = _visibleTopItem;
		_scrollDateLastItemTop = _visibleTopFromItem;
		_scrollDateHideTimer.callOnce(st::historyScrollDateHideTimeout);
	}
}

void ListWidget::scrollDateHideByTimer() {
	_scrollDateHideTimer.cancel();
	if (!_scrollDateLink || ClickHandler::getPressed() != _scrollDateLink) {
		scrollDateHide();
	}
}

void ListWidget::scrollDateHide() {
	if (_scrollDateShown) {
		toggleScrollDateShown();
	}
}

void ListWidget::keepScrollDateForNow() {
	if (!_scrollDateShown
		&& _scrollDateLastItem
		&& _scrollDateOpacity.animating()) {
		toggleScrollDateShown();
	}
	_scrollDateHideTimer.callOnce(st::historyScrollDateHideTimeout);
}

void ListWidget::toggleScrollDateShown() {
	_scrollDateShown = !_scrollDateShown;
	auto from = _scrollDateShown ? 0. : 1.;
	auto to = _scrollDateShown ? 1. : 0.;
	_scrollDateOpacity.start([this] { repaintScrollDateCallback(); }, from, to, st::historyDateFadeDuration);
}

void ListWidget::repaintScrollDateCallback() {
	auto updateTop = _visibleTop;
	auto updateHeight = st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	update(0, updateTop, width(), updateHeight);
}

auto ListWidget::collectSelectedItems() const -> SelectedItems {
	auto transformation = [&](const auto &item) {
		const auto &[itemId, selection] = item;
		auto result = SelectedItem(itemId);
		result.canDelete = selection.canDelete;
		result.canForward = selection.canForward;
		result.canSendNow = selection.canSendNow;
		return result;
	};
	auto items = SelectedItems();
	if (hasSelectedItems()) {
		items.reserve(_selected.size());
		std::transform(
			_selected.begin(),
			_selected.end(),
			std::back_inserter(items),
			transformation);
	}
	return items;
}

MessageIdsList ListWidget::collectSelectedIds() const {
	const auto selected = collectSelectedItems();
	return ranges::views::all(
		selected
	) | ranges::views::transform([](const SelectedItem &item) {
		return item.msgId;
	}) | ranges::to_vector;
}

void ListWidget::pushSelectedItems() {
	_delegate->listSelectionChanged(collectSelectedItems());
}

void ListWidget::removeItemSelection(
		const SelectedMap::const_iterator &i) {
	Expects(i != _selected.cend());

	_selected.erase(i);
	if (_selected.empty()) {
		update();
	}
	pushSelectedItems();
}

bool ListWidget::hasSelectedText() const {
	return (_selectedTextItem != nullptr) && !hasSelectedItems();
}

bool ListWidget::hasSelectedItems() const {
	return !_selected.empty();
}

SelectionModeResult ListWidget::inSelectionMode() const {
	const auto now = hasSelectedItems()
		|| !_dragSelected.empty()
		|| (_mouseAction == MouseAction::Selecting && _lastInSelectionMode);
	if (_lastInSelectionMode != now) {
		_lastInSelectionMode = now;
		if (_inSelectionModeAnimation.animating()) {
			const auto progress = !now
				? _inSelectionModeAnimation.value(0.)
				: 1. - _inSelectionModeAnimation.value(0.);
			_inSelectionModeAnimation.change(
				now ? 1. : 0.,
				st::universalDuration * (1. - progress));
		} else {
			_inSelectionModeAnimation.stop();
			_inSelectionModeAnimation.start(
				[this] {
					const_cast<ListWidget*>(this)->update(
						QRect(
							0,
							_visibleTop,
							width(),
							_visibleBottom - _visibleTop));
				},
				now ? 0. : 1.,
				now ? 1. : 0.,
				st::universalDuration);
		}
	}
	return { now, _inSelectionModeAnimation.value(now ? 1. : 0.) };
}

bool ListWidget::overSelectedItems() const {
	if (_overState.pointState == PointState::GroupPart) {
		return _overItemExact
			&& _selected.contains(_overItemExact->fullId());
	} else if (_overState.pointState == PointState::Inside) {
		return _overElement
			&& isSelectedAsGroup(_selected, _overElement->data());
	}
	return false;
}

bool ListWidget::isSelectedGroup(
		const SelectedMap &applyTo,
		not_null<const Data::Group*> group) const {
	for (const auto &other : group->items) {
		if (!applyTo.contains(other->fullId())) {
			return false;
		}
	}
	return true;
}

bool ListWidget::isSelectedAsGroup(
		const SelectedMap &applyTo,
		not_null<HistoryItem*> item) const {
	if (const auto group = session().data().groups().find(item)) {
		return isSelectedGroup(applyTo, group);
	}
	return applyTo.contains(item->fullId());
}

bool ListWidget::isGoodForSelection(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item,
		int &totalCount) const {
	if (!_delegate->listIsItemGoodForSelection(item)) {
		return false;
	} else if (!applyTo.contains(item->fullId())) {
		++totalCount;
	}
	return (totalCount <= MaxSelectedItems);
}

bool ListWidget::addToSelection(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item) const {
	const auto itemId = item->fullId();
	auto [iterator, ok] = applyTo.try_emplace(
		itemId,
		SelectionData());
	if (!ok) {
		return false;
	}
	iterator->second.canDelete = item->canDelete();
	iterator->second.canForward = item->allowsForward();
	iterator->second.canSendNow = item->allowsSendNow();
	return true;
}

bool ListWidget::removeFromSelection(
		SelectedMap &applyTo,
		FullMsgId itemId) const {
	return applyTo.remove(itemId);
}

void ListWidget::changeSelection(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item,
		SelectAction action) const {
	const auto itemId = item->fullId();
	if (action == SelectAction::Invert) {
		action = applyTo.contains(itemId)
			? SelectAction::Deselect
			: SelectAction::Select;
	}
	if (action == SelectAction::Select) {
		auto already = int(applyTo.size());
		if (isGoodForSelection(applyTo, item, already)) {
			addToSelection(applyTo, item);
		}
	} else {
		removeFromSelection(applyTo, itemId);
	}
}

void ListWidget::changeSelectionAsGroup(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item,
		SelectAction action) const {
	const auto group = session().data().groups().find(item);
	if (!group) {
		return changeSelection(applyTo, item, action);
	}
	if (action == SelectAction::Invert) {
		action = isSelectedAsGroup(applyTo, item)
			? SelectAction::Deselect
			: SelectAction::Select;
	}
	auto already = int(applyTo.size());
	const auto canSelect = [&] {
		for (const auto &other : group->items) {
			if (!isGoodForSelection(applyTo, other, already)) {
				return false;
			}
		}
		return true;
	}();
	if (action == SelectAction::Select && canSelect) {
		for (const auto &other : group->items) {
			addToSelection(applyTo, other);
		}
	} else {
		for (const auto &other : group->items) {
			removeFromSelection(applyTo, other->fullId());
		}
	}
}

bool ListWidget::isItemUnderPressSelected() const {
	return itemUnderPressSelection() != _selected.end();
}

auto ListWidget::itemUnderPressSelection() -> SelectedMap::iterator {
	return (_pressState.itemId
		&& _pressState.pointState != PointState::Outside)
		? _selected.find(_pressState.itemId)
		: _selected.end();
}

bool ListWidget::isInsideSelection(
		not_null<const Element*> view,
		not_null<HistoryItem*> exactItem,
		const MouseState &state) const {
	if (!_selected.empty()) {
		if (state.pointState == PointState::GroupPart) {
			return _selected.contains(exactItem->fullId());
		} else {
			return isSelectedAsGroup(_selected, view->data());
		}
	} else if (_selectedTextItem
		&& _selectedTextItem == view->data()
		&& state.pointState != PointState::Outside) {
		StateRequest stateRequest;
		stateRequest.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
		const auto dragState = view->textState(
			state.point,
			stateRequest);
		if (dragState.cursor == CursorState::Text
			&& base::in_range(
				dragState.symbol,
				_selectedTextRange.from,
				_selectedTextRange.to)) {
			return true;
		}
	}
	return false;
}

auto ListWidget::itemUnderPressSelection() const
-> SelectedMap::const_iterator {
	return (_pressState.itemId
		&& _pressState.pointState != PointState::Outside)
		? _selected.find(_pressState.itemId)
		: _selected.end();
}

bool ListWidget::requiredToStartDragging(not_null<Element*> view) const {
	return (_mouseCursorState == CursorState::Date);
}

bool ListWidget::isPressInSelectedText(TextState state) const {
	if (state.cursor != CursorState::Text) {
		return false;
	}
	if (!hasSelectedText()
		|| !_selectedTextItem
		|| _selectedTextItem->fullId() != _pressState.itemId) {
		return false;
	}
	auto from = _selectedTextRange.from;
	auto to = _selectedTextRange.to;
	return (state.symbol >= from && state.symbol < to);
}

void ListWidget::cancelSelection() {
	clearSelected();
	clearTextSelection();
}

void ListWidget::selectItem(not_null<HistoryItem*> item) {
	if (hasSelectRestriction()) {
		return;
	} else if (const auto view = viewForItem(item)) {
		clearTextSelection();
		changeSelection(
			_selected,
			item,
			SelectAction::Select);
		pushSelectedItems();
	}
}

void ListWidget::selectItemAsGroup(not_null<HistoryItem*> item) {
	if (hasSelectRestriction()) {
		return;
	} else if (const auto view = viewForItem(item)) {
		clearTextSelection();
		changeSelectionAsGroup(
			_selected,
			item,
			SelectAction::Select);
		pushSelectedItems();
		update();
	}
}

void ListWidget::clearSelected() {
	if (_selected.empty()) {
		return;
	}
	if (hasSelectedText()) {
		repaintItem(_selected.begin()->first);
		_selected.clear();
	} else {
		_selected.clear();
		pushSelectedItems();
		update();
	}
}

void ListWidget::clearTextSelection() {
	if (_selectedTextItem) {
		if (const auto view = viewForItem(_selectedTextItem)) {
			repaintItem(view);
		}
		_selectedTextItem = nullptr;
		_selectedTextRange = TextSelection();
		_selectedText = TextForMimeData();
	}
}

void ListWidget::setTextSelection(
		not_null<Element*> view,
		TextSelection selection) {
	clearSelected();
	const auto item = view->data();
	if (_selectedTextItem != item) {
		clearTextSelection();
		_selectedTextItem = view->data();
	}
	_selectedTextRange = selection;
	_selectedText = (selection.from != selection.to)
		? view->selectedText(selection)
		: TextForMimeData();
	repaintItem(view);
	if (!_wasSelectedText && !_selectedText.empty()) {
		_wasSelectedText = true;
		setFocus();
	}
}

bool ListWidget::loadedAtTopKnown() const {
	return !!_slice.skippedBefore;
}

bool ListWidget::loadedAtTop() const {
	return _slice.skippedBefore && (*_slice.skippedBefore == 0);
}

bool ListWidget::loadedAtBottomKnown() const {
	return !!_slice.skippedAfter;
}

bool ListWidget::loadedAtBottom() const {
	return _slice.skippedAfter && (*_slice.skippedAfter == 0);
}

bool ListWidget::isEmpty() const {
	return loadedAtTop()
		&& loadedAtBottom()
		&& (_itemsHeight + _itemsRevealHeight == 0);
}

bool ListWidget::hasCopyRestriction(HistoryItem *item) const {
	return _delegate->listCopyRestrictionType(item)
		!= CopyRestrictionType::None;
}

bool ListWidget::hasCopyMediaRestriction(not_null<HistoryItem*> item) const {
	return _delegate->listCopyMediaRestrictionType(item)
		!= CopyRestrictionType::None;
}

bool ListWidget::showCopyRestriction(HistoryItem *item) {
	const auto type = _delegate->listCopyRestrictionType(item);
	if (type == CopyRestrictionType::None) {
		return false;
	}
	_delegate->listUiShow()->showToast((type == CopyRestrictionType::Channel)
		? tr::lng_error_nocopy_channel(tr::now)
		: tr::lng_error_nocopy_group(tr::now));
	return true;
}

bool ListWidget::showCopyMediaRestriction(not_null<HistoryItem*> item) {
	const auto type = _delegate->listCopyMediaRestrictionType(item);
	if (type == CopyRestrictionType::None) {
		return false;
	}
	_delegate->listUiShow()->showToast((type == CopyRestrictionType::Channel)
		? tr::lng_error_nocopy_channel(tr::now)
		: tr::lng_error_nocopy_group(tr::now));
	return true;
}

bool ListWidget::hasCopyRestrictionForSelected() const {
	if (hasCopyRestriction()) {
		return true;
	}
	if (_selected.empty()) {
		if (_selectedTextItem && _selectedTextItem->forbidsForward()) {
			return true;
		}
	}
	for (const auto &[itemId, selection] : _selected) {
		if (const auto item = session().data().message(itemId)) {
			if (item->forbidsForward()) {
				return true;
			}
		}
	}
	return false;
}

bool ListWidget::showCopyRestrictionForSelected() {
	if (_selected.empty()) {
		if (_selectedTextItem && showCopyRestriction(_selectedTextItem)) {
			return true;
		}
	}
	for (const auto &[itemId, selection] : _selected) {
		if (showCopyRestriction(session().data().message(itemId))) {
			return true;
		}
	}
	return false;
}

bool ListWidget::hasSelectRestriction() const {
	return _delegate->listSelectRestrictionType()
		!= CopyRestrictionType::None;
}

Element *ListWidget::lookupItemByY(int y) const {
	return strictFindItemByY(y);
}

auto ListWidget::findViewForPinnedTracking(int top) const
-> std::pair<Element*, int> {
	const auto findScrollTopItem = [&](int top)
	-> std::vector<not_null<Element*>>::const_iterator {
		if (!width() || _items.empty()) {
			return end(_items);
		}
		const auto first = ranges::lower_bound(
			_items,
			top,
			std::less<>(),
			&Element::y);
		return (first == end(_items) || (*first)->y() > top)
			? first - 1
			: first;
	};
	const auto findView = [&](int top)
	-> std::pair<std::vector<not_null<Element*>>::const_iterator, int> {
		if (const auto i = findScrollTopItem(top); i != end(_items)) {
			return { i, top - (*i)->y() };
		}
		return { end(_items), 0 };
	};
	auto [view, offset] = findView(top);
	while (view != end(_items) && !(*view)->data()->isRegular()) {
		offset -= (*view)->height();
		++view;
	}
	return { (view != end(_items)) ? view->get() : nullptr, offset };
}

int ListWidget::itemMinimalHeight() const {
	return st::msgMarginTopAttached
		+ st::msgPhotoSize
		+ st::msgMargin.bottom();
}

void ListWidget::checkMoveToOtherViewer() {
	auto visibleHeight = (_visibleBottom - _visibleTop);
	if (width() <= 0
		|| visibleHeight <= 0
		|| _items.empty()
		|| _aroundIndex < 0
		|| _scrollTopState.item) {
		return;
	}

	auto topItemIndex = findItemIndexByY(_visibleTop);
	auto bottomItemIndex = findItemIndexByY(_visibleBottom);
	auto preloadedHeight = kPreloadedScreensCountFull * visibleHeight;
	auto preloadedCount = preloadedHeight / _itemAverageHeight;
	auto preloadIdsLimitMin = (preloadedCount / 2) + 1;
	auto preloadIdsLimit = preloadIdsLimitMin
		+ (visibleHeight / _itemAverageHeight);

	auto preloadBefore = kPreloadIfLessThanScreens * visibleHeight;
	auto before = _slice.skippedBefore;
	auto preloadTop = (_visibleTop < preloadBefore);
	auto topLoaded = before && (*before == 0);
	auto after = _slice.skippedAfter;
	auto preloadBottom = (height() - _visibleBottom < preloadBefore);
	auto bottomLoaded = after && (*after == 0);

	auto minScreenDelta = kPreloadedScreensCount
		- kPreloadIfLessThanScreens;
	auto minUniversalIdDelta = (minScreenDelta * visibleHeight)
		/ _itemAverageHeight;
	const auto preloadAroundMessage = [&](int index) {
		Expects(index >= 0 && index < _items.size());

		auto preloadRequired = false;
		auto itemPosition = _items[index]->data()->position();

		if (!preloadRequired) {
			preloadRequired = (_idsLimit < preloadIdsLimitMin);
		}
		if (!preloadRequired) {
			Assert(_aroundIndex >= 0);
			auto delta = std::abs(index - _aroundIndex);
			preloadRequired = (delta >= minUniversalIdDelta);
		}
		if (preloadRequired) {
			_idsLimit = preloadIdsLimit;
			_aroundPosition = itemPosition;
			_aroundIndex = index;
			refreshViewer();
		}
	};

	const auto findGoodAbove = [&](int index) {
		Expects(index >= 0 && index < _items.size());

		for (; index != _items.size(); ++index) {
			if (_delegate->listIsGoodForAroundPosition(_items[index])) {
				return index;
			}
		}
		return -1;
	};
	const auto findGoodBelow = [&](int index) {
		Expects(index >= 0 && index < _items.size());

		for (++index; index != 0;) {
			if (_delegate->listIsGoodForAroundPosition(_items[--index])) {
				return index;
			}
		}
		return -1;
	};
	if (preloadTop && !topLoaded) {
		const auto goodAboveIndex = findGoodAbove(topItemIndex);
		const auto goodIndex = (goodAboveIndex >= 0)
			? goodAboveIndex
			: findGoodBelow(topItemIndex);
		if (goodIndex >= 0) {
			preloadAroundMessage(goodIndex);
		}
	} else if (preloadBottom && !bottomLoaded) {
		const auto goodBelowIndex = findGoodBelow(bottomItemIndex);
		const auto goodIndex = (goodBelowIndex >= 0)
			? goodBelowIndex
			: findGoodAbove(bottomItemIndex);
		if (goodIndex >= 0) {
			preloadAroundMessage(goodIndex);
		}
	}
}

QString ListWidget::tooltipText() const {
	const auto item = (_overElement && _mouseAction == MouseAction::None)
		? _overElement->data().get()
		: nullptr;
	if (_mouseCursorState == CursorState::Date && item) {
		return HistoryView::DateTooltipText(_overElement);
	} else if (_mouseCursorState == CursorState::Forwarded && item) {
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			return forwarded->text.toString();
		}
	} else if (const auto link = ClickHandler::getActive()) {
		return link->tooltip();
	}
	return QString();
}

QPoint ListWidget::tooltipPos() const {
	return _mousePosition;
}

bool ListWidget::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

Context ListWidget::elementContext() {
	return _delegate->listContext();
}

bool ListWidget::elementUnderCursor(
		not_null<const HistoryView::Element*> view) {
	return (_overElement == view);
}

SelectionModeResult ListWidget::elementInSelectionMode() {
	return inSelectionMode();
}

bool ListWidget::elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) {
	Expects(view->delegate() == this);

	const auto top = itemTop(view);
	const auto bottom = top + view->height();
	return (top < till && bottom > from);
}

void ListWidget::elementStartStickerLoop(not_null<const Element*> view) {
}

void ListWidget::elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) {
	_delegate->listShowPollResults(poll, context);
}

void ListWidget::elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	_delegate->listOpenPhoto(photo, context);
}

void ListWidget::elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	_delegate->listOpenDocument(document, context, showInMediaView);
}

void ListWidget::elementCancelUpload(const FullMsgId &context) {
	if (const auto item = session().data().message(context)) {
		_delegate->listCancelUploadLayer(item);
	}
}

void ListWidget::elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	// Under the parent is supposed to be a scroll widget.
	_topToast.show(parentWidget(), &session(), text, hiddenCallback);
}

bool ListWidget::elementAnimationsPaused() {
	return _delegate->listAnimationsPaused();
}

bool ListWidget::elementHideReply(not_null<const Element*> view) {
	return _delegate->listElementHideReply(view);
}

bool ListWidget::elementShownUnread(not_null<const Element*> view) {
	return _delegate->listElementShownUnread(view);
}

void ListWidget::elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
	_delegate->listSendBotCommand(command, context);
}

void ListWidget::elementSearchInList(
		const QString &query,
		const FullMsgId &context) {
	_delegate->listSearch(query, context);
}

void ListWidget::elementHandleViaClick(not_null<UserData*> bot) {
	_delegate->listHandleViaClick(bot);
}

bool ListWidget::elementIsChatWide() {
	return _overrideIsChatWide.value_or(_isChatWide);
}

not_null<Ui::PathShiftGradient*> ListWidget::elementPathShiftGradient() {
	return _pathGradient.get();
}

void ListWidget::elementReplyTo(const FullReplyTo &to) {
	replyToMessageRequestNotify(to);
}

void ListWidget::elementStartInteraction(not_null<const Element*> view) {
}

void ListWidget::elementStartPremium(
		not_null<const Element*> view,
		Element *replacing) {
	const auto already = !_emojiInteractions->playPremiumEffect(
		view,
		replacing);
	if (already) {
		showPremiumStickerTooltip(view);
	}
}

void ListWidget::elementCancelPremium(not_null<const Element*> view) {
	_emojiInteractions->cancelPremiumEffect(view);
}

void ListWidget::elementStartEffect(
		not_null<const Element*> view,
		Element *replacing) {
	_emojiInteractions->playEffect(view);
}

QString ListWidget::elementAuthorRank(not_null<const Element*> view) {
	return _delegate->listElementAuthorRank(view);
}

bool ListWidget::elementHideTopicButton(not_null<const Element*> view) {
	return _delegate->listElementHideTopicButton(view);
}


void ListWidget::saveState(not_null<ListMemento*> memento) {
	memento->setAroundPosition(_aroundPosition);
	const auto state = countScrollState();
	memento->setIdsLimit(state.item ? _idsLimit : 0);
	memento->setScrollTopState(state);
}

void ListWidget::restoreState(not_null<ListMemento*> memento) {
	_aroundPosition = memento->aroundPosition();
	_aroundIndex = -1;
	if (const auto limit = memento->idsLimit()) {
		_idsLimit = limit;
	}
	_scrollTopState = memento->scrollTopState();
	refreshViewer();
}

void ListWidget::updateItemsGeometry() {
	const auto count = int(_items.size());
	const auto first = [&] {
		for (auto i = 0; i != count; ++i) {
			const auto view = _items[i].get();
			if (view->isHidden()) {
				view->setDisplayDate(false);
			} else {
				view->setDisplayDate(_context != Context::ShortcutMessages);
				view->setAttachToPrevious(false);
				return i;
			}
		}
		return count;
	}();
	refreshAttachmentsFromTill(first, count);
}

void ListWidget::updateSize() {
	resizeToWidth(width(), _minHeight);
	updateVisibleTopItem();
	_resizePending = false;
}

void ListWidget::resizeToWidth(int newWidth, int minHeight) {
	_minHeight = minHeight;
	TWidget::resizeToWidth(newWidth);
	restoreScrollPosition();
}

void ListWidget::startItemRevealAnimations() {
	for (const auto &view : base::take(_itemRevealPending)) {
		if (const auto height = view->height()) {
			startMessageSendingAnimation(view->data());
			if (!_itemRevealAnimations.contains(view)) {
				auto &animation = _itemRevealAnimations[view];
				animation.startHeight = height;
				_itemsRevealHeight += height;
				animation.animation.start(
					[=] { revealItemsCallback(); },
					0.,
					1.,
					kItemRevealDuration,
					anim::easeOutCirc);
				if (view->data()->out()) {
					_delegate->listChatTheme()->rotateComplexGradientBackground();
				}
			}
		}
	}
}

void ListWidget::startMessageSendingAnimation(
		not_null<HistoryItem*> item) {
	const auto sendingAnimation = _delegate->listSendingAnimation();
	if (!sendingAnimation || !sendingAnimation->checkExpectedType(item)) {
		return;
	}

	auto globalEndTopLeft = rpl::merge(
		session().data().newItemAdded() | rpl::to_empty,
		geometryValue() | rpl::to_empty
	) | rpl::map([=]() -> std::optional<QPoint> {
		const auto view = viewForItem(item);
		if (!view) {
			return std::nullopt;
		}
		const auto additional = !_visibleTop ? view->height() : 0;
		return mapToGlobal(QPoint(0, itemTop(view) - additional));
	});

	sendingAnimation->startAnimation({
		.globalEndTopLeft = std::move(globalEndTopLeft),
		.view = [=] { return viewForItem(item); },
		.paintContext = [=] { return preparePaintContext({}); },
	});
}

void ListWidget::showPremiumStickerTooltip(
		not_null<const HistoryView::Element*> view) {
	if (const auto media = view->data()->media()) {
		if (const auto document = media->document()) {
			_delegate->listShowPremiumToast(document);
		}
	}
}

void ListWidget::revealItemsCallback() {
	auto revealHeight = 0;
	for (auto i = begin(_itemRevealAnimations)
		; i != end(_itemRevealAnimations);) {
		if (!i->second.animation.animating()) {
			i = _itemRevealAnimations.erase(i);
		} else {
			revealHeight += anim::interpolate(
				i->second.startHeight,
				0,
				i->second.animation.value(1.));
			++i;
		}
	}
	if (_itemsRevealHeight != revealHeight) {
		updateVisibleTopItem();
		if (_visibleTopItem) {
			// We're not at the bottom.
			revealHeight = 0;
			_itemRevealAnimations.clear();
		}
		const auto old = std::exchange(_itemsRevealHeight, revealHeight);
		const auto delta = old - _itemsRevealHeight;
		_itemsHeight += delta;
		_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom)
			? (_minHeight - _itemsHeight - st::historyPaddingBottom)
			: 0;
		const auto wasHeight = height();
		const auto nowHeight = _itemsTop
			+ _itemsHeight
			+ st::historyPaddingBottom;
		if (wasHeight != nowHeight) {
			resize(width(), nowHeight);
		}
		update();
		restoreScrollPosition();
		updateVisibleTopItem();

		if (!_itemsRevealHeight) {
			mouseActionUpdate(QCursor::pos());
		}
	}
}

int ListWidget::resizeGetHeight(int newWidth) {
	update();

	const auto resizeAllItems = (_itemsWidth != newWidth);
	auto newHeight = 0;
	for (const auto &view : _items) {
		view->setY(newHeight);
		if (view->pendingResize() || resizeAllItems) {
			newHeight += view->resizeGetHeight(newWidth);
		} else {
			newHeight += view->height();
		}
	}
	if (newHeight > 0) {
		_itemAverageHeight = std::max(
			itemMinimalHeight(),
			newHeight / int(_items.size()));
	}
	startItemRevealAnimations();
	_itemsWidth = newWidth;
	_itemsHeight = newHeight - _itemsRevealHeight;
	_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom)
		? (_minHeight - _itemsHeight - st::historyPaddingBottom)
		: 0;
	if (_emptyInfo) {
		_emptyInfo->setVisible(isEmpty());
	}
	return _itemsTop + _itemsHeight + st::historyPaddingBottom;
}

void ListWidget::restoreScrollPosition() {
	auto newVisibleTop = _visibleTopItem
		? (itemTop(_visibleTopItem) + _visibleTopFromItem)
		: ScrollMax;
	_delegate->listScrollTo(newVisibleTop);
}

TextSelection ListWidget::computeRenderSelection(
		not_null<const SelectedMap*> selected,
		not_null<const Element*> view) const {
	const auto itemSelection = [&](not_null<HistoryItem*> item) {
		auto i = selected->find(item->fullId());
		if (i != selected->end()) {
			return FullSelection;
		}
		return TextSelection();
	};
	const auto item = view->data();
	if (const auto group = session().data().groups().find(item)) {
		if (group->items.front() != item) {
			return TextSelection();
		}
		auto result = TextSelection();
		auto allFullSelected = true;
		const auto count = int(group->items.size());
		for (auto i = 0; i != count; ++i) {
			if (itemSelection(group->items[i]) == FullSelection) {
				result = AddGroupItemSelection(result, i);
			} else {
				allFullSelected = false;
			}
		}
		if (allFullSelected) {
			return FullSelection;
		}
		const auto leaderSelection = itemSelection(item);
		if (leaderSelection != FullSelection
			&& leaderSelection != TextSelection()) {
			return leaderSelection;
		}
		return result;
	}
	return itemSelection(item);
}

TextSelection ListWidget::itemRenderSelection(
		not_null<const Element*> view) const {
	if (!_dragSelected.empty()) {
		const auto i = _dragSelected.find(view->data()->fullId());
		if (i != _dragSelected.end()) {
			return (_dragSelectAction == DragSelectAction::Selecting)
				? FullSelection
				: TextSelection();
		}
	}
	if (!_selected.empty() || !_dragSelected.empty()) {
		return computeRenderSelection(&_selected, view);
	} else if (view->data() == _selectedTextItem) {
		return _selectedTextRange;
	}
	return TextSelection();
}

Ui::ChatPaintContext ListWidget::preparePaintContext(
		const QRect &clip) const {
	return _delegate->listPreparePaintContext({
		.theme = _delegate->listChatTheme(),
		.clip = clip,
		.visibleAreaPositionGlobal = mapToGlobal(QPoint(0, _visibleTop)),
		.visibleAreaTop = _visibleTop,
		.visibleAreaWidth = width(),
	});
}

bool ListWidget::markingContentsRead() const {
	return _showFinished
		&& !_refreshingViewer
		&& _delegate->listMarkingContentRead();
}

bool ListWidget::markingMessagesRead() const {
	return markingContentsRead() && !session().supportMode();
}

void ListWidget::showFinished() {
	_showFinished = true;
	checkActivation();
}

void ListWidget::checkActivation() {
	if (_resizePending
		|| _visibleTop >= _visibleBottom
		|| !markingMessagesRead()) {
		return;
	}
	for (const auto &view : ranges::views::reverse(_items)) {
		const auto bottom = itemTop(view) + view->height();
		if (_visibleBottom + _itemsRevealHeight >= bottom) {
			const auto item = view->data();
			if (item->isRegular()) {
				delegate()->listMarkReadTill(item);
				return;
			}
		}
	}
}

void ListWidget::paintEvent(QPaintEvent *e) {
	if (_delegate->listIgnorePaintEvent(this, e)) {
		return;
	} else if (_translateTracker) {
		_translateTracker->startBunch();
	}
	auto readTill = (HistoryItem*)nullptr;
	auto readContents = base::flat_set<not_null<HistoryItem*>>();
	const auto markingAsViewed = markingMessagesRead();
	const auto guard = gsl::finally([&] {
		if (_translateTracker) {
			_delegate->listAddTranslatedItems(_translateTracker.get());
			_translateTracker->finishBunch();
		}
		if (markingAsViewed && readTill) {
			_delegate->listMarkReadTill(readTill);
		}
		if (!readContents.empty() && markingContentsRead()) {
			_delegate->listMarkContentsRead(readContents);
		}
		_userpicsCache.clear();
	});

	Painter p(this);

	_pathGradient->startFrame(
		0,
		width(),
		std::min(st::msgMaxWidth / 2, width() / 2));

	auto clip = e->rect();

	auto from = std::lower_bound(begin(_items), end(_items), clip.top(), [this](auto &elem, int top) {
		return this->itemTop(elem) + elem->height() <= top;
	});
	auto to = std::lower_bound(begin(_items), end(_items), clip.top() + clip.height(), [this](auto &elem, int bottom) {
		return this->itemTop(elem) < bottom;
	});

	auto context = preparePaintContext(clip);
	context.highlightPathCache = &_highlightPathCache;
	if (from == end(_items)) {
		_delegate->listPaintEmpty(p, context);
		return;
	}
	if (_reactionsManager) {
		_reactionsManager->startEffectsCollection();
	}

	const auto session = &this->session();
	auto top = itemTop(from->get());
	context = context.translated(0, -top);
	p.translate(0, top);
	const auto sendingAnimation = _delegate->listSendingAnimation();
	for (auto i = from; i != to; ++i) {
		const auto view = *i;
		const auto item = view->data();
		const auto height = view->height();
		if (!sendingAnimation
			|| !sendingAnimation->hasAnimatedMessage(item)) {
			if (_reactionsManager) {
				context.reactionInfo
					= _reactionsManager->currentReactionPaintInfo();
			}
			context.outbg = view->hasOutLayout();
			context.selection = itemRenderSelection(view);
			context.highlight = _highlighter.state(item);
			view->draw(p, context);
		}
		if (_translateTracker) {
			_translateTracker->add(view);
		}
		const auto isSponsored = item->isSponsored();
		const auto isUnread = _delegate->listElementShownUnread(view)
			&& item->isRegular();
		const auto withReaction = context.reactionInfo
			&& item->hasUnreadReaction();
		const auto yShown = [&](int y) {
			return (_visibleBottom >= y && _visibleTop <= y);
		};
		const auto markShown = (_context != Context::ChatPreview)
			&& (isSponsored
				? view->markSponsoredViewed(_visibleBottom - top)
				: withReaction
				? yShown(top + context.reactionInfo->position.y())
				: isUnread
				? yShown(top + height)
				: yShown(top + height / 2));
		if (markShown) {
			if (isSponsored) {
				session->sponsoredMessages().view(item->fullId());
			} else if (isUnread) {
				readTill = item;
			}
			if (markingAsViewed && item->hasViews()) {
				session->api().views().scheduleIncrement(item);
			}
			if (withReaction) {
				readContents.insert(item);
			} else if (item->isUnreadMention()
				&& !item->isUnreadMedia()) {
				readContents.insert(item);
				_highlighter.enqueue({ item });
			}
		}
		session->data().reactions().poll(item, context.now);
		if (item->hasUnpaidContent()) {
			session->api().views().pollExtendedMedia(item);
		}
		if (_reactionsManager) {
			_reactionsManager->recordCurrentReactionEffect(
				item->fullId(),
				QPoint(0, top));
		}
		top += height;
		context.translate(0, -height);
		p.translate(0, height);
	}
	context.translate(0, top);
	p.translate(0, -top);

	paintUserpics(p, context, clip);
	paintDates(p, context, clip);

	if (_reactionsManager) {
		_reactionsManager->paint(p, context);
	}
}

void ListWidget::paintUserpics(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect clip) {
	if (_context == Context::ShortcutMessages) {
		return;
	}
	const auto session = &this->session();
	enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
		// stop the enumeration if the userpic is below the painted rect
		if (userpicTop >= clip.top() + clip.height()) {
			return false;
		}

		// paint the userpic if it intersects the painted rect
		if (userpicTop + st::msgPhotoSize > clip.top()) {
			const auto item = view->data();
			const auto hasTranslation = context.gestureHorizontal.translation
				&& (context.gestureHorizontal.msgBareId
					== item->fullId().msg.bare);
			if (hasTranslation) {
				p.translate(context.gestureHorizontal.translation, 0);
				update(
					QRect(
						st::historyPhotoLeft
							+ context.gestureHorizontal.translation,
						userpicTop,
						st::msgPhotoSize
							- context.gestureHorizontal.translation,
						st::msgPhotoSize));
			}
			if (const auto from = item->displayFrom()) {
				from->paintUserpicLeft(
					p,
					_userpics[from],
					st::historyPhotoLeft,
					userpicTop,
					view->width(),
					st::msgPhotoSize);
			} else if (const auto info = item->displayHiddenSenderInfo()) {
				if (info->customUserpic.empty()) {
					info->emptyUserpic.paintCircle(
						p,
						st::historyPhotoLeft,
						userpicTop,
						view->width(),
						st::msgPhotoSize);
				} else {
					auto &userpic = _hiddenSenderUserpics[item->id];
					const auto valid = info->paintCustomUserpic(
						p,
						userpic,
						st::historyPhotoLeft,
						userpicTop,
						view->width(),
						st::msgPhotoSize);
					if (!valid) {
						info->customUserpic.load(session, item->fullId());
					}
				}
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
			if (hasTranslation) {
				p.translate(-context.gestureHorizontal.translation, 0);
			}
		}
		return true;
	});
}

void ListWidget::paintDates(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect clip) {
	if (_context == Context::ShortcutMessages) {
		return;
	}

	auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
	auto scrollDateOpacity = _scrollDateOpacity.value(_scrollDateShown ? 1. : 0.);
	enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
		// stop the enumeration if the date is above the painted rect
		if (dateTop + dateHeight <= clip.top()) {
			return false;
		}

		const auto displayDate = view->displayDate();
		auto dateInPlace = displayDate;
		if (dateInPlace) {
			const auto correctDateTop = itemtop + st::msgServiceMargin.top();
			dateInPlace = (dateTop < correctDateTop + dateHeight);
		}
		//bool noFloatingDate = (item->date.date() == lastDate && displayDate);
		//if (noFloatingDate) {
		//	if (itemtop < showFloatingBefore) {
		//		noFloatingDate = false;
		//	}
		//}

		// paint the date if it intersects the painted rect
		if (dateTop < clip.top() + clip.height()) {
			auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
			if (opacity > 0.) {
				p.setOpacity(opacity);
				int dateY = /*noFloatingDate ? itemtop :*/ (dateTop - st::msgServiceMargin.top());
				int width = view->width();
				if (const auto date = view->Get<HistoryView::DateBadge>()) {
					date->paint(p, context.st, dateY, width, _isChatWide);
				} else {
					ServiceMessagePainter::PaintDate(
						p,
						context.st,
						ItemDateText(
							view->data(),
							IsItemScheduledUntilOnline(view->data())),
						dateY,
						width,
						_isChatWide);
				}
			}
		}
		return true;
	});
}

void ListWidget::maybeMarkReactionsRead(not_null<HistoryItem*> item) {
	const auto view = viewForItem(item);
	if (!view || !markingContentsRead()) {
		return;
	}
	const auto top = itemTop(view);
	const auto reactionCenter
		= view->reactionButtonParameters({}, {}).center.y();
	if (top + reactionCenter < _visibleTop
		|| top + view->height() > _visibleBottom) {
		return;
	}
	_delegate->listMarkContentsRead({ item });
}

bool ListWidget::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return RpWidget::eventHook(e);
}

void ListWidget::applyDragSelection() {
	if (!hasSelectRestriction()) {
		applyDragSelection(_selected);
	}
	clearDragSelection();
	pushSelectedItems();
}

void ListWidget::applyDragSelection(SelectedMap &applyTo) const {
	if (_dragSelectAction == DragSelectAction::Selecting) {
		for (const auto &itemId : _dragSelected) {
			if (applyTo.size() >= MaxSelectedItems) {
				break;
			} else if (!applyTo.contains(itemId)) {
				if (const auto item = session().data().message(itemId)) {
					addToSelection(applyTo, item);
				}
			}
		}
	} else if (_dragSelectAction == DragSelectAction::Deselecting) {
		for (const auto &itemId : _dragSelected) {
			removeFromSelection(applyTo, itemId);
		}
	}
}

TextForMimeData ListWidget::getSelectedText() const {
	auto selected = _selected;

	if (_mouseAction == MouseAction::Selecting && !_dragSelected.empty()) {
		applyDragSelection(selected);
	}

	if (selected.empty()) {
		if (const auto view = viewForItem(_selectedTextItem)) {
			return view->selectedText(_selectedTextRange);
		}
		return _selectedText;
	}

	auto groups = base::flat_set<not_null<const Data::Group*>>();
	auto fullSize = 0;
	auto texts = std::vector<std::pair<
		not_null<HistoryItem*>,
		TextForMimeData>>();
	texts.reserve(selected.size());

	const auto wrapItem = [&](
			not_null<HistoryItem*> item,
			TextForMimeData &&unwrapped) {
		auto time = QString(", [%1]\n").arg(
			QLocale().toString(ItemDateTime(item), QLocale::ShortFormat));
		auto part = TextForMimeData();
		auto size = item->author()->name().size()
			+ time.size()
			+ unwrapped.expanded.size();
		part.reserve(size);
		part.append(item->author()->name()).append(time);
		part.append(std::move(unwrapped));
		texts.emplace_back(std::move(item), std::move(part));
		fullSize += size;
	};
	const auto addItem = [&](not_null<HistoryItem*> item) {
		wrapItem(item, HistoryItemText(item));
	};
	const auto addGroup = [&](not_null<const Data::Group*> group) {
		Expects(!group->items.empty());

		wrapItem(group->items.back(), HistoryGroupText(group));
	};

	for (const auto &[itemId, data] : selected) {
		if (const auto item = session().data().message(itemId)) {
			if (const auto group = session().data().groups().find(item)) {
				if (groups.contains(group)) {
					continue;
				}
				if (isSelectedGroup(selected, group)) {
					groups.emplace(group);
					addGroup(group);
				} else {
					addItem(item);
				}
			} else {
				addItem(item);
			}
		}
	}
	ranges::sort(texts, [&](
			const std::pair<not_null<HistoryItem*>, TextForMimeData> &a,
			const std::pair<not_null<HistoryItem*>, TextForMimeData> &b) {
		return _delegate->listIsLessInOrder(a.first, b.first);
	});

	auto result = TextForMimeData();
	auto sep = u"\n\n"_q;
	result.reserve(fullSize + (texts.size() - 1) * sep.size());
	for (auto i = begin(texts), e = end(texts); i != e;) {
		result.append(std::move(i->second));
		if (++i != e) {
			result.append(sep);
		}
	}
	return result;
}

MessageIdsList ListWidget::getSelectedIds() const {
	return collectSelectedIds();
}

SelectedItems ListWidget::getSelectedItems() const {
	return collectSelectedItems();
}

const TextSelection &ListWidget::getSelectedTextRange(
		not_null<HistoryItem*> item) const {
	return _selectedTextRange;
}

int ListWidget::findItemIndexByY(int y) const {
	Expects(!_items.empty());

	if (y < _itemsTop) {
		return 0;
	}
	auto i = std::lower_bound(
		begin(_items),
		end(_items),
		y,
		[this](auto &elem, int top) {
		return this->itemTop(elem) + elem->height() <= top;
	});
	return std::min(int(i - begin(_items)), int(_items.size() - 1));
}

not_null<Element*> ListWidget::findItemByY(int y) const {
	return _items[findItemIndexByY(y)];
}

Element *ListWidget::strictFindItemByY(int y) const {
	if (_items.empty()) {
		return nullptr;
	}
	return (y >= _itemsTop && y < _itemsTop + _itemsHeight)
		? findItemByY(y).get()
		: nullptr;
}

auto ListWidget::countScrollState() const -> ScrollTopState {
	if (_items.empty()
		|| (_itemsKnownTillEnd && _visibleBottom == height())) {
		return { Data::MessagePosition(), 0 };
	}
	const auto topItem = findItemByY(_visibleTop);
	return {
		topItem->data()->position(),
		_visibleTop - itemTop(topItem)
	};
}

void ListWidget::keyPressEvent(QKeyEvent *e) {
	const auto key = e->key();
	const auto hasModifiers = (Qt::NoModifier !=
		(e->modifiers()
			& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier)));
	if (key == Qt::Key_Escape || key == Qt::Key_Back) {
		if (hasSelectedText() || hasSelectedItems()) {
			cancelSelection();
		} else {
			_delegate->listCancelRequest();
		}
	} else if (e == QKeySequence::Copy
		&& (hasSelectedText() || hasSelectedItems())
		&& !showCopyRestriction()
		&& !hasCopyRestrictionForSelected()) {
		TextUtilities::SetClipboardText(getSelectedText());
#ifdef Q_OS_MAC
	} else if (key == Qt::Key_E
		&& e->modifiers().testFlag(Qt::ControlModifier)
		&& !showCopyRestriction()
		&& !hasCopyRestrictionForSelected()) {
		TextUtilities::SetClipboardText(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else if (e == QKeySequence::Delete || key == Qt::Key_Backspace) {
		_delegate->listDeleteRequest();
	} else if (!hasModifiers
		&& ((key == Qt::Key_Up)
			|| (key == Qt::Key_Down)
			|| (key == Qt::Key_PageUp)
			|| (key == Qt::Key_PageDown))) {
		_scrollKeyEvents.fire(std::move(e));
	} else if (!(e->modifiers() & ~Qt::ShiftModifier)
		&& key != Qt::Key_Shift) {
		_delegate->listTryProcessKeyInput(e);
	} else {
		e->ignore();
	}
}

auto ListWidget::scrollKeyEvents() const
-> rpl::producer<not_null<QKeyEvent*>> {
	return _scrollKeyEvents.events();
}

void ListWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());
	trySwitchToWordSelection();
	if (!ClickHandler::getActive()
		&& !ClickHandler::getPressed()
		&& (_mouseCursorState == CursorState::None
			|| _mouseCursorState == CursorState::Date)
		&& _selected.empty()
		&& _overElement
		&& _overElement->data()->isRegular()) {
		mouseActionCancel();
		switch (CurrentQuickAction()) {
		case DoubleClickQuickAction::Reply: {
			replyToMessageRequestNotify({ _overElement->data()->fullId() });
		} break;
		case DoubleClickQuickAction::React: {
			toggleFavoriteReaction(_overElement);
		} break;
		default: break;
		}
	}
}

void ListWidget::toggleFavoriteReaction(not_null<Element*> view) const {
	const auto item = view->data();
	const auto favorite = session().data().reactions().favoriteId();
	if (!ranges::contains(
			Data::LookupPossibleReactions(item).recent,
			favorite,
			&Data::Reaction::id)
		|| _delegate->listShowReactPremiumError(item, favorite)) {
		return;
	} else if (!ranges::contains(item->chosenReactions(), favorite)) {
		if (const auto top = itemTop(view); top >= 0) {
			view->animateReaction({ .id = favorite });
		}
	}
	item->toggleReaction(favorite, HistoryReactionSource::Quick);
}

void ListWidget::trySwitchToWordSelection() {
	auto selectingSome = (_mouseAction == MouseAction::Selecting)
		&& hasSelectedText();
	auto willSelectSome = (_mouseAction == MouseAction::None)
		&& !hasSelectedItems();
	auto checkSwitchToWordSelection = _overElement
		&& (_mouseSelectType == TextSelectType::Letters)
		&& (selectingSome || willSelectSome);
	if (checkSwitchToWordSelection) {
		switchToWordSelection();
	}
}

void ListWidget::switchToWordSelection() {
	Expects(_overElement != nullptr);

	StateRequest request;
	request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
	auto dragState = _overElement->textState(_pressState.point, request);
	if (dragState.cursor != CursorState::Text) {
		return;
	}
	_mouseTextSymbol = dragState.symbol;
	_mouseSelectType = TextSelectType::Words;
	if (_mouseAction == MouseAction::None) {
		_mouseAction = MouseAction::Selecting;
		setTextSelection(_overElement, TextSelection(
			dragState.symbol,
			dragState.symbol
		));
	}
	mouseActionUpdate();

	_trippleClickPoint = _mousePosition;
	_trippleClickStartTime = crl::now();
}

void ListWidget::validateTrippleClickStartTime() {
	if (_trippleClickStartTime) {
		const auto elapsed = (crl::now() - _trippleClickStartTime);
		if (elapsed >= QApplication::doubleClickInterval()) {
			_trippleClickStartTime = 0;
		}
	}
}

void ListWidget::contextMenuEvent(QContextMenuEvent *e) {
	showContextMenu(e);
}

void ListWidget::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	const auto link = ClickHandler::getActive();
	if (link
		&& !link->property(
			kSendReactionEmojiProperty).value<Data::ReactionId>().empty()
		&& _reactionsManager
		&& _reactionsManager->showContextMenu(
			this,
			e,
			session().data().reactions().favoriteId())) {
		return;
	}
	const auto overItem = _overItemExact
		? _overItemExact
		: _overElement
		? _overElement->data().get()
		: nullptr;
	const auto clickedReaction = link
		? link->property(
			kReactionsCountEmojiProperty).value<Data::ReactionId>()
		: Data::ReactionId();
	_whoReactedMenuLifetime.destroy();
	if (!clickedReaction.empty()
		&& overItem
		&& Api::WhoReactedExists(overItem, Api::WhoReactedList::One)) {
		HistoryView::ShowWhoReactedMenu(
			&_menu,
			e->globalPos(),
			this,
			overItem,
			clickedReaction,
			controller(),
			_whoReactedMenuLifetime);
		e->accept();
		return;
	}

	auto request = ContextMenuRequest(controller());

	request.link = link;
	request.view = _overElement;
	request.item = overItem;
	request.pointState = _overState.pointState;
	request.quote = (_overElement
		&& _selectedTextItem == _overElement->data())
		? _overElement->selectedQuote(_selectedTextRange)
		: SelectedQuote();
	request.selectedText = _selectedText;
	request.selectedItems = collectSelectedItems();
	const auto hasSelection = !request.selectedItems.empty()
		|| !request.selectedText.empty();
	request.overSelection = (showFromTouch && hasSelection)
		|| (_overElement
			&& isInsideSelection(
				_overElement,
				_overItemExact ? _overItemExact : _overElement->data().get(),
				_overState));

	_menu = FillContextMenu(this, request);
	if (_menu->empty()) {
		_menu = nullptr;
		return;
	}

	using namespace HistoryView::Reactions;
	const auto desiredPosition = e->globalPos();
	const auto reactItem = (_overElement
		&& _overState.pointState != PointState::Outside)
		? _overElement->data().get()
		: nullptr;
	const auto attached = reactItem
		? AttachSelectorToMenu(
			_menu.get(),
			controller(),
			desiredPosition,
			reactItem,
			[=](ChosenReaction reaction) { reactionChosen(reaction); },
			ItemReactionsAbout(reactItem))
		: AttachSelectorResult::Skipped;
	if (attached == AttachSelectorResult::Failed) {
		_menu = nullptr;
		return;
	} else if (attached == AttachSelectorResult::Attached) {
		_menu->popupPrepared();
	} else {
		_menu->popup(desiredPosition);
	}
	e->accept();
}

void ListWidget::reactionChosen(ChosenReaction reaction) {
	const auto item = session().data().message(reaction.context);
	if (!item) {
		return;
	} else if (reaction.id.paid()) {
		Payments::ShowPaidReactionDetails(
			controller(),
			item,
			viewForItem(item),
			HistoryReactionSource::Selector);
		return;
	} else if (_delegate->listShowReactPremiumError(item, reaction.id)) {
		if (_menu) {
			_menu->hideMenu();
		}
		return;
	}
	item->toggleReaction(reaction.id, HistoryReactionSource::Selector);
	if (!ranges::contains(item->chosenReactions(), reaction.id)) {
		return;
	} else if (const auto view = viewForItem(item)) {
		const auto geometry = reaction.localGeometry.isEmpty()
			? mapFromGlobal(reaction.globalGeometry)
			: reaction.localGeometry;
		if (const auto top = itemTop(view); top >= 0) {
			view->animateReaction({
				.id = reaction.id,
				.flyIcon = reaction.icon,
				.flyFrom = geometry.translated(0, -top),
			});
		}
	}
}

void ListWidget::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
}

void ListWidget::onTouchScrollTimer() {
	auto nowTime = crl::now();
	if (_touchScrollState == Ui::TouchScrollState::Acceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = Ui::TouchScrollState::Manual;
		touchResetSpeed();
	} else if (_touchScrollState == Ui::TouchScrollState::Auto || _touchScrollState == Ui::TouchScrollState::Acceleration) {
		const auto elapsed = int(nowTime - _touchTime);
		const auto delta = _touchSpeed * elapsed / 1000;
		const auto hasScrolled = _delegate->listScrollTo(
			_visibleTop - delta.y());
		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = Ui::TouchScrollState::Manual;
			_touchScroll = false;
			_touchScrollTimer.cancel();
		} else {
			_touchTime = nowTime;
		}
		touchDeaccelerate(elapsed);
	}
}

void ListWidget::touchUpdateSpeed() {
	const auto nowTime = crl::now();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > Ui::kFingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > Ui::kFingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == Ui::TouchScrollState::Auto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(std::clamp(
						(oldSpeedY + (newSpeedY / 4)),
						-Ui::kMaxScrollAccelerated,
						+Ui::kMaxScrollAccelerated));
					_touchSpeed.setX(std::clamp(
						(oldSpeedX + (newSpeedX / 4)),
						-Ui::kMaxScrollAccelerated,
						+Ui::kMaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				// we average the speed to avoid strange effects with the last delta
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(std::clamp(
						(_touchSpeed.x() / 4) + (newSpeedX * 3 / 4),
						-Ui::kMaxScrollFlick,
						+Ui::kMaxScrollFlick));
					_touchSpeed.setY(std::clamp(
						(_touchSpeed.y() / 4) + (newSpeedY * 3 / 4),
						-Ui::kMaxScrollFlick,
						+Ui::kMaxScrollFlick));
				} else {
					_touchSpeed = QPoint(newSpeedX, newSpeedY);
				}
			}
		}
	} else {
		_touchPrevPosValid = true;
	}
	_touchSpeedTime = nowTime;
	_touchPrevPos = _touchPos;
}

void ListWidget::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

void ListWidget::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void ListWidget::touchEvent(QTouchEvent *e) {
	if (e->type() == QEvent::TouchCancel) { // cancel
		if (!_touchInProgress) return;
		_touchInProgress = false;
		_touchSelectTimer.cancel();
		_touchScroll = _touchSelect = false;
		_touchScrollState = Ui::TouchScrollState::Manual;
		_touchMaybeSelecting = false;
		mouseActionCancel();
		return;
	}

	if (!e->touchPoints().isEmpty()) {
		_touchPrevPos = _touchPos;
		_touchPos = e->touchPoints().cbegin()->screenPos().toPoint();
	}

	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_menu) {
			e->accept();
			return; // ignore mouse press, that was hiding context menu
		}
		if (_touchInProgress) return;
		if (e->touchPoints().isEmpty()) return;

		_touchInProgress = true;
		if (_touchScrollState == Ui::TouchScrollState::Auto) {
			_touchMaybeSelecting = false;
			_touchScrollState = Ui::TouchScrollState::Acceleration;
			_touchWaitingAcceleration = true;
			_touchAccelerationTime = crl::now();
			touchUpdateSpeed();
			_touchStart = _touchPos;
		} else {
			_touchScroll = false;
			_touchMaybeSelecting = true;
			_touchSelectTimer.callOnce(QApplication::startDragTime());
		}
		_touchSelect = false;
		_touchStart = _touchPrevPos = _touchPos;
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchInProgress) return;
		if (_touchSelect) {
			mouseActionUpdate(_touchPos);
		} else if (!_touchScroll && (_touchPos - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchSelectTimer.cancel();
			_touchMaybeSelecting = false;
			_touchScroll = true;
			touchUpdateSpeed();
		}
		if (_touchScroll) {
			if (_touchScrollState == Ui::TouchScrollState::Manual) {
				touchScrollUpdated(_touchPos);
			} else if (_touchScrollState == Ui::TouchScrollState::Acceleration) {
				touchUpdateSpeed();
				_touchAccelerationTime = crl::now();
				if (_touchSpeed.isNull()) {
					_touchScrollState = Ui::TouchScrollState::Manual;
				}
			}
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchInProgress) {
			return;
		}
		_touchInProgress = false;
		auto weak = Ui::MakeWeak(this);
		const auto notMoved = (_touchPos - _touchStart).manhattanLength()
			< QApplication::startDragDistance();
		if (_touchSelect) {
			if (notMoved || _touchMaybeSelecting.current()) {
				mouseActionFinish(_touchPos, Qt::RightButton);
				auto contextMenu = QContextMenuEvent(
					QContextMenuEvent::Mouse,
					mapFromGlobal(_touchPos),
					_touchPos);
				showContextMenu(&contextMenu, true);
			}
			_touchScroll = false;
		} else if (_touchScroll) {
			if (_touchScrollState == Ui::TouchScrollState::Manual) {
				_touchScrollState = Ui::TouchScrollState::Auto;
				_touchPrevPosValid = false;
				_touchScrollTimer.callEach(15);
				_touchTime = crl::now();
			} else if (_touchScrollState == Ui::TouchScrollState::Auto) {
				_touchScrollState = Ui::TouchScrollState::Manual;
				_touchScroll = false;
				touchResetSpeed();
			} else if (_touchScrollState == Ui::TouchScrollState::Acceleration) {
				_touchScrollState = Ui::TouchScrollState::Auto;
				_touchWaitingAcceleration = false;
				_touchPrevPosValid = false;
			}
		} else if (notMoved) { // One short tap is like left mouse click.
			mouseActionStart(_touchPos, Qt::LeftButton);
			mouseActionFinish(_touchPos, Qt::LeftButton);
		}
		if (weak) {
			_touchSelectTimer.cancel();
			_touchMaybeSelecting = false;
			_touchSelect = false;
		}
	} break;
	}
}

void ListWidget::mouseMoveEvent(QMouseEvent *e) {
	static auto lastGlobalPosition = e->globalPos();
	auto reallyMoved = (lastGlobalPosition != e->globalPos());
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _mouseAction != MouseAction::None) {
		mouseReleaseEvent(e);
	}
	if (reallyMoved) {
		lastGlobalPosition = e->globalPos();
		if (!buttonsPressed
			|| (_scrollDateLink
				&& ClickHandler::getPressed() == _scrollDateLink)) {
			keepScrollDateForNow();
		}
	}
	mouseActionUpdate(e->globalPos());
}

void ListWidget::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void ListWidget::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	_delegate->listScrollTo(
		_visibleTop - (_touchPos - _touchPrevPos).y(),
		false);
	touchUpdateSpeed();
}

rpl::producer<bool> ListWidget::touchMaybeSelectingValue() const {
	return _touchMaybeSelecting.value();
}

void ListWidget::enterEventHook(QEnterEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return TWidget::enterEventHook(e);
}

void ListWidget::leaveEventHook(QEvent *e) {
	if (_reactionsManager) {
		_reactionsManager->updateButton({ .cursorLeft = true });
	}
	if (const auto view = _overElement) {
		if (_overState.pointState != PointState::Outside) {
			repaintItem(view);
			_overState.pointState = PointState::Outside;
		}
	}
	ClickHandler::clearActive();
	Ui::Tooltip::Hide();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return TWidget::leaveEventHook(e);
}

void ListWidget::updateDragSelection() {
	if (!_overState.itemId
		|| !_pressState.itemId
		|| hasSelectRestriction()) {
		clearDragSelection();
		return;
	} else if (_items.empty() || !_overElement || !_selectEnabled) {
		return;
	}
	const auto pressItem = session().data().message(_pressState.itemId);
	if (!pressItem) {
		return;
	}

	const auto overView = _overElement;
	const auto pressView = viewForItem(pressItem);
	const auto selectingUp = _delegate->listIsLessInOrder(
		overView->data(),
		pressItem);
	if (selectingUp != _dragSelectDirectionUp) {
		_dragSelectDirectionUp = selectingUp;
		_dragSelectAction = DragSelectAction::None;
	}
	const auto fromView = selectingUp ? overView : pressView;
	const auto tillView = selectingUp ? pressView : overView;
	const auto fromState = selectingUp ? _overState : _pressState;
	const auto tillState = selectingUp ? _pressState : _overState;
	updateDragSelection(fromView, fromState, tillView, tillState);
}

void ListWidget::onTouchSelect() {
	_touchSelect = true;
	_touchMaybeSelecting = true;
	mouseActionStart(_touchPos, Qt::LeftButton);
}

void ListWidget::updateDragSelection(
		const Element *fromView,
		const MouseState &fromState,
		const Element *tillView,
		const MouseState &tillState) {
	Expects(fromView != nullptr || tillView != nullptr);

	const auto delta = QApplication::startDragDistance();

	const auto includeFrom = [&] (
			not_null<const Element*> view,
			const MouseState &state) {
		const auto bottom = view->height() - view->marginBottom();
		return (state.point.y() < bottom - delta);
	};
	const auto includeTill = [&] (
			not_null<const Element*> view,
			const MouseState &state) {
		const auto top = view->marginTop();
		return (state.point.y() >= top + delta);
	};
	const auto includeSingleItem = [&] (
			not_null<const Element*> view,
			const MouseState &state1,
			const MouseState &state2) {
		const auto top = view->marginTop();
		const auto bottom = view->height() - view->marginBottom();
		const auto y1 = std::min(state1.point.y(), state2.point.y());
		const auto y2 = std::max(state1.point.y(), state2.point.y());
		return (y1 < bottom - delta && y2 >= top + delta)
			? (y2 - y1 >= delta)
			: false;
	};

	const auto from = [&] {
		const auto result = fromView ? ranges::find(
			_items,
			fromView,
			[](auto view) { return view.get(); }) : end(_items);
		return (result == end(_items))
			? begin(_items)
			: (fromView == tillView || includeFrom(fromView, fromState))
			? result
			: (result + 1);
	}();
	const auto till = [&] {
		if (fromView == tillView) {
			return (from == end(_items))
				? from
				: includeSingleItem(fromView, fromState, tillState)
				? (from + 1)
				: from;
		}
		const auto result = tillView ? ranges::find(
			_items,
			tillView,
			[](auto view) { return view.get(); }) : end(_items);
		return (result == end(_items))
			? end(_items)
			: includeTill(tillView, tillState)
			? (result + 1)
			: result;
	}();
	if (from < till) {
		updateDragSelection(from, till);
	} else {
		clearDragSelection();
	}
}

void ListWidget::updateDragSelection(
		std::vector<not_null<Element*>>::const_iterator from,
		std::vector<not_null<Element*>>::const_iterator till) {
	Expects(from < till);

	const auto &groups = session().data().groups();
	const auto changeItem = [&](not_null<HistoryItem*> item, bool add) {
		const auto itemId = item->fullId();
		if (add) {
			_dragSelected.emplace(itemId);
		} else {
			_dragSelected.remove(itemId);
		}
	};
	const auto changeGroup = [&](not_null<HistoryItem*> item, bool add) {
		if (const auto group = groups.find(item)) {
			for (const auto &item : group->items) {
				if (!_delegate->listIsItemGoodForSelection(item)) {
					return;
				}
			}
			for (const auto &item : group->items) {
				changeItem(item, add);
			}
		} else if (_delegate->listIsItemGoodForSelection(item)) {
			changeItem(item, add);
		}
	};
	const auto changeView = [&](not_null<Element*> view, bool add) {
		if (!view->isHidden()) {
			changeGroup(view->data(), add);
		}
	};
	for (auto i = begin(_items); i != from; ++i) {
		changeView(*i, false);
	}
	for (auto i = from; i != till; ++i) {
		changeView(*i, true);
	}
	for (auto i = till; i != end(_items); ++i) {
		changeView(*i, false);
	}

	ensureDragSelectAction(from, till);
	update();
}

void ListWidget::ensureDragSelectAction(
		std::vector<not_null<Element*>>::const_iterator from,
		std::vector<not_null<Element*>>::const_iterator till) {
	if (_dragSelectAction != DragSelectAction::None) {
		return;
	}
	const auto start = _dragSelectDirectionUp ? (till - 1) : from;
	const auto startId = (*start)->data()->fullId();
	_dragSelectAction = _selected.contains(startId)
		? DragSelectAction::Deselecting
		: DragSelectAction::Selecting;
	if (!_wasSelectedText
		&& !_dragSelected.empty()
		&& _dragSelectAction == DragSelectAction::Selecting) {
		_wasSelectedText = true;
		setFocus();
	}
}

void ListWidget::clearDragSelection() {
	_dragSelectAction = DragSelectAction::None;
	if (!_dragSelected.empty()) {
		_dragSelected.clear();
		update();
	}
}

void ListWidget::mouseActionStart(
		const QPoint &globalPosition,
		Qt::MouseButton button) {
	mouseActionUpdate(globalPosition);
	if (button != Qt::LeftButton) {
		return;
	}

	ClickHandler::pressed();
	if (_pressState != _overState) {
		if (_pressState.itemId != _overState.itemId) {
			repaintItem(_pressState.itemId);
		}
		_pressState = _overState;
		repaintItem(_overState.itemId);
	}
	_pressItemExact = _overItemExact;
	const auto pressElement = _overElement;

	_mouseAction = MouseAction::None;
	_pressWasInactive = Ui::WasInactivePress(window());
	if (_pressWasInactive) {
		Ui::MarkInactivePress(window(), false);
	}

	if (ClickHandler::getPressed()) {
		_mouseAction = MouseAction::PrepareDrag;
	} else if (hasSelectedItems()) {
		if (overSelectedItems()) {
			_mouseAction = MouseAction::PrepareDrag;
		} else if (!_pressWasInactive && !hasSelectRestriction()) {
			_mouseAction = MouseAction::PrepareSelect;
		}
	}
	if (_mouseAction == MouseAction::None && pressElement) {
		validateTrippleClickStartTime();
		TextState dragState;
		auto startDistance = (globalPosition - _trippleClickPoint).manhattanLength();
		auto validStartPoint = startDistance < QApplication::startDragDistance();
		if (_trippleClickStartTime != 0 && validStartPoint) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = pressElement->textState(_pressState.point, request);
			if (dragState.cursor == CursorState::Text) {
				setTextSelection(pressElement, TextSelection(
					dragState.symbol,
					dragState.symbol
				));
				_mouseTextSymbol = dragState.symbol;
				_mouseAction = MouseAction::Selecting;
				_mouseSelectType = TextSelectType::Paragraphs;
				mouseActionUpdate();
				_trippleClickStartTime = crl::now();
			}
		} else if (pressElement) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = pressElement->textState(_pressState.point, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			_mouseTextSymbol = dragState.symbol;
			if (isPressInSelectedText(dragState)) {
				_mouseAction = MouseAction::PrepareDrag; // start text drag
			} else if (!_pressWasInactive) {
				if (requiredToStartDragging(pressElement)
					&& _pressState.pointState != PointState::Outside) {
					_mouseAction = MouseAction::PrepareDrag;
				} else {
					if (dragState.afterSymbol) ++_mouseTextSymbol;
					if (!hasSelectedItems()
						&& _overState.pointState != PointState::Outside) {
						setTextSelection(pressElement, TextSelection(
							_mouseTextSymbol,
							_mouseTextSymbol));
						_mouseAction = MouseAction::Selecting;
					} else if (!hasSelectRestriction()) {
						_mouseAction = MouseAction::PrepareSelect;
					}
				}
			}
		}
	}
	if (!pressElement) {
		_mouseAction = MouseAction::None;
	} else if (_mouseAction == MouseAction::None) {
		mouseActionCancel();
	}
}

Reactions::ButtonParameters ListWidget::reactionButtonParameters(
		not_null<const Element*> view,
		QPoint position,
		const TextState &reactionState) const {
	if (!_useCornerReaction) {
		return {};
	}
	const auto top = itemTop(view);
	if (top < 0
		|| !view->data()->canReact()
		|| _mouseAction == MouseAction::Dragging
		|| inSelectionMode().inSelectionMode) {
		return {};
	}
	auto result = view->reactionButtonParameters(
		position,
		reactionState
	).translated({ 0, itemTop(view) });
	result.visibleTop = _visibleTop;
	result.visibleBottom = _visibleBottom;
	result.globalPointer = _mousePosition;
	return result;
}

void ListWidget::mouseActionUpdate(const QPoint &globalPosition) {
	_mousePosition = globalPosition;
	mouseActionUpdate();
}

void ListWidget::mouseActionCancel() {
	_pressState = MouseState();
	_pressItemExact = nullptr;
	_mouseAction = MouseAction::None;
	clearDragSelection();
	_wasSelectedText = false;
	_selectScroll.cancel();
}

void ListWidget::mouseActionFinish(
		const QPoint &globalPosition,
		Qt::MouseButton button) {
	mouseActionUpdate(globalPosition);

	auto pressState = base::take(_pressState);
	base::take(_pressItemExact);
	repaintItem(pressState.itemId);

	const auto toggleByHandler = [&](const ClickHandlerPtr &handler) {
		// If we are in selecting items mode perhaps we want to
		// toggle selection instead of activating the pressed link.
		return _overElement
			&& _overElement->toggleSelectionByHandlerClick(handler);
	};

	auto activated = ClickHandler::unpressed();
	auto simpleSelectionChange = pressState.itemId
		&& !_pressWasInactive
		&& (button != Qt::RightButton)
		&& (_mouseAction == MouseAction::PrepareSelect
			|| _mouseAction == MouseAction::PrepareDrag);
	auto needItemSelectionToggle = simpleSelectionChange
		&& (!activated || toggleByHandler(activated))
		&& hasSelectedItems();
	auto needTextSelectionClear = simpleSelectionChange
		&& hasSelectedText();

	_wasSelectedText = false;

	if (_mouseAction == MouseAction::Dragging
		|| _mouseAction == MouseAction::Selecting
		|| needItemSelectionToggle) {
		activated = nullptr;
	} else if (activated) {
		mouseActionCancel();
		ActivateClickHandler(window(), activated, {
			button,
			QVariant::fromValue(
				prepareClickHandlerContext(pressState.itemId))
		});
		return;
	}
	if (needItemSelectionToggle) {
		if (const auto item = session().data().message(pressState.itemId)) {
			clearTextSelection();
			if (pressState.pointState == PointState::GroupPart) {
				changeSelection(
					_selected,
					_overItemExact ? _overItemExact : item,
					SelectAction::Invert);
			} else {
				changeSelectionAsGroup(
					_selected,
					item,
					SelectAction::Invert);
			}
			pushSelectedItems();
		}
	} else if (needTextSelectionClear) {
		clearTextSelection();
	} else if (_mouseAction == MouseAction::Selecting) {
		if (!_dragSelected.empty()) {
			applyDragSelection();
		} else if (_selectedTextItem && !_pressWasInactive) {
			if (_selectedTextRange.from == _selectedTextRange.to) {
				clearTextSelection();
				_delegate->listWindowSetInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseSelectType = TextSelectType::Letters;
	_selectScroll.cancel();

	if (QGuiApplication::clipboard()->supportsSelection()
		&& _selectedTextItem
		&& _selectedTextRange.from != _selectedTextRange.to
		&& !hasCopyRestriction(_selectedTextItem)) {
		if (const auto view = viewForItem(_selectedTextItem)) {
			TextUtilities::SetClipboardText(
				view->selectedText(_selectedTextRange),
				QClipboard::Selection);
		}
	}
}

ClickHandlerContext ListWidget::prepareClickHandlerContext(FullMsgId id) {
	return {
		.itemId = id,
		.elementDelegate = [weak = Ui::MakeWeak(this)] {
			return weak
				? (ElementDelegate*)weak
				: nullptr;
		},
		.sessionWindow = base::make_weak(controller()),
	};
}

int ListWidget::SelectionViewOffset(
		not_null<const ListWidget*> inner,
		not_null<const Element*> view) {
	if (inner->_lastInSelectionMode) {
		const auto translation
			= Element::AdditionalSpaceForSelectionCheckbox(view);
		const auto progress = inner->_inSelectionModeAnimation.value(1.);
		return translation * progress;
	}
	return 0;
}


void ListWidget::mouseActionUpdate() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(
		std::clamp(mousePosition.x(), 0, width()),
		std::clamp(mousePosition.y(), _visibleTop, _visibleBottom));

	const auto reactionState = _reactionsManager
		? _reactionsManager->buttonTextState(point)
		: TextState();
	const auto reactionItem = session().data().message(reactionState.itemId);
	const auto reactionView = viewForItem(reactionItem);
	const auto view = reactionView
		? reactionView
		: strictFindItemByY(point.y());
	const auto item = view ? view->data().get() : nullptr;
	if (view) {
		point -= QPoint(SelectionViewOffset(this, view), 0);
	}
	const auto itemPoint = mapPointToItem(point, view);
	_overState = MouseState(
		item ? item->fullId() : FullMsgId(),
		view ? view->height() : 0,
		itemPoint,
		view ? view->pointState(itemPoint) : PointState::Outside);
	_overItemExact = nullptr;
	const auto viewChanged = (_overElement != view);
	if (viewChanged) {
		repaintItem(_overElement);
		_overElement = view;
		repaintItem(_overElement);
	}
	if (_reactionsManager) {
		_reactionsManager->updateButton(view
			? reactionButtonParameters(
				view,
				itemPoint,
				reactionState)
			: Reactions::ButtonParameters());
	}
	if (viewChanged && view) {
		_reactionsItem = item;
	}

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto inTextSelection = (_overState.pointState != PointState::Outside)
		&& (_overState.itemId == _pressState.itemId)
		&& hasSelectedText();
	const auto overReaction = reactionView && reactionState.link;
	if (overReaction) {
		dragState = reactionState;
		lnkhost = reactionView;
	} else if (view) {
		auto cursorDeltaLength = [&] {
			auto cursorDelta = (_overState.point - _pressState.point);
			return cursorDelta.manhattanLength();
		};
		auto dragStartLength = [] {
			return QApplication::startDragDistance();
		};
		if (_overState.itemId != _pressState.itemId
			|| cursorDeltaLength() >= dragStartLength()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [this] { performDrag(); });
			} else if (_mouseAction == MouseAction::PrepareSelect) {
				_mouseAction = MouseAction::Selecting;
			}
		}
		StateRequest request;
		if (_mouseAction == MouseAction::Selecting) {
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
		} else {
			inTextSelection = false;
		}
		if (base::IsAltPressed()) {
			request.flags &= ~Ui::Text::StateRequest::Flag::LookupLink;
		}

		const auto dateHeight = st::msgServicePadding.bottom()
			+ st::msgServiceFont->height
			+ st::msgServicePadding.top();
		const auto scrollDateOpacity = _scrollDateOpacity.value(_scrollDateShown ? 1. : 0.);
		enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
			// stop enumeration if the date is above our point
			if (dateTop + dateHeight <= point.y()) {
				return false;
			}

			const auto displayDate = view->displayDate();
			auto dateInPlace = displayDate;
			if (dateInPlace) {
				const auto correctDateTop = itemtop + st::msgServiceMargin.top();
				dateInPlace = (dateTop < correctDateTop + dateHeight);
			}

			// stop enumeration if we've found a date under the cursor
			if (dateTop <= point.y()) {
				auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
				if (opacity > 0.) {
					auto dateWidth = 0;
					if (const auto date = view->Get<HistoryView::DateBadge>()) {
						dateWidth = date->width;
					} else {
						dateWidth = st::msgServiceFont->width(langDayOfMonthFull(view->dateTime().date()));
					}
					dateWidth += st::msgServicePadding.left() + st::msgServicePadding.right();
					auto dateLeft = st::msgServiceMargin.left();
					auto maxwidth = view->width();
					if (_isChatWide) {
						maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
					}
					auto widthForDate = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

					dateLeft += (widthForDate - dateWidth) / 2;

					if (point.x() >= dateLeft && point.x() < dateLeft + dateWidth) {
						_delegate->listUpdateDateLink(_scrollDateLink, view);
						dragState = TextState(
							nullptr,
							_scrollDateLink);
						_overItemExact = session().data().message(dragState.itemId);
						lnkhost = view;
					}
				}
				return false;
			}
			return true;
		});
		if (!dragState.link) {
			dragState = view->textState(itemPoint, request);
			_overItemExact = session().data().message(dragState.itemId);
			lnkhost = view;
			if (!dragState.link
				&& itemPoint.x() >= st::historyPhotoLeft
				&& itemPoint.x() < st::historyPhotoLeft + st::msgPhotoSize) {
				if (view->hasFromPhoto()) {
					enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
						// stop enumeration if the userpic is below our point
						if (userpicTop > point.y()) {
							return false;
						}

						// stop enumeration if we've found a userpic under the cursor
						if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
							dragState = TextState(nullptr, view->fromPhotoLink());
							_overItemExact = nullptr;
							lnkhost = view;
							return false;
						}
						return true;
					});
				}
			}
		}
	}
	const auto lnkChanged = ClickHandler::setActive(dragState.link, lnkhost);
	if (lnkChanged || dragState.cursor != _mouseCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.link
		|| dragState.cursor == CursorState::Date
		|| dragState.cursor == CursorState::Forwarded) {
		Ui::Tooltip::Show(1000, this);
	}

	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		auto cursor = computeMouseCursor();
		if (_cursor != cursor) {
			setCursor((_cursor = cursor));
		}
	} else if (view) {
		if (_mouseAction == MouseAction::Selecting) {
			if (inTextSelection) {
				auto second = dragState.symbol;
				if (dragState.afterSymbol
					&& _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selection = TextSelection(
					qMin(second, _mouseTextSymbol),
					qMax(second, _mouseTextSymbol)
				);
				if (_mouseSelectType != TextSelectType::Letters) {
					selection = view->adjustSelection(
						selection,
						_mouseSelectType);
				}
				setTextSelection(view, selection);
				clearDragSelection();
			} else if (_pressState.itemId) {
				updateDragSelection();
			}
		} else if (_mouseAction == MouseAction::Dragging) {
		}
	}

	// Voice message seek support.
	if (_pressState.pointState != PointState::Outside
		&& ClickHandler::getPressed()) {
		if (const auto item = session().data().message(_pressState.itemId)) {
			if (const auto view = viewForItem(item)) {
				auto adjustedPoint = mapPointToItem(point, view);
				view->updatePressed(adjustedPoint);
			}
		}
	}

	if (_mouseAction == MouseAction::Selecting) {
		_selectScroll.checkDeltaScroll(
			mousePosition,
			_visibleTop,
			_visibleBottom);
	} else {
		_selectScroll.cancel();
	}
}

style::cursor ListWidget::computeMouseCursor() const {
	if (ClickHandler::getPressed() || ClickHandler::getActive()) {
		return style::cur_pointer;
	} else if (!hasSelectedItems()
		&& (_mouseCursorState == CursorState::Text)) {
		return style::cur_text;
	}
	return style::cur_default;
}

std::unique_ptr<QMimeData> ListWidget::prepareDrag() {
	if (_mouseAction != MouseAction::Dragging) {
		return nullptr;
	}
	auto pressedHandler = ClickHandler::getPressed();
	if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.get())
		|| hasCopyRestriction()) {
		return nullptr;
	}

	const auto pressedItem = session().data().message(_pressState.itemId);
	const auto pressedView = viewForItem(pressedItem);
	const auto uponSelected = pressedView && isInsideSelection(
		pressedView,
		_pressItemExact ? _pressItemExact : pressedItem,
		_pressState);

	auto urls = QList<QUrl>();
	const auto selectedText = [&] {
		if (uponSelected) {
			return getSelectedText();
		} else if (pressedHandler) {
			//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
			//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
			//}
			return TextForMimeData::Simple(pressedHandler->dragText());
		}
		return TextForMimeData();
	}();
	if (auto mimeData = TextUtilities::MimeDataFromText(selectedText)) {
		clearDragSelection();
		_selectScroll.cancel();

		if (!urls.isEmpty()) {
			mimeData->setUrls(urls);
		}
		if (uponSelected && !_delegate->listAllowsDragForward()) {
			const auto canForwardAll = [&] {
				for (const auto &[itemId, data] : _selected) {
					if (!data.canForward) {
						return false;
					}
				}
				return true;
			}();
			auto items = canForwardAll
				? collectSelectedIds()
				: MessageIdsList();
			if (!items.empty()) {
				session().data().setMimeForwardIds(std::move(items));
				mimeData->setData(u"application/x-td-forward"_q, "1");
			}
		}
		return mimeData;
	} else if (pressedView) {
		auto forwardIds = MessageIdsList();
		const auto exactItem = _pressItemExact
			? _pressItemExact
			: pressedItem;
		if (_mouseCursorState == CursorState::Date) {
			if (_overElement->data()->allowsForward()) {
				forwardIds = session().data().itemOrItsGroup(
					_overElement->data());
			}
		} else if (_pressState.pointState == PointState::GroupPart) {
			if (exactItem->allowsForward()) {
				forwardIds = MessageIdsList(1, exactItem->fullId());
			}
		} else if (const auto media = pressedView->media()) {
			if (pressedView->data()->allowsForward()
				&& media->dragItemByHandler(pressedHandler)) {
				forwardIds = MessageIdsList(1, exactItem->fullId());
			}
		}

		if (pressedHandler) {
			const auto lnkDocument = reinterpret_cast<DocumentData*>(
				pressedHandler->property(
					kDocumentLinkMediaProperty).toULongLong());
			if (lnkDocument) {
				const auto filepath = lnkDocument->filepath(true);
				if (!filepath.isEmpty()) {
					urls.push_back(QUrl::fromLocalFile(filepath));
				}
			}
		}

		if (forwardIds.empty() && urls.isEmpty()) {
			return nullptr;
		}

		auto result = std::make_unique<QMimeData>();
		if (!forwardIds.empty()) {
			session().data().setMimeForwardIds(std::move(forwardIds));
			result->setData(u"application/x-td-forward"_q, "1");
		}
		if (!urls.isEmpty()) {
			result->setUrls(urls);
		}
		return result;
	}
	return nullptr;
}

void ListWidget::performDrag() {
	if (auto mimeData = prepareDrag()) {
		// This call enters event loop and can destroy any QObject.
		if (_reactionsManager) {
			_reactionsManager->updateButton({});
		}
		_delegate->listLaunchDrag(
			std::move(mimeData),
			crl::guard(this, [=] { mouseActionUpdate(QCursor::pos()); }));
	}
}

int ListWidget::itemTop(not_null<const Element*> view) const {
	return _itemsTop + view->y();
}

void ListWidget::repaintItem(const Element *view) {
	if (!view) {
		return;
	}
	const auto top = itemTop(view);
	const auto range = view->verticalRepaintRange();
	update(0, top + range.top, width(), range.height);
	const auto id = view->data()->fullId();
	const auto area = _reactionsManager
		? _reactionsManager->lookupEffectArea(id)
		: std::nullopt;
	if (area) {
		update(*area);
	}
}

void ListWidget::repaintItem(FullMsgId itemId) {
	if (const auto view = viewForItem(itemId)) {
		repaintItem(view);
	}
}

void ListWidget::resizeItem(not_null<Element*> view) {
	const auto index = ranges::find(_items, view) - begin(_items);
	if (index < int(_items.size())) {
		refreshAttachmentsAtIndex(index);
	}
}

void ListWidget::refreshAttachmentsAtIndex(int index) {
	Expects(index >= 0 && index < _items.size());

	const auto from = [&] {
		if (index > 0) {
			for (auto i = index - 1; i != 0; --i) {
				if (!_items[i]->isHidden()) {
					return i;
				}
			}
		}
		return index;
	}();
	const auto till = [&] {
		const auto count = int(_items.size());
		for (auto i = index + 1; i != count; ++i) {
			if (!_items[i]->isHidden()) {
				return i + 1;
			}
		}
		return index + 1;
	}();
	refreshAttachmentsFromTill(from, till);
}

void ListWidget::refreshAttachmentsFromTill(int from, int till) {
	Expects(from >= 0 && from <= till && till <= int(_items.size()));

	const auto guard = gsl::finally([&] {
		updateSize();
	});
	if (from == till) {
		return;
	}
	auto view = _items[from].get();
	for (auto i = from + 1; i != till; ++i) {
		const auto next = _items[i].get();
		if (next->isHidden()) {
			next->setDisplayDate(false);
		} else {
			const auto viewDate = view->dateTime();
			const auto nextDate = next->dateTime();
			next->setDisplayDate(_context != Context::ShortcutMessages
				&& nextDate.date() != viewDate.date());
			auto attached = next->computeIsAttachToPrevious(view);
			next->setAttachToPrevious(attached, view);
			view->setAttachToNext(attached, next);
			view = next;
		}
	}
	if (till == int(_items.size())) {
		_items.back()->setAttachToNext(false);
	}
}

void ListWidget::refreshItem(not_null<const Element*> view) {
	const auto i = ranges::find(_items, view);
	const auto index = i - begin(_items);
	if (index < int(_items.size())) {
		const auto item = view->data();
		const auto was = [&]() -> std::unique_ptr<Element> {
			if (const auto i = _views.find(item); i != end(_views)) {
				auto result = std::move(i->second);
				_views.erase(i);
				return result;
			}
			return nullptr;
		}();
		const auto &[i, ok] = _views.emplace(
			item,
			item->createView(this, was.get()));
		const auto now = i->second.get();
		_items[index] = now;

		viewReplaced(view, i->second.get());

		refreshAttachmentsAtIndex(index);
	}
}

void ListWidget::viewReplaced(not_null<const Element*> was, Element *now) {
	if (_visibleTopItem == was) _visibleTopItem = now;
	if (_scrollDateLastItem == was) _scrollDateLastItem = now;
	if (_overElement == was) _overElement = now;
	if (_bar.element == was.get()) {
		const auto bar = _bar.element->Get<UnreadBar>();
		_bar.element = now;
		if (now && bar) {
			_bar.element->createUnreadBar(_barText.value());
		}
	}
	const auto i = _itemRevealPending.find(was);
	if (i != end(_itemRevealPending)) {
		_itemRevealPending.erase(i);
		if (now) {
			_itemRevealPending.emplace(now);
		}
	}
	const auto j = _itemRevealAnimations.find(was);
	if (j != end(_itemRevealAnimations)) {
		auto data = std::move(j->second);
		_itemRevealAnimations.erase(j);
		if (now) {
			_itemRevealAnimations.emplace(now, std::move(data));
		} else {
			revealItemsCallback();
		}
	}
}

void ListWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (_reactionsItem.current() == item) {
		_reactionsItem = nullptr;
	}
	if (_selectedTextItem == item) {
		clearTextSelection();
	}
	if (_overItemExact == item) {
		_overItemExact = nullptr;
	}
	if (_pressItemExact == item) {
		_pressItemExact = nullptr;
	}
	const auto i = _views.find(item);
	if (i == end(_views)) {
		return;
	}

	saveScrollState();
	const auto guard = gsl::finally([&] {
		restoreScrollState();
	});

	const auto view = i->second.get();
	_items.erase(
		ranges::remove(_items, view, [](auto view) { return view.get(); }),
		end(_items));
	viewReplaced(view, nullptr);
	_views.erase(i);

	if (_reactionsManager) {
		_reactionsManager->remove(item->fullId());
	}
	updateItemsGeometry();
}

QPoint ListWidget::mapPointToItem(
		QPoint point,
		const Element *view) const {
	if (!view) {
		return QPoint();
	}
	return point - QPoint(0, itemTop(view));
}

rpl::producer<FullMsgId> ListWidget::editMessageRequested() const {
	return _requestedToEditMessage.events();
}

void ListWidget::editMessageRequestNotify(FullMsgId item) const {
	_requestedToEditMessage.fire(std::move(item));
}

bool ListWidget::lastMessageEditRequestNotify() const {
	const auto now = base::unixtime::now();
	auto proj = [&](not_null<Element*> view) {
		return view->data()->allowsEdit(now)
			&& !view->data()->isUploading();
	};
	const auto &list = ranges::views::reverse(_items);
	const auto it = ranges::find_if(list, std::move(proj));
	if (it == end(list)) {
		return false;
	} else {
		const auto item
			= session().data().groups().findItemToEdit((*it)->data()).get();
		editMessageRequestNotify(item->fullId());
		return true;
	}
}

auto ListWidget::replyToMessageRequested() const
-> rpl::producer<ReplyToMessageRequest> {
	return _requestedToReplyToMessage.events();
}

void ListWidget::replyToMessageRequestNotify(
		FullReplyTo to,
		bool forceAnotherChat) {
	_requestedToReplyToMessage.fire({ std::move(to), forceAnotherChat });
}

rpl::producer<FullMsgId> ListWidget::readMessageRequested() const {
	return _requestedToReadMessage.events();
}

rpl::producer<FullMsgId> ListWidget::showMessageRequested() const {
	return _requestedToShowMessage.events();
}

void ListWidget::replyNextMessage(FullMsgId fullId, bool next) {
	const auto reply = [&](Element *view) {
		if (view) {
			const auto newFullId = view->data()->fullId();
			if (!view->data()->isRegular()) {
				return replyNextMessage(newFullId, next);
			}
			replyToMessageRequestNotify({ newFullId });
			_requestedToShowMessage.fire_copy(newFullId);
		} else {
			replyToMessageRequestNotify({});
			_highlighter.clear();
		}
	};
	const auto replyFirst = [&] {
		reply(next ? nullptr : _items.back().get());
	};
	if (!fullId) {
		replyFirst();
		return;
	}

	auto proj = [&](not_null<Element*> view) {
		return view->data()->fullId() == fullId;
	};
	const auto &list = ranges::views::reverse(_items);
	const auto it = ranges::find_if(list, std::move(proj));
	if (it == end(list)) {
		replyFirst();
		return;
	} else {
		const auto nextIt = it + (next ? -1 : 1);
		if (nextIt == end(list)) {
			return;
		} else if (next && (it == begin(list))) {
			reply(nullptr);
		} else {
			reply(nextIt->get());
		}
	}
}

void ListWidget::setEmptyInfoWidget(base::unique_qptr<Ui::RpWidget> &&w) {
	_emptyInfo = std::move(w);
	if (_emptyInfo) {
		_emptyInfo->setVisible(isEmpty());
	}
}

void ListWidget::overrideIsChatWide(bool isWide) {
	_overrideIsChatWide = isWide;
}

ListWidget::~ListWidget() {
	// Destroy child widgets first, because they may invoke leaveEvent-s.
	_emptyInfo = nullptr;
}

void ConfirmDeleteSelectedItems(not_null<ListWidget*> widget) {
	const auto items = widget->getSelectedItems();
	if (items.empty()) {
		return;
	}
	for (const auto &item : items) {
		if (!item.canDelete) {
			return;
		}
	}
	auto box = Box<DeleteMessagesBox>(
		&widget->session(),
		widget->getSelectedIds());
	box->setDeleteConfirmedCallback(crl::guard(widget, [=] {
		widget->cancelSelection();
	}));
	widget->controller()->show(std::move(box));
}

void ConfirmForwardSelectedItems(not_null<ListWidget*> widget) {
	const auto items = widget->getSelectedItems();
	if (items.empty()) {
		return;
	}
	for (const auto &item : items) {
		if (!item.canForward) {
			return;
		}
	}
	auto ids = widget->getSelectedIds();
	const auto weak = Ui::MakeWeak(widget);
	Window::ShowForwardMessagesBox(widget->controller(), std::move(ids), [=] {
		if (const auto strong = weak.data()) {
			strong->cancelSelection();
		}
	});
}

void ConfirmSendNowSelectedItems(not_null<ListWidget*> widget) {
	const auto items = widget->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto navigation = widget->controller();
	const auto history = [&]() -> History* {
		auto result = (History*)nullptr;
		auto &data = navigation->session().data();
		for (const auto &item : items) {
			if (!item.canSendNow) {
				return nullptr;
			}
			const auto message = data.message(item.msgId);
			if (message) {
				result = message->history();
			}
		}
		return result;
	}();
	if (!history) {
		return;
	}
	const auto clearSelection = [weak = Ui::MakeWeak(widget)] {
		if (const auto strong = weak.data()) {
			strong->cancelSelection();
		}
	};
	Window::ShowSendNowMessagesBox(
		navigation,
		history,
		widget->getSelectedIds(),
		clearSelection);
}

CopyRestrictionType CopyRestrictionTypeFor(
		not_null<PeerData*> peer,
		HistoryItem *item) {
	return (peer->allowsForwarding() && (!item || !item->forbidsForward()))
		? CopyRestrictionType::None
		: peer->isBroadcast()
		? CopyRestrictionType::Channel
		: CopyRestrictionType::Group;
}

CopyRestrictionType CopyMediaRestrictionTypeFor(
		not_null<PeerData*> peer,
		not_null<HistoryItem*> item) {
	if (const auto all = CopyRestrictionTypeFor(peer, item)
		; all != CopyRestrictionType::None) {
		return all;
	}
	return !item->forbidsSaving()
		? CopyRestrictionType::None
		: peer->isBroadcast()
		? CopyRestrictionType::Channel
		: CopyRestrictionType::Group;
}

CopyRestrictionType SelectRestrictionTypeFor(
		not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return chat->canDeleteMessages()
			? CopyRestrictionType::None
			: CopyRestrictionTypeFor(peer);
	} else if (const auto channel = peer->asChannel()) {
		return channel->canDeleteMessages()
			? CopyRestrictionType::None
			: CopyRestrictionTypeFor(peer);
	}
	return CopyRestrictionType::None;
}

} // namespace HistoryView
