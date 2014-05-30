/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
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
#include "fileuploader.h"
#include "supporttl.h"

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

HistoryList::HistoryList(HistoryWidget *historyWidget, ScrollArea *scroll, History *history) : QWidget(0), 
	historyWidget(historyWidget), scrollArea(scroll), hist(history), currentBlock(0), currentItem(0), _menu(0),
	_dragAction(NoDrag), _dragItem(0), _dragSelFrom(0), _dragSelTo(0), _dragSelecting(false),
	_dragSelType(TextSelectLetters), _dragWasInactive(false),
	_touchScroll(false), _touchSelect(false), _touchInProgress(false),
	_touchScrollState(TouchScrollManual), _touchPrevPosValid(false), _touchWaitingAcceleration(false), _touchSpeedTime(0), _touchAccelerationTime(0), _touchTime(0),
	_cursor(style::cur_default) {

	linkTipTimer.setSingleShot(true);
	connect(&linkTipTimer, SIGNAL(timeout()), this, SLOT(showLinkTip()));
	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));

	_trippleClickTimer.setSingleShot(true);

	setMouseTracking(true);
}

void HistoryList::messagesReceived(const QVector<MTPMessage> &messages) {
	hist->addToFront(messages);
}

void HistoryList::updateMsg(HistoryItem *msg) {
	if (!msg || !hist || hist != msg->history()) return;
	update(0, height() - hist->height - st::historyPadding + msg->block()->y + msg->y, width(), msg->height());
}

void HistoryList::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	bool trivial = (rect() == r);

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(r);
	} else {
		bool nt = true;
	}

	if (hist->isEmpty()) {
		QPoint dogPos((width() - st::msgDogImg.width()) / 2, ((height() - st::msgDogImg.height()) * 4) / 9);
		p.drawPixmap(dogPos, App::sprite(), st::msgDogImg);
	} else {
		adjustCurrent(r.top());
		HistoryBlock *block = (*hist)[currentBlock];
		HistoryItem *item = (*block)[currentItem];

		SelectedItems::const_iterator selEnd = _selected.cend();
		bool hasSel = !_selected.isEmpty();

		int32 firstItemY = height() - hist->height - st::historyPadding, drawToY = r.bottom() - firstItemY;

		int32 selfromy = 0, seltoy = 0;
		if (_dragSelFrom && _dragSelTo) {
			selfromy = _dragSelFrom->y + _dragSelFrom->block()->y;
			seltoy = _dragSelTo->y + _dragSelTo->block()->y + _dragSelTo->height();
		}

		int32 iBlock = currentBlock, iItem = currentItem, y = block->y + item->y;
		p.translate(0, firstItemY + y);
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
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || (oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
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
	onUpdateSelected(true);
}

void HistoryList::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	historyWidget->touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

QPoint HistoryList::mapMouseToItem(QPoint p, HistoryItem *item) {
	if (!item) return QPoint(0, 0);
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
			} else {
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
			} else {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}

	if (!_dragItem) {
		_dragAction = NoDrag;
	} else if (_dragAction == NoDrag) {
		_dragItem = 0;
	} else {
		connect(App::main(), SIGNAL(historyItemDeleted(HistoryItem*)), this, SLOT(itemRemoved(HistoryItem*)), Qt::UniqueConnection);
	}
}

void HistoryList::dragActionCancel() {
	_dragItem = 0;
	_dragAction = NoDrag;
	_dragStartPos = QPoint(0, 0);
	historyWidget->noSelectingScroll();
}

void HistoryList::itemRemoved(HistoryItem *item) {
	if (_dragItem == item) {
		dragActionCancel();
	}

	SelectedItems::iterator i = _selected.find(item);
	if (i != _selected.cend()) {
		_selected.erase(i);
		update();
	}

	onUpdateSelected(true);

	if (_dragSelFrom == item) _dragSelFrom = 0;
	if (_dragSelTo == item) _dragSelTo = 0;
	updateDragSelection(_dragSelFrom, _dragSelTo, _dragSelecting, true);

	parentWidget()->update();
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
	if (needClick) {
		needClick->onClick(button);
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
		} else if (!_selected.isEmpty() && !_dragWasInactive) {
			uint32 sel = _selected.cbegin().value();
			if (sel != FullItemSel && (sel & 0xFFFF) == ((sel >> 16) & 0xFFFF)) {
				_selected.clear();
				App::main()->activate();
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
	if (_dragAction == Selecting && _dragSelType == TextSelectLetters && _dragItem && !_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
		bool afterDragSymbol, uponSelected;
		uint16 symbol;
		_dragItem->getSymbol(symbol, afterDragSymbol, uponSelected, _dragStartPos.x(), _dragStartPos.y());
		if (uponSelected) {
			_dragSymbol = symbol;
			_dragSelType = TextSelectWords;
			mouseMoveEvent(e);

	        _trippleClickPoint = e->globalPos();
	        _trippleClickTimer.start(QApplication::doubleClickInterval());
		}
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
    if (_contextMenuLnk && dynamic_cast<TextLink*>(_contextMenuLnk.data())) {
		_menu = new QMenu(historyWidget);
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
		}
		_menu->addAction(lang(lng_context_open_link), this, SLOT(openContextUrl()))->setEnabled(true);
		_menu->addAction(lang(lng_context_copy_link), this, SLOT(copyContextUrl()))->setEnabled(true);
    } else if (_contextMenuLnk && dynamic_cast<EmailLink*>(_contextMenuLnk.data())) {
		_menu = new QMenu(historyWidget);
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
		}
		_menu->addAction(lang(lng_context_open_email), this, SLOT(openContextUrl()))->setEnabled(true);
		_menu->addAction(lang(lng_context_copy_email), this, SLOT(copyContextUrl()))->setEnabled(true);
	} else {
        PhotoLink *lnkPhoto = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
        VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
        AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
        DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
		if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument) {
			_menu = new QMenu(historyWidget);
			if (isUponSelected > 0) {
				_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
			}
			if (lnkPhoto) {
				_menu->addAction(lang(lng_context_open_image), this, SLOT(openContextUrl()))->setEnabled(true);
				_menu->addAction(lang(lng_context_save_image), this, SLOT(saveContextImage()))->setEnabled(true);
				_menu->addAction(lang(lng_context_copy_image), this, SLOT(copyContextImage()))->setEnabled(true);
			} else {
				if (lnkVideo && lnkVideo->video()->loader || lnkAudio && lnkAudio->audio()->loader || lnkDocument && lnkDocument->document()->loader) {
					_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
				} else {
					if (lnkVideo && !lnkVideo->video()->already(true).isEmpty() || lnkAudio && !lnkAudio->audio()->already(true).isEmpty() || lnkDocument && !lnkDocument->document()->already(true).isEmpty()) {
						_menu->addAction(lang(lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
					}
					_menu->addAction(lang(lnkVideo ? lng_context_open_video : (lnkAudio ? lng_context_open_audio : lng_context_open_document)), this, SLOT(openContextFile()))->setEnabled(true);
					_menu->addAction(lang(lnkVideo ? lng_context_save_video : (lnkAudio ? lng_context_save_audio : lng_context_save_document)), this, SLOT(saveContextFile()))->setEnabled(true);
				}
			}
			if (isUponSelected > 1) {
				_menu->addAction(lang(lng_context_forward_selected), historyWidget, SLOT(onForwardSelected()));
				_menu->addAction(lang(lng_context_delete_selected), historyWidget, SLOT(onDeleteSelected()));
				_menu->addAction(lang(lng_context_clear_selection), historyWidget, SLOT(onClearSelected()));
			} else if (isUponSelected != -2 && App::hoveredLinkItem()) {
				if (dynamic_cast<HistoryMessage*>(App::hoveredLinkItem())) {
					_menu->addAction(lang(lng_context_forward_msg), historyWidget, SLOT(forwardMessage()))->setEnabled(true);
				}
				_menu->addAction(lang(lng_context_delete_msg), historyWidget, SLOT(deleteMessage()))->setEnabled(true);
				App::contextItem(App::hoveredLinkItem());
			}
		} else { // maybe cursor on some text history item?
			HistoryItem *item = App::hoveredItem() ? App::hoveredItem() : App::hoveredLinkItem();
			bool canDelete = (item && item->itemType() == HistoryItem::MsgType);
			bool canForward = canDelete && (item->id > 0) && !item->serviceMsg();

			HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
			HistoryServiceMsg *srv = dynamic_cast<HistoryServiceMsg*>(item);

			if (isUponSelected > 0) {
				if (!_menu) _menu = new QMenu(this);
				_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
			} else if (item && !isUponSelected && !_contextMenuLnk) {
				QString contextMenuText = item->selectedText(FullItemSel);
				if (!contextMenuText.isEmpty()) {
					if (!_menu) _menu = new QMenu(this);
					_menu->addAction(lang(lng_context_copy_text), this, SLOT(copyContextText()))->setEnabled(true);
				}
			}

			if (isUponSelected > 1) {
				if (!_menu) _menu = new QMenu(this);
				_menu->addAction(lang(lng_context_forward_selected), historyWidget, SLOT(onForwardSelected()));
				_menu->addAction(lang(lng_context_delete_selected), historyWidget, SLOT(onDeleteSelected()));
				_menu->addAction(lang(lng_context_clear_selection), historyWidget, SLOT(onClearSelected()));
			} else if (isUponSelected != -2) {
				if (canForward) {
					if (!_menu) _menu = new QMenu(this);
					_menu->addAction(lang(lng_context_forward_msg), historyWidget, SLOT(forwardMessage()))->setEnabled(true);
				}

				if (canDelete) {
					if (!_menu) _menu = new QMenu(this);
					_menu->addAction(lang((msg && msg->uploading()) ? lng_context_cancel_upload : lng_context_delete_msg), historyWidget, SLOT(deleteMessage()))->setEnabled(true);
				}
			}
			App::contextItem(item);
		}
	}
	if (_menu) {
		_menu->setAttribute(Qt::WA_DeleteOnClose);
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
	QApplication::clipboard()->setText(getSelectedText());
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
	if (lnkVideo) VideoSaveLink(lnkVideo->video()).doSave(true);
	if (lnkAudio) AudioSaveLink(lnkAudio->audio()).doSave(true);
	if (lnkDocument) DocumentSaveLink(lnkDocument->document()).doSave(true);
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

bool HistoryList::getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const {
	HistoryItem *hoveredItem = App::hoveredLinkItem();
	if (hoveredItem && hoveredItem->getPhotoCoords(photo, x, y, w)) {		
		y += height() - hist->height - st::historyPadding + hoveredItem->block()->y + hoveredItem->y;
		return true;
	}
	return false;
}

bool HistoryList::getVideoCoords(VideoData *video, int32 &x, int32 &y, int32 &w) const {
	HistoryItem *hoveredItem = App::hoveredItem();
	if (hoveredItem && hoveredItem->getVideoCoords(video, x, y, w)) {		
		y += height() - hist->height - st::historyPadding + hoveredItem->block()->y + hoveredItem->y;
		return true;
	}
	return false;
}

void HistoryList::resizeEvent(QResizeEvent *e) {
	onUpdateSelected(true);
}

QString HistoryList::getSelectedText() const {
	if (_selected.isEmpty()) return QString();
	if (_selected.cbegin().value() != FullItemSel) {
		return _selected.cbegin().key()->selectedText(_selected.cbegin().value());
	}

	int32 fullSize = 0;
	QString timeFormat(qsl(", [dd.MM.yy hh:mm]\n"));
	QMap<int32, QString> texts;
	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
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
		historyWidget->onClearSelected();
	} else if (e == QKeySequence::Copy && !_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
		copySelectedText();
	} else if (e == QKeySequence::Delete) {
		historyWidget->onDeleteSelected();
	}
}

int32 HistoryList::recountHeight() {
	int32 st = hist->lastScrollTop;
	hist->geomResize(scrollArea->width(), &st);
	return st;
}

void HistoryList::updateSize() {
	int32 ph = scrollArea->height(), nh = (hist->height + st::historyPadding) > ph ? (hist->height + st::historyPadding) : ph;
	if (width() != scrollArea->width() || height() != nh) {
		resize(scrollArea->width(), nh);
	} else {
		update();
	}
}

void HistoryList::enterEvent(QEvent *e) {
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

	int32 dh = height() - hist->height - st::historyPadding;
	while ((*hist)[currentBlock]->y + dh > y && currentBlock > 0) {
		--currentBlock;
		currentItem = 0;
	}
	while ((*hist)[currentBlock]->y + (*hist)[currentBlock]->height + dh <= y && currentBlock + 1 < hist->size()) {
		++currentBlock;
		currentItem = 0;
	}
	HistoryBlock *block = (*hist)[currentBlock];
	if (currentItem >= block->size()) {
		currentItem = block->size() - 1;
	}
	int32 by = block->y;
	while ((*block)[currentItem]->y + by + dh > y && currentItem > 0) {
		--currentItem;
	}
	while ((*block)[currentItem]->y + (*block)[currentItem]->height() + by + dh <= y && currentItem + 1 < block->size()) {
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

void HistoryList::fillSelectedItems(HistoryItemSet &sel, bool forDelete) {
	if (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel) return;

	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		HistoryItem *item = i.key();
		if (item->itemType() == HistoryItem::MsgType && (item->id > 0 && !item->serviceMsg() || forDelete)) {
			sel.insert(item->y + item->block()->y, item);
		}
	}
}

void HistoryList::onTouchSelect() {
	_touchSelect = true;
	dragActionStart(_touchPos);
}

void HistoryList::onUpdateSelected(bool force) {
	if (hist->isEmpty()) return;

	QPoint mousePos(mapFromGlobal(_dragPos));
	QPoint m(historyWidget->clampMousePosition(mousePos));
	adjustCurrent(m.y());

	HistoryBlock *block = (*hist)[currentBlock];
	HistoryItem *item = (*block)[currentItem];
	App::mousedItem(item);
	m = mapMouseToItem(m, item);
	if (item->hasPoint(m.x(), m.y())) {
		updateMsg(App::hoveredItem());
		App::hoveredItem(item);
		updateMsg(App::hoveredItem());
	} else if (App::hoveredItem()) {
		updateMsg(App::hoveredItem());
		App::hoveredItem(0);
	}
	linkTipTimer.start(1000);

	Qt::CursorShape cur = style::cur_default;
	bool inText, lnkChanged = false;

	TextLinkPtr lnk;
	item->getState(lnk, inText, m.x(), m.y());
	if (lnk != textlnkOver()) {
		lnkChanged = true;
		updateMsg(App::hoveredLinkItem());
		textlnkOver(lnk);
		QToolTip::showText(_dragPos, QString(), App::wnd());
		App::hoveredLinkItem(lnk ? item : 0);
		updateMsg(App::hoveredLinkItem());
	}

	if (_dragAction == NoDrag) {
		if (lnk) {
			cur = style::cur_pointer;
		} else if (inText && (_selected.isEmpty() || _selected.cbegin().value() != FullItemSel)) {
			cur = style::cur_text;
		}
	} else {		
		if (item != _dragItem || (m - _dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
			if (_dragAction == PrepareDrag) {
				_dragAction = Dragging;
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
				_selected[_dragItem] = _dragItem->adjustSelection(qMin(second, _dragSymbol), qMax(second, _dragSymbol), _dragSelType);
				updateDragSelection(0, 0, false);
			} else {
				bool selectingDown = (_dragItem->block()->y < item->block()->y) || (_dragItem->block() == item->block()) && (_dragItem->y < item->y || _dragItem == item && _dragStartPos.y() < m.y());
				HistoryItem *dragSelFrom = _dragItem, *dragSelTo = item;
				if (!dragSelFrom->hasPoint(_dragStartPos.x(), _dragStartPos.y())) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_dragStartPos.y() >= dragSelFrom->height() - st::msgMargin.bottom() || (item == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance())) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelFrom);
						}
					} else {
						if (_dragStartPos.y() < st::msgMargin.top() || (item == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance())) {
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
		force = true;
	}
	if (!force) return;
	
	parentWidget()->update();
}

void HistoryList::applyDragSelection() {
	if (!_selected.isEmpty() && _selected.cbegin().value() != FullItemSel) {
		_selected.clear();
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
					if (item->id > 0 && !item->serviceMsg()) {
						SelectedItems::iterator i = _selected.find(item);
						if (i == _selected.cend()) {
							if (_selected.size() >= MaxSelectedItems) break;
							_selected.insert(item, FullItemSel);
						} else if (i.value() != FullItemSel) {
							*i = FullItemSel;
						}
					} else {
						SelectedItems::iterator i = _selected.find(item);
						if (i != _selected.cend()) {
							_selected.erase(i);
						}
					}
				}
				if (_selected.size() >= MaxSelectedItems) break;
				fromitem = 0;
			}
		}
	} else {
		for (SelectedItems::iterator i = _selected.begin(); i != _selected.cend(); ) {
			int32 iy = i.key()->y + i.key()->block()->y;
			if (iy >= fromy && iy < toy) {
				i = _selected.erase(i);
			} else {
				++i;
			}
		}
	}
	_dragSelFrom = _dragSelTo = 0;
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

MessageField::MessageField(HistoryWidget *history, const style::flatTextarea &st, const QString &ph, const QString &val) : FlatTextarea(history, st, ph, val), history(history) {
	connect(this, SIGNAL(changed()), this, SLOT(onChange()));
}

void MessageField::onChange() {
	int newh = ceil(document()->size().height());
	if (newh > st::maxFieldHeight) {
		newh = st::maxFieldHeight;
	} else if (newh < st::minFieldHeight) {
		newh = st::minFieldHeight;
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
			history->uploadImage(img);
			return;
		}
	}
	return FlatTextarea::insertFromMimeData(source);
}

void MessageField::focusInEvent(QFocusEvent *e) {
	FlatTextarea::focusInEvent(e);
	emit focused();
}

HistoryHider::HistoryHider(MainWidget *parent, bool forwardSelected) : QWidget(parent),
	aOpacity(0, 1), aOpacityFunc(anim::easeOutCirc), hiding(false), offered(0), _forwardRequest(0),
	toTextWidth(0), _forwardSelected(forwardSelected), sharedContact(0), shadow(st::boxShadow),
	forwardButton(this, lang(lng_forward), st::btnSelectDone),
	cancelButton(this, lang(lng_cancel), st::btnSelectCancel) {

	connect(&forwardButton, SIGNAL(clicked()), this, SLOT(forward()));
	connect(&cancelButton, SIGNAL(clicked()), this, SLOT(startHide()));
	connect(App::wnd()->getTitle(), SIGNAL(hiderClicked()), this, SLOT(startHide()));

	_chooseWidth = st::forwardFont->m.width(lang(lng_forward_choose));

	resizeEvent(0);
	anim::start(this);
}

HistoryHider::HistoryHider(MainWidget *parent, UserData *sharedContact) : QWidget(parent),
	aOpacity(0, 1), aOpacityFunc(anim::easeOutCirc), hiding(false), offered(0), _forwardRequest(0),
	toTextWidth(0), _forwardSelected(false), sharedContact(sharedContact), shadow(st::boxShadow),
	forwardButton(this, lang(lng_forward), st::btnSelectDone),
	cancelButton(this, lang(lng_cancel), st::btnSelectCancel) {

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
			shadow.paint(p, box);

			// fill bg
			p.fillRect(box, st::boxBG->b);

			// paint shadows
			p.fillRect(box.x(), box.y() + box.height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, box.width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.setPen(st::btnSelectSep->p);
			p.drawLine(box.x() + st::btnSelectCancel.width, box.y() + box.height() - st::btnSelectCancel.height, box.x() + st::btnSelectCancel.width, box.y() + box.height() - 1);

			p.setPen(st::black->p);
			toText.drawElided(p, box.left() + (box.width() - toTextWidth) / 2, box.top() + st::boxPadding.top(), toTextWidth + 1);
		} else {
			p.setBrush(st::forwardBG->b);
			p.setPen(Qt::NoPen);
			int32 w = st::forwardMargins.left() + _chooseWidth + st::forwardMargins.right(), h = st::forwardMargins.top() + st::forwardFont->height + st::forwardMargins.bottom();
			p.drawRoundedRect((width() - w) / 2, (height() - h) / 2, w, h, st::forwardRadius, st::forwardRadius);

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
	if (offered) cacheForAnim = grab(box);
	if (_forwardRequest) MTP::cancel(_forwardRequest);
	aOpacity.start(0);
	anim::start(this);
}

void HistoryHider::forward() {
	if (_forwardRequest) return;

	if (!hiding && offered) {
		if (sharedContact) {
			parent()->onShareContact(offered->id, sharedContact);
		} else {
			_forwardRequest = parent()->onForward(offered->id, _forwardSelected);
		}
	}
	if (!_forwardRequest) {
		startHide();
	}
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

void HistoryHider::offerPeer(PeerId peer) {
	offered = App::peer(peer);
	toText.setText(st::boxFont, lang(sharedContact ? lng_forward_share_contact : lng_forward_confirm).replace(qsl("{recipient}"), offered->chat ? '«' + offered->name + '»' : offered->name), _textNameOptions);
	toTextWidth = toText.maxWidth();
	if (toTextWidth > box.width() - st::boxPadding.left() - st::boxPadding.right()) {
		toTextWidth = box.width() - st::boxPadding.left() - st::boxPadding.right();
	}
	
	resizeEvent(0);
	update();
	setFocus();
}

bool HistoryHider::wasOffered() const {
	return !!offered;
}

HistoryHider::~HistoryHider() {
	if (App::wnd()) App::wnd()->getTitle()->setHideLevel(0);
	parent()->noHider(this);
}

HistoryWidget::HistoryWidget(QWidget *parent) : QWidget(parent), noTypingUpdate(false), serviceImageCacheSize(0),
	_scroll(this, st::historyScroll, false), _list(0), histPeer(0), _activePeer(0), histOffset(0), histCount(-1),
	hist(0), histPreloading(0), histReadRequestId(0), hiderOffered(false), _histInited(false),
	_send(this, lang(lng_send_button), st::btnSend), histRequestsCount(0),
	_attachDocument(this, st::btnAttachDocument), _attachPhoto(this, st::btnAttachPhoto), _attachEmoji(this, st::btnAttachEmoji),
	confirmImageId(0), loadingChatId(0), loadingRequestId(0), titlePeerTextWidth(0),
	_field(this, st::taMsgField, lang(lng_message_ph)), bg(st::msgBG), imageLoader(this),
	_attachType(this), _emojiPan(this), _attachDrag(DragStateNone), _attachDragDocument(this), _attachDragPhoto(this), _scrollDelta(0) {
	_scroll.setFocusPolicy(Qt::NoFocus);

	setAcceptDrops(true);

	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_attachDocument, SIGNAL(clicked()), this, SLOT(onDocumentSelect()));
	connect(&_attachPhoto, SIGNAL(clicked()), this, SLOT(onPhotoSelect()));
	connect(&_field, SIGNAL(submitted()), this, SLOT(onSend()));
	connect(&_field, SIGNAL(cancelled()), this, SIGNAL(cancelled()));
	connect(&_field, SIGNAL(tabbed()), this, SLOT(onFieldTabbed()));
	connect(&_field, SIGNAL(resized()), this, SLOT(onFieldResize()));
	connect(&_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(&imageLoader, SIGNAL(imageReady()), this, SLOT(onPhotoReady()));
	connect(&imageLoader, SIGNAL(imageFailed(quint64)), this, SLOT(onPhotoFailed(quint64)));
	connect(&_field, SIGNAL(changed()), this, SLOT(onTextChange()));
	connect(App::wnd()->windowHandle(), SIGNAL(visibleChanged(bool)), this, SLOT(onVisibleChanged()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(&_emojiPan, SIGNAL(emojiSelected(EmojiPtr)), &_field, SLOT(onEmojiInsert(EmojiPtr)));

	_scrollTimer.setSingleShot(false);

	_scroll.hide();
	_scroll.move(0, 0);
	_field.hide();
	_field.resize(width() - _send.width() - _attachDocument.width() - _attachEmoji.width(), _send.height() - 2 * st::sendPadding);
	_send.hide();

	_attachDocument.hide();
	_attachPhoto.hide();
	_attachEmoji.hide();

	_attachDocument.installEventFilter(&_attachType);
	_attachPhoto.installEventFilter(&_attachType);
	_attachEmoji.installEventFilter(&_emojiPan);

	connect(_attachType.addButton(new IconedButton(this, st::dropdownAttachDocument, lang(lng_attach_file))), SIGNAL(clicked()), this, SLOT(onDocumentSelect()));
	connect(_attachType.addButton(new IconedButton(this, st::dropdownAttachPhoto, lang(lng_attach_photo))), SIGNAL(clicked()), this, SLOT(onPhotoSelect()));
	_attachType.hide();
	_emojiPan.hide();
	_attachDragDocument.hide();
	_attachDragPhoto.hide();
	connect(&_attachDragDocument, SIGNAL(dropped(QDropEvent*)), this, SLOT(onDocumentDrop(QDropEvent*)));
	connect(&_attachDragPhoto, SIGNAL(dropped(QDropEvent*)), this, SLOT(onPhotoDrop(QDropEvent*)));
}

void HistoryWidget::onTextChange() {
	updateTyping();
}

void HistoryWidget::updateTyping(bool typing) {
	uint64 ms = getms() + 10000;
	if (noTypingUpdate || !hist || typing && (hist->myTyping + 5000 > ms) || !typing && (hist->myTyping + 5000 <= ms)) return;

	hist->myTyping = typing ? ms : 0;
	if (typing) MTP::send(MTPmessages_SetTyping(histPeer->input, MTP_bool(typing)));
}

void HistoryWidget::activate() {
	if (App::main()->selectingPeer()) {
		if (hiderOffered) {
//			hiderOffered = false;
			App::main()->focusPeerSelect();
			return;
		} else {
			App::main()->dialogsActivate();
//			App::main()->hidePeerSelect();
		}
	}
	if (_list) {
		if (_selCount) {
			_list->setFocus();
		} else {
			_field.setFocus();
		}
	}
}

void HistoryWidget::chatLoaded(const MTPmessages_ChatFull &res) {
	const MTPDmessages_chatFull &d(res.c_messages_chatFull());
	PeerId peerId = App::peerFromChat(d.vfull_chat.c_chatFull().vid);
	if (peerId == loadingChatId) {
		loadingRequestId = 0;
	}
	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);
	App::feedParticipants(d.vfull_chat.c_chatFull().vparticipants);
	PhotoData *photo = App::feedPhoto(d.vfull_chat.c_chatFull().vchat_photo);
	if (photo) {
		ChatData *chat = App::peer(peerId)->asChat();
		if (chat) {
			chat->photoId = photo->id;
			photo->chat = chat;
		}
	}
	peerUpdated(App::chat(peerId));
}

void HistoryWidget::showPeer(const PeerId &peer, bool force, bool leaveActive) {
	if (App::main()->selectingPeer() && !force) {
		hiderOffered = true;
		App::main()->offerPeer(peer);
		return;
	}
	if (peer) {
		App::main()->dialogsClear();
	}
	if (hist) {
		if (histPeer->id == peer) {
			if (hist->unreadBar) hist->unreadBar->destroy();
			checkUnreadLoaded();
			return activate();
		}
		updateTyping(false);
	}
	if (histPreload.size() && _list) {
		_list->messagesReceived(histPreload);
		histPreload.clear();
	}
	if (hist) {
		hist->draft = _field.getText();
		hist->draftCur = _field.textCursor();
		if (hist->unreadLoaded && _scroll.scrollTop() + 1 <= _scroll.scrollTopMax()) {
			hist->lastWidth = _list->width();
		} else {
			hist->lastWidth = 0;
		}
		hist->lastScrollTop = _scroll.scrollTop();
		if (hist->unreadBar) hist->unreadBar->destroy();
	}

	_scroll.setWidget(0);
	if (_list) _list->deleteLater();
	_list = 0;
	updateTopBarSelection();

	if (leaveActive && histPeer) {
		_activePeer = histPeer;
	} else {
		if (!leaveActive) {
			_activePeer = 0;
		}
		if (hist) {
			App::main()->dlgUpdated(hist);
		}
	}
	histPeer = peer ? App::peer(peer) : 0;
	histOffset = 0;
	histReadRequestId = 0;
	titlePeerText = QString();
	titlePeerTextWidth = 0;
	histRequestsCount = 0;
	histCount = -1;
	histPreload.clear();
	if (histPreloading) MTP::cancel(histPreloading);
	histPreloading = 0;
	hist = 0;
	_histInited = false;
	noSelectingScroll();
	_selCount = 0;
	App::main()->topBar()->showSelected(0);

	App::hoveredItem(0);
	App::pressedItem(0);
	App::hoveredLinkItem(0);
	App::pressedLinkItem(0);
	App::contextItem(0);
	App::mousedItem(0);

	if (peer) {
		App::forgetPhotos();
		App::forgetVideos();
		App::forgetAudios();
		App::forgetDocuments();
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
		if (hist->unreadLoaded) {
			_scroll.show();
		}
		if (hist) {
			App::main()->dlgUpdated(hist);
		}
		histOffset = hist->offset;
		_list = new HistoryList(this, &_scroll, hist);
		_list->hide();
		_scroll.setWidget(_list);
		_list->show();

		checkUnreadLoaded();

		App::main()->peerUpdated(histPeer);
		
		noTypingUpdate = true;
		_field.setPlainText(hist->draft);
		_field.setFocus();
		if (!hist->draft.isEmpty()) {
			_field.setTextCursor(hist->draftCur);
		}
		noTypingUpdate = false;

		connect(&_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));
		connect(&_scroll, SIGNAL(scrolled()), _list, SLOT(onUpdateSelected()));
	} else {
		updateControlsVisibility();
	}
	emit peerShown(histPeer);
	App::main()->topBar()->update();
	update();
}

void HistoryWidget::checkUnreadLoaded(bool checkOnlyShow) {
	if (!hist) return;
	if (hist->unreadLoaded) {
		if (checkOnlyShow && !_scroll.isHidden()) return;
		if (!animating()) {
			if (_scroll.isHidden()) {
				_scroll.show();
				if (!_field.isHidden()) update();
			}
		}
	} else if (checkOnlyShow) {
		return;
	}
	updateListSize(0, true);
	if (!animating()) updateControlsVisibility();
	if (hist->unreadLoaded) {
		if (!_scroll.isHidden() && !_list->isHidden()) {
			onListScroll();
		}
	} else {
		loadMessages();
	}
}

void HistoryWidget::updateControlsVisibility() {
	if (!hist) {
		_scroll.hide();
		_send.hide();
		_field.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_attachEmoji.hide();
		_attachType.hide();
		_emojiPan.hide();
		return;
	}

	if (hist->unreadLoaded) {
		if (!histPeer->chat || !histPeer->asChat()->forbidden) {
			_send.show();
			if (cDefaultAttach() == dbidaPhoto) {
				_attachPhoto.show();
			} else {
				_attachDocument.show();
			}
			_attachEmoji.show();
			if (_field.isHidden()) {
				_field.show();
				update();
			}
		} else {
			_send.hide();
			_attachDocument.hide();
			_attachPhoto.hide();
			_attachEmoji.hide();
			_attachType.hide();
			_emojiPan.hide();
			if (!_field.isHidden()) {
				_field.hide();
				update();
			}
		}
		if (hist->unreadCount && App::wnd()->historyIsActive()) {
			historyWasRead();
		}
	} else {
		loadMessages();
		if (!hist->unreadLoaded) {
			_scroll.hide();
			_send.hide();
			_attachDocument.hide();
			_attachPhoto.hide();
			_attachEmoji.hide();
			_attachType.hide();
			_emojiPan.hide();
			if (!_field.isHidden()) {
				_field.hide();
				update();
			}
		}
	}
}

void HistoryWidget::newUnreadMsg(History *history, MsgId msgId) {
	if (App::wnd()->historyIsActive()) {
		if (hist == history && hist->unreadLoaded) {
			historyWasRead();
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
			}
		} else {
			if (hist != history) {
				App::wnd()->psNotify(history, msgId);
			}
			history->setUnreadCount(history->unreadCount + 1);
		}
	} else {
		if (hist == history && hist->unreadLoaded) {
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
			}
		}
		App::wnd()->psNotify(history, msgId);
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
	if (histReadRequestId || !hist || !force && (!hist->unreadCount || !hist->unreadLoaded)) return;
	hist->inboxRead(true);
	histReadRequestId = MTP::send(MTPmessages_ReadHistory(histPeer->input, MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::partWasRead, histPeer));
}

void HistoryWidget::partWasRead(PeerData *peer, const MTPmessages_AffectedHistory &result) {
	const MTPDmessages_affectedHistory &d(result.c_messages_affectedHistory());
	App::main()->updUpdated(d.vpts.v, 0, 0, d.vseq.v);

	histReadRequestId = 0;
	int32 offset = d.voffset.v;
	if (!MTP::authedId() || offset <= 0) return;

	histReadRequestId = MTP::send(MTPmessages_ReadHistory(peer->input, MTP_int(0), MTP_int(offset)), rpcDone(&HistoryWidget::partWasRead, peer));
}

bool HistoryWidget::messagesFailed(const RPCError &e, mtpRequestId requestId) {
	LOG(("RPC Error: %1 %2: %3").arg(e.code()).arg(e.type()).arg(e.description()));
	if (histPreloading == requestId) {
		histPreloading = 0;
	}
	return true;
}

void HistoryWidget::messagesReceived(const MTPmessages_Messages &messages, mtpRequestId requestId) {
	if (histPreloading == requestId) {
		histPreloading = 0;
	}
	if (!hist) return;

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
		case mtpc_messageForwarded:
			from_id = App::peerFromUser(msg.c_messageForwarded().vfrom_id);
			to_id = App::peerFromMTP(msg.c_messageForwarded().vto_id);
		break;
		case mtpc_messageService:
			from_id = App::peerFromUser(msg.c_messageService().vfrom_id);
			to_id = App::peerFromMTP(msg.c_messageService().vto_id);
		break;
		}
		peer = (to_id == App::peerFromUser(MTP::authedId())) ? from_id : to_id;
	}

	if (peer && peer != histPeer->id) return;

	if (histList) {
		if (!histOffset) {
			addMessagesToFront(*histList);
		} else {
			histPreload = *histList;
		}

		if (histList->size()) {
			histOffset += histList->size();
			histCount = count;
		} else {
			histCount = histOffset;
		}
	} else {
		histCount = histOffset;
		if (!hist->unreadLoaded) {
			hist->setUnreadCount(hist->msgCount);
		}
		checkUnreadLoaded(true);
		return;
	}

	if (histOffset >= histCount && histPreload.size()) {
		addMessagesToFront(histPreload);
		histPreload.clear();
		loadMessages();
	} else if (histPreload.size()) {
		onListScroll();
	} else {
		loadMessages();
	}
}

void HistoryWidget::windowShown() {
	if (hist && !_histInited) {
		checkUnreadLoaded();
	}
	resizeEvent(0);
}

void HistoryWidget::loadMessages() {
	if (!hist) return;
	if (histCount >= 0 && histOffset >= histCount) {
		if (!hist->unreadLoaded) {
			hist->setUnreadCount(hist->msgCount);
		}
		checkUnreadLoaded(true);
		return;
	}

	int32 dh = 0;
	if (histPreload.size()) {
		bool unreadLoaded = hist->unreadLoaded;
		addMessagesToFront(histPreload);
		histPreload.clear();
		checkUnreadLoaded(true);
		if (!unreadLoaded && hist->unreadLoaded) {
			return;
		}
	}
	if (!histPreloading && (!hist->unreadLoaded || _scroll.scrollTop() < 3 * _scroll.height())) {
		int32 loadCount = histOffset ? MessagesPerPage : MessagesFirstLoad;
		histPreloading = MTP::send(MTPmessages_GetHistory(histInputPeer, MTP_int(histOffset), MTP_int(0), MTP_int(loadCount)), rpcDone(&HistoryWidget::messagesReceived), rpcFail(&HistoryWidget::messagesFailed));
		++histRequestsCount;
		if (!hist->unreadLoaded) update();
	} else {
		checkUnreadLoaded(true);
	}
}

void HistoryWidget::onListScroll() {
	App::checkImageCacheSize();

	if (histPreloading || !hist || (_list->isHidden() || _scroll.isHidden() || !App::wnd()->windowHandle()->isVisible()) && hist->unreadLoaded) {
		checkUnreadLoaded(true);
		return;
	}

	if (!hist->unreadLoaded || _scroll.scrollTop() < 3 * _scroll.height()) {
		loadMessages();
	} else {
		checkUnreadLoaded(true);
	}
}

void HistoryWidget::onVisibleChanged() {
	QTimer::singleShot(0, this, SLOT(onListScroll()));
}

QString HistoryWidget::prepareMessage() {
	QString result = _field.getText();
	
	result = result.replace('\t', qsl(" "));

	result = result.replace(" --", QString::fromUtf8(" \xe2\x80\x94"));
	result = result.replace("-- ", QString::fromUtf8("\xe2\x80\x94 "));
	result = result.replace("<<", qsl("\xab"));
    result = result.replace(">>", qsl("\xbb"));

	return (cReplaceEmojis() ? replaceEmojis(result) : result).trimmed();
}

void HistoryWidget::onSend() {
	if (!hist) return;

	QString text = prepareMessage();
	if (!text.isEmpty()) {
		MsgId newId = clientMsgId();
		uint64 randomId = MTP::nonce<uint64>();
	
		App::historyRegRandom(randomId, newId);

		MTPstring msgText(MTP_string(text));
		hist->addToBack(MTP_message(MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(histPeer->id), MTP_bool(true), MTP_bool(true), MTP_int(unixtime()), msgText, MTP_messageMediaEmpty()));
		App::main()->historyToDown(hist);
		App::main()->dialogsToUp();
		peerMessagesUpdated();

		MTP::send(MTPmessages_SendMessage(histInputPeer, msgText, MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentDataReceived, randomId));
		_field.setPlainText("");
	}
	_field.setFocus();
}

mtpRequestId HistoryWidget::onForward(const PeerId &peer, bool forwardSelected) {
	if (!_list) return 0;

	HistoryItemSet toForward;
	if (forwardSelected) {
		_list->fillSelectedItems(toForward, false);
	} else if (App::contextItem()) {
		toForward.insert(0, App::contextItem());
	}
	if (toForward.isEmpty()) return 0;

	if (toForward.size() == 1) {
		App::main()->showPeer(peer, false, true);
		if (!hist) return 0;

		HistoryItem *item = toForward.cbegin().value();
		uint64 randomId = MTP::nonce<uint64>();
		HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
		HistoryServiceMsg *srv = dynamic_cast<HistoryServiceMsg*>(item);
		MsgId newId = 0;
	
		if (item->id > 0 && msg) {
			newId = clientMsgId();
			hist->addToBackForwarded(newId, msg);
			MTP::send(MTPmessages_ForwardMessage(histPeer->input, MTP_int(item->id), MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentFullDataReceived, randomId));
		} else if (srv || msg && msg->selectedText(FullItemSel).isEmpty()) {
	//		newId = clientMsgId();
	//		MTP::send(MTPmessages_ForwardMessage(histPeer->input, MTP_int(item->id), MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentFullDataReceived, randomId));
		} else if (msg) {
			newId = clientMsgId();

			MTPstring msgText(MTP_string(msg->selectedText(FullItemSel)));

			hist->addToBack(MTP_message(MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(histPeer->id), MTP_bool(true), MTP_bool(true), MTP_int(unixtime()), msgText, MTP_messageMediaEmpty()));
			MTP::send(MTPmessages_SendMessage(histPeer->input, msgText, MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentDataReceived, randomId));
		}
		if (newId) {
			App::historyRegRandom(randomId, newId);
			App::main()->historyToDown(hist);
			App::main()->dialogsToUp();
			peerMessagesUpdated();
			onClearSelected();
		}
		return 0;
	}

	PeerData *toPeer = App::peerLoaded(peer);
	if (!toPeer) return 0;

	QVector<MTPint> ids;
	ids.reserve(toForward.size());
	for (HistoryItemSet::const_iterator i = toForward.cbegin(), e = toForward.cend(); i != e; ++i) {
		ids.push_back(MTP_int(i.value()->id));
	}
	return MTP::send(MTPmessages_ForwardMessages(toPeer->input, MTP_vector<MTPint>(ids)), App::main()->rpcDone(&MainWidget::forwardDone, peer));
}

void HistoryWidget::onShareContact(const PeerId &peer, UserData *contact) {
	if (!contact || contact->phone.isEmpty()) return;

	App::main()->showPeer(peer, false, true);
	if (!hist) return;

	uint64 randomId = MTP::nonce<uint64>();
	MsgId newId = clientMsgId();

	hist->addToBack(MTP_message(MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(histPeer->id), MTP_bool(true), MTP_bool(true), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaContact(MTP_string(contact->phone), MTP_string(contact->firstName), MTP_string(contact->lastName), MTP_int(int32(contact->id & 0xFFFFFFFF)))));
	
	MTP::send(MTPmessages_SendMedia(histPeer->input, MTP_inputMediaContact(MTP_string(contact->phone), MTP_string(contact->firstName), MTP_string(contact->lastName)), MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentFullDataReceived, randomId));

	App::historyRegRandom(randomId, newId);
	App::main()->historyToDown(hist);
	App::main()->dialogsToUp();
	peerMessagesUpdated();
}

PeerData *HistoryWidget::peer() const {
	return histPeer;
}

PeerData *HistoryWidget::activePeer() const {
	return histPeer ? histPeer : _activePeer;
}

void HistoryWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back) {
	_bgAnimCache = bgAnimCache;
	_bgAnimTopBarCache = bgAnimTopBarCache;
	_animCache = grab(rect());
	App::main()->topBar()->showAll();
	_animTopBarCache = App::main()->topBar()->grab(QRect(0, 0, width(), st::topBarHeight));
	App::main()->topBar()->hideAll();
	_scroll.hide();
	_attachDocument.hide();
	_attachPhoto.hide();
	_attachEmoji.hide();
	_field.hide();
	_send.hide();
	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);
	anim::start(this);
	App::main()->topBar()->update();
}

bool HistoryWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		res = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();
		_bgAnimCache = _animCache = _animTopBarCache = _bgAnimTopBarCache = QPixmap();
		App::main()->topBar()->showAll();
		updateControlsVisibility();
		if (hist && hist->unreadLoaded) {
			_scroll.show();
			if (hist->lastScrollTop == History::ScrollMax) {
				_scroll.scrollToY(hist->lastScrollTop);
			}
			onListScroll();
		}
		activate();
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
	if (!animating()) return;
	anim::stop(this);
}

void HistoryWidget::onPhotoSelect() {
	if (!hist) return;

	_attachDocument.clearState();
	_attachDocument.hide();
	_attachPhoto.show();
	_attachType.fastHide();

	if (cDefaultAttach() != dbidaPhoto) {
		cSetDefaultAttach(dbidaPhoto);
		App::writeUserConfig();
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
		App::writeUserConfig();
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
	_attachDrag = DragStateNone;
	updateDragAreas();
}

void HistoryWidget::leaveEvent(QEvent *e) {
	_attachDrag = DragStateNone;
	updateDragAreas();
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
	_attachDrag = DragStateNone;
	updateDragAreas();
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
		_attachDragDocument.setText(lang(lng_drag_files_here), lang(lng_drag_to_send_documents));
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

void HistoryWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (animating()) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimTopBarCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animTopBarCache);
		return;
	}

	if (!hist) return;

	QRect rectForName(st::topBarForwardPadding.left(), st::topBarForwardPadding.top(), width() - decreaseWidth - st::topBarForwardPadding.left() - st::topBarForwardPadding.right(), st::msgNameFont->height);
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

	if (!decreaseWidth) {
		p.setOpacity(st::topBarForwardAlpha + (1 - st::topBarForwardAlpha) * over);
		p.drawPixmap(QPoint(width() - (st::topBarForwardPadding.right() + st::topBarForwardImg.width()) / 2, (st::topBarHeight - st::topBarForwardImg.height()) / 2), App::sprite(), st::topBarForwardImg);
	}
}

void HistoryWidget::topBarClick() {
	if (hist) App::main()->showPeerProfile(histPeer);
}

void HistoryWidget::updateOnlineDisplay(int32 x, int32 w) {
	if (!hist) return;

	QString text;
	int32 t = unixtime();
	if (histPeer->chat) {
		ChatData *chat = histPeer->asChat();
		if (chat->forbidden || chat->count <= 0) {
			text = lang(lng_chat_no_members);
		} else if (chat->participants.isEmpty()) {
			text = titlePeerText.isEmpty() ? lang(lng_chat_members).arg(chat->count) : titlePeerText;
		} else {
			int32 onlineCount = 0;
			for (ChatData::Participants::const_iterator i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
				if (i.key()->onlineTill > t) {
					++onlineCount;
				}
			}
			if (onlineCount) {
				text = lang(lng_chat_members_online).arg(chat->participants.size()).arg(onlineCount);
			} else {
				text = lang(lng_chat_members).arg(chat->participants.size());
			}
		}
	} else {
		text = App::onlineText(histPeer->asUser()->onlineTill, t);
	}
	if (titlePeerText != text) {
		titlePeerText = text;
		titlePeerTextWidth = st::dlgHistFont->m.width(titlePeerText);
		App::main()->topBar()->update();
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
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key()->onlineTill, t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else {
		minIn = App::onlineWillChangeIn(histPeer->asUser()->onlineTill, t);
	}
	App::main()->updateOnlineDisplayIn(minIn * 1000);
}

void HistoryWidget::onFieldResize() {
	_field.move(_attachDocument.x() + _attachDocument.width(), height() - _field.height() - st::sendPadding);
	updateListSize();
}

void HistoryWidget::onFieldFocused() {
	if (_list) _list->clearSelectedItems(true);
}

void HistoryWidget::uploadImage(const QImage &img) {
	if (!hist || confirmImageId) return;

	App::wnd()->activateWindow();
	confirmImageId = imageLoader.append(img, histPeer->id, ToPreparePhoto);
}

void HistoryWidget::uploadMedias(const QStringList &files, ToPrepareMediaType type) {
	if (!hist) return;

	App::wnd()->activateWindow();
	imageLoader.append(files, histPeer->id, type);
}

void HistoryWidget::uploadMedia(const QByteArray &fileContent, ToPrepareMediaType type) {
	if (!hist) return;

	App::wnd()->activateWindow();
	imageLoader.append(fileContent, histPeer->id, type);
}

void HistoryWidget::onPhotoReady() {
	QMutexLocker lock(imageLoader.readyMutex());
	ReadyLocalMedias &list(imageLoader.readyList());

	for (ReadyLocalMedias::const_iterator i = list.cbegin(), e = list.cend(); i != e; ++i) {
		if (i->id == confirmImageId) {
			App::wnd()->showLayer(new PhotoSendBox(*i));
		} else {
			confirmSendImage(*i);
		}
	}
	list.clear();
}

void HistoryWidget::onPhotoFailed(quint64 id) {
	id = id;
}

void HistoryWidget::confirmSendImage(const ReadyLocalMedia &img) {
	if (img.id == confirmImageId) {
		confirmImageId = 0;
	}
	MsgId newId = clientMsgId();

	connect(App::uploader(), SIGNAL(photoReady(MsgId, const MTPInputFile &)), this, SLOT(onPhotoUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentReady(MsgId, const MTPInputFile &)), this, SLOT(onDocumentUploaded(MsgId, const MTPInputFile &)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(thumbDocumentReady(MsgId, const MTPInputFile &, const MTPInputFile &)), this, SLOT(onThumbDocumentUploaded(MsgId, const MTPInputFile &, const MTPInputFile &)), Qt::UniqueConnection);
//	connect(App::uploader(), SIGNAL(photoProgress(MsgId)), this, SLOT(onPhotoProgress(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentProgress(MsgId)), this, SLOT(onDocumentProgress(MsgId)), Qt::UniqueConnection);
//	connect(App::uploader(), SIGNAL(photoFailed(MsgId)), this, SLOT(onPhotoFailed(MsgId)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentFailed(MsgId)), this, SLOT(onDocumentFailed(MsgId)), Qt::UniqueConnection);

	App::uploader()->uploadMedia(newId, img);

	if (img.type == ToPreparePhoto) {
		App::history(img.peer)->addToBack(MTP_message(MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTP_bool(true), MTP_bool(true), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaPhoto(img.photo)));
	} else if (img.type == ToPrepareDocument) {
		App::history(img.peer)->addToBack(MTP_message(MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(img.peer), MTP_bool(true), MTP_bool(true), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(img.document)));
	}

	if (hist && histPeer && img.peer == histPeer->id) App::main()->historyToDown(hist);
	App::main()->dialogsToUp();
	peerMessagesUpdated(img.peer);
}

void HistoryWidget::cancelSendImage() {
	confirmImageId = 0;
}

void HistoryWidget::onPhotoUploaded(MsgId newId, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		uint64 randomId = MTP::nonce<uint64>();
		App::historyRegRandom(randomId, newId);
		MTP::send(MTPmessages_SendMedia(item->history()->peer->input, MTP_inputMediaUploadedPhoto(file), MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentFullDataReceived, randomId));
	}
}

void HistoryWidget::onDocumentUploaded(MsgId newId, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::histItemById(newId));
	if (item) {
		HistoryDocument *media = dynamic_cast<HistoryDocument*>(item->getMedia());
		if (media) {
			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			DocumentData *document = media->document();
			MTP::send(MTPmessages_SendMedia(item->history()->peer->input, MTP_inputMediaUploadedDocument(file, MTP_string(document->name), MTP_string(document->mime)), MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentFullDataReceived, randomId));
		}
	}
}

void HistoryWidget::onThumbDocumentUploaded(MsgId newId, const MTPInputFile &file, const MTPInputFile &thumb) {
	if (!MTP::authedId()) return;
	HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::histItemById(newId));
	if (item) {
		HistoryDocument *media = dynamic_cast<HistoryDocument*>(item->getMedia());
		if (media) {
			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			DocumentData *document = media->document();
			MTP::send(MTPmessages_SendMedia(item->history()->peer->input, MTP_inputMediaUploadedThumbDocument(file, thumb, MTP_string(document->name), MTP_string(document->mime)), MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentFullDataReceived, randomId));
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

void HistoryWidget::onDocumentFailed(MsgId newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		msgUpdated(item->history()->peer->id, item);
	}
}

void HistoryWidget::peerMessagesUpdated(PeerId peer) {
	if (histPeer && _list && peer == histPeer->id) {
		updateListSize();
	}
}

void HistoryWidget::peerMessagesUpdated() {
	if (_list) updateListSize();
}

void HistoryWidget::msgUpdated(PeerId peer, HistoryItem *msg) {
	if (histPeer && _list && peer == histPeer->id) {
		_list->updateMsg(msg);
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	_attachDocument.move(0, height() - _attachDocument.height());
	_attachPhoto.move(_attachDocument.x(), _attachDocument.y());

	_field.move(_attachDocument.x() + _attachDocument.width(), height() - _field.height() - st::sendPadding);

	updateListSize();

	_field.resize(width() - _send.width() - _attachDocument.width() - _attachEmoji.width(), _field.height());
	_attachEmoji.move(_field.x() + _field.width(), height() - _attachEmoji.height());
	_send.move(width() - _send.width(), _attachDocument.y());

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

void HistoryWidget::updateListSize(int32 addToY, bool initial) {
	if (!hist || !_histInited && !initial) return;

	if (!App::wnd()->isVisible()) return; // scrollTopMax etc are not working after recountHeight()

	int32 newScrollHeight = height() - (hist->unreadLoaded && (!histPeer->chat || !histPeer->asChat()->forbidden) ? (_field.height() + 2 * st::sendPadding) : 0);
	bool wasAtBottom = _scroll.scrollTop() + 1 > _scroll.scrollTopMax(), needResize = _scroll.width() != width() || _scroll.height() != newScrollHeight;
	if (needResize) {
		_scroll.resize(width(), newScrollHeight);
	}

	if (!initial) {
		hist->lastScrollTop = _scroll.scrollTop();
	}
	int32 newSt = _list->recountHeight();
	bool washidden = _scroll.isHidden();
	if (washidden) {
		_scroll.show();
	}
	_list->updateSize();
	if (washidden) {
		_scroll.hide();
	}
	if (!hist->unreadLoaded) return;

	if (!initial && !wasAtBottom) {
		_scroll.scrollToY(newSt + addToY);
		return;
	}
	if (!hist->unreadLoaded) return;

	if (initial) {
		_histInited = true;
	}

	int32 toY = History::ScrollMax;
	if (initial && hist->unreadBar) {
		toY = hist->unreadBar->y + hist->unreadBar->block()->y;
	} else if (hist->showFrom) {
		toY = hist->showFrom->y + hist->showFrom->block()->y;
		if (toY < _scroll.scrollTopMax() + st::unreadBarHeight) {
			hist->addUnreadBar();
			if (hist->unreadBar) {
				return updateListSize(0, true);
			}
		}
	} else if (initial && hist->lastWidth) {
		toY = newSt;
		hist->lastWidth = 0;
	} else {
		int blabla = 0;
	}
	_scroll.scrollToY(toY);
}

void HistoryWidget::addMessagesToFront(const QVector<MTPMessage> &messages) {
	int32 oldH = hist->height;
	_list->messagesReceived(messages);
	updateListSize(hist->height - oldH);
	checkUnreadLoaded(true);
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!hist) return;

	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_PageDown) {
		if (e->modifiers() & Qt::ControlModifier) {
			PeerData *after = App::main()->peerAfter(histPeer);
			if (after) App::main()->showPeer(after->id);
		} else {
			_scroll.scrollToY(_scroll.scrollTop() + _scroll.height());
		}
	} else if (e->key() == Qt::Key_PageUp) {
		if (e->modifiers() & Qt::ControlModifier) {
			PeerData *before = App::main()->peerBefore(histPeer);
			if (before) App::main()->showPeer(before->id);
		} else {
			_scroll.scrollToY(_scroll.scrollTop() - _scroll.height());
		}
	} else if (e->key() == Qt::Key_Down) {
		_scroll.scrollToY(_scroll.scrollTop() + _scroll.height() / 10);
	} else if (e->key() == Qt::Key_Up) {
		_scroll.scrollToY(_scroll.scrollTop() - _scroll.height() / 10);
	} else {
		e->ignore();
	}
}

void HistoryWidget::onFieldTabbed() {
	QString v = _field.getText(), t = supportTemplate(v.trimmed());
	if (!t.isEmpty()) {
		if (t.indexOf(qsl("img:")) == 0) {
			QImage img(cWorkingDir() + t.mid(4).trimmed());
			if (!img.isNull()) {
				_field.setPlainText(QString());
				uploadImage(img);
			}
		} else {
			_field.setPlainText(t);
			QTextCursor c = _field.textCursor();
			c.movePosition(QTextCursor::End);
			_field.setTextCursor(c);
		}
	}
}

void HistoryWidget::peerUpdated(PeerData *data) {
	if (data && data == histPeer) {
		updateListSize();
		if (!animating()) updateControlsVisibility();
		if (data->chat && data->asChat()->count > 0 && data->asChat()->participants.isEmpty() && (!loadingRequestId || loadingChatId != data->id)) {
			loadingChatId = data->id;
			loadingRequestId = MTP::send(MTPmessages_GetFullChat(App::peerToMTP(data->id).c_peerChat().vchat_id), rpcDone(&HistoryWidget::chatLoaded));
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

	HistoryItemSet sel;
	_list->fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	App::main()->deleteLayer(sel.size());
}

void HistoryWidget::onDeleteSelectedSure() {
	if (!_list) return;

	HistoryItemSet sel;
	_list->fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	QVector<MTPint> ids;
	for (HistoryItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		if (i.value()->id > 0) {
			ids.push_back(MTP_int(i.value()->id));
		}
	}

	if (!ids.isEmpty()) {
		MTP::send(MTPmessages_DeleteMessages(MTP_vector<MTPint>(ids)));
	}

	onClearSelected();
	for (HistoryItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		i.value()->destroy();
	}
	App::wnd()->hideLayer();
}

void HistoryWidget::onDeleteContextSure() {
	HistoryItem *item = App::contextItem();
	if (!item || item->itemType() != HistoryItem::MsgType) {
		return;
	}

	if (item->id > 0) {
		MTP::send(MTPmessages_DeleteMessages(MTP_vector<MTPint>(QVector<MTPint>(1, MTP_int(item->id)))));
	}
	item->destroy();
	App::wnd()->hideLayer();
}

void HistoryWidget::onClearSelected() {
	if (_list) _list->clearSelectedItems();
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
	if (_selCount) {
		_list->setFocus();
	} else {
		_field.setFocus();
	}
	App::main()->topBar()->update();
	update();
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	if (animating()) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
		return;
	}
	if (cCatsAndDogs()) {
		int32 i_from = r.left() / bg.width(), i_to = (r.left() + r.width() - 1) / bg.width() + 1;
		int32 j_from = r.top() / bg.height(), j_to = (r.top() + r.height() - 1) / bg.height() + 1;
		for (int32 i = i_from; i < i_to; ++i) {
			for (int32 j = j_from; j < j_to; ++j) {
				p.drawPixmap(i * bg.width(), j * bg.height(), bg);
			}
		}
	} else {
		p.fillRect(r, st::historyBG->b);
	}
	if (_list) {
		if (!_scroll.isHidden()) {
			if (!_field.isHidden()) {
				p.fillRect(0, _field.y() - st::sendPadding, width(), _field.height() + 2 * st::sendPadding, st::taMsgField.bgColor->b);
			}
		} else {
			QPoint dogPos((width() - st::msgDogImg.width()) / 2, ((height() - _field.height() - 2 * st::sendPadding - st::msgDogImg.height()) * 4) / 9);
			p.drawPixmap(dogPos, App::sprite(), st::msgDogImg);

			int32 pointsCount = 8, w = pointsCount * (st::introPointWidth + 2 * st::introPointDelta), h = st::introPointHeight;
			int32 pointsLeft = (width() - w) / 2 + st::introPointDelta - st::introPointLeft, pointsTop = dogPos.y() + (st::msgDogImg.height() * 6) / 5;

			int32 curPoint = histRequestsCount % pointsCount;

			p.setOpacity(st::introPointHoverAlpha);
			p.fillRect(pointsLeft + curPoint * (st::introPointWidth + 2 * st::introPointDelta), pointsTop, st::introPointHoverWidth, st::introPointHoverHeight, st::introPointHoverColor->b);

			// points
			p.setOpacity(st::introPointAlpha);
			int x = pointsLeft + st::introPointLeft;
			for (uint32 i = 0; i < pointsCount; ++i) {
				p.fillRect(x, pointsTop + st::introPointTop, st::introPointWidth, st::introPointHeight, st::introPointColor->b);
				x += (st::introPointWidth + 2 * st::introPointDelta);
			}
		}
	} else {
		style::font font(st::msgServiceFont);
		int32 w = font->m.width(lang(lng_willbe_history)) + st::msgPadding.left() + st::msgPadding.right(), h = font->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + 2;
		QRect tr((width() - w) / 2, (height() - _field.height() - 2 * st::sendPadding - h) / 2, w, h);
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgServiceBG->b);
		p.drawRoundedRect(tr, st::msgServiceRadius, st::msgServiceRadius);

		p.setPen(st::msgServiceColor->p);
		p.setFont(font->f);
		p.drawText(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top() + 1 + font->ascent, lang(lng_willbe_history));
	}
}

bool HistoryWidget::getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const {
	if (_list && _list->getPhotoCoords(photo, x, y, w)) {
		x += _list->x();
		y += _list->y();
		return true;
	}
	return false;
}

bool HistoryWidget::getVideoCoords(VideoData *video, int32 &x, int32 &y, int32 &w) const {
	if (_list && _list->getVideoCoords(video, x, y, w)) {
		x += _list->x();
		y += _list->y();
		return true;
	}
	return false;
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
