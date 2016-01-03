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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "lang.h"
#include "window.h"
#include "mainwidget.h"
#include "overviewwidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "boxes/photocropbox.h"
#include "application.h"
#include "gui/filedialog.h"

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

OverviewInner::OverviewInner(OverviewWidget *overview, ScrollArea *scroll, PeerData *peer, MediaOverviewType type) : QWidget(0)
, _overview(overview)
, _scroll(scroll)
, _resizeIndex(-1)
, _resizeSkip(0)
, _peer(peer->migrateTo() ? peer->migrateTo() : peer)
, _type(type)
, _reversed(_type != OverviewDocuments && _type != OverviewLinks)
, _migrated(_peer->migrateFrom() ? App::history(_peer->migrateFrom()->id) : 0)
, _history(App::history(_peer->id))
, _channel(peerToChannel(_peer->id))
, _selMode(false)
, _rowsLeft(0)
, _rowWidth(st::msgMinWidth)
, _search(this, st::dlgFilter, lang(lng_dlg_filter))
, _cancelSearch(this, st::btnCancelSearch)
, _itemsToBeLoaded(LinksOverviewPerPage * 2)
, _photosInRow(1)
, _photosToAdd(0)
, _inSearch(false)
, _searchFull(false)
, _searchFullMigrated(false)
, _searchRequest(0)
, _lastSearchId(0)
, _lastSearchMigratedId(0)
, _searchedCount(0)
, _width(st::wndMinWidth)
, _height(0)
, _minHeight(0)
, _marginTop(0)
, _marginBottom(0)
, _cursor(style::cur_default)
, _cursorState(HistoryDefaultCursorState)
, _dragAction(NoDrag)
, _dragItem(0)
, _selectedMsgId(0)
, _dragItemIndex(-1)
, _mousedItem(0)
, _mousedItemIndex(-1)
, _dragWasInactive(false)
, _dragSelFrom(0)
, _dragSelTo(0)
, _dragSelecting(false)
, _touchScroll(false)
, _touchSelect(false)
, _touchInProgress(false)
, _touchScrollState(TouchScrollManual)
, _touchPrevPosValid(false)
, _touchWaitingAcceleration(false)
, _touchSpeedTime(0)
, _touchAccelerationTime(0)
, _touchTime(0)
, _menu(0) {
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));

	resize(_width, st::wndMinHeight);

	App::contextItem(0);

	_linkTipTimer.setSingleShot(true);
	connect(&_linkTipTimer, SIGNAL(timeout()), this, SLOT(showLinkTip()));
	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));

	mediaOverviewUpdated();
	setMouseTracking(true);

	connect(&_cancelSearch, SIGNAL(clicked()), this, SLOT(onCancelSearch()));
	connect(&_search, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(&_search, SIGNAL(changed()), this, SLOT(onSearchUpdate()));

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	_cancelSearch.hide();
	if (_type == OverviewLinks || _type == OverviewDocuments) {
		_search.show();
	} else {
		_search.hide();
	}
}

bool OverviewInner::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}

	return QWidget::event(e);
}

void OverviewInner::touchUpdateSpeed() {
	const uint64 nowTime = getms();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > FingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > FingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == TouchScrollAuto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(snap((oldSpeedY + (newSpeedY / 4)), -MaxScrollAccelerated, +MaxScrollAccelerated));
					_touchSpeed.setX(snap((oldSpeedX + (newSpeedX / 4)), -MaxScrollAccelerated, +MaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				// we average the speed to avoid strange effects with the last delta
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(snap((_touchSpeed.x() / 4) + (newSpeedX * 3 / 4), -MaxScrollFlick, +MaxScrollFlick));
					_touchSpeed.setY(snap((_touchSpeed.y() / 4) + (newSpeedY * 3 / 4), -MaxScrollFlick, +MaxScrollFlick));
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

bool OverviewInner::itemMigrated(MsgId msgId) const {
	return _migrated && (msgId < 0) && (-msgId < ServerMaxMsgId);
}

ChannelId OverviewInner::itemChannel(MsgId msgId) const {
	return itemMigrated(msgId) ? _migrated->channelId() : _channel;
}

MsgId OverviewInner::itemMsgId(MsgId msgId) const {
	return itemMigrated(msgId) ? -msgId : msgId;
}

int32 OverviewInner::migratedIndexSkip() const {
	return (_migrated && _history->overviewLoaded(_type)) ? _migrated->overview[_type].size() : 0;
}

void OverviewInner::fixItemIndex(int32 &current, MsgId msgId) const {
	if (!msgId) {
		current = -1;
	} else {
		int32 l = _items.size();
		if (current < 0 || current >= l || complexMsgId(_items.at(current)->getItem()) != msgId) {
			current = -1;
			for (int32 i = 0; i < l; ++i) {
				if (complexMsgId(_items.at(i)->getItem()) == msgId) {
					current = i;
					break;
				}
			}
		}
	}
}

void OverviewInner::searchReceived(SearchRequestType type, const MTPmessages_Messages &result, mtpRequestId req) {
	if (!_search.text().isEmpty()) {
		if (type == SearchFromStart) {
			SearchQueries::iterator i = _searchQueries.find(req);
			if (i != _searchQueries.cend()) {
				_searchCache[i.value()] = result;
				_searchQueries.erase(i);
			}
		}
	}

	if (_searchRequest == req) {
		const QVector<MTPMessage> *messages = 0;
		switch (result.type()) {
		case mtpc_messages_messages: {
			const MTPDmessages_messages &d(result.c_messages_messages());
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			messages = &d.vmessages.c_vector().v;
			_searchedCount = messages->size();
		} break;

		case mtpc_messages_messagesSlice: {
			const MTPDmessages_messagesSlice &d(result.c_messages_messagesSlice());
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			messages = &d.vmessages.c_vector().v;
			_searchedCount = d.vcount.v;
		} break;

		case mtpc_messages_channelMessages: {
			const MTPDmessages_channelMessages &d(result.c_messages_channelMessages());
			if (_peer && _peer->isChannel()) {
				_peer->asChannel()->ptsReceived(d.vpts.v);
			} else {
				LOG(("API Error: received messages.channelMessages when no channel was passed! (OverviewInner::searchReceived)"));
			}
			if (d.has_collapsed()) { // should not be returned
				LOG(("API Error: channels.getMessages and messages.getMessages should not return collapsed groups! (OverviewInner::searchReceived)"));
			}

			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			messages = &d.vmessages.c_vector().v;
			_searchedCount = d.vcount.v;
		} break;
		}
		if (messages) {
			bool migratedSearch = (type == SearchMigratedFromStart || type == SearchMigratedFromOffset);
			if (messages->isEmpty()) {
				if (migratedSearch) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
			if (type == SearchFromStart) {
				_searchResults.clear();
				_lastSearchId = _lastSearchMigratedId = 0;
				_itemsToBeLoaded = LinksOverviewPerPage * 2;
			}
			if (type == SearchMigratedFromStart) {
				_lastSearchMigratedId = 0;
			}
			for (QVector<MTPMessage>::const_iterator i = messages->cbegin(), e = messages->cend(); i != e; ++i) {
				HistoryItem *item = App::histories().addNewMessage(*i, NewMessageExisting);
				if (migratedSearch) {
					_searchResults.push_front(-item->id);
					_lastSearchMigratedId = item->id;
				} else {
					_searchResults.push_front(item->id);
					_lastSearchId = item->id;
				}
			}
			mediaOverviewUpdated();
			if (messages->isEmpty()) {
				update();
			}
		}

		_searchRequest = 0;
		_overview->onScroll();
	}
}

bool OverviewInner::searchFailed(SearchRequestType type, const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (_searchRequest == req) {
		_searchRequest = 0;
		if (type == SearchFromStart || type == SearchFromOffset) {
			_searchFull = true;
		} else if (type == SearchMigratedFromStart || type == SearchMigratedFromOffset) {
			_searchFullMigrated = true;
		}
	}
	return true;
}

bool OverviewInner::itemHasPoint(MsgId msgId, int32 index, int32 x, int32 y) const {
	fixItemIndex(index, msgId);
	if (index < 0) return false;

	if (_type == OverviewPhotos || _type == OverviewVideos) {
		if (x >= 0 && x < _rowWidth && y >= 0 && y < _rowWidth) {
			return true;
		}
	} else {
		if (x >= _rowsLeft && x < _rowsLeft + _rowWidth && y >= 0 && y < itemHeight(msgId, index)) {
			return true;
		}
	}
	return false;
}

int32 OverviewInner::itemHeight(MsgId msgId, int32 index) const {
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		return _rowWidth;
	}

	fixItemIndex(index, msgId);
	return (index < 0) ? 0 : _items.at(index)->height();
}

void OverviewInner::moveToNextItem(MsgId &msgId, int32 &index, MsgId upTo, int32 delta) const {
	fixItemIndex(index, msgId);
	if (msgId == upTo || index < 0) {
		msgId = 0;
		index = -1;
		return;
	}

	index += delta;
	while (index >= 0 && index < _items.size() && !_items.at(index)->toLayoutMediaItem()) {
		index += (delta > 0) ? 1 : -1;
	}
	if (index < 0 || index >= _items.size()) {
		msgId = 0;
		index = -1;
	} else {
		msgId = complexMsgId(_items.at(index)->getItem());
	}
}

void OverviewInner::repaintItem(MsgId itemId, int32 itemIndex) {
	fixItemIndex(itemIndex, itemId);
	if (itemIndex >= 0) {
		if (_type == OverviewPhotos || _type == OverviewVideos) {
			int32 shownAtIndex = _items.size() - itemIndex - 1;
			float64 w = (float64(_width - st::overviewPhotoSkip) / _photosInRow);
			int32 vsize = (_rowWidth + st::overviewPhotoSkip);
			int32 row = (_photosToAdd + shownAtIndex) / _photosInRow, col = (_photosToAdd + shownAtIndex) % _photosInRow;
			update(int32(col * w), _marginTop + int32(row * vsize), qCeil(w), vsize);
		} else {
			int32 top = _items.at(itemIndex)->getOverviewItemInfo()->top();
			if (_reversed) top = _height - top;
			update(_rowsLeft, _marginTop + top, _rowWidth, _items.at(itemIndex)->height());
		}
	}
}

void OverviewInner::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

void OverviewInner::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void OverviewInner::touchEvent(QTouchEvent *e) {
	const Qt::TouchPointStates &states(e->touchPointStates());
	if (e->type() == QEvent::TouchCancel) { // cancel
		if (!_touchInProgress) return;
		_touchInProgress = false;
		_touchSelectTimer.stop();
		_touchScroll = _touchSelect = false;
		_touchScrollState = TouchScrollManual;
		dragActionCancel();
		return;
	}

	if (!e->touchPoints().isEmpty()) {
		_touchPrevPos = _touchPos;
		_touchPos = e->touchPoints().cbegin()->screenPos().toPoint();
	}

	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_touchInProgress) return;
		if (e->touchPoints().isEmpty()) return;

		_touchInProgress = true;
		if (_touchScrollState == TouchScrollAuto) {
			_touchScrollState = TouchScrollAcceleration;
			_touchWaitingAcceleration = true;
			_touchAccelerationTime = getms();
			touchUpdateSpeed();
			_touchStart = _touchPos;
		} else {
			_touchScroll = false;
			_touchSelectTimer.start(QApplication::startDragTime());
		}
		_touchSelect = false;
		_touchStart = _touchPrevPos = _touchPos;
		break;

	case QEvent::TouchUpdate:
		if (!_touchInProgress) return;
		if (_touchSelect) {
			dragActionUpdate(_touchPos);
		} else if (!_touchScroll && (_touchPos - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchSelectTimer.stop();
			_touchScroll = true;
			touchUpdateSpeed();
		}
		if (_touchScroll) {
			if (_touchScrollState == TouchScrollManual) {
				touchScrollUpdated(_touchPos);
			} else if (_touchScrollState == TouchScrollAcceleration) {
				touchUpdateSpeed();
				_touchAccelerationTime = getms();
				if (_touchSpeed.isNull()) {
					_touchScrollState = TouchScrollManual;
				}
			}
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchInProgress) return;
		_touchInProgress = false;
		if (_touchSelect) {
			dragActionFinish(_touchPos, Qt::RightButton);
			QContextMenuEvent contextMenu(QContextMenuEvent::Mouse, mapFromGlobal(_touchPos), _touchPos);
			showContextMenu(&contextMenu, true);
			_touchScroll = false;
		} else if (_touchScroll) {
			if (_touchScrollState == TouchScrollManual) {
				_touchScrollState = TouchScrollAuto;
				_touchPrevPosValid = false;
				_touchScrollTimer.start(15);
				_touchTime = getms();
			} else if (_touchScrollState == TouchScrollAuto) {
				_touchScrollState = TouchScrollManual;
				_touchScroll = false;
				touchResetSpeed();
			} else if (_touchScrollState == TouchScrollAcceleration) {
				_touchScrollState = TouchScrollAuto;
				_touchWaitingAcceleration = false;
				_touchPrevPosValid = false;
			}
		} else { // one short tap -- like mouse click
			dragActionStart(_touchPos);
			dragActionFinish(_touchPos);
		}
		_touchSelectTimer.stop();
		_touchSelect = false;
		break;
	}
}

void OverviewInner::dragActionUpdate(const QPoint &screenPos) {
	_dragPos = screenPos;
	onUpdateSelected();
}

void OverviewInner::dragActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	dragActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	if (textlnkDown() != textlnkOver()) {
		repaintItem(App::pressedLinkItem());
		textlnkDown(textlnkOver());
		App::pressedLinkItem(App::hoveredLinkItem());
		repaintItem(App::pressedLinkItem());
	}

	_dragAction = NoDrag;
	_dragItem = _mousedItem;
	_dragItemIndex = _mousedItemIndex;
	_dragStartPos = mapMouseToItem(mapFromGlobal(screenPos), _dragItem, _dragItemIndex);
	_dragWasInactive = App::wnd()->inactivePress();
	if (_dragWasInactive) App::wnd()->inactivePress(false);
	if (textlnkDown() && _selected.isEmpty()) {
		_dragAction = PrepareDrag;
	} else if (!_selected.isEmpty()) {
		if (_selected.cbegin().value() == FullSelection) {
			if (_selected.constFind(_dragItem) != _selected.cend() && textlnkDown()) {
				_dragAction = PrepareDrag; // start items drag
			} else {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}
	if (_dragAction == NoDrag && _dragItem) {
		bool afterDragSymbol = false , uponSymbol = false;
		uint16 symbol = 0;
		if (!_dragWasInactive) {
			if (textlnkDown()) {
				_dragSymbol = symbol;
				uint32 selStatus = (_dragSymbol << 16) | _dragSymbol;
				if (selStatus != FullSelection && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection)) {
					if (!_selected.isEmpty()) {
						repaintItem(_selected.cbegin().key(), -1);
						_selected.clear();
					}
					_selected.insert(_dragItem, selStatus);
					_dragAction = Selecting;
					repaintItem(_dragItem, _dragItemIndex);
					_overview->updateTopBarSelection();
				} else {
					_dragAction = PrepareSelect;
				}
			} else {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}

	if (!_dragItem) {
		_dragAction = NoDrag;
	} else if (_dragAction == NoDrag) {
		_dragItem = 0;
	}
}

void OverviewInner::dragActionCancel() {
	_dragItem = 0;
	_dragItemIndex = -1;
	_dragAction = NoDrag;
	_dragSelFrom = _dragSelTo = 0;
	_dragSelFromIndex = _dragSelToIndex = -1;
	_dragStartPos = QPoint(0, 0);
	_overview->noSelectingScroll();
}

void OverviewInner::dragActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	TextLinkPtr needClick;

	dragActionUpdate(screenPos);

	if (textlnkOver()) {
		if (textlnkDown() == textlnkOver() && _dragAction != Dragging && !_selMode) {
			needClick = textlnkDown();
		}
	}
	if (textlnkDown()) {
		repaintItem(App::pressedLinkItem());
		textlnkDown(TextLinkPtr());
		App::pressedLinkItem(0);
		if (!textlnkOver() && _cursor != style::cur_default) {
			_cursor = style::cur_default;
			setCursor(_cursor);
		}
	}
	if (needClick) {
		needClick->onClick(button);
		dragActionCancel();
		return;
	}
	if (_dragAction == PrepareSelect && !needClick && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i == _selected.cend() && itemMsgId(_dragItem) > 0) {
			if (_selected.size() < MaxSelectedItems) {
				if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
					_selected.clear();
				}
				_selected.insert(_dragItem, FullSelection);
			}
		} else {
			_selected.erase(i);
		}
		repaintItem(_dragItem, _dragItemIndex);
	} else if (_dragAction == PrepareDrag && !needClick && !_dragWasInactive && button != Qt::RightButton) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i != _selected.cend() && i.value() == FullSelection) {
			_selected.erase(i);
			repaintItem(_dragItem, _dragItemIndex);
		} else if (i == _selected.cend() && itemMsgId(_dragItem) > 0 && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.insert(_dragItem, FullSelection);
				repaintItem(_dragItem, _dragItemIndex);
			}
		} else {
			_selected.clear();
			update();
		}
	} else if (_dragAction == Selecting) {
		if (_dragSelFrom && _dragSelTo) {
			applyDragSelection();
		} else if (!_selected.isEmpty() && !_dragWasInactive) {
			uint32 sel = _selected.cbegin().value();
			if (sel != FullSelection && (sel & 0xFFFF) == ((sel >> 16) & 0xFFFF)) {
				_selected.clear();
				App::main()->activate();
			}
		}
	}
	_dragAction = NoDrag;
	_overview->noSelectingScroll();
	_overview->updateTopBarSelection();
}

void OverviewInner::onDragExec() {
	if (_dragAction != Dragging) return;

	bool uponSelected = false;
	if (_dragItem) {
		bool afterDragSymbol;
		uint16 symbol;
		if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
			uponSelected = _selected.contains(_dragItem);
		} else {
			uponSelected = false;
		}
	}
	QString sel;
	QList<QUrl> urls;
	bool forwardSelected = false;
	if (uponSelected) {
		forwardSelected = !_selected.isEmpty() && _selected.cbegin().value() == FullSelection && cWideMode();
	} else if (textlnkDown()) {
		sel = textlnkDown()->encoded();
		if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
//			urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
		}
	}
	if (!sel.isEmpty() || forwardSelected) {
		updateDragSelection(0, -1, 0, -1, false);
		_overview->noSelectingScroll();

		QDrag *drag = new QDrag(App::wnd());
		QMimeData *mimeData = new QMimeData;

		if (!sel.isEmpty()) mimeData->setText(sel);
		if (!urls.isEmpty()) mimeData->setUrls(urls);
		if (forwardSelected) {
			mimeData->setData(qsl("application/x-td-forward-selected"), "1");
		}
		drag->setMimeData(mimeData);
		drag->exec(Qt::CopyAction);
		if (App::main()) App::main()->updateAfterDrag();
		return;
	} else {
		HistoryItem *pressedLnkItem = App::pressedLinkItem(), *pressedItem = App::pressedItem();
		QLatin1String lnkType = (textlnkDown() && pressedLnkItem) ? textlnkDown()->type() : qstr("");
		bool lnkPhoto = (lnkType == qstr("PhotoLink")),
			lnkVideo = (lnkType == qstr("VideoOpenLink")),
			lnkAudio = (lnkType == qstr("AudioOpenLink")),
			lnkDocument = (lnkType == qstr("DocumentOpenLink") || lnkType == qstr("GifOpenLink"));
		if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument) {
			QDrag *drag = new QDrag(App::wnd());
			QMimeData *mimeData = new QMimeData;

			mimeData->setData(qsl("application/x-td-forward-pressed-link"), "1");
			if (lnkDocument) {
				QString already = static_cast<DocumentOpenLink*>(textlnkDown().data())->document()->already(true);
				if (!already.isEmpty()) {
					QList<QUrl> urls;
					urls.push_back(QUrl::fromLocalFile(already));
					mimeData->setUrls(urls);
				}
			}

			drag->setMimeData(mimeData);
			drag->exec(Qt::CopyAction);
			if (App::main()) App::main()->updateAfterDrag();
			return;
		}
	}
}

void OverviewInner::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	_overview->touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

void OverviewInner::addSelectionRange(int32 selFrom, int32 selTo, History *history) {
	if (selFrom < 0 || selTo < 0) return;
	for (int32 i = selFrom; i <= selTo; ++i) {
		MsgId msgid = complexMsgId(_items.at(i)->getItem());
		if (!msgid) continue;

		SelectedItems::iterator j = _selected.find(msgid);
		if (_dragSelecting && itemMsgId(msgid) > 0) {
			if (j == _selected.cend()) {
				if (_selected.size() >= MaxSelectedItems) break;
				_selected.insert(msgid, FullSelection);
			} else if (j.value() != FullSelection) {
				*j = FullSelection;
			}
		} else {
			if (j != _selected.cend()) {
				_selected.erase(j);
			}
		}
	}
}

void OverviewInner::applyDragSelection() {
	if (_dragSelFromIndex < 0 || _dragSelToIndex < 0) return;

	if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
		_selected.clear();
	}
	addSelectionRange(_dragSelToIndex, _dragSelFromIndex, _history);

	_dragSelFrom = _dragSelTo = 0;
	_dragSelFromIndex = _dragSelToIndex = -1;
	_overview->updateTopBarSelection();
}

QPoint OverviewInner::mapMouseToItem(QPoint p, MsgId itemId, int32 itemIndex) {
	fixItemIndex(itemIndex, itemId);
	if (itemIndex < 0) return QPoint(0, 0);

	if (_type == OverviewPhotos || _type == OverviewVideos) {
		int32 shownAtIndex = _items.size() - itemIndex - 1;
		int32 row = (_photosToAdd + shownAtIndex) / _photosInRow, col = (_photosToAdd + shownAtIndex) % _photosInRow;
		float64 w = (_width - st::overviewPhotoSkip) / float64(_photosInRow);
		p.setX(p.x() - int32(col * w) - st::overviewPhotoSkip);
		p.setY(p.y() - _marginTop - row * (_rowWidth + st::overviewPhotoSkip) - st::overviewPhotoSkip);
	} else {
		int32 top = _items.at(itemIndex)->getOverviewItemInfo()->top();
		if (_reversed) top = _height - top;
		p.setY(p.y() - _marginTop - top);
	}
	return p;
}

void OverviewInner::activate() {
	if (_type == OverviewLinks || _type == OverviewDocuments) {
		_search.setFocus();
	} else {
		setFocus();
	}
}

void OverviewInner::clear() {
	_selected.clear();
	_dragItemIndex = _mousedItemIndex = _dragSelFromIndex = _dragSelToIndex = -1;
	_dragItem = _mousedItem = _dragSelFrom = _dragSelTo = 0;
	_dragAction = NoDrag;
	for (LayoutItems::const_iterator i = _layoutItems.cbegin(), e = _layoutItems.cend(); i != e; ++i) {
		delete i.value();
	}
	_layoutItems.clear();
	for (LayoutDates::const_iterator i = _layoutDates.cbegin(), e = _layoutDates.cend(); i != e; ++i) {
		delete i.value();
	}
	_layoutDates.clear();
	_items.clear();
}

int32 OverviewInner::itemTop(const FullMsgId &msgId) const {
	if (_type == OverviewAudioDocuments) {
		int32 itemIndex = -1;
		fixItemIndex(itemIndex, (msgId.channel == _channel) ? msgId.msg : ((_migrated && msgId.channel == _migrated->channelId()) ? -msgId.msg : 0));
		if (itemIndex >= 0) {
			int32 top = _items.at(itemIndex)->getOverviewItemInfo()->top();
			if (_reversed) top = _height - top;
			return _marginTop + top;
		}
	}
	return -1;
}

void OverviewInner::preloadMore() {
	if (_inSearch) {
		if (!_searchRequest) {
			MTPmessagesFilter filter = (_type == OverviewLinks) ? MTP_inputMessagesFilterUrl() : MTP_inputMessagesFilterDocument();
			if (!_searchFull) {
				int32 flags = (_history->peer->isChannel() && !_history->peer->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
				_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _history->peer->input, MTP_string(_searchQuery), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(_lastSearchId), MTP_int(SearchPerPage)), rpcDone(&OverviewInner::searchReceived, _lastSearchId ? SearchFromOffset : SearchFromStart), rpcFail(&OverviewInner::searchFailed, _lastSearchId ? SearchFromOffset : SearchFromStart));
				if (!_lastSearchId) {
					_searchQueries.insert(_searchRequest, _searchQuery);
				}
			} else if (_migrated && !_searchFullMigrated) {
				int32 flags = (_migrated->peer->isChannel() && !_migrated->peer->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
				_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _migrated->peer->input, MTP_string(_searchQuery), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(_lastSearchMigratedId), MTP_int(SearchPerPage)), rpcDone(&OverviewInner::searchReceived, _lastSearchMigratedId ? SearchMigratedFromOffset : SearchMigratedFromStart), rpcFail(&OverviewInner::searchFailed, _lastSearchMigratedId ? SearchMigratedFromOffset : SearchMigratedFromStart));
			}
		}
	} else if (App::main()) {
		if (_migrated && _history->overviewLoaded(_type)) {
			App::main()->loadMediaBack(_migrated->peer, _type, true);
		} else {
			App::main()->loadMediaBack(_history->peer, _type, true);
		}
	}
}

bool OverviewInner::preloadLocal() {
	if (_itemsToBeLoaded >= migratedIndexSkip() + _history->overview[_type].size()) return false;
	_itemsToBeLoaded += LinksOverviewPerPage;
	mediaOverviewUpdated();
	return true;
}

uint32 OverviewInner::itemSelectedValue(int32 index) const {
	int32 selfrom = -1, selto = -1;
	if (_dragSelFromIndex >= 0 && _dragSelToIndex >= 0) {
		selfrom = _dragSelToIndex;
		selto = _dragSelFromIndex;
	}
	if (_items.at(index)->toLayoutMediaItem()) { // draw item
		if (index >= _dragSelToIndex && index <= _dragSelFromIndex && _dragSelToIndex >= 0) {
			return (_dragSelecting && _items.at(index)->msgId() > 0) ? FullSelection : 0;
		} else if (!_selected.isEmpty()) {
			SelectedItems::const_iterator j = _selected.constFind(complexMsgId(_items.at(index)->getItem()));
			if (j != _selected.cend()) {
				return j.value();
			}
		}
	}
	return 0;
}

void OverviewInner::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);

	QRect r(e->rect());
	bool trivial = (r == rect());
	if (!trivial) {
		p.setClipRect(r);
	}
	uint64 ms = getms();
	OverviewPaintContext context(ms, _selMode);

	if (_history->overview[_type].isEmpty() && (!_migrated || !_history->overviewLoaded(_type) || _migrated->overview[_type].isEmpty())) {
		QPoint dogPos((_width - st::msgDogImg.pxWidth()) / 2, ((height() - st::msgDogImg.pxHeight()) * 4) / 9);
		p.drawPixmap(dogPos, *cChatDogImage());
		return;
	} else if (_inSearch && _searchResults.isEmpty() && _searchFull && (!_migrated || _searchFullMigrated) && !_searchTimer.isActive()) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(_rowsLeft, _marginTop, _rowWidth, _marginTop), lng_search_found_results(lt_count, 0), style::al_center);
		return;
	}

	int32 selfrom = -1, selto = -1;
	if (_dragSelFromIndex >= 0 && _dragSelToIndex >= 0) {
		selfrom = _dragSelToIndex;
		selto = _dragSelFromIndex;
	}

	SelectedItems::const_iterator selEnd = _selected.cend();
	bool hasSel = !_selected.isEmpty();

	if (_type == OverviewPhotos || _type == OverviewVideos) {
		int32 count = _items.size(), rowsCount = (_photosToAdd + count) / _photosInRow + (((_photosToAdd + count) % _photosInRow) ? 1 : 0);
		int32 rowFrom = floorclamp(r.y() - _marginTop - st::overviewPhotoSkip, _rowWidth + st::overviewPhotoSkip, 0, rowsCount);
		int32 rowTo = ceilclamp(r.y() + r.height() - _marginTop - st::overviewPhotoSkip, _rowWidth + st::overviewPhotoSkip, 0, rowsCount);
		float64 w = float64(_width - st::overviewPhotoSkip) / _photosInRow;
		for (int32 row = rowFrom; row < rowTo; ++row) {
			if (row * _photosInRow >= _photosToAdd + count) break;
			for (int32 col = 0; col < _photosInRow; ++col) {
				int32 i = count - (row * _photosInRow + col - _photosToAdd) - 1;
				if (i < 0) continue;
				if (i >= count) break;

				QPoint pos(int32(col * w + st::overviewPhotoSkip), _marginTop + row * (_rowWidth + st::overviewPhotoSkip) + st::overviewPhotoSkip);
				p.translate(pos.x(), pos.y());
				_items.at(i)->paint(p, r.translated(-pos.x(), -pos.y()), itemSelectedValue(i), &context);
				p.translate(-pos.x(), -pos.y());
			}
		}
	} else {
		p.translate(_rowsLeft, _marginTop);
		int32 y = 0, w = _rowWidth;
		for (int32 j = 0, l = _items.size(); j < l; ++j) {
			int32 i = _reversed ? (l - j - 1) : j, nexti = _reversed ? (i - 1) : (i + 1);
			int32 nextItemTop = (j + 1 == l) ? (_reversed ? 0 : _height) : _items.at(nexti)->getOverviewItemInfo()->top();
			if (_reversed) nextItemTop = _height - nextItemTop;
			if (_marginTop + nextItemTop > r.top()) {
				OverviewItemInfo *info = _items.at(i)->getOverviewItemInfo();
				int32 curY = info->top();
				if (_reversed) curY = _height - curY;
				if (_marginTop + curY >= r.y() + r.height()) break;

				context.isAfterDate = (j > 0) ? !_items.at(j - 1)->toLayoutMediaItem() : false;
				p.translate(0, curY - y);
				_items.at(i)->paint(p, r.translated(-_rowsLeft, -_marginTop - curY), itemSelectedValue(i), &context);
				y = curY;
			}
		}
	}
}

void OverviewInner::mouseMoveEvent(QMouseEvent *e) {
	if (!(e->buttons() & (Qt::LeftButton | Qt::MiddleButton)) && (textlnkDown() || _dragAction != NoDrag)) {
		mouseReleaseEvent(e);
	}
	dragActionUpdate(e->globalPos());
}

void OverviewInner::onUpdateSelected() {
	if (isHidden()) return;

	QPoint mousePos(mapFromGlobal(_dragPos));
	QPoint m(_overview->clampMousePosition(mousePos));

	TextLinkPtr lnk;
	HistoryItem *item = 0;
	int32 index = -1;
	int32 newsel = 0;
	HistoryCursorState cursorState = HistoryDefaultCursorState;
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		float64 w = (float64(_width - st::overviewPhotoSkip) / _photosInRow);
		int32 col = int32((m.x() - (st::overviewPhotoSkip / 2)) / w), vsize = (_rowWidth + st::overviewPhotoSkip);
		int32 row = int32((m.y() - _marginTop - (st::overviewPhotoSkip / 2)) / vsize);
		if (col < 0) col = 0;
		if (row < 0) row = 0;
		bool upon = true;

		int32 count = _items.size(), i = count - (row * _photosInRow + col - _photosToAdd) - 1;
		if (i < 0) {
			i = 0;
			upon = false;
		}
		if (i >= count) {
			i = count - 1;
			upon = false;
		}
		if (i >= 0) {
			if (LayoutMediaItem *media = _items.at(i)->toLayoutMediaItem()) {
				item = media->getItem();
				index = i;
				if (upon) {
					media->getState(lnk, cursorState, m.x() - col * w - st::overviewPhotoSkip, m.y() - _marginTop - row * vsize - st::overviewPhotoSkip);
				}
			}
		}
	} else {
		for (int32 j = 0, l = _items.size(); j < l; ++j) {
			bool lastItem = (j + 1 == l);
			int32 i = _reversed ? (l - j - 1) : j, nexti = _reversed ? (i - 1) : (i + 1);
			int32 nextItemTop = lastItem ? (_reversed ? 0 : _height) : _items.at(nexti)->getOverviewItemInfo()->top();
			if (_reversed) nextItemTop = _height - nextItemTop;
			if (_marginTop + nextItemTop > m.y() || lastItem) {
				int32 top = _items.at(i)->getOverviewItemInfo()->top();
				if (_reversed) top = _height - top;
				if (!_items.at(i)->toLayoutMediaItem()) { // day item
					int32 h = _items.at(i)->height();
					bool beforeItem = (_marginTop + top + h / 2) >= m.y();
					if (_reversed) beforeItem = !beforeItem;
					if (i > 0 && (beforeItem || i == _items.size() - 1)) {
						--i;
						if (!_items.at(i)->toLayoutMediaItem()) break; // wtf
						top = _items.at(i)->getOverviewItemInfo()->top();
					} else if (i < _items.size() - 1 && (!beforeItem || !i)) {
						++i;
						if (!_items.at(i)->toLayoutMediaItem()) break; // wtf
						top = _items.at(i)->getOverviewItemInfo()->top();
					} else {
						break; // wtf
					}
					if (_reversed) top = _height - top;
					j = _reversed ? (l - i - 1) : i;
				}

				if (LayoutMediaItem *media = _items.at(i)->toLayoutMediaItem()) {
					item = media->getItem();
					index = i;
					media->getState(lnk, cursorState, m.x() - _rowsLeft, m.y() - _marginTop - top);
				}
				break;
			}
		}
	}

	MsgId oldMousedItem = _mousedItem;
	int32 oldMousedItemIndex = _mousedItemIndex;
	_mousedItem = item ? ((item->history() == _migrated) ? -item->id : item->id) : 0;
	_mousedItemIndex = index;
	m = mapMouseToItem(m, _mousedItem, _mousedItemIndex);

	Qt::CursorShape cur = style::cur_default;
	bool lnkChanged = false;
	if (lnk != textlnkOver()) {
		lnkChanged = true;
		if (textlnkOver()) {
			if (HistoryItem *item = App::hoveredLinkItem()) {
				MsgId itemId = complexMsgId(item);
				int32 itemIndex = oldMousedItemIndex;
				fixItemIndex(itemIndex, itemId);
				if (itemIndex >= 0) {
					_items.at(itemIndex)->linkOut(textlnkOver());
					repaintItem(itemId, itemIndex);
				}
			}
		}
		textlnkOver(lnk);
		QToolTip::hideText();
		App::hoveredLinkItem(lnk ? item : 0);
		if (textlnkOver()) {
			if (item && index >= 0) {
				_items.at(index)->linkOver(textlnkOver());
				repaintItem(complexMsgId(item), index);
			}
		}
	} else {
		App::mousedItem(item);
	}
	if (_mousedItem != oldMousedItem) {
		lnkChanged = true;
		if (oldMousedItem) repaintItem(oldMousedItem, oldMousedItemIndex);
		if (item) repaintItem(item);
		QToolTip::hideText();
	}
	if (_cursorState == HistoryInDateCursorState && cursorState != HistoryInDateCursorState) {
		QToolTip::hideText();
	}
	if (cursorState != _cursorState) {
		_cursorState = cursorState;
	}
	if (lnk || cursorState == HistoryInDateCursorState) {
		_linkTipTimer.start(1000);
	}

	fixItemIndex(_dragItemIndex, _dragItem);
	fixItemIndex(_mousedItemIndex, _mousedItem);
	if (_dragAction == NoDrag) {
		if (lnk) {
			cur = style::cur_pointer;
		}
	} else {
		if (_dragItemIndex < 0 || _mousedItemIndex < 0) {
			_dragAction = NoDrag;
			return;
		}
		if (_mousedItem != _dragItem || (m - _dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
			if (_dragAction == PrepareDrag) {
				_dragAction = Dragging;
				QTimer::singleShot(1, this, SLOT(onDragExec()));
			} else if (_dragAction == PrepareSelect) {
				_dragAction = Selecting;
			}
		}
		cur = textlnkDown() ? style::cur_pointer : style::cur_default;
		if (_dragAction == Selecting) {
			bool canSelectMany = (_peer != 0);
			if (_mousedItem == _dragItem && lnk && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
				bool afterSymbol = false, uponSymbol = false;
				uint16 second = 0;
				_selected[_dragItem] = 0;
				updateDragSelection(0, -1, 0, -1, false);
			} else if (canSelectMany) {
				bool selectingDown = (_reversed ? (_mousedItemIndex < _dragItemIndex) : (_mousedItemIndex > _dragItemIndex)) || (_mousedItemIndex == _dragItemIndex && ((_type == OverviewPhotos || _type == OverviewVideos) ? (_dragStartPos.x() < m.x()) : (_dragStartPos.y() < m.y())));
				MsgId dragSelFrom = _dragItem, dragSelTo = _mousedItem;
				int32 dragSelFromIndex = _dragItemIndex, dragSelToIndex = _mousedItemIndex;
				if (!itemHasPoint(dragSelFrom, dragSelFromIndex, _dragStartPos.x(), _dragStartPos.y())) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_type == OverviewPhotos || _type == OverviewVideos) {
							if (_dragStartPos.x() >= _rowWidth || ((_mousedItem == dragSelFrom) && (m.x() < _dragStartPos.x() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, -1);
							}
						} else {
							if (_dragStartPos.y() >= itemHeight(dragSelFrom, dragSelFromIndex) || ((_mousedItem == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, _reversed ? -1 : 1);
							}
						}
					} else {
						if (_type == OverviewPhotos || _type == OverviewVideos) {
							if (_dragStartPos.x() < 0 || ((_mousedItem == dragSelFrom) && (m.x() >= _dragStartPos.x() - QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, 1);
							}
						} else {
							if (_dragStartPos.y() < 0 || ((_mousedItem == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, _reversed ? 1 : -1);
							}
						}
					}
				}
				if (_dragItem != _mousedItem) { // maybe exclude dragSelTo
					if (selectingDown) {
						if (_type == OverviewPhotos || _type == OverviewVideos) {
							if (m.x() < 0) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, 1);
							}
						} else {
							if (m.y() < 0) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, _reversed ? 1 : -1);
							}
						}
					} else {
						if (_type == OverviewPhotos || _type == OverviewVideos) {
							if (m.x() >= _rowWidth) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, -1);
							}
						} else {
							if (m.y() >= itemHeight(dragSelTo, dragSelToIndex)) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, _reversed ? -1 : 1);
							}
						}
					}
				}
				bool dragSelecting = false;
				MsgId dragFirstAffected = dragSelFrom;
				int32 dragFirstAffectedIndex = dragSelFromIndex;
				while (dragFirstAffectedIndex >= 0 && itemMsgId(dragFirstAffected) <= 0) {
					moveToNextItem(dragFirstAffected, dragFirstAffectedIndex, dragSelTo, selectingDown ? (_reversed ? -1 : 1) : (_reversed ? 1 : -1));
				}
				if (dragFirstAffectedIndex >= 0) {
					SelectedItems::const_iterator i = _selected.constFind(dragFirstAffected);
					dragSelecting = (i == _selected.cend() || i.value() != FullSelection);
				}
				updateDragSelection(dragSelFrom, dragSelFromIndex, dragSelTo, dragSelToIndex, dragSelecting);
			}
		} else if (_dragAction == Dragging) {
		}

		if (textlnkDown()) {
			cur = style::cur_pointer;
		} else if (_dragAction == Selecting && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
			if (!_dragSelFrom || !_dragSelTo) {
				cur = style::cur_text;
			}
		}
	}
	if (_dragAction == Selecting) {
		_overview->checkSelectingScroll(mousePos);
	} else {
		updateDragSelection(0, -1, 0, -1, false);
		_overview->noSelectingScroll();
	}

	if (lnkChanged || cur != _cursor) {
		setCursor(_cursor = cur);
	}
}


void OverviewInner::showLinkTip() {
	TextLinkPtr lnk = textlnkOver();
	int32 dd = QApplication::startDragDistance();
	QPoint dp(mapFromGlobal(_dragPos));
	QRect r(dp.x() - dd, dp.y() - dd, 2 * dd, 2 * dd);
	if (lnk && !lnk->fullDisplayed()) {
		QToolTip::showText(_dragPos, lnk->readable(), this, r);
	} else if (_cursorState == HistoryInDateCursorState && _dragAction == NoDrag && _mousedItem) {
		if (HistoryItem *item = App::histItemById(itemChannel(_mousedItem), itemMsgId(_mousedItem))) {
			QToolTip::showText(_dragPos, item->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat)), this, r);
		}
	}
}

void OverviewInner::updateDragSelection(MsgId dragSelFrom, int32 dragSelFromIndex, MsgId dragSelTo, int32 dragSelToIndex, bool dragSelecting) {
	if (_dragSelFrom != dragSelFrom || _dragSelFromIndex != dragSelFromIndex || _dragSelTo != dragSelTo || _dragSelToIndex != dragSelToIndex || _dragSelecting != dragSelecting) {
		_dragSelFrom = dragSelFrom;
		_dragSelFromIndex = dragSelFromIndex;
		_dragSelTo = dragSelTo;
		_dragSelToIndex = dragSelToIndex;
		if (_dragSelFromIndex >= 0 && _dragSelToIndex >= 0 && _dragSelFromIndex < _dragSelToIndex) {
			qSwap(_dragSelFrom, _dragSelTo);
			qSwap(_dragSelFromIndex, _dragSelToIndex);
		}
		_dragSelecting = dragSelecting;
		update();
	}
}

void OverviewInner::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	dragActionStart(e->globalPos(), e->button());
}

void OverviewInner::mouseReleaseEvent(QMouseEvent *e) {
	dragActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void OverviewInner::keyPressEvent(QKeyEvent *e) {
	if ((_search.isHidden() || !_search.hasFocus()) && !_overview->isHidden() && e->key() == Qt::Key_Escape) {
		onCancel();
	} else if (e->key() == Qt::Key_Back) {
		App::main()->showBackFromStack();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		onSearchMessages();
	}
}

void OverviewInner::enterEvent(QEvent *e) {
	return QWidget::enterEvent(e);
}

void OverviewInner::leaveEvent(QEvent *e) {
	if (_selectedMsgId) {
		repaintItem(_selectedMsgId, -1);
		_selectedMsgId = 0;
	}
	if (textlnkOver()) {
		repaintItem(App::hoveredLinkItem());
		textlnkOver(TextLinkPtr());
		App::hoveredLinkItem(0);
		if (!textlnkDown() && _cursor != style::cur_default) {
			_cursor = style::cur_default;
			setCursor(_cursor);
		}
	}
	return QWidget::leaveEvent(e);
}

void OverviewInner::resizeEvent(QResizeEvent *e) {
	onUpdateSelected();
	update();
}

void OverviewInner::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
		repaintItem(App::contextItem());
		if (_selectedMsgId) repaintItem(_selectedMsgId, -1);
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		dragActionUpdate(e->globalPos());
	}

	bool ignoreMousedItem = false;
	if (itemMsgId(_mousedItem) > 0) {
		QPoint m = mapMouseToItem(mapFromGlobal(e->globalPos()), _mousedItem, _mousedItemIndex);
		if (m.y() < 0 || m.y() >= itemHeight(_mousedItem, _mousedItemIndex)) {
			ignoreMousedItem = true;
		}
	}

	int32 selectedForForward, selectedForDelete;
	getSelectionState(selectedForForward, selectedForDelete);

	// -2 - has full selected items, but not over, 0 - no selection, 2 - over full selected items
	int32 isUponSelected = 0, hasSelected = 0;
	if (!_selected.isEmpty()) {
		isUponSelected = -1;
		if (_selected.cbegin().value() == FullSelection) {
			hasSelected = 2;
			if (!ignoreMousedItem && App::mousedItem() && _selected.constFind(App::mousedItem()->history() == _migrated ? -App::mousedItem()->id : App::mousedItem()->id) != _selected.cend()) {
				isUponSelected = 2;
			} else {
				isUponSelected = -2;
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_contextMenuLnk = textlnkOver();
	PhotoLink *lnkPhoto = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument) {
		_menu = new PopupMenu();
		if (App::hoveredLinkItem()) {
			_menu->addAction(lang(lng_context_to_msg), this, SLOT(goToMessage()))->setEnabled(true);
		}
		if (lnkPhoto) {
			_menu->addAction(lang(lng_context_open_image), this, SLOT(openContextUrl()))->setEnabled(true);
		} else {
			if ((lnkVideo && lnkVideo->video()->loading()) || (lnkAudio && lnkAudio->audio()->loading()) || (lnkDocument && lnkDocument->document()->loading())) {
				_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
			} else {
				if ((lnkVideo && !lnkVideo->video()->already(true).isEmpty()) || (lnkAudio && !lnkAudio->audio()->already(true).isEmpty()) || (lnkDocument && !lnkDocument->document()->already(true).isEmpty())) {
					_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
				}
				_menu->addAction(lang(lnkVideo ? lng_context_open_video : (lnkAudio ? lng_context_open_audio : lng_context_open_file)), this, SLOT(openContextFile()))->setEnabled(true);
				_menu->addAction(lang(lnkVideo ? lng_context_save_video : (lnkAudio ? lng_context_save_audio : lng_context_save_file)), this, SLOT(saveContextFile()))->setEnabled(true);
			}
		}
		if (isUponSelected > 1) {
			_menu->addAction(lang(lng_context_forward_selected), _overview, SLOT(onForwardSelected()));
			if (selectedForDelete == selectedForForward) {
				_menu->addAction(lang(lng_context_delete_selected), _overview, SLOT(onDeleteSelected()));
			}
			_menu->addAction(lang(lng_context_clear_selection), _overview, SLOT(onClearSelected()));
		} else if (App::hoveredLinkItem()) {
			if (isUponSelected != -2) {
				if (App::hoveredLinkItem()->toHistoryMessage()) {
					_menu->addAction(lang(lng_context_forward_msg), this, SLOT(forwardMessage()))->setEnabled(true);
				}
				if (App::hoveredLinkItem()->canDelete()) {
					_menu->addAction(lang(lng_context_delete_msg), this, SLOT(deleteMessage()))->setEnabled(true);
				}
			}
			if (App::hoveredLinkItem()->id > 0) {
				_menu->addAction(lang(lng_context_select_msg), this, SLOT(selectMessage()))->setEnabled(true);
			}
		}
		App::contextItem(App::hoveredLinkItem());
		repaintItem(App::contextItem());
		if (_selectedMsgId) repaintItem(_selectedMsgId, -1);
	} else if (!ignoreMousedItem && App::mousedItem() && App::mousedItem()->channelId() == itemChannel(_mousedItem) && App::mousedItem()->id == itemMsgId(_mousedItem)) {
		_menu = new PopupMenu();
		if ((_contextMenuLnk && dynamic_cast<TextLink*>(_contextMenuLnk.data()))) {
			_menu->addAction(lang(lng_context_open_link), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_link), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if ((_contextMenuLnk && dynamic_cast<EmailLink*>(_contextMenuLnk.data()))) {
			_menu->addAction(lang(lng_context_open_email), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_email), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (_contextMenuLnk && dynamic_cast<MentionLink*>(_contextMenuLnk.data())) {
			_menu->addAction(lang(lng_context_open_mention), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_mention), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (_contextMenuLnk && dynamic_cast<HashtagLink*>(_contextMenuLnk.data())) {
			_menu->addAction(lang(lng_context_open_hashtag), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_hashtag), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else {
		}
		_menu->addAction(lang(lng_context_to_msg), this, SLOT(goToMessage()))->setEnabled(true);
		if (isUponSelected > 1) {
			_menu->addAction(lang(lng_context_forward_selected), _overview, SLOT(onForwardSelected()));
			if (selectedForDelete == selectedForForward) {
				_menu->addAction(lang(lng_context_delete_selected), _overview, SLOT(onDeleteSelected()));
			}
			_menu->addAction(lang(lng_context_clear_selection), _overview, SLOT(onClearSelected()));
		} else {
			if (isUponSelected != -2) {
				if (App::mousedItem()->toHistoryMessage()) {
					_menu->addAction(lang(lng_context_forward_msg), this, SLOT(forwardMessage()))->setEnabled(true);
				}
				if (App::mousedItem()->canDelete()) {
					_menu->addAction(lang(lng_context_delete_msg), this, SLOT(deleteMessage()))->setEnabled(true);
				}
			}
			if (App::mousedItem()->id > 0) {
				_menu->addAction(lang(lng_context_select_msg), this, SLOT(selectMessage()))->setEnabled(true);
			}
		}
		App::contextItem(App::mousedItem());
		repaintItem(App::contextItem());
		if (_selectedMsgId) repaintItem(_selectedMsgId, -1);
	}
	if (_menu) {
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

int32 OverviewInner::resizeToWidth(int32 nwidth, int32 scrollTop, int32 minHeight, bool force) {
	if (!force && _width == nwidth && minHeight == _minHeight) return scrollTop;

	if ((_type == OverviewPhotos || _type == OverviewVideos) && _resizeIndex < 0) {
		_resizeIndex = _photosInRow * ((scrollTop + minHeight) / int32(_rowWidth + st::overviewPhotoSkip)) + _photosInRow - 1;
		_resizeSkip = (scrollTop + minHeight) - ((scrollTop + minHeight) / int32(_rowWidth + st::overviewPhotoSkip)) * int32(_rowWidth + st::overviewPhotoSkip);
	}

	_width = nwidth;
	_minHeight = minHeight;
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		_photosInRow = int32(_width - st::overviewPhotoSkip) / int32(st::overviewPhotoMinSize + st::overviewPhotoSkip);
		_rowWidth = (int32(_width - st::overviewPhotoSkip) / _photosInRow) - st::overviewPhotoSkip;
	} else if (_type == OverviewLinks) {
		_rowWidth = qMin(_width - st::linksSearchMargin.left() - st::linksSearchMargin.right(), int32(st::linksMaxWidth));
	} else {
		_rowWidth = qMin(_width - st::profilePadding.left() - st::profilePadding.right(), int32(st::profileMaxWidth));
	}
	_rowsLeft = (_width - _rowWidth) / 2;

	_search.setGeometry(_rowsLeft, st::linksSearchMargin.top(), _rowWidth, _search.height());
	_cancelSearch.moveToLeft(_rowsLeft + _rowWidth - _cancelSearch.width(), _search.y());

	if (_type == OverviewPhotos || _type == OverviewVideos) {
		for (int32 i = 0, l = _items.size(); i < l; ++i) {
			_items.at(i)->resizeGetHeight(_rowWidth);
		}
		_height = countHeight();
	} else {
		bool resize = (_type == OverviewLinks);
		if (resize) _height = 0;
		for (int32 i = 0, l = _items.size(); i < l; ++i) {
			int32 h = _items.at(i)->resizeGetHeight(_rowWidth);
			if (resize) {
				_items.at(i)->getOverviewItemInfo()->setTop(_height + (_reversed ? h : 0));
				_height += h;
			}
		}
	}
	recountMargins();

	resize(_width, _marginTop + _height + _marginBottom);

	if (_type == OverviewPhotos || _type == OverviewVideos) {
        int32 newRow = _resizeIndex / _photosInRow;
        return newRow * int32(_rowWidth + st::overviewPhotoSkip) + _resizeSkip - minHeight;
    }
    return scrollTop;
}

void OverviewInner::dropResizeIndex() {
	_resizeIndex = -1;
}

PeerData *OverviewInner::peer() const {
	return _peer;
}

PeerData *OverviewInner::migratePeer() const {
	return _migrated ? _migrated->peer : 0;
}

MediaOverviewType OverviewInner::type() const {
	return _type;
}

void OverviewInner::switchType(MediaOverviewType type) {
	if (_type != type) {
		clear();
		_type = type;
		_reversed = (_type != OverviewLinks && _type != OverviewDocuments);
		if (_type == OverviewLinks || _type == OverviewDocuments) {
			_search.show();
		} else {
			_search.hide();
		}

		if (!_search.getLastText().isEmpty()) {
			_search.setText(QString());
			_search.updatePlaceholder();
			onSearchUpdate();
		}
		_cancelSearch.hide();

		resizeToWidth(_width, 0, _minHeight, true);
	}
	mediaOverviewUpdated();
	if (App::wnd()) App::wnd()->update();
}

void OverviewInner::setSelectMode(bool enabled) {
	_selMode = enabled;
}

void OverviewInner::openContextUrl() {
	if (_contextMenuLnk) {
		HistoryItem *was = App::hoveredLinkItem();
		App::hoveredLinkItem(App::contextItem());
		_contextMenuLnk->onClick(Qt::LeftButton);
		App::hoveredLinkItem(was);
	}
}

void OverviewInner::copyContextUrl() {
	QString enc = _contextMenuLnk ? _contextMenuLnk->encoded() : QString();
	if (!enc.isEmpty()) {
		QApplication::clipboard()->setText(enc);
	}
}

void OverviewInner::goToMessage() {
	HistoryItem *item = App::contextItem();
	if (!item) return;

	Ui::showPeerHistoryAtItem(item);
}

void OverviewInner::forwardMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg || item->serviceMsg()) return;

	App::main()->forwardLayer();
}

void OverviewInner::deleteMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg) return;

	HistoryMessage *msg = item->toHistoryMessage();
	App::main()->deleteLayer((msg && msg->uploading()) ? -2 : -1);
}

MsgId OverviewInner::complexMsgId(const HistoryItem *item) const {
	return item ? ((item->history() == _migrated) ? -item->id : item->id) : 0;
}

void OverviewInner::selectMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg || item->serviceMsg()) return;

	if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
		_selected.clear();
	} else if (_selected.size() == MaxSelectedItems && _selected.constFind(complexMsgId(item)) == _selected.cend()) {
		return;
	}
	_selected.insert(complexMsgId(item), FullSelection);
	_overview->updateTopBarSelection();
	_overview->update();
}

void OverviewInner::cancelContextDownload() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) {
		lnkVideo->video()->cancel();
	} else if (lnkAudio) {
		lnkAudio->audio()->cancel();
	} else if (lnkDocument) {
		lnkDocument->document()->cancel();
	}
}

void OverviewInner::showContextInFolder() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	QString already = lnkVideo ? lnkVideo->video()->already(true) : (lnkAudio ? lnkAudio->audio()->already(true) : (lnkDocument ? lnkDocument->document()->already(true) : QString()));
	if (!already.isEmpty()) psShowInFolder(already);
}

void OverviewInner::saveContextFile() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) VideoSaveLink::doSave(lnkVideo->video(), true);
	if (lnkAudio) AudioSaveLink::doSave(lnkAudio->audio(), true);
	if (lnkDocument) DocumentSaveLink::doSave(lnkDocument->document(), true);
}

void OverviewInner::openContextFile() {
	HistoryItem *was = App::hoveredLinkItem();
	App::hoveredLinkItem(App::contextItem());
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) VideoOpenLink(lnkVideo->video()).onClick(Qt::LeftButton);
	if (lnkAudio) AudioOpenLink(lnkAudio->audio()).onClick(Qt::LeftButton);
	if (lnkDocument) DocumentOpenLink(lnkDocument->document()).onClick(Qt::LeftButton);
	App::hoveredLinkItem(was);
}

bool OverviewInner::onSearchMessages(bool searchCache) {
	_searchTimer.stop();
	QString q = _search.text().trimmed();
	if (q.isEmpty()) {
		if (_searchRequest) {
			_searchRequest = 0;
		}
		return true;
	}
	if (searchCache) {
		SearchCache::const_iterator i = _searchCache.constFind(q);
		if (i != _searchCache.cend()) {
			_searchQuery = q;
			_searchFull = _searchFullMigrated = false;
			_searchRequest = 0;
			searchReceived(SearchFromStart, i.value(), 0);
			return true;
		}
	} else if (_searchQuery != q) {
		_searchQuery = q;
		_searchFull = _searchFullMigrated = false;
		int32 flags = (_history->peer->isChannel() && !_history->peer->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
		MTPmessagesFilter filter = (_type == OverviewLinks) ? MTP_inputMessagesFilterUrl() : MTP_inputMessagesFilterDocument();
		_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _history->peer->input, MTP_string(_searchQuery), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&OverviewInner::searchReceived, SearchFromStart), rpcFail(&OverviewInner::searchFailed, SearchFromStart));
		_searchQueries.insert(_searchRequest, _searchQuery);
	}
	return false;
}

void OverviewInner::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
		if (_inSearch && _searchFull && (!_migrated || _searchFullMigrated) && _searchResults.isEmpty()) {
			update();
		}
	}
}

void OverviewInner::onSearchUpdate() {
	QString filterText = (_type == OverviewLinks || _type == OverviewDocuments) ? _search.text().trimmed() : QString();
	bool inSearch = !filterText.isEmpty(), changed = (inSearch != _inSearch);
	_inSearch = inSearch;

	onNeedSearchMessages();

	if (!_inSearch) {
		_searchCache.clear();
		_searchQueries.clear();
		_searchQuery = QString();
		_searchResults.clear();
		_cancelSearch.hide();
	} else if (_cancelSearch.isHidden()) {
		_cancelSearch.show();
	}

	if (changed) {
		_itemsToBeLoaded = LinksOverviewPerPage * 2;
		mediaOverviewUpdated();
	}
	_overview->scrollReset();
}

void OverviewInner::onCancel() {
	if (_selected.isEmpty()) {
		if (onCancelSearch()) return;
		App::main()->showBackFromStack();
	} else {
		_overview->onClearSelected();
	}
}

bool OverviewInner::onCancelSearch() {
	if (_search.isHidden()) return false;
	bool clearing = !_search.text().isEmpty();
	_cancelSearch.hide();
	_search.clear();
	_search.updatePlaceholder();
	onSearchUpdate();
	return clearing;
}

void OverviewInner::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
		dragActionUpdate(QCursor::pos());
		repaintItem(App::contextItem());
		if (_selectedMsgId) repaintItem(_selectedMsgId, -1);
	}
}

void OverviewInner::getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const {
	selectedForForward = selectedForDelete = 0;
	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		if (i.value() == FullSelection) {
			if (HistoryItem *item = App::histItemById(itemChannel(i.key()), itemMsgId(i.key()))) {
				if (item->canDelete()) {
					++selectedForDelete;
				}
			}
			++selectedForForward;
		}
	}
	if (!selectedForDelete && !selectedForForward && !_selected.isEmpty()) { // text selection
		selectedForForward = -1;
	}
}

void OverviewInner::clearSelectedItems(bool onlyTextSelection) {
	if (!_selected.isEmpty() && (!onlyTextSelection || _selected.cbegin().value() != FullSelection)) {
		_selected.clear();
		_overview->updateTopBarSelection();
		_overview->update();
	}
}

void OverviewInner::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	if (_selected.isEmpty() || _selected.cbegin().value() != FullSelection) return;

	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		HistoryItem *item = App::histItemById(itemChannel(i.key()), itemMsgId(i.key()));
		if (item && item->toHistoryMessage() && item->id > 0) {
			if (item->history() == _migrated) {
				sel.insert(item->id - ServerMaxMsgId, item);
			} else {
				sel.insert(item->id, item);
			}
		}
	}
}

void OverviewInner::onTouchSelect() {
	_touchSelect = true;
	dragActionStart(_touchPos);
}

void OverviewInner::onTouchScrollTimer() {
	uint64 nowTime = getms();
	if (_touchScrollState == TouchScrollAcceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = TouchScrollManual;
		touchResetSpeed();
	} else if (_touchScrollState == TouchScrollAuto || _touchScrollState == TouchScrollAcceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = _overview->touchScroll(delta);

		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = TouchScrollManual;
			_touchScroll = false;
			_touchScrollTimer.stop();
		} else {
			_touchTime = nowTime;
		}
		touchDeaccelerate(elapsed);
	}
}

void OverviewInner::mediaOverviewUpdated() {
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		History::MediaOverview &o(_history->overview[_type]), *migratedOverview = _migrated ? &_migrated->overview[_type] : 0;
		int32 migrateCount = migratedIndexSkip();
		int32 wasCount = _items.size(), fullCount = (migrateCount + o.size());
		int32 tocheck = qMin(fullCount, _itemsToBeLoaded);
		_items.reserve(tocheck);

		int32 index = 0;
		bool allGood = true;
		for (int32 i = fullCount, l = fullCount - tocheck; i > l;) {
			--i;
			MsgId msgid = ((i < migrateCount) ? -migratedOverview->at(i) : o.at(i - migrateCount));
			if (allGood) {
				if (_items.size() > index && complexMsgId(_items.at(index)->getItem()) == msgid) {
					++index;
					continue;
				}
				allGood = false;
			}
			HistoryItem *item = App::histItemById(itemChannel(msgid), itemMsgId(msgid));
			LayoutMediaItem *layout = layoutPrepare(item);
			if (!layout) continue;

			setLayoutItem(index, layout, 0);
			++index;
		}
		if (_items.size() > index) _items.resize(index);

		_height = countHeight();
	} else {
		bool dateEveryMonth = (_type == OverviewDocuments), dateEveryDay = (_type == OverviewLinks);
		bool withDates = (dateEveryMonth || dateEveryDay);

		History::MediaOverview &o(_history->overview[_type]), *migratedOverview = _migrated ? &_migrated->overview[_type] : 0;
		int32 migrateCount = migratedIndexSkip();
		int32 l = _inSearch ? _searchResults.size() : (migrateCount + o.size()), tocheck = qMin(l, _itemsToBeLoaded);
		_items.reserve(withDates * tocheck); // day items

		int32 top = 0, index = 0;
		bool allGood = true;
		QDate prevDate;
		for (int32 i = 0; i < tocheck; ++i) {
			MsgId msgid = _inSearch ? _searchResults.at(l - i - 1) : ((l - i - 1 < migrateCount) ? -migratedOverview->at(l - i - 1) : o.at(l - i - 1 - migrateCount));
			if (allGood) {
				if (_items.size() > index && complexMsgId(_items.at(index)->getItem()) == msgid) {
					if (withDates) prevDate = _items.at(index)->getItem()->date.date();
					top = _items.at(index)->getOverviewItemInfo()->top();
					if (!_reversed) {
						top += _items.at(index)->height();
					}
					++index;
					continue;
				}
				if (_items.size() > index + 1 && !_items.at(index)->toLayoutMediaItem() && complexMsgId(_items.at(index + 1)->getItem()) == msgid) { // day item
					++index;
					if (withDates) prevDate = _items.at(index)->getItem()->date.date();
					top = _items.at(index)->getOverviewItemInfo()->top();
					if (!_reversed) {
						top += _items.at(index)->height();
					}
					++index;
					continue;
				}
				allGood = false;
			}
			HistoryItem *item = App::histItemById(itemChannel(msgid), itemMsgId(msgid));
			LayoutMediaItem *layout = layoutPrepare(item);
			if (!layout) continue;

			if (withDates) {
				QDate date = item->date.date();
				if (!index || (index > 0 && (dateEveryMonth ? (date.month() != prevDate.month() || date.year() != prevDate.year()) : (date != prevDate)))) {
					top += setLayoutItem(index, layoutPrepare(date, dateEveryMonth), top);
					++index;
					prevDate = date;
				}
			}
			top += setLayoutItem(index, layout, top);
			++index;
		}
		if (_items.size() > index) _items.resize(index);

		_height = top;
	}

	fixItemIndex(_dragSelFromIndex, _dragSelFrom);
	fixItemIndex(_dragSelToIndex, _dragSelTo);
	fixItemIndex(_mousedItemIndex, _mousedItem);
	fixItemIndex(_dragItemIndex, _dragItem);

	recountMargins();
	int32 newHeight = _marginTop + _height + _marginBottom, deltaHeight = newHeight - height();
	if (deltaHeight) {
		resize(_width, newHeight);
		if (_type != OverviewLinks && _type != OverviewDocuments) {
			_overview->scrollBy(deltaHeight);
		}
	} else {
		onUpdateSelected();
		update();
	}
}

void OverviewInner::changingMsgId(HistoryItem *row, MsgId newId) {
	MsgId oldId = complexMsgId(row);
	if (row->history() == _migrated) newId = -newId;

	if (_dragSelFrom == oldId) _dragSelFrom = newId;
	if (_dragSelTo == oldId) _dragSelTo = newId;
	if (_mousedItem == oldId) _mousedItem = newId;
	if (_dragItem == oldId) _dragItem = newId;
	if (_selectedMsgId == oldId) _selectedMsgId = newId;
	for (SelectedItems::iterator i = _selected.begin(), e = _selected.end(); i != e; ++i) {
		if (i.key() == oldId) {
			uint32 sel = i.value();
			_selected.erase(i);
			_selected.insert(newId, sel);
			break;
		}
	}
}

void OverviewInner::itemRemoved(HistoryItem *item) {
	MsgId msgId = (item->history() == _migrated) ? -item->id : item->id;
	if (_dragItem == msgId) {
		dragActionCancel();
	}
	if (_selectedMsgId == msgId) {
		_selectedMsgId = 0;
	}

	SelectedItems::iterator i = _selected.find(msgId);
	if (i != _selected.cend()) {
		_selected.erase(i);
		_overview->updateTopBarSelection();
	}

	onUpdateSelected();

	if (_dragSelFrom == msgId) {
		_dragSelFrom = 0;
		_dragSelFromIndex = -1;
	}
	if (_dragSelTo == msgId) {
		_dragSelTo = 0;
		_dragSelToIndex = -1;
	}
	updateDragSelection(_dragSelFrom, _dragSelFromIndex, _dragSelTo, _dragSelToIndex, _dragSelecting);

	update();
}

void OverviewInner::repaintItem(const HistoryItem *msg) {
	if (!msg) return;

	History *history = (msg->history() == _history) ? _history : (msg->history() == _migrated ? _migrated : 0);
	if (!history) return;

	int32 migrateindex = migratedIndexSkip();
	MsgId msgid = msg->id;
	if (history->overviewHasMsgId(_type, msgid) && (history == _history || migrateindex > 0)) {
		if (_type == OverviewPhotos || _type == OverviewVideos) {
			if (history == _migrated) msgid = -msgid;
			for (int32 i = 0, l = _items.size(); i != l; ++i) {
				if (complexMsgId(_items.at(i)->getItem()) == msgid) {
					int32 shownAtIndex = _items.size() - i - 1;
					float64 w = (float64(width() - st::overviewPhotoSkip) / _photosInRow);
					int32 vsize = (_rowWidth + st::overviewPhotoSkip);
					int32 row = (_photosToAdd + shownAtIndex) / _photosInRow, col = (_photosToAdd + shownAtIndex) % _photosInRow;
					update(int32(col * w), _marginTop + int32(row * vsize), qCeil(w), vsize);
					break;
				}
			}
		} else {
			if (history == _migrated) msgid = -msgid;
			for (int32 i = 0, l = _items.size(); i != l; ++i) {
				if (complexMsgId(_items.at(i)->getItem()) == msgid) {
					int32 top = _items.at(i)->getOverviewItemInfo()->top();
					if (_reversed) top = _height - top;
					update(_rowsLeft, _marginTop + top, _rowWidth, _items.at(i)->height());
					break;
				}
			}
		}
	}
}

int32 OverviewInner::countHeight() {
	int32 result = _height;
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		int32 count = _items.size();
		int32 migratedFullCount = _migrated ? _migrated->overviewCount(_type) : 0;
		int32 fullCount = migratedFullCount + _history->overviewCount(_type);
		if (fullCount > 0 && migratedFullCount >= 0) {
			int32 cnt = count - (fullCount % _photosInRow);
			if (cnt < 0) cnt += _photosInRow;
			_photosToAdd = (_photosInRow - (cnt % _photosInRow)) % _photosInRow;
		} else {
			_photosToAdd = 0;
		}
		int32 rows = ((_photosToAdd + count) / _photosInRow) + (((_photosToAdd + count) % _photosInRow) ? 1 : 0);
		result = (_rowWidth + st::overviewPhotoSkip) * rows + st::overviewPhotoSkip;
	}
	return result;
}

void OverviewInner::recountMargins() {
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		_marginBottom = 0;
		_marginTop = qMax(_minHeight - _height - _marginBottom, 0);
	} else if (_type == OverviewAudioDocuments) {
		_marginTop = st::playlistPadding;
		_marginBottom = qMax(_minHeight - _height - _marginTop, int32(st::playlistPadding));
	} else if (_type == OverviewLinks || _type == OverviewDocuments) {
		_marginTop = st::linksSearchMargin.top() + _search.height() + st::linksSearchMargin.bottom();
		_marginBottom = qMax(_minHeight - _height - _marginTop, int32(st::playlistPadding));
	} else {
		_marginBottom = st::playlistPadding;
		_marginTop = qMax(_minHeight - _height - _marginBottom, int32(st::playlistPadding));
	}
}

LayoutMediaItem *OverviewInner::layoutPrepare(HistoryItem *item) {
	if (!item) return 0;

	LayoutItems::const_iterator i = _layoutItems.cend();
	HistoryMedia *media = item->getMedia();
	if (_type == OverviewPhotos) {
		if (media && media->type() == MediaTypePhoto) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new LayoutOverviewPhoto(static_cast<HistoryPhoto*>(media)->photo(), item));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewVideos) {
		if (media && media->type() == MediaTypeVideo) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new LayoutOverviewVideo(static_cast<HistoryVideo*>(media)->video(), item));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewAudios) {
		if (media && media->type() == MediaTypeAudio) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new LayoutOverviewAudio(static_cast<HistoryAudio*>(media)->audio(), item));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewDocuments || _type == OverviewAudioDocuments) {
		if (media && (media->type() == MediaTypeDocument || media->type() == MediaTypeGif)) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new LayoutOverviewDocument(media->getDocument(), item));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewLinks) {
		if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
			i = _layoutItems.insert(item, new LayoutOverviewLink(media, item));
			i.value()->initDimensions();
		}
	}
	return (i == _layoutItems.cend()) ? 0 : i.value();
}

LayoutItem *OverviewInner::layoutPrepare(const QDate &date, bool month) {
	int32 key = date.year() * 100 + date.month();
	if (!month) key = key * 100 + date.day();
	LayoutDates::const_iterator i = _layoutDates.constFind(key);
	if (i == _layoutDates.cend()) {
		i = _layoutDates.insert(key, new LayoutOverviewDate(date, month));
		i.value()->initDimensions();
	}
	return i.value();
}

int32 OverviewInner::setLayoutItem(int32 index, LayoutItem *item, int32 top) {
	if (_items.size() > index) {
		_items[index] = item;
	} else {
		_items.push_back(item);
	}
	int32 h = item->resizeGetHeight(_rowWidth);
	if (OverviewItemInfo *info = item->getOverviewItemInfo()) {
		info->setTop(top + (_reversed ? h : 0));
	}
	return h;
}

OverviewInner::~OverviewInner() {
	clear();
}

OverviewWidget::OverviewWidget(QWidget *parent, PeerData *peer, MediaOverviewType type) : TWidget(parent)
, _scroll(this, st::historyScroll, false)
, _inner(this, &_scroll, peer, type)
, _noDropResizeIndex(false)
, _a_show(animation(this, &OverviewWidget::step_show))
, _scrollSetAfterShow(0)
, _scrollDelta(0)
, _selCount(0)
, _sideShadow(this, st::shadowColor)
, _topShadow(this, st::shadowColor)
, _inGrab(false) {
	_scroll.setFocusPolicy(Qt::NoFocus);
	_scroll.setWidget(&_inner);
	_scroll.move(0, 0);
	_inner.move(0, 0);

	_sideShadow.setVisible(cWideMode());

	updateScrollColors();

	_scroll.show();
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(onUpdateSelected()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	_scrollTimer.setSingleShot(false);

	connect(App::main()->player(), SIGNAL(playerSongChanged(const FullMsgId&)), this, SLOT(onPlayerSongChanged(const FullMsgId&)));

	switchType(type);
}

void OverviewWidget::clear() {
	_inner.clear();
}

void OverviewWidget::onScroll() {
	MTP::clearLoaderPriorities();
	int32 preloadThreshold = _scroll.height() * 5;
	bool needToPreload = false;
	do {
		needToPreload = (type() == OverviewLinks || type() == OverviewDocuments) ? (_scroll.scrollTop() + preloadThreshold > _scroll.scrollTopMax()) : (_scroll.scrollTop() < preloadThreshold);
		if (!needToPreload || !_inner.preloadLocal()) {
			break;
		}
	} while (true);
	if (needToPreload) {
		_inner.preloadMore();
	}
	if (!_noDropResizeIndex) {
		_inner.dropResizeIndex();
	}
}

void OverviewWidget::resizeEvent(QResizeEvent *e) {
	_noDropResizeIndex = true;
	int32 st = _scroll.scrollTop();
	_scroll.resize(size());
	int32 newScrollTop = _inner.resizeToWidth(width(), st, height());
	if (int32 addToY = App::main() ? App::main()->contentScrollAddToY() : 0) {
		newScrollTop += addToY;
	}
	if (newScrollTop != _scroll.scrollTop()) {
		_scroll.scrollToY(newScrollTop);
	}
	_noDropResizeIndex = false;

	_topShadow.resize(width() - ((cWideMode() && !_inGrab) ? st::lineWidth : 0), st::lineWidth);
	_topShadow.moveToLeft((cWideMode() && !_inGrab) ? st::lineWidth : 0, 0);
	_sideShadow.resize(st::lineWidth, height());
	_sideShadow.moveToLeft(0, 0);
}

void OverviewWidget::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);
	if (_a_show.animating()) {
		if (a_coordOver.current() > 0) {
			p.drawPixmap(QRect(0, 0, a_coordOver.current(), height()), _cacheUnder, QRect(-a_coordUnder.current() * cRetinaFactor(), 0, a_coordOver.current() * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(a_shadow.current() * st::slideFadeOut);
			p.fillRect(0, 0, a_coordOver.current(), height(), st::black->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow);
		return;
	}

	p.fillRect(e->rect(), st::white);
}

void OverviewWidget::contextMenuEvent(QContextMenuEvent *e) {
	return _inner.showContextMenu(e);
}

void OverviewWidget::scrollBy(int32 add) {
	if (_scroll.isHidden()) {
		_scrollSetAfterShow += add;
	} else {
		_scroll.scrollToY(_scroll.scrollTop() + add);
	}
}

void OverviewWidget::scrollReset() {
	_scroll.scrollToY((type() == OverviewLinks || type() == OverviewDocuments) ? 0 : _scroll.scrollTopMax());
}

void OverviewWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (_a_show.animating()) {
		p.drawPixmap(a_coordUnder.current(), 0, _cacheTopBarUnder);
		p.drawPixmap(a_coordOver.current(), 0, _cacheTopBarOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), st::topBarHeight), App::sprite(), st::slideShadow);
		return;
	}
	p.setOpacity(st::topBarBackAlpha + (1 - st::topBarBackAlpha) * over);
	p.drawPixmap(QPoint(st::topBarBackPadding.left(), (st::topBarHeight - st::topBarBackImg.pxHeight()) / 2), App::sprite(), st::topBarBackImg);
	p.setFont(st::topBarBackFont->f);
	p.setPen(st::topBarBackColor->p);
	p.drawText(st::topBarBackPadding.left() + st::topBarBackImg.pxWidth() + st::topBarBackPadding.right(), (st::topBarHeight - st::topBarBackFont->height) / 2 + st::topBarBackFont->ascent, _header);
}

void OverviewWidget::topBarClick() {
	App::main()->showBackFromStack();
}

PeerData *OverviewWidget::peer() const {
	return _inner.peer();
}

PeerData *OverviewWidget::migratePeer() const {
	return _inner.migratePeer();
}

MediaOverviewType OverviewWidget::type() const {
	return _inner.type();
}

void OverviewWidget::switchType(MediaOverviewType type) {
	_selCount = 0;

	disconnect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(onUpdateSelected()));
	disconnect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	_inner.setSelectMode(false);
	_inner.switchType(type);
	switch (type) {
	case OverviewPhotos: _header = lang(lng_profile_photos_header); break;
	case OverviewVideos: _header = lang(lng_profile_videos_header); break;
	case OverviewAudioDocuments: _header = lang(lng_profile_songs_header); break;
	case OverviewDocuments: _header = lang(lng_profile_files_header); break;
	case OverviewAudios: _header = lang(lng_profile_audios_header); break;
	case OverviewLinks: _header = lang(lng_profile_shared_links_header); break;
	}
	noSelectingScroll();
	App::main()->topBar()->showSelected(0);
	updateTopBarSelection();
	scrollReset();

	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(onUpdateSelected()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	onScroll();
	activate();
}

void OverviewWidget::updateTopBarSelection() {
	int32 selectedForForward, selectedForDelete;
	_inner.getSelectionState(selectedForForward, selectedForDelete);
	_selCount = selectedForForward ? selectedForForward : selectedForDelete;
	_inner.setSelectMode(_selCount > 0);
	if (App::main()) {
		App::main()->topBar()->showSelected(_selCount > 0 ? _selCount : 0, (selectedForDelete == selectedForForward));
		App::main()->topBar()->update();
	}
	if (App::wnd() && !Ui::isLayerShown()) {
		_inner.activate();
	}
	update();
}

int32 OverviewWidget::lastWidth() const {
	return width();
}

int32 OverviewWidget::lastScrollTop() const {
	return _scroll.scrollTop();
}

int32 OverviewWidget::countBestScroll() const {
	if (type() == OverviewAudioDocuments && audioPlayer()) {
		SongMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		audioPlayer()->currentState(&playing, &playingState);
		if (playing) {
			int32 top = _inner.itemTop(playing.msgId);
			if (top >= 0) {
				return snap(top - int(_scroll.height() - (st::msgPadding.top() + st::mediaThumbSize + st::msgPadding.bottom())) / 2, 0, _scroll.scrollTopMax());
			}
		}
	} else if (type() == OverviewLinks || type() == OverviewDocuments) {
		return 0;
	}
	return _scroll.scrollTopMax();
}

void OverviewWidget::fastShow(bool back, int32 lastScrollTop) {
	resizeEvent(0);
	_scrollSetAfterShow = (lastScrollTop < 0 ? countBestScroll() : lastScrollTop);
	show();
	_inner.activate();
	doneShow();

	if (App::app()) App::app()->mtpUnpause();
}

void OverviewWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back, int32 lastScrollTop) {
	if (App::app()) App::app()->mtpPause();

	(back ? _cacheOver : _cacheUnder) = bgAnimCache;
	(back ? _cacheTopBarOver : _cacheTopBarUnder) = bgAnimTopBarCache;
	resizeEvent(0);
	_scroll.scrollToY(lastScrollTop < 0 ? countBestScroll() : lastScrollTop);
	(back ? _cacheUnder : _cacheOver) = myGrab(this);
	App::main()->topBar()->stopAnim();
	(back ? _cacheTopBarUnder : _cacheTopBarOver) = myGrab(App::main()->topBar());
	App::main()->topBar()->startAnim();

	_scrollSetAfterShow = _scroll.scrollTop();
	_scroll.hide();
	_topShadow.hide();

	a_coordUnder = back ? anim::ivalue(-qFloor(st::slideShift * width()), 0) : anim::ivalue(0, -qFloor(st::slideShift * width()));
	a_coordOver = back ? anim::ivalue(0, width()) : anim::ivalue(width(), 0);
	a_shadow = back ? anim::fvalue(1, 0) : anim::fvalue(0, 1);
	_a_show.start();

	show();

	App::main()->topBar()->update();
	_inner.activate();
}

void OverviewWidget::step_show(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		_a_show.stop();
		_sideShadow.setVisible(cWideMode());
		_topShadow.show();

		a_coordUnder.finish();
		a_coordOver.finish();
		a_shadow.finish();
		_cacheUnder = _cacheOver = _cacheTopBarUnder = _cacheTopBarOver = QPixmap();
		App::main()->topBar()->stopAnim();

		doneShow();

		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordUnder.update(dt, st::slideFunction);
		a_coordOver.update(dt, st::slideFunction);
		a_shadow.update(dt, st::slideFunction);
	}
	if (timer) {
		update();
		App::main()->topBar()->update();
	}
}

void OverviewWidget::updateWideMode() {
	_sideShadow.setVisible(cWideMode());
}

void OverviewWidget::doneShow() {
	_scroll.show();
	_scroll.scrollToY(_scrollSetAfterShow);
	activate();
	onScroll();
}

void OverviewWidget::mediaOverviewUpdated(PeerData *p, MediaOverviewType t) {
	if ((peer() == p || migratePeer() == p) && t == type()) {
		_inner.mediaOverviewUpdated();
		onScroll();
		updateTopBarSelection();
	}
}

void OverviewWidget::changingMsgId(HistoryItem *row, MsgId newId) {
	if (peer() == row->history()->peer || migratePeer() == row->history()->peer) {
		_inner.changingMsgId(row, newId);
	}
}

void OverviewWidget::ui_repaintHistoryItem(const HistoryItem *item) {
	if (peer() == item->history()->peer || migratePeer() == item->history()->peer) {
		_inner.repaintItem(item);
	}
}

void OverviewWidget::notify_historyItemLayoutChanged(const HistoryItem *item) {
	if (peer() == item->history()->peer || migratePeer() == item->history()->peer) {
		_inner.onUpdateSelected();
	}
}

void OverviewWidget::itemRemoved(HistoryItem *row) {
	_inner.itemRemoved(row);
}

void OverviewWidget::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	_inner.fillSelectedItems(sel, forDelete);
}

void OverviewWidget::updateScrollColors() {
	if (!App::historyScrollBarColor()) return;
	_scroll.updateColors(App::historyScrollBarColor(), App::historyScrollBgColor(), App::historyScrollBarOverColor(), App::historyScrollBgOverColor());
}

void OverviewWidget::updateAfterDrag() {
	_inner.dragActionUpdate(QCursor::pos());
}

OverviewWidget::~OverviewWidget() {
	onClearSelected();
	updateTopBarSelection();
}

void OverviewWidget::activate() {
	if (_scroll.isHidden()) {
		setFocus();
	} else {
		_inner.activate();
	}
}

QPoint OverviewWidget::clampMousePosition(QPoint point) {
	if (point.x() < 0) {
		point.setX(0);
	} else if (point.x() >= _scroll.width()) {
		point.setX(_scroll.width() - 1);
	}
	if (point.y() < _scroll.scrollTop()) {
		point.setY(_scroll.scrollTop());
	} else if (point.y() >= _scroll.scrollTop() + _scroll.height()) {
		point.setY(_scroll.scrollTop() + _scroll.height() - 1);
	}
	return point;
}

void OverviewWidget::onScrollTimer() {
	int32 d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll.scrollToY(_scroll.scrollTop() + d);
}

void OverviewWidget::onPlayerSongChanged(const FullMsgId &msgId) {
	if (type() == OverviewAudioDocuments) {
//		int32 top = _inner.itemTop(msgId);
//		if (top > 0) {
//			_scroll.scrollToY(snap(top - int(_scroll.height() - (st::msgPadding.top() + st::mediaThumbSize + st::msgPadding.bottom())) / 2, 0, _scroll.scrollTopMax()));
//		}
	}
}

void OverviewWidget::checkSelectingScroll(QPoint point) {
	if (point.y() < _scroll.scrollTop()) {
		_scrollDelta = point.y() - _scroll.scrollTop();
	} else if (point.y() >= _scroll.scrollTop() + _scroll.height()) {
		_scrollDelta = point.y() - _scroll.scrollTop() - _scroll.height() + 1;
	} else {
		_scrollDelta = 0;
	}
	if (_scrollDelta) {
		_scrollTimer.start(15);
	} else {
		_scrollTimer.stop();
	}
}

void OverviewWidget::noSelectingScroll() {
	_scrollTimer.stop();
}

bool OverviewWidget::touchScroll(const QPoint &delta) {
	int32 scTop = _scroll.scrollTop(), scMax = _scroll.scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	_scroll.scrollToY(scNew);
	return true;
}

void OverviewWidget::onForwardSelected() {
	App::main()->forwardLayer(true);
}

void OverviewWidget::onDeleteSelected() {
	SelectedItemSet sel;
	_inner.fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	App::main()->deleteLayer(sel.size());
}

void OverviewWidget::onDeleteSelectedSure() {
	SelectedItemSet sel;
	_inner.fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	QMap<PeerData*, QVector<MTPint> > ids;
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		if (i.value()->id > 0) {
			ids[i.value()->history()->peer].push_back(MTP_int(i.value()->id));
		}
	}

	onClearSelected();
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		i.value()->destroy();
	}
	Notify::historyItemsResized();
	Ui::hideLayer();

	for (QMap<PeerData*, QVector<MTPint> >::const_iterator i = ids.cbegin(), e = ids.cend(); i != e; ++i) {
		App::main()->deleteMessages(i.key(), i.value());
	}
}

void OverviewWidget::onDeleteContextSure() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg) {
		return;
	}

	QVector<MTPint> toDelete(1, MTP_int(item->id));
	History *h = item->history();
	bool wasOnServer = (item->id > 0), wasLast = (h->lastMsg == item);
	item->destroy();
	if (!wasOnServer && wasLast && !h->lastMsg) {
		App::main()->checkPeerHistory(h->peer);
	}

	Notify::historyItemsResized();
	Ui::hideLayer();

	if (wasOnServer) {
		App::main()->deleteMessages(h->peer, toDelete);
	}
}

void OverviewWidget::onClearSelected() {
	_inner.clearSelectedItems();
}
