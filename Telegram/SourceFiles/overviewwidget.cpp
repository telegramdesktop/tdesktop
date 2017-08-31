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
#include "styles/style_overview.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/photo_crop_box.h"
#include "core/file_utilities.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/tooltip.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "window/top_bar_widget.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "overviewwidget.h"
#include "application.h"
#include "overview/overview_layout.h"
#include "history/history_message.h"
#include "history/history_media_types.h"
#include "history/history_service_layout.h"
#include "media/media_audio.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "storage/file_download.h"
#include "ui/widgets/dropdown_menu.h"

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

OverviewInner::OverviewInner(OverviewWidget *overview, Ui::ScrollArea *scroll, PeerData *peer, MediaOverviewType type) : TWidget(nullptr)
, _overview(overview)
, _scroll(scroll)
, _peer(peer->migrateTo() ? peer->migrateTo() : peer)
, _type(type)
, _reversed(_type != OverviewFiles && _type != OverviewLinks)
, _history(App::history(_peer))
, _migrated(_history->migrateFrom())
, _channel(peerToChannel(_peer->id))
, _rowWidth(st::msgMinWidth)
, _search(this, st::overviewFilter, langFactory(lng_dlg_filter))
, _cancelSearch(this, st::dialogsCancelSearch)
, _itemsToBeLoaded(LinksOverviewPerPage * 2)
, _width(st::windowMinWidth) {
	subscribe(Auth().downloader().taskFinished(), [this] { update(); });
	subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
		itemRemoved(item);
	});

	resize(_width, st::windowMinHeight);

	App::contextItem(0);

	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));

	mediaOverviewUpdated();
	setMouseTracking(true);

	connect(_cancelSearch, SIGNAL(clicked()), this, SLOT(onCancelSearch()));
	connect(_search, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(_search, SIGNAL(changed()), this, SLOT(onSearchUpdate()));

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			invalidateCache();
		}
	});
	subscribe(App::wnd()->dragFinished(), [this] {
		dragActionUpdate(QCursor::pos());
	});
	subscribe(Auth().messageIdChanging, [this](std::pair<HistoryItem*, MsgId> update) {
		changingMsgId(update.first, update.second);
	});

	if (_type == OverviewLinks || _type == OverviewFiles) {
		_search->show();
	} else {
		_search->hide();
	}
}

void OverviewInner::invalidateCache() {
	for_const (auto item, _layoutItems) {
		item->invalidateCache();
	}
	for_const (auto item, _layoutDates) {
		item->invalidateCache();
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
	const auto nowTime = getms();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > FingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > FingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == Ui::TouchScrollState::Auto) {
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
	return (_migrated && _history->overviewLoaded(_type)) ? _migrated->overview(_type).size() : 0;
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
	if (!_search->text().isEmpty()) {
		if (type == SearchFromStart) {
			SearchQueries::iterator i = _searchQueries.find(req);
			if (i != _searchQueries.cend()) {
				_searchCache[i.value()] = result;
				_searchQueries.erase(i);
			}
		}
	}

	if (_searchRequest == req) {
		const QVector<MTPMessage> *messages = nullptr;
		switch (result.type()) {
		case mtpc_messages_messages: {
			auto &d = result.c_messages_messages();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			messages = &d.vmessages.v;
			_searchedCount = messages->size();
		} break;

		case mtpc_messages_messagesSlice: {
			auto &d = result.c_messages_messagesSlice();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			messages = &d.vmessages.v;
			_searchedCount = d.vcount.v;
		} break;

		case mtpc_messages_channelMessages: {
			auto &d = result.c_messages_channelMessages();
			if (_peer && _peer->isChannel()) {
				_peer->asChannel()->ptsReceived(d.vpts.v);
			} else {
				LOG(("API Error: received messages.channelMessages when no channel was passed! (OverviewInner::searchReceived)"));
			}
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			messages = &d.vmessages.v;
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
			for (auto i = messages->cbegin(), e = messages->cend(); i != e; ++i) {
				auto item = App::histories().addNewMessage(*i, NewMessageExisting);
				auto msgId = item ? item->id : idFromMessage(*i);
				if (migratedSearch) {
					if (item) _searchResults.push_front(-item->id);
					_lastSearchMigratedId = msgId;
				} else {
					if (item) _searchResults.push_front(item->id);
					_lastSearchId = msgId;
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
	if (MTP::isDefaultHandledError(error)) return false;

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
	while (index >= 0 && index < _items.size() && !_items.at(index)->toMediaItem()) {
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
			float64 w = (float64(_width - st::overviewPhotoSkip) / _photosInRow);
			int32 vsize = (_rowWidth + st::overviewPhotoSkip);
			int32 row = itemIndex / _photosInRow, col = itemIndex % _photosInRow;
			update(int32(col * w), _marginTop + int32(row * vsize), qCeil(w), vsize);
		} else {
			int32 top = _items.at(itemIndex)->Get<Overview::Layout::Info>()->top;
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
		_touchScrollState = Ui::TouchScrollState::Manual;
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
		if (_touchScrollState == Ui::TouchScrollState::Auto) {
			_touchScrollState = Ui::TouchScrollState::Acceleration;
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
			if (_touchScrollState == Ui::TouchScrollState::Manual) {
				touchScrollUpdated(_touchPos);
			} else if (_touchScrollState == Ui::TouchScrollState::Acceleration) {
				touchUpdateSpeed();
				_touchAccelerationTime = getms();
				if (_touchSpeed.isNull()) {
					_touchScrollState = Ui::TouchScrollState::Manual;
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
			if (_touchScrollState == Ui::TouchScrollState::Manual) {
				_touchScrollState = Ui::TouchScrollState::Auto;
				_touchPrevPosValid = false;
				_touchScrollTimer.start(15);
				_touchTime = getms();
			} else if (_touchScrollState == Ui::TouchScrollState::Auto) {
				_touchScrollState = Ui::TouchScrollState::Manual;
				_touchScroll = false;
				touchResetSpeed();
			} else if (_touchScrollState == Ui::TouchScrollState::Acceleration) {
				_touchScrollState = Ui::TouchScrollState::Auto;
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

	ClickHandler::pressed();

	_dragAction = NoDrag;
	_dragItem = _mousedItem;
	_dragItemIndex = _mousedItemIndex;
	_dragStartPos = mapMouseToItem(mapFromGlobal(screenPos), _dragItem, _dragItemIndex);
	_dragWasInactive = App::wnd()->wasInactivePress();
	if (_dragWasInactive) App::wnd()->setInactivePress(false);
	if (ClickHandler::getPressed() && _selected.isEmpty()) {
		_dragAction = PrepareDrag;
	} else if (!_selected.isEmpty()) {
		if (_selected.cbegin().value() == FullSelection) {
			if (_selected.constFind(_dragItem) != _selected.cend() && ClickHandler::getPressed()) {
				_dragAction = PrepareDrag; // start items drag
			} else {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}
	if (_dragAction == NoDrag && _dragItem) {
		if (!_dragWasInactive) {
			_dragAction = PrepareSelect;
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
	dragActionUpdate(screenPos);

	ClickHandlerPtr activated = ClickHandler::unpressed();
	if (_dragAction == Dragging || _selMode) {
		activated.clear();
	}
	if (!ClickHandler::getActive() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	if (activated) {
		dragActionCancel();
		App::activateClickHandler(activated, button);
		return;
	}
	if (_dragAction == PrepareSelect && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
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
	} else if (_dragAction == PrepareDrag && !_dragWasInactive && button != Qt::RightButton) {
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
			auto sel = _selected.cbegin().value();
			if (sel != FullSelection && sel.from == sel.to) {
				_selected.clear();
				App::wnd()->setInnerFocus();
			}
		}
	}
	_dragAction = NoDrag;
	_overview->noSelectingScroll();
	_overview->updateTopBarSelection();
}

void OverviewInner::performDrag() {
	if (_dragAction != Dragging) return;

	bool uponSelected = false;
	if (_dragItem) {
		if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
			uponSelected = _selected.contains(_dragItem);
		} else {
			uponSelected = false;
		}
	}
	ClickHandlerPtr pressedHandler = ClickHandler::getPressed();
	QString sel;
	QList<QUrl> urls;
	bool forwardSelected = false;
	if (uponSelected) {
		if (!Adaptive::OneColumn()) {
			auto selectedState = getSelectionState();
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				forwardSelected = true;
			}
		}
	} else if (pressedHandler) {
		sel = pressedHandler->dragText();
		//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
		//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
		//}
	}
	if (!sel.isEmpty() || forwardSelected) {
		updateDragSelection(0, -1, 0, -1, false);
		_overview->noSelectingScroll();

		auto mimeData = std::make_unique<QMimeData>();
		if (!sel.isEmpty()) mimeData->setText(sel);
		if (!urls.isEmpty()) mimeData->setUrls(urls);
		if (forwardSelected) {
			mimeData->setData(qsl("application/x-td-forward-selected"), "1");
		}

		// This call enters event loop and can destroy any QObject.
		App::wnd()->launchDrag(std::move(mimeData));
		return;
	} else {
		QString forwardMimeType;
		HistoryMedia *pressedMedia = nullptr;
		if (auto pressedLnkItem = App::pressedLinkItem()) {
			if ((pressedMedia = pressedLnkItem->getMedia())) {
				if (forwardMimeType.isEmpty() && pressedMedia->dragItemByHandler(pressedHandler)) {
					forwardMimeType = qsl("application/x-td-forward-pressed-link");
				}
			}
		}
		if (!forwardMimeType.isEmpty()) {
			auto mimeData = std::make_unique<QMimeData>();
			mimeData->setData(qsl("application/x-td-forward-pressed-link"), "1");
			if (auto document = (pressedMedia ? pressedMedia->getDocument() : nullptr)) {
				auto filepath = document->filepath(DocumentData::FilePathResolveChecked);
				if (!filepath.isEmpty()) {
					QList<QUrl> urls;
					urls.push_back(QUrl::fromLocalFile(filepath));
					mimeData->setUrls(urls);
				}
			}

			// This call enters event loop and can destroy any QObject.
			App::wnd()->launchDrag(std::move(mimeData));
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
		int32 row = itemIndex / _photosInRow, col = itemIndex % _photosInRow;
		float64 w = (_width - st::overviewPhotoSkip) / float64(_photosInRow);
		p.setX(p.x() - int32(col * w) - st::overviewPhotoSkip);
		p.setY(p.y() - _marginTop - row * (_rowWidth + st::overviewPhotoSkip) - st::overviewPhotoSkip);
	} else {
		int32 top = _items.at(itemIndex)->Get<Overview::Layout::Info>()->top;
		if (_reversed) top = _height - top;
		p.setY(p.y() - _marginTop - top);
	}
	return p;
}

void OverviewInner::activate() {
	if (_type == OverviewLinks || _type == OverviewFiles) {
		_search->setFocus();
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

	App::clearMousedItems();
}

int32 OverviewInner::itemTop(const FullMsgId &msgId) const {
	if (_type == OverviewMusicFiles) {
		int32 itemIndex = -1;
		fixItemIndex(itemIndex, (msgId.channel == _channel) ? msgId.msg : ((_migrated && msgId.channel == _migrated->channelId()) ? -msgId.msg : 0));
		if (itemIndex >= 0) {
			int32 top = _items.at(itemIndex)->Get<Overview::Layout::Info>()->top;
			if (_reversed) top = _height - top;
			return _marginTop + top;
		}
	}
	return -1;
}

void OverviewInner::preloadMore() {
	if (_inSearch) {
		if (!_searchRequest) {
			auto filter = (_type == OverviewLinks) ? MTP_inputMessagesFilterUrl() : MTP_inputMessagesFilterDocument();
			if (!_searchFull) {
				_searchRequest = MTP::send(MTPmessages_Search(MTP_flags(0), _history->peer->input, MTP_string(_searchQuery), MTP_inputUserEmpty(), filter, MTP_int(0), MTP_int(0), MTP_int(_lastSearchId), MTP_int(0), MTP_int(SearchPerPage), MTP_int(0), MTP_int(0)), rpcDone(&OverviewInner::searchReceived, _lastSearchId ? SearchFromOffset : SearchFromStart), rpcFail(&OverviewInner::searchFailed, _lastSearchId ? SearchFromOffset : SearchFromStart));
				if (!_lastSearchId) {
					_searchQueries.insert(_searchRequest, _searchQuery);
				}
			} else if (_migrated && !_searchFullMigrated) {
				_searchRequest = MTP::send(MTPmessages_Search(MTP_flags(0), _migrated->peer->input, MTP_string(_searchQuery), MTP_inputUserEmpty(), filter, MTP_int(0), MTP_int(0), MTP_int(_lastSearchMigratedId), MTP_int(0), MTP_int(SearchPerPage), MTP_int(0), MTP_int(0)), rpcDone(&OverviewInner::searchReceived, _lastSearchMigratedId ? SearchMigratedFromOffset : SearchMigratedFromStart), rpcFail(&OverviewInner::searchFailed, _lastSearchMigratedId ? SearchMigratedFromOffset : SearchMigratedFromStart));
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
	if (_itemsToBeLoaded >= migratedIndexSkip() + _history->overview(_type).size()) return false;
	_itemsToBeLoaded += LinksOverviewPerPage;
	mediaOverviewUpdated();
	return true;
}

TextSelection OverviewInner::itemSelectedValue(int32 index) const {
	int32 selfrom = -1, selto = -1;
	if (_dragSelFromIndex >= 0 && _dragSelToIndex >= 0) {
		selfrom = _dragSelToIndex;
		selto = _dragSelFromIndex;
	}
	if (_items.at(index)->toMediaItem()) { // draw item
		if (index >= _dragSelToIndex && index <= _dragSelFromIndex && _dragSelToIndex >= 0) {
			return (_dragSelecting && _items.at(index)->msgId() > 0) ? FullSelection : TextSelection{ 0, 0 };
		} else if (!_selected.isEmpty()) {
			SelectedItems::const_iterator j = _selected.constFind(complexMsgId(_items.at(index)->getItem()));
			if (j != _selected.cend()) {
				return j.value();
			}
		}
	}
	return { 0, 0 };
}

void OverviewInner::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);

	QRect r(e->rect());
	bool trivial = (r == rect());
	if (!trivial) {
		p.setClipRect(r);
	}
	auto ms = getms();
	Overview::Layout::PaintContext context(ms, _selMode);

	if (_history->overview(_type).isEmpty() && (!_migrated || !_history->overviewLoaded(_type) || _migrated->overview(_type).isEmpty())) {
		HistoryLayout::paintEmpty(p, _width, height());
		return;
	} else if (_inSearch && _searchResults.isEmpty() && _searchFull && (!_migrated || _searchFullMigrated) && !_searchTimer.isActive()) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(_rowsLeft, _marginTop, _rowWidth, _marginTop), lang(lng_search_no_results), style::al_center);
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
		int32 count = _items.size(), rowsCount = count / _photosInRow + ((count % _photosInRow) ? 1 : 0);
		int32 rowFrom = floorclamp(r.y() - _marginTop - st::overviewPhotoSkip, _rowWidth + st::overviewPhotoSkip, 0, rowsCount);
		int32 rowTo = ceilclamp(r.y() + r.height() - _marginTop - st::overviewPhotoSkip, _rowWidth + st::overviewPhotoSkip, 0, rowsCount);
		float64 w = float64(_width - st::overviewPhotoSkip) / _photosInRow;
		for (int32 row = rowFrom; row < rowTo; ++row) {
			if (row * _photosInRow >= count) break;
			for (int32 col = 0; col < _photosInRow; ++col) {
				int32 i = row * _photosInRow + col;
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
			int32 nextItemTop = (j + 1 == l) ? (_reversed ? 0 : _height) : _items.at(nexti)->Get<Overview::Layout::Info>()->top;
			if (_reversed) nextItemTop = _height - nextItemTop;
			if (_marginTop + nextItemTop > r.top()) {
				auto info = _items.at(i)->Get<Overview::Layout::Info>();
				int32 curY = info->top;
				if (_reversed) curY = _height - curY;
				if (_marginTop + curY >= r.y() + r.height()) break;

				context.isAfterDate = (j > 0) ? !_items.at(j - 1)->toMediaItem() : false;
				p.translate(0, curY - y);
				_items.at(i)->paint(p, r.translated(-_rowsLeft, -_marginTop - curY), itemSelectedValue(i), &context);
				y = curY;
			}
		}
	}
}

void OverviewInner::mouseMoveEvent(QMouseEvent *e) {
	if (!(e->buttons() & (Qt::LeftButton | Qt::MiddleButton)) && _dragAction != NoDrag) {
		mouseReleaseEvent(e);
	}
	dragActionUpdate(e->globalPos());
}

void OverviewInner::onUpdateSelected() {
	if (isHidden()) return;

	QPoint mousePos(mapFromGlobal(_dragPos));
	QPoint m(_overview->clampMousePosition(mousePos));

	ClickHandlerPtr lnk;
	ClickHandlerHost *lnkhost = nullptr;
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

		int32 count = _items.size(), i = row * _photosInRow + col;
		if (i < 0) {
			i = 0;
			upon = false;
		}
		if (i >= count) {
			i = count - 1;
			upon = false;
		}
		if (i >= 0) {
			if (auto media = _items.at(i)->toMediaItem()) {
				item = media->getItem();
				index = i;
				if (upon) {
					media->getState(lnk, cursorState, m - QPoint(col * w + st::overviewPhotoSkip, _marginTop + row * vsize + st::overviewPhotoSkip));
					lnkhost = media;
				}
			}
		}
	} else {
		for (int32 j = 0, l = _items.size(); j < l; ++j) {
			bool lastItem = (j + 1 == l);
			int32 i = _reversed ? (l - j - 1) : j, nexti = _reversed ? (i - 1) : (i + 1);
			int32 nextItemTop = lastItem ? (_reversed ? 0 : _height) : _items.at(nexti)->Get<Overview::Layout::Info>()->top;
			if (_reversed) nextItemTop = _height - nextItemTop;
			if (_marginTop + nextItemTop > m.y() || lastItem) {
				int32 top = _items.at(i)->Get<Overview::Layout::Info>()->top;
				if (_reversed) top = _height - top;
				if (!_items.at(i)->toMediaItem()) { // day item
					int32 h = _items.at(i)->height();
					bool beforeItem = (_marginTop + top + h / 2) >= m.y();
					if (_reversed) beforeItem = !beforeItem;
					if (i > 0 && (beforeItem || i == _items.size() - 1)) {
						--i;
						if (!_items.at(i)->toMediaItem()) break; // wtf
						top = _items.at(i)->Get<Overview::Layout::Info>()->top;
					} else if (i < _items.size() - 1 && (!beforeItem || !i)) {
						++i;
						if (!_items.at(i)->toMediaItem()) break; // wtf
						top = _items.at(i)->Get<Overview::Layout::Info>()->top;
					} else {
						break; // wtf
					}
					if (_reversed) top = _height - top;
					j = _reversed ? (l - i - 1) : i;
				}

				if (auto media = _items.at(i)->toMediaItem()) {
					item = media->getItem();
					index = i;
					media->getState(lnk, cursorState, m - QPoint(_rowsLeft, _marginTop + top));
					lnkhost = media;
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
	bool lnkChanged = ClickHandler::setActive(lnk, lnkhost);
	if (lnkChanged) {
		Ui::Tooltip::Hide();
	}
	App::mousedItem(item);
	if (_mousedItem != oldMousedItem) {
		Ui::Tooltip::Hide();
		if (oldMousedItem) repaintItem(oldMousedItem, oldMousedItemIndex);
		if (item) repaintItem(item);
	}
	if (_cursorState == HistoryInDateCursorState && cursorState != HistoryInDateCursorState) {
		Ui::Tooltip::Hide();
	}
	if (cursorState != _cursorState) {
		_cursorState = cursorState;
	}
	if (lnk || cursorState == HistoryInDateCursorState) {
		Ui::Tooltip::Show(1000, this);
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
				InvokeQueued(this, [this] { performDrag(); });
			} else if (_dragAction == PrepareSelect) {
				_dragAction = Selecting;
			}
		}
		if (_dragAction == Selecting) {
			bool canSelectMany = (_peer != 0);
			if (_mousedItem == _dragItem && lnk && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
				bool afterSymbol = false, uponSymbol = false;
				uint16 second = 0;
				_selected[_dragItem] = { 0, 0 };
				updateDragSelection(0, -1, 0, -1, false);
			} else if (canSelectMany) {
				bool selectingDown = (_reversed ? (_mousedItemIndex < _dragItemIndex) : (_mousedItemIndex > _dragItemIndex)) || (_mousedItemIndex == _dragItemIndex && ((_type == OverviewPhotos || _type == OverviewVideos) ? (_dragStartPos.x() < m.x()) : (_dragStartPos.y() < m.y())));
				MsgId dragSelFrom = _dragItem, dragSelTo = _mousedItem;
				int32 dragSelFromIndex = _dragItemIndex, dragSelToIndex = _mousedItemIndex;
				if (!itemHasPoint(dragSelFrom, dragSelFromIndex, _dragStartPos.x(), _dragStartPos.y())) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_type == OverviewPhotos || _type == OverviewVideos) {
							if (_dragStartPos.x() >= _rowWidth || ((_mousedItem == dragSelFrom) && (m.x() < _dragStartPos.x() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, 1);
							}
						} else {
							if (_dragStartPos.y() >= itemHeight(dragSelFrom, dragSelFromIndex) || ((_mousedItem == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, _reversed ? -1 : 1);
							}
						}
					} else {
						if (_type == OverviewPhotos || _type == OverviewVideos) {
							if (_dragStartPos.x() < 0 || ((_mousedItem == dragSelFrom) && (m.x() >= _dragStartPos.x() - QApplication::startDragDistance()))) {
								moveToNextItem(dragSelFrom, dragSelFromIndex, dragSelTo, -1);
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
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, -1);
							}
						} else {
							if (m.y() < 0) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, _reversed ? 1 : -1);
							}
						}
					} else {
						if (_type == OverviewPhotos || _type == OverviewVideos) {
							if (m.x() >= _rowWidth) {
								moveToNextItem(dragSelTo, dragSelToIndex, dragSelFrom, 1);
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

		if (ClickHandler::getPressed()) {
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

QPoint OverviewInner::tooltipPos() const {
	return _dragPos;
}

QString OverviewInner::tooltipText() const {
	if (_cursorState == HistoryInDateCursorState && _dragAction == NoDrag && _mousedItem) {
		if (HistoryItem *item = App::histItemById(itemChannel(_mousedItem), itemMsgId(_mousedItem))) {
			return item->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat));
		}
	} else if (ClickHandlerPtr lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
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
	if ((_search->isHidden() || !_search->hasFocus()) && !_overview->isHidden() && e->key() == Qt::Key_Escape) {
		onCancel();
	} else if (e->key() == Qt::Key_Back) {
		App::main()->showBackFromStack();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		onSearchMessages();
	}
}

void OverviewInner::enterEventHook(QEvent *e) {
	return TWidget::enterEventHook(e);
}

void OverviewInner::leaveEventHook(QEvent *e) {
	if (auto selectedMsgId = base::take(_selectedMsgId)) {
		repaintItem(selectedMsgId, -1);
	}
	ClickHandler::clearActive();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return TWidget::leaveEventHook(e);
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

	auto selectedState = getSelectionState();

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

	_contextMenuLnk = ClickHandler::getActive();
	PhotoClickHandler *lnkPhoto = dynamic_cast<PhotoClickHandler*>(_contextMenuLnk.data());
	DocumentClickHandler *lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLnk.data());
	bool lnkIsVideo = lnkDocument ? lnkDocument->document()->isVideo() : false;
	bool lnkIsAudio = lnkDocument ? (lnkDocument->document()->voice() != nullptr) : false;
	bool lnkIsSong = lnkDocument ? (lnkDocument->document()->song() != nullptr) : false;
	if (lnkPhoto || lnkDocument) {
		_menu = new Ui::PopupMenu(nullptr);
		if (App::hoveredLinkItem()) {
			_menu->addAction(lang(lng_context_to_msg), this, SLOT(goToMessage()))->setEnabled(true);
		}
		if (lnkPhoto) {
		} else {
			if (auto document = lnkDocument->document()) {
				if (document->loading()) {
					_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
				} else {
					if (!document->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
						_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
					}
					_menu->addAction(lang(lnkIsVideo ? lng_context_save_video : (lnkIsAudio ? lng_context_save_audio : (lnkIsSong ? lng_context_save_audio_file : lng_context_save_file))), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
						saveDocumentToFile(document);
					}))->setEnabled(true);
				}
			}
		}
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				_menu->addAction(lang(lng_context_forward_selected), _overview, SLOT(onForwardSelected()));
			}
			if (selectedState.count > 0 && selectedState.count == selectedState.canDeleteCount) {
				_menu->addAction(lang(lng_context_delete_selected), base::lambda_guarded(this, [this] {
					_overview->confirmDeleteSelectedItems();
				}));
			}
			_menu->addAction(lang(lng_context_clear_selection), _overview, SLOT(onClearSelected()));
		} else if (App::hoveredLinkItem()) {
			if (isUponSelected != -2) {
				if (App::hoveredLinkItem()->canForward()) {
					_menu->addAction(lang(lng_context_forward_msg), this, SLOT(forwardMessage()))->setEnabled(true);
				}
				if (App::hoveredLinkItem()->canDelete()) {
					_menu->addAction(lang(lng_context_delete_msg), base::lambda_guarded(this, [this] {
						_overview->confirmDeleteContextItem();
					}));
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
		_menu = new Ui::PopupMenu(nullptr);
		QString linkCopyToClipboardText = _contextMenuLnk ? _contextMenuLnk->copyToClipboardContextItemText() : QString();
		if (!linkCopyToClipboardText.isEmpty()) {
			_menu->addAction(linkCopyToClipboardText, this, SLOT(copyContextUrl()))->setEnabled(true);
		}
		_menu->addAction(lang(lng_context_to_msg), this, SLOT(goToMessage()))->setEnabled(true);
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				_menu->addAction(lang(lng_context_forward_selected), _overview, SLOT(onForwardSelected()));
			}
			if (selectedState.count > 0 && selectedState.count == selectedState.canDeleteCount) {
				_menu->addAction(lang(lng_context_delete_selected), base::lambda_guarded(this, [this] {
					_overview->confirmDeleteSelectedItems();
				}));
			}
			_menu->addAction(lang(lng_context_clear_selection), _overview, SLOT(onClearSelected()));
		} else {
			if (isUponSelected != -2) {
				if (App::mousedItem()->canForward()) {
					_menu->addAction(lang(lng_context_forward_msg), this, SLOT(forwardMessage()))->setEnabled(true);
				}
				if (App::mousedItem()->canDelete()) {
					_menu->addAction(lang(lng_context_delete_msg), base::lambda_guarded(this, [this] {
						_overview->confirmDeleteContextItem();
					}));
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
		_rowsLeft = st::overviewPhotoSkip;
	} else {
		auto contentLeftMin = st::overviewLeftMin;
		auto contentLeftMax = st::overviewLeftMax;
		if (_type == OverviewMusicFiles || _type == OverviewVoiceFiles) {
			contentLeftMin -= st::overviewFileLayout.songPadding.left();
			contentLeftMax -= st::overviewFileLayout.songPadding.left();
		}
		auto widthWithMin = st::windowMinWidth;
		auto widthWithMax = st::overviewFileLayout.maxWidth + 2 * contentLeftMax;
		_rowsLeft = anim::interpolate(contentLeftMax, contentLeftMin, qMax(widthWithMax - _width, 0) / float64(widthWithMax - widthWithMin));
		_rowWidth = qMin(_width - 2 * _rowsLeft, st::overviewFileLayout.maxWidth);
	}

	_search->setGeometry(_rowsLeft, st::linksSearchTop, _rowWidth, _search->height());
	_cancelSearch->moveToLeft(_rowsLeft + _rowWidth - _cancelSearch->width(), _search->y());

	resizeItems();
	recountMargins();

	resize(_width, _marginTop + _height + _marginBottom);

	if (_type == OverviewPhotos || _type == OverviewVideos) {
        int32 newRow = _resizeIndex / _photosInRow;
        return newRow * int32(_rowWidth + st::overviewPhotoSkip) + _resizeSkip - minHeight;
    }
    return scrollTop;
}

void OverviewInner::resizeItems() {
	if (_type == OverviewPhotos || _type == OverviewVideos || _type == OverviewLinks) {
		resizeAndRepositionItems();
	} else {
		for (auto i = 0, l = _items.size(); i != l; ++i) {
			_items.at(i)->resizeGetHeight(_rowWidth);
		}
	}
}

void OverviewInner::resizeAndRepositionItems() {
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		for (auto i = 0, l = _items.size(); i < l; ++i) {
			_items.at(i)->resizeGetHeight(_rowWidth);
		}
		_height = countHeight();
	} else {
		_height = 0;
		for (auto i = 0, l = _items.size(); i < l; ++i) {
			auto h = _items.at(i)->resizeGetHeight(_rowWidth);
			_items.at(i)->Get<Overview::Layout::Info>()->top = _height + (_reversed ? h : 0);
			_height += h;
		}
	}
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
		_reversed = (_type != OverviewLinks && _type != OverviewFiles);
		if (_type == OverviewLinks || _type == OverviewFiles) {
			_search->show();
		} else {
			_search->hide();
		}

		if (!_search->getLastText().isEmpty()) {
			_search->setText(QString());
			_search->updatePlaceholder();
			onSearchUpdate();
		}
		_cancelSearch->hideFast();

		resizeToWidth(_width, 0, _minHeight, true);
	}
	mediaOverviewUpdated();
	if (App::wnd()) App::wnd()->update();
}

void OverviewInner::setSelectMode(bool enabled) {
	_selMode = enabled;
}

void OverviewInner::copyContextUrl() {
	if (_contextMenuLnk) {
		_contextMenuLnk->copyToClipboard();
	}
}

void OverviewInner::goToMessage() {
	HistoryItem *item = App::contextItem();
	if (!item) return;

	Ui::showPeerHistoryAtItem(item);
}

void OverviewInner::forwardMessage() {
	auto item = App::contextItem();
	if (!item || item->id < 0 || item->serviceMsg()) return;

	auto items = SelectedItemSet();
	items.insert(item->id, item);
	App::main()->showForwardLayer(items);
}

MsgId OverviewInner::complexMsgId(const HistoryItem *item) const {
	return item ? ((item->history() == _migrated) ? -item->id : item->id) : 0;
}

void OverviewInner::selectMessage() {
	auto item = App::contextItem();
	if (!item || item->id < 0) return;

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
	DocumentClickHandler *lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLnk.data());
	if (lnkDocument) {
		lnkDocument->document()->cancel();
	}
}

void OverviewInner::showContextInFolder() {
	if (auto lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLnk.data())) {
		auto filepath = lnkDocument->document()->filepath(DocumentData::FilePathResolveChecked);
		if (!filepath.isEmpty()) {
			File::ShowInFolder(filepath);
		}
	}
}

void OverviewInner::saveDocumentToFile(DocumentData *document) {
	DocumentSaveClickHandler::doSave(document, true);
}

bool OverviewInner::onSearchMessages(bool searchCache) {
	_searchTimer.stop();
	auto q = _search->text().trimmed();
	if (q.isEmpty()) {
		MTP::cancel(base::take(_searchRequest));
		return true;
	}
	if (searchCache) {
		auto i = _searchCache.constFind(q);
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
		auto filter = (_type == OverviewLinks) ? MTP_inputMessagesFilterUrl() : MTP_inputMessagesFilterDocument();
		_searchRequest = MTP::send(MTPmessages_Search(MTP_flags(0), _history->peer->input, MTP_string(_searchQuery), MTP_inputUserEmpty(), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(SearchPerPage), MTP_int(0), MTP_int(0)), rpcDone(&OverviewInner::searchReceived, SearchFromStart), rpcFail(&OverviewInner::searchFailed, SearchFromStart));
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
	QString filterText = (_type == OverviewLinks || _type == OverviewFiles) ? _search->text().trimmed() : QString();
	bool inSearch = !filterText.isEmpty(), changed = (inSearch != _inSearch);
	_inSearch = inSearch;

	onNeedSearchMessages();

	if (!_inSearch) {
		_searchCache.clear();
		_searchQueries.clear();
		_searchQuery = QString();
		_searchResults.clear();
		_cancelSearch->hideAnimated();
	} else {
		_cancelSearch->showAnimated();
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
	if (_search->isHidden()) return false;
	bool clearing = !_search->text().isEmpty();
	_cancelSearch->hideAnimated();
	_search->clear();
	_search->updatePlaceholder();
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

Window::TopBarWidget::SelectedState OverviewInner::getSelectionState() const {
	auto result = Window::TopBarWidget::SelectedState {};
	for (auto i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		if (i.value() == FullSelection) {
			if (auto item = App::histItemById(itemChannel(i.key()), itemMsgId(i.key()))) {
				++result.count;
				if (item->canForward()) {
					++result.canForwardCount;
				}
				if (item->canDelete()) {
					++result.canDeleteCount;
				}
			}
		}
	}
	return result;
}

void OverviewInner::clearSelectedItems(bool onlyTextSelection) {
	if (!_selected.isEmpty() && (!onlyTextSelection || _selected.cbegin().value() != FullSelection)) {
		_selected.clear();
		_overview->updateTopBarSelection();
		_overview->update();
	}
}

SelectedItemSet OverviewInner::getSelectedItems() const {
	auto result = SelectedItemSet();
	if (_selected.isEmpty() || _selected.cbegin().value() != FullSelection) {
		return result;
	}

	for (auto i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		auto item = App::histItemById(itemChannel(i.key()), itemMsgId(i.key()));
		if (item && item->toHistoryMessage() && item->id > 0) {
			if (item->history() == _migrated) {
				result.insert(item->id - ServerMaxMsgId, item);
			} else {
				result.insert(item->id, item);
			}
		}
	}
	return result;
}

void OverviewInner::onTouchSelect() {
	_touchSelect = true;
	dragActionStart(_touchPos);
}

void OverviewInner::onTouchScrollTimer() {
	auto nowTime = getms();
	if (_touchScrollState == Ui::TouchScrollState::Acceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = Ui::TouchScrollState::Manual;
		touchResetSpeed();
	} else if (_touchScrollState == Ui::TouchScrollState::Auto || _touchScrollState == Ui::TouchScrollState::Acceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = _overview->touchScroll(delta);

		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = Ui::TouchScrollState::Manual;
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
		auto &o = _history->overview(_type);
		auto migratedOverview = _migrated ? &_migrated->overview(_type) : nullptr;
		auto migrateCount = migratedIndexSkip();
		auto wasCount = _items.size();
		auto fullCount = (migrateCount + o.size());
		auto tocheck = qMin(fullCount, _itemsToBeLoaded);
		_items.reserve(tocheck);

		auto index = 0;
		auto allGood = true;
		auto migrateIt = migratedOverview ? migratedOverview->end() : o.end();
		auto it = o.end();
		for (auto i = fullCount, l = fullCount - tocheck; i > l;) {
			--i;
			auto msgid = MsgId(0);
			if (i < migrateCount) {
				--migrateIt;
				msgid = -(*migrateIt);
			} else {
				--it;
				msgid = *it;
			}
			if (allGood) {
				if (_items.size() > index && complexMsgId(_items.at(index)->getItem()) == msgid) {
					++index;
					continue;
				}
				allGood = false;
			}
			HistoryItem *item = App::histItemById(itemChannel(msgid), itemMsgId(msgid));
			auto layout = layoutPrepare(item);
			if (!layout) continue;

			setLayoutItem(index, layout, 0);
			++index;
		}
		if (_items.size() > index) _items.resize(index);

		_height = countHeight();
	} else {
		bool dateEveryMonth = (_type == OverviewFiles), dateEveryDay = (_type == OverviewLinks);
		bool withDates = (dateEveryMonth || dateEveryDay);

		auto &o = _history->overview(_type);
		auto migratedOverview = _migrated ? &_migrated->overview(_type) : nullptr;
		auto migrateCount = migratedIndexSkip();
		auto l = _inSearch ? _searchResults.size() : (migrateCount + o.size());
		auto tocheck = qMin(l, _itemsToBeLoaded);
		_items.reserve((withDates ? 2 : 1) * tocheck); // day items

		auto migrateIt = migratedOverview ? migratedOverview->end() : o.end();
		auto it = o.end();

		auto top = 0;
		auto count = 0;
		bool allGood = true;
		QDate prevDate;
		for (auto i = 0; i < tocheck; ++i) {
			auto msgid = MsgId(0);
			auto index = l - i - 1;
			if (_inSearch) {
				msgid = _searchResults[index];
			} else if (index < migrateCount) {
				--migrateIt;
				msgid = -(*migrateIt);
			} else {
				--it;
				msgid = *it;
			}
			if (allGood) {
				if (_items.size() > count && complexMsgId(_items.at(count)->getItem()) == msgid) {
					if (withDates) prevDate = _items.at(count)->getItem()->date.date();
					top = _items.at(count)->Get<Overview::Layout::Info>()->top;
					if (!_reversed) {
						top += _items.at(count)->height();
					}
					++count;
					continue;
				}
				if (_items.size() > count + 1 && !_items.at(count)->toMediaItem() && complexMsgId(_items.at(count + 1)->getItem()) == msgid) { // day item
					++count;
					if (withDates) prevDate = _items.at(count)->getItem()->date.date();
					top = _items.at(count)->Get<Overview::Layout::Info>()->top;
					if (!_reversed) {
						top += _items.at(count)->height();
					}
					++count;
					continue;
				}
				allGood = false;
			}
			auto item = App::histItemById(itemChannel(msgid), itemMsgId(msgid));
			auto layout = layoutPrepare(item);
			if (!layout) continue;

			if (withDates) {
				QDate date = item->date.date();
				if (!count || (count > 0 && (dateEveryMonth ? (date.month() != prevDate.month() || date.year() != prevDate.year()) : (date != prevDate)))) {
					top += setLayoutItem(count, layoutPrepare(date, dateEveryMonth), top);
					++count;
					prevDate = date;
				}
			}
			top += setLayoutItem(count, layout, top);
			++count;
		}
		if (_items.size() > count) _items.resize(count);

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
		if (_type == OverviewMusicFiles || _type == OverviewVoiceFiles) {
			_overview->scrollBy(deltaHeight);
		}
	} else {
		onUpdateSelected();
		update();
	}
}

void OverviewInner::changingMsgId(HistoryItem *row, MsgId newId) {
	if (peer() != row->history()->peer && migratePeer() != row->history()->peer) {
		return;
	}

	MsgId oldId = complexMsgId(row);
	if (row->history() == _migrated) newId = -newId;

	if (_dragSelFrom == oldId) _dragSelFrom = newId;
	if (_dragSelTo == oldId) _dragSelTo = newId;
	if (_mousedItem == oldId) _mousedItem = newId;
	if (_dragItem == oldId) _dragItem = newId;
	if (_selectedMsgId == oldId) _selectedMsgId = newId;
	for (SelectedItems::iterator i = _selected.begin(), e = _selected.end(); i != e; ++i) {
		if (i.key() == oldId) {
			auto sel = i.value();
			_selected.erase(i);
			_selected.insert(newId, sel);
			break;
		}
	}
}

void OverviewInner::itemRemoved(HistoryItem *item) {
	if (_history != item->history() && _migrated != item->history()) {
		return;
	}

	MsgId msgId = (item->history() == _migrated) ? -item->id : item->id;
	if (_dragItem == msgId) {
		dragActionCancel();
	}
	if (_selectedMsgId == msgId) {
		_selectedMsgId = 0;
	}

	auto i = _selected.find(msgId);
	if (i != _selected.cend()) {
		_selected.erase(i);
		_overview->updateTopBarSelection();
	}

	auto j = _layoutItems.find(item);
	if (j != _layoutItems.cend()) {
		int32 index = _items.indexOf(j.value());
		if (index >= 0) {
			_items.remove(index);
		}
		delete j.value();
		_layoutItems.erase(j);

		resizeAndRepositionItems();
	}

	if (_dragSelFrom == msgId || _dragSelTo == msgId) {
		_dragSelFrom = 0;
		_dragSelFromIndex = -1;
		_dragSelTo = 0;
		_dragSelToIndex = -1;
		update();
	}

	onUpdateSelected();
}

void OverviewInner::repaintItem(const HistoryItem *msg) {
	if (!msg) return;

	History *history = (msg->history() == _history) ? _history : (msg->history() == _migrated ? _migrated : 0);
	if (!history) return;

	int32 migrateindex = migratedIndexSkip();
	MsgId msgid = msg->id;
	if ((history == _history || migrateindex > 0) && (_inSearch || history->overviewHasMsgId(_type, msgid))) {
		if (_type == OverviewPhotos || _type == OverviewVideos) {
			if (history == _migrated) msgid = -msgid;
			for (int32 i = 0, l = _items.size(); i != l; ++i) {
				if (complexMsgId(_items.at(i)->getItem()) == msgid) {
					float64 w = (float64(width() - st::overviewPhotoSkip) / _photosInRow);
					int32 vsize = (_rowWidth + st::overviewPhotoSkip);
					int32 row = i / _photosInRow, col = i % _photosInRow;
					update(int32(col * w), _marginTop + int32(row * vsize), qCeil(w), vsize);
					break;
				}
			}
		} else {
			if (history == _migrated) msgid = -msgid;
			for (int32 i = 0, l = _items.size(); i != l; ++i) {
				if (complexMsgId(_items.at(i)->getItem()) == msgid) {
					int32 top = _items.at(i)->Get<Overview::Layout::Info>()->top;
					if (_reversed) top = _height - top;
					update(_rowsLeft, _marginTop + top, _rowWidth, _items.at(i)->height());
					break;
				}
			}
		}
	}
}

int OverviewInner::countHeight() {
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		auto count = _items.size();
		auto migratedFullCount = _migrated ? _migrated->overviewCount(_type) : 0;
		auto fullCount = migratedFullCount + _history->overviewCount(_type);
		auto rows = (count / _photosInRow) + ((count % _photosInRow) ? 1 : 0);
		return (_rowWidth + st::overviewPhotoSkip) * rows + st::overviewPhotoSkip;
	}
	return _height;
}

void OverviewInner::recountMargins() {
	if (_type == OverviewPhotos || _type == OverviewVideos) {
		_marginBottom = qMax(_minHeight - _height - _marginTop, 0);
		_marginTop = 0;
	} else if (_type == OverviewMusicFiles) {
		_marginTop = st::playlistPadding;
		_marginBottom = qMax(_minHeight - _height - _marginTop, int32(st::playlistPadding));
	} else if (_type == OverviewLinks || _type == OverviewFiles) {
		_marginTop = st::linksSearchTop + _search->height();
		_marginBottom = qMax(_minHeight - _height - _marginTop, int32(st::playlistPadding));
	} else {
		_marginBottom = st::playlistPadding;
		_marginTop = qMax(_minHeight - _height - _marginBottom, int32(st::playlistPadding));
	}
}

Overview::Layout::ItemBase *OverviewInner::layoutPrepare(HistoryItem *item) {
	if (!item) return nullptr;

	LayoutItems::const_iterator i = _layoutItems.cend();
	HistoryMedia *media = item->getMedia();
	if (_type == OverviewPhotos) {
		if (media && media->type() == MediaTypePhoto) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new Overview::Layout::Photo(static_cast<HistoryPhoto*>(media)->photo(), item));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewVideos) {
		if (media && media->type() == MediaTypeVideo) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new Overview::Layout::Video(media->getDocument(), item));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewVoiceFiles) {
		if (media && (media->type() == MediaTypeVoiceFile)) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new Overview::Layout::Voice(media->getDocument(), item, st::overviewFileLayout));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewFiles || _type == OverviewMusicFiles) {
		if (media && (media->type() == MediaTypeFile || media->type() == MediaTypeMusicFile || media->type() == MediaTypeGif)) {
			if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
				i = _layoutItems.insert(item, new Overview::Layout::Document(media->getDocument(), item, st::overviewFileLayout));
				i.value()->initDimensions();
			}
		}
	} else if (_type == OverviewLinks) {
		if ((i = _layoutItems.constFind(item)) == _layoutItems.cend()) {
			i = _layoutItems.insert(item, new Overview::Layout::Link(media, item));
			i.value()->initDimensions();
		}
	}
	return (i == _layoutItems.cend()) ? nullptr : i.value();
}

Overview::Layout::AbstractItem *OverviewInner::layoutPrepare(const QDate &date, bool month) {
	int32 key = date.year() * 100 + date.month();
	if (!month) key = key * 100 + date.day();
	LayoutDates::const_iterator i = _layoutDates.constFind(key);
	if (i == _layoutDates.cend()) {
		i = _layoutDates.insert(key, new Overview::Layout::Date(date, month));
		i.value()->initDimensions();
	}
	return i.value();
}

int32 OverviewInner::setLayoutItem(int32 index, Overview::Layout::AbstractItem *item, int32 top) {
	if (_items.size() > index) {
		_items[index] = item;
	} else {
		_items.push_back(item);
	}
	int32 h = item->resizeGetHeight(_rowWidth);
	if (auto info = item->Get<Overview::Layout::Info>()) {
		info->top = top + (_reversed ? h : 0);
	}
	return h;
}

OverviewInner::~OverviewInner() {
	clear();
}

OverviewWidget::OverviewWidget(QWidget *parent, not_null<Window::Controller*> controller, PeerData *peer, MediaOverviewType type) : Window::AbstractSectionWidget(parent, controller)
, _topBar(this, controller)
, _scroll(this, st::settingsScroll, false)
, _mediaType(this, st::defaultDropdownMenu)
, _topShadow(this, st::shadowFg) {
	_inner = _scroll->setOwnedWidget(object_ptr<OverviewInner>(this, _scroll, peer, type));
	_scroll->move(0, 0);
	_inner->move(0, 0);

	connect(_topBar, &Window::TopBarWidget::clicked, this, [this] { topBarClick(); });

	_mediaType->hide();
	_mediaType->setOrigin(Ui::PanelAnimation::Origin::TopRight);
	_topBar->mediaTypeButton()->installEventFilter(_mediaType);

	_topBar->show();
	_scroll->show();
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	_scrollTimer.setSingleShot(false);

	switchType(type);
}

void OverviewWidget::clear() {
	_inner->clear();
}

void OverviewWidget::onScroll() {
	Auth().downloader().clearPriorities();
	int32 preloadThreshold = _scroll->height() * 5;
	bool needToPreload = false;
	do {
		needToPreload = (type() == OverviewMusicFiles || type() == OverviewVoiceFiles) ? (_scroll->scrollTop() < preloadThreshold) : (_scroll->scrollTop() + preloadThreshold > _scroll->scrollTopMax());
		if (!needToPreload || !_inner->preloadLocal()) {
			break;
		}
	} while (true);
	if (needToPreload) {
		_inner->preloadMore();
	}
	if (!_noDropResizeIndex) {
		_inner->dropResizeIndex();
	}
}

void OverviewWidget::resizeEvent(QResizeEvent *e) {
	_topBar->setGeometryToLeft(0, 0, width(), st::topBarHeight);
	auto scrollAreaTop = _topBar->bottomNoMargins();

	_noDropResizeIndex = true;
	auto st = _scroll->scrollTop();
	_scroll->setGeometryToLeft(0, scrollAreaTop, width(), height() - scrollAreaTop);
	auto newScrollTop = _inner->resizeToWidth(width(), st, height() - _topBar->height());
	if (auto addToY = App::main() ? App::main()->contentScrollAddToY() : 0) {
		newScrollTop += addToY;
	}
	if (newScrollTop != _scroll->scrollTop()) {
		_scroll->scrollToY(newScrollTop);
	}
	_noDropResizeIndex = false;

	_topShadow->resize(width() - ((!Adaptive::OneColumn() && !_inGrab) ? st::lineWidth : 0), st::lineWidth);
	_topShadow->moveToLeft((!Adaptive::OneColumn() && !_inGrab) ? st::lineWidth : 0, _topBar->bottomNoMargins());

	_mediaType->moveToRight(0, scrollAreaTop);
}

void OverviewWidget::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);
	auto progress = _a_show.current(getms(), 1.);
	if (_a_show.animating()) {
		auto retina = cIntRetinaFactor();
		auto fromLeft = (_showDirection == Window::SlideDirection::FromLeft);
		auto coordUnder = fromLeft ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = fromLeft ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		auto shadow = fromLeft ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, 0, coordOver, height()), _cacheUnder, QRect(-coordUnder * retina, 0, coordOver * retina, height() * retina));
			p.setOpacity(shadow);
			p.fillRect(0, 0, coordOver, height(), st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(QRect(coordOver, 0, _cacheOver.width() / retina, height()), _cacheOver, QRect(0, 0, _cacheOver.width(), height() * retina));
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), height()));
		return;
	}

	p.fillRect(e->rect(), st::windowBg);
}

void OverviewWidget::contextMenuEvent(QContextMenuEvent *e) {
	return _inner->showContextMenu(e);
}

void OverviewWidget::scrollBy(int32 add) {
	if (_scroll->isHidden()) {
		_scrollSetAfterShow += add;
	} else {
		_scroll->scrollToY(_scroll->scrollTop() + add);
	}
}

void OverviewWidget::scrollReset() {
	_scroll->scrollToY((type() == OverviewMusicFiles || type() == OverviewVoiceFiles) ? _scroll->scrollTopMax() : 0);
}

bool OverviewWidget::paintTopBar(Painter &p, int decreaseWidth) {
	st::topBarBack.paint(p, (st::topBarArrowPadding.left() - st::topBarBack.width()) / 2, (st::topBarHeight - st::topBarBack.height()) / 2, width());
	p.setFont(st::defaultLightButton.font);
	p.setPen(st::defaultLightButton.textFg);
	p.drawTextLeft(st::topBarArrowPadding.left(), st::topBarButton.padding.top() + st::topBarButton.textTop, width(), _header);
	return true;
}

void OverviewWidget::topBarClick() {
	App::main()->showBackFromStack();
}

PeerData *OverviewWidget::peer() const {
	return _inner->peer();
}

PeerData *OverviewWidget::migratePeer() const {
	return _inner->migratePeer();
}

MediaOverviewType OverviewWidget::type() const {
	return _inner->type();
}

void OverviewWidget::switchType(MediaOverviewType type) {
	disconnect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	_inner->setSelectMode(false);
	_inner->switchType(type);
	switch (type) {
	case OverviewPhotos: _header = lang(lng_profile_photos_header); break;
	case OverviewVideos: _header = lang(lng_profile_videos_header); break;
	case OverviewMusicFiles: _header = lang(lng_profile_songs_header); break;
	case OverviewFiles: _header = lang(lng_profile_files_header); break;
	case OverviewVoiceFiles: _header = lang(lng_profile_audios_header); break;
	case OverviewLinks: _header = lang(lng_profile_shared_links_header); break;
	}
	_header = _header.toUpper();

	noSelectingScroll();
	_topBar->showSelected(Window::TopBarWidget::SelectedState {});
	updateTopBarSelection();
	scrollReset();

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	onScroll();
	activate();
}

bool OverviewWidget::showMediaTypeSwitch() const {
	for (int32 i = 0; i < OverviewCount; ++i) {
		if (!(_mediaTypeMask & ~(1 << i))) {
			return false;
		}
	}
	return true;
}

bool OverviewWidget::contentOverlapped(const QRect &globalRect) {
	return _mediaType->overlaps(globalRect);
}

void OverviewWidget::updateTopBarSelection() {
	auto selectedState = _inner->getSelectionState();
	_inner->setSelectMode(selectedState.count > 0);
	if (App::main()) {
		_topBar->showSelected(selectedState);
		_topBar->update();
	}
	if (App::wnd() && !Ui::isLayerShown()) {
		_inner->activate();
	}
	update();
}

int32 OverviewWidget::lastWidth() const {
	return width();
}

int32 OverviewWidget::lastScrollTop() const {
	return _scroll->scrollTop();
}

bool OverviewWidget::wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) {
	return _scroll->viewportEvent(e);
}

QRect OverviewWidget::rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) {
	return mapToGlobal(_scroll->geometry());
}

int32 OverviewWidget::countBestScroll() const {
	if (type() == OverviewMusicFiles) {
		auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
		if (state.id) {
			int32 top = _inner->itemTop(state.id.contextId());
			if (top >= 0) {
				return snap(top - int(_scroll->height() - (st::msgPadding.top() + st::mediaThumbSize + st::msgPadding.bottom())) / 2, 0, _scroll->scrollTopMax());
			}
		}
	} else if (type() == OverviewLinks || type() == OverviewFiles) {
		return 0;
	}
	return _scroll->scrollTopMax();
}

void OverviewWidget::fastShow(bool back, int32 lastScrollTop) {
	resizeEvent(0);
	_scrollSetAfterShow = (lastScrollTop < 0 ? countBestScroll() : lastScrollTop);
	show();
	_inner->activate();
	doneShow();
}

void OverviewWidget::setLastScrollTop(int lastScrollTop) {
	resizeEvent(0);
	_scroll->scrollToY(lastScrollTop < 0 ? countBestScroll() : lastScrollTop);
}

void OverviewWidget::showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params) {
	_showDirection = direction;
	resizeEvent(0);

	_a_show.finish();

	_cacheUnder = params.oldContentCache;
	show();
	_topBar->updateControlsVisibility();
	_topShadow->setVisible(params.withTopBarShadow ? false : true);
	_cacheOver = App::main()->grabForShowAnimation(params);
	_topShadow->setVisible(params.withTopBarShadow ? true : false);

	_topBar->hide();
	_scrollSetAfterShow = _scroll->scrollTop();
	_scroll->hide();

	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());

	_backAnimationButton.create(this);
	_backAnimationButton->setClickedCallback([this] { topBarClick(); });
	_backAnimationButton->setGeometry(_topBar->geometry());
	_backAnimationButton->show();

	activate();
}

void OverviewWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		_topShadow->show();
		_cacheUnder = _cacheOver = QPixmap();
		doneShow();
	}
}

void OverviewWidget::doneShow() {
	_topBar->animationFinished();
	_backAnimationButton.destroy();
	_topBar->show();
	_scroll->show();
	_scroll->scrollToY(_scrollSetAfterShow);
	activate();
	onScroll();
}

void OverviewWidget::mediaOverviewUpdated(const Notify::PeerUpdate &update) {
	if ((peer() == update.peer || migratePeer() == update.peer) && (update.mediaTypesMask & (1 << type()))) {
		_inner->mediaOverviewUpdated();
		onScroll();
		updateTopBarSelection();
	}
	int32 mask = 0;
	History *h = update.peer ? App::historyLoaded(update.peer->migrateTo() ? update.peer->migrateTo() : update.peer) : nullptr;
	History *m = (update.peer && update.peer->migrateFrom()) ? App::historyLoaded(update.peer->migrateFrom()->id) : 0;
	if (h) {
		for (int32 i = 0; i < OverviewCount; ++i) {
			if (!h->overview(i).isEmpty() || h->overviewCount(i) > 0 || i == type()) {
				mask |= (1 << i);
			} else if (m && (!m->overview(i).isEmpty() || m->overviewCount(i) > 0)) {
				mask |= (1 << i);
			}
		}
	}
	if (mask != _mediaTypeMask) {
		auto typeLabel = [](MediaOverviewType type) -> QString {
			switch (type) {
			case OverviewPhotos: return lang(lng_media_type_photos);
			case OverviewVideos: return lang(lng_media_type_videos);
			case OverviewMusicFiles: return lang(lng_media_type_songs);
			case OverviewFiles: return lang(lng_media_type_files);
			case OverviewVoiceFiles: return lang(lng_media_type_audios);
			case OverviewLinks: return lang(lng_media_type_links);
			}
			return QString();
		};
		_mediaType->clearActions();
		for (auto i = 0; i != OverviewCount; ++i) {
			if (mask & (1 << i)) {
				auto type = static_cast<MediaOverviewType>(i);
				auto label = typeLabel(type);
				if (!label.isEmpty()) {
					_mediaType->addAction(label, [this, type] {
						switchType(type);
						_mediaType->hideAnimated();
					});
				}
			}
		}
		_mediaTypeMask = mask;
		_mediaType->move(width() - _mediaType->width(), st::topBarHeight);
		updateTopBarSelection();
	}
}

void OverviewWidget::grapWithoutTopBarShadow() {
	grabStart();
	_topShadow->hide();
}

void OverviewWidget::grabFinish() {
	_inGrab = false;
	resizeEvent(0);
	_topShadow->show();
}

void OverviewWidget::ui_repaintHistoryItem(not_null<const HistoryItem*> item) {
	if (peer() == item->history()->peer || migratePeer() == item->history()->peer) {
		_inner->repaintItem(item);
	}
}

void OverviewWidget::notify_historyItemLayoutChanged(const HistoryItem *item) {
	if (peer() == item->history()->peer || migratePeer() == item->history()->peer) {
		_inner->onUpdateSelected();
	}
}

SelectedItemSet OverviewWidget::getSelectedItems() const {
	return _inner->getSelectedItems();
}

OverviewWidget::~OverviewWidget() {
	onClearSelected();
	updateTopBarSelection();
}

void OverviewWidget::activate() {
	if (_scroll->isHidden()) {
		setFocus();
	} else {
		_inner->activate();
	}
}

QPoint OverviewWidget::clampMousePosition(QPoint point) {
	if (point.x() < 0) {
		point.setX(0);
	} else if (point.x() >= _scroll->width()) {
		point.setX(_scroll->width() - 1);
	}
	if (point.y() < _scroll->scrollTop()) {
		point.setY(_scroll->scrollTop());
	} else if (point.y() >= _scroll->scrollTop() + _scroll->height()) {
		point.setY(_scroll->scrollTop() + _scroll->height() - 1);
	}
	return point;
}

void OverviewWidget::onScrollTimer() {
	int32 d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + d);
}

void OverviewWidget::checkSelectingScroll(QPoint point) {
	if (point.y() < _scroll->scrollTop()) {
		_scrollDelta = point.y() - _scroll->scrollTop();
	} else if (point.y() >= _scroll->scrollTop() + _scroll->height()) {
		_scrollDelta = point.y() - _scroll->scrollTop() - _scroll->height() + 1;
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
	int32 scTop = _scroll->scrollTop(), scMax = _scroll->scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	_scroll->scrollToY(scNew);
	return true;
}

void OverviewWidget::onForwardSelected() {
	App::main()->showForwardLayer(getSelectedItems());
}

void OverviewWidget::confirmDeleteContextItem() {
	auto item = App::contextItem();
	if (!item) return;

	if (auto message = item->toHistoryMessage()) {
		if (message->uploading()) {
			App::main()->cancelUploadLayer();
			return;
		}
	}
	App::main()->deleteLayer();
}

void OverviewWidget::confirmDeleteSelectedItems() {
	auto selected = _inner->getSelectedItems();
	if (selected.isEmpty()) return;

	App::main()->deleteLayer(selected.size());
}

void OverviewWidget::deleteContextItem(bool forEveryone) {
	Ui::hideLayer();

	auto item = App::contextItem();
	if (!item) {
		return;
	}

	auto toDelete = QVector<MTPint>(1, MTP_int(item->id));
	auto history = item->history();
	auto wasOnServer = (item->id > 0);
	auto wasLast = (history->lastMsg == item);
	item->destroy();

	if (!wasOnServer && wasLast && !history->lastMsg) {
		App::main()->checkPeerHistory(history->peer);
	}

	if (wasOnServer) {
		App::main()->deleteMessages(history->peer, toDelete, forEveryone);
	}
}

void OverviewWidget::deleteSelectedItems(bool forEveryone) {
	Ui::hideLayer();

	auto selected = _inner->getSelectedItems();
	if (selected.isEmpty()) return;

	QMap<PeerData*, QVector<MTPint>> idsByPeer;
	for_const (auto item, selected) {
		if (item->id > 0) {
			idsByPeer[item->history()->peer].push_back(MTP_int(item->id));
		}
	}

	onClearSelected();
	for_const (auto item, selected) {
		item->destroy();
	}

	for (auto i = idsByPeer.cbegin(), e = idsByPeer.cend(); i != e; ++i) {
		App::main()->deleteMessages(i.key(), i.value(), forEveryone);
	}
}

void OverviewWidget::onClearSelected() {
	_inner->clearSelectedItems();
}
