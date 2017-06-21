/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_admin_log_inner.h"

#include "styles/style_history.h"
#include "history/history_media_types.h"
#include "history/history_admin_log_item.h"
#include "history/history_admin_log_section.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "lang/lang_keys.h"

namespace AdminLog {
namespace {

constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kEventsPerPage = 10;

} // namespace

InnerWidget::InnerWidget(QWidget *parent, gsl::not_null<Window::Controller*> controller, gsl::not_null<ChannelData*> channel, base::lambda<void(int top)> scrollTo) : TWidget(parent)
, _controller(controller)
, _channel(channel)
, _history(App::history(channel))
, _scrollTo(std::move(scrollTo))
, _scrollDateCheck([this] { scrollDateCheck(); }) {
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([this] { scrollDateHideByTimer(); });
	subscribe(AuthSession::Current().data().repaintLogEntry(), [this](gsl::not_null<const HistoryItem*> historyItem) {
		auto it = _itemsByHistoryItems.find(historyItem);
		if (it != _itemsByHistoryItems.cend()) {
			repaintItem(it->second);
		}
	});
}

void InnerWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	updateVisibleTopItem();
	checkPreloadMore();
}

void InnerWidget::updateVisibleTopItem() {
	auto start = std::rbegin(_items), end = std::rend(_items);
	auto from = std::upper_bound(start, end, _visibleTop, [](int top, auto &elem) {
		return top <= elem->top() + elem->height();
	});
	if (from != end) {
		_visibleTopItem = from->get();
		_visibleTopFromItem = _visibleTop - _visibleTopItem->top();
	} else {
		_visibleTopItem = nullptr;
		_visibleTopFromItem = _visibleTop;
	}
}

bool InnerWidget::displayScrollDate() const {
	return (_visibleTop <= height() - 2 * (_visibleBottom - _visibleTop));
}

void InnerWidget::scrollDateCheck() {
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

void InnerWidget::scrollDateHideByTimer() {
	_scrollDateHideTimer.cancel();
	if (!_scrollDateLink || ClickHandler::getPressed() != _scrollDateLink) {
		scrollDateHide();
	}
}

void InnerWidget::scrollDateHide() {
	if (_scrollDateShown) {
		toggleScrollDateShown();
	}
}

void InnerWidget::keepScrollDateForNow() {
	if (!_scrollDateShown && _scrollDateLastItem && _scrollDateOpacity.animating()) {
		toggleScrollDateShown();
	}
	_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
}

void InnerWidget::toggleScrollDateShown() {
	_scrollDateShown = !_scrollDateShown;
	auto from = _scrollDateShown ? 0. : 1.;
	auto to = _scrollDateShown ? 1. : 0.;
	_scrollDateOpacity.start([this] { repaintScrollDateCallback(); }, from, to, st::historyDateFadeDuration);
}

void InnerWidget::repaintScrollDateCallback() {
	auto updateTop = _visibleTop;
	auto updateHeight = st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	update(0, updateTop, width(), updateHeight);
}

void InnerWidget::checkPreloadMore() {
	if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
		preloadMore(Direction::Down);
	}
	if (_visibleTop < PreloadHeightsCount * (_visibleBottom - _visibleTop)) {
		preloadMore(Direction::Up);
	}
}

void InnerWidget::applyFilter(MTPDchannelAdminLogEventsFilter::Flags flags, const std::vector<gsl::not_null<UserData*>> &admins) {
	_filterFlags = flags;
	_filterAdmins = admins;
}

QString InnerWidget::tooltipText() const {
	if (_mouseCursorState == HistoryInDateCursorState && _mouseAction == MouseAction::None) {
		if (_itemOver) {
			auto dateText = _itemOver->date().toString(QLocale::system().dateTimeFormat(QLocale::LongFormat));
			return dateText;
		}
	} else if (_mouseCursorState == HistoryInForwardedCursorState && _mouseAction == MouseAction::None) {
		if (_itemOver) {
			auto forwarded = _itemOver->getForwardedInfoText();
		}
	} else if (auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

QPoint InnerWidget::tooltipPos() const {
	return _mousePosition;
}

void InnerWidget::saveState(gsl::not_null<SectionMemento*> memento) const {
	//if (auto count = _items.size()) {
	//	QList<gsl::not_null<PeerData*>> groups;
	//	groups.reserve(count);
	//	for_const (auto item, _items) {
	//		groups.push_back(item->peer);
	//	}
	//	memento->setCommonGroups(groups);
	//}
}

void InnerWidget::restoreState(gsl::not_null<const SectionMemento*> memento) {
	//auto list = memento->getCommonGroups();
	//_allLoaded = false;
	//if (!list.empty()) {
	//	showInitial(list);
	//}
}

//void InnerWidget::showInitial(const QList<PeerData*> &list) {
//	for_const (auto group, list) {
//		if (auto item = computeItem(group)) {
//			_items.push_back(item);
//		}
//		_preloadGroupId = group->bareId();
//	}
//	updateSize();
//}

void InnerWidget::preloadMore(Direction direction) {
	auto &requestId = (direction == Direction::Up) ? _preloadUpRequestId : _preloadDownRequestId;
	auto &loadedFlag = (direction == Direction::Up) ? _upLoaded : _downLoaded;
	if (requestId != 0 || loadedFlag) {
		return;
	}

	auto flags = MTPchannels_GetAdminLog::Flags(0);
	auto filter = MTP_channelAdminLogEventsFilter(MTP_flags(_filterFlags));
	if (_filterFlags != 0) {
		flags |= MTPchannels_GetAdminLog::Flag::f_events_filter;
	}
	auto admins = QVector<MTPInputUser>(0);
	if (!_filterAdmins.empty()) {
		admins.reserve(_filterAdmins.size());
		for (auto &admin : _filterAdmins) {
			admins.push_back(admin->inputUser);
		}
		flags |= MTPchannels_GetAdminLog::Flag::f_admins;
	}
	auto query = QString();
	auto maxId = (direction == Direction::Up) ? _minId : 0;
	auto minId = (direction == Direction::Up) ? 0 : _maxId;
	requestId = request(MTPchannels_GetAdminLog(MTP_flags(flags), _channel->inputChannel, MTP_string(query), filter, MTP_vector<MTPInputUser>(admins), MTP_long(maxId), MTP_long(minId), MTP_int(kEventsPerPage))).done([this, &requestId, &loadedFlag, direction](const MTPchannels_AdminLogResults &result) {
		Expects(result.type() == mtpc_channels_adminLogResults);
		requestId = 0;

		auto &results = result.c_channels_adminLogResults();
		App::feedUsers(results.vusers);
		App::feedChats(results.vchats);
		auto &events = results.vevents.v;
		if (!events.empty()) {
			_items.reserve(_items.size() + events.size());
			for_const (auto &event, events) {
				t_assert(event.type() == mtpc_channelAdminLogEvent);
				_items.push_back(std::make_unique<Item>(_history, _idManager, event.c_channelAdminLogEvent()));
			}
			if (!_items.empty()) {
				_maxId = _items.front()->id();
				_minId = _items.back()->id();
				if (_minId == 1) {
					_upLoaded = true;
				}
			}
			itemsAdded(direction);
		} else {
			loadedFlag = true;
		}
	}).fail([this, &requestId, &loadedFlag](const RPCError &error) {
		requestId = 0;
		loadedFlag = true;
	}).send();
}

void InnerWidget::itemsAdded(Direction direction) {
	updateSize();
}

void InnerWidget::updateSize() {
	TWidget::resizeToWidth(width());
	auto newVisibleTop = _visibleTopItem ? (itemTop(_visibleTopItem) + _visibleTopFromItem) : ScrollMax;
	_scrollTo(newVisibleTop);
	updateVisibleTopItem();
	checkPreloadMore();
}

int InnerWidget::resizeGetHeight(int newWidth) {
	update();

	auto newHeight = 0;
	for (auto &item : base::reversed(_items)) {
		item->setTop(newHeight);
		newHeight += item->resizeGetHeight(newWidth);
	}
	_itemsHeight = newHeight;
	_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom) ? (_minHeight - _itemsHeight - st::historyPaddingBottom) : 0;
	return _itemsTop + _itemsHeight + st::historyPaddingBottom;
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	Painter p(this);

	auto ms = getms();
	auto clip = e->rect();

	if (_items.empty() && _upLoaded && _downLoaded) {
		paintEmpty(p);
	} else {
		auto start = std::rbegin(_items), end = std::rend(_items);
		auto from = std::upper_bound(start, end, clip.top(), [this](int top, auto &elem) {
			return top <= itemTop(elem.get()) + elem->height();
		});
		auto to = std::lower_bound(start, end, clip.top() + clip.height(), [this](auto &elem, int bottom) {
			return itemTop(elem.get()) < bottom;
		});
		if (from != end) {
			auto top = itemTop(from->get());
			p.translate(0, top);
			for (auto i = from; i != to; ++i) {
				(*i)->draw(p, clip.translated(0, -top), TextSelection(), ms);
				auto height = (*i)->height();
				top += height;
				p.translate(0, height);
			}
		}
	}
}

void InnerWidget::paintEmpty(Painter &p) {
	//style::font font(st::msgServiceFont);
	//int32 w = font->width(lang(lng_willbe_history)) + st::msgPadding.left() + st::msgPadding.right(), h = font->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + 2;
	//QRect tr((width() - w) / 2, (height() - _field->height() - 2 * st::historySendPadding - h) / 2, w, h);
	//HistoryLayout::ServiceMessagePainter::paintBubble(p, tr.x(), tr.y(), tr.width(), tr.height());

	//p.setPen(st::msgServiceFg);
	//p.setFont(font->f);
	//p.drawText(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top() + 1 + font->ascent, lang(lng_willbe_history));
}

void InnerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && _cancelledCallback) {
		_cancelledCallback();
	}
}

void InnerWidget::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
}

void InnerWidget::mouseMoveEvent(QMouseEvent *e) {
	static auto lastGlobalPosition = e->globalPos();
	auto reallyMoved = (lastGlobalPosition != e->globalPos());
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _mouseAction != MouseAction::None) {
		mouseReleaseEvent(e);
	}
	if (reallyMoved) {
		lastGlobalPosition = e->globalPos();
		if (!buttonsPressed || (_scrollDateLink && ClickHandler::getPressed() == _scrollDateLink)) {
			keepScrollDateForNow();
		}
	}
	mouseActionUpdate(e->globalPos());
}

void InnerWidget::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void InnerWidget::enterEventHook(QEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return TWidget::enterEventHook(e);
}

void InnerWidget::leaveEventHook(QEvent *e) {
	repaintItem(base::take(_itemOver));
	ClickHandler::clearActive();
	Ui::Tooltip::Hide();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return TWidget::leaveEventHook(e);
}

void InnerWidget::mouseActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	ClickHandler::pressed();
	if (_itemPressed != _itemOver) {
		repaintItem(_itemPressed);
		_itemPressed = _itemOver;
		repaintItem(_itemPressed);
	}

	_mouseAction = MouseAction::None;
	_mouseActionItem = _itemNearest;
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
			dragState = _mouseActionItem->getState(_dragStartPosition, request).data;
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
		} else if (_itemPressed) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->getState(_dragStartPosition, request).data;
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (_itemPressed) {
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
					//if (dynamic_cast<HistorySticker*>(_itemPressed->getMedia())) {
					//	_mouseAction = MouseAction::PrepareDrag; // start sticker drag or by-date drag
					//} else { // TODO
						if (dragState.afterSymbol) ++_mouseTextSymbol;
						auto selection = TextSelection { _mouseTextSymbol, _mouseTextSymbol };
						repaintItem(std::exchange(_selectedItem, _mouseActionItem));
						_selectedText = selection;
						_mouseAction = MouseAction::Selecting;
						repaintItem(_mouseActionItem);
					//} // TODO
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

void InnerWidget::mouseActionUpdate(const QPoint &screenPos) {
	_mousePosition = screenPos;
	updateSelected();
}

void InnerWidget::mouseActionCancel() {
	_mouseActionItem = nullptr;
	_mouseAction = MouseAction::None;
	_dragStartPosition = QPoint(0, 0);
	_wasSelectedText = false;
	//_widget->noSelectingScroll(); // TODO
}

void InnerWidget::mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);

	ClickHandlerPtr activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging) {
		activated.clear();
	}
	repaintItem(base::take(_itemPressed));

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
	//_widget->noSelectingScroll(); // TODO

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (_selectedItem && _selectedText.from != _selectedText.to) {
		setToClipboard(_selectedItem->selectedText(_selectedText), QClipboard::Selection);
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void InnerWidget::updateSelected() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(snap(mousePosition.x(), 0, width()), snap(mousePosition.y(), _visibleTop, _visibleBottom));

	auto itemPoint = QPoint();
	auto start = std::rbegin(_items), end = std::rend(_items);
	auto from = (point.y() >= _itemsTop && point.y() < _itemsTop + _itemsHeight) ? std::upper_bound(start, end, point.y(), [this](int top, auto &elem) {
		return top <= itemTop(elem.get()) + elem->height();
	}) : end;
	if (from != end) {
		_itemNearest = from->get();
		itemPoint = mapPointToItem(point, _itemNearest);
		if (_itemNearest->hasPoint(itemPoint)) {
			if (_itemOver != _itemNearest) {
				repaintItem(std::exchange(_itemOver, _itemNearest));
				repaintItem(_itemNearest);
			}
		} else {
			repaintItem(base::take(_itemOver));
		}
	} else {
		_itemNearest = nullptr;
	}

	Item::TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto selectingText = (_itemNearest == _mouseActionItem && _itemNearest == _itemOver && _selectedItem);
	if (_itemNearest) {
		if (_itemNearest != _mouseActionItem || (itemPoint - _dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [this] { performDrag(); });
			}
		}

		auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
		auto scrollDateOpacity = _scrollDateOpacity.current(_scrollDateShown ? 1. : 0.);
		//enumerateDates([this, &dragState, &lnkhost, &point, scrollDateOpacity, dateHeight/*, lastDate, showFloatingBefore*/](HistoryItem *item, int itemtop, int dateTop) {
		//	// stop enumeration if the date is above our point
		//	if (dateTop + dateHeight <= point.y()) {
		//		return false;
		//	}

		//	bool displayDate = item->displayDate();
		//	bool dateInPlace = displayDate;
		//	if (dateInPlace) {
		//		int correctDateTop = itemtop + st::msgServiceMargin.top();
		//		dateInPlace = (dateTop < correctDateTop + dateHeight);
		//	}

		//	// stop enumeration if we've found a date under the cursor
		//	if (dateTop <= point.y()) {
		//		auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
		//		if (opacity > 0.) {
		//			auto dateWidth = 0;
		//			if (auto date = item->Get<HistoryMessageDate>()) {
		//				dateWidth = date->_width;
		//			} else {
		//				dateWidth = st::msgServiceFont->width(langDayOfMonthFull(item->date.date()));
		//			}
		//			dateWidth += st::msgServicePadding.left() + st::msgServicePadding.right();
		//			auto dateLeft = st::msgServiceMargin.left();
		//			auto maxwidth = item->history()->width;
		//			if (Adaptive::ChatWide()) {
		//				maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
		//			}
		//			auto widthForDate = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

		//			dateLeft += (widthForDate - dateWidth) / 2;

		//			if (point.x() >= dateLeft && point.x() < dateLeft + dateWidth) {
		//				if (!_scrollDateLink) {
		//					_scrollDateLink = MakeShared<DateClickHandler>(item->history()->peer, item->date.date());
		//				} else {
		//					static_cast<DateClickHandler*>(_scrollDateLink.data())->setDate(item->date.date());
		//				}
		//				dragState.link = _scrollDateLink;
		//				lnkhost = item;
		//			}
		//		}
		//		return false;
		//	}
		//	return true;
		//}); // TODO
		if (!dragState.data.link) {
			HistoryStateRequest request;
			if (_mouseAction == MouseAction::Selecting) {
				request.flags |= Text::StateRequest::Flag::LookupSymbol;
			} else {
				selectingText = false;
			}
			dragState = _itemNearest->getState(itemPoint, request);
			lnkhost = dragState.handler;
			if (!dragState.data.link && itemPoint.x() >= st::historyPhotoLeft && itemPoint.x() < st::historyPhotoLeft + st::msgPhotoSize) {
				//if (auto msg = item->toHistoryMessage()) {
				//	if (msg->hasFromPhoto()) {
				//		enumerateUserpics([&dragState, &lnkhost, &point](HistoryMessage *message, int userpicTop) -> bool {
				//			// stop enumeration if the userpic is below our point
				//			if (userpicTop > point.y()) {
				//				return false;
				//			}

				//			// stop enumeration if we've found a userpic under the cursor
				//			if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
				//				dragState.link = message->from()->openLink();
				//				lnkhost = message;
				//				return false;
				//			}
				//			return true;
				//		});
				//	}
				//} // TODO
			}
		}
	}
	auto lnkChanged = ClickHandler::setActive(dragState.data.link, lnkhost);
	if (lnkChanged || dragState.data.cursor != _mouseCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.data.link || dragState.data.cursor == HistoryInDateCursorState || dragState.data.cursor == HistoryInForwardedCursorState) {
		Ui::Tooltip::Show(1000, this);
	}

	auto cursor = style::cur_default;
	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.data.cursor;
		if (dragState.data.link) {
			cursor = style::cur_pointer;
		} else if (_mouseCursorState == HistoryInTextCursorState) {
			cursor = style::cur_text;
		} else if (_mouseCursorState == HistoryInDateCursorState) {
//			cursor = style::cur_cross;
		}
	} else if (_itemNearest) {
		if (_mouseAction == MouseAction::Selecting) {
			if (selectingText) {
				auto second = dragState.data.symbol;
				if (dragState.data.afterSymbol && _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selection = _mouseActionItem->adjustSelection({ qMin(second, _mouseTextSymbol), qMax(second, _mouseTextSymbol) }, _mouseSelectType);
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
	if (_itemPressed) {
		auto adjustedPoint = mapPointToItem(point, _itemPressed);
		_itemPressed->updatePressed(adjustedPoint);
	}

	//if (_mouseAction == MouseAction::Selecting) {
	//	_widget->checkSelectingScroll(mousePos);
	//} else {
	//	_widget->noSelectingScroll();
	//} // TODO

	if (_mouseAction == MouseAction::None && (lnkChanged || cursor != _cursor)) {
		setCursor(_cursor = cursor);
	}
}

void InnerWidget::performDrag() {
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
	//			mimeData->setData(qsl("application/x-td-forward-selected"), "1");
	//		}
	//	}
	//	_controller->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryMedia*>(nullptr);
	//	if (auto pressedItem = App::pressedItem()) {
	//		pressedMedia = pressedItem->getMedia();
	//		if (_mouseCursorState == HistoryInDateCursorState || (pressedMedia && pressedMedia->dragItem())) {
	//			forwardMimeType = qsl("application/x-td-forward-pressed");
	//		}
	//	}
	//	if (auto pressedLnkItem = App::pressedLinkItem()) {
	//		if ((pressedMedia = pressedLnkItem->getMedia())) {
	//			if (forwardMimeType.isEmpty() && pressedMedia->dragItemByHandler(pressedHandler)) {
	//				forwardMimeType = qsl("application/x-td-forward-pressed-link");
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
	//} // TODO
}

int InnerWidget::itemTop(gsl::not_null<Item*> item) const {
	return _itemsTop + item->top();
}

void InnerWidget::repaintItem(Item *item) {
	if (!item) {
		return;
	}
	update(0, itemTop(item), width(), item->height());
}

QPoint InnerWidget::mapPointToItem(QPoint point, Item *item) const {
	if (!item) {
		return QPoint();
	}
	return point - QPoint(0, itemTop(item));
}

InnerWidget::~InnerWidget() = default;

} // namespace AdminLog
