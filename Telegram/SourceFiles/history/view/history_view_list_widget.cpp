/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_list_widget.h"

#include "history/history_message.h"
#include "history/history_item_components.h"
#include "history/history_item_text.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_cursor_state.h"
#include "chat_helpers/message_field.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/application.h"
#include "apiwrap.h"
#include "layout.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "main/main_session.h"
#include "ui/widgets/popup_menu.h"
#include "ui/toast/toast.h"
#include "lang/lang_keys.h"
#include "boxes/peers/edit_participant_box.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_media_types.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "styles/style_history.h"

namespace HistoryView {
namespace {

constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadIfLessThanScreens = 2;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;

} // namespace

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
		Assert(itemTop(from->get()) < _visibleBottom);
	}

	while (true) {
		auto view = from->get();
		auto itemtop = itemTop(view);
		auto itembottom = itemtop + view->height();

		// Binary search should've skipped all the items that are above / below the visible area.
		if (TopToBottom) {
			Assert(itembottom > _visibleTop);
		} else {
			Assert(itemtop < _visibleBottom);
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
		auto message = view->data()->toHistoryMessage();
		if (!message) return true;

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
	not_null<Window::SessionController*> controller,
	not_null<ListDelegate*> delegate)
: RpWidget(parent)
, _delegate(delegate)
, _controller(controller)
, _context(_delegate->listContext())
, _itemAverageHeight(itemMinimalHeight())
, _scrollDateCheck([this] { scrollDateCheck(); })
, _applyUpdatedScrollState([this] { applyUpdatedScrollState(); })
, _selectEnabled(_delegate->listAllowsMultiSelect())
, _highlightTimer([this] { updateHighlightedMessage(); }) {
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([this] { scrollDateHideByTimer(); });
	session().data().viewRepaintRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			repaintItem(view);
		}
	}, lifetime());
	session().data().viewResizeRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			resizeItem(view);
		}
	}, lifetime());
	session().data().itemViewRefreshRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			refreshItem(view);
		}
	}, lifetime());
	session().data().viewLayoutChanged(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			if (view->isUnderCursor()) {
				mouseActionUpdate();
			}
		}
	}, lifetime());
	session().data().animationPlayInlineRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			if (const auto media = view->media()) {
				media->playAnimation();
			}
		}
	}, lifetime());
	session().data().itemRemoved(
	) | rpl::start_with_next(
		[this](auto item) { itemRemoved(item); },
		lifetime());
	subscribe(session().data().queryItemVisibility(), [this](const Data::Session::ItemVisibilityQuery &query) {
		if (const auto view = viewForItem(query.item)) {
			const auto top = itemTop(view);
			if (top >= 0
				&& top + view->height() > _visibleTop
				&& top < _visibleBottom) {
				*query.isVisible = true;
			}
		}
	});
}

Main::Session &ListWidget::session() const {
	return _controller->session();
}

not_null<ListDelegate*> ListWidget::delegate() const {
	return _delegate;
}

void ListWidget::refreshViewer() {
	_viewerLifetime.destroy();
	_delegate->listSource(
		_aroundPosition,
		_idsLimit,
		_idsLimit
	) | rpl::start_with_next([=](Data::MessagesSlice &&slice) {
		_slice = std::move(slice);
		refreshRows();
	}, _viewerLifetime);
}

void ListWidget::refreshRows() {
	saveScrollState();

	_items.clear();
	_items.reserve(_slice.ids.size());
	for (const auto &fullId : _slice.ids) {
		if (const auto item = session().data().message(fullId)) {
			_items.push_back(enforceViewForItem(item));
		}
	}
	updateAroundPositionFromRows();

	updateItemsGeometry();
	checkUnreadBarCreation();
	restoreScrollState();
	mouseActionUpdate(QCursor::pos());
	_delegate->listContentRefreshed();
}

std::optional<int> ListWidget::scrollTopForPosition(
		Data::MessagePosition position) const {
	if (position == Data::MaxMessagePosition) {
		if (loadedAtBottom()) {
			return height();
		}
		return std::nullopt;
	} else if (_items.empty()
		|| isBelowPosition(position)
		|| isAbovePosition(position)) {
		return std::nullopt;
	}
	const auto index = findNearestItem(position);
	const auto view = _items[index];
	return scrollTopForView(_items[index]);
}

std::optional<int> ListWidget::scrollTopForView(
		not_null<Element*> view) const {
	if (view->isHiddenByGroup()) {
		if (const auto group = session().data().groups().find(view->data())) {
			if (const auto leader = viewForItem(group->items.back())) {
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

void ListWidget::animatedScrollTo(
		int scrollTop,
		Data::MessagePosition attachPosition,
		int delta,
		AnimatedScroll type) {
	_scrollToAnimation.stop();
	if (!delta || _items.empty()) {
		_delegate->listScrollTo(scrollTop);
		return;
	}
	const auto index = findNearestItem(attachPosition);
	Assert(index >= 0 && index < int(_items.size()));
	const auto attachTo = _items[index];
	const auto attachToId = attachTo->data()->fullId();
	const auto transition = (type == AnimatedScroll::Full)
		? anim::sineInOut
		: anim::easeOutCubic;
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

void ListWidget::scrollToAnimationCallback(
		FullMsgId attachToId,
		int relativeTo) {
	const auto attachTo = session().data().message(attachToId);
	const auto attachToView = viewForItem(attachTo);
	if (!attachToView) {
		_scrollToAnimation.stop();
	} else {
		const auto current = int(std::round(_scrollToAnimation.value(
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

void ListWidget::highlightMessage(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (const auto view = viewForItem(item)) {
			_highlightStart = crl::now();
			_highlightedMessageId = itemId;
			_highlightTimer.callEach(AnimationTimerDelta);

			repaintItem(view);
		}
	}
}

void ListWidget::updateHighlightedMessage() {
	if (const auto item = session().data().message(_highlightedMessageId)) {
		if (const auto view = viewForItem(item)) {
			repaintItem(view);
			auto duration = st::activeFadeInDuration + st::activeFadeOutDuration;
			if (crl::now() - _highlightStart <= duration) {
				return;
			}
		}
	}
	_highlightTimer.cancel();
	_highlightedMessageId = FullMsgId();
}

void ListWidget::checkUnreadBarCreation() {
	if (!_unreadBarElement) {
		if (const auto index = _delegate->listUnreadBarView(_items)) {
			_unreadBarElement = _items[*index].get();
			_unreadBarElement->setUnreadBarCount(UnreadBar::kCountUnknown);
			refreshAttachmentsAtIndex(*index);
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
	}
	if (!_scrollTopState.item) {
		if (!_unreadBarElement) {
			return;
		}
		_scrollTopState.item = _unreadBarElement->data()->position();
		_scrollTopState.shift = st::lineWidth + st::historyUnreadBarMargin;
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
		not_null<HistoryItem*> item) {
	if (const auto view = viewForItem(item)) {
		return view;
	}
	const auto [i, ok] = _views.emplace(
		item,
		item->createView(this));
	return i->second.get();
}

void ListWidget::updateAroundPositionFromRows() {
	_aroundIndex = findNearestItem(_aroundPosition);
	if (_aroundIndex >= 0) {
		const auto newPosition = _items[_aroundIndex]->data()->position();
		if (_aroundPosition != newPosition) {
			_aroundPosition = newPosition;
			crl::on_main(this, [=] { refreshViewer(); });
		}
	}
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

	if (initializing) {
		checkUnreadBarCreation();
	}
	updateVisibleTopItem();
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		scrollDateHideByTimer();
	}
	_controller->floatPlayerAreaUpdated().notify(true);
	_applyUpdatedScrollState.call();
}

void ListWidget::applyUpdatedScrollState() {
	checkMoveToOtherViewer();
	_delegate->listVisibleItemsChanged(collectVisibleItems());
}

void ListWidget::updateVisibleTopItem() {
	if (_visibleBottom == height()) {
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
		_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
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
	_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
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
		const auto [itemId, selection] = item;
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
	return ranges::view::all(
		selected
	) | ranges::view::transform([](const SelectedItem &item) {
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
	for (const auto other : group->items) {
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
		for (const auto other : group->items) {
			if (!isGoodForSelection(applyTo, other, already)) {
				return false;
			}
		}
		return true;
	}();
	if (action == SelectAction::Select && canSelect) {
		for (const auto other : group->items) {
			addToSelection(applyTo, other);
		}
	} else {
		for (const auto other : group->items) {
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

bool ListWidget::requiredToStartDragging(
		not_null<Element*> view) const {
	if (_mouseCursorState == CursorState::Date) {
		return true;
	} else if (const auto media = view->media()) {
		if (media->dragItem()) {
			return true;
		}
	}
	return false;
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
	if (const auto view = viewForItem(item)) {
		clearTextSelection();
		changeSelection(
			_selected,
			item,
			SelectAction::Select);
		pushSelectedItems();
	}
}

void ListWidget::selectItemAsGroup(not_null<HistoryItem*> item) {
	if (const auto view = viewForItem(item)) {
		clearTextSelection();
		changeSelectionAsGroup(
			_selected,
			item,
			SelectAction::Select);
		pushSelectedItems();
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
	return loadedAtTop() && loadedAtBottom() && (_itemsHeight == 0);
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

	auto topItem = findItemByY(_visibleTop);
	auto bottomItem = findItemByY(_visibleBottom);
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
	auto preloadAroundMessage = [&](not_null<Element*> view) {
		auto preloadRequired = false;
		auto itemPosition = view->data()->position();
		auto itemIndex = ranges::find(_items, view) - begin(_items);
		Assert(itemIndex < _items.size());

		if (!preloadRequired) {
			preloadRequired = (_idsLimit < preloadIdsLimitMin);
		}
		if (!preloadRequired) {
			Assert(_aroundIndex >= 0);
			auto delta = std::abs(itemIndex - _aroundIndex);
			preloadRequired = (delta >= minUniversalIdDelta);
		}
		if (preloadRequired) {
			_idsLimit = preloadIdsLimit;
			_aroundPosition = itemPosition;
			_aroundIndex = itemIndex;
			refreshViewer();
		}
	};

	if (preloadTop && !topLoaded) {
		preloadAroundMessage(topItem);
	} else if (preloadBottom && !bottomLoaded) {
		preloadAroundMessage(bottomItem);
	}
}

QString ListWidget::tooltipText() const {
	const auto item = (_overElement && _mouseAction == MouseAction::None)
		? _overElement->data().get()
		: nullptr;
	if (_mouseCursorState == CursorState::Date && item) {
		return _overElement->dateTime().toString(
			QLocale::system().dateTimeFormat(QLocale::LongFormat));
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

Context ListWidget::elementContext() {
	return _delegate->listContext();
}

std::unique_ptr<Element> ListWidget::elementCreate(
		not_null<HistoryMessage*> message) {
	return std::make_unique<Message>(this, message);
}

std::unique_ptr<Element> ListWidget::elementCreate(
		not_null<HistoryService*> message) {
	return std::make_unique<Service>(this, message);
}

bool ListWidget::elementUnderCursor(
		not_null<const HistoryView::Element*> view) {
	return (_overElement == view);
}

void ListWidget::elementAnimationAutoplayAsync(
		not_null<const Element*> view) {
	crl::on_main(this, [this, msgId = view->data()->fullId()]{
		if (const auto view = viewForItem(msgId)) {
			if (const auto media = view->media()) {
				media->autoplayAnimation();
			}
		}
	});
}

crl::time ListWidget::elementHighlightTime(
		not_null<const HistoryView::Element*> element) {
	if (element->data()->fullId() == _highlightedMessageId) {
		if (_highlightTimer.isActive()) {
			return crl::now() - _highlightStart;
		}
	}
	return crl::time(0);
}

bool ListWidget::elementInSelectionMode() {
	return hasSelectedItems() || !_dragSelected.empty();
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

void ListWidget::saveState(not_null<ListMemento*> memento) {
	memento->setAroundPosition(_aroundPosition);
	auto state = countScrollState();
	if (state.item) {
		memento->setIdsLimit(_idsLimit);
		memento->setScrollTopState(state);
	}
}

void ListWidget::restoreState(not_null<ListMemento*> memento) {
	_aroundPosition = memento->aroundPosition();
	_aroundIndex = -1;
	if (const auto limit = memento->idsLimit()) {
		_idsLimit = limit;
		_scrollTopState = memento->scrollTopState();
	}
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
				view->setDisplayDate(true);
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
}

void ListWidget::resizeToWidth(int newWidth, int minHeight) {
	_minHeight = minHeight;
	TWidget::resizeToWidth(newWidth);
	restoreScrollPosition();
}

int ListWidget::resizeGetHeight(int newWidth) {
	update();

	const auto resizeAllItems = (_itemsWidth != newWidth);
	auto newHeight = 0;
	for (auto &view : _items) {
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
	_itemsWidth = newWidth;
	_itemsHeight = newHeight;
	_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom)
		? (_minHeight - _itemsHeight - st::historyPaddingBottom)
		: 0;
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
		if (group->items.back() != item) {
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

void ListWidget::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	Painter p(this);

	auto ms = crl::now();
	auto clip = e->rect();

	auto from = std::lower_bound(begin(_items), end(_items), clip.top(), [this](auto &elem, int top) {
		return this->itemTop(elem) + elem->height() <= top;
	});
	auto to = std::lower_bound(begin(_items), end(_items), clip.top() + clip.height(), [this](auto &elem, int bottom) {
		return this->itemTop(elem) < bottom;
	});
	if (from != end(_items)) {
		auto top = itemTop(from->get());
		p.translate(0, top);
		for (auto i = from; i != to; ++i) {
			const auto view = *i;
			view->draw(
				p,
				clip.translated(0, -top),
				itemRenderSelection(view),
				ms);
			const auto height = view->height();
			top += height;
			p.translate(0, height);
		}
		p.translate(0, -top);

		enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
			// stop the enumeration if the userpic is below the painted rect
			if (userpicTop >= clip.top() + clip.height()) {
				return false;
			}

			// paint the userpic if it intersects the painted rect
			if (userpicTop + st::msgPhotoSize > clip.top()) {
				const auto message = view->data()->toHistoryMessage();
				Assert(message != nullptr);

				if (const auto from = message->displayFrom()) {
					from->paintUserpicLeft(
						p,
						st::historyPhotoLeft,
						userpicTop,
						view->width(),
						st::msgPhotoSize);
				} else if (const auto info = message->hiddenForwardedInfo()) {
					info->userpic.paint(
						p,
						st::historyPhotoLeft,
						userpicTop,
						view->width(),
						st::msgPhotoSize);
				} else {
					Unexpected("Corrupt forwarded information in message.");
				}
			}
			return true;
		});

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
						date->paint(p, dateY, width);
					} else {
						ServiceMessagePainter::paintDate(
							p,
							view->dateTime(),
							dateY,
							width);
					}
				}
			}
			return true;
		});
	}
}

void ListWidget::applyDragSelection() {
	applyDragSelection(_selected);
	clearDragSelection();
	pushSelectedItems();
}

void ListWidget::applyDragSelection(SelectedMap &applyTo) const {
	if (_dragSelectAction == DragSelectAction::Selecting) {
		for (const auto itemId : _dragSelected) {
			if (applyTo.size() >= MaxSelectedItems) {
				break;
			} else if (!applyTo.contains(itemId)) {
				if (const auto item = session().data().message(itemId)) {
					addToSelection(applyTo, item);
				}
			}
		}
	} else if (_dragSelectAction == DragSelectAction::Deselecting) {
		for (const auto itemId : _dragSelected) {
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

	const auto timeFormat = qsl(", [dd.MM.yy hh:mm]\n");
	auto groups = base::flat_set<not_null<const Data::Group*>>();
	auto fullSize = 0;
	auto texts = std::vector<std::pair<
		not_null<HistoryItem*>,
		TextForMimeData>>();
	texts.reserve(selected.size());

	const auto wrapItem = [&](
			not_null<HistoryItem*> item,
			TextForMimeData &&unwrapped) {
		auto time = ItemDateTime(item).toString(timeFormat);
		auto part = TextForMimeData();
		auto size = item->author()->name.size()
			+ time.size()
			+ unwrapped.expanded.size();
		part.reserve(size);
		part.append(item->author()->name).append(time);
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

	for (const auto [itemId, data] : selected) {
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
	auto sep = qstr("\n\n");
	result.reserve(fullSize + (texts.size() - 1) * sep.size());
	for (auto i = begin(texts), e = end(texts); i != e;) {
		result.append(std::move(i->second));
		if (++i != e) {
			result.append(sep);
		}
	}
	return result;
}

MessageIdsList ListWidget::getSelectedItems() const {
	return collectSelectedIds();
}

not_null<Element*> ListWidget::findItemByY(int y) const {
	Expects(!_items.empty());

	if (y < _itemsTop) {
		return _items.front();
	}
	auto i = std::lower_bound(
		begin(_items),
		end(_items),
		y,
		[this](auto &elem, int top) {
			return this->itemTop(elem) + elem->height() <= top;
		});
	return (i != end(_items)) ? i->get() : _items.back().get();
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
	if (_items.empty()) {
		return { Data::MessagePosition(), 0 };
	}
	auto topItem = findItemByY(_visibleTop);
	return {
		topItem->data()->position(),
		_visibleTop - itemTop(topItem)
	};
}

void ListWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		if (hasSelectedText() || hasSelectedItems()) {
			cancelSelection();
		} else {
			_delegate->listCancelRequest();
		}
	} else if (e == QKeySequence::Copy
		&& (hasSelectedText() || hasSelectedItems())) {
		SetClipboardText(getSelectedText());
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E
		&& e->modifiers().testFlag(Qt::ControlModifier)) {
		SetClipboardText(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else if (e == QKeySequence::Delete) {
		_delegate->listDeleteRequest();
	} else {
		e->ignore();
	}
}

void ListWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());
	trySwitchToWordSelection();
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

	auto request = ContextMenuRequest(_controller);

	request.link = ClickHandler::getActive();
	request.view = _overElement;
	request.item = _overItemExact
		? _overItemExact
		: _overElement
		? _overElement->data().get()
		: nullptr;
	request.pointState = _overState.pointState;
	request.selectedText = _selectedText;
	request.selectedItems = collectSelectedItems();
	request.overSelection = showFromTouch
		|| (_overElement && isInsideSelection(
			_overElement,
			_overItemExact ? _overItemExact : _overElement->data().get(),
			_overState));

	_menu = FillContextMenu(this, request);
	if (_menu && !_menu->actions().empty()) {
		_menu->popup(e->globalPos());
		e->accept();
	} else if (_menu) {
		_menu = nullptr;
	}
}

void ListWidget::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
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

void ListWidget::enterEventHook(QEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return TWidget::enterEventHook(e);
}

void ListWidget::leaveEventHook(QEvent *e) {
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
	if (!_overState.itemId || !_pressState.itemId) {
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
	updateDragSelection(
		selectingUp ? overView : pressView,
		selectingUp ? _overState : _pressState,
		selectingUp ? pressView : overView,
		selectingUp ? _pressState : _overState);
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
			for (const auto item : group->items) {
				if (!_delegate->listIsItemGoodForSelection(item)) {
					return;
				}
			}
			for (const auto item : group->items) {
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
	_pressWasInactive = _controller->window()->wasInactivePress();
	if (_pressWasInactive) _controller->window()->setInactivePress(false);

	if (ClickHandler::getPressed()) {
		_mouseAction = MouseAction::PrepareDrag;
	} else if (hasSelectedItems()) {
		if (overSelectedItems()) {
			_mouseAction = MouseAction::PrepareDrag;
		} else if (!_pressWasInactive) {
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
				if (requiredToStartDragging(pressElement)) {
					_mouseAction = MouseAction::PrepareDrag;
				} else {
					if (dragState.afterSymbol) ++_mouseTextSymbol;
					if (!hasSelectedItems()
						&& _overState.pointState != PointState::Outside) {
						setTextSelection(pressElement, TextSelection(
							_mouseTextSymbol,
							_mouseTextSymbol));
						_mouseAction = MouseAction::Selecting;
					} else {
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
	//_widget->noSelectingScroll(); // #TODO select scroll
}

void ListWidget::mouseActionFinish(
		const QPoint &globalPosition,
		Qt::MouseButton button) {
	mouseActionUpdate(globalPosition);

	auto pressState = base::take(_pressState);
	base::take(_pressItemExact);
	repaintItem(pressState.itemId);

	const auto toggleByHandler = [&](const ClickHandlerPtr &handler) {
		if (_overElement) {
			// If we are in selecting items mode perhaps we want to
			// toggle selection instead of activating the pressed link.
			if (const auto media = _overElement->media()) {
				if (media->toggleSelectionByHandlerClick(handler)) {
					return true;
				}
			}
		}
		return false;
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
		App::activateClickHandler(activated, {
			button,
			QVariant::fromValue(pressState.itemId)
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
				App::wnd()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseSelectType = TextSelectType::Letters;
	//_widget->noSelectingScroll(); // #TODO select scroll

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (_selectedTextItem
		&& _selectedTextRange.from != _selectedTextRange.to) {
		if (const auto view = viewForItem(_selectedTextItem)) {
			SetClipboardText(
				view->selectedText(_selectedTextRange),
				QClipboard::Selection);
}
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void ListWidget::mouseActionUpdate() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(snap(mousePosition.x(), 0, width()), snap(mousePosition.y(), _visibleTop, _visibleBottom));

	const auto view = strictFindItemByY(point.y());
	const auto item = view ? view->data().get() : nullptr;
	const auto itemPoint = mapPointToItem(point, view);
	_overState = MouseState(
		item ? item->fullId() : FullMsgId(),
		view ? view->height() : 0,
		itemPoint,
		view ? view->pointState(itemPoint) : PointState::Outside);
	if (_overElement != view) {
		repaintItem(_overElement);
		_overElement = view;
		repaintItem(_overElement);
	}

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto inTextSelection = (_overState.pointState != PointState::Outside)
		&& (_overState.itemId == _pressState.itemId)
		&& hasSelectedText();
	if (view) {
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
					const auto item = view->data();
					auto dateWidth = 0;
					if (const auto date = view->Get<HistoryView::DateBadge>()) {
						dateWidth = date->width;
					} else {
						dateWidth = st::msgServiceFont->width(langDayOfMonthFull(view->dateTime().date()));
					}
					dateWidth += st::msgServicePadding.left() + st::msgServicePadding.right();
					auto dateLeft = st::msgServiceMargin.left();
					auto maxwidth = view->width();
					if (Adaptive::ChatWide()) {
						maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
					}
					auto widthForDate = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

					dateLeft += (widthForDate - dateWidth) / 2;

					if (point.x() >= dateLeft && point.x() < dateLeft + dateWidth) {
						_scrollDateLink = _delegate->listDateLink(view);
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
							const auto message = view->data()->toHistoryMessage();
							Assert(message != nullptr);

							const auto from = message->displayFrom();
							dragState = TextState(nullptr, from
								? from->openLink()
								: hiddenUserpicLink(message->fullId()));
							_overItemExact = session().data().message(dragState.itemId);
							lnkhost = view;
							return false;
						}
						return true;
					});
				}
			}
		}
	}
	auto lnkChanged = ClickHandler::setActive(dragState.link, lnkhost);
	if (lnkChanged || dragState.cursor != _mouseCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.link
		|| dragState.cursor == CursorState::Date
		|| dragState.cursor == CursorState::Forwarded) {
		Ui::Tooltip::Show(1000, this);
	}

	auto cursor = style::cur_default;
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

	//if (_mouseAction == MouseAction::Selecting) {
	//	_widget->checkSelectingScroll(mousePos);
	//} else {
	//	_widget->noSelectingScroll();
	//} // #TODO select scroll
}

ClickHandlerPtr ListWidget::hiddenUserpicLink(FullMsgId id) {
	static const auto result = std::make_shared<LambdaClickHandler>([] {
		Ui::Toast::Show(tr::lng_forwarded_hidden(tr::now));
	});
	return result;
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
	if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.get())) {
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
	if (auto mimeData = MimeDataFromText(selectedText)) {
		clearDragSelection();
//		_widget->noSelectingScroll(); #TODO scroll

		if (!urls.isEmpty()) {
			mimeData->setUrls(urls);
		}
		if (uponSelected && !Adaptive::OneColumn()) {
			const auto canForwardAll = [&] {
				for (const auto &[itemId, data] : _selected) {
					if (!data.canForward) {
						return false;
					}
				}
				return true;
			}();
			auto items = canForwardAll
				? getSelectedItems()
				: MessageIdsList();
			if (!items.empty()) {
				session().data().setMimeForwardIds(std::move(items));
				mimeData->setData(qsl("application/x-td-forward"), "1");
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
				&& (media->dragItemByHandler(pressedHandler)
					|| media->dragItem())) {
				forwardIds = MessageIdsList(1, exactItem->fullId());
			}
		}
		if (forwardIds.empty()) {
			return nullptr;
		}
		session().data().setMimeForwardIds(std::move(forwardIds));
		auto result = std::make_unique<QMimeData>();
		result->setData(qsl("application/x-td-forward"), "1");
		if (const auto media = pressedView->media()) {
			if (const auto document = media->getDocument()) {
				const auto filepath = document->filepath(
					DocumentData::FilePathResolve::Checked);
				if (!filepath.isEmpty()) {
					QList<QUrl> urls;
					urls.push_back(QUrl::fromLocalFile(filepath));
					result->setUrls(urls);
				}
			}
		}
		return result;
	}
	return nullptr;
}

void ListWidget::performDrag() {
	if (auto mimeData = prepareDrag()) {
		// This call enters event loop and can destroy any QObject.
		_controller->window()->launchDrag(std::move(mimeData));
	}
}

int ListWidget::itemTop(not_null<const Element*> view) const {
	return _itemsTop + view->y();
}

void ListWidget::repaintItem(const Element *view) {
	if (!view) {
		return;
	}
	update(0, itemTop(view), width(), view->height());
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

	if (from == till) {
		updateSize();
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
			next->setDisplayDate(nextDate.date() != viewDate.date());
			auto attached = next->computeIsAttachToPrevious(view);
			next->setAttachToPrevious(attached);
			view->setAttachToNext(attached);
			view = next;
		}
	}
	updateSize();
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
		const auto [i, ok] = _views.emplace(
			item,
			item->createView(this));
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
	if (_unreadBarElement == was) {
		const auto bar = _unreadBarElement->Get<UnreadBar>();
		const auto count = bar ? bar->count : 0;
		const auto freezed = bar ? bar->freezed : false;
		_unreadBarElement = now;
		if (now && count) {
			_unreadBarElement->setUnreadBarCount(count);
			if (freezed) {
				_unreadBarElement->setUnreadBarFreezed();
			}
		}
	}
}

void ListWidget::itemRemoved(not_null<const HistoryItem*> item) {
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
	const auto view = i->second.get();
	_items.erase(
		ranges::remove(_items, view, [](auto view) { return view.get(); }),
		end(_items));
	viewReplaced(view, nullptr);
	_views.erase(i);

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

ListWidget::~ListWidget() = default;

} // namespace HistoryView
