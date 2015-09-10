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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "boxes/confirmbox.h"
#include "historywidget.h"
#include "gui/filedialog.h"
#include "boxes/photosendbox.h"
#include "mainwidget.h"
#include "window.h"
#include "passcodewidget.h"
#include "window.h"
#include "fileuploader.h"
#include "audio.h"

#include "localstorage.h"

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

HistoryList::HistoryList(HistoryWidget *historyWidget, ScrollArea *scroll, History *history) : QWidget(0)
    , hist(history)
	, ySkip(0)
	, botInfo(history->peer->chat ? 0 : history->peer->asUser()->botInfo)
	, botDescWidth(0), botDescHeight(0)
    , historyWidget(historyWidget)
    , scrollArea(scroll)
    , currentBlock(0)
    , currentItem(0)
	, _firstLoading(false)
    , _cursor(style::cur_default)
    , _dragAction(NoDrag)
    , _dragSelType(TextSelectLetters)
    , _dragItem(0)
	, _dragCursorState(HistoryDefaultCursorState)
    , _dragWasInactive(false)
    , _dragSelFrom(0)
    , _dragSelTo(0)
    , _dragSelecting(false)
	, _wasSelectedText(false)
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

	linkTipTimer.setSingleShot(true);
	connect(&linkTipTimer, SIGNAL(timeout()), this, SLOT(showLinkTip()));
	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));

	_trippleClickTimer.setSingleShot(true);

	if (botInfo && !botInfo->inited) {
		App::api()->requestFullPeer(hist->peer);
	}

	setMouseTracking(true);
}

void HistoryList::messagesReceived(const QVector<MTPMessage> &messages) {
	hist->addToFront(messages);
}

void HistoryList::messagesReceivedDown(const QVector<MTPMessage> &messages) {
	hist->addToBack(messages);
}

void HistoryList::updateMsg(const HistoryItem *msg) {
	if (!msg || msg->detached() || !hist || hist != msg->history()) return;
	update(0, ySkip + msg->block()->y + msg->y, width(), msg->height());
}

void HistoryList::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	bool trivial = (rect() == r);

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(r);
	}

	if (!_firstLoading && botInfo && !botInfo->text.isEmpty() && botDescHeight > 0) {
		if (r.top() < botDescRect.y() + botDescRect.height() && r.bottom() > botDescRect.y()) {
			textstyleSet(&st::inTextStyle);
			App::roundRect(p, botDescRect, st::msgInBg, MessageInCorners, &st::msgInShadow);

			p.setFont(st::msgNameFont->f);
			p.setPen(st::black->p);
			p.drawText(botDescRect.left() + st::msgPadding.left(), botDescRect.top() + st::msgPadding.top() + st::msgNameFont->ascent, lang(lng_bot_description));

			botInfo->text.draw(p, botDescRect.left() + st::msgPadding.left(), botDescRect.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip, botDescWidth);

			textstyleRestore();
		}
	} else if (_firstLoading || hist->isEmpty()) {
		QPoint dogPos((width() - st::msgDogImg.pxWidth()) / 2, ((height() - st::msgDogImg.pxHeight()) * 4) / 9);
		p.drawPixmap(dogPos, *cChatDogImage());
	}
	if (!_firstLoading && !hist->isEmpty()) {
		adjustCurrent(r.top());
		HistoryBlock *block = (*hist)[currentBlock];
		HistoryItem *item = (*block)[currentItem];

		SelectedItems::const_iterator selEnd = _selected.cend();
		bool hasSel = !_selected.isEmpty();

		int32 drawToY = r.bottom() - ySkip;

		int32 selfromy = 0, seltoy = 0;
		if (_dragSelFrom && _dragSelTo) {
			selfromy = _dragSelFrom->y + _dragSelFrom->block()->y;
			seltoy = _dragSelTo->y + _dragSelTo->block()->y + _dragSelTo->height();
		}

		int32 iBlock = currentBlock, iItem = currentItem, y = block->y + item->y;
		p.translate(0, ySkip + y);
		while (y < drawToY) {
			int32 h = item->height();
			uint32 sel = 0;
			if (y >= selfromy && y < seltoy) {
				sel = (_dragSelecting && !item->serviceMsg() && item->id > 0) ? FullItemSel : 0;
			} else if (hasSel) {
				SelectedItems::const_iterator i = _selected.constFind(item);
				if (i != selEnd) {
					sel = i.value();
				}
			}
			item->draw(p, sel);
			p.translate(0, h);
			++iItem;
			if (iItem == block->size()) {
				iItem = 0;
				++iBlock;
				if (iBlock == hist->size()) {
					break;
				}
				block = (*hist)[iBlock];
			}
			item = (*block)[iItem];
			y += h;
		}
	}
}

bool HistoryList::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
  			return true;
		}
	}
	return QWidget::event(e);
}

void HistoryList::onTouchScrollTimer() {
	uint64 nowTime = getms();
	if (_touchScrollState == TouchScrollAcceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = TouchScrollManual;
		touchResetSpeed();
	} else if (_touchScrollState == TouchScrollAuto || _touchScrollState == TouchScrollAcceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = historyWidget->touchScroll(delta);

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

void HistoryList::touchUpdateSpeed() {
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
					_touchSpeed  = QPoint(newSpeedX, newSpeedY);
				}
			}
		}
	} else {
		_touchPrevPosValid = true;
	}
	_touchSpeedTime = nowTime;
	_touchPrevPos = _touchPos;
}

void HistoryList::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

void HistoryList::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void HistoryList::touchEvent(QTouchEvent *e) {
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
		if (_menu) {
			e->accept();
			return; // ignore mouse press, that was hiding context menu
		}
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

void HistoryList::mouseMoveEvent(QMouseEvent *e) {
	if (!(e->buttons() & (Qt::LeftButton | Qt::MiddleButton)) && (textlnkDown() || _dragAction != NoDrag)) {
		mouseReleaseEvent(e);
	}
	dragActionUpdate(e->globalPos());
}

void HistoryList::dragActionUpdate(const QPoint &screenPos) {
	_dragPos = screenPos;
	onUpdateSelected();
}

void HistoryList::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	historyWidget->touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

QPoint HistoryList::mapMouseToItem(QPoint p, HistoryItem *item) {
	if (!item || item->detached()) return QPoint(0, 0);
	p.setY(p.y() - (height() - hist->height - st::historyPadding) - item->block()->y - item->y);
	return p;
}

void HistoryList::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	dragActionStart(e->globalPos(), e->button());
}

void HistoryList::dragActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	dragActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	if (App::pressedItem() != App::hoveredItem()) {
		updateMsg(App::pressedItem());
		App::pressedItem(App::hoveredItem());
		updateMsg(App::pressedItem());
	}
	if (textlnkDown() != textlnkOver()) {
		updateMsg(App::pressedLinkItem());
		textlnkDown(textlnkOver());
		App::pressedLinkItem(App::hoveredLinkItem());
		updateMsg(App::pressedLinkItem());
		updateMsg(App::pressedItem());
	}

	_dragAction = NoDrag;
	_dragItem = App::mousedItem();
	_dragStartPos = mapMouseToItem(mapFromGlobal(screenPos), _dragItem);
	_dragWasInactive = App::wnd()->inactivePress();
	if (_dragWasInactive) App::wnd()->inactivePress(false);
	if (textlnkDown()) {
		_dragAction = PrepareDrag;
	} else if (!_selected.isEmpty()) {
		if (_selected.cbegin().value() == FullItemSel) {
			if (_selected.constFind(_dragItem) != _selected.cend() && App::hoveredItem()) {
				_dragAction = PrepareDrag; // start items drag
			} else if (!_dragWasInactive) {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}
	if (_dragAction == NoDrag && _dragItem) {
		bool afterDragSymbol, uponSymbol;
		uint16 symbol;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			_dragItem->getSymbol(symbol, afterDragSymbol, uponSymbol, _dragStartPos.x(), _dragStartPos.y());
			if (uponSymbol) {
				uint32 selStatus = (symbol << 16) | symbol;
				if (selStatus != FullItemSel && (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel)) {
					if (!_selected.isEmpty()) {
						updateMsg(_selected.cbegin().key());
						_selected.clear();
					}
					_selected.insert(_dragItem, selStatus);
					_dragSymbol = symbol;
					_dragAction = Selecting;
					_dragSelType = TextSelectParagraphs;
					dragActionUpdate(_dragPos);
				    _trippleClickTimer.start(QApplication::doubleClickInterval());
				}
			}
		} else if (App::pressedItem()) {
			_dragItem->getSymbol(symbol, afterDragSymbol, uponSymbol, _dragStartPos.x(), _dragStartPos.y());
		}
		if (_dragSelType != TextSelectParagraphs) {
			if (App::pressedItem()) {
				_dragSymbol = symbol;
				bool uponSelected = uponSymbol;
				if (uponSelected) {
					if (_selected.isEmpty() ||
						_selected.cbegin().value() == FullItemSel ||
						_selected.cbegin().key() != _dragItem
					) {
						uponSelected = false;
					} else {
						uint16 selFrom = (_selected.cbegin().value() >> 16) & 0xFFFF, selTo = _selected.cbegin().value() & 0xFFFF;
						if (_dragSymbol < selFrom || _dragSymbol >= selTo) {
							uponSelected = false;
						}
					}
				}
				if (uponSelected) {
					_dragAction = PrepareDrag; // start text drag
				} else if (!_dragWasInactive) {
					if (dynamic_cast<HistorySticker*>(App::pressedItem()->getMedia()) || _dragCursorState == HistoryInDateCursorState) {
						_dragAction = PrepareDrag; // start sticker drag or by-date drag
					} else {
						if (afterDragSymbol) ++_dragSymbol;
						uint32 selStatus = (_dragSymbol << 16) | _dragSymbol;
						if (selStatus != FullItemSel && (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel)) {
							if (!_selected.isEmpty()) {
								updateMsg(_selected.cbegin().key());
								_selected.clear();
							}
							_selected.insert(_dragItem, selStatus);
							_dragAction = Selecting;
							updateMsg(_dragItem);
						} else {
							_dragAction = PrepareSelect;
						}
					}
				}
			} else if (!_dragWasInactive) {
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

void HistoryList::dragActionCancel() {
	_dragItem = 0;
	_dragAction = NoDrag;
	_dragStartPos = QPoint(0, 0);
	_dragSelFrom = _dragSelTo = 0;
	_wasSelectedText = false;
	historyWidget->noSelectingScroll();
}

void HistoryList::onDragExec() {
	if (_dragAction != Dragging) return;

	bool uponSelected = false;
	if (_dragItem) {
		bool afterDragSymbol;
		uint16 symbol;
		if (!_selected.isEmpty() && _selected.cbegin().value() == FullItemSel) {
			uponSelected = _selected.contains(_dragItem);
		} else {
			_dragItem->getSymbol(symbol, afterDragSymbol, uponSelected, _dragStartPos.x(), _dragStartPos.y());
			if (uponSelected) {
				if (_selected.isEmpty() ||
					_selected.cbegin().value() == FullItemSel ||
					_selected.cbegin().key() != _dragItem
					) {
					uponSelected = false;
				} else {
					uint16 selFrom = (_selected.cbegin().value() >> 16) & 0xFFFF, selTo = _selected.cbegin().value() & 0xFFFF;
					if (symbol < selFrom || symbol >= selTo) {
						uponSelected = false;
					}
				}
			}
		}
	}
	QString sel;
	QList<QUrl> urls;
	if (uponSelected) {
		sel = getSelectedText();
	} else if (textlnkDown()) {
		sel = textlnkDown()->encoded();
		if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
//			urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
		}
	}
	if (!sel.isEmpty()) {
		updateDragSelection(0, 0, false);
		historyWidget->noSelectingScroll();

		QDrag *drag = new QDrag(App::wnd());
		QMimeData *mimeData = new QMimeData;

		mimeData->setText(sel);
		if (!urls.isEmpty()) mimeData->setUrls(urls);
		if (uponSelected && !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel && cWideMode()) {
			mimeData->setData(qsl("application/x-td-forward-selected"), "1");
		}
		drag->setMimeData(mimeData);
		drag->exec(Qt::CopyAction);
		return;
	} else {
		HistoryItem *pressedLnkItem = App::pressedLinkItem(), *pressedItem = App::pressedItem();
		QLatin1String lnkType = (textlnkDown() && pressedLnkItem) ? textlnkDown()->type() : qstr("");
		bool lnkPhoto = (lnkType == qstr("PhotoLink")),
			lnkVideo = (lnkType == qstr("VideoOpenLink")),
			lnkAudio = (lnkType == qstr("AudioOpenLink")),
			lnkDocument = (lnkType == qstr("DocumentOpenLink")),
			lnkContact = (lnkType == qstr("PeerLink") && dynamic_cast<HistoryContact*>(pressedLnkItem->getMedia())),
			dragSticker = dynamic_cast<HistorySticker*>(pressedItem ? pressedItem->getMedia() : 0),
			dragByDate = (_dragCursorState == HistoryInDateCursorState);
		if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument || lnkContact || dragSticker || dragByDate) {
			QDrag *drag = new QDrag(App::wnd());
			QMimeData *mimeData = new QMimeData;

			if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument || lnkContact) {
				mimeData->setData(qsl("application/x-td-forward-pressed-link"), "1");
			} else {
				mimeData->setData(qsl("application/x-td-forward-pressed"), "1");
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
			return;
		}
	}
}

void HistoryList::itemRemoved(HistoryItem *item) {
	SelectedItems::iterator i = _selected.find(item);
	if (i != _selected.cend()) {
		_selected.erase(i);
		historyWidget->updateTopBarSelection();
	}

	if (_dragAction == NoDrag) return;
	
	if (_dragItem == item) {
		dragActionCancel();
	}

	onUpdateSelected();

	if (_dragSelFrom == item) _dragSelFrom = 0;
	if (_dragSelTo == item) _dragSelTo = 0;
	updateDragSelection(_dragSelFrom, _dragSelTo, _dragSelecting, true);
}

void HistoryList::itemReplaced(HistoryItem *oldItem, HistoryItem *newItem) {
	if (_dragItem == oldItem) _dragItem = newItem;

	SelectedItems::iterator i = _selected.find(oldItem);
	if (i != _selected.cend()) {
		uint32 v = i.value();
		_selected.erase(i);
		_selected.insert(newItem, v);
	}

	if (_dragSelFrom == oldItem) _dragSelFrom = newItem;
	if (_dragSelTo == oldItem) _dragSelTo = newItem;
}

void HistoryList::dragActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	TextLinkPtr needClick;

	dragActionUpdate(screenPos);

	if (textlnkOver()) {
		if (textlnkDown() == textlnkOver() && _dragAction != Dragging) {
			needClick = textlnkDown();

			QLatin1String lnkType = needClick->type();
			bool lnkPhoto = (lnkType == qstr("PhotoLink")),
				lnkVideo = (lnkType == qstr("VideoOpenLink")),
				lnkAudio = (lnkType == qstr("AudioOpenLink")),
				lnkDocument = (lnkType == qstr("DocumentOpenLink")),
				lnkContact = (lnkType == qstr("PeerLink") && dynamic_cast<HistoryContact*>(App::pressedLinkItem() ? App::pressedLinkItem()->getMedia() : 0));
			if (_dragAction == PrepareDrag && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel && button != Qt::RightButton) {
				if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument || lnkContact) {
					needClick = TextLinkPtr();
				}
			}
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
	if (App::pressedItem()) {
		updateMsg(App::pressedItem());
		App::pressedItem(0);
	}

	_wasSelectedText = false;

	if (needClick) {
		DEBUG_LOG(("Clicked link: %1 (%2) %3").arg(needClick->text()).arg(needClick->readable()).arg(needClick->encoded()));
		needClick->onClick(button);
		dragActionCancel();
		return;
	}
	if (_dragAction == PrepareSelect && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i == _selected.cend() && !_dragItem->serviceMsg() && _dragItem->id > 0) {
			if (_selected.size() < MaxSelectedItems) {
				if (!_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
					_selected.clear();
				}
				_selected.insert(_dragItem, FullItemSel);
			}
		} else {
			_selected.erase(i);
		}
		updateMsg(_dragItem);
	} else if (_dragAction == PrepareDrag && !_dragWasInactive && button != Qt::RightButton) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i != _selected.cend() && i.value() == FullItemSel) {
			_selected.erase(i);
			updateMsg(_dragItem);
		} else if (i == _selected.cend() && !_dragItem->serviceMsg() && _dragItem->id > 0 && !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.insert(_dragItem, FullItemSel);
				updateMsg(_dragItem);
			}
		} else {
			_selected.clear();
			parentWidget()->update();
		}
	} else if (_dragAction == Selecting) {
		if (_dragSelFrom && _dragSelTo) {
			applyDragSelection();
			_dragSelFrom = _dragSelTo = 0;
		} else if (!_selected.isEmpty() && !_dragWasInactive) {
			uint32 sel = _selected.cbegin().value();
			if (sel != FullItemSel && (sel & 0xFFFF) == ((sel >> 16) & 0xFFFF)) {
				_selected.clear();
				App::wnd()->setInnerFocus();
			}
		}
	}
	_dragAction = NoDrag;
	_dragSelType = TextSelectLetters;
	historyWidget->noSelectingScroll();
	historyWidget->updateTopBarSelection();
}

void HistoryList::mouseReleaseEvent(QMouseEvent *e) {
	dragActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void HistoryList::mouseDoubleClickEvent(QMouseEvent *e) {
	if (!hist) return;

	if (((_dragAction == Selecting && !_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) || (_dragAction == NoDrag && (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel))) && _dragSelType == TextSelectLetters && _dragItem) {
		bool afterDragSymbol, uponSelected;
		uint16 symbol;
		_dragItem->getSymbol(symbol, afterDragSymbol, uponSelected, _dragStartPos.x(), _dragStartPos.y());
		if (uponSelected) {
			_dragSymbol = symbol;
			_dragSelType = TextSelectWords;
			if (_dragAction == NoDrag) {
				_dragAction = Selecting;
				uint32 selStatus = (symbol << 16) | symbol;
				if (!_selected.isEmpty()) {
					updateMsg(_selected.cbegin().key());
					_selected.clear();
				}
				_selected.insert(_dragItem, selStatus);
			}
			mouseMoveEvent(e);

	        _trippleClickPoint = e->globalPos();
	        _trippleClickTimer.start(QApplication::doubleClickInterval());
		}
	} else {
		mousePressEvent(e);
	}
}

void HistoryList::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		dragActionUpdate(e->globalPos());
	}

	// -2 - has full selected items, but not over, -1 - has selection, but no over, 0 - no selection, 1 - over text, 2 - over full selected items
	int32 isUponSelected = 0, hasSelected = 0;;
	if (!_selected.isEmpty()) {
		isUponSelected = -1;
		if (_selected.cbegin().value() == FullItemSel) {
			hasSelected = 2;
			if (App::hoveredItem() && _selected.constFind(App::hoveredItem()) != _selected.cend()) {
				isUponSelected = 2;
			} else {
				isUponSelected = -2;
			}
		} else {
			uint16 symbol, selFrom = (_selected.cbegin().value() >> 16) & 0xFFFF, selTo = _selected.cbegin().value() & 0xFFFF;
			hasSelected = (selTo > selFrom) ? 1 : 0;
			if (_dragItem && _dragItem == App::hoveredItem()) {
				QPoint mousePos(mapMouseToItem(mapFromGlobal(_dragPos), _dragItem));
				bool afterDragSymbol, uponSymbol;
				_dragItem->getSymbol(symbol, afterDragSymbol, uponSymbol, mousePos.x(), mousePos.y());
				if (uponSymbol && symbol >= selFrom && symbol < selTo) {
					isUponSelected = 1;
				}
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_contextMenuLnk = textlnkOver();
	HistoryItem *item = App::hoveredItem() ? App::hoveredItem() : App::hoveredLinkItem();
	PhotoLink *lnkPhoto = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
    VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
    AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
    DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument) {
		_menu = new ContextMenu(historyWidget);
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
		}
		if (item && item->id > 0 && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(lang(lng_context_reply_msg), historyWidget, SLOT(onReplyToMessage()));
		}
		if (lnkPhoto) {
			_menu->addAction(lang(lng_context_open_image), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_save_image), this, SLOT(saveContextImage()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_image), this, SLOT(copyContextImage()))->setEnabled(true);
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
			_menu->addAction(lang(lng_context_forward_selected), historyWidget, SLOT(onForwardSelected()));
			_menu->addAction(lang(lng_context_delete_selected), historyWidget, SLOT(onDeleteSelected()));
			_menu->addAction(lang(lng_context_clear_selection), historyWidget, SLOT(onClearSelected()));
		} else if (App::hoveredLinkItem()) {
			if (isUponSelected != -2) {
				if (dynamic_cast<HistoryMessage*>(App::hoveredLinkItem()) && App::hoveredLinkItem()->id > 0) {
					_menu->addAction(lang(lng_context_forward_msg), historyWidget, SLOT(forwardMessage()))->setEnabled(true);
				}
				_menu->addAction(lang(lng_context_delete_msg), historyWidget, SLOT(deleteMessage()))->setEnabled(true);
			}
			if (App::hoveredLinkItem()->id > 0) {
				_menu->addAction(lang(lng_context_select_msg), historyWidget, SLOT(selectMessage()))->setEnabled(true);
			}
			App::contextItem(App::hoveredLinkItem());
		}
	} else { // maybe cursor on some text history item?
		bool canDelete = (item && item->itemType() == HistoryItem::MsgType);
		bool canForward = canDelete && (item->id > 0) && !item->serviceMsg();

		HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
		HistoryServiceMsg *srv = dynamic_cast<HistoryServiceMsg*>(item);

		if (isUponSelected > 0) {
			if (!_menu) _menu = new ContextMenu(this);
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
			if (item && item->id > 0 && isUponSelected != 2) {
				_menu->addAction(lang(lng_context_reply_msg), historyWidget, SLOT(onReplyToMessage()));
			}
		} else {
			if (item && item->id > 0 && isUponSelected != -2) {
				if (!_menu) _menu = new ContextMenu(this);
				_menu->addAction(lang(lng_context_reply_msg), historyWidget, SLOT(onReplyToMessage()));
			}
			if (item && !isUponSelected && !_contextMenuLnk) {
				if (HistorySticker *sticker = dynamic_cast<HistorySticker*>(msg ? msg->getMedia() : 0)) {
					DocumentData *doc = sticker->document();
					if (doc && doc->sticker() && doc->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
						if (!_menu) _menu = new ContextMenu(this);
						_menu->addAction(lang(doc->sticker()->setInstalled() ? lng_context_pack_info : lng_context_pack_add), historyWidget, SLOT(onStickerPackInfo()));
					}
				}
				QString contextMenuText = item->selectedText(FullItemSel);
				if (!contextMenuText.isEmpty() && (!msg || !msg->getMedia() || msg->getMedia()->type() != MediaTypeSticker)) {
					if (!_menu) _menu = new ContextMenu(this);
					_menu->addAction(lang(lng_context_copy_text), this, SLOT(copyContextText()))->setEnabled(true);
				}
			}
		}

		if (_contextMenuLnk && dynamic_cast<TextLink*>(_contextMenuLnk.data())) {
			if (!_menu) _menu = new ContextMenu(historyWidget);
			_menu->addAction(lang(lng_context_open_link), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_link), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (_contextMenuLnk && dynamic_cast<EmailLink*>(_contextMenuLnk.data())) {
			if (!_menu) _menu = new ContextMenu(historyWidget);
			_menu->addAction(lang(lng_context_open_email), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_email), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (_contextMenuLnk && dynamic_cast<MentionLink*>(_contextMenuLnk.data())) {
			if (!_menu) _menu = new ContextMenu(historyWidget);
			_menu->addAction(lang(lng_context_open_mention), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_mention), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (_contextMenuLnk && dynamic_cast<HashtagLink*>(_contextMenuLnk.data())) {
			if (!_menu) _menu = new ContextMenu(historyWidget);
			_menu->addAction(lang(lng_context_open_hashtag), this, SLOT(openContextUrl()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_hashtag), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else {
		}
		if (isUponSelected > 1) {
			if (!_menu) _menu = new ContextMenu(this);
			_menu->addAction(lang(lng_context_forward_selected), historyWidget, SLOT(onForwardSelected()));
			_menu->addAction(lang(lng_context_delete_selected), historyWidget, SLOT(onDeleteSelected()));
			_menu->addAction(lang(lng_context_clear_selection), historyWidget, SLOT(onClearSelected()));
		} else if (item && ((isUponSelected != -2 && (canForward || canDelete)) || item->id > 0)) {
			if (!_menu) _menu = new ContextMenu(this);
			if (isUponSelected != -2) {
				if (canForward) {
					_menu->addAction(lang(lng_context_forward_msg), historyWidget, SLOT(forwardMessage()))->setEnabled(true);
				}

				if (canDelete) {
					_menu->addAction(lang((msg && msg->uploading()) ? lng_context_cancel_upload : lng_context_delete_msg), historyWidget, SLOT(deleteMessage()))->setEnabled(true);
				}
			}
			if (item->id > 0) {
				_menu->addAction(lang(lng_context_select_msg), historyWidget, SLOT(selectMessage()))->setEnabled(true);
			}
		} else {
			if (App::mousedItem() && App::mousedItem()->itemType() == HistoryItem::MsgType && App::mousedItem()->id > 0) {
				if (!_menu) _menu = new ContextMenu(this);
				_menu->addAction(lang(lng_context_select_msg), historyWidget, SLOT(selectMessage()))->setEnabled(true);
				item = App::mousedItem();
			}
		}
		App::contextItem(item);
	}

	if (_menu) {
		_menu->deleteOnHide();
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void HistoryList::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
}

void HistoryList::copySelectedText() {
	QString sel = getSelectedText();
	if (!sel.isEmpty()) {
		QApplication::clipboard()->setText(sel);
	}
}

void HistoryList::openContextUrl() {
	HistoryItem *was = App::hoveredLinkItem();
	App::hoveredLinkItem(App::contextItem());
	_contextMenuLnk->onClick(Qt::LeftButton);
	App::hoveredLinkItem(was);
}

void HistoryList::copyContextUrl() {
	QString enc = _contextMenuLnk->encoded();
	if (!enc.isEmpty()) {
		QApplication::clipboard()->setText(enc);
	}
}

void HistoryList::saveContextImage() {
    PhotoLink *lnk = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
	if (!lnk) return;
	
	PhotoData *photo = lnk->photo();
	if (!photo || !photo->date || !photo->full->loaded()) return;

	QString file;
	if (filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")))) {
		if (!file.isEmpty()) {
			photo->full->pix().toImage().save(file, "JPG");
		}
	}
}

void HistoryList::copyContextImage() {
    PhotoLink *lnk = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
	if (!lnk) return;
	
	PhotoData *photo = lnk->photo();
	if (!photo || !photo->date || !photo->full->loaded()) return;

	QApplication::clipboard()->setPixmap(photo->full->pix());
}

void HistoryList::cancelContextDownload() {
    VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
    AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
    DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	mtpFileLoader *loader = lnkVideo ? lnkVideo->video()->loader : (lnkAudio ? lnkAudio->audio()->loader : (lnkDocument ? lnkDocument->document()->loader : 0));
	if (loader) loader->cancel();
}

void HistoryList::showContextInFolder() {
    VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
    AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
    DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	QString already = lnkVideo ? lnkVideo->video()->already(true) : (lnkAudio ? lnkAudio->audio()->already(true) : (lnkDocument ? lnkDocument->document()->already(true) : QString()));
	if (!already.isEmpty()) psShowInFolder(already);
}

void HistoryList::openContextFile() {
    VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
    AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
    DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) VideoOpenLink(lnkVideo->video()).onClick(Qt::LeftButton);
	if (lnkAudio) AudioOpenLink(lnkAudio->audio()).onClick(Qt::LeftButton);
	if (lnkDocument) DocumentOpenLink(lnkDocument->document()).onClick(Qt::LeftButton);
}

void HistoryList::saveContextFile() {
    VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
    AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
    DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) VideoSaveLink::doSave(lnkVideo->video(), true);
	if (lnkAudio) AudioSaveLink::doSave(lnkAudio->audio(), true);
	if (lnkDocument) DocumentSaveLink::doSave(lnkDocument->document(), true);
}

void HistoryList::copyContextText() {
	HistoryItem *item = App::contextItem();
	if (item && item->itemType() != HistoryItem::MsgType) {
		item = 0;
	}

	if (!item) return;

	QString contextMenuText = item->selectedText(FullItemSel);
	if (!contextMenuText.isEmpty()) {
		QApplication::clipboard()->setText(contextMenuText);
	}
}

void HistoryList::resizeEvent(QResizeEvent *e) {
	onUpdateSelected();
}

QString HistoryList::getSelectedText() const {
	SelectedItems sel = _selected;

	if (_dragAction == Selecting && _dragSelFrom && _dragSelTo) {
		applyDragSelection(&sel);
	}

	if (sel.isEmpty()) return QString();
	if (sel.cbegin().value() != FullItemSel) {
		return sel.cbegin().key()->selectedText(sel.cbegin().value());
	}

	int32 fullSize = 0;
	QString timeFormat(qsl(", [dd.MM.yy hh:mm]\n"));
	QMap<int32, QString> texts;
	for (SelectedItems::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		HistoryItem *item = i.key();
		if (item->detached()) continue;

		QString text, sel = item->selectedText(FullItemSel), time = item->date.toString(timeFormat);
		int32 size = item->from()->name.size() + time.size() + sel.size();
		text.reserve(size);
		texts.insert(item->y + item->block()->y, text.append(item->from()->name).append(time).append(sel));
		fullSize += size;
	}

	QString result, sep(qsl("\n\n"));
	result.reserve(fullSize + (texts.size() - 1) * 2);
	for (QMap<int32, QString>::const_iterator i = texts.cbegin(), e = texts.cend(); i != e; ++i) {
		result.append(i.value());
		if (i + 1 != e) {
			result.append(sep);
		}
	}
	return result;
}

void HistoryList::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		historyWidget->onListEscapePressed();
	} else if (e == QKeySequence::Copy && !_selected.isEmpty()) {
		copySelectedText();
	} else if (e == QKeySequence::Delete) {
		historyWidget->onDeleteSelected();
	} else {
		e->ignore();
	}
}

int32 HistoryList::recountHeight(HistoryItem *resizedItem) {
	int32 st = hist->lastScrollTop;

	int32 ph = scrollArea->height(), minadd = 0;
	int32 wasYSkip = ph - (hist->height + st::historyPadding);
	if (botInfo && !botInfo->text.isEmpty()) {
		minadd = st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + botDescHeight;
	}
	if (wasYSkip < minadd) wasYSkip = minadd;

	hist->geomResize(scrollArea->width(), &st, resizedItem);
	updateBotInfo(false);
	if (botInfo && !botInfo->text.isEmpty()) {
		int32 tw = scrollArea->width() - st::msgMargin.left() - st::msgMargin.right();
		if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
		tw -= st::msgPadding.left() + st::msgPadding.right();
		int32 mw = qMax(botInfo->text.maxWidth(), st::msgNameFont->m.width(lang(lng_bot_description)));
		if (tw > mw) tw = mw;

		botDescWidth = tw;
		botDescHeight = botInfo->text.countHeight(botDescWidth);

		int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + botDescHeight + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descAtX = (scrollArea->width() - botDescWidth) / 2 - st::msgPadding.left();
		int32 descAtY = qMin(ySkip - descH, (scrollArea->height() - descH) / 2) + st::msgMargin.top();

		botDescRect = QRect(descAtX, descAtY, botDescWidth + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
	} else {
		botDescWidth = botDescHeight = 0;
		botDescRect = QRect();
	}

	int32 newYSkip = ph - (hist->height + st::historyPadding);
	if (botInfo && !botInfo->text.isEmpty()) {
		minadd = st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + botDescHeight;
	}
	if (newYSkip < minadd) newYSkip = minadd;

	return st + (newYSkip - wasYSkip);
}

void HistoryList::updateBotInfo(bool recount) {
	int32 newh = 0;
	if (botInfo && !botInfo->description.isEmpty()) {
		if (botInfo->text.isEmpty()) {
			botInfo->text.setText(st::msgFont, botInfo->description, _historyBotOptions);
			if (recount) {
				int32 tw = scrollArea->width() - st::msgMargin.left() - st::msgMargin.right();
				if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
				tw -= st::msgPadding.left() + st::msgPadding.right();
				int32 mw = qMax(botInfo->text.maxWidth(), st::msgNameFont->m.width(lang(lng_bot_description)));
				if (tw > mw) tw = mw;

				botDescWidth = tw;
				newh = botInfo->text.countHeight(botDescWidth);
			}
		} else if (recount) {
			newh = botDescHeight;
		}
	}
	if (recount) {
		if (botDescHeight != newh) {
			botDescHeight = newh;
			updateSize();
		}
		if (botDescHeight > 0) {
			int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + botDescHeight + st::msgPadding.bottom() + st::msgMargin.bottom();
			int32 descAtX = (scrollArea->width() - botDescWidth) / 2 - st::msgPadding.left();
			int32 descAtY = qMin(ySkip - descH, (scrollArea->height() - descH) / 2) + st::msgMargin.top();

			botDescRect = QRect(descAtX, descAtY, botDescWidth + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
		} else {
			botDescWidth = 0;
			botDescRect = QRect();
		}
	}
}

bool HistoryList::wasSelectedText() const {
	return _wasSelectedText;
}

void HistoryList::setFirstLoading(bool loading) {
	_firstLoading = loading;
	update();
}

void HistoryList::updateSize() {
	int32 ph = scrollArea->height(), minadd = 0;
	int32 newYSkip = ph - (hist->height + st::historyPadding);
	if (botInfo && !botInfo->text.isEmpty()) {
		minadd = st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + botDescHeight;
	}
	if (newYSkip < minadd) newYSkip = minadd;

	if (botDescHeight > 0) {
		int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + botDescHeight + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descAtX = (scrollArea->width() - botDescWidth) / 2 - st::msgPadding.left();
		int32 descAtY = qMin(newYSkip - descH, (scrollArea->height() - descH) / 2) + st::msgMargin.top();

		botDescRect = QRect(descAtX, descAtY, botDescWidth + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
	}

	int32 yAdded = newYSkip - ySkip;
	ySkip = newYSkip;

	int32 nh = hist->height + st::historyPadding + ySkip;
	if (width() != scrollArea->width() || height() != nh) {
		resize(scrollArea->width(), nh);

		dragActionUpdate(QCursor::pos());
	} else {
		update();
	}
}

void HistoryList::enterEvent(QEvent *e) {
	return QWidget::enterEvent(e);
}

void HistoryList::leaveEvent(QEvent *e) {
	if (textlnkOver()) {
		updateMsg(App::hoveredItem());
		updateMsg(App::hoveredLinkItem());
		textlnkOver(TextLinkPtr());
		App::hoveredLinkItem(0);
		App::hoveredItem(0);
		if (!textlnkDown() && _cursor != style::cur_default) {
			_cursor = style::cur_default;
			setCursor(_cursor);
		}
	}
	return QWidget::leaveEvent(e);
}

HistoryList::~HistoryList() {
	delete _menu;
	_dragAction = NoDrag;
}

void HistoryList::adjustCurrent(int32 y) {
	if (hist->isEmpty()) return;
	if (currentBlock >= hist->size()) {
		currentBlock = hist->size() - 1;
		currentItem = 0;
	}

	while ((*hist)[currentBlock]->y + ySkip > y && currentBlock > 0) {
		--currentBlock;
		currentItem = 0;
	}
	while ((*hist)[currentBlock]->y + (*hist)[currentBlock]->height + ySkip <= y && currentBlock + 1 < hist->size()) {
		++currentBlock;
		currentItem = 0;
	}
	HistoryBlock *block = (*hist)[currentBlock];
	if (currentItem >= block->size()) {
		currentItem = block->size() - 1;
	}
	int32 by = block->y;
	while ((*block)[currentItem]->y + by + ySkip > y && currentItem > 0) {
		--currentItem;
	}
	while ((*block)[currentItem]->y + (*block)[currentItem]->height() + by + ySkip <= y && currentItem + 1 < block->size()) {
		++currentItem;
	}
}

HistoryItem *HistoryList::prevItem(HistoryItem *item) {
	if (!item) return 0;
	HistoryBlock *block = item->block();
	int32 blockIndex = hist->indexOf(block), itemIndex = block->indexOf(item);
	if (blockIndex < 0  || itemIndex < 0) return 0;
	if (itemIndex > 0) {
		return (*block)[itemIndex - 1];
	}
	if (blockIndex > 0) {
		return *((*hist)[blockIndex - 1]->cend() - 1);
	}
	return 0;
}

HistoryItem *HistoryList::nextItem(HistoryItem *item) {
	if (!item) return 0;
	HistoryBlock *block = item->block();
	int32 blockIndex = hist->indexOf(block), itemIndex = block->indexOf(item);
	if (blockIndex < 0  || itemIndex < 0) return 0;
	if (itemIndex + 1 < block->size()) {
		return (*block)[itemIndex + 1];
	}
	if (blockIndex + 1 < hist->size()) {
		return *(*hist)[blockIndex + 1]->cbegin();
	}
	return 0;
}

bool HistoryList::canCopySelected() const {
	return !_selected.isEmpty();
}

bool HistoryList::canDeleteSelected() const {
	return !_selected.isEmpty() && (_selected.cbegin().value() == FullItemSel);
}

void HistoryList::getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const {
	selectedForForward = selectedForDelete = 0;
	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		if (i.key()->itemType() == HistoryItem::MsgType && i.value() == FullItemSel) {
			++selectedForDelete;
			if (!i.key()->serviceMsg() && i.key()->id > 0) {
				++selectedForForward;
			}
		}
	}
	if (!selectedForDelete && !selectedForForward && !_selected.isEmpty()) { // text selection
		selectedForForward = -1;
	}
}

void HistoryList::clearSelectedItems(bool onlyTextSelection) {
	if (!_selected.isEmpty() && (!onlyTextSelection || _selected.cbegin().value() != FullItemSel)) {
		_selected.clear();
		historyWidget->updateTopBarSelection();
		historyWidget->update();
	}
}

void HistoryList::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	if (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel) return;

	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		HistoryItem *item = i.key();
		if (dynamic_cast<HistoryMessage*>(item) && item->id > 0) {
			sel.insert(item->id, item);
		}
	}
}

void HistoryList::selectItem(HistoryItem *item) {
	if (!_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
		_selected.clear();
	} else if (_selected.size() == MaxSelectedItems && _selected.constFind(item) == _selected.cend()) {
		return;
	}
	_selected.insert(item, FullItemSel);
	historyWidget->updateTopBarSelection();
	historyWidget->update();
}

void HistoryList::onTouchSelect() {
	_touchSelect = true;
	dragActionStart(_touchPos);
}

void HistoryList::onUpdateSelected() {
	if (!hist) return;

	QPoint mousePos(mapFromGlobal(_dragPos));
	QPoint point(historyWidget->clampMousePosition(mousePos));

	HistoryBlock *block = 0;
	HistoryItem *item = 0;
	QPoint m;
	if (!hist->isEmpty()) {
		adjustCurrent(point.y());

		block = (*hist)[currentBlock];
		item = (*block)[currentItem];

		App::mousedItem(item);
		m = mapMouseToItem(point, item);
		if (item->hasPoint(m.x(), m.y())) {
			updateMsg(App::hoveredItem());
			App::hoveredItem(item);
			updateMsg(App::hoveredItem());
		} else if (App::hoveredItem()) {
			updateMsg(App::hoveredItem());
			App::hoveredItem(0);
		}
	}
	if (_dragItem && _dragItem->detached()) {
		dragActionCancel();
	}

	Qt::CursorShape cur = style::cur_default;
	HistoryCursorState cursorState = HistoryDefaultCursorState;
	bool lnkChanged = false, lnkInDesc = false;

	TextLinkPtr lnk;
	if (point.y() < ySkip) {
		if (botInfo && !botInfo->text.isEmpty() && botDescHeight > 0) {
			bool inText = false;
			botInfo->text.getState(lnk, inText, point.x() - botDescRect.left() - st::msgPadding.left(), point.y() - botDescRect.top() - st::msgPadding.top() - st::botDescSkip - st::msgNameFont->height, botDescWidth);
			cursorState = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
			lnkInDesc = true;
		}
	} else if (item) {
		item->getState(lnk, cursorState, m.x(), m.y());
	}
	if (lnk != textlnkOver()) {
		lnkChanged = true;
		if (textlnkOver()) {
			if (App::hoveredLinkItem()) {
				updateMsg(App::hoveredLinkItem());
			} else {
				update(botDescRect);
			}
		}
		textlnkOver(lnk);
		QToolTip::hideText();
		App::hoveredLinkItem((lnk && !lnkInDesc) ? item : 0);
		if (textlnkOver()) {
			if (App::hoveredLinkItem()) {
				updateMsg(App::hoveredLinkItem());
			} else {
				update(botDescRect);
			}
		}
	}
	if (lnk || cursorState == HistoryInDateCursorState) {
		linkTipTimer.start(1000);
	}
	if (_dragCursorState == HistoryInDateCursorState && cursorState != HistoryInDateCursorState) {
		QToolTip::hideText();
	}

	if (_dragAction == NoDrag) {
		_dragCursorState = cursorState;
		if (lnk) {
			cur = style::cur_pointer;
		} else if (_dragCursorState == HistoryInTextCursorState && (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel)) {
			cur = style::cur_text;
		} else if (_dragCursorState == HistoryInDateCursorState) {
//			cur = style::cur_cross;
		}
	} else if (item) {		
		if (item != _dragItem || (m - _dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
			if (_dragAction == PrepareDrag) {
				_dragAction = Dragging;
				QTimer::singleShot(1, this, SLOT(onDragExec()));
			} else if (_dragAction == PrepareSelect) {
				_dragAction = Selecting;
			}
		}
		cur = textlnkDown() ? style::cur_pointer : style::cur_default;
		if (_dragAction == Selecting) {
			if (item == _dragItem && item == App::hoveredItem() && !_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
				bool afterSymbol, uponSymbol;
				uint16 second;
				_dragItem->getSymbol(second, afterSymbol, uponSymbol, m.x(), m.y());
				if (afterSymbol && _dragSelType == TextSelectLetters) ++second;
				uint32 selState = _dragItem->adjustSelection(qMin(second, _dragSymbol), qMax(second, _dragSymbol), _dragSelType);
				_selected[_dragItem] = selState;
				if (!_wasSelectedText && (selState == FullItemSel || (selState & 0xFFFF) != ((selState >> 16) & 0xFFFF))) {
					_wasSelectedText = true;
					setFocus();
				}
				updateDragSelection(0, 0, false);
			} else {
				bool selectingDown = (_dragItem->block()->y < item->block()->y) || ((_dragItem->block() == item->block()) && (_dragItem->y < item->y || (_dragItem == item && _dragStartPos.y() < m.y())));
				HistoryItem *dragSelFrom = _dragItem, *dragSelTo = item;
				if (!dragSelFrom->hasPoint(_dragStartPos.x(), _dragStartPos.y())) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_dragStartPos.y() >= dragSelFrom->height() - st::msgMargin.bottom() || ((item == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelFrom);
						}
					} else {
						if (_dragStartPos.y() < st::msgMargin.top() || ((item == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : prevItem(dragSelFrom);
						}
					}
				}
				if (_dragItem != item) { // maybe exclude dragSelTo
					if (selectingDown) {
						if (m.y() < st::msgMargin.top()) {
							dragSelTo = (dragSelFrom == dragSelTo) ? 0 : prevItem(dragSelTo);
						}
					} else {
						if (m.y() >= dragSelTo->height() - st::msgMargin.bottom()) {
							dragSelTo = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelTo);
						}
					}
				}
				bool dragSelecting = false;
				HistoryItem *dragFirstAffected = dragSelFrom;
				while (dragFirstAffected && (dragFirstAffected->id < 0 || dragFirstAffected->serviceMsg())) {
					dragFirstAffected = (dragFirstAffected == dragSelTo) ? 0 : (selectingDown ? nextItem(dragFirstAffected) : prevItem(dragFirstAffected));
				}
				if (dragFirstAffected) {
					SelectedItems::const_iterator i = _selected.constFind(dragFirstAffected);
					dragSelecting = (i == _selected.cend() || i.value() != FullItemSel);
				}
				updateDragSelection(dragSelFrom, dragSelTo, dragSelecting);
			}
		} else if (_dragAction == Dragging) {
		}

		if (textlnkDown()) {
			cur = style::cur_pointer;
		} else if (_dragAction == Selecting && !_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
			if (!_dragSelFrom || !_dragSelTo) {
				cur = style::cur_text;
			}
		}
	}
	if (_dragAction == Selecting) {
		historyWidget->checkSelectingScroll(mousePos);
	} else {
		updateDragSelection(0, 0, false);
		historyWidget->noSelectingScroll();
	}

	if (lnkChanged || cur != _cursor) {
		setCursor(_cursor = cur);
	}
}

void HistoryList::updateDragSelection(HistoryItem *dragSelFrom, HistoryItem *dragSelTo, bool dragSelecting, bool force) {
	if (_dragSelFrom != dragSelFrom || _dragSelTo != dragSelTo || _dragSelecting != dragSelecting) {
		_dragSelFrom = dragSelFrom;
		_dragSelTo = dragSelTo;
		if (_dragSelFrom && _dragSelTo && _dragSelFrom->y + _dragSelFrom->block()->y > _dragSelTo->y + _dragSelTo->block()->y) {
			qSwap(_dragSelFrom, _dragSelTo);
		}
		_dragSelecting = dragSelecting;
		if (!_wasSelectedText && _dragSelFrom && _dragSelTo && _dragSelecting) {
			_wasSelectedText = true;
			setFocus();
		}
		force = true;
	}
	if (!force) return;
	
	parentWidget()->update();
}

void HistoryList::applyDragSelection() {
	applyDragSelection(&_selected);
}

void HistoryList::applyDragSelection(SelectedItems *toItems) const {
	if (!toItems->isEmpty() && toItems->cbegin().value() != FullItemSel) {
		toItems->clear();
	}

	int32 fromy = _dragSelFrom->y + _dragSelFrom->block()->y, toy = _dragSelTo->y + _dragSelTo->block()->y + _dragSelTo->height();
	if (_dragSelecting) {
		int32 fromblock = hist->indexOf(_dragSelFrom->block()), fromitem = _dragSelFrom->block()->indexOf(_dragSelFrom);
		int32 toblock = hist->indexOf(_dragSelTo->block()), toitem = _dragSelTo->block()->indexOf(_dragSelTo);
		if (fromblock >= 0 && fromitem >= 0 && toblock >= 0 && toitem >= 0) {
			for (; fromblock <= toblock; ++fromblock) {
				HistoryBlock *block = (*hist)[fromblock];
				for (int32 cnt = (fromblock < toblock) ? block->size() : (toitem + 1); fromitem < cnt; ++fromitem) {
					HistoryItem *item = (*block)[fromitem];
					SelectedItems::iterator i = toItems->find(item);
					if (item->id > 0 && !item->serviceMsg()) {
						if (i == toItems->cend()) {
							if (toItems->size() >= MaxSelectedItems) break;
							toItems->insert(item, FullItemSel);
						} else if (i.value() != FullItemSel) {
							*i = FullItemSel;
						}
					} else {
						if (i != toItems->cend()) {
							toItems->erase(i);
						}
					}
				}
				if (toItems->size() >= MaxSelectedItems) break;
				fromitem = 0;
			}
		}
	} else {
		for (SelectedItems::iterator i = toItems->begin(); i != toItems->cend();) {
			int32 iy = i.key()->y + i.key()->block()->y;
			if (iy >= fromy && iy < toy) {
				i = toItems->erase(i);
			} else {
				++i;
			}
		}
	}
}

void HistoryList::showLinkTip() {
	TextLinkPtr lnk = textlnkOver();
	int32 dd = QApplication::startDragDistance();
	QPoint dp(mapFromGlobal(_dragPos));
	QRect r(dp.x() - dd, dp.y() - dd, 2 * dd, 2 * dd);
	if (lnk && !lnk->fullDisplayed()) {
		QToolTip::showText(_dragPos, lnk->readable(), this, r);
	} else if (_dragCursorState == HistoryInDateCursorState && _dragAction == NoDrag) {
		if (App::hoveredItem()) {
			QToolTip::showText(_dragPos, App::hoveredItem()->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat)), this, r);
		}
	}
}

void HistoryList::onParentGeometryChanged() {
	bool needToUpdate = (_dragAction != NoDrag || _touchScroll || rect().contains(mapFromGlobal(QCursor::pos())));
	if (needToUpdate) {
		dragActionUpdate(QCursor::pos());
	}
}

MessageField::MessageField(HistoryWidget *history, const style::flatTextarea &st, const QString &ph, const QString &val) : FlatTextarea(history, st, ph, val), history(history), _maxHeight(st::maxFieldHeight) {
	connect(this, SIGNAL(changed()), this, SLOT(onChange()));
}

void MessageField::setMaxHeight(int32 maxHeight) {
	_maxHeight = maxHeight;
	int newh = ceil(document()->size().height()) + 2 * fakeMargin(), minh = st::btnSend.height - 2 * st::sendPadding;
	if (newh > _maxHeight) {
		newh = _maxHeight;
	} else if (newh < minh) {
		newh = minh;
	}
	if (height() != newh) {
		resize(width(), newh);
	}
}

void MessageField::onChange() {
	int newh = ceil(document()->size().height()) + 2 * fakeMargin(), minh = st::btnSend.height - 2 * st::sendPadding;
	if (newh > _maxHeight) {
		newh = _maxHeight;
	} else if (newh < minh) {
		newh = minh;
	}
	
	if (height() != newh) {
		resize(width(), newh);
		emit resized();
	}
}

void MessageField::onEmojiInsert(EmojiPtr emoji) {
	insertEmoji(emoji, textCursor());
}

void MessageField::dropEvent(QDropEvent *e) {
	FlatTextarea::dropEvent(e);
	if (e->isAccepted()) {
		App::wnd()->activateWindow();
	}
}

void MessageField::resizeEvent(QResizeEvent *e) {
	FlatTextarea::resizeEvent(e);
	onChange();
}

bool MessageField::canInsertFromMimeData(const QMimeData *source) const {
	if (source->hasImage()) return true;
	return FlatTextarea::canInsertFromMimeData(source);
}

void MessageField::insertFromMimeData(const QMimeData *source) {
	if (source->hasImage()) {
		QImage img = qvariant_cast<QImage>(source->imageData());
		if (!img.isNull()) {
			history->uploadImage(img, false, source->text());
			return;
		}
	}
	FlatTextarea::insertFromMimeData(source);
}

void MessageField::focusInEvent(QFocusEvent *e) {
	FlatTextarea::focusInEvent(e);
	emit focused();
}

ReportSpamPanel::ReportSpamPanel(HistoryWidget *parent) : TWidget(parent),
_report(this, lang(lng_report_spam), st::reportSpamHide),
_hide(this, lang(lng_report_spam_hide), st::reportSpamHide),
_clear(this, lang(lng_profile_delete_conversation)) {
	resize(parent->width(), _hide.height() + st::titleShadow);

	connect(&_report, SIGNAL(clicked()), this, SIGNAL(reportClicked()));
	connect(&_hide, SIGNAL(clicked()), this, SIGNAL(hideClicked()));
	connect(&_clear, SIGNAL(clicked()), this, SIGNAL(clearClicked()));

	_clear.hide();
}

void ReportSpamPanel::resizeEvent(QResizeEvent *e) {
	_report.resize(width() - (_hide.width() + st::reportSpamSeparator) * 2, _report.height());
	_report.moveToLeft(_hide.width() + st::reportSpamSeparator, 0, width());
	_hide.moveToRight(0, 0, width());
	_clear.move((width() - _clear.width()) / 2, height() - _clear.height() - ((height() - st::msgFont->height - _clear.height()) / 2));
}

void ReportSpamPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(QRect(0, 0, width(), height() - st::titleShadow), st::reportSpamBg->b);
	if (cWideMode()) {
		p.fillRect(st::titleShadow, height() - st::titleShadow, width() - st::titleShadow, st::titleShadow, st::titleShadowColor->b);
	} else {
		p.fillRect(0, height() - st::titleShadow, width(), st::titleShadow, st::titleShadowColor->b);
	}
	if (!_clear.isHidden()) {
		p.setPen(st::black->p);
		p.setFont(st::msgFont->f);
		p.drawText(QRect(_report.x(), (_clear.y() - st::msgFont->height) / 2, _report.width(), st::msgFont->height), lang(lng_report_spam_thanks), style::al_top);
	}
}

void ReportSpamPanel::setReported(bool reported) {
	if (reported) {
		_report.hide();
		_clear.show();
	} else {
		_report.show();
		_clear.hide();
	}
	update();
}

BotKeyboard::BotKeyboard() : _wasForMsgId(0), _height(0), _maxOuterHeight(0), _maximizeSize(false), _singleUse(false), _forceReply(false),
_sel(-1), _down(-1), _hoverAnim(animFunc(this, &BotKeyboard::hoverStep)), _st(&st::botKbButton) {
	setGeometry(0, 0, _st->margin, _st->margin);
	_height = _st->margin;
	setMouseTracking(true);

	_cmdTipTimer.setSingleShot(true);
	connect(&_cmdTipTimer, SIGNAL(timeout()), this, SLOT(showCommandTip()));
}

void BotKeyboard::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect());
	p.setClipRect(r);
	p.fillRect(r, st::white->b);

	p.setPen(st::botKbColor->p);
	p.setFont(st::botKbFont->f);
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		int32 j = 0, s = _btns.at(i).size();
		for (; j != s; ++j) {
			const Button &btn(_btns.at(i).at(j));
			QRect rect(btn.rect);
			if (rect.top() >= r.bottom()) break;
			if (rect.bottom() < r.top()) continue;

			if (rtl()) rect.moveLeft(width() - rect.left() - rect.width());

			int32 tx = rect.x(), tw = rect.width();
			if (tw > st::botKbFont->elidew + _st->padding * 2) {
				tx += _st->padding;
				tw -= _st->padding * 2;
			} else if (tw > st::botKbFont->elidew) {
				tx += (tw - st::botKbFont->elidew) / 2;
				tw = st::botKbFont->elidew;
			}
			if (_down == i * MatrixRowShift + j) {
				App::roundRect(p, rect, st::botKbDownBg, BotKeyboardDownCorners);
				btn.text.drawElided(p, tx, rect.y() + _st->downTextTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
			} else {
				App::roundRect(p, rect, st::botKbBg, BotKeyboardCorners);
				float64 hover = btn.hover;
				if (hover > 0) {
					p.setOpacity(hover);
					App::roundRect(p, rect, st::botKbOverBg, BotKeyboardOverCorners);
					p.setOpacity(1);
				}
				btn.text.drawElided(p, tx, rect.y() + _st->textTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
			}
		}
		if (j < s) break;
	}
}

void BotKeyboard::resizeEvent(QResizeEvent *e) {
	updateStyle();

	_height = (_btns.size() + 1) * _st->margin + _btns.size() * _st->height;
	if (_maximizeSize) _height = qMax(_height, _maxOuterHeight);
	if (height() != _height) {
		resize(width(), _height);
		return;
	}

	float64 y = _st->margin, btnh = _btns.isEmpty() ? _st->height : (float64(_height - _st->margin) / _btns.size());
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		int32 j = 0, s = _btns.at(i).size();

		float64 widthForText = width() - (s * _st->margin + st::botKbScroll.width + s * 2 * _st->padding), widthOfText = 0.;
		for (; j != s; ++j) {
			Button &btn(_btns[i][j]);
			if (btn.text.isEmpty()) btn.text.setText(st::botKbFont, textOneLine(btn.cmd), _textPlainOptions);
			if (!btn.cwidth) btn.cwidth = btn.cmd.size();
			if (!btn.cwidth) btn.cwidth = 1;
			widthOfText += qMax(btn.text.maxWidth(), 1);
		}

		float64 x = _st->margin, coef = widthForText / widthOfText;
		for (j = 0; j != s; ++j) {
			Button &btn(_btns[i][j]);
			float64 tw = widthForText / float64(s), w = 2 * _st->padding + tw;
			if (w < _st->padding) w = _st->padding;

			btn.rect = QRect(qRound(x), qRound(y), qRound(w), qRound(btnh - _st->margin));
			x += w + _st->margin;

			btn.full = tw >= btn.text.maxWidth();
		}
		y += btnh;
	}
}

void BotKeyboard::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	_down = _sel;
	update();
}

void BotKeyboard::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void BotKeyboard::mouseReleaseEvent(QMouseEvent *e) {
	int32 down = _down;
	_down = -1;

	_lastMousePos = e->globalPos();
	updateSelected();
	if (_sel == down && down >= 0) {
		int row = (down / MatrixRowShift), col = down % MatrixRowShift;
		App::sendBotCommand(_btns.at(row).at(col).cmd, _wasForMsgId);
	}
}

void BotKeyboard::leaveEvent(QEvent *e) {
	_lastMousePos = QPoint(-1, -1);
	updateSelected();
}

bool BotKeyboard::updateMarkup(HistoryItem *to) {
	if (to && to->hasReplyMarkup()) {
		if (_wasForMsgId == to->id) return false;

		_wasForMsgId = to->id;
		clearSelection();
		_btns.clear();
		const ReplyMarkup &markup(App::replyMarkup(to->id));
		_forceReply = markup.flags & MTPDreplyKeyboardMarkup_flag_FORCE_REPLY;
		_maximizeSize = !(markup.flags & MTPDreplyKeyboardMarkup_flag_resize);
		_singleUse = _forceReply || (markup.flags & MTPDreplyKeyboardMarkup_flag_single_use);

		const ReplyMarkup::Commands &commands(markup.commands);
		if (!commands.isEmpty()) {
			int32 i = 0, l = qMin(commands.size(), 512);
			_btns.reserve(l);
			for (; i != l; ++i) {
				const QList<QString> &row(commands.at(i));
				QList<Button> btns;
				int32 j = 0, s = qMin(row.size(), 16);
				btns.reserve(s);
				for (; j != s; ++j) {
					btns.push_back(Button(row.at(j)));
				}
				if (!btns.isEmpty()) _btns.push_back(btns);
			}

			updateStyle();
			_height = (_btns.size() + 1) * _st->margin + _btns.size() * _st->height;
			if (_maximizeSize) _height = qMax(_height, _maxOuterHeight);
			if (height() != _height) {
				resize(width(), _height);
			} else {
				resizeEvent(0);
			}
		}
		return true;
	}
	if (_wasForMsgId) {
		_maximizeSize = _singleUse = _forceReply = false;
		_wasForMsgId = 0;
		clearSelection();
		_btns.clear();
		return true;
	}
	return false;
}

bool BotKeyboard::hasMarkup() const {
	return !_btns.isEmpty();
}

bool BotKeyboard::forceReply() const {
	return _forceReply;
}

bool BotKeyboard::hoverStep(float64 ms) {
	uint64 now = getms();
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		float64 dt = float64(now - i.value()) / st::botKbDuration;
		if (dt >= 1) {
			_btns[row][col].hover = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_btns[row][col].hover = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	update();
	return !_animations.isEmpty();
}

void BotKeyboard::resizeToWidth(int32 width, int32 maxOuterHeight) {
	updateStyle(width);
	_height = (_btns.size() + 1) * _st->margin + _btns.size() * _st->height;
	_maxOuterHeight = maxOuterHeight;

	if (_maximizeSize) _height = qMax(_height, _maxOuterHeight);
	resize(width, _height);
}

bool BotKeyboard::maximizeSize() const {
	return _maximizeSize;
}

bool BotKeyboard::singleUse() const {
	return _singleUse;
}

void BotKeyboard::updateStyle(int32 w) {
	if (w < 0) w = width();
	_st = &st::botKbButton;
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		int32 j = 0, s = _btns.at(i).size();
		int32 widthLeft = w - (s * _st->margin + st::botKbScroll.width + s * 2 * _st->padding);
		for (; j != s; ++j) {
			Button &btn(_btns[i][j]);
			if (btn.text.isEmpty()) btn.text.setText(st::botKbFont, textOneLine(btn.cmd), _textPlainOptions);
			widthLeft -= qMax(btn.text.maxWidth(), 1);
			if (widthLeft < 0) break;
		}
		if (j != s && s > 3) {
			_st = &st::botKbTinyButton;
			break;
		}
	}
}

void BotKeyboard::clearSelection() {
	for (Animations::const_iterator i = _animations.cbegin(), e = _animations.cend(); i != e; ++i) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		_btns[row][col].hover = 0;
	}
	_animations.clear();
	_hoverAnim.stop();
	if (_sel >= 0) {
		int row = (_sel / MatrixRowShift), col = _sel % MatrixRowShift;
		_btns[row][col].hover = 0;
		_sel = -1;
	}
}

void BotKeyboard::showCommandTip() {
	if (_sel >= 0) {
		int row = (_sel / MatrixRowShift), col = _sel % MatrixRowShift;
		if (!_btns.at(row).at(col).full) {
			int32 dd = QApplication::startDragDistance();
			QPoint dp(mapFromGlobal(_lastMousePos));
			QRect r(dp.x() - dd, dp.y() - dd, 2 * dd, 2 * dd);
			QToolTip::showText(_lastMousePos, _btns.at(row).at(col).cmd, this, r);
		}
	}
}

void BotKeyboard::updateSelected() {
	_cmdTipTimer.start(1000);

	if (_down >= 0) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	int32 newSel = -1;
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		for (int32 j = 0, s = _btns.at(i).size(); j != s; ++j) {
			QRect r(_btns.at(i).at(j).rect);

			if (rtl()) r.moveLeft(width() - r.left() - r.width());

			if (r.contains(p)) {
				newSel = i * MatrixRowShift + j;
				break;
			}
		}
		if (newSel >= 0) break;
	}
	if (newSel != _sel) {
		QToolTip::hideText();
		if (newSel < 0) {
			setCursor(style::cur_default);
		} else if (_sel < 0) {
			setCursor(style::cur_pointer);
		}
		bool startanim = false;
		if (_sel >= 0) {
			_animations.remove(_sel + 1);
			if (_animations.find(-_sel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-_sel - 1, getms());
			}
		}
		_sel = newSel;
		if (_sel >= 0) {
			_animations.remove(-_sel - 1);
			if (_animations.find(_sel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(_sel + 1, getms());
			}
		}
		if (startanim) _hoverAnim.start();
	}
}

HistoryHider::HistoryHider(MainWidget *parent, bool forwardSelected) : QWidget(parent)
, _sharedContact(0)
, _forwardSelected(forwardSelected)
, _sendPath(false)
, forwardButton(this, lang(lng_forward), st::btnSelectDone)
, cancelButton(this, lang(lng_cancel), st::btnSelectCancel)
, offered(0)
, aOpacity(0, 1)
, aOpacityFunc(anim::easeOutCirc)
, hiding(false)
, _forwardRequest(0)
, toTextWidth(0)
, shadow(st::boxShadow)
{
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, UserData *sharedContact) : QWidget(parent)
, _sharedContact(sharedContact)
, _forwardSelected(false)
, _sendPath(false)
, forwardButton(this, lang(lng_forward_send), st::btnSelectDone)
, cancelButton(this, lang(lng_cancel), st::btnSelectCancel)
, offered(0)
, aOpacity(0, 1)
, aOpacityFunc(anim::easeOutCirc)
, hiding(false)
, _forwardRequest(0)
, toTextWidth(0)
, shadow(st::boxShadow)
{
	init();
}

HistoryHider::HistoryHider(MainWidget *parent) : QWidget(parent)
, _sharedContact(0)
, _forwardSelected(false)
, _sendPath(true)
, forwardButton(this, lang(lng_forward_send), st::btnSelectDone)
, cancelButton(this, lang(lng_cancel), st::btnSelectCancel)
, offered(0)
, aOpacity(0, 1)
, aOpacityFunc(anim::easeOutCirc)
, hiding(false)
, _forwardRequest(0)
, toTextWidth(0)
, shadow(st::boxShadow)
{
	init();
}

void HistoryHider::init() {
	connect(&forwardButton, SIGNAL(clicked()), this, SLOT(forward()));
	connect(&cancelButton, SIGNAL(clicked()), this, SLOT(startHide()));
	connect(App::wnd()->getTitle(), SIGNAL(hiderClicked()), this, SLOT(startHide()));

	_chooseWidth = st::forwardFont->m.width(lang(lng_forward_choose));

	resizeEvent(0);
	anim::start(this);
}

bool HistoryHider::animStep(float64 ms) {
	float64 dt = ms / 200;
	bool res = true;
	if (dt >= 1) {
		aOpacity.finish();
		if (hiding)	{
			QTimer::singleShot(0, this, SLOT(deleteLater()));
		}
		res = false;
	} else {
		aOpacity.update(dt, aOpacityFunc);
	}
	App::wnd()->getTitle()->setHideLevel(aOpacity.current());
	forwardButton.setOpacity(aOpacity.current());
	cancelButton.setOpacity(aOpacity.current());
	update();
	return res;
}

bool HistoryHider::withConfirm() const {
	return _sharedContact || _sendPath;
}

void HistoryHider::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (!hiding || !cacheForAnim.isNull() || !offered) {
		p.setOpacity(aOpacity.current() * st::layerAlpha);
		p.fillRect(0, st::titleShadow, width(), height() - st::titleShadow, st::layerBG->b);
		p.setOpacity(aOpacity.current());
	}
	if (cacheForAnim.isNull() || !offered) {
		p.setFont(st::forwardFont->f);
		if (offered) {
			shadow.paint(p, box, st::boxShadowShift);

			// fill bg
			p.fillRect(box, st::boxBG->b);

			// paint shadows
			p.fillRect(box.x(), box.y() + box.height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, box.width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.fillRect(box.x() + st::btnSelectCancel.width, box.y() + box.height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

			p.setPen(st::black->p);
			toText.drawElided(p, box.left() + (box.width() - toTextWidth) / 2, box.top() + st::boxPadding.top(), toTextWidth + 1);
		} else {
			int32 w = st::forwardMargins.left() + _chooseWidth + st::forwardMargins.right(), h = st::forwardMargins.top() + st::forwardFont->height + st::forwardMargins.bottom();
			App::roundRect(p, (width() - w) / 2, (height() - h) / 2, w, h, st::forwardBg, ForwardCorners);

			p.setPen(st::white->p);
			p.drawText(box, lang(lng_forward_choose), QTextOption(style::al_center));
		}
	} else {
		p.drawPixmap(box.left(), box.top(), cacheForAnim);
	}
}

void HistoryHider::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (offered) {
			offered = 0;
			resizeEvent(0);
			update();
			App::main()->dialogsActivate();
		} else {
			startHide();
		}
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (offered) {
			forward();
		}
	}
}

void HistoryHider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!box.contains(e->pos())) {
			startHide();
		}
	}
}

void HistoryHider::startHide() {
	if (hiding) return;
	hiding = true;
	if (cWideMode()) {
		if (offered) cacheForAnim = myGrab(this, box);
		if (_forwardRequest) MTP::cancel(_forwardRequest);
		aOpacity.start(0);
		anim::start(this);
	} else {
		QTimer::singleShot(0, this, SLOT(deleteLater()));
	}
}

void HistoryHider::forward() {
	if (!hiding && offered) {
		if (_sharedContact) {
			parent()->onShareContact(offered->id, _sharedContact);
		} else if (_sendPath) {
			parent()->onSendPaths(offered->id);
		} else {
			parent()->onForward(offered->id, _forwardSelected ? ForwardSelectedMessages : ForwardContextMessage);
		}
	}
	emit forwarded();
}

void HistoryHider::forwardDone() {
	_forwardRequest = 0;
	startHide();
}

MainWidget *HistoryHider::parent() {
	return static_cast<MainWidget*>(parentWidget());
}

void HistoryHider::resizeEvent(QResizeEvent *e) {
	int32 w = st::forwardWidth, h = st::boxPadding.top() + st::forwardFont->height + st::boxPadding.bottom();
	if (offered) {
		forwardButton.show();
		cancelButton.show();
		h += forwardButton.height() + st::scrollDef.bottomsh;
	} else {
		forwardButton.hide();
		cancelButton.hide();
	}
	box = QRect((width() - w) / 2, (height() - h) / 2, w, h);
	cancelButton.move(box.x(), box.y() + h - cancelButton.height());
	forwardButton.move(box.x() + box.width() - forwardButton.width(), cancelButton.y());
}

bool HistoryHider::offerPeer(PeerId peer) {
	if (!peer) {
		offered = 0;
		toText.setText(st::boxFont, QString());
		toTextWidth = 0;
		resizeEvent(0);
		return false;
	}
	offered = App::peer(peer);
	LangString phrase;
	QString recipient = offered->chat ? '\xAB' + offered->name + '\xBB' : offered->name;
	if (_sharedContact) {
		phrase = lng_forward_share_contact(lt_recipient, recipient);
	} else if (_sendPath) {
		if (cSendPaths().size() > 1) {
			phrase = lng_forward_send_files_confirm(lt_recipient, recipient);
		} else {
			QString name(QFileInfo(cSendPaths().front()).fileName());
			if (name.size() > 10) {
				name = name.mid(0, 8) + '.' + '.';
			}
			phrase = lng_forward_send_file_confirm(lt_name, name, lt_recipient, recipient);
		}
	} else {
		PeerId to = offered->id;
		offered = 0;
		parent()->onForward(to, _forwardSelected ? ForwardSelectedMessages : ForwardContextMessage);
		startHide();
		return false;
	}

	toText.setText(st::boxFont, phrase, _textNameOptions);
	toTextWidth = toText.maxWidth();
	if (toTextWidth > box.width() - st::boxPadding.left() - st::boxPadding.right()) {
		toTextWidth = box.width() - st::boxPadding.left() - st::boxPadding.right();
	}
	
	resizeEvent(0);
	update();
	setFocus();

	return true;
}

QString HistoryHider::offeredText() const {
	return toText.original();
}

bool HistoryHider::wasOffered() const {
	return !!offered;
}

HistoryHider::~HistoryHider() {
	if (_sendPath) cSetSendPaths(QStringList());
	if (App::wnd()) App::wnd()->getTitle()->setHideLevel(0);
	parent()->noHider(this);
}

HistoryWidget::HistoryWidget(QWidget *parent) : TWidget(parent)
, _replyToId(0)
, _replyTo(0)
, _replyToNameVersion(0)
, _replyForwardPreviewCancel(this, st::replyCancel)
, _reportSpamStatus(dbiprsUnknown)
, _previewData(0)
, _previewRequest(0)
, _previewCancelled(false)
, _replyForwardPressed(false)
, _replyReturn(0)
, _stickersUpdateRequest(0)
, _peer(0)
, _clearPeer(0)
, _showAtMsgId(0)
, _preloadRequest(0), _preloadDownRequest(0)
, _delayedShowAtMsgId(-1)
, _delayedShowAtRequest(0)
, _activeAnimMsgId(0)
, _scroll(this, st::historyScroll, false)
, _list(0)
, _history(0)
, _histInited(false)
, _toHistoryEnd(this, st::historyToEnd)
, _attachMention(this)
, _reportSpamPanel(this)
, _send(this, lang(lng_send_button), st::btnSend)
, _unblock(this, lang(lng_unblock_button), st::btnUnblock)
, _botStart(this, lang(lng_bot_start), st::btnSend)
, _unblockRequest(0)
, _reportSpamRequest(0)
, _attachDocument(this, st::btnAttachDocument)
, _attachPhoto(this, st::btnAttachPhoto)
, _attachEmoji(this, st::btnAttachEmoji)
, _kbShow(this, st::btnBotKbShow)
, _kbHide(this, st::btnBotKbHide)
, _cmdStart(this, st::btnBotCmdStart)
, _cmdStartShown(false)
, _field(this, st::taMsgField, lang(lng_message_ph))
, _recordAnim(animFunc(this, &HistoryWidget::recordStep))
, _recordingAnim(animFunc(this, &HistoryWidget::recordingStep))
, _recording(false), _inRecord(false), _inField(false), _inReply(false)
, a_recordingLevel(0, 0), _recordingSamples(0)
, a_recordOver(0, 0), a_recordDown(0, 0), a_recordCancel(st::recordCancel->c, st::recordCancel->c)
, _recordCancelWidth(st::recordFont->m.width(lang(lng_record_cancel)))
, _kbShown(false)
, _kbWasHidden(false)
, _kbReplyTo(0)
, _kbScroll(this, st::botKbScroll)
, _keyboard()
, _attachType(this)
, _emojiPan(this)
, _attachDrag(DragStateNone)
, _attachDragDocument(this)
, _attachDragPhoto(this)
, _imageLoader(this)
, _synthedTextUpdate(false)
, _serviceImageCacheSize(0)
, _confirmImageId(0)
, _confirmWithText(false)
, _titlePeerTextWidth(0)
, _showAnim(animFunc(this, &HistoryWidget::showStep))
, _scrollDelta(0)
, _saveDraftStart(0)
, _saveDraftText(false) {
	_scroll.setFocusPolicy(Qt::NoFocus);

	setAcceptDrops(true);

	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(&_reportSpamPanel, SIGNAL(reportClicked()), this, SLOT(onReportSpamClicked()));
	connect(&_reportSpamPanel, SIGNAL(hideClicked()), this, SLOT(onReportSpamHide()));
	connect(&_reportSpamPanel, SIGNAL(clearClicked()), this, SLOT(onReportSpamClear()));
	connect(&_toHistoryEnd, SIGNAL(clicked()), this, SLOT(onHistoryToEnd()));
	connect(&_replyForwardPreviewCancel, SIGNAL(clicked()), this, SLOT(onReplyForwardPreviewCancel()));
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_unblock, SIGNAL(clicked()), this, SLOT(onUnblock()));
	connect(&_botStart, SIGNAL(clicked()), this, SLOT(onBotStart()));
	connect(&_attachDocument, SIGNAL(clicked()), this, SLOT(onDocumentSelect()));
	connect(&_attachPhoto, SIGNAL(clicked()), this, SLOT(onPhotoSelect()));
	connect(&_field, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
	connect(&_field, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(&_field, SIGNAL(tabbed()), this, SLOT(onFieldTabbed()));
	connect(&_field, SIGNAL(resized()), this, SLOT(onFieldResize()));
	connect(&_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(&_imageLoader, SIGNAL(imageReady()), this, SLOT(onPhotoReady()));
	connect(&_imageLoader, SIGNAL(imageFailed(quint64)), this, SLOT(onPhotoFailed(quint64)));
	connect(&_field, SIGNAL(changed()), this, SLOT(onTextChange()));
	connect(&_field, SIGNAL(spacedReturnedPasted()), this, SLOT(onPreviewParse()));
	connect(&_field, SIGNAL(linksChanged()), this, SLOT(onPreviewCheck()));
	connect(App::wnd()->windowHandle(), SIGNAL(visibleChanged(bool)), this, SLOT(onVisibleChanged()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(&_emojiPan, SIGNAL(emojiSelected(EmojiPtr)), &_field, SLOT(onEmojiInsert(EmojiPtr)));
	connect(&_emojiPan, SIGNAL(stickerSelected(DocumentData*)), this, SLOT(onStickerSend(DocumentData*)));
	connect(&_emojiPan, SIGNAL(updateStickers()), this, SLOT(updateStickers()));
	connect(&_sendActionStopTimer, SIGNAL(timeout()), this, SLOT(onCancelSendAction()));
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreviewTimeout()));
	if (audioCapture()) {
		connect(audioCapture(), SIGNAL(onError()), this, SLOT(onRecordError()));
		connect(audioCapture(), SIGNAL(onUpdate(qint16,qint32)), this, SLOT(onRecordUpdate(qint16,qint32)));
		connect(audioCapture(), SIGNAL(onDone(QByteArray,qint32)), this, SLOT(onRecordDone(QByteArray,qint32)));
	}

	_scrollTimer.setSingleShot(false);

	_sendActionStopTimer.setSingleShot(true);

	_animActiveTimer.setSingleShot(false);
	connect(&_animActiveTimer, SIGNAL(timeout()), this, SLOT(onAnimActiveStep()));

	_saveDraftTimer.setSingleShot(true);
	connect(&_saveDraftTimer, SIGNAL(timeout()), this, SLOT(onDraftSave()));
	connect(_field.verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onDraftSaveDelayed()));
	connect(&_field, SIGNAL(cursorPositionChanged()), this, SLOT(onFieldCursorChanged()));

	_replyForwardPreviewCancel.hide();

	_scroll.hide();
	_scroll.move(0, 0);

	_kbScroll.setFocusPolicy(Qt::NoFocus);
	_kbScroll.viewport()->setFocusPolicy(Qt::NoFocus);
	_kbScroll.setWidget(&_keyboard);
	_kbScroll.hide();

	connect(&_kbScroll, SIGNAL(scrolled()), &_keyboard, SLOT(updateSelected()));

	updateScrollColors();

	_toHistoryEnd.hide();
	_toHistoryEnd.installEventFilter(this);

	_attachMention.hide();
	connect(&_attachMention, SIGNAL(chosen(QString)), this, SLOT(onMentionHashtagOrBotCommandInsert(QString)));
	_field.installEventFilter(&_attachMention);

	_field.hide();
	_field.resize(width() - _send.width() - _attachDocument.width() - _attachEmoji.width(), _send.height() - 2 * st::sendPadding);
	_send.hide();
	_unblock.hide();
	_botStart.hide();

	_reportSpamPanel.move(0, 0);
	_reportSpamPanel.hide();

	_attachDocument.hide();
	_attachPhoto.hide();
	_attachEmoji.hide();
	_kbShow.hide();
	_kbHide.hide();
	_cmdStart.hide();

	_attachDocument.installEventFilter(&_attachType);
	_attachPhoto.installEventFilter(&_attachType);
	_attachEmoji.installEventFilter(&_emojiPan);

	connect(&_kbShow, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(&_kbHide, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(&_cmdStart, SIGNAL(clicked()), this, SLOT(onCmdStart()));

	connect(_attachType.addButton(new IconedButton(this, st::dropdownAttachDocument, lang(lng_attach_file))), SIGNAL(clicked()), this, SLOT(onDocumentSelect()));
	connect(_attachType.addButton(new IconedButton(this, st::dropdownAttachPhoto, lang(lng_attach_photo))), SIGNAL(clicked()), this, SLOT(onPhotoSelect()));
	_attachType.hide();
	_emojiPan.hide();
	_attachDragDocument.hide();
	_attachDragPhoto.hide();

	connect(&_attachDragDocument, SIGNAL(dropped(const QMimeData*)), this, SLOT(onDocumentDrop(const QMimeData*)));
	connect(&_attachDragPhoto, SIGNAL(dropped(const QMimeData*)), this, SLOT(onPhotoDrop(const QMimeData*)));
}

void HistoryWidget::start() {
	connect(App::main(), SIGNAL(stickersUpdated()), &_emojiPan, SLOT(refreshStickers()));
	updateRecentStickers();
	connect(App::api(), SIGNAL(fullPeerUpdated(PeerData*)), this, SLOT(onFullPeerUpdated(PeerData*)));
}

void HistoryWidget::onMentionHashtagOrBotCommandInsert(QString str) {
	if (str.at(0) == '/') { // bot command
		App::sendBotCommand(str);
		setFieldText(_field.getLastText().mid(_field.textCursor().position()));
	} else {
		_field.onMentionHashtagOrBotCommandInsert(str);
	}
}

void HistoryWidget::onTextChange() {
	updateSendAction(_history, SendActionTyping);

	if (cHasAudioCapture()) {
		if (_field.getLastText().isEmpty() && !App::main()->hasForwardingItems()) {
			_previewCancelled = false;
			_send.hide();
			setMouseTracking(true);
			mouseMoveEvent(0);
		} else if (!_field.isHidden() && _send.isHidden()) {
			_send.show();
			setMouseTracking(false);
			_recordAnim.stop();
			_inRecord = _inField = false;
			a_recordOver = a_recordDown = anim::fvalue(0, 0);
			a_recordCancel = anim::cvalue(st::recordCancel->c, st::recordCancel->c);
		}
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		resizeEvent(0);
		update();
	}

	if (!_history || _synthedTextUpdate) return;
	_saveDraftText = true;
	onDraftSave(true);
}

void HistoryWidget::onDraftSaveDelayed() {
	if (!_history || _synthedTextUpdate) return;
	if (!_field.textCursor().anchor() && !_field.textCursor().position() && !_field.verticalScrollBar()->value()) {
		if (!Local::hasDraftPositions(_history->peer->id)) return;
	}
	onDraftSave(true);
}

void HistoryWidget::onDraftSave(bool delayed) {
	if (!_history) return;
	if (delayed) {
		uint64 ms = getms();
		if (!_saveDraftStart) {
			_saveDraftStart = ms;
			return _saveDraftTimer.start(SaveDraftTimeout);
		} else if (ms - _saveDraftStart < SaveDraftAnywayTimeout) {
			return _saveDraftTimer.start(SaveDraftTimeout);
		}
	}
	writeDraft();
}

void HistoryWidget::writeDraft(MsgId *replyTo, const QString *text, const MessageCursor *cursor, bool *previewCancelled) {
	bool save = _history && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.stop();
	if (_saveDraftText) {
		if (save) Local::writeDraft(_history->peer->id, Local::MessageDraft(replyTo ? (*replyTo) : _replyToId, text ? (*text) : _field.getLastText(), previewCancelled ? (*previewCancelled) : _previewCancelled));
		_saveDraftText = false;
	}
	if (save) Local::writeDraftPositions(_history->peer->id, cursor ? (*cursor) : MessageCursor(_field));
}

void HistoryWidget::cancelSendAction(History *history, SendActionType type) {
	QMap<QPair<History*, SendActionType>, mtpRequestId>::iterator i = _sendActionRequests.find(qMakePair(history, type));
	if (i != _sendActionRequests.cend()) {
		MTP::cancel(i.value());
		_sendActionRequests.erase(i);
	}
}

void HistoryWidget::onCancelSendAction() {
	cancelSendAction(_history, SendActionTyping);
}

void HistoryWidget::updateSendAction(History *history, SendActionType type, int32 progress) {
	if (!history) return;
	if (type == SendActionTyping && _synthedTextUpdate) return;

	bool doing = (progress >= 0);

	uint64 ms = getms(true) + 10000;
	QMap<SendActionType, uint64>::iterator i = history->mySendActions.find(type);
	if (doing && i != history->mySendActions.cend() && i.value() + 5000 > ms) return;
	if (!doing && (i == history->mySendActions.cend() || i.value() + 5000 <= ms)) return;

	if (doing) {
		if (i == history->mySendActions.cend()) {
			history->mySendActions.insert(type, ms);
		} else {
			i.value() = ms;
		}
	} else if (i != history->mySendActions.cend()) {
		history->mySendActions.erase(i);
	}

	cancelSendAction(history, type);
	if (doing) {
		MTPsendMessageAction action;
		switch (type) {
		case SendActionTyping: action = MTP_sendMessageTypingAction(); break;
		case SendActionRecordVideo: action = MTP_sendMessageRecordVideoAction(); break;
		case SendActionUploadVideo: action = MTP_sendMessageUploadVideoAction(MTP_int(progress)); break;
		case SendActionRecordAudio: action = MTP_sendMessageRecordAudioAction(); break;
		case SendActionUploadAudio: action = MTP_sendMessageUploadAudioAction(MTP_int(progress)); break;
		case SendActionUploadPhoto: action = MTP_sendMessageUploadPhotoAction(MTP_int(progress)); break;
		case SendActionUploadFile: action = MTP_sendMessageUploadDocumentAction(MTP_int(progress)); break;
		case SendActionChooseLocation: action = MTP_sendMessageGeoLocationAction(); break;
		case SendActionChooseContact: action = MTP_sendMessageChooseContactAction(); break;
		}
		_sendActionRequests.insert(qMakePair(history, type), MTP::send(MTPmessages_SetTyping(history->peer->input, action), rpcDone(&HistoryWidget::sendActionDone)));
		if (type == SendActionTyping) _sendActionStopTimer.start(5000);
	}
}

void HistoryWidget::updateRecentStickers() {
	_emojiPan.refreshStickers();
}

void HistoryWidget::stickersInstalled(uint64 setId) {
	_emojiPan.stickersInstalled(setId);
}

void HistoryWidget::sendActionDone(const MTPBool &result, mtpRequestId req) {
	for (QMap<QPair<History*, SendActionType>, mtpRequestId>::iterator i = _sendActionRequests.begin(), e = _sendActionRequests.end(); i != e; ++i) {
		if (i.value() == req) {
			_sendActionRequests.erase(i);
			break;
		}
	}
}

void HistoryWidget::activate() {
	if (_history) updateListSize(0, true);
	if (_list) {
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked()) {
			_list->setFocus();
		} else {
			_field.setFocus();
		}
	}
}

void HistoryWidget::onRecordError() {
	stopRecording(false);
}

void HistoryWidget::onRecordDone(QByteArray result, qint32 samples) {
	if (!_peer) return;

	App::wnd()->activateWindow();
	int32 duration = samples / AudioVoiceMsgFrequency;
	_imageLoader.append(result, duration, _peer->id, replyToId(), ToPrepareAudio);
	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::onRecordUpdate(qint16 level, qint32 samples) {
	if (!_recording) {
		return;
	}

	a_recordingLevel.start(level);
	_recordingAnim.start();
	_recordingSamples = samples;
	if (samples < 0 || samples >= AudioVoiceMsgFrequency * AudioVoiceMsgMaxLength) {
		stopRecording(_peer && samples > 0 && _inField);
	}
	updateField();
	updateSendAction(_history, SendActionRecordAudio);
}

void HistoryWidget::updateStickers() {
	if (cLastStickersUpdate() && getms(true) < cLastStickersUpdate() + StickersUpdateTimeout) return;
	if (_stickersUpdateRequest) return;

	_stickersUpdateRequest = MTP::send(MTPmessages_GetAllStickers(MTP_string(cStickersHash())), rpcDone(&HistoryWidget::stickersGot), rpcFail(&HistoryWidget::stickersFailed));
}

void HistoryWidget::botCommandsChanged(UserData *user) {
	if (_peer && (_peer == user || _peer->chat)) {
		if (_attachMention.clearFilteredCommands()) {
			checkMentionDropdown();
		}
	}
}

void HistoryWidget::stickersGot(const MTPmessages_AllStickers &stickers) {
	cSetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_allStickers) return;
	const MTPDmessages_allStickers &d(stickers.c_messages_allStickers());

	const QVector<MTPStickerSet> &d_sets(d.vsets.c_vector().v);

	QByteArray wasHash = cStickersHash();
	cSetStickersHash(qba(d.vhash));

	StickerSetsOrder &setsOrder(cRefStickerSetsOrder());
	setsOrder.clear();

	StickerSets &sets(cRefStickerSets());
	QMap<uint64, uint64> setsToRequest;
	for (StickerSets::iterator i = sets.begin(), e = sets.end(); i != e; ++i) {
		i->access = 0; // mark for removing
	}
	for (int32 i = 0, l = d_sets.size(); i != l; ++i) {
		if (d_sets.at(i).type() == mtpc_stickerSet) {
			const MTPDstickerSet &set(d_sets.at(i).c_stickerSet());
			StickerSets::iterator i = sets.find(set.vid.v);
			QString title = qs(set.vtitle);
			if (set.vflags.v & MTPDstickerSet_flag_official) {
				if (!title.compare(qstr("Great Minds"), Qt::CaseInsensitive)) {
					title = lang(lng_stickers_default_set);
				}
				setsOrder.push_front(set.vid.v);
			} else {
				setsOrder.push_back(set.vid.v);
			}

			if (i == sets.cend()) {
				i = sets.insert(set.vid.v, StickerSet(set.vid.v, set.vaccess_hash.v, title, qs(set.vshort_name), set.vcount.v, set.vhash.v, set.vflags.v | MTPDstickerSet_flag_NOT_LOADED));
				if (!(i->flags & MTPDstickerSet_flag_disabled)) {
					setsToRequest.insert(set.vid.v, set.vaccess_hash.v);
				}
			} else {
				i->access = set.vaccess_hash.v;
				i->title = title;
				i->shortName = qs(set.vshort_name);
				i->flags = set.vflags.v;
				if (i->count != set.vcount.v || i->hash != set.vhash.v) {
					i->count = set.vcount.v;
					i->hash = set.vhash.v;
					i->flags |= MTPDstickerSet_flag_NOT_LOADED; // need to request this set
					if (!(i->flags & MTPDstickerSet_flag_disabled)) {
						setsToRequest.insert(set.vid.v, set.vaccess_hash.v);
					}
				}
			}
		}
	}
	for (StickerSets::iterator i = sets.begin(), e = sets.end(); i != e;) {
		if (i->id == CustomStickerSetId || i->access != 0) {
			++i;
		} else {
			i = sets.erase(i);
		}
	}

	if (!setsToRequest.isEmpty()) {
		for (QMap<uint64, uint64>::const_iterator i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			App::api()->scheduleStickerSetRequest(i.key(), i.value());
		}
		App::api()->requestStickerSets();
	}

	Local::writeStickers();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::stickersFailed(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	LOG(("App Fail: Failed to get stickers!"));

	cSetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = 0;
}

void HistoryWidget::pushReplyReturn(HistoryItem *item) {
	if (!item) return;
	_replyReturn = item;
	_replyReturns.push_back(_replyReturn->id);
	updateControlsVisibility();
}

QList<MsgId> HistoryWidget::replyReturns() {
	return _replyReturns;
}

void HistoryWidget::setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns) {
	if (!_peer || _peer->id != peer) return;

	_replyReturns = replyReturns;
	_replyReturn = _replyReturns.isEmpty() ? 0 : App::histItemById(_replyReturns.back());
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		_replyReturn = _replyReturns.isEmpty() ? 0 : App::histItemById(_replyReturns.back());
	}
	updateControlsVisibility();
}

void HistoryWidget::calcNextReplyReturn() {
	_replyReturn = 0;
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		_replyReturn = _replyReturns.isEmpty() ? 0 : App::histItemById(_replyReturns.back());
	}
	if (!_replyReturn) updateControlsVisibility();
}

bool HistoryWidget::kbWasHidden() {
	return _kbWasHidden;
}

void HistoryWidget::setKbWasHidden() {
	if (_kbWasHidden || (!_keyboard.hasMarkup() && !_keyboard.forceReply())) return;

	_kbWasHidden = true;
	if (!_showAnim.animating()) {
		_kbScroll.hide();
		_attachEmoji.show();
		_kbHide.hide();
		_cmdStart.hide();
		_kbShow.show();
	}
	_field.setMaxHeight(st::maxFieldHeight);
	_kbShown = false;
	_kbReplyTo = 0;
	if (!App::main()->hasForwardingItems() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
		_replyForwardPreviewCancel.hide();
	}
	resizeEvent(0);
	update();
}

void HistoryWidget::fastShowAtEnd(History *h) {
	h->getReadyFor(ShowAtTheEndMsgId);

	if (_history != h) return;

	clearAllLoadRequests();

	_showAtMsgId = ShowAtTheEndMsgId;
	_histInited = false;

	if (h->isReadyFor(_showAtMsgId)) {
		historyLoaded();
	} else {
		firstLoadMessages();
		doneShow();
	}
}

void HistoryWidget::showPeerHistory(const PeerId &peerId, MsgId showAtMsgId) {
	if (_history) {
		if (_peer->id == peerId) {
			_history->lastWidth = 0;

			bool canShowNow = _history->isReadyFor(showAtMsgId, true);
			if (!canShowNow) {
				delayedShowAt(showAtMsgId);
			} else {
				clearDelayedShowAt();
				if (_replyReturn && _replyReturn->id == showAtMsgId) {
					calcNextReplyReturn();
				}

				_showAtMsgId = showAtMsgId;
				_histInited = false;

				historyLoaded();
			}
			emit peerShown(_peer);
			App::main()->topBar()->update();
			update();
			return;
		}
		if (_history->mySendActions.contains(SendActionTyping)) updateSendAction(_history, SendActionTyping, -1);
	}

	stopGif();
	clearReplyReturns();

	clearAllLoadRequests();

	if (_history) {
		_history->draft = _field.getLastText();
		_history->draftCursor.fillFrom(_field);
		_history->draftToId = _replyToId;
		_history->draftPreviewCancelled = _previewCancelled;

		writeDraft(&_history->draftToId, &_history->draft, &_history->draftCursor, &_history->draftPreviewCancelled);

		if (_scroll.scrollTop() + 1 <= _scroll.scrollTopMax()) {
			_history->lastWidth = _list->width();
			_history->lastShowAtMsgId = _showAtMsgId;
		} else {
			_history->lastWidth = 0;
			_history->lastShowAtMsgId = ShowAtUnreadMsgId;
		}
		_history->lastScrollTop = _scroll.scrollTop();
		if (_history->unreadBar) {
			_history->unreadBar->destroy();
		}
		_history = 0;
	}

	if (_replyToId) {
		_replyTo = 0;
		_replyToId = 0;
		_replyForwardPreviewCancel.hide();
	}
	if (_previewData && _previewData->pendingTill >= 0) {
		_previewData = 0;
		_replyForwardPreviewCancel.hide();
	}
	_previewCache.clear();
	_scroll.setWidget(0);
	if (_list) _list->deleteLater();
	_list = 0;
	updateTopBarSelection();

	_showAtMsgId = showAtMsgId;
	_histInited = false;
	if (_history) {
		App::main()->dlgUpdated(_history);
	}

	_peer = peerId ? App::peer(peerId) : 0;
	_unblockRequest = _reportSpamRequest = 0;

	_titlePeerText = QString();
	_titlePeerTextWidth = 0;

	noSelectingScroll();
	_selCount = 0;
	App::main()->topBar()->showSelected(0);

	App::hoveredItem(0);
	App::pressedItem(0);
	App::hoveredLinkItem(0);
	App::pressedLinkItem(0);
	App::contextItem(0);
	App::mousedItem(0);

	_kbWasHidden = false;

	if (_peer) {
		App::forgetMedia();
		_serviceImageCacheSize = imageCacheSize();
		MTP::clearLoaderPriorities();

		if (_peer->input.type() == mtpc_inputPeerEmpty) { // maybe should load user
		}
		_history = App::history(_peer->id);

		if (_showAtMsgId == ShowAtUnreadMsgId) {
			if (_history->lastWidth) {
				_showAtMsgId = _history->lastShowAtMsgId;
			}
		} else {
			_history->lastWidth = 0;
		}

		_list = new HistoryList(this, &_scroll, _history);
		_list->hide();
		_scroll.hide();
		_scroll.setWidget(_list);
		_list->show();

		if (_history->lastWidth || _history->isReadyFor(_showAtMsgId, true)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}
		if (_history) {
			App::main()->dlgUpdated(_history);
		}

		App::main()->peerUpdated(_peer);
		
		if (_history->draftToId > 0 || !_history->draft.isEmpty()) {
			setFieldText(_history->draft);
			_field.setFocus();
			_history->draftCursor.applyTo(_field, &_synthedTextUpdate);
			_replyToId = App::main()->hasForwardingItems() ? 0 : _history->draftToId;
			if (_history->draftPreviewCancelled) {
				_previewCancelled = true;
			}
		} else {
			Local::MessageDraft draft = Local::readDraft(_peer->id);
			setFieldText(draft.text);
			_field.setFocus();
			if (!draft.text.isEmpty()) {
				MessageCursor cur = Local::readDraftPositions(_peer->id);
				cur.applyTo(_field, &_synthedTextUpdate);
			}
			_replyToId = App::main()->hasForwardingItems() ? 0 : draft.replyTo;
			if (draft.previewCancelled) {
				_previewCancelled = true;
			}
		}
		if (_replyToId) {
			updateReplyTo();
			if (!_replyTo) App::api()->requestReplyTo(0, _replyToId);
			resizeEvent(0);
		}
		if (!_previewCancelled) {
			onPreviewParse();
		}

		connect(&_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));
		connect(&_scroll, SIGNAL(scrolled()), _list, SLOT(onUpdateSelected()));
	} else {
		doneShow();
	}

	emit peerShown(_peer);
	App::main()->topBar()->update();
	update();
}

void HistoryWidget::clearDelayedShowAt() {
	_delayedShowAtMsgId = -1;
	if (_delayedShowAtRequest) {
		MTP::cancel(_delayedShowAtRequest);
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::clearAllLoadRequests() {
	clearDelayedShowAt();
	if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
	if (_preloadRequest) MTP::cancel(_preloadRequest);
	if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
	_preloadRequest = _preloadDownRequest = _firstLoadRequest = 0;
}

void HistoryWidget::contactsReceived() {
	if (!_peer) return;
	updateReportSpamStatus();
	updateControlsVisibility();
}

void HistoryWidget::updateReportSpamStatus() {
	if (!_peer || !_peer->chat && (App::userFromPeer(_peer->id) == MTP::authedId() || isNotificationsUser(_peer->id) || isServiceUser(_peer->id))) {
		_reportSpamStatus = dbiprsNoButton;
		return;
	} else {
		ReportSpamStatuses::const_iterator i = cReportSpamStatuses().constFind(_peer->id);
		if (i != cReportSpamStatuses().cend()) {
			_reportSpamStatus = i.value();
			_reportSpamPanel.setReported(_reportSpamStatus == dbiprsReportSent);
			return;
		}
	}
	if ((!_history->loadedAtTop() && (_history->size() < 2 || (_history->size() == 2 && _history->at(1)->size() < 2))) || !cContactsReceived() || _firstLoadRequest) {
		_reportSpamStatus = dbiprsUnknown;
	} else if (_peer->chat) {
		if (_peer->asChat()->inviterForSpamReport > 0) {
			UserData *user = App::userLoaded(_peer->asChat()->inviterForSpamReport);
			if (user && user->contact > 0) {
				_reportSpamStatus = dbiprsNoButton;
			} else {
				_reportSpamStatus = dbiprsShowButton;
			}
		} else {
			_reportSpamStatus = dbiprsNoButton;
		}
	} else {
		if (_peer->asUser()->contact > 0) {
			_reportSpamStatus = dbiprsNoButton;
		} else {
			bool anyFound = false, outFound = false;
			for (int32 i = 0, l = _history->size(); i < l; ++i) {
				for (int32 j = 0, c = _history->at(i)->size(); j < c; ++j) {
					anyFound = true;
					if (_history->at(i)->at(j)->out()) {
						outFound = true;
						break;
					}
				}
			}
			if (anyFound) {
				if (outFound) {
					_reportSpamStatus = dbiprsNoButton;
				} else {
					_reportSpamStatus = dbiprsShowButton;
				}
			} else {
				_reportSpamStatus = dbiprsUnknown;
			}
		}
	}
	if (_reportSpamStatus == dbiprsShowButton || _reportSpamStatus == dbiprsNoButton) {
		_reportSpamPanel.setReported(false);
		cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
		Local::writeReportSpamStatuses();
	}
}

void HistoryWidget::updateControlsVisibility() {
	if (!_history || _showAnim.animating()) {
		_reportSpamPanel.hide();
		_scroll.hide();
		_kbScroll.hide();
		_send.hide();
		_unblock.hide();
		_botStart.hide();
		_attachMention.hide();
		_field.hide();
		_replyForwardPreviewCancel.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_attachEmoji.hide();
		_toHistoryEnd.hide();
		_kbShow.hide();
		_kbHide.hide();
		_cmdStart.hide();
		_attachType.hide();
		_emojiPan.hide();
		return;
	}

	updateToEndVisibility();
	if (_firstLoadRequest) {
		_scroll.hide();
	} else {
		_scroll.show();
	}
	if (_reportSpamStatus == dbiprsShowButton || _reportSpamStatus == dbiprsReportSent) {
		_reportSpamPanel.show();
	} else {
		_reportSpamPanel.hide();
	}
	if ((_peer->chat && !_peer->asChat()->forbidden && !_peer->asChat()->left) || (!_peer->chat && _peer->asUser()->access != UserNoAccess)) {
		checkMentionDropdown();
		if (isBlocked()) {
			_botStart.hide();
			if (_unblock.isHidden()) {
				_unblock.clearState();
				_unblock.show();
				_kbShown = false;
			}
			_send.hide();
			_field.hide();
			_attachEmoji.hide();
			_kbShow.hide();
			_kbHide.hide();
			_cmdStart.hide();
			_attachDocument.hide();
			_attachPhoto.hide();
			_kbScroll.hide();
			_replyForwardPreviewCancel.hide();
		} else if (isBotStart()) {
			_unblock.hide();
			if (_botStart.isHidden()) {
				_botStart.clearState();
				_botStart.show();
				_kbShown = false;
			}
			_send.hide();
			_field.hide();
			_attachEmoji.hide();
			_kbShow.hide();
			_kbHide.hide();
			_cmdStart.hide();
			_attachDocument.hide();
			_attachPhoto.hide();
			_kbScroll.hide();
			_replyForwardPreviewCancel.hide();
		} else {
			_unblock.hide();
			_botStart.hide();
			if (cHasAudioCapture() && _field.getLastText().isEmpty() && !App::main()->hasForwardingItems()) {
				_send.hide();
				setMouseTracking(true);
				mouseMoveEvent(0);
			} else {
				_send.show();
				setMouseTracking(false);
				_recordAnim.stop();
				_inRecord = _inField = false;
				a_recordOver = anim::fvalue(0, 0);
			}
			if (_recording) {
				_field.hide();
				_attachEmoji.hide();
				_kbShow.hide();
				_kbHide.hide();
				_cmdStart.hide();
				_attachDocument.hide();
				_attachPhoto.hide();
				if (_kbShown) {
					_kbScroll.show();
				} else {
					_kbScroll.hide();
				}
			} else {
				_field.show();
				if (_kbShown) {
					_kbScroll.show();
					_attachEmoji.hide();
					_kbHide.show();
					_kbShow.hide();
					_cmdStart.hide();
				} else if (_kbReplyTo) {
					_kbScroll.hide();
					_attachEmoji.show();
					_kbHide.hide();
					_kbShow.hide();
					_cmdStart.hide();
				} else {
					_kbScroll.hide();
					_attachEmoji.show();
					_kbHide.hide();
					if (_keyboard.hasMarkup()) {
						_kbShow.show();
						_cmdStart.hide();
					} else {
						_kbShow.hide();
						if (_cmdStartShown) {
							_cmdStart.show();
						} else {
							_cmdStart.hide();
						}
					}
				}
				if (cDefaultAttach() == dbidaPhoto) {
					_attachDocument.hide();
					_attachPhoto.show();
				} else {
					_attachDocument.show();
					_attachPhoto.hide();
				}
			}
			if (_replyToId || App::main()->hasForwardingItems() || (_previewData && _previewData->pendingTill >= 0) || _kbReplyTo) {
				if (_replyForwardPreviewCancel.isHidden()) {
					_replyForwardPreviewCancel.show();
					resizeEvent(0);
					update();
				}
			} else {
				_replyForwardPreviewCancel.hide();
			}
		}
	} else {
		_attachMention.hide();
		_send.hide();
		_unblock.hide();
		_botStart.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_attachEmoji.hide();
		_kbShow.hide();
		_kbHide.hide();
		_cmdStart.hide();
		_attachType.hide();
		_emojiPan.hide();
		if (!_field.isHidden()) {
			_field.hide();
			resizeEvent(0);
			update();
		}
	}
}

void HistoryWidget::newUnreadMsg(History *history, HistoryItem *item) {
	if (App::wnd()->historyIsActive()) {
		if (_history == history) {
			historyWasRead();
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
			}
		} else {
			App::wnd()->notifySchedule(history, item);
			history->setUnreadCount(history->unreadCount + 1);
		}
	} else {
		if (_history == history) {
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
			}
		}
		App::wnd()->notifySchedule(history, item);
		history->setUnreadCount(history->unreadCount + 1);
	}
}

void HistoryWidget::historyToDown(History *history) {
	history->lastScrollTop = History::ScrollMax;
	if (history == _history) {
		_scroll.scrollToY(_scroll.scrollTopMax());
	}
}

void HistoryWidget::historyWasRead(bool force) {
	App::main()->readServerHistory(_history, force);
}

void HistoryWidget::historyCleared(History *history) {
	if (history == _history) {
		_list->dragActionCancel();
	}
}

bool HistoryWidget::messagesFailed(const RPCError &error, mtpRequestId requestId) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_preloadRequest == requestId) {
		_preloadRequest = 0;
	} else if (_preloadDownRequest == requestId) {
		_preloadDownRequest = 0;
	} else if (_firstLoadRequest == requestId) {
		_firstLoadRequest = 0;
		App::main()->showDialogs();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
	}
	return true;
}

void HistoryWidget::messagesReceived(const MTPmessages_Messages &messages, mtpRequestId requestId) {
	if (!_history) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	int32 count = 0;
	const QVector<MTPMessage> *histList = 0;
	switch (messages.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &data(messages.c_messages_messages());
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		histList = &data.vmessages.c_vector().v;
		count = histList->size();
	} break;
	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &data(messages.c_messages_messagesSlice());
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		histList = &data.vmessages.c_vector().v;
		count = data.vcount.v;
	} break;
	}
	
	if (_preloadRequest == requestId) {
		addMessagesToFront(*histList);
		_preloadRequest = 0;
		onListScroll();
		if (_reportSpamStatus == dbiprsUnknown) {
			updateReportSpamStatus();
			if (_reportSpamStatus != dbiprsUnknown) updateControlsVisibility();
		}
	} else if (_preloadDownRequest == requestId) {
		addMessagesToBack(*histList);
		_preloadDownRequest = 0;
		onListScroll();
		if (_history->loadedAtBottom() && App::wnd()) App::wnd()->checkHistoryActivation();
	} else if (_firstLoadRequest == requestId) {
		addMessagesToFront(*histList);
		_firstLoadRequest = 0;
		if (_history->loadedAtTop()) {
			if (_history->unreadCount > count) {
				_history->setUnreadCount(count);
			}
			if (_history->isEmpty() && count > 0) {
				firstLoadMessages();
				return;
			}
		}

		historyLoaded();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
		_history->getReadyFor(_delayedShowAtMsgId);
		if (_history->isEmpty()) {
			if (_preloadRequest) MTP::cancel(_preloadRequest);
			if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
			if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
			_preloadRequest = _preloadDownRequest = 0;
			_firstLoadRequest = -1; // hack - don't updateListSize yet
			addMessagesToFront(*histList);
			_firstLoadRequest = 0;
			if (_history->loadedAtTop()) {
				if (_history->unreadCount > count) {
					_history->setUnreadCount(count);
				}
				if (_history->isEmpty() && count > 0) {
					firstLoadMessages();
					return;
				}
			}
		}
		if (_replyReturn && _replyReturn->id == _delayedShowAtMsgId) {
			calcNextReplyReturn();
		}

		_showAtMsgId = _delayedShowAtMsgId;
		_histInited = false;

		historyLoaded();
	}
}

void HistoryWidget::historyLoaded() {
	countHistoryShowFrom();
	if (_history->unreadBar) {
		_history->unreadBar->destroy();
	}
	doneShow();
}

void HistoryWidget::windowShown() {
	resizeEvent(0);
}

bool HistoryWidget::isActive() const {
	if (!_history) return true;
	if (_firstLoadRequest || _showAnim.animating()) return false;
	if (_history->loadedAtBottom()) return true;
	if (_history->showFrom && !_history->showFrom->detached() && _history->unreadBar) return true;
	return false;
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) return;

	int32 from = 0, offset = 0, loadCount = MessagesPerPage;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (_history->unreadCount > loadCount) {
			_history->getReadyFor(_showAtMsgId);
			offset = _history->unreadCount - loadCount / 2;
		} else {
			_history->getReadyFor(ShowAtTheEndMsgId);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		_history->getReadyFor(_showAtMsgId);
		loadCount = MessagesFirstLoad;
	} else if (_showAtMsgId > 0) {
		_history->getReadyFor(_showAtMsgId);
		offset = -loadCount / 2;
		from = _showAtMsgId;
	}
	_firstLoadRequest = MTP::send(MTPmessages_GetHistory(_peer->input, MTP_int(offset), MTP_int(from), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessages() {
	if (!_history || _history->loadedAtTop() || _preloadRequest) return;

	MsgId min = _history->minMsgId();
	int32 offset = 0, loadCount = min ? MessagesPerPage : MessagesFirstLoad;
	_preloadRequest = MTP::send(MTPmessages_GetHistory(_peer->input, MTP_int(offset), MTP_int(min), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _history->loadedAtBottom() || _preloadDownRequest) return;

	MsgId max = _history->maxMsgId();
	if (!max) return;

	int32 loadCount = MessagesPerPage, offset = -loadCount - 1;
	_preloadDownRequest = MTP::send(MTPmessages_GetHistory(_peer->input, MTP_int(offset), MTP_int(max + 1), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::delayedShowAt(MsgId showAtMsgId) {
	if (!_history || (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId)) return;

	clearDelayedShowAt();
	_delayedShowAtMsgId = showAtMsgId;

	int32 from = _delayedShowAtMsgId, offset = 0, loadCount = MessagesPerPage;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		offset = qMax(_history->unreadCount - loadCount / 2, 0);
		from = 0;
	} else if (_delayedShowAtMsgId == ShowAtTheEndMsgId) {
		loadCount = MessagesFirstLoad;
		from = 0;
	} else if (_delayedShowAtMsgId > 0) {
		offset = -loadCount / 2;
	}
	_delayedShowAtRequest = MTP::send(MTPmessages_GetHistory(_peer->input, MTP_int(offset), MTP_int(from), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::onListScroll() {
	App::checkImageCacheSize();
	if (_firstLoadRequest || _scroll.isHidden()) return;

	updateToEndVisibility();
	
	int st = _scroll.scrollTop(), stm = _scroll.scrollTopMax(), sh = _scroll.height();
	if (st + PreloadHeightsCount * sh > stm) {
		loadMessagesDown();
	}

	if (st < PreloadHeightsCount * sh) {
		loadMessages();
	}

	while (_replyReturn) {
		bool below = (_replyReturn->detached() && !_history->isEmpty() && _replyReturn->id < _history->back()->back()->id);
		if (!below && !_replyReturn->detached()) below = (st >= stm) || (_replyReturn->y + _replyReturn->block()->y < st + sh / 2);
		if (below) {
			calcNextReplyReturn();
		} else {
			break;
		}
	}
}

void HistoryWidget::onVisibleChanged() {
	QTimer::singleShot(0, this, SLOT(onListScroll()));
}

QString HistoryWidget::prepareMessage(QString result) {
	result = result.replace('\t', qsl(" "));

	result = result.replace(" --", QString::fromUtf8(" \xe2\x80\x94"));
	result = result.replace("-- ", QString::fromUtf8("\xe2\x80\x94 "));
	result = result.replace("<<", QString::fromUtf8("\xc2\xab"));
	result = result.replace(">>", QString::fromUtf8("\xc2\xbb"));

	return (cReplaceEmojis() ? replaceEmojis(result) : result).trimmed();
}

void HistoryWidget::onHistoryToEnd() {
	if (_replyReturn) {
		showPeerHistory(_peer->id, _replyReturn->id);
	} else if (_peer) {
		showPeerHistory(_peer->id, ShowAtUnreadMsgId);
	}
}

void HistoryWidget::onSend(bool ctrlShiftEnter, MsgId replyTo) {
	if (!_history) return;

	bool lastKeyboardUsed = lastForceReplyReplied(replyTo);
	QString text = prepareMessage(_field.getLastText());
	if (!text.isEmpty()) {
		App::main()->readServerHistory(_history, false);
		fastShowAtEnd(_history);

		WebPageId webPageId = _previewCancelled ? 0xFFFFFFFFFFFFFFFFULL : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);
		App::main()->sendPreparedText(_history, text, replyTo, webPageId);

		setFieldText(QString());
		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();

		if (!_attachMention.isHidden()) _attachMention.hideStart();
		if (!_attachType.isHidden()) _attachType.hideStart();
		if (!_emojiPan.isHidden()) _emojiPan.hideStart();

	} else if (App::main()->hasForwardingItems()) {
		App::main()->readServerHistory(_history, false);
		fastShowAtEnd(_history);
		App::main()->finishForwarding(_history);
	}
	if (replyTo < 0) cancelReply(lastKeyboardUsed);
	if (_previewData && _previewData->pendingTill) previewCancel();
	_field.setFocus();

	if (!_keyboard.hasMarkup() && _keyboard.forceReply() && !_kbReplyTo) onKbToggle();
}

void HistoryWidget::onUnblock() {
	if (_unblockRequest) return;
	if (!_peer || _peer->chat || _peer->asUser()->blocked != UserIsBlocked) {
		updateControlsVisibility();
		return;
	}

	_unblockRequest = MTP::send(MTPcontacts_Unblock(_peer->asUser()->inputUser), rpcDone(&HistoryWidget::unblockDone, _peer), rpcFail(&HistoryWidget::unblockFail));
}

void HistoryWidget::unblockDone(PeerData *peer, const MTPBool &result) {
	if (peer->chat) return;
	_unblockRequest = 0;
	peer->asUser()->blocked = UserIsNotBlocked;
	emit App::main()->peerUpdated(peer);
}

bool HistoryWidget::unblockFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_unblockRequest = 0;
	return false;
}

void HistoryWidget::blockDone(PeerData *peer, const MTPBool &result) {
	if (peer->chat) return;

	peer->asUser()->blocked = UserIsBlocked;
	emit App::main()->peerUpdated(peer);
}

void HistoryWidget::onBotStart() {
	if (!_peer || _peer->chat || !_peer->asUser()->botInfo) {
		updateControlsVisibility();
		return;
	}

	QString token = _peer->asUser()->botInfo->startToken;
	if (token.isEmpty()) {
		sendBotCommand(qsl("/start"), 0);
	} else {
		uint64 randomId = MTP::nonce<uint64>();
		MTP::send(MTPmessages_StartBot(_peer->asUser()->inputUser, MTP_int(0), MTP_long(randomId), MTP_string(token)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, _peer->asUser()));

		_peer->asUser()->botInfo->startToken = QString();
		if (_keyboard.hasMarkup()) {
			if (_keyboard.singleUse() && _keyboard.forMsgId() == _history->lastKeyboardId && _history->lastKeyboardUsed) {
				_kbWasHidden = true;
			}
			if (!_kbWasHidden) _kbShown = _keyboard.hasMarkup();
		}
	}
	updateControlsVisibility();
	resizeEvent(0);
}

void HistoryWidget::onShareContact(const PeerId &peer, UserData *contact) {
	if (!contact || contact->phone.isEmpty()) return;

	App::main()->showPeerHistory(peer, ShowAtTheEndMsgId);
	if (!_history) return;

	shareContact(peer, contact->phone, contact->firstName, contact->lastName, replyToId(), int32(contact->id & 0xFFFFFFFF));
}

void HistoryWidget::shareContact(const PeerId &peer, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, int32 userId) {
	History *h = App::history(peer);

	uint64 randomId = MTP::nonce<uint64>();
	MsgId newId = clientMsgId();

	App::main()->readServerHistory(h, false);
	fastShowAtEnd(h);

	PeerData *p = App::peer(peer);
	int32 flags = newMessageFlags(p) | MTPDmessage::flag_media; // unread, out
	
	bool lastKeyboardUsed = lastForceReplyReplied(replyTo);

	int32 sendFlags = 0;
	if (replyTo) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}
	h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(peer), MTPint(), MTPint(), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname), MTP_int(userId)), MTPnullMarkup, MTPnullEntities));
	h->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), p->input, MTP_int(replyTo), MTP_inputMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, h->sendRequestId);

	App::historyRegRandom(randomId, newId);

	App::main()->finishForwarding(h);
	cancelReply(lastKeyboardUsed);
}

void HistoryWidget::onSendPaths(const PeerId &peer) {
	App::main()->showPeerHistory(peer, ShowAtTheEndMsgId);
	if (!_history) return;

	uploadMedias(cSendPaths(), ToPrepareDocument);
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

void HistoryWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back) {
	_bgAnimCache = bgAnimCache;
	_bgAnimTopBarCache = bgAnimTopBarCache;
	_animCache = myGrab(this, rect());
	App::main()->topBar()->stopAnim();
	_animTopBarCache = myGrab(App::main()->topBar(), QRect(0, 0, width(), st::topBarHeight));
	App::main()->topBar()->startAnim();
	_scroll.hide();
	_kbScroll.hide();
	_reportSpamPanel.hide();
	_toHistoryEnd.hide();
	_attachDocument.hide();
	_attachPhoto.hide();
	_attachEmoji.hide();
	_attachMention.hide();
	_kbShow.hide();
	_kbHide.hide();
	_cmdStart.hide();
	_field.hide();
	_replyForwardPreviewCancel.hide();
	_send.hide();
	_unblock.hide();
	_botStart.hide();
	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);
	_showAnim.start();
	App::main()->topBar()->update();
}

bool HistoryWidget::showStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		_showAnim.stop();
		res = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();
		_bgAnimCache = _animCache = _animTopBarCache = _bgAnimTopBarCache = QPixmap();
		App::main()->topBar()->stopAnim();
		App::main()->topBar()->enableShadow();
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

void HistoryWidget::doneShow() {
	updateReportSpamStatus();
	updateBotKeyboard();
	updateControlsVisibility();
	updateListSize(0, true);
	onListScroll();
	if (App::wnd()) App::wnd()->checkHistoryActivation();
	App::wnd()->setInnerFocus();
}

void HistoryWidget::animStop() {
	if (!_showAnim.animating()) return;
	_showAnim.stop();
}

bool HistoryWidget::recordStep(float64 ms) {
	float64 dt = ms / st::btnSend.duration;
	bool res = true;
	if (dt >= 1 || !_send.isHidden() || isBotStart() || isBlocked()) {
		res = false;
		a_recordOver.finish();
		a_recordDown.finish();
		a_recordCancel.finish();
	} else {
		a_recordOver.update(dt, anim::linear);
		a_recordDown.update(dt, anim::linear);
		a_recordCancel.update(dt, anim::linear);
	}
	if (_recording) {
		updateField();
	} else {
		update(_send.geometry());
	}
	return res;
}

bool HistoryWidget::recordingStep(float64 ms) {
	float64 dt = ms / AudioVoiceMsgUpdateView;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_recordingLevel.finish();
	} else {
		a_recordingLevel.update(dt, anim::linear);
	}
	update(_attachDocument.geometry());
	return res;
}

void HistoryWidget::onPhotoSelect() {
	if (!_history) return;

	_attachDocument.clearState();
	_attachDocument.hide();
	_attachPhoto.show();
	_attachType.fastHide();

	if (cDefaultAttach() != dbidaPhoto) {
		cSetDefaultAttach(dbidaPhoto);
		Local::writeUserSettings();
	}

	QStringList photoExtensions(cPhotoExtensions());
	QStringList imgExtensions(cImgExtensions());	
	QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;Photo files (*") + photoExtensions.join(qsl(" *")) + qsl(");;All files (*.*)"));

	QStringList files;
	QByteArray file;
	if (filedialogGetOpenFiles(files, file, lang(lng_choose_images), filter)) {
		if (!file.isEmpty()) {
			uploadMedia(file, ToPreparePhoto);
		} else if (!files.isEmpty()) {
			uploadMedias(files, ToPreparePhoto);
		}
	}
}

void HistoryWidget::onDocumentSelect() {
	if (!_history) return;

	_attachPhoto.clearState();
	_attachPhoto.hide();
	_attachDocument.show();
	_attachType.fastHide();

	if (cDefaultAttach() != dbidaDocument) {
		cSetDefaultAttach(dbidaDocument);
		Local::writeUserSettings();
	}

	QStringList photoExtensions(cPhotoExtensions());
	QStringList imgExtensions(cImgExtensions());	
	QString filter(qsl("All files (*.*);;Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;Photo files (*") + photoExtensions.join(qsl(" *")) + qsl(")"));

	QStringList files;
	QByteArray file;
	if (filedialogGetOpenFiles(files, file, lang(lng_choose_images), filter)) {
		if (!file.isEmpty()) {
			uploadMedia(file, ToPrepareDocument);
		} else if (!files.isEmpty()) {
			uploadMedias(files, ToPrepareDocument);
		}
	}
}


void HistoryWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!_history) return;

	_attachDrag = getDragState(e->mimeData());
	updateDragAreas();

	if (_attachDrag) {
		e->setDropAction(Qt::IgnoreAction);
		e->accept();
	}
}

void HistoryWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto.isHidden() || !_attachDragDocument.isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
}

void HistoryWidget::leaveEvent(QEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto.isHidden() || !_attachDragDocument.isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
	if (hasMouseTracking()) mouseMoveEvent(0);
}

void HistoryWidget::mouseMoveEvent(QMouseEvent *e) {
	QPoint pos(e ? e->pos() : mapFromGlobal(QCursor::pos()));
	bool inRecord = _send.geometry().contains(pos);
	bool inField = pos.y() >= (_scroll.y() + _scroll.height()) && pos.y() < height() && pos.x() >= 0 && pos.x() < width();
	bool inReply = QRect(st::replySkip, _field.y() - st::sendPadding - st::replyHeight, width() - st::replySkip - _replyForwardPreviewCancel.width(), st::replyHeight).contains(pos) && replyToId();
	bool startAnim = false;
	if (inRecord != _inRecord) {
		_inRecord = inRecord;
		a_recordOver.start(_inRecord ? 1 : 0);
		a_recordDown.restart();
		a_recordCancel.restart();
		startAnim = true;
	}
	if (inField != _inField && _recording) {
		_inField = inField;
		a_recordOver.restart();
		a_recordDown.start(_inField ? 1 : 0);
		a_recordCancel.start(_inField ? st::recordCancel->c : st::recordCancelActive->c);
		startAnim = true;
	}
	if (inReply != _inReply) {
		_inReply = inReply;
		setCursor(inReply ? style::cur_pointer : style::cur_default);
	}
	if (startAnim) _recordAnim.start();
}

void HistoryWidget::leaveToChildEvent(QEvent *e) { // e -- from enterEvent() of child TWidget
	if (hasMouseTracking()) mouseMoveEvent(0);
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_replyForwardPressed) {
		_replyForwardPressed = false;
		update(0, _field.y() - st::sendPadding - st::replyHeight, width(), st::replyHeight);
	}
	if (_attachDrag != DragStateNone || !_attachDragPhoto.isHidden() || !_attachDragDocument.isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
	if (_recording && cHasAudioCapture()) {
		stopRecording(_peer && _inField);
	}
}

void HistoryWidget::stopRecording(bool send) {
	audioCapture()->stop(send);

	a_recordingLevel = anim::ivalue(0, 0);
	_recordingAnim.stop();

	_recording = false;
	_recordingSamples = 0;
	updateSendAction(_history, SendActionRecordAudio, -1);

	updateControlsVisibility();
	activate();

	updateField();

	a_recordDown.start(0);
	a_recordOver.restart();
	a_recordCancel = anim::cvalue(st::recordCancel->c, st::recordCancel->c);
	_recordAnim.start();
}

void HistoryWidget::sendBotCommand(const QString &cmd, MsgId replyTo) { // replyTo != 0 from ReplyKeyboardMarkup, == 0 from cmd links
	if (!_history) return;

	App::main()->readServerHistory(_history, false);
	fastShowAtEnd(_history);

	bool lastKeyboardUsed = (_keyboard.forMsgId() == _history->lastKeyboardId) && (_keyboard.forMsgId() == replyTo);

	QString toSend = cmd;
	UserData *bot = _peer->chat ? (App::hoveredLinkItem() ? (App::hoveredLinkItem()->toHistoryForwarded() ? App::hoveredLinkItem()->toHistoryForwarded()->fromForwarded() : App::hoveredLinkItem()->from()) : 0) : _peer->asUser();
	QString username = (bot && bot->botInfo) ? bot->username : QString();
	if (!replyTo && toSend.indexOf('@') < 2 && _peer->chat && !username.isEmpty() && (_peer->asChat()->botStatus == 0 || _peer->asChat()->botStatus == 2)) {
		toSend += '@' + username;
	}

	int32 botStatus = _peer->chat ? _peer->asChat()->botStatus : -1;
	App::main()->sendPreparedText(_history, toSend, replyTo ? ((_peer->chat/* && (botStatus == 0 || botStatus == 2)*/) ? replyTo : -1) : 0);
	if (replyTo) {
		cancelReply();
		if (_keyboard.singleUse() && _keyboard.hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) onKbToggle(false);
			_history->lastKeyboardUsed = true;
		}
	}

	_field.setFocus();
}

void HistoryWidget::insertBotCommand(const QString &cmd) {
	if (!_history) return;

	QString toInsert = cmd;
	UserData *bot = _peer->chat ? (App::hoveredLinkItem() ? (App::hoveredLinkItem()->toHistoryForwarded() ? App::hoveredLinkItem()->toHistoryForwarded()->fromForwarded() : App::hoveredLinkItem()->from()) : 0) : _peer->asUser();
	QString username = (bot && bot->botInfo) ? bot->username : QString();
	if (toInsert.indexOf('@') < 2 && _peer->chat && !username.isEmpty() && (_peer->asChat()->botStatus == 0 || _peer->asChat()->botStatus == 2)) {
		toInsert += '@' + username;
	}
	toInsert += ' ';

	QString text = _field.getLastText();
	QRegularExpressionMatch m = QRegularExpression(qsl("^/[A-Za-z_0-9]{0,64}(@[A-Za-z_0-9]{0,32})?(\\s|$)")).match(text);
	if (m.hasMatch()) {
		text = toInsert + text.mid(m.capturedLength());
	} else {
		text = toInsert + text;
	}
	_field.setText(text);

	QTextCursor cur(_field.textCursor());
	cur.movePosition(QTextCursor::End);
	_field.setTextCursor(cur);
}

bool HistoryWidget::eventFilter(QObject *obj, QEvent *e) {
	if (obj == &_toHistoryEnd && e->type() == QEvent::Wheel) {
		return _scroll.viewportEvent(e);
	}
	return TWidget::eventFilter(obj, e);
}

DragState HistoryWidget::getDragState(const QMimeData *d) {
	if (!d || d->hasFormat(qsl("application/x-td-forward-pressed-link"))) return DragStateNone;

	if (d->hasImage()) return DragStateImage;

	QString uriListFormat(qsl("text/uri-list"));
	if (!d->hasFormat(uriListFormat)) return DragStateNone;

	QStringList imgExtensions(cImgExtensions()), files;

	const QList<QUrl> &urls(d->urls());
	if (urls.isEmpty()) return DragStateNone;

	bool allAreSmallImages = true;
	for (QList<QUrl>::const_iterator i = urls.cbegin(), en = urls.cend(); i != en; ++i) {
		if (!i->isLocalFile()) return DragStateNone;

		QString file(i->toLocalFile());
		if (file.startsWith(qsl("/.file/id="))) file = psConvertFileUrl(file);

		quint64 s = QFileInfo(file).size();
		if (s >= MaxUploadDocumentSize) {
			return DragStateNone;
		}
		if (allAreSmallImages) {
			if (s >= MaxUploadPhotoSize) {
				allAreSmallImages = false;
			} else {
				bool foundImageExtension = false;
				for (QStringList::const_iterator j = imgExtensions.cbegin(), end = imgExtensions.cend(); j != end; ++j) {
					if (file.right(j->size()).toLower() == (*j).toLower()) {
						foundImageExtension = true;
						break;
					}
				}
				if (!foundImageExtension) {
					allAreSmallImages = false;
				}
			}
		}
	}
	return allAreSmallImages ? DragStatePhotoFiles : DragStateFiles;
}

void HistoryWidget::updateDragAreas() {
	_field.setAcceptDrops(!_attachDrag);
	switch (_attachDrag) {
	case DragStateNone:
		_attachDragDocument.otherLeave();
		_attachDragPhoto.otherLeave();
	break;
	case DragStateFiles:
		_attachDragDocument.otherEnter();
		_attachDragDocument.setText(lang(lng_drag_files_here), lang(lng_drag_to_send_files));
		_attachDragPhoto.fastHide();
	break;
	case DragStatePhotoFiles:
		_attachDragDocument.otherEnter();
		_attachDragDocument.setText(lang(lng_drag_images_here), lang(lng_drag_to_send_no_compression));
		_attachDragPhoto.otherEnter();
		_attachDragPhoto.setText(lang(lng_drag_photos_here), lang(lng_drag_to_send_quick));
	break;
	case DragStateImage:
		_attachDragDocument.fastHide();
		_attachDragPhoto.otherEnter();
		_attachDragPhoto.setText(lang(lng_drag_images_here), lang(lng_drag_to_send_quick));
	break;
	};
	resizeEvent(0);
}

bool HistoryWidget::isBotStart() const {
	if (!_peer || _peer->chat || !_peer->asUser()->botInfo) return false;
	return !_peer->asUser()->botInfo->startToken.isEmpty() || (_history->isEmpty() && !_history->lastMsg);
}

bool HistoryWidget::isBlocked() const {
	return _peer && !_peer->chat && _peer->asUser()->blocked == UserIsBlocked;
}

bool HistoryWidget::updateCmdStartShown() {
	bool cmdStartShown = false;
	if (_history && _peer && ((_peer->chat && _peer->asChat()->botStatus > 0) || (!_peer->chat && _peer->asUser()->botInfo))) {
		if (!isBotStart() && !isBlocked() && !_keyboard.hasMarkup() && !_keyboard.forceReply()) {
			if (_field.getLastText().isEmpty()) {
				cmdStartShown = true;
			}
		}
	}
	if (_cmdStartShown != cmdStartShown) {
		_cmdStartShown = cmdStartShown;
		return true;
	}
	return false;
}

void HistoryWidget::dropEvent(QDropEvent *e) {
	_attachDrag = DragStateNone;
	updateDragAreas();
	e->acceptProposedAction();
}

void HistoryWidget::onPhotoDrop(const QMimeData *data) {
	if (!_history) return;

	if (data->hasImage()) {
		QImage image = qvariant_cast<QImage>(data->imageData());
		if (image.isNull()) return;

		uploadImage(image, false, data->text());
	} else {
		QStringList files = getMediasFromMime(data);
		if (files.isEmpty()) return;

		uploadMedias(files, ToPreparePhoto);
	}
}

void HistoryWidget::onDocumentDrop(const QMimeData *data) {
	if (!_history) return;

	QStringList files = getMediasFromMime(data);
	if (files.isEmpty()) return;

	uploadMedias(files, ToPrepareDocument);
}

void HistoryWidget::onFilesDrop(const QMimeData *data) {
	if (data->hasImage()) {
		QImage image = qvariant_cast<QImage>(data->imageData());
		if (image.isNull()) return;

		uploadImage(image, false, data->text());
	} else {
		QStringList files = getMediasFromMime(data);
		if (files.isEmpty()) return;

		uploadMedias(files, ToPrepareAuto);
	}
}

void HistoryWidget::onKbToggle(bool manual) {
	if (_kbShown || _kbReplyTo) {
		_kbHide.hide();
		if (_kbShown) {
			_kbShow.show();
			if (manual) _kbWasHidden = true;

			_kbScroll.hide();
			_kbShown = false;

			_field.setMaxHeight(st::maxFieldHeight);

			_kbReplyTo = 0;
			if (!App::main()->hasForwardingItems() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
				_replyForwardPreviewCancel.hide();
			}
		} else {
			if (_history) {
				_history->clearLastKeyboard();
			}
			updateBotKeyboard();
		}
	} else if (!_keyboard.hasMarkup() && _keyboard.forceReply()) {
		_kbHide.hide();
		_kbShow.hide();
		_cmdStart.show();
		_kbScroll.hide();
		_kbShown = false;

		_field.setMaxHeight(st::maxFieldHeight);

		_kbReplyTo = (_history->peer->chat || _keyboard.forceReply()) ? App::histItemById(_keyboard.forMsgId()) : 0;
		if (_kbReplyTo && !_replyToId) {
			updateReplyToName();
			_replyToText.setText(st::msgFont, _kbReplyTo->inDialogsText(), _textDlgOptions);
			_replyForwardPreviewCancel.show();
		}
		if (manual) _kbWasHidden = false;
	} else {
		_kbHide.show();
		_kbShow.hide();
		_kbScroll.show();
		_kbShown = true;

		int32 maxh = qMin(_keyboard.height(), int(st::maxFieldHeight) - (int(st::maxFieldHeight) / 2));
		_field.setMaxHeight(st::maxFieldHeight - maxh);

		_kbReplyTo = (_history->peer->chat || _keyboard.forceReply()) ? App::histItemById(_keyboard.forMsgId()) : 0;
		if (_kbReplyTo && !_replyToId) {
			updateReplyToName();
			_replyToText.setText(st::msgFont, _kbReplyTo->inDialogsText(), _textDlgOptions);
			_replyForwardPreviewCancel.show();
		}
		if (manual) _kbWasHidden = false;
	}
	resizeEvent(0);
	if (_kbHide.isHidden()) {
		_attachEmoji.show();
	} else {
		_attachEmoji.hide();
	}
	updateField();
}

void HistoryWidget::onCmdStart() {
	setFieldText(qsl("/"));
	_field.moveCursor(QTextCursor::End);
}

void HistoryWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (!_list) return;

	return _list->showContextMenu(e);
}

void HistoryWidget::deleteMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->itemType() != HistoryItem::MsgType) return;

	HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
	App::main()->deleteLayer((msg && msg->uploading()) ? -2 : -1);
}

void HistoryWidget::forwardMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->itemType() != HistoryItem::MsgType) return;

	App::main()->forwardLayer();
}

void HistoryWidget::selectMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->itemType() != HistoryItem::MsgType) return;

	if (_list) _list->selectItem(item);
}

void HistoryWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (_showAnim.animating()) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimTopBarCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animTopBarCache);
		return;
	}

	if (!_history) return;

	int32 increaseLeft = cWideMode() ? 0 : (st::topBarForwardPadding.right() - st::topBarForwardPadding.left());
	decreaseWidth += increaseLeft;
	QRect rectForName(st::topBarForwardPadding.left() + increaseLeft, st::topBarForwardPadding.top(), width() - decreaseWidth - st::topBarForwardPadding.left() - st::topBarForwardPadding.right(), st::msgNameFont->height);
	p.setFont(st::dlgHistFont->f);
	if (_history->typing.isEmpty() && _history->sendActions.isEmpty()) {
		p.setPen(st::titleStatusColor->p);
		p.drawText(rectForName.x(), st::topBarHeight - st::topBarForwardPadding.bottom() - st::dlgHistFont->height + st::dlgHistFont->ascent, _titlePeerText);
	} else {
		p.setPen(st::titleTypingColor->p);
		_history->typingText.drawElided(p, rectForName.x(), st::topBarHeight - st::topBarForwardPadding.bottom() - st::dlgHistFont->height, rectForName.width());
	}

	p.setPen(st::dlgNameColor->p);
	_history->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());

	if (cWideMode()) {
		p.setOpacity(st::topBarForwardAlpha + (1 - st::topBarForwardAlpha) * over);
		p.drawPixmap(QPoint(width() - (st::topBarForwardPadding.right() + st::topBarForwardImg.pxWidth()) / 2, (st::topBarHeight - st::topBarForwardImg.pxHeight()) / 2), App::sprite(), st::topBarForwardImg);
	} else {
		p.setOpacity(st::topBarForwardAlpha + (1 - st::topBarForwardAlpha) * over);
		p.drawPixmap(QPoint((st::topBarForwardPadding.right() - st::topBarBackwardImg.pxWidth()) / 2, (st::topBarHeight - st::topBarBackwardImg.pxHeight()) / 2), App::sprite(), st::topBarBackwardImg);
	}
}

void HistoryWidget::topBarShadowParams(int32 &x, float64 &o) {
	if (_showAnim.animating() && a_coord.current() >= 0) {
		x = a_coord.current();
		o = a_alpha.current();
	}
}

void HistoryWidget::topBarClick() {
	if (cWideMode()) {
		if (_history) App::main()->showPeerProfile(_peer);
	} else {
		App::main()->showDialogs();
	}
}

void HistoryWidget::updateOnlineDisplay(int32 x, int32 w) {
	if (!_history) return;

	QString text;
	int32 t = unixtime();
	if (_peer->chat) {
		ChatData *chat = _peer->asChat();
		if (chat->forbidden || chat->left) {
			text = lang(lng_chat_status_unaccessible);
		} else if (chat->participants.isEmpty()) {
			text = _titlePeerText.isEmpty() ? lng_chat_status_members(lt_count, chat->count < 0 ? 0 : chat->count) : _titlePeerText;
		} else {
			int32 onlineCount = 0;
            bool onlyMe = true;
			for (ChatData::Participants::const_iterator i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
				if (i.key()->onlineTill > t) {
					++onlineCount;
                    if (onlyMe && i.key() != App::self()) onlyMe = false;
				}
			}
            if (onlineCount && !onlyMe) {
				text = lng_chat_status_members_online(lt_count, chat->participants.size(), lt_count_online, onlineCount);
			} else {
				text = lng_chat_status_members(lt_count, chat->participants.size());
			}
		}
	} else {
		text = App::onlineText(_peer->asUser(), t);
	}
	if (_titlePeerText != text) {
		_titlePeerText = text;
		_titlePeerTextWidth = st::dlgHistFont->m.width(_titlePeerText);
		if (App::main()) {
			App::main()->topBar()->update();
		}
	}
	updateOnlineDisplayTimer();
}

void HistoryWidget::updateOnlineDisplayTimer() {
	if (!_history) return;

	int32 t = unixtime(), minIn = 86400;
	if (_peer->chat) {
		ChatData *chat = _peer->asChat();
		if (chat->participants.isEmpty()) return;

		for (ChatData::Participants::const_iterator i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key(), t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else {
		minIn = App::onlineWillChangeIn(_peer->asUser(), t);
	}
	App::main()->updateOnlineDisplayIn(minIn * 1000);
}

void HistoryWidget::onFieldResize() {
	int32 maxKeyboardHeight = int(st::maxFieldHeight) - _field.height();
	_keyboard.resizeToWidth(width(), maxKeyboardHeight);

	int32 kbh = 0;
	if (_kbShown) {
		kbh = qMin(_keyboard.height(), maxKeyboardHeight);
		_kbScroll.setGeometry(0, height() - kbh, width(), kbh);
	}
	_field.move(_attachDocument.x() + _attachDocument.width(), height() - kbh - _field.height() - st::sendPadding);
	_replyForwardPreviewCancel.move(width() - _replyForwardPreviewCancel.width(), _field.y() - st::sendPadding - _replyForwardPreviewCancel.height());

	_attachDocument.move(0, height() - kbh - _attachDocument.height());
	_attachPhoto.move(_attachDocument.x(), _attachDocument.y());
	_botStart.setGeometry(0, _attachDocument.y(), width(), _botStart.height());
	_unblock.setGeometry(0, _attachDocument.y(), width(), _unblock.height());
	_send.move(width() - _send.width(), _attachDocument.y());
	_attachEmoji.move(_send.x() - _attachEmoji.width(), height() - kbh - _attachEmoji.height());
	_kbShow.move(_attachEmoji.x() - _kbShow.width(), height() - kbh - _kbShow.height());
	_kbHide.move(_attachEmoji.x(), _attachEmoji.y());
	_cmdStart.move(_attachEmoji.x() - _cmdStart.width(), height() - kbh - _cmdStart.height());

	_attachType.move(0, _attachDocument.y() - _attachType.height());
	_emojiPan.move(width() - _emojiPan.width(), _attachEmoji.y() - _emojiPan.height());

	updateListSize();
	updateField();
}

void HistoryWidget::onFieldFocused() {
	if (_list) _list->clearSelectedItems(true);
}

void HistoryWidget::checkMentionDropdown() {
	if (!_history || _showAnim.animating()) return;

	QString start;
	_field.getMentionHashtagBotCommandStart(start);
	if (!start.isEmpty()) {
		if (start.at(0) == '#' && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) Local::readRecentHashtags();
		if (start.at(0) == '@' && !_history->peer->chat) return;
		if (start.at(0) == '/' && !_history->peer->chat && !_history->peer->asUser()->botInfo) return;
		_attachMention.showFiltered(_history->peer, start);
	} else if (!_attachMention.isHidden()) {
		_attachMention.hideStart();
	}
}

void HistoryWidget::onFieldCursorChanged() {
	checkMentionDropdown();
	onDraftSaveDelayed();
}

void HistoryWidget::uploadImage(const QImage &img, bool withText, const QString &source) {
	if (!_history || _confirmImageId) return;

	App::wnd()->activateWindow();
	_confirmImage = img;
	_confirmWithText = withText;
	_confirmSource = source;
	_confirmImageId = _imageLoader.append(img, _peer->id, replyToId(), ToPreparePhoto);
}

void HistoryWidget::uploadFile(const QString &file, bool withText) {
	if (!_history || _confirmImageId) return;

	App::wnd()->activateWindow();
	_confirmWithText = withText;
	_confirmImageId = _imageLoader.append(file, _peer->id, replyToId(), ToPrepareDocument);
}

void HistoryWidget::shareContactConfirmation(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool withText) {
	if (!_history || _confirmImageId) return;

	App::wnd()->activateWindow();
	_confirmWithText = withText;
	_confirmImageId = 0xFFFFFFFFFFFFFFFFL;
	App::wnd()->showLayer(new PhotoSendBox(phone, fname, lname, replyTo));
}

void HistoryWidget::uploadConfirmImageUncompressed(bool ctrlShiftEnter, MsgId replyTo) {
	if (!_history || !_confirmImageId || _confirmImage.isNull()) return;

	App::wnd()->activateWindow();
	PeerId peerId = _peer->id;
	if (_confirmWithText) {
		onSend(ctrlShiftEnter, replyTo);
	}
	bool lastKeyboardUsed = lastForceReplyReplied(replyTo);
	_imageLoader.append(_confirmImage, peerId, replyTo, ToPrepareDocument, ctrlShiftEnter);
	_confirmImageId = 0;
	_confirmWithText = false;
	_confirmImage = QImage();
	cancelReply(lastKeyboardUsed);
}

void HistoryWidget::uploadMedias(const QStringList &files, ToPrepareMediaType type) {
	if (!_history) return;

	App::wnd()->activateWindow();
	_imageLoader.append(files, _peer->id, replyToId(), type);
	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::uploadMedia(const QByteArray &fileContent, ToPrepareMediaType type, PeerId peer) {
	if (!peer && !_history) return;

	App::wnd()->activateWindow();
	_imageLoader.append(fileContent, peer ? peer : _peer->id, replyToId(), type);
	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::onPhotoReady() {
	QMutexLocker lock(_imageLoader.readyMutex());
	ReadyLocalMedias &list(_imageLoader.readyList());

	for (ReadyLocalMedias::const_iterator i = list.cbegin(), e = list.cend(); i != e; ++i) {
		if (i->id == _confirmImageId) {
			PhotoSendBox *box = new PhotoSendBox(*i);
			connect(box, SIGNAL(confirmed()), this, SLOT(onSendConfirmed()));
			connect(box, SIGNAL(destroyed(QObject*)), this, SLOT(onSendCancelled()));
			App::wnd()->showLayer(box);
		} else {
			confirmSendImage(*i);
		}
	}
	list.clear();
}

void HistoryWidget::onSendConfirmed() {
	if (!_confirmSource.isEmpty()) _confirmSource = QString();
}

void HistoryWidget::onSendCancelled() {
	if (!_confirmSource.isEmpty()) {
		_field.textCursor().insertText(_confirmSource);
		_confirmSource = QString();
	}
}

void HistoryWidget::onPhotoFailed(quint64 id) {
}

void HistoryWidget::confirmShareContact(bool ctrlShiftEnter, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo) {
	if (!_peer) return;

	PeerId peerId = _peer->id;
	if (0xFFFFFFFFFFFFFFFFL == _confirmImageId) {
		if (_confirmWithText) {
			onSend(ctrlShiftEnter, replyTo);
		}
		_confirmImageId = 0;
		_confirmWithText = false;
		_confirmImage = QImage();
	}
	shareContact(peerId, phone, fname, lname, replyTo);
}

void HistoryWidget::confirmSendImage(const ReadyLocalMedia &img) {
	if (img.id == _confirmImageId) {
		if (_confirmWithText) {
			onSend(img.ctrlShiftEnter, img.replyTo);
		}
		_confirmImageId = 0;
		_confirmWithText = false;
		_confirmImage = QImage();
	}
	MsgId newId = clientMsgId();

	connect(App::uploader(), SIGNAL(photoReady(MsgId, const MTPInputFile &)), this, SLOT(onPhotoUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentReady(MsgId, const MTPInputFile &)), this, SLOT(onDocumentUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(thumbDocumentReady(MsgId, const MTPInputFile &, const MTPInputFile &)), this, SLOT(onThumbDocumentUploaded(MsgId, const MTPInputFile &, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(audioReady(MsgId, const MTPInputFile &)), this, SLOT(onAudioUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoProgress(MsgId)), this, SLOT(onPhotoProgress(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentProgress(MsgId)), this, SLOT(onDocumentProgress(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(audioProgress(MsgId)), this, SLOT(onAudioProgress(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoFailed(MsgId)), this, SLOT(onPhotoFailed(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentFailed(MsgId)), this, SLOT(onDocumentFailed(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(audioFailed(MsgId)), this, SLOT(onAudioFailed(MsgId)), Qt::UniqueConnection);

	App::uploader()->uploadMedia(newId, img);

	History *h = App::history(img.peer);

	fastShowAtEnd(h);

	int32 flags = newMessageFlags(h->peer) | MTPDmessage::flag_media; // unread, out
	if (img.replyTo) flags |= MTPDmessage::flag_reply_to_msg_id;
	if (img.type == ToPreparePhoto) {
		h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTPint(), MTPint(), MTP_int(img.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaPhoto(img.photo, MTP_string("")), MTPnullMarkup, MTPnullEntities));
	} else if (img.type == ToPrepareDocument) {
		h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTPint(), MTPint(), MTP_int(img.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(img.document), MTPnullMarkup, MTPnullEntities));
	} else if (img.type == ToPrepareAudio) {
		flags |= MTPDmessage_flag_media_unread;
		h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTPint(), MTPint(), MTP_int(img.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaAudio(img.audio), MTPnullMarkup, MTPnullEntities));
	}

	if (_peer && img.peer == _peer->id) {
		App::main()->historyToDown(_history);
	}
	App::main()->dialogsToUp();
	peerMessagesUpdated(img.peer);
}

void HistoryWidget::cancelSendImage() {
	if (_confirmImageId && _confirmWithText) setFieldText(QString());
	_confirmImageId = 0;
	_confirmWithText = false;
	_confirmImage = QImage();
}

void HistoryWidget::onPhotoUploaded(MsgId newId, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		uint64 randomId = MTP::nonce<uint64>();
		App::historyRegRandom(randomId, newId);
		History *hist = item->history();
		MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
		int32 sendFlags = 0;
		if (replyTo) {
			sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
		}
		hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedPhoto(file, MTP_string("")), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendPhotoFail, randomId), 0, 0, hist->sendRequestId);
	}
}

namespace {
	MTPVector<MTPDocumentAttribute> _composeDocumentAttributes(DocumentData *document) {
		QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(document->name)));
		if (document->dimensions.width() > 0 && document->dimensions.height() > 0) {
			attributes.push_back(MTP_documentAttributeImageSize(MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
		}
		if (document->type == AnimatedDocument) {
			attributes.push_back(MTP_documentAttributeAnimated());
		} else if (document->type == StickerDocument && document->sticker()) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_string(document->sticker()->alt), document->sticker()->set));
		} else if (document->type == SongDocument && document->song()) {
			attributes.push_back(MTP_documentAttributeAudio(MTP_int(document->song()->duration), MTP_string(document->song()->title), MTP_string(document->song()->performer)));
		}
		return MTP_vector<MTPDocumentAttribute>(attributes);
	}
}

void HistoryWidget::onDocumentUploaded(MsgId newId, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::histItemById(newId));
	if (item) {
		DocumentData *document = 0;
		if (HistoryDocument *media = dynamic_cast<HistoryDocument*>(item->getMedia())) {
			document = media->document();
		} else if (HistorySticker *media = dynamic_cast<HistorySticker*>(item->getMedia())) {
			document = media->document();
		}
		if (document) {
			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedDocument(file, MTP_string(document->mime), _composeDocumentAttributes(document)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onThumbDocumentUploaded(MsgId newId, const MTPInputFile &file, const MTPInputFile &thumb) {
	if (!MTP::authedId()) return;
	HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::histItemById(newId));
	if (item) {
		DocumentData *document = 0;
		if (HistoryDocument *media = dynamic_cast<HistoryDocument*>(item->getMedia())) {
			document = media->document();
		} else if (HistorySticker *media = dynamic_cast<HistorySticker*>(item->getMedia())) {
			document = media->document();
		}
		if (document) {
			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedThumbDocument(file, thumb, MTP_string(document->mime), _composeDocumentAttributes(document)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onAudioUploaded(MsgId newId, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::histItemById(newId));
	if (item) {
		AudioData *audio = 0;
		if (HistoryAudio *media = dynamic_cast<HistoryAudio*>(item->getMedia())) {
			audio = media->audio();
		}
		if (audio) {
			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedAudio(file, MTP_int(audio->duration), MTP_string(audio->mime)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onPhotoProgress(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		PhotoData *photo = (item->getMedia() && item->getMedia()->type() == MediaTypePhoto) ? static_cast<HistoryPhoto*>(item->getMedia())->photo() : 0;
		updateSendAction(item->history(), SendActionUploadPhoto, 0);
//		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onDocumentProgress(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		DocumentData *doc = (item->getMedia() && item->getMedia()->type() == MediaTypeDocument) ? static_cast<HistoryDocument*>(item->getMedia())->document() : 0;
		updateSendAction(item->history(), SendActionUploadFile, doc ? doc->uploadOffset : 0);
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onAudioProgress(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		AudioData *audio = (item->getMedia() && item->getMedia()->type() == MediaTypeAudio) ? static_cast<HistoryAudio*>(item->getMedia())->audio() : 0;
		updateSendAction(item->history(), SendActionUploadAudio, audio ? audio->uploadOffset : 0);
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onPhotoFailed(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		updateSendAction(item->history(), SendActionUploadPhoto, -1);
//		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onDocumentFailed(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		updateSendAction(item->history(), SendActionUploadFile, -1);
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onAudioFailed(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		updateSendAction(item->history(), SendActionUploadAudio, -1);
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onReportSpamClicked() {
	if (_reportSpamRequest) return;

	if (!_peer->chat) MTP::send(MTPcontacts_Block(_peer->asUser()->inputUser), rpcDone(&HistoryWidget::blockDone, _peer), RPCFailHandlerPtr(), 0, 5);
	_reportSpamRequest = MTP::send(MTPmessages_ReportSpam(_peer->input), rpcDone(&HistoryWidget::reportSpamDone, _peer), rpcFail(&HistoryWidget::reportSpamFail));
}

void HistoryWidget::reportSpamDone(PeerData *peer, const MTPBool &result, mtpRequestId req) {
	if (req == _reportSpamRequest) {
		_reportSpamRequest = 0;
	}
	if (peer) {
		cRefReportSpamStatuses().insert(peer->id, dbiprsReportSent);
		Local::writeReportSpamStatuses();
	}
	_reportSpamStatus = dbiprsReportSent;
	_reportSpamPanel.setReported(_reportSpamStatus == dbiprsReportSent);
}

bool HistoryWidget::reportSpamFail(const RPCError &error, mtpRequestId req) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	if (req == _reportSpamRequest) {
		_reportSpamRequest = 0;
	}
	return false;
}

void HistoryWidget::onReportSpamHide() {
	if (_peer) {
		cRefReportSpamStatuses().insert(_peer->id, dbiprsNoButton);
		Local::writeReportSpamStatuses();
	}
	_reportSpamStatus = dbiprsNoButton;
	updateControlsVisibility();
}

void HistoryWidget::onReportSpamClear() {
	ConfirmBox *box = new ConfirmBox(_peer->chat ? lng_sure_delete_and_exit(lt_group, _peer->name) : lng_sure_delete_history(lt_contact, _peer->name));
	connect(box, SIGNAL(confirmed()), this, SLOT(onReportSpamClearSure()));
	App::wnd()->showLayer(box);
	_clearPeer = _peer;
}

void HistoryWidget::onReportSpamClearSure() {
	if (_clearPeer->chat) {
		App::wnd()->hideLayer();
		App::main()->showDialogs();
		MTP::send(MTPmessages_DeleteChatUser(MTP_int(_clearPeer->id & 0xFFFFFFFF), App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, _clearPeer), App::main()->rpcFail(&MainWidget::leaveChatFailed, _clearPeer));
	} else {
		App::main()->deleteConversation(_clearPeer);
	}
}

void HistoryWidget::peerMessagesUpdated(PeerId peer) {
	if (_peer && _list && peer == _peer->id) {
		updateListSize();
		updateBotKeyboard();
		if (!_scroll.isHidden() && !isBlocked() && _botStart.isHidden() == isBotStart()) {
			updateControlsVisibility();
			resizeEvent(0);
		}
	}
}

void HistoryWidget::peerMessagesUpdated() {
	if (_list) peerMessagesUpdated(_peer->id);
}

void HistoryWidget::msgUpdated(PeerId peer, const HistoryItem *msg) {
	if (_peer && _list && peer == _peer->id) {
		_list->updateMsg(msg);
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	_reportSpamPanel.resize(width(), _reportSpamPanel.height());

	int32 maxKeyboardHeight = int(st::maxFieldHeight) - _field.height();
	_keyboard.resizeToWidth(width(), maxKeyboardHeight);

	int32 kbh = 0;
	if (_kbShown) {
		kbh = qMin(_keyboard.height(), maxKeyboardHeight);
		_kbScroll.setGeometry(0, height() - kbh, width(), kbh);
	}
	_field.move(_attachDocument.x() + _attachDocument.width(), height() - kbh - _field.height() - st::sendPadding);

	_attachDocument.move(0, height() - kbh - _attachDocument.height());
	_attachPhoto.move(_attachDocument.x(), _attachDocument.y());

	_replyForwardPreviewCancel.move(width() - _replyForwardPreviewCancel.width(), _field.y() - st::sendPadding - _replyForwardPreviewCancel.height());
	updateListSize(App::main() ? App::main()->contentScrollAddToY() : 0);

	bool kbShowShown = _history && !_kbShown && _keyboard.hasMarkup();
	_field.resize(width() - _send.width() - _attachDocument.width() - _attachEmoji.width() - (kbShowShown ? _kbShow.width() : 0) - (_cmdStartShown ? _cmdStart.width() : 0), _field.height());

	_toHistoryEnd.move((width() - _toHistoryEnd.width()) / 2, _scroll.y() + _scroll.height() - _toHistoryEnd.height() - st::historyToEndSkip);

	_send.move(width() - _send.width(), _attachDocument.y());
	_botStart.setGeometry(0, _attachDocument.y(), width(), _botStart.height());
	_unblock.setGeometry(0, _attachDocument.y(), width(), _unblock.height());
	_attachEmoji.move(_send.x() - _attachEmoji.width(), height() - kbh - _attachEmoji.height());
	_kbShow.move(_attachEmoji.x() - _kbShow.width(), height() - kbh - _kbShow.height());
	_kbHide.move(_attachEmoji.x(), _attachEmoji.y());
	_cmdStart.move(_attachEmoji.x() - _cmdStart.width(), height() - kbh - _cmdStart.height());

	_attachType.move(0, _attachDocument.y() - _attachType.height());
	_emojiPan.setMaxHeight(height() - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom() - _attachEmoji.height());
	_emojiPan.move(width() - _emojiPan.width(), _attachEmoji.y() - _emojiPan.height());

	switch (_attachDrag) {
	case DragStateFiles:
		_attachDragDocument.resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragDocument.move(st::dragMargin.left(), st::dragMargin.top());
	break;
	case DragStatePhotoFiles:
		_attachDragDocument.resize(width() - st::dragMargin.left() - st::dragMargin.right(), (height() - st::dragMargin.top() - st::dragMargin.bottom()) / 2);
		_attachDragDocument.move(st::dragMargin.left(), st::dragMargin.top());
		_attachDragPhoto.resize(_attachDragDocument.width(), _attachDragDocument.height());
		_attachDragPhoto.move(st::dragMargin.left(), height() - _attachDragPhoto.height() - st::dragMargin.bottom());
	break;
	case DragStateImage:
		_attachDragPhoto.resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragPhoto.move(st::dragMargin.left(), st::dragMargin.top());
	break;
	}
}

void HistoryWidget::itemRemoved(HistoryItem *item) {
	if (_list) _list->itemRemoved(item);
	if (item == _replyTo) {
		cancelReply();
	}
	if (item == _replyReturn) {
		calcNextReplyReturn();
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		onKbToggle();
		_kbReplyTo = 0;
	}
}

void HistoryWidget::itemReplaced(HistoryItem *oldItem, HistoryItem *newItem) {
	if (_list) _list->itemReplaced(oldItem, newItem);
	if (_replyTo == oldItem) _replyTo = newItem;
	if (_kbReplyTo == oldItem) _kbReplyTo = newItem;
	if (_replyReturn == oldItem) _replyReturn = newItem;
}

void HistoryWidget::itemResized(HistoryItem *row, bool scrollToIt) {
	updateListSize(0, false, false, row, scrollToIt);
}

void HistoryWidget::updateScrollColors() {
	if (!App::historyScrollBarColor()) return;
	_scroll.updateColors(App::historyScrollBarColor(), App::historyScrollBgColor(), App::historyScrollBarOverColor(), App::historyScrollBgOverColor());
}

MsgId HistoryWidget::replyToId() const {
	return _replyToId ? _replyToId : (_kbReplyTo ? _kbReplyTo->id : 0);
}

void HistoryWidget::updateListSize(int32 addToY, bool initial, bool loadedDown, HistoryItem *resizedItem, bool scrollToIt) {
	if (!_history || (initial && _histInited) || (!initial && !_histInited)) return;
	if (_firstLoadRequest) {
		if (resizedItem) _list->recountHeight(resizedItem);
		return; // scrollTopMax etc are not working after recountHeight()
	}

	int32 newScrollHeight = height();
	if (isBlocked()) {
		newScrollHeight -= _unblock.height();
	} else if (isBotStart()) {
		newScrollHeight -= _botStart.height();
	} else {
		if ((_peer->chat && !_peer->asChat()->forbidden && !_peer->asChat()->left) || (!_peer->chat && _peer->asUser()->access != UserNoAccess)) {
			newScrollHeight -= (_field.height() + 2 * st::sendPadding);
		}
		if (replyToId() || App::main()->hasForwardingItems() || (_previewData && _previewData->pendingTill >= 0)) {
			newScrollHeight -= st::replyHeight;
		}
		if (_kbShown) {
			newScrollHeight -= _kbScroll.height();
		}
	}
	bool wasAtBottom = _scroll.scrollTop() + 1 > _scroll.scrollTopMax(), needResize = _scroll.width() != width() || _scroll.height() != newScrollHeight;
	if (needResize) {
		_scroll.resize(width(), newScrollHeight);
		_attachMention.setBoundings(_scroll.geometry());
		_toHistoryEnd.move((width() - _toHistoryEnd.width()) / 2, _scroll.y() + _scroll.height() - _toHistoryEnd.height() - st::historyToEndSkip);
	}

	if (!initial) {
		_history->lastScrollTop = _scroll.scrollTop();
	}
	int32 newSt = _list->recountHeight(resizedItem);
	bool washidden = _scroll.isHidden();
	if (washidden) {
		_scroll.show();
	}
	_list->updateSize();
	int32 firstItemY = _list->height() - _history->height - st::historyPadding;
	if (resizedItem && !resizedItem->detached() && scrollToIt) {
		if (newSt + _scroll.height() < firstItemY + resizedItem->block()->y + resizedItem->y + resizedItem->height()) {
			newSt = firstItemY + resizedItem->block()->y + resizedItem->y + resizedItem->height() - _scroll.height();
		}
		if (newSt > firstItemY + resizedItem->block()->y + resizedItem->y) {
			newSt = firstItemY + resizedItem->block()->y + resizedItem->y;
		}
		wasAtBottom = false;
	}
	if (washidden) {
		_scroll.hide();
	}

	if ((!initial && !wasAtBottom) || loadedDown) {
		_scroll.scrollToY(newSt + addToY);
		return;
	}

	if (initial) {
		_histInited = true;
	}

	int32 toY = History::ScrollMax;
	if (initial && _history->lastWidth) {
		toY = newSt;
		_history->lastWidth = 0;
	} else if (initial && _showAtMsgId > 0) {
		HistoryItem *item = App::histItemById(_showAtMsgId);
		if (!item || item->detached()) {
			_showAtMsgId = 0;
			_histInited = false;
			return updateListSize(addToY, initial);
		} else {
			toY = (_scroll.height() > item->height()) ? qMax(firstItemY + item->y + item->block()->y - (_scroll.height() - item->height()) / 2, 0) : (firstItemY + item->y + item->block()->y);
			_animActiveStart = getms();
			_animActiveTimer.start(AnimationTimerDelta);
			_activeAnimMsgId = _showAtMsgId;
		}
	} else if (initial && _history->unreadBar) {
		toY = firstItemY + _history->unreadBar->y + _history->unreadBar->block()->y;
	} else if (_history->showFrom) {
		toY = firstItemY + _history->showFrom->y + _history->showFrom->block()->y;
		if (toY < _scroll.scrollTopMax() + st::unreadBarHeight) {
			_history->addUnreadBar();
			if (_history->unreadBar) {
				_showAtMsgId = ShowAtUnreadMsgId;
				_histInited = false;
				return updateListSize(0, true);
			}
		}
	} else {
	}
	_scroll.scrollToY(toY);
}

void HistoryWidget::addMessagesToFront(const QVector<MTPMessage> &messages) {
	int32 oldH = _history->height;
	_list->messagesReceived(messages);
	if (!_firstLoadRequest) {
		updateListSize(_history->height - oldH);
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(const QVector<MTPMessage> &messages) {
	_list->messagesReceivedDown(messages);
	if (!_firstLoadRequest) {
		updateListSize(0, false, true);
	}
}

void HistoryWidget::countHistoryShowFrom() {
	if (_showAtMsgId != ShowAtUnreadMsgId || !_history->unreadCount) {
		_history->showFrom = 0;
		return;
	}
	if (_history->showFrom) return;

	int32 skip = qMin(MessagesPerPage / 2, _history->unreadCount);
	for (History::const_iterator i = _history->cend(); i != _history->cbegin();) {
		--i;
		for (HistoryBlock::const_iterator j = (*i)->cend(); j != (*i)->cbegin();) {
			--j;
			if ((*j)->itemType() == HistoryItem::MsgType) {
				if (!--skip) {
					_history->showFrom = *j;
					return;
				}
			}
		}
	}
}

void HistoryWidget::updateBotKeyboard() {
	bool changed = false;
	bool wasVisible = _kbShown || _kbReplyTo;
	if ((_replyToId && !_replyTo) || !_history) {
		changed = _keyboard.updateMarkup(0);
	} else if (_replyTo) {
		changed = _keyboard.updateMarkup(_replyTo);
	} else {
		changed = _keyboard.updateMarkup(_history->lastKeyboardId ? App::histItemById(_history->lastKeyboardId) : 0);
	}
	updateCmdStartShown();
	if (!changed) return;

	bool hasMarkup = _keyboard.hasMarkup(), forceReply = _keyboard.forceReply() && !_replyTo;
	if (hasMarkup || forceReply) {
		if (_keyboard.singleUse() && _keyboard.hasMarkup() && _keyboard.forMsgId() == _history->lastKeyboardId && _history->lastKeyboardUsed) _kbWasHidden = true;
		if (!isBotStart() && !isBlocked() && (wasVisible || _replyTo || (_field.getLastText().isEmpty() && !_kbWasHidden))) {
			if (!_showAnim.animating()) {
				if (hasMarkup) {
					_kbScroll.show();
					_attachEmoji.hide();
					_kbHide.show();
				} else {
					_kbScroll.hide();
					_attachEmoji.show();
					_kbHide.hide();
				}
				_kbShow.hide();
				_cmdStart.hide();
			}
			int32 maxh = hasMarkup ? qMin(_keyboard.height(), int(st::maxFieldHeight) - (int(st::maxFieldHeight) / 2)) : 0;
			_field.setMaxHeight(st::maxFieldHeight - maxh);
			_kbShown = hasMarkup;
			_kbReplyTo = (_history->peer->chat || _keyboard.forceReply()) ? App::histItemById(_keyboard.forMsgId()) : 0;
			if (_kbReplyTo && !_replyToId) {
				updateReplyToName();
				_replyToText.setText(st::msgFont, _kbReplyTo->inDialogsText(), _textDlgOptions);
				_replyForwardPreviewCancel.show();
			}
		} else {
			if (!_showAnim.animating()) {
				_kbScroll.hide();
				_attachEmoji.show();
				_kbHide.hide();
				_kbShow.show();
				_cmdStart.hide();
			}
			_field.setMaxHeight(st::maxFieldHeight);
			_kbShown = false;
			_kbReplyTo = 0;
			if (!App::main()->hasForwardingItems() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
				_replyForwardPreviewCancel.hide();
			}
		}
	} else {
		if (!_scroll.isHidden()) {
			_kbScroll.hide();
			_attachEmoji.show();
			_kbHide.hide();
			_kbShow.hide();
			_cmdStart.show();
		}
		_field.setMaxHeight(st::maxFieldHeight);
		_kbShown = false;
		_kbReplyTo = 0;
		if (!App::main()->hasForwardingItems() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
			_replyForwardPreviewCancel.hide();
		}
	}
	resizeEvent(0);
	update();
}

void HistoryWidget::updateToEndVisibility() {
	bool toEndVisible = !_showAnim.animating() && _history && !_firstLoadRequest && (!_history->loadedAtBottom() || _replyReturn || _scroll.scrollTop() + st::wndMinHeight < _scroll.scrollTopMax());
	if (toEndVisible && _toHistoryEnd.isHidden()) {
		_toHistoryEnd.show();
	} else if (!toEndVisible && !_toHistoryEnd.isHidden()) {
		_toHistoryEnd.hide();
	}
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	_replyForwardPressed = QRect(0, _field.y() - st::sendPadding - st::replyHeight, st::replySkip, st::replyHeight).contains(e->pos());
	if (_replyForwardPressed && !_replyForwardPreviewCancel.isHidden()) {
		updateField();
	} else if (_inRecord && cHasAudioCapture()) {
		audioCapture()->start();

		_recording = _inField = true;
		updateControlsVisibility();
		activate();

		updateField();

		a_recordDown.start(1);
		a_recordOver.restart();
		_recordAnim.start();
	} else if (_inReply) {
		App::main()->showPeerHistory(_peer->id, replyToId());
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	MsgId msgid = qMax(_showAtMsgId, 0);
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		onCancel();
	} else if (e->key() == Qt::Key_PageDown) {
		if ((e->modifiers() & Qt::ControlModifier) || (e->modifiers() & Qt::MetaModifier)) {
			PeerData *after = 0;
			MsgId afterMsgId = 0;
			App::main()->peerAfter(_peer, msgid, after, afterMsgId);
			if (after) App::main()->showPeerHistory(after->id, afterMsgId);
		} else {
			_scroll.keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_PageUp) {
		if ((e->modifiers() & Qt::ControlModifier) || (e->modifiers() & Qt::MetaModifier)) {
			PeerData *before = 0;
			MsgId beforeMsgId = 0;
			App::main()->peerBefore(_peer, msgid, before, beforeMsgId);
			if (before) App::main()->showPeerHistory(before->id, beforeMsgId);
		} else {
			_scroll.keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Down) {
		if (e->modifiers() & Qt::AltModifier) {
			PeerData *after = 0;
			MsgId afterMsgId = 0;
			App::main()->peerAfter(_peer, msgid, after, afterMsgId);
			if (after) App::main()->showPeerHistory(after->id, afterMsgId);
		} else if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll.keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Up) {
		if (e->modifiers() & Qt::AltModifier) {
			PeerData *before = 0;
			MsgId beforeMsgId = 0;
			App::main()->peerBefore(_peer, msgid, before, beforeMsgId);
			if (before) App::main()->showPeerHistory(before->id, beforeMsgId);
		} else if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll.keyPressEvent(e);
		}
	} else if ((e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) && ((e->modifiers() & Qt::ControlModifier) || (e->modifiers() & Qt::MetaModifier))) {
		PeerData *p = 0;
		MsgId m = 0;
		if ((e->modifiers() & Qt::ShiftModifier) || e->key() == Qt::Key_Backtab) {
			App::main()->peerBefore(_peer, msgid, p, m);
		} else {
			App::main()->peerAfter(_peer, msgid, p, m);
		}
		if (p) App::main()->showPeerHistory(p->id, m);
	} else if (_history && (e->key() == Qt::Key_Search || e == QKeySequence::Find)) {
		App::main()->searchInPeer(_history->peer);
	} else {
		e->ignore();
	}
}

void HistoryWidget::onFieldTabbed() {
	QString sel = _attachMention.isHidden() ? QString() : _attachMention.getSelected();
	if (!sel.isEmpty()) {
		_field.onMentionHashtagOrBotCommandInsert(sel);
	}
}

void HistoryWidget::onStickerSend(DocumentData *sticker) {
	if (!_history || !sticker) return;

	App::main()->readServerHistory(_history, false);
	fastShowAtEnd(_history);

	uint64 randomId = MTP::nonce<uint64>();
	MsgId newId = clientMsgId();

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = (_peer->input.type() != mtpc_inputPeerSelf), unread = (_peer->input.type() != mtpc_inputPeerSelf);
	int32 flags = newMessageFlags(_peer) | MTPDmessage::flag_media; // unread, out
	int32 sendFlags = 0;
	if (replyToId()) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}
	_history->addToBackDocument(newId, flags, replyToId(), date(MTP_int(unixtime())), MTP::authedId(), sticker);

	_history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), _peer->input, MTP_int(replyToId()), MTP_inputMediaDocument(MTP_inputDocument(MTP_long(sticker->id), MTP_long(sticker->access))), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history);
	cancelReply(lastKeyboardUsed);

	if (sticker->sticker()) App::main()->incrementSticker(sticker);

	App::historyRegRandom(randomId, newId);
	App::main()->historyToDown(_history);

	App::main()->dialogsToUp();
	peerMessagesUpdated(_peer->id);

	if (!_attachMention.isHidden()) _attachMention.hideStart();
	if (!_attachType.isHidden()) _attachType.hideStart();
	if (!_emojiPan.isHidden()) _emojiPan.hideStart();

	_field.setFocus();
}

void HistoryWidget::setFieldText(const QString &text) {
	_synthedTextUpdate = true;
	_field.setPlainText(text);
	_synthedTextUpdate = false;

	_previewCancelled = false;
	_previewData = 0;
	if (_previewRequest) {
		MTP::cancel(_previewRequest);
		_previewRequest = 0;
	}
	_previewLinks.clear();
}

void HistoryWidget::onReplyToMessage() {
	HistoryItem *to = App::contextItem();
	if (!to || to->id <= 0) return;

	App::main()->cancelForwarding();

	_replyTo = to;
	_replyToId = to->id;
	_replyToText.setText(st::msgFont, _replyTo->inDialogsText(), _textDlgOptions);

	updateBotKeyboard();

	if (!_field.isHidden()) _replyForwardPreviewCancel.show();
	updateReplyToName();
	resizeEvent(0);
	updateField();

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	_field.setFocus();
}

bool HistoryWidget::lastForceReplyReplied(MsgId replyTo) const {
	return _keyboard.forceReply() && _keyboard.forMsgId() == _history->lastKeyboardId && _keyboard.forMsgId() == (replyTo < 0 ? replyToId() : replyTo);
}

void HistoryWidget::cancelReply(bool lastKeyboardUsed) {
	if (_replyToId) {
		_replyTo = 0;
		_replyToId = 0;
		mouseMoveEvent(0);
		if (!App::main()->hasForwardingItems() && (!_previewData || _previewData->pendingTill < 0) && !_kbReplyTo) {
			_replyForwardPreviewCancel.hide();
		}

		updateBotKeyboard();

		resizeEvent(0);
		update();

		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	}
	if (_keyboard.singleUse() && _keyboard.forceReply() && lastKeyboardUsed) {
		if (_kbReplyTo) {
			onKbToggle(false);
		}
	}
}

void HistoryWidget::cancelForwarding() {
	updateControlsVisibility();
	resizeEvent(0);
	update();
}

void HistoryWidget::onReplyForwardPreviewCancel() {
	_replyForwardPressed = false;
	if (_previewData && _previewData->pendingTill >= 0) {
		_previewCancelled = true;
		previewCancel();

		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	} else if (App::main()->hasForwardingItems()) {
		App::main()->cancelForwarding();
	} else if (_replyToId) {
		cancelReply();
	} else if (_kbReplyTo) {
		onKbToggle();
	}
}

void HistoryWidget::onStickerPackInfo() {
	if (HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::contextItem())) {
		if (HistorySticker *sticker = dynamic_cast<HistorySticker*>(item->getMedia())) {
			if (sticker->document() && sticker->document()->sticker() && sticker->document()->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
				App::main()->stickersBox(sticker->document()->sticker()->set);
			}
		}
	}
}

void HistoryWidget::previewCancel() {
	MTP::cancel(_previewRequest);
	_previewRequest = 0;
	_previewData = 0;
	_previewLinks.clear();
	updatePreview();
	if (!_replyToId && !App::main()->hasForwardingItems() && !_kbReplyTo) _replyForwardPreviewCancel.hide();
}

void HistoryWidget::onPreviewParse() {
	if (_previewCancelled) return;
	_field.parseLinks();
}

void HistoryWidget::onPreviewCheck() {
	if (_previewCancelled) return;
	QStringList linksList = _field.linksList();
	QString newLinks = linksList.join(' ');
	if (newLinks != _previewLinks) {
		MTP::cancel(_previewRequest);
		_previewLinks = newLinks;
		if (_previewLinks.isEmpty()) {
			if (_previewData && _previewData->pendingTill >= 0) previewCancel();
		} else {
			PreviewCache::const_iterator i = _previewCache.constFind(_previewLinks);
			if (i == _previewCache.cend()) {
				_previewRequest = MTP::send(MTPmessages_GetWebPagePreview(MTP_string(_previewLinks)), rpcDone(&HistoryWidget::gotPreview, _previewLinks));
			} else if (i.value()) {
				_previewData = App::webPage(i.value());
				updatePreview();
			} else {
				if (_previewData && _previewData->pendingTill >= 0) previewCancel();
			}
		}
	}
}

void HistoryWidget::onPreviewTimeout() {
	if (_previewData && _previewData->pendingTill > 0 && !_previewLinks.isEmpty()) {
		_previewRequest = MTP::send(MTPmessages_GetWebPagePreview(MTP_string(_previewLinks)), rpcDone(&HistoryWidget::gotPreview, _previewLinks));
	}
}

void HistoryWidget::gotPreview(QString links, const MTPMessageMedia &result, mtpRequestId req) {
	if (req == _previewRequest) {
		_previewRequest = 0;
	}
	if (result.type() == mtpc_messageMediaWebPage) {
		WebPageData *data = App::feedWebPage(result.c_messageMediaWebPage().vwebpage);
		_previewCache.insert(links, data->id);
		if (data->pendingTill > 0 && data->pendingTill <= unixtime()) {
			data->pendingTill = -1;
		}
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = (data->id && data->pendingTill >= 0) ? data : 0;
			updatePreview();
		}
		if (App::main()) App::main()->webPagesUpdate();
	} else if (result.type() == mtpc_messageMediaEmpty) {
		_previewCache.insert(links, 0);
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = 0;
			updatePreview();
		}
	}
}

void HistoryWidget::updatePreview() {
	_previewTimer.stop();
	if (_previewData && _previewData->pendingTill >= 0) {
		_replyForwardPreviewCancel.show();
		if (_previewData->pendingTill) {
			_previewTitle.setText(st::msgServiceNameFont, lang(lng_preview_loading), _textNameOptions);
			_previewDescription.setText(st::msgFont, _previewLinks.splitRef(' ').at(0).toString(), _textDlgOptions);

			int32 t = (_previewData->pendingTill - unixtime()) * 1000;
			if (t <= 0) t = 1;
			_previewTimer.start(t);
		} else {
			QString title, desc;
			if (_previewData->siteName.isEmpty()) {
				if (_previewData->title.isEmpty()) {
					if (_previewData->description.isEmpty()) {
						title = _previewData->author;
						desc = ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url);
					} else {
						title = _previewData->description;
						desc = _previewData->author.isEmpty() ? ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url) : _previewData->author;
					}
				} else {
					title = _previewData->title;
					desc = _previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url) : _previewData->author) : _previewData->description;
				}
			} else {
				title = _previewData->siteName;
				desc = _previewData->title.isEmpty() ? (_previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url) : _previewData->author) : _previewData->description) : _previewData->title;
			}
			if (title.isEmpty()) {
				if (_previewData->photo) {
					title = lang(lng_attach_photo);
				} else if (_previewData->doc) {
					title = lang(lng_attach_file);
				}
			}
			_previewTitle.setText(st::msgServiceNameFont, title, _textNameOptions);
			_previewDescription.setText(st::msgFont, desc, _textDlgOptions);
		}
	} else if (!App::main()->hasForwardingItems() && !replyToId()) {
		_replyForwardPreviewCancel.hide();
	}
	resizeEvent(0);
	update();
}

void HistoryWidget::onCancel() {
	if (App::main()) App::main()->showDialogs();
	emit cancelled();
}

void HistoryWidget::onFullPeerUpdated(PeerData *data) {
	int32 newScrollTop = _scroll.scrollTop();
	if (_list && data == _peer) {
		checkMentionDropdown();
		updateReportSpamStatus();
		int32 lh = _list->height(), st = _scroll.scrollTop();
		_list->updateBotInfo();
		newScrollTop = st + _list->height() - lh;
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		resizeEvent(0);
		update();
	} else if (!_scroll.isHidden() && _unblock.isHidden() == isBlocked()) {
		updateControlsVisibility();
		resizeEvent(0);
	}
	if (newScrollTop != _scroll.scrollTop()) {
		if (_scroll.isVisible()) {
			_scroll.scrollToY(newScrollTop);
		} else {
			_history->lastScrollTop = newScrollTop;
		}
	}
}

void HistoryWidget::peerUpdated(PeerData *data) {
	if (data && data == _peer) {
		updateListSize();
		if (!_showAnim.animating()) updateControlsVisibility();
		if (data->chat && data->asChat()->count > 0 && data->asChat()->participants.isEmpty()) {
			App::api()->requestFullPeer(data);
		} else if (!data->chat && data->asUser()->blocked == UserBlockUnknown) {
			App::api()->requestFullPeer(data);
		} else if (!_scroll.isHidden() && _unblock.isHidden() == isBlocked()) {
			updateControlsVisibility();
			resizeEvent(0);
		}
		App::main()->updateOnlineDisplay();
	}
}

void HistoryWidget::onForwardSelected() {
	if (!_list) return;
	App::main()->forwardLayer(true);
}

void HistoryWidget::onDeleteSelected() {
	if (!_list) return;

	SelectedItemSet sel;
	_list->fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	App::main()->deleteLayer(sel.size());
}

void HistoryWidget::onDeleteSelectedSure() {
	if (!_list) return;

	SelectedItemSet sel;
	_list->fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	QVector<MTPint> ids;
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		if (i.value()->id > 0) {
			ids.push_back(MTP_int(i.value()->id));
		}
	}

	if (!ids.isEmpty()) {
		App::main()->deleteMessages(ids);
	}

	onClearSelected();
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		i.value()->destroy();
	}
	if (App::main() && App::main()->peer() == peer()) {
		App::main()->itemResized(0);
	}
	App::wnd()->hideLayer();
}

void HistoryWidget::onDeleteContextSure() {
	HistoryItem *item = App::contextItem();
	if (!item || item->itemType() != HistoryItem::MsgType) {
		return;
	}

	if (item->id > 0) {
		App::main()->deleteMessages(QVector<MTPint>(1, MTP_int(item->id)));
	}
	item->destroy();
	if (App::main() && App::main()->peer() == peer()) {
		App::main()->itemResized(0);
	}
	App::wnd()->hideLayer();
}

void HistoryWidget::onListEscapePressed() {
	if (_selCount && _list) {
		onClearSelected();
	} else {
		onCancel();
	}
}

void HistoryWidget::onClearSelected() {
	if (_list) _list->clearSelectedItems();
}

void HistoryWidget::onAnimActiveStep() {
	if (!_history || _activeAnimMsgId <= 0) return _animActiveTimer.stop();

	HistoryItem *item = App::histItemById(_activeAnimMsgId);
	if (!item || item->detached()) return _animActiveTimer.stop();

	if (getms() - _animActiveStart > st::activeFadeInDuration + st::activeFadeOutDuration) {
		stopAnimActive();
	} else {
		App::main()->msgUpdated(_peer->id, item);
	}
}

uint64 HistoryWidget::animActiveTime(MsgId id) const {
	return (id == _activeAnimMsgId && _animActiveTimer.isActive()) ? (getms() - _animActiveStart) : 0;
}

void HistoryWidget::stopAnimActive() {
	_animActiveTimer.stop();
	_activeAnimMsgId = 0;
}

void HistoryWidget::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	if (_list) _list->fillSelectedItems(sel, forDelete);
}

void HistoryWidget::updateTopBarSelection() {
	if (!_list) {
		App::main()->topBar()->showSelected(0);
		return;
	}

	int32 selectedForForward, selectedForDelete;
	_list->getSelectionState(selectedForForward, selectedForDelete);
	_selCount = selectedForDelete ? selectedForDelete : selectedForForward;
	App::main()->topBar()->showSelected(_selCount > 0 ? _selCount : 0);
	updateControlsVisibility();
	updateListSize();
	if (!App::wnd()->layerShown() && !App::passcoded()) {
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked()) {
			_list->setFocus();
		} else {
			_field.setFocus();
		}
	}
	App::main()->topBar()->update();
	update();
}

void HistoryWidget::updateReplyTo(bool force) {
	if (!_replyToId || _replyTo) return;
	_replyTo = App::histItemById(_replyToId);
	if (_replyTo) {
		_replyToText.setText(st::msgFont, _replyTo->inDialogsText(), _textDlgOptions);

		updateBotKeyboard();

		if (!_field.isHidden() || _recording) _replyForwardPreviewCancel.show();
		updateReplyToName();
		updateField();
	} else if (force) {
		cancelReply();
	}
}

void HistoryWidget::updateForwarding(bool force) {
	if (App::main()->hasForwardingItems()) {
		updateControlsVisibility();
	} else {
		resizeEvent(0);
		update();
	}
}

void HistoryWidget::updateReplyToName() {
	if (!_replyTo && (_replyToId || !_kbReplyTo)) return;
	_replyToName.setText(st::msgServiceNameFont, App::peerName((_replyTo ? _replyTo : _kbReplyTo)->from()), _textNameOptions);
	_replyToNameVersion = (_replyTo ? _replyTo : _kbReplyTo)->from()->nameVersion;
}

void HistoryWidget::updateField() {
	int32 fy = _scroll.y() + _scroll.height();
	update(0, fy, width(), height() - fy);
}

void HistoryWidget::drawField(Painter &p) {
	int32 backy = _field.y() - st::sendPadding, backh = _field.height() + 2 * st::sendPadding;
	Text *from = 0, *text = 0;
	bool serviceColor = false, hasForward = App::main()->hasForwardingItems();
	ImagePtr preview;
	HistoryItem *drawReplyTo = _replyToId ? _replyTo : _kbReplyTo;
	if (_replyToId || (!hasForward && _kbReplyTo)) {
		if (drawReplyTo && drawReplyTo->from()->nameVersion > _replyToNameVersion) {
			updateReplyToName();
		}
		backy -= st::replyHeight;
		backh += st::replyHeight;
	} else if (hasForward) {
		App::main()->fillForwardingInfo(from, text, serviceColor, preview);
		backy -= st::replyHeight;
		backh += st::replyHeight;
	} else if (_previewData && _previewData->pendingTill >= 0) {
		backy -= st::replyHeight;
		backh += st::replyHeight;
	}
	bool drawPreview = (_previewData && _previewData->pendingTill >= 0) && !_replyForwardPressed;
	p.fillRect(0, backy, width(), backh, st::taMsgField.bgColor->b);
	if (_replyToId || (!hasForward && _kbReplyTo)) {
		int32 replyLeft = st::replySkip;
		p.drawPixmap(QPoint(st::replyIconPos.x(), backy + st::replyIconPos.y()), App::sprite(), st::replyIcon);
		if (!drawPreview) {
			if (drawReplyTo) {
				if (drawReplyTo->getMedia() && drawReplyTo->getMedia()->hasReplyPreview()) {
					ImagePtr replyPreview = drawReplyTo->getMedia()->replyPreview();
					if (!replyPreview->isNull()) {
						QRect to(replyLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
						p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height()));
					}
					replyLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
				}
				p.setPen(st::replyColor->p);
				_replyToName.drawElided(p, replyLeft, backy + st::msgReplyPadding.top(), width() - replyLeft - _replyForwardPreviewCancel.width() - st::msgReplyPadding.right());
				p.setPen((((drawReplyTo->toHistoryMessage() && drawReplyTo->toHistoryMessage()->justMedia()) || drawReplyTo->serviceMsg()) ? st::msgInDateColor : st::msgColor)->p);
				_replyToText.drawElided(p, replyLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - replyLeft - _replyForwardPreviewCancel.width() - st::msgReplyPadding.right());
			} else {
				p.setFont(st::msgDateFont->f);
				p.setPen(st::msgInDateColor->p);
				p.drawText(replyLeft, backy + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->m.elidedText(lang(lng_profile_loading), Qt::ElideRight, width() - replyLeft - _replyForwardPreviewCancel.width() - st::msgReplyPadding.right()));
			}
		}
	} else if (from && text) {
		int32 forwardLeft = st::replySkip;
		p.drawPixmap(QPoint(st::replyIconPos.x(), backy + st::replyIconPos.y()), App::sprite(), st::forwardIcon);
		if (!drawPreview) {
			if (!preview->isNull()) {
				QRect to(forwardLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (preview->width() == preview->height()) {
					p.drawPixmap(to.x(), to.y(), preview->pix());
				} else {
					QRect from = (preview->width() > preview->height()) ? QRect((preview->width() - preview->height()) / 2, 0, preview->height(), preview->height()) : QRect(0, (preview->height() - preview->width()) / 2, preview->width(), preview->width());
					p.drawPixmap(to, preview->pix(), from);
				}
				forwardLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
			}
			p.setPen(st::replyColor->p);
			from->drawElided(p, forwardLeft, backy + st::msgReplyPadding.top(), width() - forwardLeft - _replyForwardPreviewCancel.width() - st::msgReplyPadding.right());
			p.setPen((serviceColor ? st::msgInDateColor : st::msgColor)->p);
			text->drawElided(p, forwardLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - forwardLeft - _replyForwardPreviewCancel.width() - st::msgReplyPadding.right());
		}
	}
	if (drawPreview) {
		int32 previewLeft = st::replySkip + st::webPageLeft;
		p.fillRect(st::replySkip, backy + st::msgReplyPadding.top(), st::webPageBar, st::msgReplyBarSize.height(), st::msgInReplyBarColor->b);
		if ((_previewData->photo && !_previewData->photo->thumb->isNull()) || (_previewData->doc && !_previewData->doc->thumb->isNull())) {
			ImagePtr replyPreview = _previewData->photo ? _previewData->photo->makeReplyPreview() : _previewData->doc->makeReplyPreview();
			if (!replyPreview->isNull()) {
				QRect to(previewLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (replyPreview->width() == replyPreview->height()) {
					p.drawPixmap(to.x(), to.y(), replyPreview->pix());
				} else {
					QRect from = (replyPreview->width() > replyPreview->height()) ? QRect((replyPreview->width() - replyPreview->height()) / 2, 0, replyPreview->height(), replyPreview->height()) : QRect(0, (replyPreview->height() - replyPreview->width()) / 2, replyPreview->width(), replyPreview->width());
					p.drawPixmap(to, replyPreview->pix(), from);
				}
			}
			previewLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::replyColor->p);
		_previewTitle.drawElided(p, previewLeft, backy + st::msgReplyPadding.top(), width() - previewLeft - _replyForwardPreviewCancel.width() - st::msgReplyPadding.right());
		p.setPen(st::msgColor->p);
		_previewDescription.drawElided(p, previewLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - previewLeft - _replyForwardPreviewCancel.width() - st::msgReplyPadding.right());
	}
}

void HistoryWidget::drawRecordButton(Painter &p) {
	if (a_recordDown.current() < 1) {
		p.setOpacity(st::btnAttachEmoji.opacity * (1 - a_recordOver.current()) + st::btnAttachEmoji.overOpacity * a_recordOver.current());
		p.drawSprite(_send.x() + (_send.width() - st::btnRecordAudio.pxWidth()) / 2, _send.y() + (_send.height() - st::btnRecordAudio.pxHeight()) / 2, st::btnRecordAudio);
	}
	if (a_recordDown.current() > 0) {
		p.setOpacity(a_recordDown.current());
		p.drawSprite(_send.x() + (_send.width() - st::btnRecordAudioActive.pxWidth()) / 2, _send.y() + (_send.height() - st::btnRecordAudioActive.pxHeight()) / 2, st::btnRecordAudioActive);
	}
	p.setOpacity(1);
}

void HistoryWidget::drawRecording(Painter &p) {
	p.setPen(Qt::NoPen);
	p.setBrush(st::recordSignalColor->b);
	p.setRenderHint(QPainter::HighQualityAntialiasing);
	float64 delta = qMin(float64(a_recordingLevel.current()) * 3 * M_PI / 0x7fff, 1.);
	int32 d = 2 * qRound(st::recordSignalMin + (delta * (st::recordSignalMax - st::recordSignalMin)));
	p.drawEllipse(_attachPhoto.x() + (_attachEmoji.width() - d) / 2, _attachPhoto.y() + (_attachPhoto.height() - d) / 2, d, d);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	QString duration = formatDurationText(_recordingSamples / AudioVoiceMsgFrequency);
	p.setFont(st::recordFont->f);

	p.setPen(st::black->p);
	p.drawText(_attachPhoto.x() + _attachEmoji.width(), _attachPhoto.y() + st::recordTextTop + st::recordFont->ascent, duration);

	int32 left = _attachPhoto.x() + _attachEmoji.width() + st::recordFont->m.width(duration) + ((_send.width() - st::btnRecordAudio.pxWidth()) / 2);
	int32 right = width() - _send.width();

	p.setPen(a_recordCancel.current());
	p.drawText(left + (right - left - _recordCancelWidth) / 2, _attachPhoto.y() + st::recordTextTop + st::recordFont->ascent, lang(lng_record_cancel));
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	if (_showAnim.animating()) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
		return;
	}

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

	if (_list) {
		if (!_field.isHidden() || _recording) {
			drawField(p);
			if (_send.isHidden()) {
				drawRecordButton(p);
				if (_recording) drawRecording(p);
			}
		}
		if (_scroll.isHidden()) {
			QPoint dogPos((width() - st::msgDogImg.pxWidth()) / 2, ((height() - _field.height() - 2 * st::sendPadding - st::msgDogImg.pxHeight()) * 4) / 9);
			p.drawPixmap(dogPos, *cChatDogImage());
		}
	} else {
		style::font font(st::msgServiceFont);
		int32 w = font->m.width(lang(lng_willbe_history)) + st::msgPadding.left() + st::msgPadding.right(), h = font->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + 2;
		QRect tr((width() - w) / 2, (height() - _field.height() - 2 * st::sendPadding - h) / 2, w, h);
		App::roundRect(p, tr, App::msgServiceBg(), ServiceCorners);

		p.setPen(st::msgServiceColor->p);
		p.setFont(font->f);
		p.drawText(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top() + 1 + font->ascent, lang(lng_willbe_history));
	}
}

QRect HistoryWidget::historyRect() const {
	return _scroll.geometry();
}

void HistoryWidget::destroyData() {
	showPeerHistory(0, 0);
}

QStringList HistoryWidget::getMediasFromMime(const QMimeData *d) {
	QString uriListFormat(qsl("text/uri-list"));
	QStringList photoExtensions(cPhotoExtensions()), files;
	if (!d->hasFormat(uriListFormat)) return QStringList();

	const QList<QUrl> &urls(d->urls());
	if (urls.isEmpty()) return QStringList();

	files.reserve(urls.size());
	for (QList<QUrl>::const_iterator i = urls.cbegin(), en = urls.cend(); i != en; ++i) {
		if (!i->isLocalFile()) return QStringList();

		QString file(i->toLocalFile());
		if (file.startsWith(qsl("/.file/id="))) file = psConvertFileUrl(file);

		QFileInfo info(file);
		uint64 s = info.size();
		if (s >= MaxUploadDocumentSize) {
			if (s >= MaxUploadPhotoSize) {
				continue;
			} else {
				bool foundGoodExtension = false;
				for (QStringList::const_iterator j = photoExtensions.cbegin(), end = photoExtensions.cend(); j != end; ++j) {
					if (file.right(j->size()).toLower() == (*j).toLower()) {
						foundGoodExtension = true;
					}
				}
				if (!foundGoodExtension) {
					continue;
				}
			}
		}
		files.push_back(file);
	}
	return files;
}

QPoint HistoryWidget::clampMousePosition(QPoint point) {
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

void HistoryWidget::onScrollTimer() {
	int32 d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll.scrollToY(_scroll.scrollTop() + d);
}

void HistoryWidget::checkSelectingScroll(QPoint point) {
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

void HistoryWidget::noSelectingScroll() {
	_scrollTimer.stop();
}

bool HistoryWidget::touchScroll(const QPoint &delta) {
	int32 scTop = _scroll.scrollTop(), scMax = _scroll.scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	_scroll.scrollToY(scNew);
	return true;
}

HistoryWidget::~HistoryWidget() {
	delete _list;
}
