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

OverviewInner::CachedLink::CachedLink(HistoryItem *item) : titleWidth(0), page(0), pixw(0), pixh(0), text(st::msgMinWidth) {
	QString msgText;
	LinksInText msgLinks;
	item->getTextWithLinks(msgText, msgLinks);
	int32 from = 0, till = msgText.size(), lnk = msgLinks.size();
	for (int32 i = 0; i < lnk; ++i) {
		if (msgLinks[i].type != LinkInTextUrl && msgLinks[i].type != LinkInTextCustomUrl && msgLinks[i].type != LinkInTextEmail) {
			continue;
		}
		QString url = msgLinks[i].text, text = msgText.mid(msgLinks[i].offset, msgLinks[i].length);
		urls.push_back(Link(url.isEmpty() ? text : url, text));
	}
	while (lnk > 0 && till > from) {
		--lnk;
		if (msgLinks[lnk].type != LinkInTextUrl && msgLinks[lnk].type != LinkInTextCustomUrl && msgLinks[lnk].type != LinkInTextEmail) {
			++lnk;
			break;
		}
		int32 afterLinkStart = msgLinks[lnk].offset + msgLinks[lnk].length;
		if (till > afterLinkStart) {
			if (!QRegularExpression(qsl("^[,.\\s_=+\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(msgText.mid(afterLinkStart, till - afterLinkStart)).hasMatch()) {
				++lnk;
				break;
			}
		}
		till = msgLinks[lnk].offset;
	}
	if (!lnk) {
		if (QRegularExpression(qsl("^[,.\\s\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(msgText.mid(from, till - from)).hasMatch()) {
			till = from;
		}
	}

	HistoryMedia *media = item->getMedia();
	page = (media && media->type() == MediaTypeWebPage) ? static_cast<HistoryWebPage*>(media)->webpage() : 0;
	if (from >= till && page) {
		msgText = page->description;
		from = 0;
		till = msgText.size();
	}
	if (till > from) {
		TextParseOptions opts = { TextParseMultiline, int32(st::linksMaxWidth), 3 * st::msgFont->height, Qt::LayoutDirectionAuto };
		text.setText(st::msgFont, msgText.mid(from, till - from), opts);
	}
	int32 tw = 0, th = 0;
	if (page && page->photo) {
		if (!page->photo->full->loaded()) page->photo->medium->load(false, false);

		tw = convertScale(page->photo->medium->width());
		th = convertScale(page->photo->medium->height());
	} else if (page && page->doc) {
		if (!page->doc->thumb->loaded()) page->doc->thumb->load(false, false);

		tw = convertScale(page->doc->thumb->width());
		th = convertScale(page->doc->thumb->height());
	}
	if (tw > st::dlgPhotoSize) {
		if (th > tw) {
			th = th * st::dlgPhotoSize / tw;
			tw = st::dlgPhotoSize;
		} else if (th > st::dlgPhotoSize) {
			tw = tw * st::dlgPhotoSize / th;
			th = st::dlgPhotoSize;
		}
	}
	pixw = tw;
	pixh = th;
	if (pixw < 1) pixw = 1;
	if (pixh < 1) pixh = 1;

	if (page) {
		title = page->title;
	}
	QVector<QStringRef> parts = (page ? page->url : (urls.isEmpty() ? QString() : urls.at(0).url)).splitRef('/');
	if (!parts.isEmpty()) {
		QStringRef domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').back().split('.');
		if (parts.size() > 1) {
			letter = parts.at(parts.size() - 2).at(0).toUpper();
			if (title.isEmpty()) {
				title.reserve(parts.at(parts.size() - 2).size());
				title.append(letter).append(parts.at(parts.size() - 2).mid(1));
			}
		}
	}
	titleWidth = st::webPageTitleFont->width(title);
}

int32 OverviewInner::CachedLink::countHeight(int32 w) {
	int32 result = 0;
	if (!title.isEmpty()) {
		result += st::webPageTitleFont->height;
	}
	if (!text.isEmpty()) {
		result += qMin(3 * st::msgFont->height, text.countHeight(w - st::dlgPhotoSize - st::dlgPhotoPadding));
	}
	result += urls.size() * st::msgFont->height;
	return qMax(result, int(st::dlgPhotoSize)) + st::linksMargin * 2 + st::linksBorder;
}

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

OverviewInner::OverviewInner(OverviewWidget *overview, ScrollArea *scroll, const PeerData *peer, MediaOverviewType type) : QWidget(0)
, _overview(overview)
, _scroll(scroll)
, _resizeIndex(-1)
, _resizeSkip(0)
, _peer(App::peer(peer->id))
, _type(type)
, _hist(App::history(peer->id))
, _channel(peerToChannel(peer->id))
, _photosInRow(1)
, _photosToAdd(0)
, _selMode(false)
, _audioLeft(st::msgMargin.left())
, _audioWidth(st::msgMinWidth)
, _audioHeight(st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom())
, _linksLeft(st::linksSearchMargin.left())
, _linksWidth(st::msgMinWidth)
, _search(this, st::dlgFilter, lang(lng_dlg_filter))
, _cancelSearch(this, st::btnCancelSearch)
, _itemsToBeLoaded(LinksOverviewPerPage * 2)
, _inSearch(false)
, _searchFull(false)
, _searchRequest(0)
, _lastSearchId(0)
, _searchedCount(0)
, _width(st::wndMinWidth)
, _height(0)
, _minHeight(0)
, _addToY(0)
, _cursor(style::cur_default)
, _cursorState(HistoryDefaultCursorState)
, _dragAction(NoDrag)
, _dragItem(0), _selectedMsgId(0)
, _dragItemIndex(-1)
, _mousedItem(0)
, _mousedItemIndex(-1)
, _lnkOverIndex(0)
, _lnkDownIndex(0)
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

	resize(_width, height());

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
	if (_type == OverviewLinks) {
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

void OverviewInner::fixItemIndex(int32 &current, MsgId msgId) const {
	if (!msgId) {
		current = -1;
	} else if (_type == OverviewPhotos || _type == OverviewAudioDocuments) {
		int32 l = _hist->overview[_type].size();
		if (current < 0 || current >= l || _hist->overview[_type][current] != msgId) {
			current = -1;
			for (int32 i = 0; i < l; ++i) {
				if (_hist->overview[_type][i] == msgId) {
					current = i;
					break;
				}
			}
		}
	} else {
		int32 l = _items.size();
		if (current < 0 || current >= l || _items[current].msgid != msgId) {
			current = -1;
			for (int32 i = 0; i < l; ++i) {
				if (_items[i].msgid == msgId) {
					current = i;
					break;
				}
			}
		}
	}
}

void OverviewInner::searchReceived(bool fromStart, const MTPmessages_Messages &result, mtpRequestId req) {
	if (fromStart && !_search.text().isEmpty()) {
		SearchQueries::iterator i = _searchQueries.find(req);
		if (i != _searchQueries.cend()) {
			_searchCache[i.value()] = result;
			_searchQueries.erase(i);
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
			if (messages->isEmpty()) {
				_searchFull = true;
			}
			if (fromStart) {
				_searchResults.clear();
				_lastSearchId = 0;
				_itemsToBeLoaded = LinksOverviewPerPage * 2;
			}
			for (QVector<MTPMessage>::const_iterator i = messages->cbegin(), e = messages->cend(); i != e; ++i) {
				HistoryItem *item = App::histories().addNewMessage(*i, NewMessageExisting);
				_searchResults.push_front(item->id);
				_lastSearchId = item->id;
			}
			mediaOverviewUpdated();
		}

		_searchRequest = 0;
		_overview->onScroll();
	}
}

bool OverviewInner::searchFailed(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (_searchRequest == req) {
		_searchRequest = 0;
		_searchFull = true;
	}
	return true;
}

OverviewInner::CachedLink *OverviewInner::cachedLink(HistoryItem *item) {
	CachedLinks::const_iterator i = _links.constFind(item->id);
	if (i == _links.cend()) i = _links.insert(item->id, new CachedLink(item));
	return i.value();
}

QString OverviewInner::urlByIndex(MsgId msgid, int32 index, int32 lnkIndex, bool *fullShown) const {
	fixItemIndex(index, msgid);
	if (index < 0 || !_items[index].link) return QString();

	if (lnkIndex < 0) {
		if (fullShown) *fullShown = (_items[index].link->urls.size() == 1) && (_items[index].link->urls.at(0).width <= _linksWidth - (st::dlgPhotoSize + st::dlgPhotoPadding));
		if (_items[index].link->page) {
			return _items[index].link->page->url;
		} else if (!_items[index].link->urls.isEmpty()) {
			return _items[index].link->urls.at(0).url;
		}
	} else if (lnkIndex > 0 && lnkIndex <= _items[index].link->urls.size()) {
		if (fullShown) *fullShown = _items[index].link->urls.at(lnkIndex - 1).width <= _linksWidth - (st::dlgPhotoSize + st::dlgPhotoPadding);
		return _items[index].link->urls.at(lnkIndex - 1).url;
	}
	return QString();
}

bool OverviewInner::urlIsEmail(const QString &url) const {
	int32 at = url.indexOf('@'), slash = url.indexOf('/');
	return (at > 0) && (slash < 0 || slash > at);
}

bool OverviewInner::itemHasPoint(MsgId msgId, int32 index, int32 x, int32 y) const {
	fixItemIndex(index, msgId);
	if (index < 0) return false;

	if (_type == OverviewPhotos) {
		if (x >= 0 && x < _vsize && y >= 0 && y < _vsize) {
			return true;
		}
	} else if (_type == OverviewAudioDocuments) {
		if (x >= _audioLeft && x < _audioLeft + _audioWidth && y >= 0 && y < _audioHeight) {
			return true;
		}
	} else if (_type == OverviewLinks) {
		if (x >= _linksLeft && x < _linksLeft + _linksWidth && y >= 0 && y < itemHeight(msgId, index)) {
			return true;
		}
	} else {
		HistoryItem *item = App::histItemById(_channel, msgId);
		HistoryMedia *media = item ? item->getMedia(true) : 0;
		if (media) {
			int32 w = _width - st::msgMargin.left() - st::msgMargin.right();
			bool out = item->out(), fromChannel = item->fromChannel(), outbg = out && !fromChannel;
			int32 mw = media->maxWidth(), left = (fromChannel ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out ? st::msgMargin.right() : st::msgMargin.left())) + ((mw < w) ? (fromChannel ? 0 : (out ? w - mw : 0)) : 0);
			if (item->displayFromPhoto()) {
				left += st::msgPhotoSkip;
			}
			return media->hasPoint(x - left, y - st::msgMargin.top(), item, w);
		}
	}
	return false;
}

int32 OverviewInner::itemHeight(MsgId msgId, int32 index) const {
	if (_type == OverviewPhotos) {
		return _vsize;
	} else if (_type == OverviewAudioDocuments) {
		return _audioHeight;
	}

	fixItemIndex(index, msgId);
	if (_type == OverviewLinks) {
		return (index < 0) ? 0 : ((index + 1 < _items.size() ? _items[index + 1].y : (_height - _addToY)) - _items[index].y);
	}
	return (index < 0) ? 0 : (_items[index].y - (index > 0 ? _items[index - 1].y : 0));
}

void OverviewInner::moveToNextItem(MsgId &msgId, int32 &index, MsgId upTo, int32 delta) const {
	fixItemIndex(index, msgId);
	if (msgId == upTo || index < 0) {
		msgId = 0;
		index = -1;
		return;
	}

	index += delta;
	if (_type == OverviewPhotos || _type == OverviewAudioDocuments) {
		if (index < 0 || index >= _hist->overview[_type].size()) {
			msgId = 0;
			index = -1;
		} else {
			msgId = _hist->overview[_type][index];
		}
	} else {
		while (index >= 0 && index < _items.size() && !_items[index].msgid) {
			index += (delta > 0) ? 1 : -1;
		}
		if (index < 0 || index >= _items.size()) {
			msgId = 0;
			index = -1;
		} else {
			msgId = _items[index].msgid;
		}
	}
}

void OverviewInner::updateMsg(HistoryItem *item) {
	if (App::main() && item) {
		App::main()->msgUpdated(item->history()->peer->id, item);
	}
}

void OverviewInner::updateMsg(MsgId itemId, int32 itemIndex) {
	fixItemIndex(itemIndex, itemId);
	if (itemIndex >= 0) {
		if (_type == OverviewPhotos) {
			float64 w = (float64(_width - st::overviewPhotoSkip) / _photosInRow);
			int32 vsize = (_vsize + st::overviewPhotoSkip);
			int32 row = (_photosToAdd + itemIndex) / _photosInRow, col = (_photosToAdd + itemIndex) % _photosInRow;
			update(int32(col * w), _addToY + int32(row * vsize), qCeil(w), vsize);
		} else if (_type == OverviewAudioDocuments) {
			update(_audioLeft, _addToY + int32(itemIndex * _audioHeight), _audioWidth, _audioHeight);
		} else if (_type == OverviewLinks) {
			update(_linksLeft, _addToY + _items[itemIndex].y, _linksWidth, itemHeight(itemId, itemIndex));
		} else {
			update(0, _addToY + _height - _items[itemIndex].y, _width, itemHeight(itemId, itemIndex));
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
		updateMsg(App::pressedLinkItem());
		textlnkDown(textlnkOver());
		App::pressedLinkItem(App::hoveredLinkItem());
		updateMsg(App::pressedLinkItem());
	}
	if (_lnkDownIndex != _lnkOverIndex) {
		if (_dragItem) updateMsg(_dragItem, _dragItemIndex);
		_lnkDownIndex = _lnkOverIndex;
		if (_mousedItem) updateMsg(_mousedItem, _mousedItemIndex);
	}

	_dragAction = NoDrag;
	_dragItem = _mousedItem;
	_dragItemIndex = _mousedItemIndex;
	_dragStartPos = mapMouseToItem(mapFromGlobal(screenPos), _dragItem, _dragItemIndex);
	_dragWasInactive = App::wnd()->inactivePress();
	if (_dragWasInactive) App::wnd()->inactivePress(false);
	if ((textlnkDown() || _lnkDownIndex) && _selected.isEmpty()) {
		_dragAction = PrepareDrag;
	} else if (!_selected.isEmpty()) {
		if (_selected.cbegin().value() == FullItemSel) {
			if (_selected.constFind(_dragItem) != _selected.cend() && (textlnkDown() || _lnkDownIndex)) {
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
			if (textlnkDown() || _lnkDownIndex) {
				_dragSymbol = symbol;
				uint32 selStatus = (_dragSymbol << 16) | _dragSymbol;
				if (selStatus != FullItemSel && (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel)) {
					if (!_selected.isEmpty()) {
						updateMsg(_selected.cbegin().key(), -1);
						_selected.clear();
					}
					_selected.insert(_dragItem, selStatus);
					_dragAction = Selecting;
					updateMsg(_dragItem, _dragItemIndex);
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
	int32 needClickIndex = 0;

	dragActionUpdate(screenPos);

	if (textlnkOver()) {
		if (textlnkDown() == textlnkOver() && _dragAction != Dragging && !_selMode) {
			needClick = textlnkDown();
		}
	}
	if (_lnkOverIndex) {
		if (_lnkDownIndex == _lnkOverIndex && _dragAction != Dragging && !_selMode) {
			needClickIndex = _lnkDownIndex;
		}
	}
	if (textlnkDown()) {
		updateMsg(App::pressedLinkItem());
		textlnkDown(TextLinkPtr());
		App::pressedLinkItem(0);
		if (!textlnkOver() && _cursor != style::cur_default) {
			_cursor = style::cur_default;
			setCursor(_cursor);
		}
	}
	if (_lnkDownIndex) {
		updateMsg(_dragItem, _dragItemIndex);
		_lnkDownIndex = 0;
		if (!_lnkOverIndex && _cursor != style::cur_default) {
			_cursor = style::cur_default;
			setCursor(_cursor);
		}
	}
	if (needClick) {
		needClick->onClick(button);
		dragActionCancel();
		return;
	}
	if (needClickIndex) {
		QString url = urlByIndex(_dragItem, _dragItemIndex, needClickIndex);
		if (urlIsEmail(url)) {
			EmailLink(url).onClick(button);
		} else {
			TextLink(url).onClick(button);
		}
		dragActionCancel();
		return;
	}
	if (_dragAction == PrepareSelect && !needClick && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i == _selected.cend() && _dragItem > 0) {
			if (_selected.size() < MaxSelectedItems) {
				if (!_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
					_selected.clear();
				}
				_selected.insert(_dragItem, FullItemSel);
			}
		} else {
			_selected.erase(i);
		}
		updateMsg(_dragItem, _dragItemIndex);
	} else if (_dragAction == PrepareDrag && !needClick && !_dragWasInactive && button != Qt::RightButton) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i != _selected.cend() && i.value() == FullItemSel) {
			_selected.erase(i);
			updateMsg(_dragItem, _dragItemIndex);
		} else if (i == _selected.cend() && _dragItem > 0 && !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.insert(_dragItem, FullItemSel);
				updateMsg(_dragItem, _dragItemIndex);
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
			if (sel != FullItemSel && (sel & 0xFFFF) == ((sel >> 16) & 0xFFFF)) {
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
		if (!_selected.isEmpty() && _selected.cbegin().value() == FullItemSel) {
			uponSelected = _selected.contains(_dragItem);
		} else {
			uponSelected = false;
		}
	}
	QString sel;
	QList<QUrl> urls;
	bool forwardSelected = false;
	if (uponSelected) {
		forwardSelected = !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel && cWideMode() && !_hist->peer->isChannel();
	} else if (textlnkDown()) {
		sel = textlnkDown()->encoded();
		if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
//			urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
		}
	} else if (_lnkDownIndex) {
		QString url = urlByIndex(_dragItem, _dragItemIndex, _lnkDownIndex);
		if (urlIsEmail(url)) {
			sel = EmailLink(url).encoded();
		} else {
			sel = TextLink(url).encoded();
		}
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
			lnkDocument = (lnkType == qstr("DocumentOpenLink"));
		if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument) {
			QDrag *drag = new QDrag(App::wnd());
			QMimeData *mimeData = new QMimeData;

			if (!_hist->peer->isChannel()) {
				mimeData->setData(qsl("application/x-td-forward-pressed-link"), "1");
			}
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

void OverviewInner::applyDragSelection() {
	if (_dragSelFromIndex < 0 || _dragSelToIndex < 0) return;

	if (!_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
		_selected.clear();
	}
	if (_dragSelecting) {
		for (int32 i = _dragSelToIndex; i <= _dragSelFromIndex; ++i) {
			MsgId msgid = (_type == OverviewPhotos || _type == OverviewAudioDocuments) ? _hist->overview[_type][i] : _items[i].msgid;
			if (!msgid) continue;

			SelectedItems::iterator j = _selected.find(msgid);
			if (msgid > 0) {
				if (j == _selected.cend()) {
					if (_selected.size() >= MaxSelectedItems) break;
					_selected.insert(msgid, FullItemSel);
				} else if (j.value() != FullItemSel) {
					*j = FullItemSel;
				}
			} else {
				if (j != _selected.cend()) {
					_selected.erase(j);
				}
			}
		}
	} else {
		for (int32 i = _dragSelToIndex; i <= _dragSelFromIndex; ++i) {
			MsgId msgid = (_type == OverviewPhotos || _type == OverviewAudioDocuments) ? _hist->overview[_type][i] : _items[i].msgid;
			if (!msgid) continue;

			SelectedItems::iterator j = _selected.find(msgid);
			if (j != _selected.cend()) {
				_selected.erase(j);
			}
		}
	}
	_dragSelFrom = _dragSelTo = 0;
	_dragSelFromIndex = _dragSelToIndex = -1;
	_overview->updateTopBarSelection();
}

QPoint OverviewInner::mapMouseToItem(QPoint p, MsgId itemId, int32 itemIndex) {
	fixItemIndex(itemIndex, itemId);
	if (itemIndex < 0) return QPoint(0, 0);

	if (_type == OverviewPhotos) {
		int32 row = (_photosToAdd + itemIndex) / _photosInRow, col = (_photosToAdd + itemIndex) % _photosInRow;
		float64 w = (_width - st::overviewPhotoSkip) / float64(_photosInRow);
		p.setX(p.x() - int32(col * w) - st::overviewPhotoSkip);
		p.setY(p.y() - _addToY - row * (_vsize + st::overviewPhotoSkip) - st::overviewPhotoSkip);
	} else if (_type == OverviewAudioDocuments) {
		p.setY(p.y() - _addToY - itemIndex * _audioHeight);
	} else if (_type == OverviewLinks) {
		p.setY(p.y() - _addToY - _items[itemIndex].y);
	} else {
		p.setY(p.y() - _addToY - (_height - _items[itemIndex].y));
	}
	return p;
}

void OverviewInner::activate() {
	if (_type == OverviewLinks) {
		_search.setFocus();
	} else {
		setFocus();
	}
}

void OverviewInner::clear() {
	_cached.clear();
}

int32 OverviewInner::itemTop(const FullMsgId &msgId) const {
	if (_type == OverviewAudioDocuments && msgId.channel == _channel) {
		int32 index = _hist->overview[_type].indexOf(msgId.msg);
		if (index >= 0) {
			return _addToY + int32(index * _audioHeight);
		}
	}
	return -1;
}

void OverviewInner::preloadMore() {
	if (_inSearch) {
		if (!_searchRequest && !_searchFull) {
			int32 flags = _hist->peer->isChannel() ? MTPmessages_Search_flag_only_important : 0;
			_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _hist->peer->input, MTP_string(_searchQuery), MTP_inputMessagesFilterUrl(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(_lastSearchId), MTP_int(SearchPerPage)), rpcDone(&OverviewInner::searchReceived, !_lastSearchId), rpcFail(&OverviewInner::searchFailed));
			if (!_lastSearchId) {
				_searchQueries.insert(_searchRequest, _searchQuery);
			}
		}
	} else if (App::main()) {
		App::main()->loadMediaBack(_hist->peer, _type, _type != OverviewLinks);
	}
}

bool OverviewInner::preloadLocal() {
	if (_type != OverviewLinks) return false;
	if (_itemsToBeLoaded >= _hist->overview[_type].size()) return false;
	_itemsToBeLoaded += LinksOverviewPerPage;
	mediaOverviewUpdated();
	return true;
}

QPixmap OverviewInner::genPix(PhotoData *photo, int32 size) {
	size *= cIntRetinaFactor();
	QImage img = (photo->full->loaded() ? photo->full : (photo->medium->loaded() ? photo->medium : photo->thumb))->pix().toImage();
	if (!photo->full->loaded() && !photo->medium->loaded()) {
		img = imageBlur(img);
	}
	if (img.width() == img.height()) {
		if (img.width() != size) {
			img = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		}
	} else if (img.width() > img.height()) {
        img = img.copy((img.width() - img.height()) / 2, 0, img.height(), img.height()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	} else {
        img = img.copy(0, (img.height() - img.width()) / 2, img.width(), img.width()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	}
//	imageRound(img);
	img.setDevicePixelRatio(cRetinaFactor());
	photo->forget();
	return QPixmap::fromImage(img, Qt::ColorOnly);
}

void OverviewInner::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);

	QRect r(e->rect());
	p.setClipRect(r);

	if (_hist->overview[_type].isEmpty()) {
		QPoint dogPos((_width - st::msgDogImg.pxWidth()) / 2, ((height() - st::msgDogImg.pxHeight()) * 4) / 9);
		p.drawPixmap(dogPos, *cChatDogImage());
		return;
	} else if (_inSearch && _searchResults.isEmpty() && _searchFull && !_searchTimer.isActive()) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(_linksLeft, _addToY, _linksWidth, _addToY), lng_search_found_results(lt_count, 0), style::al_center);
		return;
	}

	int32 selfrom = -1, selto = -1;
	if (_dragSelFromIndex >= 0 && _dragSelToIndex >= 0) {
		selfrom = _dragSelToIndex;
		selto = _dragSelFromIndex;
	}

	SelectedItems::const_iterator selEnd = _selected.cend();
	bool hasSel = !_selected.isEmpty();

	if (_type == OverviewPhotos) {
		History::MediaOverview &overview(_hist->overview[_type]);
		int32 count = overview.size();
		int32 rowFrom = floorclamp(r.y() - _addToY - st::overviewPhotoSkip, _vsize + st::overviewPhotoSkip, 0, count);
		int32 rowTo = ceilclamp(r.y() + r.height() - _addToY - st::overviewPhotoSkip, _vsize + st::overviewPhotoSkip, 0, count);
		float64 w = float64(_width - st::overviewPhotoSkip) / _photosInRow;
		for (int32 row = rowFrom; row < rowTo; ++row) {
			if (row * _photosInRow >= _photosToAdd + count) break;
			for (int32 i = 0; i < _photosInRow; ++i) {
				int32 index = row * _photosInRow + i - _photosToAdd;
				if (index < 0) continue;
				if (index >= count) break;

				HistoryItem *item = App::histItemById(_channel, overview[index]);
				HistoryMedia *m = item ? item->getMedia(true) : 0;
				if (!m) continue;

				switch (m->type()) {
				case MediaTypePhoto: {
					PhotoData *photo = static_cast<HistoryPhoto*>(m)->photo();
					bool quality = photo->full->loaded();
					if (!quality) {
						if (photo->thumb->loaded()) {
							photo->medium->load(false, false);
							quality = photo->medium->loaded();
						} else {
							photo->thumb->load();
						}
					}
					CachedSizes::iterator it = _cached.find(photo);
					if (it == _cached.cend()) {
						CachedSize size;
						size.medium = quality;
						size.vsize = _vsize;
						size.pix = genPix(photo, _vsize);
						it = _cached.insert(photo, size);
					} else if (it->medium != quality || it->vsize != _vsize) {
						it->medium = quality;
						it->vsize = _vsize;
						it->pix = genPix(photo, _vsize);
					}
					QPoint pos(int32(i * w + st::overviewPhotoSkip), _addToY + row * (_vsize + st::overviewPhotoSkip) + st::overviewPhotoSkip);
					p.drawPixmap(pos, it->pix);
					if (!quality) {
						uint64 dt = itemAnimations().animate(item, getms());
						int32 cnt = int32(st::photoLoaderCnt), period = int32(st::photoLoaderPeriod), t = dt % period, delta = int32(st::photoLoaderDelta);

						int32 x = pos.x() + (_vsize - st::overviewLoader.width()) / 2, y = pos.y() + (_vsize - st::overviewLoader.height()) / 2;
						p.fillRect(x, y, st::overviewLoader.width(), st::overviewLoader.height(), st::photoLoaderBg->b);
						x += (st::overviewLoader.width() - cnt * st::overviewLoaderPoint.width() - (cnt - 1) * st::overviewLoaderSkip) / 2;
						y += (st::overviewLoader.height() - st::overviewLoaderPoint.height()) / 2;
						QColor c(st::white->c);
						QBrush b(c);
						for (int32 i = 0; i < cnt; ++i) {
							t -= delta;
							while (t < 0) t += period;

							float64 alpha = (t >= st::photoLoaderDuration1 + st::photoLoaderDuration2) ? 0 : ((t > st::photoLoaderDuration1 ? ((st::photoLoaderDuration1 + st::photoLoaderDuration2 - t) / st::photoLoaderDuration2) : (t / st::photoLoaderDuration1)));
							c.setAlphaF(st::photoLoaderAlphaMin + alpha * (1 - st::photoLoaderAlphaMin));
							b.setColor(c);
							p.fillRect(x + i * (st::overviewLoaderPoint.width() + st::overviewLoaderSkip), y, st::overviewLoaderPoint.width(), st::overviewLoaderPoint.height(), b);
						}
					}

					uint32 sel = 0;
					if (index >= selfrom && index <= selto) {
						sel = (_dragSelecting && item->id > 0) ? FullItemSel : 0;
					} else if (hasSel) {
						SelectedItems::const_iterator i = _selected.constFind(item->id);
						if (i != selEnd) {
							sel = i.value();
						}
					}
					if (sel == FullItemSel) {
						p.fillRect(QRect(pos.x(), pos.y(), _vsize, _vsize), st::overviewPhotoSelectOverlay->b);
						p.drawPixmap(QPoint(pos.x() + _vsize - st::overviewPhotoCheck.pxWidth(), pos.y() + _vsize - st::overviewPhotoCheck.pxHeight()), App::sprite(), st::overviewPhotoChecked);
					} else if (_selMode/* || (selfrom < count && selfrom <= selto && 0 <= selto)*/) {
						p.drawPixmap(QPoint(pos.x() + _vsize - st::overviewPhotoCheck.pxWidth(), pos.y() + _vsize - st::overviewPhotoCheck.pxHeight()), App::sprite(), st::overviewPhotoCheck);
					}
				} break;
				}
			}
		}
	} else if (_type == OverviewAudioDocuments) {
		History::MediaOverview &overview(_hist->overview[_type]);
		int32 count = overview.size();
		int32 from = floorclamp(r.y() - _addToY, _audioHeight, 0, count);
		int32 to = ceilclamp(r.y() + r.height() - _addToY, _audioHeight, 0, count);
		p.translate(_audioLeft, _addToY + from * _audioHeight);
		for (int32 index = from; index < to; ++index) {
			if (index >= count) break;

			HistoryItem *item = App::histItemById(_channel, overview[index]);
			HistoryMedia *m = item ? item->getMedia(true) : 0;
			if (!m || m->type() != MediaTypeDocument) continue;

			uint32 sel = 0;
			if (index >= selfrom && index <= selto) {
				sel = (_dragSelecting && item->id > 0) ? FullItemSel : 0;
			} else if (hasSel) {
				SelectedItems::const_iterator i = _selected.constFind(item->id);
				if (i != selEnd) {
					sel = i.value();
				}
			}

			static_cast<HistoryDocument*>(m)->drawInPlaylist(p, item, (sel == FullItemSel), ((_menu ? (App::contextItem() ? App::contextItem()->id : 0) : _selectedMsgId) == item->id), _audioWidth);
			p.translate(0, _audioHeight);
		}
	} else if (_type == OverviewLinks) {
		p.translate(_linksLeft, _addToY);
		int32 y = 0, w = _linksWidth;
		for (int32 i = 0, l = _items.size(); i < l; ++i) {
			if (i + 1 == l || _addToY + _items[i + 1].y > r.top()) {
				int32 left = st::dlgPhotoSize + st::dlgPhotoPadding, top = st::linksMargin + st::linksBorder, curY = _items[i].y;
				if (_addToY + curY >= r.y() + r.height()) break;

				p.translate(0, curY - y);
				if (_items[i].msgid) { // draw item
					CachedLink *lnk = _items[i].link;
					WebPageData *page = lnk->page;
					if (page && page->photo) {
						QPixmap pix;
						if (page->photo->full->loaded()) {
							pix = page->photo->full->pixSingle(lnk->pixw, lnk->pixh, st::dlgPhotoSize, st::dlgPhotoSize);
						} else if (page->photo->medium->loaded()) {
							pix = page->photo->medium->pixSingle(lnk->pixw, lnk->pixh, st::dlgPhotoSize, st::dlgPhotoSize);
						} else {
							pix = page->photo->thumb->pixBlurredSingle(lnk->pixw, lnk->pixh, st::dlgPhotoSize, st::dlgPhotoSize);
						}
						p.drawPixmap(0, top, pix);
					} else if (page && page->doc && !page->doc->thumb->isNull()) {
						p.drawPixmap(0, top, page->doc->thumb->pixSingle(lnk->pixw, lnk->pixh, st::dlgPhotoSize, st::dlgPhotoSize));
					} else {
						int32 index = lnk->letter.isEmpty() ? 0 : (lnk->letter.at(0).unicode() % 4);
						switch (index) {
						case 0: App::roundRect(p, QRect(0, top, st::dlgPhotoSize, st::dlgPhotoSize), st::mvDocRedColor, DocRedCorners); break;
						case 1: App::roundRect(p, QRect(0, top, st::dlgPhotoSize, st::dlgPhotoSize), st::mvDocYellowColor, DocYellowCorners); break;
						case 2: App::roundRect(p, QRect(0, top, st::dlgPhotoSize, st::dlgPhotoSize), st::mvDocGreenColor, DocGreenCorners); break;
						case 3: App::roundRect(p, QRect(0, top, st::dlgPhotoSize, st::dlgPhotoSize), st::mvDocBlueColor, DocBlueCorners); break;
						}
						
						if (!lnk->letter.isEmpty()) {
							p.setFont(st::linksLetterFont->f);
							p.setPen(st::white->p);
							p.drawText(QRect(0, top, st::dlgPhotoSize, st::dlgPhotoSize), lnk->letter, style::al_center);
						}
					}

					uint32 sel = 0;
					if (i >= selfrom && i <= selto) {
						sel = (_dragSelecting && _items[i].msgid > 0) ? FullItemSel : 0;
					} else if (hasSel) {
						SelectedItems::const_iterator j = _selected.constFind(_items[i].msgid);
						if (j != selEnd) {
							sel = j.value();
						}
					}
					if (sel == FullItemSel) {
						App::roundRect(p, QRect(0, top, st::dlgPhotoSize, st::dlgPhotoSize), st::overviewPhotoSelectOverlay, PhotoSelectOverlayCorners);
						p.drawPixmap(QPoint(st::dlgPhotoSize - st::linksPhotoCheck.pxWidth(), top + st::dlgPhotoSize - st::linksPhotoCheck.pxHeight()), App::sprite(), st::linksPhotoChecked);
					} else if (_selMode/* || (selfrom < count && selfrom <= selto && 0 <= selto)*/) {
						p.drawPixmap(QPoint(st::dlgPhotoSize - st::linksPhotoCheck.pxWidth(), top + st::dlgPhotoSize - st::linksPhotoCheck.pxHeight()), App::sprite(), st::linksPhotoCheck);
					}

					if (!lnk->title.isEmpty() && lnk->text.isEmpty() && lnk->urls.size() == 1) {
						top += (st::dlgPhotoSize - st::webPageTitleFont->height - st::msgFont->height) / 2;
					}

					p.setPen(st::black->p);
					p.setFont(st::webPageTitleFont->f);
					if (!lnk->title.isEmpty()) {
						p.drawText(left, top + st::webPageTitleFont->ascent, (_linksWidth - left < lnk->titleWidth) ? st::webPageTitleFont->elided(lnk->title, _linksWidth - left) : lnk->title);
						top += st::webPageTitleFont->height;
					}
					p.setFont(st::msgFont->f);
					if (!lnk->text.isEmpty()) {
						lnk->text.drawElided(p, left, top, _linksWidth - left, 3);
						top += qMin(st::msgFont->height * 3, lnk->text.countHeight(_linksWidth - left));
					}

					p.setPen(st::btnYesColor->p);
					for (int32 j = 0, c = lnk->urls.size(); j < c; ++j) {
						bool sel = (_mousedItem == _items[i].msgid && j + 1 == _lnkOverIndex);
						if (sel) p.setFont(st::msgFont->underline()->f);
						p.drawText(left, top + st::msgFont->ascent, (_linksWidth - left < lnk->urls[j].width) ? st::msgFont->elided(lnk->urls[j].text, _linksWidth - left) : lnk->urls[j].text);
						if (sel) p.setFont(st::msgFont->f);
						top += st::msgFont->height;
					}
					p.fillRect(left, _items[i].y - curY, _linksWidth - left, st::linksBorder, st::linksBorderColor->b);
				} else {
					QString str = langDayOfMonth(_items[i].date);

					p.setPen(st::linksDateColor->p);
					p.setFont(st::msgFont->f);
					p.drawText(0, st::linksDateMargin + st::msgFont->ascent, str);
				}
				y = curY;
			}
		}
	} else {
		p.translate(0, st::msgMargin.top() + _addToY);
		int32 y = 0, w = _width - st::msgMargin.left() - st::msgMargin.right();
		for (int32 i = _items.size(); i > 0;) {
			--i;
			if (!i || (_addToY + _height - _items[i - 1].y > r.top())) {
				int32 curY = _height - _items[i].y;
				if (_addToY + curY >= r.y() + r.height()) break;

				p.translate(0, curY - y);
				if (_items[i].msgid) { // draw item
					HistoryItem *item = App::histItemById(_channel, _items[i].msgid);
					HistoryMedia *media = item ? item->getMedia(true) : 0;
					if (media) {
						bool out = item->out(), fromChannel = item->fromChannel(), outbg = out && !fromChannel;
						int32 mw = media->maxWidth(), left = (fromChannel ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out ? st::msgMargin.right() : st::msgMargin.left())) + ((mw < w) ? (fromChannel ? 0 : (out ? w - mw : 0)) : 0);
						if (item->displayFromPhoto()) {
							p.drawPixmap(left, media->countHeight(item, w) - st::msgPhotoSize, item->from()->photo->pixRounded(st::msgPhotoSize));
							left += st::msgPhotoSkip;
						}

						uint32 sel = 0;
						if (i >= selfrom && i <= selto) {
							sel = (_dragSelecting && item->id > 0) ? FullItemSel : 0;
						} else if (hasSel) {
							SelectedItems::const_iterator i = _selected.constFind(item->id);
							if (i != selEnd) {
								sel = i.value();
							}
						}

						p.save();
						p.translate(left, 0);
						media->draw(p, item, (sel == FullItemSel), w);
						p.restore();
					}
				} else {
					QString str = langDayOfMonth(_items[i].date);

					int32 left = st::msgServiceMargin.left(), width = _width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom();
					if (width < 1) return;

					int32 strwidth = st::msgServiceFont->width(str) + st::msgServicePadding.left() + st::msgServicePadding.right();

					QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
					left += (width - strwidth) / 2;
					width = strwidth;

					QRect r(left, st::msgServiceMargin.top(), width, height);
					App::roundRect(p, r, App::msgServiceBg(), ServiceCorners);

					p.setBrush(Qt::NoBrush);
					p.setPen(st::msgServiceColor->p);
					p.setFont(st::msgServiceFont->f);
					p.drawText(r.x() + st::msgServicePadding.left(), r.y() + st::msgServicePadding.top() + st::msgServiceFont->ascent, str);
				}
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
	int32 lnkIndex = 0; // for OverviewLinks
	HistoryItem *item = 0;
	int32 index = -1;
	int32 newsel = 0;
	HistoryCursorState cursorState = HistoryDefaultCursorState;
	if (_type == OverviewPhotos) {
		float64 w = (float64(_width - st::overviewPhotoSkip) / _photosInRow);
		int32 inRow = int32((m.x() - (st::overviewPhotoSkip / 2)) / w), vsize = (_vsize + st::overviewPhotoSkip);
		int32 row = int32((m.y() - _addToY - (st::overviewPhotoSkip / 2)) / vsize);
		if (inRow < 0) inRow = 0;
		if (row < 0) row = 0;
		bool upon = true;

		int32 i = row * _photosInRow + inRow - _photosToAdd, count = _hist->overview[_type].size();
		if (i < 0) {
			i = 0;
			upon = false;
		}
		if (i >= count) {
			i = count - 1;
			upon = false;
		}
		if (i >= 0) {
			MsgId msgid = _hist->overview[_type][i];
			HistoryItem *histItem = App::histItemById(_channel, msgid);
			if (histItem) {
				item = histItem;
				index = i;
				if (upon && m.x() >= inRow * w + st::overviewPhotoSkip && m.x() < inRow * w + st::overviewPhotoSkip + _vsize) {
					if (m.y() >= _addToY + row * vsize + st::overviewPhotoSkip && m.y() < _addToY + (row + 1) * vsize + st::overviewPhotoSkip) {
						HistoryMedia *media = item->getMedia(true);
						if (media && media->type() == MediaTypePhoto) {
							lnk = static_cast<HistoryPhoto*>(media)->lnk();
						}
					}
				}
			}
		}
	} else if (_type == OverviewAudioDocuments) {
		int32 i = int32((m.y() - _addToY) / _audioHeight), count = _hist->overview[_type].size();

		bool upon = true;
		if (m.y() < _addToY) {
			i = 0;
			upon = false;
		}
		if (i >= count) {
			i = count - 1;
			upon = false;
		}
		if (i >= 0) {
			MsgId msgid = _hist->overview[_type][i];
			HistoryItem *histItem = App::histItemById(_channel, msgid);
			if (histItem) {
				item = histItem;
				index = i;
				if (upon && m.x() >= _audioLeft && m.x() < _audioLeft + _audioWidth) {
					HistoryMedia *media = item->getMedia(true);
					if (media && media->type() == MediaTypeDocument) {
						lnk = static_cast<HistoryDocument*>(media)->linkInPlaylist();
						newsel = item->id;
					}
				}
			}
		}
		if (newsel != _selectedMsgId) {
			if (_selectedMsgId) updateMsg(App::histItemById(_channel, _selectedMsgId));
			_selectedMsgId = newsel;
			updateMsg(item);
		}
	} else if (_type == OverviewLinks) {
		int32 w = _width - st::msgMargin.left() - st::msgMargin.right();
		for (int32 i = 0, l = _items.size(); i < l; ++i) {
			if ((i + 1 == l) || (_addToY + _items[i + 1].y > m.y())) {
				int32 left = st::dlgPhotoSize + st::dlgPhotoPadding, y = _addToY + _items[i].y;
				if (!_items[i].msgid) { // day item
					int32 h = 2 * st::linksDateMargin + st::msgFont->height;// itemHeight(_items[i].msgid, i);
					if (i > 0 && ((y + h / 2) >= m.y() || i == _items.size() - 1)) {
						--i;
						if (!_items[i].msgid) break; // wtf
						y = _addToY + _items[i].y;
					} else if (i < _items.size() - 1 && ((y + h / 2) < m.y() || !i)) {
						++i;
						if (!_items[i].msgid) break; // wtf
						y = _addToY + _items[i].y;
					} else {
						break; // wtf
					}
				}

				HistoryItem *histItem = App::histItemById(_channel, _items[i].msgid);
				if (histItem) {
					item = histItem;
					index = i;

					int32 top = y + st::linksMargin + st::linksBorder, left = _linksLeft + st::dlgPhotoSize + st::dlgPhotoPadding, w = _linksWidth - st::dlgPhotoSize - st::dlgPhotoPadding;
					if (!_items[i].link->title.isEmpty() && _items[i].link->text.isEmpty() && _items[i].link->urls.size() == 1) {
						top += (st::dlgPhotoSize - st::webPageTitleFont->height - st::msgFont->height) / 2;
					}
					if (QRect(_linksLeft, y + st::linksMargin + st::linksBorder, st::dlgPhotoSize, st::dlgPhotoSize).contains(m)) {
						lnkIndex = -1;
					} else if (!_items[i].link->title.isEmpty() && QRect(left, top, qMin(w, _items[i].link->titleWidth), st::webPageTitleFont->height).contains(m)) {
						lnkIndex = -1;
					} else {
						if (!_items[i].link->title.isEmpty()) top += st::webPageTitleFont->height;
						if (!_items[i].link->text.isEmpty()) top += qMin(st::msgFont->height * 3, _items[i].link->text.countHeight(w));
						for (int32 j = 0, c = _items[i].link->urls.size(); j < c; ++j) {
							if (QRect(left, top, qMin(w, _items[i].link->urls[j].width), st::msgFont->height).contains(m)) {
								lnkIndex = j + 1;
								break;
							}
							top += st::msgFont->height;
						}
					}
				}
				break;
			}
		}
	} else {
		int32 w = _width - st::msgMargin.left() - st::msgMargin.right();
		for (int32 i = _items.size(); i > 0;) {
			--i;
			if (!i || (_addToY + _height - _items[i - 1].y > m.y())) {
				int32 y = _addToY + _height - _items[i].y;
				if (!_items[i].msgid) { // day item
					int32 h = st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom(); // itemHeight(_items[i].msgid, i);
					if (i > 0 && ((y + h / 2) < m.y() || i == _items.size() - 1)) {
						--i;
						if (!_items[i].msgid) break; // wtf
						y = _addToY + _height - _items[i].y;
					} else if (i < _items.size() - 1 && ((y + h / 2) >= m.y() || !i)) {
						++i;
						if (!_items[i].msgid) break; // wtf
						y = _addToY + _height - _items[i].y;
					} else {
						break; // wtf
					}
				}

				HistoryItem *histItem = App::histItemById(_channel, _items[i].msgid);
				if (histItem) {
					item = histItem;
					index = i;
					HistoryMedia *media = item->getMedia(true);
					if (media) {
						bool out = item->out(), fromChannel = item->fromChannel(), outbg = out && !fromChannel;
						int32 mw = media->maxWidth(), left = (fromChannel ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out ? st::msgMargin.right() : st::msgMargin.left())) + ((mw < w) ? (fromChannel ? 0 : (out ? w - mw : 0)) : 0);
						if (item->displayFromPhoto()) {
							if (QRect(left, y + st::msgMargin.top() + media->countHeight(item, w) - st::msgPhotoSize, st::msgPhotoSize, st::msgPhotoSize).contains(m)) {
								lnk = item->from()->lnk;
							}
							left += st::msgPhotoSkip;
						}
						TextLinkPtr link;
						media->getState(link, cursorState, m.x() - left, m.y() - y - st::msgMargin.top(), item, w);
						if (link) lnk = link;
					}
				}
				break;
			}
		}
	}

	MsgId oldMousedItem = _mousedItem;
	int32 oldMousedItemIndex = _mousedItemIndex;
	_mousedItem = item ? item->id : 0;
	_mousedItemIndex = index;
	m = mapMouseToItem(m, _mousedItem, _mousedItemIndex);

	Qt::CursorShape cur = style::cur_default;
	bool lnkChanged = false;
	if (lnk != textlnkOver()) {
		lnkChanged = true;
		updateMsg(App::hoveredLinkItem());
		textlnkOver(lnk);
		App::hoveredLinkItem(lnk ? item : 0);
		updateMsg(App::hoveredLinkItem());
		QToolTip::hideText();
	} else {
		App::mousedItem(item);
	}
	if (lnkIndex != _lnkOverIndex || _mousedItem != oldMousedItem) {
		lnkChanged = true;
		if (oldMousedItem) updateMsg(App::histItemById(_channel, oldMousedItem));
		_lnkOverIndex = lnkIndex;
		if (item) updateMsg(item);
		QToolTip::hideText();
	}
	if (_cursorState == HistoryInDateCursorState && cursorState != HistoryInDateCursorState) {
		QToolTip::hideText();
	}
	if (cursorState != _cursorState) {
		_cursorState = cursorState;
	}
	if (lnk || lnkIndex || cursorState == HistoryInDateCursorState) {
		_linkTipTimer.start(1000);
	}

	fixItemIndex(_dragItemIndex, _dragItem);
	fixItemIndex(_mousedItemIndex, _mousedItem);
	if (_dragAction == NoDrag) {
		if (lnk || lnkIndex) {
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
		cur = (textlnkDown() || _lnkDownIndex) ? style::cur_pointer : style::cur_default;
		if (_dragAction == Selecting) {
			bool canSelectMany = (_peer != 0);
			if (_mousedItem == _dragItem && (lnk || lnkIndex) && !_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
				bool afterSymbol = false, uponSymbol = false;
				uint16 second = 0;
				_selected[_dragItem] = 0;
				updateDragSelection(0, -1, 0, -1, false);
			} else if (canSelectMany) {
				bool selectingDown = ((_type == OverviewPhotos || _type == OverviewAudioDocuments || _type == OverviewLinks) ? (_mousedItemIndex > _dragItemIndex) : (_mousedItemIndex < _dragItemIndex)) || (_mousedItemIndex == _dragItemIndex && (_type == OverviewPhotos ? (_dragStartPos.x() < m.x()) : (_dragStartPos.y() < m.y())));
				MsgId dragSelFrom = _dragItem, dragSelTo = _mousedItem;
				int32 dragSelFromIndex = _dragItemIndex, dragSelToIndex = _mousedItemIndex;
				if (!itemHasPoint(dragSelFrom, dragSelFromIndex, _dragStartPos.x(), _dragStartPos.y())) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_type == OverviewPhotos) {
							if (_dragStartPos.x() >= _vsize || ((_mousedItem == dragSelFrom) && (m.x() < _dragStartPos.x() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, 1);
							}
						} else if (_type == OverviewAudioDocuments) {
							if (_dragStartPos.y() >= itemHeight(dragSelFrom, dragSelFromIndex) || ((_mousedItem == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, 1);
							}
						} else if (_type == OverviewLinks) {
							if (_dragStartPos.y() >= itemHeight(dragSelFrom, dragSelFromIndex) || ((_mousedItem == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, 1);
							}
						} else {
							if (_dragStartPos.y() >= (itemHeight(dragSelFrom, dragSelFromIndex) - st::msgMargin.bottom()) || ((_mousedItem == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, -1);
							}
						}
					} else {
						if (_type == OverviewPhotos) {
							if (_dragStartPos.x() < 0 || ((_mousedItem == dragSelFrom) && (m.x() >= _dragStartPos.x() - QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, -1);
							}
						} else if (_type == OverviewAudioDocuments) {
							if (_dragStartPos.y() < 0 || ((_mousedItem == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, -1);
							}
						} else if (_type == OverviewLinks) {
							if (_dragStartPos.y() < 0 || ((_mousedItem == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, -1);
							}
						} else {
							if (_dragStartPos.y() < st::msgMargin.top() || ((_mousedItem == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, 1);
							}
						}
					}
				}
				if (_dragItem != _mousedItem) { // maybe exclude dragSelTo
					if (selectingDown) {
						if (_type == OverviewPhotos) {
							if (m.x() < 0) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, -1);
							}
						} else if (_type == OverviewAudioDocuments) {
							if (m.y() < 0) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, 1);
							}
						} else if (_type == OverviewLinks) {
							if (m.y() < 0) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, -1);
							}
						} else {
							if (m.y() < st::msgMargin.top()) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, 1);
							}
						}
					} else {
						if (_type == OverviewPhotos) {
							if (m.x() >= _vsize) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, 1);
							}
						} else if (_type == OverviewAudioDocuments) {
							if (m.y() >= itemHeight(dragSelTo, dragSelToIndex)) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, -1);
							}
						} else if (_type == OverviewLinks) {
							if (m.y() >= itemHeight(dragSelTo, dragSelToIndex)) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, 1);
							}
						} else {
							if (m.y() >= itemHeight(dragSelTo, dragSelToIndex) - st::msgMargin.bottom()) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, -1);
							}
						}
					}
				}
				bool dragSelecting = false;
				MsgId dragFirstAffected = dragSelFrom;
				int32 dragFirstAffectedIndex = dragSelFromIndex;
				while (dragFirstAffectedIndex >= 0 && dragFirstAffected <= 0) {
					moveToNextItem(dragFirstAffected, dragFirstAffectedIndex, dragSelTo, ((selectingDown && (_type == OverviewPhotos || _type == OverviewAudioDocuments)) || (!selectingDown && (_type != OverviewPhotos && _type != OverviewAudioDocuments))) ? -1 : 1);
				}
				if (dragFirstAffectedIndex >= 0) {
					SelectedItems::const_iterator i = _selected.constFind(dragFirstAffected);
					dragSelecting = (i == _selected.cend() || i.value() != FullItemSel);
				}
				updateDragSelection(dragSelFrom, dragSelFromIndex, dragSelTo, dragSelToIndex, dragSelecting);
			}
		} else if (_dragAction == Dragging) {
		}

		if (textlnkDown() || _lnkDownIndex) {
			cur = style::cur_pointer;
		} else if (_dragAction == Selecting && !_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
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
	} else if (_lnkOverIndex) {
		bool fullLink = false;
		QString url = urlByIndex(_mousedItem, _mousedItemIndex, _lnkOverIndex, &fullLink);
		if (!fullLink) {
			QToolTip::showText(_dragPos, url, this, r);
		}
	} else if (_cursorState == HistoryInDateCursorState && _dragAction == NoDrag && _mousedItem) {
		if (HistoryItem *item = App::histItemById(_channel, _mousedItem)) {
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
	if (_selectedMsgId > 0) {
		updateMsg(App::histItemById(_channel, _selectedMsgId));
		_selectedMsgId = 0;
	}
	if (textlnkOver()) {
		updateMsg(App::hoveredLinkItem());
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
	_width = width();
	_audioWidth = qMin(_width - st::profilePadding.left() - st::profilePadding.right(), int(st::profileMaxWidth));
	_audioLeft = (_width - _audioWidth) / 2;
	_linksWidth = qMin(_width - st::linksSearchMargin.left() - st::linksSearchMargin.right(), int(st::linksMaxWidth));
	_linksLeft = (_width - _linksWidth) / 2;
	_search.setGeometry(_linksLeft, st::linksSearchMargin.top(), _linksWidth, _search.height());
	_cancelSearch.move(_linksLeft + _linksWidth - _cancelSearch.width(), _search.y());
	showAll(true);
	onUpdateSelected();
	update();
}

void OverviewInner::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
		updateMsg(App::contextItem());
		if (_selectedMsgId > 0) updateMsg(App::histItemById(_channel, _selectedMsgId));
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		dragActionUpdate(e->globalPos());
	}

	bool ignoreMousedItem = false;
	if (_mousedItem > 0) {
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
		if (_selected.cbegin().value() == FullItemSel) {
			hasSelected = 2;
			if (!ignoreMousedItem && App::mousedItem() && _selected.constFind(App::mousedItem()->id) != _selected.cend()) {
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
		_menu = new ContextMenu(_overview);
		if (App::hoveredLinkItem()) {
			_menu->addAction(lang(lng_context_to_msg), this, SLOT(goToMessage()))->setEnabled(true);
		}
		if (lnkPhoto) {
			_menu->addAction(lang(lng_context_open_image), this, SLOT(openContextUrl()))->setEnabled(true);
		} else {
			if ((lnkVideo && lnkVideo->video()->loader) || (lnkAudio && lnkAudio->audio()->loader) || (lnkDocument && lnkDocument->document()->loader)) {
				_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
			} else {
				if ((lnkVideo && !lnkVideo->video()->already(true).isEmpty()) || (lnkAudio && !lnkAudio->audio()->already(true).isEmpty()) || (lnkDocument && !lnkDocument->document()->already(true).isEmpty())) {
					_menu->addAction(lang(cPlatform() == dbipMac ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
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
		updateMsg(App::contextItem());
		if (_selectedMsgId > 0) updateMsg(App::histItemById(_channel, _selectedMsgId));
	} else if (!ignoreMousedItem && App::mousedItem() && App::mousedItem()->id == _mousedItem) {
		_contextMenuUrl = _lnkOverIndex ? urlByIndex(_mousedItem, _mousedItemIndex, _lnkOverIndex) : QString();
		_menu = new ContextMenu(_overview);
		if ((_contextMenuLnk && dynamic_cast<TextLink*>(_contextMenuLnk.data())) || (!_contextMenuUrl.isEmpty() && !urlIsEmail(_contextMenuUrl))) {
			_menu->addAction(lang(lng_context_open_link), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_link), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if ((_contextMenuLnk && dynamic_cast<EmailLink*>(_contextMenuLnk.data())) || (!_contextMenuUrl.isEmpty() && urlIsEmail(_contextMenuUrl))) {
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
		updateMsg(App::contextItem());
		if (_selectedMsgId > 0) updateMsg(App::histItemById(_channel, _selectedMsgId));
	}
	if (_menu) {
		_menu->deleteOnHide();
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

int32 OverviewInner::resizeToWidth(int32 nwidth, int32 scrollTop, int32 minHeight) {
	if (width() == nwidth && minHeight == _minHeight) return scrollTop;
	_minHeight = minHeight;
	if (_type == OverviewAudioDocuments) {
		_addToY = st::playlistPadding;
	} else if (_type == OverviewLinks) {
		_addToY = st::linksSearchMargin.top() + _search.height() + st::linksSearchMargin.bottom();
	} else {
		_addToY = (_height < _minHeight) ? (_minHeight - _height) : 0;
	}
	if (_type == OverviewPhotos && _resizeIndex < 0) {
		_resizeIndex = _photosInRow * ((scrollTop + minHeight) / int32(_vsize + st::overviewPhotoSkip)) + _photosInRow - 1;
		_resizeSkip = (scrollTop + minHeight) - ((scrollTop + minHeight) / int32(_vsize + st::overviewPhotoSkip)) * int32(_vsize + st::overviewPhotoSkip);
	}
	resize(nwidth, height() > _minHeight ? height() : _minHeight);
	showAll();
	if (_type == OverviewPhotos) {
        int32 newRow = _resizeIndex / _photosInRow;
        return newRow * int32(_vsize + st::overviewPhotoSkip) + _resizeSkip - minHeight;
    }
    return scrollTop;
}

void OverviewInner::dropResizeIndex() {
	_resizeIndex = -1;
}

PeerData *OverviewInner::peer() const {
	return _peer;
}

MediaOverviewType OverviewInner::type() const {
	return _type;
}

void OverviewInner::switchType(MediaOverviewType type) {
	if (_type != type) {
		_selected.clear();
		_dragItemIndex = _mousedItemIndex = _dragSelFromIndex = _dragSelToIndex = -1;
		_dragItem = _mousedItem = _dragSelFrom = _dragSelTo = 0;
		_lnkOverIndex = _lnkDownIndex = 0;
		_items.clear();
		_cached.clear();
		_type = type;
		if (_type == OverviewLinks) {
			_search.show();
		} else {
			_search.hide();
		}
		_cancelSearch.hide();
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
	} else if (urlIsEmail(_contextMenuUrl)) {
		EmailLink(_contextMenuUrl).onClick(Qt::LeftButton);
	} else {
		TextLink(_contextMenuUrl).onClick(Qt::LeftButton);
	}
}

void OverviewInner::copyContextUrl() {
	QString enc = _contextMenuLnk ? _contextMenuLnk->encoded() : _contextMenuUrl;
	if (!enc.isEmpty()) {
		QApplication::clipboard()->setText(enc);
	}
}

void OverviewInner::goToMessage() {
	HistoryItem *item = App::contextItem();
	if (!item) return;

	App::main()->showPeerHistory(item->history()->peer->id, item->id);
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

void OverviewInner::selectMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg || item->serviceMsg()) return;

	if (!_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
		_selected.clear();
	} else if (_selected.size() == MaxSelectedItems && _selected.constFind(item->id) == _selected.cend()) {
		return;
	}
	_selected.insert(item->id, FullItemSel);
	_overview->updateTopBarSelection();
	_overview->update();
}

void OverviewInner::cancelContextDownload() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	mtpFileLoader *loader = lnkVideo ? lnkVideo->video()->loader : (lnkAudio ? lnkAudio->audio()->loader : (lnkDocument ? lnkDocument->document()->loader : 0));
	if (loader) loader->cancel();
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
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) VideoOpenLink(lnkVideo->video()).onClick(Qt::LeftButton);
	if (lnkAudio) AudioOpenLink(lnkAudio->audio()).onClick(Qt::LeftButton);
	if (lnkDocument) DocumentOpenLink(lnkDocument->document()).onClick(Qt::LeftButton);
}

bool OverviewInner::onSearchMessages(bool searchCache) {
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
			_searchFull = false;
			_searchRequest = 0;
			searchReceived(true, i.value(), 0);
			return true;
		}
	} else if (_searchQuery != q) {
		_searchQuery = q;
		_searchFull = false;
		int32 flags = _hist->peer->isChannel() ? MTPmessages_Search_flag_only_important : 0;
		_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _hist->peer->input, MTP_string(_searchQuery), MTP_inputMessagesFilterUrl(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&OverviewInner::searchReceived, true), rpcFail(&OverviewInner::searchFailed));
		_searchQueries.insert(_searchRequest, _searchQuery);
	}
	return false;
}

void OverviewInner::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
		if (_inSearch && _searchFull && _searchResults.isEmpty()) {
			update();
		}
	}
}

void OverviewInner::onSearchUpdate() {
	QString filterText = _search.text().trimmed();
	bool inSearch = !filterText.isEmpty(), changed = (inSearch != _inSearch);
	_inSearch = inSearch;

	onNeedSearchMessages();

	if (filterText.isEmpty()) {
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
		updateMsg(App::contextItem());
		if (_selectedMsgId > 0) updateMsg(App::histItemById(_channel, _selectedMsgId));
	}
}

void OverviewInner::getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const {
	selectedForForward = selectedForDelete = 0;
	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		if (i.value() == FullItemSel) {
			if (HistoryItem *item = App::histItemById(_channel, i.key())) {
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
	if (!_selected.isEmpty() && (!onlyTextSelection || _selected.cbegin().value() != FullItemSel)) {
		_selected.clear();
		_overview->updateTopBarSelection();
		_overview->update();
	}
}

void OverviewInner::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	if (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel) return;

	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		HistoryItem *item = App::histItemById(_channel, i.key());
		if (item && item->toHistoryMessage() && item->id > 0) {
			sel.insert(item->id, item);
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

void OverviewInner::mediaOverviewUpdated(bool fromResize) {
	int32 oldHeight = _height;
	if (_type == OverviewLinks) {
		History::MediaOverview &o(_inSearch ? _searchResults : _hist->overview[_type]);
		int32 l = o.size(), tocheck = qMin(l, _itemsToBeLoaded);
		_items.reserve(2 * l); // day items

		int32 y = 0, in = 0;
		bool allGood = true;
		QDate prevDate;
		for (int32 i = 0; i < tocheck; ++i) {
			MsgId msgid = o.at(l - i - 1);
			if (allGood) {
				if (_items.size() > in && _items.at(in).msgid == msgid) {
					prevDate = _items.at(in).date;
					if (fromResize) {
						_items[in].y = y;
						y += _items[in].link->countHeight(_linksWidth);
					} else {
						y = (in + 1 < _items.size()) ? _items.at(in + 1).y : _height;
					}
					++in;
					continue;
				}
				if (_items.size() > in + 1 && !_items.at(in).msgid && _items.at(in + 1).msgid == msgid) { // day item
					if (fromResize) {
						_items[in].y = y;
						y += st::msgFont->height + st::linksDateMargin * 2 + st::linksBorder;
					}
					++in;
					prevDate = _items.at(in).date;
					if (fromResize) {
						_items[in].y = y;
						y += _items[in].link->countHeight(_linksWidth);
					} else {
						y = (in + 1 < _items.size()) ? _items.at(in + 1).y : _height;
					}
					++in;
					continue;
				}
				allGood = false;
			}
			HistoryItem *item = App::histItemById(_channel, msgid);

			QDate date = item->date.date();
			if (!in || (in > 0 && date != prevDate)) {
				if (_items.size() > in) {
					_items[in].msgid = 0;
					_items[in].date = date;
					_items[in].y = y;
				} else {
					_items.push_back(CachedItem(0, date, y));
				}
				y += st::msgFont->height + st::linksDateMargin * 2 + st::linksBorder;
				++in;
				prevDate = date;
			}

			if (_items.size() > in) {
				_items[in] = CachedItem(item->id, item->date.date(), y);
				_items[in].link = cachedLink(item);
				y += _items[in].link->countHeight(_linksWidth);
			} else {
				_items.push_back(CachedItem(item->id, item->date.date(), y));
				_items.back().link = cachedLink(item);
				y += _items.back().link->countHeight(_linksWidth);
			}
			++in;
		}
		if (_items.size() != in) {
			_items.resize(in);
		}
		if (_height != _addToY + y + st::linksSearchMargin.top()) {
			_height = _addToY + y + st::linksSearchMargin.top();
			if (!fromResize) {
				resize(width(), _minHeight > _height ? _minHeight : _height);
			}
		}
		dragActionUpdate(QCursor::pos());
		update();
	} else if (_type != OverviewPhotos && _type != OverviewAudioDocuments) {
		History::MediaOverview &o(_hist->overview[_type]);
		int32 l = o.size();
		_items.reserve(2 * l); // day items

		int32 y = 0, in = 0;
		int32 w = _width - st::msgMargin.left() - st::msgMargin.right();
		bool allGood = true;
		QDate prevDate;
		for (int32 i = 0; i < l; ++i) {
			MsgId msgid = o.at(l - i - 1);
			if (allGood) {
				if (_items.size() > in && _items.at(in).msgid == msgid) {
					prevDate = _items.at(in).date;
					if (fromResize) {
						HistoryItem *item = App::histItemById(_channel, msgid);
						HistoryMedia *media = item ? item->getMedia(true) : 0;
						if (media) {
							y += media->countHeight(item, w) + st::msgMargin.top() + st::msgMargin.bottom(); // item height
						}
						_items[in].y = y;
					} else {
						y = _items.at(in).y;
					}
					++in;
					continue;
				}
				if (_items.size() > in + 1 && !_items.at(in).msgid && _items.at(in + 1).msgid == msgid) { // day item
					if (fromResize) {
						y += st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom(); // day item height
						_items[in].y = y;
					}
					++in;
					prevDate = _items.at(in).date;
					if (fromResize) {
						HistoryItem *item = App::histItemById(_channel, msgid);
						HistoryMedia *media = item ? item->getMedia(true) : 0;
						if (media) {
							y += media->countHeight(item, w) + st::msgMargin.top() + st::msgMargin.bottom(); // item height
						}
						_items[in].y = y;
					} else {
						y = _items.at(in).y;
					}
					++in;
					continue;
				}
				allGood = false;
			}
			HistoryItem *item = App::histItemById(_channel, msgid);
			HistoryMedia *media = item ? item->getMedia(true) : 0;
			if (!media) continue;

			QDate date = item->date.date();
			if (in > 0) {
				if (date != prevDate) { // add day item
					y += st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom(); // day item height
					if (_items.size() > in) {
						_items[in].msgid = 0;
						_items[in].date = prevDate;
						_items[in].y = y;
					} else {
						_items.push_back(CachedItem(0, prevDate, y));
					}
					++in;
					prevDate = date;
				}
			} else {
				prevDate = date;
			}
			media->initDimensions(item);
			y += media->countHeight(item, w) + st::msgMargin.top() + st::msgMargin.bottom(); // item height
			if (_items.size() > in) {
				_items[in].msgid = msgid;
				_items[in].date = date;
				_items[in].y = y;
			} else {
				_items.push_back(CachedItem(msgid, date, y));
			}
			++in;
		}
		if (!_items.isEmpty()) {
			y += st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom(); // day item height
			if (_items.size() > in) {
				_items[in].msgid = 0;
				_items[in].date = prevDate;
				_items[in].y = y;
			} else {
				_items.push_back(CachedItem(0, prevDate, y));
			}
			_items.resize(++in);
		}
		if (_height != y) {
			_height = y;
			if (!fromResize) {
				_addToY = (_height < _minHeight) ? (_minHeight - _height) : 0;
				resize(width(), _minHeight > _height ? _minHeight : _height);
			}
		}
	}

	fixItemIndex(_dragSelFromIndex, _dragSelFrom);
	fixItemIndex(_dragSelToIndex, _dragSelTo);
	fixItemIndex(_mousedItemIndex, _mousedItem);
	fixItemIndex(_dragItemIndex, _dragItem);

	if (!fromResize) {
		resizeEvent(0);
		if (_height != oldHeight && _type != OverviewLinks) {
			_overview->scrollBy(_height - oldHeight);
		}
	}
}

void OverviewInner::changingMsgId(HistoryItem *row, MsgId newId) {
	if (_dragSelFrom == row->id) _dragSelFrom = newId;
	if (_dragSelTo == row->id) _dragSelTo = newId;
	if (_mousedItem == row->id) _mousedItem = newId;
	if (_dragItem == row->id) _dragItem = newId;
	for (SelectedItems::iterator i = _selected.begin(), e = _selected.end(); i != e; ++i) {
		if (i.key() == row->id) {
			uint32 sel = i.value();
			_selected.erase(i);
			_selected.insert(newId, sel);
			break;
		}
	}
	if (_links.contains(row->id) && row->id != newId) {
		if (_links.contains(newId)) {
			for (CachedItems::iterator i = _items.begin(), e = _items.end(); i != e; ++i) {
				if (i->msgid == newId && i->link) {
					i->link = _links[row->id];
					break;
				}
			}
		}
		_links[newId] = _links[row->id];
		delete _links[row->id];
		_links.remove(row->id);
	}
	for (CachedItems::iterator i = _items.begin(), e = _items.end(); i != e; ++i) {
		if (i->msgid == row->id) {
			i->msgid = newId;
			break;
		}
	}
}

void OverviewInner::itemRemoved(HistoryItem *item) {
	if (_dragItem == item->id) {
		dragActionCancel();
	}

	SelectedItems::iterator i = _selected.find(item->id);
	if (i != _selected.cend()) {
		_selected.erase(i);
		_overview->updateTopBarSelection();
	}

	onUpdateSelected();

	if (_dragSelFrom == item->id) {
		_dragSelFrom = 0;
		_dragSelFromIndex = -1;
	}
	if (_dragSelTo == item->id) {
		_dragSelTo = 0;
		_dragSelToIndex = -1;
	}
	updateDragSelection(_dragSelFrom, _dragSelFromIndex, _dragSelTo, _dragSelToIndex, _dragSelecting);

	update();
}

void OverviewInner::itemResized(HistoryItem *item, bool scrollToIt) {
	if (_type != OverviewPhotos && _type != OverviewAudioDocuments && _type != OverviewLinks) {
		HistoryMedia *media = item ? item->getMedia(true) : 0;
		if (!media) return;

		for (int32 i = 0, l = _items.size(); i < l; ++i) {
			if (_items[i].msgid == item->id) {
				int32 from = 0;
				if (i > 0) from = _items[i - 1].y;

				int32 oldh = _items[i].y - from;
				int32 w = _width - st::msgMargin.left() - st::msgMargin.right();
				int32 newh = media->countHeight(item, w) + st::msgMargin.top() + st::msgMargin.bottom(); // item height
				if (oldh != newh) {
					newh -= oldh;
					for (int32 j = i; j < l; ++j) {
						_items[j].y += newh;
					}
					_height = _items[l - 1].y;
					_addToY = (_height < _minHeight) ? (_minHeight - _height) : 0;
					resize(width(), _minHeight > _height ? _minHeight : _height);
					if (scrollToIt) {
						if (_addToY + _height - from > _scroll->scrollTop() + _scroll->height()) {
							_scroll->scrollToY(_addToY + _height - from - _scroll->height());
						}
						if (_addToY + _height - _items[i].y < _scroll->scrollTop()) {
							_scroll->scrollToY(_addToY + _height - _items[i].y);
						}
					}
					update();
				}
				break;
			}
		}
	}
}

void OverviewInner::msgUpdated(const HistoryItem *msg) {
	if (!msg || _hist != msg->history()) return;
	MsgId msgid = msg->id;
	if (_hist->overviewIds[_type].constFind(msgid) != _hist->overviewIds[_type].cend()) {
		if (_type == OverviewPhotos) {
			int32 index = _hist->overview[_type].indexOf(msgid);
			if (index >= 0) {
				float64 w = (float64(width() - st::overviewPhotoSkip) / _photosInRow);
				int32 vsize = (_vsize + st::overviewPhotoSkip);
				int32 row = (_photosToAdd + index) / _photosInRow, col = (_photosToAdd + index) % _photosInRow;
				update(int32(col * w), _addToY + int32(row * vsize), qCeil(w), vsize);
			}
		} else if (_type == OverviewAudioDocuments) {
			int32 index = _hist->overview[_type].indexOf(msgid);
			if (index >= 0) {
				update(_audioLeft, _addToY + int32(index * _audioHeight), _audioWidth, _audioHeight);
			}
		} else if (_type == OverviewLinks) {
			for (int32 i = 0, l = _items.size(); i != l; ++i) {
				if (_items[i].msgid == msgid) {
					update(_linksLeft, _addToY + _items[i].y, _linksWidth, itemHeight(msgid, i));
					break;
				}
			}
		} else {
			for (int32 i = 0, l = _items.size(); i != l; ++i) {
				if (_items[i].msgid == msgid) {
					update(0, _addToY + _height - _items[i].y, _width, itemHeight(msgid, i));
					break;
				}
			}
		}
	}
}

void OverviewInner::showAll(bool recountHeights) {
	int32 newHeight = height();
	if (_type == OverviewPhotos) {
		_photosInRow = int32(width() - st::overviewPhotoSkip) / int32(st::overviewPhotoMinSize + st::overviewPhotoSkip);
		_vsize = (int32(width() - st::overviewPhotoSkip) / _photosInRow) - st::overviewPhotoSkip;
		int32 count = _hist->overview[_type].size(), fullCount = _hist->overviewCount[_type];
		if (fullCount > 0) {
			int32 cnt = count - (fullCount % _photosInRow);
			if (cnt < 0) cnt += _photosInRow;
			_photosToAdd = (_photosInRow - (cnt % _photosInRow)) % _photosInRow;
		} else {
			_photosToAdd = 0;
		}
		int32 rows = ((_photosToAdd + count) / _photosInRow) + (((_photosToAdd + count) % _photosInRow) ? 1 : 0);
		newHeight = _height = (_vsize + st::overviewPhotoSkip) * rows + st::overviewPhotoSkip;
		_addToY = (_height < _minHeight) ? (_minHeight - _height) : 0;
	} else if (_type == OverviewAudioDocuments) {
		int32 count = _hist->overview[_type].size(), fullCount = _hist->overviewCount[_type];
		newHeight = _height = count * _audioHeight + 2 * st::playlistPadding;
		_addToY = st::playlistPadding;
	} else if (_type == OverviewLinks) {
		if (recountHeights) { // recount heights because of texts
			mediaOverviewUpdated(true);
		}
		newHeight = _height;
		_addToY = st::linksSearchMargin.top() + _search.height() + st::linksSearchMargin.bottom();
	} else {
		if (recountHeights && _type == OverviewVideos) { // recount heights because of captions
			mediaOverviewUpdated(true);
		}
		newHeight = _height;
		_addToY = (_height < _minHeight) ? (_minHeight - _height) : 0;
	}

	if (newHeight < _minHeight) {
		newHeight = _minHeight;
	}
	if (height() != newHeight) {
		resize(width(), newHeight);
	}
}

OverviewInner::~OverviewInner() {
	_dragAction = NoDrag;
	for (CachedLinks::const_iterator i = _links.cbegin(), e = _links.cend(); i != e; ++i) {
		delete i.value();
	}
	_links.clear();
}

OverviewWidget::OverviewWidget(QWidget *parent, const PeerData *peer, MediaOverviewType type) : QWidget(parent)
, _scroll(this, st::historyScroll, false)
, _inner(this, &_scroll, peer, type)
, _noDropResizeIndex(false)
, _showing(false)
, _scrollSetAfterShow(0)
, _scrollDelta(0)
, _selCount(0) {
	_scroll.setFocusPolicy(Qt::NoFocus);
	_scroll.setWidget(&_inner);
	_scroll.move(0, 0);
	_inner.move(0, 0);

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
		needToPreload = (type() == OverviewLinks) ? (_scroll.scrollTop() + preloadThreshold > _scroll.scrollTopMax()) : (_scroll.scrollTop() < preloadThreshold);
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
	int32 st = _scroll.scrollTop();
	_scroll.resize(size());
	int32 newScrollTop = _inner.resizeToWidth(width(), st, height());
	if (int32 addToY = App::main() ? App::main()->contentScrollAddToY() : 0) {
		newScrollTop += addToY;
	}
	if (newScrollTop != _scroll.scrollTop()) {
		_noDropResizeIndex = true;
		_scroll.scrollToY(newScrollTop);
		_noDropResizeIndex = false;
	}
}

void OverviewWidget::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);
	if (animating() && _showing) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
		return;
	}

	QRect r(e->rect());
	if (type() == OverviewPhotos || type() == OverviewAudioDocuments || type() == OverviewLinks) {
		p.fillRect(r, st::white->b);
	} else {
		bool hasTopBar = !App::main()->topBar()->isHidden(), hasPlayer = !App::main()->player()->isHidden();
		QRect fill(0, 0, width(), App::main()->height());
		int fromy = (hasTopBar ? (-st::topBarHeight) : 0) + (hasPlayer ? (-st::playerHeight) : 0), x = 0, y = 0;
		QPixmap cached = App::main()->cachedBackground(fill, x, y);
		if (cached.isNull()) {
			const QPixmap &pix(*cChatBackground());
			if (cTileBackground()) {
				int left = r.left(), top = r.top(), right = r.left() + r.width(), bottom = r.top() + r.height();
				float64 w = pix.width() / cRetinaFactor(), h = pix.height() / cRetinaFactor();
				int sx = qFloor(left / w), sy = qFloor((top - fromy) / h), cx = qCeil(right / w), cy = qCeil((bottom - fromy) / h);
				for (int i = sx; i < cx; ++i) {
					for (int j = sy; j < cy; ++j) {
						p.drawPixmap(QPointF(i * w, fromy + j * h), pix);
					}
				}
			} else {
				bool smooth = p.renderHints().testFlag(QPainter::SmoothPixmapTransform);
				p.setRenderHint(QPainter::SmoothPixmapTransform);

				QRect to, from;
				App::main()->backgroundParams(fill, to, from);
				to.moveTop(to.top() + fromy);
				p.drawPixmap(to, pix, from);

				if (!smooth) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
			}
		} else {
			p.drawPixmap(x, fromy + y, cached);
		}
	}
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
	_scroll.scrollToY((type() == OverviewLinks) ? 0 : _scroll.scrollTopMax());
}

void OverviewWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (animating() && _showing) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimTopBarCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animTopBarCache);
	} else {
		p.setOpacity(st::topBarBackAlpha + (1 - st::topBarBackAlpha) * over);
		p.drawPixmap(QPoint(st::topBarBackPadding.left(), (st::topBarHeight - st::topBarBackImg.pxHeight()) / 2), App::sprite(), st::topBarBackImg);
		p.setFont(st::topBarBackFont->f);
		p.setPen(st::topBarBackColor->p);
		p.drawText(st::topBarBackPadding.left() + st::topBarBackImg.pxWidth() + st::topBarBackPadding.right(), (st::topBarHeight - st::topBarBackFont->height) / 2 + st::topBarBackFont->ascent, _header);
	}
}

void OverviewWidget::topBarShadowParams(int32 &x, float64 &o) {
	if (animating() && a_coord.current() >= 0) {
		x = a_coord.current();
		o = a_alpha.current();
	}
}

void OverviewWidget::topBarClick() {
	App::main()->showBackFromStack();
}

PeerData *OverviewWidget::peer() const {
	return _inner.peer();
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
	case OverviewDocuments: _header = lang(lng_profile_files_header); break;
	case OverviewAudios: _header = lang(lng_profile_audios_header); break;
	case OverviewAudioDocuments: _header = lang(lng_profile_audio_files_header); break;
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
	if (App::wnd() && !App::wnd()->layerShown()) {
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
				return snap(top - int(_scroll.height() - (st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom())) / 2, 0, _scroll.scrollTopMax());
			}
		}
	} else if (type() == OverviewLinks) {
		return 0;
	}
	return _scroll.scrollTopMax();
}

void OverviewWidget::fastShow(bool back, int32 lastScrollTop) {
	stopGif();
	resizeEvent(0);
	_scrollSetAfterShow = (lastScrollTop < 0 ? countBestScroll() : lastScrollTop);
	show();
	_inner.activate();
	doneShow();
}

void OverviewWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back, int32 lastScrollTop) {
	stopGif();
	_bgAnimCache = bgAnimCache;
	_bgAnimTopBarCache = bgAnimTopBarCache;
	resizeEvent(0);
	_scroll.scrollToY(lastScrollTop < 0 ? countBestScroll() : lastScrollTop);
	_animCache = myGrab(this, rect());
	App::main()->topBar()->stopAnim();
	_animTopBarCache = myGrab(App::main()->topBar(), QRect(0, 0, width(), st::topBarHeight));
	App::main()->topBar()->startAnim();
	_scrollSetAfterShow = _scroll.scrollTop();
	_scroll.hide();
	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);
	anim::start(this);
	_showing = true;
	show();
	_inner.activate();
	App::main()->topBar()->update();
}

bool OverviewWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		res = _showing = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();
		_bgAnimCache = _animCache = _animTopBarCache = _bgAnimTopBarCache = QPixmap();
		App::main()->topBar()->stopAnim();
		doneShow();
	} else {
		a_bgCoord.update(dt1, st::introHideFunc);
		a_bgAlpha.update(dt1, st::introAlphaHideFunc);
		a_coord.update(dt2, st::introShowFunc);
		a_alpha.update(dt2, st::introAlphaShowFunc);
	}
	update();
	App::main()->topBar()->update();
	return res;
}

void OverviewWidget::doneShow() {
	_scroll.show();
	_scroll.scrollToY(_scrollSetAfterShow);
	activate();
	onScroll();
}

void OverviewWidget::mediaOverviewUpdated(PeerData *p, MediaOverviewType t) {
	if (peer() == p && t == type()) {
		_inner.mediaOverviewUpdated();
		onScroll();
		updateTopBarSelection();
	}
}

void OverviewWidget::changingMsgId(HistoryItem *row, MsgId newId) {
	if (peer() == row->history()->peer) {
		_inner.changingMsgId(row, newId);
	}
}

void OverviewWidget::msgUpdated(PeerId p, const HistoryItem *msg) {
	if (peer()->id == p) {
		_inner.msgUpdated(msg);
	}
}

void OverviewWidget::itemRemoved(HistoryItem *row) {
	_inner.itemRemoved(row);
}

void OverviewWidget::itemResized(HistoryItem *row, bool scrollToIt) {
	if (!row || row->history()->peer == peer()) {
		_inner.itemResized(row, scrollToIt);
	}
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
//			_scroll.scrollToY(snap(top - int(_scroll.height() - (st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom())) / 2, 0, _scroll.scrollTopMax()));
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

	QVector<MTPint> ids;
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		if (i.value()->id > 0) {
			ids.push_back(MTP_int(i.value()->id));
		}
	}

	onClearSelected();
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		i.value()->destroy();
	}
	if (App::main() && App::main()->peer() == peer()) {
		App::main()->itemResized(0);
	}
	App::wnd()->hideLayer();

	if (!ids.isEmpty()) {
		App::main()->deleteMessages(peer(), ids);
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

	if (App::main() && App::main()->peer() == peer()) {
		App::main()->itemResized(0);
	}
	App::wnd()->hideLayer();

	if (wasOnServer) {
		App::main()->deleteMessages(h->peer, toDelete);
	}
}

void OverviewWidget::onClearSelected() {
	_inner.clearSelectedItems();
}
