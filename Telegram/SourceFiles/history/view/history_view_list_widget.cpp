/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_list_widget.h"

#include "history/history_media_types.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_service_message.h"
#include "chat_helpers/message_field.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "messenger.h"
#include "apiwrap.h"
#include "layout.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "ui/widgets/popup_menu.h"
#include "core/file_utilities.h"
#include "core/tl_help.h"
#include "base/overload.h"
#include "lang/lang_keys.h"
#include "boxes/edit_participant_box.h"
#include "data/data_session.h"
#include "data/data_feed.h"
#include "data/data_media_types.h"
#include "styles/style_history.h"

namespace HistoryView {
namespace {

constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadIfLessThanScreens = 2;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;

} // namespace

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
		if (lowestInOneDayItemBottom < 0 && item->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = itembottom - view->marginBottom();
		}

		// Call method on a date for all messages that have it and for those who are not showing it
		// because they are in a one day together with the previous message if they are top-most visible.
		if (item->displayDate() || (!item->isEmpty() && itemtop <= _visibleTop)) {
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
		if (!item->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::BottomToTop>(dateCallback);
}

ListWidget::ListWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<ListDelegate*> delegate)
: RpWidget(parent)
, _delegate(delegate)
, _controller(controller)
, _context(_delegate->listContext())
, _scrollDateCheck([this] { scrollDateCheck(); }) {
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([this] { scrollDateHideByTimer(); });
	Auth().data().itemRepaintRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			repaintItem(view);
		}
	}, lifetime());
	subscribe(Auth().data().pendingHistoryResize(), [this] { handlePendingHistoryResize(); });
	subscribe(Auth().data().queryItemVisibility(), [this](const Data::Session::ItemVisibilityQuery &query) {
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
		if (const auto item = App::histItemById(fullId)) {
			_items.push_back(enforceViewForItem(item));
		}
	}
	updateAroundPositionFromRows();

	RpWidget::resizeToWidth(width());
	restoreScrollState();
	mouseActionUpdate(QCursor::pos());
}

void ListWidget::saveScrollState() {
	if (!_scrollTopState.item) {
		_scrollTopState = countScrollState();
	}
}

void ListWidget::restoreScrollState() {
	if (_items.empty() || !_scrollTopState.item) {
		return;
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
		item->createView(_controller, _context));
	return i->second.get();
}

void ListWidget::updateAroundPositionFromRows() {
	_aroundIndex = findNearestItem(_aroundPosition);
	if (_aroundIndex >= 0) {
		_aroundPosition = _items[_aroundIndex]->data()->position();
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

void ListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto scrolledUp = (visibleTop < _visibleTop);
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	updateVisibleTopItem();
	checkMoveToOtherViewer();
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		scrollDateHideByTimer();
	}
	_controller->floatPlayerAreaUpdated().notify(true);
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
	scrollDateHide();
}

void ListWidget::scrollDateHide() {
	if (_scrollDateShown) {
		toggleScrollDateShown();
	}
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
	auto minItemHeight = st::msgMarginTopAttached
		+ st::msgPhotoSize
		+ st::msgMargin.bottom();
	auto preloadedCount = preloadedHeight / minItemHeight;
	auto preloadIdsLimitMin = (preloadedCount / 2) + 1;
	auto preloadIdsLimit = preloadIdsLimitMin
		+ (visibleHeight / minItemHeight);

	auto preloadBefore = kPreloadIfLessThanScreens * visibleHeight;
	auto after = _slice.skippedAfter;
	auto preloadTop = (_visibleTop < preloadBefore);
	auto topLoaded = after && (*after == 0);
	auto before = _slice.skippedBefore;
	auto preloadBottom = (height() - _visibleBottom < preloadBefore);
	auto bottomLoaded = before && (*before == 0);

	auto minScreenDelta = kPreloadedScreensCount
		- kPreloadIfLessThanScreens;
	auto minUniversalIdDelta = (minScreenDelta * visibleHeight)
		/ minItemHeight;
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
	if (_mouseCursorState == HistoryInDateCursorState && _mouseAction == MouseAction::None) {
		if (const auto view = App::hoveredItem()) {
			auto dateText = view->data()->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat));
			return dateText;
		}
	} else if (_mouseCursorState == HistoryInForwardedCursorState && _mouseAction == MouseAction::None) {
		if (const auto view = App::hoveredItem()) {
			if (const auto forwarded = view->data()->Get<HistoryMessageForwarded>()) {
				return forwarded->text.originalText(AllTextSelection, ExpandLinksNone);
			}
		}
	} else if (const auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

QPoint ListWidget::tooltipPos() const {
	return _mousePosition;
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

void ListWidget::itemsAdded(Direction direction, int addedCount) {
	Expects(addedCount >= 0);

	auto checkFrom = (direction == Direction::Up)
		? (_items.size() - addedCount)
		: 1; // Should be ": 0", but zero is skipped anyway.
	auto checkTo = (direction == Direction::Up)
		? (_items.size() + 1)
		: (addedCount + 1);
	for (auto i = checkFrom; i != checkTo; ++i) {
		if (i > 0) {
			const auto view = _items[i - 1].get();
			if (i < _items.size()) {
				// #TODO feeds show
				auto previous = _items[i].get();
				view->setDisplayDate(view->data()->date.date() != previous->data()->date.date());
				auto attachToPrevious = view->computeIsAttachToPrevious(previous);
				view->setAttachToPrevious(attachToPrevious);
				previous->setAttachToNext(attachToPrevious);
			} else {
				view->setDisplayDate(true);
			}
		}
	}
	updateSize();
}

void ListWidget::updateSize() {
	TWidget::resizeToWidth(width());
	restoreScrollPosition();
	updateVisibleTopItem();
}

int ListWidget::resizeGetHeight(int newWidth) {
	update();

	auto newHeight = 0;
	for (auto &view : _items) {
		view->setY(newHeight);
		newHeight += view->resizeGetHeight(newWidth);
	}
	_itemsHeight = newHeight;
	_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom) ? (_minHeight - _itemsHeight - st::historyPaddingBottom) : 0;
	return _itemsTop + _itemsHeight + st::historyPaddingBottom;
}

void ListWidget::restoreScrollPosition() {
	auto newVisibleTop = _visibleTopItem
		? (itemTop(_visibleTopItem) + _visibleTopFromItem)
		: ScrollMax;
	_delegate->listScrollTo(newVisibleTop);
}

void ListWidget::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	Painter p(this);

	auto ms = getms();
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
			const auto selection = (view == _selectedItem)
				? _selectedText
				: TextSelection();
			view->draw(p, clip.translated(0, -top), selection, ms);
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

				message->from()->paintUserpicLeft(
					p,
					st::historyPhotoLeft,
					userpicTop,
					view->width(),
					st::msgPhotoSize);
			}
			return true;
		});

		auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
		auto scrollDateOpacity = _scrollDateOpacity.current(ms, _scrollDateShown ? 1. : 0.);
		enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
			// stop the enumeration if the date is above the painted rect
			if (dateTop + dateHeight <= clip.top()) {
				return false;
			}

			const auto item = view->data();
			const auto displayDate = item->displayDate();
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
					if (const auto date = item->Get<HistoryMessageDate>()) {
						date->paint(p, dateY, width);
					} else {
						ServiceMessagePainter::paintDate(
							p, item->date, dateY, width);
					}
				}
			}
			return true;
		});
	}
}

TextWithEntities ListWidget::getSelectedText() const {
	return _selectedItem
		? _selectedItem->selectedText(_selectedText)
		: TextWithEntities();
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
		_delegate->listCloseRequest();
	} else if (e == QKeySequence::Copy && _selectedItem != nullptr) {
		copySelectedText();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		setToClipboard(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else {
		e->ignore();
	}
}

void ListWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());
	if (((_mouseAction == MouseAction::Selecting && _selectedItem != nullptr) || (_mouseAction == MouseAction::None)) && _mouseSelectType == TextSelectType::Letters && _mouseActionItem) {
		HistoryStateRequest request;
		request.flags |= Text::StateRequest::Flag::LookupSymbol;
		auto dragState = _mouseActionItem->getState(_dragStartPosition, request);
		if (dragState.cursor == HistoryInTextCursorState) {
			_mouseTextSymbol = dragState.symbol;
			_mouseSelectType = TextSelectType::Words;
			if (_mouseAction == MouseAction::None) {
				_mouseAction = MouseAction::Selecting;
				auto selection = TextSelection { dragState.symbol, dragState.symbol };
				repaintItem(std::exchange(_selectedItem, _mouseActionItem));
				_selectedText = selection;
			}
			mouseMoveEvent(e);

			_trippleClickPoint = e->globalPos();
			_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
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

	// -1 - has selection, but no over, 0 - no selection, 1 - over text
	auto isUponSelected = 0;
	auto hasSelected = 0;
	if (_selectedItem) {
		isUponSelected = -1;

		auto selFrom = _selectedText.from;
		auto selTo = _selectedText.to;
		hasSelected = (selTo > selFrom) ? 1 : 0;
		if (App::mousedItem() && App::mousedItem() == App::hoveredItem()) {
			auto mousePos = mapPointToItem(
				mapFromGlobal(_mousePosition),
				App::mousedItem());
			HistoryStateRequest request;
			request.flags |= Text::StateRequest::Flag::LookupSymbol;
			auto dragState = App::mousedItem()->getState(mousePos, request);
			if (dragState.cursor == HistoryInTextCursorState && dragState.symbol >= selFrom && dragState.symbol < selTo) {
				isUponSelected = 1;
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_menu = base::make_unique_q<Ui::PopupMenu>(nullptr);

	_contextMenuLink = ClickHandler::getActive();
	auto view = App::hoveredItem()
		? App::hoveredItem()
		: App::hoveredLinkItem();
	auto lnkPhoto = dynamic_cast<PhotoClickHandler*>(_contextMenuLink.get());
	auto lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLink.get());
	auto lnkPeer = dynamic_cast<PeerClickHandler*>(_contextMenuLink.get());
	auto lnkIsVideo = lnkDocument ? lnkDocument->document()->isVideoFile() : false;
	auto lnkIsVoice = lnkDocument ? lnkDocument->document()->isVoiceMessage() : false;
	auto lnkIsAudio = lnkDocument ? lnkDocument->document()->isAudioFile() : false;
	if (lnkPhoto || lnkDocument) {
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), [=] {
				copySelectedText();
			})->setEnabled(true);
		}
		if (lnkPhoto) {
			const auto photo = lnkPhoto->photo();
			_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
				savePhotoToFile(photo);
			}))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_image), [=] {
				copyContextImage(photo);
			})->setEnabled(true);
		} else {
			auto document = lnkDocument->document();
			if (document->loading()) {
				_menu->addAction(lang(lng_context_cancel_download), [=] {
					cancelContextDownload(document);
				})->setEnabled(true);
			} else {
				if (document->loaded() && document->isGifv()) {
					if (!cAutoPlayGif()) {
						const auto itemId = view
							? view->data()->fullId()
							: FullMsgId();
						_menu->addAction(lang(lng_context_open_gif), [=] {
							openContextGif(itemId);
						})->setEnabled(true);
					}
				}
				if (!document->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
					_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), [=] {
						showContextInFolder(document);
					})->setEnabled(true);
				}
				_menu->addAction(lang(lnkIsVideo ? lng_context_save_video : (lnkIsVoice ? lng_context_save_audio : (lnkIsAudio ? lng_context_save_audio_file : lng_context_save_file))), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
					saveDocumentToFile(document);
				}))->setEnabled(true);
			}
		}
	} else if (lnkPeer) { // suggest to block
		// #TODO suggest restrict peer
	} else { // maybe cursor on some text history item?
		const auto item = view ? view->data().get() : nullptr;
		const auto itemId = item ? item->fullId() : FullMsgId();
		bool canDelete = item && item->canDelete() && (item->id > 0 || !item->serviceMsg());
		bool canForward = item && item->allowsForward();

		auto msg = dynamic_cast<HistoryMessage*>(item);
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), [this] { copySelectedText(); })->setEnabled(true);
		} else {
			if (item && !isUponSelected) {
				auto mediaHasTextForCopy = false;
				if (auto media = view->media()) {
					mediaHasTextForCopy = media->hasTextForCopy();
					if (media->type() == MediaTypeWebPage && static_cast<HistoryWebPage*>(media)->attach()) {
						media = static_cast<HistoryWebPage*>(media)->attach();
					}
					if (media->type() == MediaTypeSticker) {
						if (auto document = media->getDocument()) {
							if (document->sticker() && document->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
								_menu->addAction(lang(document->sticker()->setInstalled() ? lng_context_pack_info : lng_context_pack_add), [=] {
									showStickerPackInfo(document);
								});
							}
							_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
								saveDocumentToFile(document);
							}))->setEnabled(true);
						}
					} else if (media->type() == MediaTypeGif && !_contextMenuLink) {
						if (auto document = media->getDocument()) {
							if (document->loading()) {
								_menu->addAction(lang(lng_context_cancel_download), [=] {
									cancelContextDownload(document);
								})->setEnabled(true);
							} else {
								if (document->isGifv()) {
									if (!cAutoPlayGif()) {
										_menu->addAction(lang(lng_context_open_gif), [=] {
											openContextGif(itemId);
										})->setEnabled(true);
									}
								}
								if (!document->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
									_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), [=] {
										showContextInFolder(document);
									})->setEnabled(true);
								}
								_menu->addAction(lang(lng_context_save_file), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
									saveDocumentToFile(document);
								}))->setEnabled(true);
							}
						}
					}
				}
				if (msg && !_contextMenuLink && (!msg->emptyText() || mediaHasTextForCopy)) {
					_menu->addAction(lang(lng_context_copy_text), [=] {
						copyContextText(itemId);
					})->setEnabled(true);
				}
			}
		}

		auto linkCopyToClipboardText = _contextMenuLink ? _contextMenuLink->copyToClipboardContextItemText() : QString();
		if (!linkCopyToClipboardText.isEmpty()) {
			_menu->addAction(linkCopyToClipboardText, [this] { copyContextUrl(); })->setEnabled(true);
		}
	}

	if (_menu->actions().isEmpty()) {
		_menu = nullptr;
	} else {
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void ListWidget::savePhotoToFile(PhotoData *photo) {
	if (!photo || !photo->date || !photo->loaded()) return;

	auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
	FileDialog::GetWritePath(
		lang(lng_save_photo),
		filter,
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		base::lambda_guarded(this, [this, photo](const QString &result) {
			if (!result.isEmpty()) {
				photo->full->pix().toImage().save(result, "JPG");
			}
		}));
}

void ListWidget::saveDocumentToFile(DocumentData *document) {
	DocumentSaveClickHandler::doSave(document, true);
}

void ListWidget::copyContextImage(PhotoData *photo) {
	if (!photo || !photo->date || !photo->loaded()) return;

	QApplication::clipboard()->setPixmap(photo->full->pix());
}

void ListWidget::copySelectedText() {
	setToClipboard(getSelectedText());
}

void ListWidget::copyContextUrl() {
	if (_contextMenuLink) {
		_contextMenuLink->copyToClipboard();
	}
}

void ListWidget::showStickerPackInfo(not_null<DocumentData*> document) {
	if (auto sticker = document->sticker()) {
		if (sticker->set.type() != mtpc_inputStickerSetEmpty) {
			App::main()->stickersBox(sticker->set);
		}
	}
}

void ListWidget::cancelContextDownload(not_null<DocumentData*> document) {
	document->cancel();
}

void ListWidget::showContextInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(
		DocumentData::FilePathResolveChecked);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void ListWidget::openContextGif(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		if (auto media = item->media()) {
			if (auto document = media->document()) {
				Messenger::Instance().showDocument(document, item);
			}
		}
	}
}

void ListWidget::copyContextText(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		if (const auto view = viewForItem(item)) {
			setToClipboard(view->selectedText(FullSelection));
		}
	}
}

void ListWidget::setToClipboard(
		const TextWithEntities &forClipboard,
		QClipboard::Mode mode) {
	if (auto data = MimeDataFromTextWithEntities(forClipboard)) {
		QApplication::clipboard()->setMimeData(data.release(), mode);
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
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _mouseAction != MouseAction::None) {
		mouseReleaseEvent(e);
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
	if (const auto view = App::hoveredItem()) {
		repaintItem(view);
		App::hoveredItem(nullptr);
	}
	ClickHandler::clearActive();
	Ui::Tooltip::Hide();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return TWidget::leaveEventHook(e);
}

void ListWidget::mouseActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	ClickHandler::pressed();
	if (App::pressedItem() != App::hoveredItem()) {
		repaintItem(App::pressedItem());
		App::pressedItem(App::hoveredItem());
		repaintItem(App::pressedItem());
	}

	_mouseAction = MouseAction::None;
	_mouseActionItem = App::mousedItem();
	_dragStartPosition = mapPointToItem(mapFromGlobal(screenPos), _mouseActionItem);
	_pressWasInactive = _controller->window()->wasInactivePress();
	if (_pressWasInactive) _controller->window()->setInactivePress(false);

	if (ClickHandler::getPressed()) {
		_mouseAction = MouseAction::PrepareDrag;
	}
	if (_mouseAction == MouseAction::None && _mouseActionItem) {
		HistoryTextState dragState;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->getState(_dragStartPosition, request);
			if (dragState.cursor == HistoryInTextCursorState) {
				auto selection = TextSelection { dragState.symbol, dragState.symbol };
				repaintItem(std::exchange(_selectedItem, _mouseActionItem));
				_selectedText = selection;
				_mouseTextSymbol = dragState.symbol;
				_mouseAction = MouseAction::Selecting;
				_mouseSelectType = TextSelectType::Paragraphs;
				mouseActionUpdate(_mousePosition);
				_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
			}
		} else if (App::pressedItem()) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->getState(_dragStartPosition, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (App::pressedItem()) {
				_mouseTextSymbol = dragState.symbol;
				auto uponSelected = (dragState.cursor == HistoryInTextCursorState);
				if (uponSelected) {
					if (!_selectedItem || _selectedItem != _mouseActionItem) {
						uponSelected = false;
					} else if (_mouseTextSymbol < _selectedText.from || _mouseTextSymbol >= _selectedText.to) {
						uponSelected = false;
					}
				}
				if (uponSelected) {
					_mouseAction = MouseAction::PrepareDrag; // start text drag
				} else if (!_pressWasInactive) {
					if (dragState.afterSymbol) ++_mouseTextSymbol;
					auto selection = TextSelection { _mouseTextSymbol, _mouseTextSymbol };
					repaintItem(std::exchange(_selectedItem, _mouseActionItem));
					_selectedText = selection;
					_mouseAction = MouseAction::Selecting;
					repaintItem(_mouseActionItem);
				}
			}
		}
	}

	if (!_mouseActionItem) {
		_mouseAction = MouseAction::None;
	} else if (_mouseAction == MouseAction::None) {
		_mouseActionItem = nullptr;
	}
}

void ListWidget::mouseActionUpdate(const QPoint &screenPos) {
	_mousePosition = screenPos;
	updateSelected();
}

void ListWidget::mouseActionCancel() {
	_mouseActionItem = nullptr;
	_mouseAction = MouseAction::None;
	_dragStartPosition = QPoint(0, 0);
	_wasSelectedText = false;
	//_widget->noSelectingScroll(); // #TODO select scroll
}

void ListWidget::mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);

	auto activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging) {
		activated = nullptr;
	}
	if (const auto view = App::pressedItem()) {
		repaintItem(view);
		App::pressedItem(nullptr);
	}

	_wasSelectedText = false;

	if (activated) {
		mouseActionCancel();
		App::activateClickHandler(activated, button);
		return;
	}
	if (_mouseAction == MouseAction::PrepareDrag && !_pressWasInactive && button != Qt::RightButton) {
		repaintItem(base::take(_selectedItem));
	} else if (_mouseAction == MouseAction::Selecting) {
		if (_selectedItem && !_pressWasInactive) {
			if (_selectedText.from == _selectedText.to) {
				_selectedItem = nullptr;
				App::wnd()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseActionItem = nullptr;
	_mouseSelectType = TextSelectType::Letters;
	//_widget->noSelectingScroll(); // #TODO select scroll

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (_selectedItem && _selectedText.from != _selectedText.to) {
		setToClipboard(_selectedItem->selectedText(_selectedText), QClipboard::Selection);
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void ListWidget::updateSelected() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(snap(mousePosition.x(), 0, width()), snap(mousePosition.y(), _visibleTop, _visibleBottom));

	auto itemPoint = QPoint();
	const auto view = strictFindItemByY(point.y());
	const auto item = view ? view->data().get() : nullptr;
	if (view) {
		App::mousedItem(view);
		itemPoint = mapPointToItem(point, view);
		if (view->hasPoint(itemPoint)) {
			if (App::hoveredItem() != view) {
				repaintItem(App::hoveredItem());
				App::hoveredItem(view);
				repaintItem(view);
			}
		} else if (const auto view = App::hoveredItem()) {
			repaintItem(view);
			App::hoveredItem(nullptr);
		}
	}

	HistoryTextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto selectingText = _selectedItem
		&& (view == _mouseActionItem)
		&& (view == App::hoveredItem());
	if (view) {
		if (view != _mouseActionItem || (itemPoint - _dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [this] { performDrag(); });
			}
		}
		HistoryStateRequest request;
		if (_mouseAction == MouseAction::Selecting) {
			request.flags |= Text::StateRequest::Flag::LookupSymbol;
		} else {
			selectingText = false;
		}
		dragState = view->getState(itemPoint, request);
		lnkhost = view;
		if (!dragState.link && itemPoint.x() >= st::historyPhotoLeft && itemPoint.x() < st::historyPhotoLeft + st::msgPhotoSize) {
			if (auto message = item->toHistoryMessage()) {
				if (view->hasFromPhoto()) {
					enumerateUserpics([&](not_null<Element*> view, int userpicTop) -> bool {
						// stop enumeration if the userpic is below our point
						if (userpicTop > point.y()) {
							return false;
						}

						// stop enumeration if we've found a userpic under the cursor
						if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
							const auto message = view->data()->toHistoryMessage();
							Assert(message != nullptr);

							dragState.link = message->from()->openLink();
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
	if (dragState.link || dragState.cursor == HistoryInDateCursorState || dragState.cursor == HistoryInForwardedCursorState) {
		Ui::Tooltip::Show(1000, this);
	}

	auto cursor = style::cur_default;
	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		if (dragState.link) {
			cursor = style::cur_pointer;
		} else if (_mouseCursorState == HistoryInTextCursorState) {
			cursor = style::cur_text;
		} else if (_mouseCursorState == HistoryInDateCursorState) {
//			cursor = style::cur_cross;
		}
	} else if (view) {
		if (_mouseAction == MouseAction::Selecting) {
			if (selectingText) {
				auto second = dragState.symbol;
				if (dragState.afterSymbol && _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selection = TextSelection { qMin(second, _mouseTextSymbol), qMax(second, _mouseTextSymbol) };
				if (_mouseSelectType != TextSelectType::Letters) {
					selection = _mouseActionItem->adjustSelection(selection, _mouseSelectType);
				}
				if (_selectedText != selection) {
					_selectedText = selection;
					repaintItem(_mouseActionItem);
				}
				if (!_wasSelectedText && (selection.from != selection.to)) {
					_wasSelectedText = true;
					setFocus();
				}
			}
		} else if (_mouseAction == MouseAction::Dragging) {
		}

		if (ClickHandler::getPressed()) {
			cursor = style::cur_pointer;
		} else if (_mouseAction == MouseAction::Selecting && _selectedItem) {
			cursor = style::cur_text;
		}
	}

	// Voice message seek support.
	if (const auto pressedView = App::pressedLinkItem()) {
		auto adjustedPoint = mapPointToItem(point, pressedView);
		pressedView->updatePressed(adjustedPoint);
	}

	//if (_mouseAction == MouseAction::Selecting) {
	//	_widget->checkSelectingScroll(mousePos);
	//} else {
	//	_widget->noSelectingScroll();
	//} // #TODO select scroll

	if (_mouseAction == MouseAction::None && (lnkChanged || cursor != _cursor)) {
		setCursor(_cursor = cursor);
	}
}

void ListWidget::performDrag() {
	if (_mouseAction != MouseAction::Dragging) return;

	auto uponSelected = false;
	//if (_mouseActionItem) {
	//	if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
	//		uponSelected = _selected.contains(_mouseActionItem);
	//	} else {
	//		HistoryStateRequest request;
	//		request.flags |= Text::StateRequest::Flag::LookupSymbol;
	//		auto dragState = _mouseActionItem->getState(_dragStartPosition.x(), _dragStartPosition.y(), request);
	//		uponSelected = (dragState.cursor == HistoryInTextCursorState);
	//		if (uponSelected) {
	//			if (_selected.isEmpty() ||
	//				_selected.cbegin().value() == FullSelection ||
	//				_selected.cbegin().key() != _mouseActionItem
	//				) {
	//				uponSelected = false;
	//			} else {
	//				uint16 selFrom = _selected.cbegin().value().from, selTo = _selected.cbegin().value().to;
	//				if (dragState.symbol < selFrom || dragState.symbol >= selTo) {
	//					uponSelected = false;
	//				}
	//			}
	//		}
	//	}
	//}
	//auto pressedHandler = ClickHandler::getPressed();

	//if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.data())) {
	//	return;
	//}

	//TextWithEntities sel;
	//QList<QUrl> urls;
	//if (uponSelected) {
	//	sel = getSelectedText();
	//} else if (pressedHandler) {
	//	sel = { pressedHandler->dragText(), EntitiesInText() };
	//	//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
	//	//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
	//	//}
	//}
	//if (auto mimeData = mimeDataFromTextWithEntities(sel)) {
	//	updateDragSelection(0, 0, false);
	//	_widget->noSelectingScroll();

	//	if (!urls.isEmpty()) mimeData->setUrls(urls);
	//	if (uponSelected && !Adaptive::OneColumn()) {
	//		auto selectedState = getSelectionState();
	//		if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
	//			Auth().data().setMimeForwardIds(getSelectedItems());
	//			mimeData->setData(qsl("application/x-td-forward"), "1");
	//		}
	//	}
	//	_controller->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryMedia*>(nullptr);
	//	if (auto pressedItem = App::pressedItem()) {
	//		pressedMedia = pressedItem->media();
	//		if (_mouseCursorState == HistoryInDateCursorState
	//			|| (pressedMedia && pressedMedia->dragItem())) {
	//			Auth().data().setMimeForwardIds(
	//				Auth().data().itemOrItsGroup(pressedItem->data()));
	//			forwardMimeType = qsl("application/x-td-forward");
	//		}
	//	}
	//	if (auto pressedLnkItem = App::pressedLinkItem()) {
	//		if ((pressedMedia = pressedLnkItem->media())) {
	//			if (forwardMimeType.isEmpty()
	//				&& pressedMedia->dragItemByHandler(pressedHandler)) {
	//				Auth().data().setMimeForwardIds(
	//					{ 1, pressedLnkItem->fullId() });
	//				forwardMimeType = qsl("application/x-td-forward");
	//			}
	//		}
	//	}
	//	if (!forwardMimeType.isEmpty()) {
	//		auto mimeData = std::make_unique<QMimeData>();
	//		mimeData->setData(forwardMimeType, "1");
	//		if (auto document = (pressedMedia ? pressedMedia->getDocument() : nullptr)) {
	//			auto filepath = document->filepath(DocumentData::FilePathResolveChecked);
	//			if (!filepath.isEmpty()) {
	//				QList<QUrl> urls;
	//				urls.push_back(QUrl::fromLocalFile(filepath));
	//				mimeData->setUrls(urls);
	//			}
	//		}

	//		// This call enters event loop and can destroy any QObject.
	//		_controller->window()->launchDrag(std::move(mimeData));
	//		return;
	//	}
	//} // #TODO drag
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

QPoint ListWidget::mapPointToItem(
		QPoint point,
		const Element *view) const {
	if (!view) {
		return QPoint();
	}
	return point - QPoint(0, itemTop(view));
}

void ListWidget::handlePendingHistoryResize() {
	// #TODO resize
	//if (_history->hasPendingResizedItems()) {
	//	_history->resizeGetHeight(width());
	//	updateSize();
	//}
}

ListWidget::~ListWidget() = default;

} // namespace HistoryView
