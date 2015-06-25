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
    , _cursor(style::cur_default)
    , _dragAction(NoDrag)
    , _dragSelType(TextSelectLetters)
    , _dragItem(0)
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

	if (botInfo && !botInfo->inited) App::api()->requestFullPeer(hist->peer);

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

	if (botInfo && !botInfo->text.isEmpty() && botDescHeight > 0) {
		if (r.top() < botDescRect.y() + botDescRect.height() && r.bottom() > botDescRect.y()) {
			textstyleSet(&st::inTextStyle);
			App::roundRect(p, botDescRect, st::msgInBg, MessageInCorners, &st::msgInShadow);

			p.setFont(st::msgNameFont->f);
			p.setPen(st::black->p);
			p.drawText(botDescRect.left() + st::msgPadding.left(), botDescRect.top() + st::msgPadding.top() + st::msgNameFont->ascent, lang(lng_bot_description));

			botInfo->text.draw(p, botDescRect.left() + st::msgPadding.left(), botDescRect.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip, botDescWidth);

			textstyleRestore();
		}
	} else if (hist->isEmpty()) {
		QPoint dogPos((width() - st::msgDogImg.pxWidth()) / 2, ((height() - st::msgDogImg.pxHeight()) * 4) / 9);
		p.drawPixmap(dogPos, *cChatDogImage());
	}
	if (!hist->isEmpty()) {
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
	bool textLink = textlnkDown() && !textlnkDown()->encoded().isEmpty();
	if (textLink) {
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
	if (_dragAction == PrepareSelect && !needClick && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullItemSel) {
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
	} else if (_dragAction == PrepareDrag && !needClick && !_dragWasInactive && button != Qt::RightButton) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i != _selected.cend() && i.value() == FullItemSel) {
			_selected.erase(i);
			updateMsg(_dragItem);
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
					if (doc && doc->sticker && doc->sticker->set.type() != mtpc_inputStickerSetEmpty) {
						if (!_menu) _menu = new ContextMenu(this);
						_menu->addAction(lang(doc->sticker->setInstalled() ? lng_context_pack_info : lng_context_pack_add), historyWidget, SLOT(onStickerPackInfo()));
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

int32 HistoryList::recountHeight(bool dontRecountText) {
	int32 st = hist->lastScrollTop;
	hist->geomResize(scrollArea->width(), &st, dontRecountText);
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
	return st;
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

void HistoryList::updateSize() {
	int32 ph = scrollArea->height(), minadd = 0;
	ySkip = ph - (hist->height + st::historyPadding);
	if (botInfo && !botInfo->text.isEmpty()) {
		minadd = st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + botDescHeight;
	}
	if (ySkip < minadd) ySkip = minadd;

	if (botDescHeight > 0) {
		int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + botDescHeight + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descAtX = (scrollArea->width() - botDescWidth) / 2 - st::msgPadding.left();
		int32 descAtY = qMin(ySkip - descH, (scrollArea->height() - descH) / 2) + st::msgMargin.top();

		botDescRect = QRect(descAtX, descAtY, botDescWidth + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
	}

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
	linkTipTimer.start(1000);

	Qt::CursorShape cur = style::cur_default;
	bool inText = false, lnkChanged = false, lnkInDesc = false;

	TextLinkPtr lnk;
	if (point.y() < ySkip) {
		if (botInfo && !botInfo->text.isEmpty() && botDescHeight > 0) {
			botInfo->text.getState(lnk, inText, point.x() - botDescRect.left() - st::msgPadding.left(), point.y() - botDescRect.top() - st::msgPadding.top() - st::botDescSkip - st::msgNameFont->height, botDescWidth);
			lnkInDesc = true;
		}
	} else if (item) {
		item->getState(lnk, inText, m.x(), m.y());
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
		QToolTip::showText(_dragPos, QString(), App::wnd());
		App::hoveredLinkItem((lnk && !lnkInDesc) ? item : 0);
		if (textlnkOver()) {
			if (App::hoveredLinkItem()) {
				updateMsg(App::hoveredLinkItem());
			} else {
				update(botDescRect);
			}
		}
	}

	if (_dragAction == NoDrag) {
		if (lnk) {
			cur = style::cur_pointer;
		} else if (inText && (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel)) {
			cur = style::cur_text;
		}
	} else if (item) {		
		if (item != _dragItem || (m - _dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
			if (_dragAction == PrepareDrag) {
				_dragAction = Dragging;

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
						urls.push_back(QUrl::fromEncoded(sel.toUtf8()));
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
						QStringList ids;
						ids.reserve(_selected.size());
						for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
							ids.push_back(QString::number(i.key()->id, 16));
						}
						mimeData->setData(qsl("application/x-td-forward-selected"), "1");
					}
					drag->setMimeData(mimeData);
					drag->exec();
					return;
				}
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
	if (lnk && !lnk->fullDisplayed()) {
		QToolTip::showText(_dragPos, lnk->readable(), App::wnd());
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
			QToolTip::showText(_lastMousePos, _btns.at(row).at(col).cmd);
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
		QToolTip::showText(_lastMousePos, QString(), App::wnd());
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
			parent()->onForward(offered->id, _forwardSelected);
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
		parent()->onForward(to, _forwardSelected);
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
, _previewData(0)
, _previewRequest(0)
, _previewCancelled(false)
, _replyForwardPressed(false)
, _replyReturn(0)
, _stickersUpdateRequest(0)
, _loadingMessages(false)
, histRequestsCount(0)
, histPeer(0)
, _activeHist(0)
, histPreloading(0)
, _loadingAroundId(-1)
, _loadingAroundRequest(0)
, _scroll(this, st::historyScroll, false)
, _list(0)
, hist(0)
, _histInited(false), _histNeedUpdate(false)
, _toHistoryEnd(this, st::historyToEnd)
, _attachMention(this)
, _send(this, lang(lng_send_button), st::btnSend)
, _botStart(this, lang(lng_bot_start), st::btnSend)
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
, imageLoader(this)
, _synthedTextUpdate(false)
, serviceImageCacheSize(0)
, confirmImageId(0)
, confirmWithText(false)
, titlePeerTextWidth(0)
, hiderOffered(false)
, _showAnim(animFunc(this, &HistoryWidget::showStep))
, _scrollDelta(0)
, _typingRequest(0)
, _saveDraftStart(0)
, _saveDraftText(false) {
	_scroll.setFocusPolicy(Qt::NoFocus);

	setAcceptDrops(true);

	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(&_toHistoryEnd, SIGNAL(clicked()), this, SLOT(onHistoryToEnd()));
	connect(&_replyForwardPreviewCancel, SIGNAL(clicked()), this, SLOT(onReplyForwardPreviewCancel()));
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_botStart, SIGNAL(clicked()), this, SLOT(onBotStart()));
	connect(&_attachDocument, SIGNAL(clicked()), this, SLOT(onDocumentSelect()));
	connect(&_attachPhoto, SIGNAL(clicked()), this, SLOT(onPhotoSelect()));
	connect(&_field, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
	connect(&_field, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(&_field, SIGNAL(resized()), this, SLOT(onFieldResize()));
	connect(&_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(&imageLoader, SIGNAL(imageReady()), this, SLOT(onPhotoReady()));
	connect(&imageLoader, SIGNAL(imageFailed(quint64)), this, SLOT(onPhotoFailed(quint64)));
	connect(&_field, SIGNAL(changed()), this, SLOT(onTextChange()));
	connect(&_field, SIGNAL(spacedReturnedPasted()), this, SLOT(onPreviewParse()));
	connect(&_field, SIGNAL(linksChanged()), this, SLOT(onPreviewCheck()));
	connect(App::wnd()->windowHandle(), SIGNAL(visibleChanged(bool)), this, SLOT(onVisibleChanged()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(&_emojiPan, SIGNAL(emojiSelected(EmojiPtr)), &_field, SLOT(onEmojiInsert(EmojiPtr)));
	connect(&_emojiPan, SIGNAL(stickerSelected(DocumentData*)), this, SLOT(onStickerSend(DocumentData*)));
	connect(&_emojiPan, SIGNAL(updateStickers()), this, SLOT(updateStickers()));
	connect(&_typingStopTimer, SIGNAL(timeout()), this, SLOT(cancelTyping()));
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreviewTimeout()));
	if (audioCapture()) {
		connect(audioCapture(), SIGNAL(onError()), this, SLOT(onRecordError()));
		connect(audioCapture(), SIGNAL(onUpdate(qint16,qint32)), this, SLOT(onRecordUpdate(qint16,qint32)));
		connect(audioCapture(), SIGNAL(onDone(QByteArray,qint32)), this, SLOT(onRecordDone(QByteArray,qint32)));
	}

	_scrollTimer.setSingleShot(false);

	_typingStopTimer.setSingleShot(true);

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
	_botStart.hide();

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

	connect(&_attachDragDocument, SIGNAL(dropped(QDropEvent*)), this, SLOT(onDocumentDrop(QDropEvent*)));
	connect(&_attachDragPhoto, SIGNAL(dropped(QDropEvent*)), this, SLOT(onPhotoDrop(QDropEvent*)));
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
	updateTyping();

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

	if (!hist || _synthedTextUpdate) return;
	_saveDraftText = true;
	onDraftSave(true);
}

void HistoryWidget::onDraftSaveDelayed() {
	if (!hist || _synthedTextUpdate) return;
	if (!_field.textCursor().anchor() && !_field.textCursor().position() && !_field.verticalScrollBar()->value()) {
		if (!Local::hasDraftPositions(hist->peer->id)) return;
	}
	onDraftSave(true);
}

void HistoryWidget::onDraftSave(bool delayed) {
	if (!hist) return;
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
	bool save = hist && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.stop();
	if (_saveDraftText) {
		if (save) Local::writeDraft(hist->peer->id, Local::MessageDraft(replyTo ? (*replyTo) : _replyToId, text ? (*text) : _field.getLastText(), previewCancelled ? (*previewCancelled) : _previewCancelled));
		_saveDraftText = false;
	}
	if (save) Local::writeDraftPositions(hist->peer->id, cursor ? (*cursor) : MessageCursor(_field));
}

void HistoryWidget::cancelTyping() {
	if (_typingRequest) {
		MTP::cancel(_typingRequest);
		_typingRequest = 0;
	}
}

void HistoryWidget::updateTyping(bool typing) {
	uint64 ms = getms(true) + 10000;
	if (_synthedTextUpdate || !hist || (typing && (hist->myTyping + 5000 > ms)) || (!typing && (hist->myTyping + 5000 <= ms))) return;

	hist->myTyping = typing ? ms : 0;
	cancelTyping();
	if (typing) {
		_typingRequest = MTP::send(MTPmessages_SetTyping(histPeer->input, typing ? MTP_sendMessageTypingAction() : MTP_sendMessageCancelAction()), rpcDone(&HistoryWidget::typingDone));
		_typingStopTimer.start(5000);
	}
}

void HistoryWidget::updateRecentStickers() {
	_emojiPan.refreshStickers();
}

void HistoryWidget::stickersInstalled(uint64 setId) {
	_emojiPan.stickersInstalled(setId);
}

void HistoryWidget::typingDone(const MTPBool &result, mtpRequestId req) {
	if (_typingRequest == req) {
		_typingRequest = 0;
	}
}

void HistoryWidget::activate() {
	if (hist) {
		if (!_histInited) checkUnreadLoaded();
		if (_histNeedUpdate) updateListSize();
	}
	if (App::main()->selectingPeer()) {
		if (hiderOffered) {
			App::main()->focusPeerSelect();
			return;
		} else {
			App::main()->dialogsActivate();
			return;
		}
	}
	if (_list) {
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart()) {
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
	App::wnd()->activateWindow();
	int32 duration = samples / AudioVoiceMsgFrequency;
	imageLoader.append(result, duration, histPeer->id, replyToId(), ToPrepareAudio);
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
		stopRecording(samples > 0 && _inField);
	}
	updateField();
}

void HistoryWidget::updateStickers() {
	if (cLastStickersUpdate() && getms(true) < cLastStickersUpdate() + StickersUpdateTimeout) return;
	if (_stickersUpdateRequest) return;

	_stickersUpdateRequest = MTP::send(MTPmessages_GetAllStickers(MTP_string(cStickersHash())), rpcDone(&HistoryWidget::stickersGot), rpcFail(&HistoryWidget::stickersFailed));
}

void HistoryWidget::stickersGot(const MTPmessages_AllStickers &stickers) {
	cSetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_allStickers) return;
	const MTPDmessages_allStickers &d(stickers.c_messages_allStickers());

	EmojiStickersMap map;
		
	const QVector<MTPDocument> &d_docs(d.vdocuments.c_vector().v);
	const QVector<MTPStickerSet> &d_sets(d.vsets.c_vector().v);

	QByteArray wasHash = cStickersHash();
	cSetStickersHash(qba(d.vhash));

	StickerSetsOrder &setsOrder(cRefStickerSetsOrder());
	setsOrder.clear();

	StickerSets &sets(cRefStickerSets());
	StickerSets::iterator def = sets.find(DefaultStickerSetId);
	if (def == sets.cend()) {
		def = sets.insert(DefaultStickerSetId, StickerSet(DefaultStickerSetId, 0, lang(lng_stickers_default_set), QString()));
	}
	for (int32 i = 0, l = d_sets.size(); i != l; ++i) {
		if (d_sets.at(i).type() == mtpc_stickerSet) {
			const MTPDstickerSet &set(d_sets.at(i).c_stickerSet());
			StickerSets::iterator i = sets.find(set.vid.v);
			setsOrder.push_back(set.vid.v);
			if (i == sets.cend()) {
				i = sets.insert(set.vid.v, StickerSet(set.vid.v, set.vaccess_hash.v, qs(set.vtitle), qs(set.vshort_name)));
			} else {
				i->access = set.vaccess_hash.v;
				i->title = qs(set.vtitle);
				i->shortName = qs(set.vshort_name);
			}
		}
	}

	StickerSets::iterator custom = sets.find(CustomStickerSetId);

	bool added = false, removed = false;
	QSet<DocumentData*> found;
	QMap<uint64, int32> wasCount;
	for (int32 i = 0, l = d_docs.size(); i != l; ++i) {
		DocumentData *doc = App::feedDocument(d_docs.at(i));
		if (!doc || !doc->sticker) continue;

		switch (doc->sticker->set.type()) {
		case mtpc_inputStickerSetEmpty: { // default set - great minds
			if (!wasCount.contains(DefaultStickerSetId)) wasCount.insert(DefaultStickerSetId, def->stickers.size());
			if (def->stickers.indexOf(doc) < 0) {
				def->stickers.push_back(doc);
				added = true;
			} else {
				found.insert(doc);
			}
		} break;
		case mtpc_inputStickerSetID: {
			StickerSets::iterator it = sets.find(doc->sticker->set.c_inputStickerSetID().vid.v);
			if (it == sets.cend()) {
				LOG(("Sticker Set not found by ID: %1").arg(doc->sticker->set.c_inputStickerSetID().vid.v));
			} else {
				if (!wasCount.contains(it->id)) wasCount.insert(it->id, it->stickers.size());
				if (it->stickers.indexOf(doc) < 0) {
					it->stickers.push_back(doc);
					added = true;
				} else {
					found.insert(doc);
				}
			}
		} break;
		case mtpc_inputStickerSetShortName: {
			QString name = qs(doc->sticker->set.c_inputStickerSetShortName().vshort_name).toLower().trimmed();
			StickerSets::iterator it = sets.begin();
			for (; it != sets.cend(); ++it) {
				if (it->shortName.toLower().trimmed() == name) {
					break;
				}
			}
			if (it == sets.cend()) {
				LOG(("Sticker Set not found by name: %1").arg(name));
			} else {
				if (!wasCount.contains(it->id)) wasCount.insert(it->id, it->stickers.size());
				if (it->stickers.indexOf(doc) < 0) {
					it->stickers.push_back(doc);
					added = true;
				} else {
					found.insert(doc);
				}
			}
		} break;
		}
		if (custom != sets.cend()) {
			int32 index = custom->stickers.indexOf(doc);
			if (index >= 0) {
				custom->stickers.removeAt(index);
				removed = true;
			}
		}
	}
	if (custom != sets.cend() && custom->stickers.isEmpty()) {
		sets.erase(custom);
		custom = sets.end();
	}
	bool writeRecent = false;
	RecentStickerPack &recent(cGetRecentStickers());
	for (StickerSets::iterator it = sets.begin(); it != sets.cend();) {
		if (it->id == CustomStickerSetId || it->id == RecentStickerSetId) {
			++it;
			continue;
		}
		QMap<uint64, int32>::const_iterator was = wasCount.constFind(it->id);
		if (was == wasCount.cend()) { // no such stickers added
			for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
			setsOrder.removeOne(it->id);
			it = sets.erase(it);
			removed = true;
		} else {
			for (int32 j = 0, l = was.value(); j < l;) {
				if (found.contains(it->stickers.at(j))) {
					++j;
				} else {
					for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
						if (it->stickers.at(j) == i->first) {
							i = recent.erase(i);
							writeRecent = true;
						} else {
							++i;
						}
					}
					it->stickers.removeAt(j);
					--l;
					removed = true;
				}
			}
			if (it->stickers.isEmpty()) {
				setsOrder.removeOne(it->id);
				it = sets.erase(it);
			} else {
				++it;
			}
		}
	}
	if (added || removed || cStickersHash() != wasHash) {
		Local::writeStickers();
	}
	if (writeRecent) {
		Local::writeUserSettings();
	}
		
	const QVector<MTPStickerPack> &packs(d.vpacks.c_vector().v);
	for (int32 i = 0, l = packs.size(); i != l; ++i) {
		if (packs.at(i).type() == mtpc_stickerPack) {
			const MTPDstickerPack &p(packs.at(i).c_stickerPack());
			QString emoticon(qs(p.vemoticon));
			EmojiPtr e = 0;
			for (const QChar *ch = emoticon.constData(), *end = emoticon.constEnd(); ch != end; ++ch) {
				int len = 0;
				e = emojiFromText(ch, end, len);
				if (e) break;

				if (ch + 1 < end && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) ++ch;
			}
			if (e) {
				const QVector<MTPlong> docs(p.vdocuments.c_vector().v);
				if (!docs.isEmpty()) {
					for (int32 j = 0, s = docs.size(); j < s; ++j) {
						DocumentData *doc = App::document(docs.at(j).v);
						map.insert(doc, e);
					}
				}
			} else {
				LOG(("Sticker Error: Could not find emoji for string: %1").arg(emoticon));
			}
		}
	}

	cSetEmojiStickers(map);

	const DocumentItems &items(App::documentItems());
	for (EmojiStickersMap::const_iterator i = map.cbegin(), e = map.cend(); i != e; ++i) {
		DocumentItems::const_iterator j = items.constFind(i.key());
		if (j != items.cend()) {
			for (HistoryItemsMap::const_iterator k = j->cbegin(), end = j->cend(); k != end; ++k) {
				k.key()->updateStickerEmoji();
			}
		}
	}

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::stickersFailed(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	LOG(("App Fail: Failed to get stickers!"));

	cSetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::clearLoadingAround() {
	_loadingAroundId = -1;
	if (_loadingAroundRequest) {
		MTP::cancel(_loadingAroundRequest);
		_loadingAroundRequest = 0;
	}
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
	if (!histPeer || histPeer->id != peer) return;
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

void HistoryWidget::showPeer(const PeerId &peer, MsgId msgId, bool force, bool leaveActive) {
	if (App::main()->selectingPeer() && !force) {
		hiderOffered = true;
		App::main()->offerPeer(peer);
		return;
	}
	if (hist) {
		if (histPeer->id == peer) {
			if (msgId != hist->activeMsgId) {
				bool canShowNow = hist->canShowAround(msgId);
				if (!force && !canShowNow) {
					if (_loadingAroundId != msgId) {
						clearLoadingAround();
						_loadingAroundId = msgId;
						loadMessagesAround();
					}
					return;
				}
				hist->loadAround(msgId);
				if (!canShowNow) {
					histPreload.clear();
					histPreloadDown.clear();
					if (histPreloading) MTP::cancel(histPreloading);
					if (histPreloadingDown) MTP::cancel(histPreloadingDown);
					histPreloading = histPreloadingDown = 0;
				}
			}
			if (_replyReturn && _replyReturn->id == msgId) calcNextReplyReturn();

			if (hist->unreadBar) hist->unreadBar->destroy();
			checkUnreadLoaded();

			clearLoadingAround();
			emit peerShown(histPeer);
			return App::wnd()->setInnerFocus();
		}
		updateTyping(false);
	}
	stopGif();
	clearLoadingAround();
	clearReplyReturns();
	if (_list) {
		if (!histPreload.isEmpty()) {
			_list->messagesReceived(histPreload);
			updateBotKeyboard();
			histPreload.clear();
		}
		if (!histPreloadDown.isEmpty()) {
			_list->messagesReceivedDown(histPreloadDown);
			histPreloadDown.clear();
		}
	}
	if (hist) {
		hist->draft = _field.getLastText();
		hist->draftCursor.fillFrom(_field);
		hist->draftToId = _replyToId;
		hist->draftPreviewCancelled = _previewCancelled;

		writeDraft(&hist->draftToId, &hist->draft, &hist->draftCursor, &hist->draftPreviewCancelled);

		if (hist->readyForWork() && _scroll.scrollTop() + 1 <= _scroll.scrollTopMax()) {
			hist->lastWidth = _list->width();
		} else {
			hist->lastWidth = 0;
		}
		hist->lastScrollTop = _scroll.scrollTop();
		if (hist->unreadBar) hist->unreadBar->destroy();
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

	if (_activeHist && _activeHist->peer->id != peer && (!leaveActive || _activeHist != hist)) {
		if (!_activeHist->peer->chat && _activeHist->peer->asUser()->botInfo) {
			_activeHist->peer->asUser()->botInfo->startToken = QString();
		}
	}
	if (leaveActive && hist) {
		_activeHist = hist;
	} else {
		if (!leaveActive) {
			_activeHist = 0;
		}
		if (hist) {
			App::main()->dlgUpdated(hist);
			if (!hist->peer->chat && hist->peer->asUser()->botInfo) {
				hist->peer->asUser()->botInfo->startToken = QString();
			}
		}
	}
	histPeer = peer ? App::peer(peer) : 0;
	titlePeerText = QString();
	titlePeerTextWidth = 0;
	histRequestsCount = 0;
	histPreload.clear();
	histPreloadDown.clear();
	if (histPreloading) MTP::cancel(histPreloading);
	if (histPreloadingDown) MTP::cancel(histPreloadingDown);
	histPreloading = histPreloadingDown = 0;
	hist = 0;
	_histInited = _histNeedUpdate = false;
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

	if (peer) {
		App::forgetMedia();
		serviceImageCacheSize = imageCacheSize();
		MTP::clearLoaderPriorities();
		histInputPeer = histPeer->input;
		if (histInputPeer.type() == mtpc_inputPeerEmpty) { // maybe should load user
		}
		Histories::iterator i = App::histories().find(peer);
		if (i == App::histories().end()) {
			hist = new History(peer);
			i = App::histories().insert(peer, hist);
		} else {
			hist = i.value();
		}
		if (hist->readyForWork()) {
			_scroll.show();
		}
		if (hist) {
			App::main()->dlgUpdated(hist);
		}
		_list = new HistoryList(this, &_scroll, hist);
		hist->loadAround(msgId);

		_list->hide();
		_scroll.setWidget(_list);
		_list->show();

		updateBotKeyboard();
		checkUnreadLoaded();

		App::main()->peerUpdated(histPeer);
		
		if (hist->draftToId > 0 || !hist->draft.isEmpty()) {
			setFieldText(hist->draft);
			_field.setFocus();
			hist->draftCursor.applyTo(_field, &_synthedTextUpdate);
			_replyToId = App::main()->hasForwardingItems() ? 0 : hist->draftToId;
			if (hist->draftPreviewCancelled) {
				_previewCancelled = true;
			}
		} else {
			Local::MessageDraft draft = Local::readDraft(hist->peer->id);
			setFieldText(draft.text);
			_field.setFocus();
			if (!draft.text.isEmpty()) {
				MessageCursor cur = Local::readDraftPositions(hist->peer->id);
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
		updateBotKeyboard();
		updateControlsVisibility();
	}

	emit peerShown(histPeer);
	App::main()->topBar()->update();
	update();
}

void HistoryWidget::checkUnreadLoaded(bool checkOnlyShow) {
	if (!hist) return;
	if (hist->readyForWork()) {
		if (checkOnlyShow && !_scroll.isHidden()) return;
		if (!_showAnim.animating()) {
			if (_scroll.isHidden()) {
				_scroll.show();
				if (!_field.isHidden()) update();
			}
		}
		updateBotKeyboard();
	} else if (checkOnlyShow) {
		return;
	}
	updateListSize(0, true);
	if (!_showAnim.animating()) updateControlsVisibility();
	if (hist->readyForWork()) {
		if (!_scroll.isHidden() && !_list->isHidden()) {
			onListScroll();
		}
	} else {
		loadMessages();
	}
}

void HistoryWidget::updateControlsVisibility() {
	if (!hist || _showAnim.animating()) {
		_scroll.hide();
		_kbScroll.hide();
		_send.hide();
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
	if (hist->readyForWork()) {
		if (!histPeer->chat || !histPeer->asChat()->forbidden) {
			checkMentionDropdown();
			if (isBotStart()) {
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

		if (hist->unreadCount && App::wnd()->historyIsActive()) {
			historyWasRead();
		}
	} else {
		loadMessages();
		if (!hist->readyForWork()) {
			_scroll.hide();
			_kbScroll.hide();
			_attachMention.hide();
			_send.hide();
			_botStart.hide();
			_attachDocument.hide();
			_attachPhoto.hide();
			_attachEmoji.hide();
			_kbShow.hide();
			_kbHide.hide();
			_cmdStart.hide();
			_attachType.hide();
			_emojiPan.hide();
			_replyForwardPreviewCancel.hide();
			if (!_field.isHidden()) {
				_field.hide();
				update();
			}
		}
	}
}

void HistoryWidget::newUnreadMsg(History *history, HistoryItem *item) {
	if (App::wnd()->historyIsActive()) {
		if (hist == history && hist->readyForWork()) {
			historyWasRead();
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
			}
		} else {
			if (hist != history) {
				App::wnd()->notifySchedule(history, item);
			}
			history->setUnreadCount(history->unreadCount + 1);
		}
	} else {
		if (hist == history && hist->readyForWork()) {
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
			}
		}
		App::wnd()->notifySchedule(history, item);
		history->setUnreadCount(history->unreadCount + 1);
		history->lastWidth = 0;
	}
}

void HistoryWidget::historyToDown(History *history) {
	history->lastScrollTop = History::ScrollMax;
	if (history == hist) {
		_scroll.scrollToY(_scroll.scrollTopMax());
	}
}

void HistoryWidget::historyWasRead(bool force) {
    App::main()->readServerHistory(hist, force);
}

void HistoryWidget::historyCleared(History *history) {
	if (history == hist) {
		_list->dragActionCancel();
	}
}

bool HistoryWidget::messagesFailed(const RPCError &error, mtpRequestId requestId) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (histPreloading == requestId) {
		histPreloading = 0;
	} else if (histPreloadingDown == requestId) {
		histPreloadingDown = 0;
	} else if (_loadingAroundRequest == requestId) {
		_loadingAroundRequest = 0;
	}
	return true;
}

void HistoryWidget::messagesReceived(const MTPmessages_Messages &messages, mtpRequestId requestId) {
	if (!hist) {
		histPreloading = histPreloadingDown = _loadingAroundRequest = 0;
		histPreload.clear();
		histPreloadDown.clear();
		return;
	}

	PeerId peer = 0;
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
	if (histList && !histList->isEmpty()) {
		const MTPmessage &msg(histList->front());
		PeerId from_id(0), to_id(0);
		switch (msg.type()) {
		case mtpc_message:
			from_id = App::peerFromUser(msg.c_message().vfrom_id);
			to_id = App::peerFromMTP(msg.c_message().vto_id);
		break;
		case mtpc_messageService:
			from_id = App::peerFromUser(msg.c_messageService().vfrom_id);
			to_id = App::peerFromMTP(msg.c_messageService().vto_id);
		break;
		}
		peer = (to_id == App::peerFromUser(MTP::authedId())) ? from_id : to_id;
	}

	bool down = false;
	if (histPreloading == requestId) {
		histPreloading = 0;
	} else if (histPreloadingDown == requestId) {
		histPreloadingDown = 0;
		down = true;
	} else {
		if (_loadingAroundRequest == requestId) {
			_loadingAroundRequest = 0;
			hist->loadAround(_loadingAroundId);
			if (hist->isEmpty()) {
				histPreload.clear();
				histPreloadDown.clear();
				if (histPreloading) MTP::cancel(histPreloading);
				if (histPreloadingDown) MTP::cancel(histPreloadingDown);
				histPreloading = histPreloadingDown = 0;
				addMessagesToFront(*histList);
			}
			showPeer(hist->peer->id, _loadingAroundId, true);
		}
		return;
	}

	if (peer && peer != histPeer->id) return;

	if (histList) {
		if (!hist->minMsgId() || histList->isEmpty()) {
			if (down) {
				addMessagesToBack(*histList);
				histPreloadDown.clear();
			} else {
				addMessagesToFront(*histList);
				histPreload.clear();
			}
		} else {
			if (down) {
				histPreloadDown = *histList;
			} else {
				histPreload = *histList;
			}
		}
	} else {
		if (down) {
			addMessagesToBack(QVector<MTPMessage>());
		} else {
			addMessagesToFront(QVector<MTPMessage>());
		}
		if (!hist->readyForWork()) {
			if (hist->activeMsgId) {
				hist->activeMsgId = 0;
			}
			if (!hist->readyForWork()) {
				hist->setUnreadCount(hist->msgCount);
			}
		}
		checkUnreadLoaded(true);
		return;
	}

	if (down && hist->loadedAtBottom() && histPreloadDown.size()) {
		addMessagesToBack(histPreloadDown);
		histPreloadDown.clear();
		loadMessagesDown();
	} else if (!down && hist->loadedAtTop() && histPreload.size()) {
		addMessagesToFront(histPreload);
		histPreload.clear();
		loadMessages();
	} else if ((down && histPreloadDown.size()) || (!down && histPreload.size())) {
		onListScroll();
	} else if (down) {
		loadMessagesDown();
	} else {
		loadMessages();
	}
}

void HistoryWidget::windowShown() {
	if (hist) {
		if (!_histInited) checkUnreadLoaded();
		if (_histNeedUpdate) updateListSize();
	}
	resizeEvent(0);
}

bool HistoryWidget::isActive() const {
	return !hist || hist->loadedAtBottom();
}

void HistoryWidget::loadMessages() {
	if (!hist || _loadingMessages) return;
	if (hist->loadedAtTop()) {
		if (!hist->readyForWork()) {
			if (hist->activeMsgId) {
				hist->activeMsgId = 0;
			}
			if (!hist->readyForWork()) {
				hist->setUnreadCount(hist->msgCount);
			}
		}
		checkUnreadLoaded(true);
		return;
	}

	_loadingMessages = true;
	if (histPreload.size()) {
		bool loaded = hist->readyForWork();
		addMessagesToFront(histPreload);
		histPreload.clear();
		checkUnreadLoaded(true);
		if (!loaded && hist->readyForWork()) {
			_loadingMessages = false;
			return;
		}
	}
	if (!histPreloading && (!hist->readyForWork() || _scroll.scrollTop() < PreloadHeightsCount * _scroll.height())) {
		MsgId min = hist->minMsgId();
		int32 offset = 0, loadCount = min ? MessagesPerPage : MessagesFirstLoad;
		if (!min && hist->activeMsgId) {
			min = hist->activeMsgId;
			offset = -loadCount / 2;
		}
		histPreloading = MTP::send(MTPmessages_GetHistory(histInputPeer, MTP_int(offset), MTP_int(min), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
		++histRequestsCount;
		if (!hist->readyForWork()) update();
	} else {
		checkUnreadLoaded(true);
	}
	_loadingMessages = false;
}

void HistoryWidget::loadMessagesDown() {
	if (!hist) return;
	if (hist->loadedAtBottom()) {
		return;
	}

	int32 dh = 0;
	if (histPreloadDown.size()) {
		bool loaded = hist->readyForWork();
		addMessagesToBack(histPreloadDown);
		histPreloadDown.clear();
		checkUnreadLoaded(true);
		if (!loaded && hist->readyForWork()) {
			return;
		}
	}
	if (!histPreloadingDown && hist->readyForWork() && (_scroll.scrollTop() + PreloadHeightsCount * _scroll.height() > _scroll.scrollTopMax())) {
		MsgId max = hist->maxMsgId();
		if (max) {
			int32 loadCount = MessagesPerPage, offset = -loadCount;
			histPreloadingDown = MTP::send(MTPmessages_GetHistory(histInputPeer, MTP_int(offset), MTP_int(max + 1), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
			++histRequestsCount;
			if (!hist->readyForWork()) update();
		}
	} else {
		checkUnreadLoaded(true);
	}
}

void HistoryWidget::loadMessagesAround() {
	if (!hist || _loadingAroundRequest || _loadingAroundId < 0) return;

	int32 offset = 0, loadCount = MessagesPerPage;
	if (_loadingAroundId) {
		offset = -loadCount / 2;
	}
	_loadingAroundRequest = MTP::send(MTPmessages_GetHistory(histInputPeer, MTP_int(offset), MTP_int(_loadingAroundId), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::onListScroll() {
	App::checkImageCacheSize();

	if (histPreloading || !hist || ((_list->isHidden() || _scroll.isHidden() || _showAnim.animating() || !App::wnd()->windowHandle()->isVisible()) && hist->readyForWork())) {
		checkUnreadLoaded(true);
		return;
	}

	updateToEndVisibility();
	
	int st = _scroll.scrollTop(), stm = _scroll.scrollTopMax(), sh = _scroll.height();
	if (hist->readyForWork() && (st + PreloadHeightsCount * sh > stm)) {
		loadMessagesDown();
	}

	if (!hist->readyForWork() || st < PreloadHeightsCount * sh) {
		loadMessages();
	} else {
		checkUnreadLoaded(true);
	}

	while (_replyReturn) {
		bool below = (_replyReturn->detached() && !hist->isEmpty() && _replyReturn->id < hist->back()->back()->id);
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
		showPeer(histPeer->id, _replyReturn->id);
	} else if (hist) {
		showPeer(histPeer->id, 0);
	}
}

void HistoryWidget::onSend(bool ctrlShiftEnter, MsgId replyTo) {
	if (!hist) return;

	bool lastKeyboardUsed = lastForceReplyReplied(replyTo);
	QString text = prepareMessage(_field.getLastText());
	if (!text.isEmpty()) {
		App::main()->readServerHistory(hist, false);
		hist->loadAround(0);

		WebPageId webPageId = _previewCancelled ? 0xFFFFFFFFFFFFFFFFULL : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);
		App::main()->sendPreparedText(hist, text, replyTo, webPageId);

		setFieldText(QString());
		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();

		if (!_attachMention.isHidden()) _attachMention.hideStart();
		if (!_attachType.isHidden()) _attachType.hideStart();
		if (!_emojiPan.isHidden()) _emojiPan.hideStart();

	} else if (App::main()->hasForwardingItems()) {
		App::main()->readServerHistory(hist, false);
		hist->loadAround(0);

		App::main()->finishForwarding(hist);
	}
	if (replyTo < 0) cancelReply(lastKeyboardUsed);
	if (_previewData && _previewData->pendingTill) previewCancel();
	_field.setFocus();

	if (!_keyboard.hasMarkup() && _keyboard.forceReply() && !_kbReplyTo) onKbToggle();
}

void HistoryWidget::onBotStart() {
	if (histPeer->chat || !histPeer->asUser()->botInfo) {
		updateControlsVisibility();
		return;
	}
	QString token = histPeer->asUser()->botInfo->startToken;
	if (token.isEmpty()) {
		sendBotCommand(qsl("/start"), 0);
	} else {
		uint64 randomId = MTP::nonce<uint64>();
		MTP::send(MTPmessages_StartBot(histPeer->asUser()->inputUser, MTP_int(0), MTP_long(randomId), MTP_string(token)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, histPeer->asUser()));

		histPeer->asUser()->botInfo->startToken = QString();
		if (_keyboard.hasMarkup()) {
			if (_keyboard.singleUse() && _keyboard.forMsgId() == hist->lastKeyboardId && hist->lastKeyboardUsed) _kbWasHidden = true;
			if (!_kbWasHidden) _kbShown = _keyboard.hasMarkup();
		}
	}
	updateControlsVisibility();
	resizeEvent(0);
}

void HistoryWidget::onShareContact(const PeerId &peer, UserData *contact) {
	if (!contact || contact->phone.isEmpty()) return;

	App::main()->showPeer(peer, 0, false, true);
	if (!hist) return;

	shareContact(peer, contact->phone, contact->firstName, contact->lastName, replyToId(), int32(contact->id & 0xFFFFFFFF));
}

void HistoryWidget::shareContact(const PeerId &peer, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, int32 userId) {
	History *h = App::history(peer);
	App::main()->readServerHistory(h, false);

	uint64 randomId = MTP::nonce<uint64>();
	MsgId newId = clientMsgId();

	h->loadAround(0);

	PeerData *p = App::peer(peer);
	int32 flags = newMessageFlags(p); // unread, out
	
	bool lastKeyboardUsed = lastForceReplyReplied(replyTo);

	int32 sendFlags = 0;
	if (replyTo) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}
	h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(peer), MTPint(), MTPint(), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname), MTP_int(userId)), MTPnullMarkup));
	h->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), p->input, MTP_int(replyTo), MTP_inputMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, hist->sendRequestId);

	App::historyRegRandom(randomId, newId);

	App::main()->finishForwarding(h);
	cancelReply(lastKeyboardUsed);
}

void HistoryWidget::onSendPaths(const PeerId &peer) {
	App::main()->showPeer(peer, 0, false, true);
	if (!hist) return;

	uploadMedias(cSendPaths(), ToPrepareDocument);
}

PeerData *HistoryWidget::peer() const {
	return histPeer;
}

PeerData *HistoryWidget::activePeer() const {
	return histPeer ? histPeer : (_activeHist ? _activeHist->peer : 0);
}

MsgId HistoryWidget::activeMsgId() const {
	return (_loadingAroundId >= 0) ? _loadingAroundId : (hist ? hist->activeMsgId : (_activeHist ? _activeHist->activeMsgId : 0));
}

int32 HistoryWidget::lastWidth() const {
	return width();
}

int32 HistoryWidget::lastScrollTop() const {
	return _scroll.scrollTop();
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
		if (hist && hist->readyForWork()) {
			_scroll.show();
			if (hist->lastScrollTop == History::ScrollMax) {
				_scroll.scrollToY(hist->lastScrollTop);
			}

			onListScroll();
		}
		if (hist) {
			if (!_histInited) checkUnreadLoaded();
			if (_histNeedUpdate) updateListSize();
		}
		updateControlsVisibility();
		App::wnd()->setInnerFocus();
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

void HistoryWidget::animStop() {
	if (!_showAnim.animating()) return;
	_showAnim.stop();
}

bool HistoryWidget::recordStep(float64 ms) {
	float64 dt = ms / st::btnSend.duration;
	bool res = true;
	if (dt >= 1 || !_send.isHidden() || isBotStart()) {
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
	if (!hist) return;

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
	if (!hist) return;

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
	if (!hist) return;

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
		stopRecording(_inField);
	}
}

void HistoryWidget::stopRecording(bool send) {
	audioCapture()->stop(send);

	a_recordingLevel = anim::ivalue(0, 0);
	_recordingAnim.stop();

	_recording = false;
	_recordingSamples = 0;
	updateControlsVisibility();
	activate();

	updateField();

	a_recordDown.start(0);
	a_recordOver.restart();
	a_recordCancel = anim::cvalue(st::recordCancel->c, st::recordCancel->c);
	_recordAnim.start();
}

void HistoryWidget::sendBotCommand(const QString &cmd, MsgId replyTo) { // replyTo != 0 from ReplyKeyboardMarkup, == 0 from cmd links
	if (!hist) return;

	App::main()->readServerHistory(hist, false);
	hist->loadAround(0);

	bool lastKeyboardUsed = (_keyboard.forMsgId() == hist->lastKeyboardId) && (_keyboard.forMsgId() == replyTo);

	QString toSend = cmd;
	UserData *bot = histPeer->chat ? (App::hoveredLinkItem() ? (App::hoveredLinkItem()->toHistoryForwarded() ? App::hoveredLinkItem()->toHistoryForwarded()->fromForwarded() : App::hoveredLinkItem()->from()) : 0) : histPeer->asUser();
	QString username = (bot && bot->botInfo) ? bot->username : QString();
	if (!replyTo && toSend.indexOf('@') < 2 && histPeer->chat && !username.isEmpty() && (histPeer->asChat()->botStatus == 0 || histPeer->asChat()->botStatus == 2)) {
		toSend += '@' + username;
	}

	int32 botStatus = histPeer->chat ? histPeer->asChat()->botStatus : -1;
	App::main()->sendPreparedText(hist, toSend, replyTo ? ((histPeer->chat/* && (botStatus == 0 || botStatus == 2)*/) ? replyTo : -1) : 0);
	if (replyTo) {
		cancelReply();
		if (_keyboard.singleUse() && _keyboard.hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) onKbToggle(false);
			hist->lastKeyboardUsed = true;
		}
	}
}

void HistoryWidget::insertBotCommand(const QString &cmd) {
	if (!hist) return;

	QString toInsert = cmd;
	UserData *bot = histPeer->chat ? (App::hoveredLinkItem() ? (App::hoveredLinkItem()->toHistoryForwarded() ? App::hoveredLinkItem()->toHistoryForwarded()->fromForwarded() : App::hoveredLinkItem()->from()) : 0) : histPeer->asUser();
	QString username = (bot && bot->botInfo) ? bot->username : QString();
	if (toInsert.indexOf('@') < 2 && histPeer->chat && !username.isEmpty() && (histPeer->asChat()->botStatus == 0 || histPeer->asChat()->botStatus == 2)) {
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
	if (!d) return DragStateNone;

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
	if (!hist || !histPeer || histPeer->chat || !histPeer->asUser()->botInfo) return false;
	return !histPeer->asUser()->botInfo->startToken.isEmpty() || (hist->isEmpty() && !hist->lastMsg);
}

bool HistoryWidget::updateCmdStartShown() {
	bool cmdStartShown = false;
	if (hist && histPeer && ((histPeer->chat && histPeer->asChat()->botStatus > 0) || (!histPeer->chat && histPeer->asUser()->botInfo))) {
		if (!isBotStart() && !_keyboard.hasMarkup() && !_keyboard.forceReply()) {
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

void HistoryWidget::onDocumentDrop(QDropEvent *e) {
	if (!hist) return;

	QStringList files = getMediasFromMime(e->mimeData());
	if (files.isEmpty()) return;

	uploadMedias(files, ToPrepareDocument);
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
			if (hist) {
				hist->clearLastKeyboard();
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

		_kbReplyTo = hist->peer->chat ? App::histItemById(_keyboard.forMsgId()) : 0;
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

		_kbReplyTo = hist->peer->chat ? App::histItemById(_keyboard.forMsgId()) : 0;
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

void HistoryWidget::onPhotoDrop(QDropEvent *e) {
	if (!hist) return;

	if (e->mimeData()->hasImage()) {
		QImage image = qvariant_cast<QImage>(e->mimeData()->imageData());
		if (image.isNull()) return;
		
		uploadImage(image);
	} else {
		QStringList files = getMediasFromMime(e->mimeData());
		if (files.isEmpty()) return;

		uploadMedias(files, ToPreparePhoto);
	}
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

	if (!hist) return;

	int32 increaseLeft = cWideMode() ? 0 : (st::topBarForwardPadding.right() - st::topBarForwardPadding.left());
	decreaseWidth += increaseLeft;
	QRect rectForName(st::topBarForwardPadding.left() + increaseLeft, st::topBarForwardPadding.top(), width() - decreaseWidth - st::topBarForwardPadding.left() - st::topBarForwardPadding.right(), st::msgNameFont->height);
	p.setFont(st::dlgHistFont->f);
	if (hist->typing.isEmpty()) {
		p.setPen(st::titleStatusColor->p);
		p.drawText(rectForName.x(), st::topBarHeight - st::topBarForwardPadding.bottom() - st::dlgHistFont->height + st::dlgHistFont->ascent, titlePeerText);
	} else {
		p.setPen(st::titleTypingColor->p);
		hist->typingText.drawElided(p, rectForName.x(), st::topBarHeight - st::topBarForwardPadding.bottom() - st::dlgHistFont->height, rectForName.width());
	}

	p.setPen(st::dlgNameColor->p);
	hist->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());

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
		if (hist) App::main()->showPeerProfile(histPeer);
	} else {
		App::main()->onShowDialogs();
	}
}

void HistoryWidget::updateOnlineDisplay(int32 x, int32 w) {
	if (!hist) return;

	QString text;
	int32 t = unixtime();
	if (histPeer->chat) {
		ChatData *chat = histPeer->asChat();
		if (chat->forbidden) {
			text = lang(lng_chat_status_unaccessible);
		} else if (chat->participants.isEmpty()) {
			text = titlePeerText.isEmpty() ? lng_chat_status_members(lt_count, chat->count < 0 ? 0 : chat->count) : titlePeerText;
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
		text = App::onlineText(histPeer->asUser(), t);
	}
	if (titlePeerText != text) {
		titlePeerText = text;
		titlePeerTextWidth = st::dlgHistFont->m.width(titlePeerText);
		if (App::main()) {
			App::main()->topBar()->update();
		}
	}
	updateOnlineDisplayTimer();
}

void HistoryWidget::updateOnlineDisplayTimer() {
	if (!hist) return;

	int32 t = unixtime(), minIn = 86400;
	if (histPeer->chat) {
		ChatData *chat = histPeer->asChat();
		if (chat->participants.isEmpty()) return;

		for (ChatData::Participants::const_iterator i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key(), t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else {
		minIn = App::onlineWillChangeIn(histPeer->asUser(), t);
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
	if (!hist || _showAnim.animating()) return;

	QString start;
	_field.getMentionHashtagBotCommandStart(start);
	if (!start.isEmpty()) {
		if (start.at(0) == '#' && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) Local::readRecentHashtags();
		if (start.at(0) == '@' && !hist->peer->chat) return;
		if (start.at(0) == '/' && !hist->peer->chat && !hist->peer->asUser()->botInfo) return;
		_attachMention.showFiltered(hist->peer, start);
	} else if (!_attachMention.isHidden()) {
		_attachMention.hideStart();
	}
}

void HistoryWidget::onFieldCursorChanged() {
	checkMentionDropdown();
	onDraftSaveDelayed();
}

void HistoryWidget::uploadImage(const QImage &img, bool withText, const QString &source) {
	if (!hist || confirmImageId) return;

	App::wnd()->activateWindow();
	confirmImage = img;
	confirmWithText = withText;
	confirmSource = source;
	confirmImageId = imageLoader.append(img, histPeer->id, replyToId(), ToPreparePhoto);
}

void HistoryWidget::uploadFile(const QString &file, bool withText) {
	if (!hist || confirmImageId) return;

	App::wnd()->activateWindow();
	confirmWithText = withText;
	confirmImageId = imageLoader.append(file, histPeer->id, replyToId(), ToPrepareDocument);
}

void HistoryWidget::shareContactConfirmation(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool withText) {
	if (!hist || confirmImageId) return;

	App::wnd()->activateWindow();
	confirmWithText = withText;
	confirmImageId = 0xFFFFFFFFFFFFFFFFL;
	App::wnd()->showLayer(new PhotoSendBox(phone, fname, lname, replyTo));
}

void HistoryWidget::uploadConfirmImageUncompressed(bool ctrlShiftEnter, MsgId replyTo) {
	if (!hist || !confirmImageId || confirmImage.isNull()) return;

	App::wnd()->activateWindow();
	PeerId peerId = histPeer->id;
	if (confirmWithText) {
		onSend(ctrlShiftEnter, replyTo);
	}
	bool lastKeyboardUsed = lastForceReplyReplied(replyTo);
	imageLoader.append(confirmImage, peerId, replyTo, ToPrepareDocument, ctrlShiftEnter);
	confirmImageId = 0;
	confirmWithText = false;
	confirmImage = QImage();
	cancelReply(lastKeyboardUsed);
}

void HistoryWidget::uploadMedias(const QStringList &files, ToPrepareMediaType type) {
	if (!hist) return;

	App::wnd()->activateWindow();
	imageLoader.append(files, histPeer->id, replyToId(), type);
	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::uploadMedia(const QByteArray &fileContent, ToPrepareMediaType type, PeerId peer) {
	if (!peer && !hist) return;

	App::wnd()->activateWindow();
	imageLoader.append(fileContent, peer ? peer : histPeer->id, replyToId(), type);
	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::onPhotoReady() {
	QMutexLocker lock(imageLoader.readyMutex());
	ReadyLocalMedias &list(imageLoader.readyList());

	for (ReadyLocalMedias::const_iterator i = list.cbegin(), e = list.cend(); i != e; ++i) {
		if (i->id == confirmImageId) {
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
	if (!confirmSource.isEmpty()) confirmSource = QString();
}

void HistoryWidget::onSendCancelled() {
	if (!confirmSource.isEmpty()) {
		_field.textCursor().insertText(confirmSource);
		confirmSource = QString();
	}
}

void HistoryWidget::onPhotoFailed(quint64 id) {
}

void HistoryWidget::confirmShareContact(bool ctrlShiftEnter, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo) {
	if (!histPeer) return;

	PeerId peerId = histPeer->id;
	if (0xFFFFFFFFFFFFFFFFL == confirmImageId) {
		if (confirmWithText) {
			onSend(ctrlShiftEnter, replyTo);
		}
		confirmImageId = 0;
		confirmWithText = false;
		confirmImage = QImage();
	}
	shareContact(peerId, phone, fname, lname, replyTo);
}

void HistoryWidget::confirmSendImage(const ReadyLocalMedia &img) {
	if (img.id == confirmImageId) {
		if (confirmWithText) {
			onSend(img.ctrlShiftEnter, img.replyTo);
		}
		confirmImageId = 0;
		confirmWithText = false;
		confirmImage = QImage();
	}
	MsgId newId = clientMsgId();

	connect(App::uploader(), SIGNAL(photoReady(MsgId, const MTPInputFile &)), this, SLOT(onPhotoUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentReady(MsgId, const MTPInputFile &)), this, SLOT(onDocumentUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(thumbDocumentReady(MsgId, const MTPInputFile &, const MTPInputFile &)), this, SLOT(onThumbDocumentUploaded(MsgId, const MTPInputFile &, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(audioReady(MsgId, const MTPInputFile &)), this, SLOT(onAudioUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
//	connect(App::uploader(), SIGNAL(photoProgress(MsgId)), this, SLOT(onPhotoProgress(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentProgress(MsgId)), this, SLOT(onDocumentProgress(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(audioProgress(MsgId)), this, SLOT(onAudioProgress(MsgId)), Qt::UniqueConnection);
//	connect(App::uploader(), SIGNAL(photoFailed(MsgId)), this, SLOT(onPhotoFailed(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentFailed(MsgId)), this, SLOT(onDocumentFailed(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(audioFailed(MsgId)), this, SLOT(onAudioFailed(MsgId)), Qt::UniqueConnection);

	App::uploader()->uploadMedia(newId, img);

	History *h = App::history(img.peer);
	h->loadAround(0);
	int32 flags = newMessageFlags(h->peer); // unread, out
	if (img.replyTo) flags |= MTPDmessage::flag_reply_to_msg_id;
	if (img.type == ToPreparePhoto) {
		h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTPint(), MTPint(), MTP_int(img.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaPhoto(img.photo, MTP_string("")), MTPnullMarkup));
	} else if (img.type == ToPrepareDocument) {
		h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTPint(), MTPint(), MTP_int(img.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(img.document), MTPnullMarkup));
	} else if (img.type == ToPrepareAudio) {
		h->addToBack(MTP_message(MTP_int(flags), MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTPint(), MTPint(), MTP_int(img.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaAudio(img.audio), MTPnullMarkup));
	}

	if (hist && histPeer && img.peer == histPeer->id) {
		App::main()->historyToDown(hist);
	}
	App::main()->dialogsToUp();
	peerMessagesUpdated(img.peer);
}

void HistoryWidget::cancelSendImage() {
	if (confirmImageId && confirmWithText) setFieldText(QString());
	confirmImageId = 0;
	confirmWithText = false;
	confirmImage = QImage();
}

void HistoryWidget::onPhotoUploaded(MsgId newId, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		//App::main()->readServerHistory(item->history(), false);

		uint64 randomId = MTP::nonce<uint64>();
		App::historyRegRandom(randomId, newId);
		History *hist = item->history();
		MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
		int32 sendFlags = 0;
		if (replyTo) {
			sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
		}
		hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedPhoto(file, MTP_string("")), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendPhotoFailed, randomId), 0, 0, hist->sendRequestId);
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
		} else if (document->type == StickerDocument && document->sticker) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_string(document->sticker->alt), document->sticker->set));
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
			//App::main()->readServerHistory(item->history(), false);

			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedDocument(file, MTP_string(document->mime), _composeDocumentAttributes(document)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, hist->sendRequestId);
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
			//App::main()->readServerHistory(item->history(), false);

			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedThumbDocument(file, thumb, MTP_string(document->mime), _composeDocumentAttributes(document)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, hist->sendRequestId);
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
			//App::main()->readServerHistory(item->history(), false);

			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedAudio(file, MTP_int(audio->duration), MTP_string(audio->mime)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onDocumentProgress(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onAudioProgress(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onDocumentFailed(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::onAudioFailed(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::peerMessagesUpdated(PeerId peer) {
	if (histPeer && _list && peer == histPeer->id) {
		updateListSize();
		updateBotKeyboard();
	}
}

void HistoryWidget::peerMessagesUpdated() {
	if (_list) updateListSize();
}

void HistoryWidget::msgUpdated(PeerId peer, const HistoryItem *msg) {
	if (histPeer && _list && peer == histPeer->id) {
		_list->updateMsg(msg);
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
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
	updateListSize();

	bool kbShowShown = hist && !_kbShown && _keyboard.hasMarkup();
	_field.resize(width() - _send.width() - _attachDocument.width() - _attachEmoji.width() - (kbShowShown ? _kbShow.width() : 0) - (_cmdStartShown ? _cmdStart.width() : 0), _field.height());

	_toHistoryEnd.move((width() - _toHistoryEnd.width()) / 2, _scroll.y() + _scroll.height() - _toHistoryEnd.height() - st::historyToEndSkip);

	_send.move(width() - _send.width(), _attachDocument.y());
	_botStart.setGeometry(0, _attachDocument.y(), width(), _botStart.height());
	_attachEmoji.move(_send.x() - _attachEmoji.width(), height() - kbh - _attachEmoji.height());
	_kbShow.move(_attachEmoji.x() - _kbShow.width(), height() - kbh - _kbShow.height());
	_kbHide.move(_attachEmoji.x(), _attachEmoji.y());
	_cmdStart.move(_attachEmoji.x() - _cmdStart.width(), height() - kbh - _cmdStart.height());

	_attachType.move(0, _attachDocument.y() - _attachType.height());
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
	if (!hist || (!_histInited && !initial)) return;

	if (!isVisible() || _showAnim.animating()) {
		if (initial) {
			_histInited = false;
		} else {
			_histNeedUpdate = true;
		}
		if (resizedItem) _list->recountHeight(true);
		return; // scrollTopMax etc are not working after recountHeight()
	}

	int32 newScrollHeight = height();
	if (isBotStart()) {
		newScrollHeight -= _botStart.height();
	} else {
		if (hist->readyForWork() && (!histPeer->chat || !histPeer->asChat()->forbidden)) {
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
		hist->lastScrollTop = _scroll.scrollTop();
	}
	int32 newSt = _list->recountHeight(!!resizedItem);
	bool washidden = _scroll.isHidden();
	if (washidden) {
		_scroll.show();
	}
	_list->updateSize();
	if (resizedItem && !resizedItem->detached() && scrollToIt) {
		int32 firstItemY = _list->height() - hist->height - st::historyPadding;
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
	if (!hist->readyForWork()) return;

	if ((!initial && !wasAtBottom) || loadedDown) {
		_scroll.scrollToY(newSt + addToY);
		return;
	}

	if (initial) {
		_histInited = true;
	}
	_histNeedUpdate = false;

	int32 toY = History::ScrollMax;
	if (initial && hist->activeMsgId && !hist->lastWidth) {
		HistoryItem *item = App::histItemById(hist->activeMsgId);
		if (!item || item->detached()) {
			hist->activeMsgId = 0;
			return updateListSize(addToY, initial);
		} else {
			toY = (_scroll.height() > item->height()) ? qMax(item->y + item->block()->y - (_scroll.height() - item->height()) / 2, 0) : (item->y + item->block()->y);
			_animActiveStart = getms();
			_animActiveTimer.start(AnimationTimerDelta);
		}
	} else if (initial && hist->unreadBar) {
		toY = hist->unreadBar->y + hist->unreadBar->block()->y;
	} else if (hist->showFrom) {
		toY = hist->showFrom->y + hist->showFrom->block()->y;
		if (toY < _scroll.scrollTopMax() + st::unreadBarHeight) {
			hist->addUnreadBar();
			if (hist->unreadBar) {
				hist->activeMsgId = 0;
				return updateListSize(0, true);
			}
		}
	} else if (initial && hist->lastWidth) {
		toY = newSt;
		hist->lastWidth = 0;
	} else {
	}
	_scroll.scrollToY(toY);
}

void HistoryWidget::addMessagesToFront(const QVector<MTPMessage> &messages) {
	int32 oldH = hist->height;
	_list->messagesReceived(messages);
	updateListSize(hist->height - oldH);
	updateBotKeyboard();
	checkUnreadLoaded(true);
}

void HistoryWidget::addMessagesToBack(const QVector<MTPMessage> &messages) {
	int32 sliceFrom = 0;
	_list->messagesReceivedDown(messages);
	updateListSize(0, false, true);
	checkUnreadLoaded(true);
}

void HistoryWidget::updateBotKeyboard() {
	bool changed = false;
	bool wasVisible = _kbShown || _kbReplyTo;
	if ((_replyToId && !_replyTo) || !hist) {
		changed = _keyboard.updateMarkup(0);
	} else if (_replyTo) {
		changed = _keyboard.updateMarkup(_replyTo);
	} else {
		changed = _keyboard.updateMarkup(hist->lastKeyboardId ? App::histItemById(hist->lastKeyboardId) : 0);
	}
	updateCmdStartShown();
	if (!changed) return;

	bool hasMarkup = _keyboard.hasMarkup(), forceReply = _keyboard.forceReply() && !_replyTo;
	if (hasMarkup || forceReply) {
		if (_keyboard.singleUse() && _keyboard.hasMarkup() && _keyboard.forMsgId() == hist->lastKeyboardId && hist->lastKeyboardUsed) _kbWasHidden = true;
		if (!isBotStart() && (wasVisible || _replyTo || (_field.getLastText().isEmpty() && !_kbWasHidden))) {
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
			_kbReplyTo = hist->peer->chat ? App::histItemById(_keyboard.forMsgId()) : 0;
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
	bool toEndVisible = !_showAnim.animating() && hist && hist->readyForWork() && (!hist->loadedAtBottom() || _replyReturn || _scroll.scrollTop() + st::wndMinHeight < _scroll.scrollTopMax());
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
		App::main()->showPeer(histPeer->id, replyToId());
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!hist) return;

	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		onCancel();
	} else if (e->key() == Qt::Key_PageDown) {
		if ((e->modifiers() & Qt::ControlModifier) || (e->modifiers() & Qt::MetaModifier)) {
			PeerData *after = 0;
			MsgId afterMsgId = 0;
			App::main()->peerAfter(histPeer, hist ? hist->activeMsgId : 0, after, afterMsgId);
			if (after) App::main()->showPeer(after->id, afterMsgId);
		} else {
			_scroll.keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_PageUp) {
		if ((e->modifiers() & Qt::ControlModifier) || (e->modifiers() & Qt::MetaModifier)) {
			PeerData *before = 0;
			MsgId beforeMsgId = 0;
			App::main()->peerBefore(histPeer, hist ? hist->activeMsgId : 0, before, beforeMsgId);
			if (before) App::main()->showPeer(before->id, beforeMsgId);
		} else {
			_scroll.keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Down) {
		if (e->modifiers() & Qt::AltModifier) {
			PeerData *after = 0;
			MsgId afterMsgId = 0;
			App::main()->peerAfter(histPeer, hist ? hist->activeMsgId : 0, after, afterMsgId);
			if (after) App::main()->showPeer(after->id, afterMsgId);
		} else if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll.keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Up) {
		if (e->modifiers() & Qt::AltModifier) {
			PeerData *before = 0;
			MsgId beforeMsgId = 0;
			App::main()->peerBefore(histPeer, hist ? hist->activeMsgId : 0, before, beforeMsgId);
			if (before) App::main()->showPeer(before->id, beforeMsgId);
		} else if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll.keyPressEvent(e);
		}
	} else if ((e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) && ((e->modifiers() & Qt::ControlModifier) || (e->modifiers() & Qt::MetaModifier))) {
		PeerData *p = 0;
		MsgId m = 0;
		if ((e->modifiers() & Qt::ShiftModifier) || e->key() == Qt::Key_Backtab) {
			App::main()->peerBefore(histPeer, hist ? hist->activeMsgId : 0, p, m);
		} else {
			App::main()->peerAfter(histPeer, hist ? hist->activeMsgId : 0, p, m);
		}
		if (p) App::main()->showPeer(p->id, m);
	} else {
		e->ignore();
	}
}

void HistoryWidget::onStickerSend(DocumentData *sticker) {
	if (!hist || !sticker) return;

	App::main()->readServerHistory(hist, false);

	uint64 randomId = MTP::nonce<uint64>();
	MsgId newId = clientMsgId();

	hist->loadAround(0);

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = (histPeer->input.type() != mtpc_inputPeerSelf), unread = (histPeer->input.type() != mtpc_inputPeerSelf);
	int32 flags = newMessageFlags(histPeer); // unread, out
	int32 sendFlags = 0;
	if (replyToId()) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}
	hist->addToBackDocument(newId, flags, replyToId(), date(MTP_int(unixtime())), MTP::authedId(), sticker);

	hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), histPeer->input, MTP_int(replyToId()), MTP_inputMediaDocument(MTP_inputDocument(MTP_long(sticker->id), MTP_long(sticker->access))), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, hist->sendRequestId);
	App::main()->finishForwarding(hist);
	cancelReply(lastKeyboardUsed);

	if (sticker->sticker) App::main()->incrementSticker(sticker);

	App::historyRegRandom(randomId, newId);
	App::main()->historyToDown(hist);

	App::main()->dialogsToUp();
	peerMessagesUpdated(histPeer->id);

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
	return _keyboard.forceReply() && _keyboard.forMsgId() == hist->lastKeyboardId && _keyboard.forMsgId() == (replyTo < 0 ? replyToId() : replyTo);
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
			if (sticker->document() && sticker->document()->sticker && sticker->document()->sticker->set.type() != mtpc_inputStickerSetEmpty) {
				App::main()->stickersBox(sticker->document()->sticker->set);
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
						desc = _previewData->url;
					} else {
						title = _previewData->description;
						desc = _previewData->author.isEmpty() ? _previewData->url : _previewData->author;
					}
				} else {
					title = _previewData->title;
					desc = _previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? _previewData->url : _previewData->author) : _previewData->description;
				}
			} else {
				title = _previewData->siteName;
				desc = _previewData->title.isEmpty() ? (_previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? _previewData->url : _previewData->author) : _previewData->description) : _previewData->title;
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
	if (App::main()) App::main()->showPeer(0);
	emit cancelled();
}

void HistoryWidget::onFullPeerUpdated(PeerData *data) {
	peerUpdated(data);
	if (_list && data == histPeer) {
		checkMentionDropdown();
		_list->updateBotInfo();
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		resizeEvent(0);
		update();
	}
}

void HistoryWidget::peerUpdated(PeerData *data) {
	if (data && data == histPeer) {
		updateListSize();
		if (!_showAnim.animating()) updateControlsVisibility();
		if (data->chat && data->asChat()->count > 0 && data->asChat()->participants.isEmpty()) {
			App::api()->requestFullPeer(data);
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
	if (!hist || !hist->activeMsgId) return _animActiveTimer.stop();
	HistoryItem *item = App::histItemById(hist->activeMsgId);
	if (!item || item->detached()) return _animActiveTimer.stop();

	App::main()->msgUpdated(histPeer->id, item);
}

uint64 HistoryWidget::animActiveTime() const {
	return _animActiveTimer.isActive() ? (getms() - _animActiveStart) : 0;
}

void HistoryWidget::stopAnimActive() {
	_animActiveTimer.stop();
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
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart()) {
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
				p.setPen(((drawReplyTo->getMedia() || drawReplyTo->serviceMsg()) ? st::msgInDateColor : st::msgColor)->p);
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
		if (_previewData->photo && !_previewData->photo->thumb->isNull()) {
			ImagePtr replyPreview = _previewData->photo->makeReplyPreview();
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

	bool hasTopBar = !App::main()->topBar()->isHidden();
	QRect fill(0, 0, width(), App::main()->height());
	int fromy = hasTopBar ? (-st::topBarHeight) : 0, x = 0, y = 0;
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
		if (!_scroll.isHidden()) {
			if (!_field.isHidden() || _recording) {
				drawField(p);
				if (_send.isHidden()) {
					drawRecordButton(p);
					if (_recording) drawRecording(p);
				}
			}
		} else {
			QPoint dogPos((width() - st::msgDogImg.pxWidth()) / 2, ((height() - _field.height() - 2 * st::sendPadding - st::msgDogImg.pxHeight()) * 4) / 9);
			p.drawPixmap(dogPos, *cChatDogImage());

			int32 pointsCount = 8, w = pointsCount * (st::introPointWidth + 2 * st::introPointDelta), h = st::introPointHeight;
			int32 pointsLeft = (width() - w) / 2 + st::introPointDelta - st::introPointLeft, pointsTop = dogPos.y() + (st::msgDogImg.pxHeight() * 6) / 5;

			int32 curPoint = histRequestsCount % pointsCount;

			p.fillRect(pointsLeft + curPoint * (st::introPointWidth + 2 * st::introPointDelta), pointsTop, st::introPointHoverWidth, st::introPointHoverHeight, App::introPointHoverColor()->b);

			// points
			p.setOpacity(st::introPointAlpha);
			int32 x = pointsLeft + st::introPointLeft;
			for (int32 i = 0; i < pointsCount; ++i) {
				p.fillRect(x, pointsTop + st::introPointTop, st::introPointWidth, st::introPointHeight, st::introPointColor->b);
				x += (st::introPointWidth + 2 * st::introPointDelta);
			}
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
	showPeer(0);
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
