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
#include "history/history_inner_widget.h"

#include <rpl/merge.h>
#include "styles/style_history.h"
#include "core/file_utilities.h"
#include "history/history_message.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers.h"
#include "history/history_widget.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "messenger.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"

namespace {

constexpr auto kScrollDateHideTimeout = 1000;

class DateClickHandler : public ClickHandler {
public:
	DateClickHandler(PeerData *peer, QDate date) : _peer(peer), _date(date) {
	}

	void setDate(QDate date) {
		_date = date;
	}

	void onClick(Qt::MouseButton) const override {
		App::wnd()->controller()->showJumpToDate(_peer, _date);
	}

private:
	PeerData *_peer = nullptr;
	QDate _date;

};

// Helper binary search for an item in a list that is not completely
// above the given top of the visible area or below the given bottom of the visible area
// is applied once for blocks list in a history and once for items list in the found block.
template <bool TopToBottom, typename T>
int BinarySearchBlocksOrItems(const T &list, int edge) {
	// static_cast to work around GCC bug #78693
	auto start = 0, end = static_cast<int>(list.size());
	while (end - start > 1) {
		auto middle = (start + end) / 2;
		auto top = list[middle]->y();
		auto chooseLeft = (TopToBottom ? (top <= edge) : (top < edge));
		if (chooseLeft) {
			start = middle;
		} else {
			end = middle;
		}
	}
	return start;
}

} // namespace

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

HistoryInner::HistoryInner(
	HistoryWidget *historyWidget,
	not_null<Window::Controller*> controller,
	Ui::ScrollArea *scroll,
	History *history)
: RpWidget(nullptr)
, _controller(controller)
, _peer(history->peer)
, _migrated(history->migrateFrom())
, _history(history)
, _widget(historyWidget)
, _scroll(scroll)
, _scrollDateCheck([this] { onScrollDateCheck(); }) {
	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));

	_trippleClickTimer.setSingleShot(true);

	connect(&_scrollDateHideTimer, SIGNAL(timeout()), this, SLOT(onScrollDateHideByTimer()));

	notifyIsBotChanged();

	setMouseTracking(true);
	subscribe(_controller->gifPauseLevelChanged(), [this] {
		if (!_controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any)) {
			update();
		}
	});
	subscribe(_controller->window()->dragFinished(), [this] {
		mouseActionUpdate(QCursor::pos());
	});
	Auth().data().itemRemoved()
		| rpl::start_with_next(
			[this](auto item) { itemRemoved(item); },
			lifetime());
	rpl::merge(
		Auth().data().historyUnloaded(),
		Auth().data().historyCleared())
		| rpl::filter([this](not_null<const History*> history) {
			return (_history == history);
		})
		| rpl::start_with_next([this] {
			mouseActionCancel();
		}, lifetime());
}

void HistoryInner::messagesReceived(PeerData *peer, const QVector<MTPMessage> &messages) {
	if (_history && _history->peer == peer) {
		_history->addOlderSlice(messages);
	} else if (_migrated && _migrated->peer == peer) {
		bool newLoaded = (_migrated && _migrated->isEmpty() && !_history->isEmpty());
		_migrated->addOlderSlice(messages);
		if (newLoaded) {
			_migrated->addNewerSlice(QVector<MTPMessage>());
		}
	}
}

void HistoryInner::messagesReceivedDown(PeerData *peer, const QVector<MTPMessage> &messages) {
	if (_history && _history->peer == peer) {
		bool oldLoaded = (_migrated && _history->isEmpty() && !_migrated->isEmpty());
		_history->addNewerSlice(messages);
		if (oldLoaded) {
			_history->addOlderSlice(QVector<MTPMessage>());
		}
	} else if (_migrated && _migrated->peer == peer) {
		_migrated->addNewerSlice(messages);
	}
}

void HistoryInner::repaintItem(const HistoryItem *item) {
	if (!item || item->detached() || !_history) return;
	int32 msgy = itemTop(item);
	if (msgy >= 0) {
		update(0, msgy, width(), item->height());
	}
}

template <bool TopToBottom, typename Method>
void HistoryInner::enumerateItemsInHistory(History *history, int historytop, Method method) {
	// No displayed messages in this history.
	if (historytop < 0 || history->isEmpty()) {
		return;
	}
	if (_visibleAreaBottom <= historytop || historytop + history->height <= _visibleAreaTop) {
		return;
	}

	auto searchEdge = TopToBottom ? _visibleAreaTop : _visibleAreaBottom;

	// Binary search for blockIndex of the first block that is not completely below the visible area.
	auto blockIndex = BinarySearchBlocksOrItems<TopToBottom>(history->blocks, searchEdge - historytop);

	// Binary search for itemIndex of the first item that is not completely below the visible area.
	auto block = history->blocks.at(blockIndex);
	auto blocktop = historytop + block->y();
	auto blockbottom = blocktop + block->height();
	auto itemIndex = BinarySearchBlocksOrItems<TopToBottom>(block->items, searchEdge - blocktop);

	while (true) {
		while (true) {
			auto item = block->items.at(itemIndex);
			auto itemtop = blocktop + item->y();
			auto itembottom = itemtop + item->height();

			// Binary search should've skipped all the items that are above / below the visible area.
			if (TopToBottom) {
				Assert(itembottom > _visibleAreaTop);
			} else {
				Assert(itemtop < _visibleAreaBottom);
			}

			if (!method(item, itemtop, itembottom)) {
				return;
			}

			// Skip all the items that are below / above the visible area.
			if (TopToBottom) {
				if (itembottom >= _visibleAreaBottom) {
					return;
				}
			} else {
				if (itemtop <= _visibleAreaTop) {
					return;
				}
			}

			if (TopToBottom) {
				if (++itemIndex >= block->items.size()) {
					break;
				}
			} else {
				if (--itemIndex < 0) {
					break;
				}
			}
		}

		// Skip all the rest blocks that are below / above the visible area.
		if (TopToBottom) {
			if (blockbottom >= _visibleAreaBottom) {
				return;
			}
		} else {
			if (blocktop <= _visibleAreaTop) {
				return;
			}
		}

		if (TopToBottom) {
			if (++blockIndex >= history->blocks.size()) {
				return;
			}
		} else {
			if (--blockIndex < 0) {
				return;
			}
		}
		block = history->blocks[blockIndex];
		blocktop = historytop + block->y();
		blockbottom = blocktop + block->height();
		if (TopToBottom) {
			itemIndex = 0;
		} else {
			itemIndex = block->items.size() - 1;
		}
	}
}

template <typename Method>
void HistoryInner::enumerateUserpics(Method method) {
	if ((!_history || !_history->canHaveFromPhotos()) && (!_migrated || !_migrated->canHaveFromPhotos())) {
		return;
	}

	// Find and remember the top of an attached messages pack
	// -1 means we didn't find an attached to next message yet.
	int lowestAttachedItemTop = -1;

	auto userpicCallback = [this, &lowestAttachedItemTop, &method](not_null<HistoryItem*> item, int itemtop, int itembottom) {
		// Skip all service messages.
		auto message = item->toHistoryMessage();
		if (!message) return true;

		if (lowestAttachedItemTop < 0 && message->isAttachedToNext()) {
			lowestAttachedItemTop = itemtop + message->marginTop();
		}

		// Call method on a userpic for all messages that have it and for those who are not showing it
		// because of their attachment to the next message if they are bottom-most visible.
		if (message->displayFromPhoto() || (message->hasFromPhoto() && itembottom >= _visibleAreaBottom)) {
			if (lowestAttachedItemTop < 0) {
				lowestAttachedItemTop = itemtop + message->marginTop();
			}
			// Attach userpic to the bottom of the visible area with the same margin as the last message.
			auto userpicMinBottomSkip = st::historyPaddingBottom + st::msgMargin.bottom();
			auto userpicBottom = qMin(itembottom - message->marginBottom(), _visibleAreaBottom - userpicMinBottomSkip);

			// Do not let the userpic go above the attached messages pack top line.
			userpicBottom = qMax(userpicBottom, lowestAttachedItemTop + st::msgPhotoSize);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(message, userpicBottom - st::msgPhotoSize)) {
				return false;
			}
		}

		// Forget the found top of the pack, search for the next one from scratch.
		if (!message->isAttachedToNext()) {
			lowestAttachedItemTop = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::TopToBottom>(userpicCallback);
}

template <typename Method>
void HistoryInner::enumerateDates(Method method) {
	auto drawtop = historyDrawTop();

	// Find and remember the bottom of an single-day messages pack
	// -1 means we didn't find a same-day with previous message yet.
	auto lowestInOneDayItemBottom = -1;

	auto dateCallback = [this, &lowestInOneDayItemBottom, &method, drawtop](not_null<HistoryItem*> item, int itemtop, int itembottom) {
		if (lowestInOneDayItemBottom < 0 && item->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = itembottom - item->marginBottom();
		}

		// Call method on a date for all messages that have it and for those who are not showing it
		// because they are in a one day together with the previous message if they are top-most visible.
		if (item->displayDate() || (!item->isEmpty() && itemtop <= _visibleAreaTop)) {
			// skip the date of history migrate item if it will be in migrated
			if (itemtop < drawtop && item->history() == _history) {
				if (itemtop > _visibleAreaTop) {
					// Previous item (from the _migrated history) is drawing date now.
					return false;
				} else if (item == _history->blocks.front()->items.front() && item->isGroupMigrate()
					&& _migrated->blocks.back()->items.back()->isGroupMigrate()) {
					// This item is completely invisible and should be completely ignored.
					return false;
				}
			}

			if (lowestInOneDayItemBottom < 0) {
				lowestInOneDayItemBottom = itembottom - item->marginBottom();
			}
			// Attach date to the top of the visible area with the same margin as it has in service message.
			int dateTop = qMax(itemtop, _visibleAreaTop) + st::msgServiceMargin.top();

			// Do not let the date go below the single-day messages pack bottom line.
			int dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			dateTop = qMin(dateTop, lowestInOneDayItemBottom - dateHeight);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(item, itemtop, dateTop)) {
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

void HistoryInner::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	if (hasPendingResizedItems()) {
		return;
	}

	Painter p(this);
	auto clip = e->rect();
	auto ms = getms();

	bool historyDisplayedEmpty = (_history->isDisplayedEmpty() && (!_migrated || _migrated->isDisplayedEmpty()));
	bool noHistoryDisplayed = _firstLoading || historyDisplayedEmpty;
	if (!_firstLoading && _botAbout && !_botAbout->info->text.isEmpty() && _botAbout->height > 0) {
		if (clip.y() < _botAbout->rect.y() + _botAbout->rect.height() && clip.y() + clip.height() > _botAbout->rect.y()) {
			p.setTextPalette(st::inTextPalette);
			App::roundRect(p, _botAbout->rect, st::msgInBg, MessageInCorners, &st::msgInShadow);

			p.setFont(st::msgNameFont);
			p.setPen(st::dialogsNameFg);
			p.drawText(_botAbout->rect.left() + st::msgPadding.left(), _botAbout->rect.top() + st::msgPadding.top() + st::msgNameFont->ascent, lang(lng_bot_description));

			p.setPen(st::historyTextInFg);
			_botAbout->info->text.draw(p, _botAbout->rect.left() + st::msgPadding.left(), _botAbout->rect.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip, _botAbout->width);

			p.restoreTextPalette();
		}
	} else if (noHistoryDisplayed) {
		HistoryLayout::paintEmpty(p, width(), height());
	}
	if (!noHistoryDisplayed) {
		auto readMentions = HistoryItemsMap();

		adjustCurrent(clip.top());

		auto selEnd = _selected.cend();
		auto hasSel = !_selected.empty();

		auto drawToY = clip.y() + clip.height();

		auto selfromy = itemTop(_dragSelFrom);
		auto seltoy = itemTop(_dragSelTo);
		if (selfromy < 0 || seltoy < 0) {
			selfromy = seltoy = -1;
		} else {
			seltoy += _dragSelTo->height();
		}

		auto mtop = migratedTop();
		auto htop = historyTop();
		auto hdrawtop = historyDrawTop();
		if (mtop >= 0) {
			auto iBlock = (_curHistory == _migrated ? _curBlock : (_migrated->blocks.size() - 1));
			auto block = _migrated->blocks[iBlock];
			auto iItem = (_curHistory == _migrated ? _curItem : (block->items.size() - 1));
			auto item = block->items[iItem];

			auto y = mtop + block->y() + item->y();
			p.save();
			p.translate(0, y);
			if (clip.y() < y + item->height()) while (y < drawToY) {
				TextSelection sel;
				if (y >= selfromy && y < seltoy) {
					if (_dragSelecting && !item->serviceMsg() && item->id > 0) {
						sel = FullSelection;
					}
				} else if (hasSel) {
					auto i = _selected.find(item);
					if (i != selEnd) {
						sel = i->second;
					}
				}
				item->draw(p, clip.translated(0, -y), sel, ms);

				if (item->hasViews()) {
					App::main()->scheduleViewIncrement(item);
				}
				if (item->mentionsMe() && item->isMediaUnread()) {
					readMentions.insert(item);
					_widget->enqueueMessageHighlight(item);
				}

				int32 h = item->height();
				p.translate(0, h);
				y += h;

				++iItem;
				if (iItem == block->items.size()) {
					iItem = 0;
					++iBlock;
					if (iBlock == _migrated->blocks.size()) {
						break;
					}
					block = _migrated->blocks[iBlock];
				}
				item = block->items[iItem];
			}
			p.restore();
		}
		if (htop >= 0) {
			auto iBlock = (_curHistory == _history ? _curBlock : 0);
			auto block = _history->blocks[iBlock];
			auto iItem = (_curHistory == _history ? _curItem : 0);
			auto item = block->items[iItem];

			auto historyRect = clip.intersected(QRect(0, hdrawtop, width(), clip.top() + clip.height()));
			auto y = htop + block->y() + item->y();
			p.save();
			p.translate(0, y);
			while (y < drawToY) {
				auto h = item->height();
				if (historyRect.y() < y + h && hdrawtop < y + h) {
					TextSelection sel;
					if (y >= selfromy && y < seltoy) {
						if (_dragSelecting && !item->serviceMsg() && item->id > 0) {
							sel = FullSelection;
						}
					} else if (hasSel) {
						auto i = _selected.find(item);
						if (i != selEnd) {
							sel = i->second;
						}
					}
					item->draw(p, historyRect.translated(0, -y), sel, ms);

					if (item->hasViews()) {
						App::main()->scheduleViewIncrement(item);
					}
					if (item->mentionsMe() && item->isMediaUnread()) {
						readMentions.insert(item);
						_widget->enqueueMessageHighlight(item);
					}
				}
				p.translate(0, h);
				y += h;

				++iItem;
				if (iItem == block->items.size()) {
					iItem = 0;
					++iBlock;
					if (iBlock == _history->blocks.size()) {
						break;
					}
					block = _history->blocks[iBlock];
				}
				item = block->items[iItem];
			}
			p.restore();
		}

		if (!readMentions.empty() && App::wnd()->doWeReadMentions()) {
			App::main()->mediaMarkRead(readMentions);
		}

		if (mtop >= 0 || htop >= 0) {
			enumerateUserpics([&p, &clip](not_null<HistoryMessage*> message, int userpicTop) {
				// stop the enumeration if the userpic is below the painted rect
				if (userpicTop >= clip.top() + clip.height()) {
					return false;
				}

				// paint the userpic if it intersects the painted rect
				if (userpicTop + st::msgPhotoSize > clip.top()) {
					message->from()->paintUserpicLeft(p, st::historyPhotoLeft, userpicTop, message->history()->width, st::msgPhotoSize);
				}
				return true;
			});

			int dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			//QDate lastDate;
			//if (!_history->isEmpty()) {
			//	lastDate = _history->blocks.back()->items.back()->date.date();
			//}

			//// if item top is before this value always show date as a floating date
			//int showFloatingBefore = height() - 2 * (_visibleAreaBottom - _visibleAreaTop) - dateHeight;

			auto scrollDateOpacity = _scrollDateOpacity.current(ms, _scrollDateShown ? 1. : 0.);
			enumerateDates([&p, &clip, scrollDateOpacity, dateHeight/*, lastDate, showFloatingBefore*/](not_null<HistoryItem*> item, int itemtop, int dateTop) {
				// stop the enumeration if the date is above the painted rect
				if (dateTop + dateHeight <= clip.top()) {
					return false;
				}

				bool displayDate = item->displayDate();
				bool dateInPlace = displayDate;
				if (dateInPlace) {
					int correctDateTop = itemtop + st::msgServiceMargin.top();
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
						int width = item->history()->width;
						if (auto date = item->Get<HistoryMessageDate>()) {
							date->paint(p, dateY, width);
						} else {
							HistoryLayout::ServiceMessagePainter::paintDate(p, item->date, dateY, width);
						}
					}
				}
				return true;
			});
		}
	}
}

bool HistoryInner::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return RpWidget::eventHook(e);
}

void HistoryInner::onTouchScrollTimer() {
	auto nowTime = getms();
	if (_touchScrollState == Ui::TouchScrollState::Acceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = Ui::TouchScrollState::Manual;
		touchResetSpeed();
	} else if (_touchScrollState == Ui::TouchScrollState::Auto || _touchScrollState == Ui::TouchScrollState::Acceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = _widget->touchScroll(delta);

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

void HistoryInner::touchUpdateSpeed() {
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

void HistoryInner::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

void HistoryInner::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void HistoryInner::touchEvent(QTouchEvent *e) {
	const Qt::TouchPointStates &states(e->touchPointStates());
	if (e->type() == QEvent::TouchCancel) { // cancel
		if (!_touchInProgress) return;
		_touchInProgress = false;
		_touchSelectTimer.stop();
		_touchScroll = _touchSelect = false;
		_touchScrollState = Ui::TouchScrollState::Manual;
		mouseActionCancel();
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
		mouseActionUpdate(_touchPos);
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
		mouseActionFinish(_touchPos, Qt::RightButton);
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
	} else { // One short tap is like left mouse click.
		mouseActionStart(_touchPos, Qt::LeftButton);
		mouseActionFinish(_touchPos, Qt::LeftButton);
	}
	_touchSelectTimer.stop();
	_touchSelect = false;
	break;
	}
}

void HistoryInner::mouseMoveEvent(QMouseEvent *e) {
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

void HistoryInner::mouseActionUpdate(const QPoint &screenPos) {
	_mousePosition = screenPos;
	onUpdateSelected();
}

void HistoryInner::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	_widget->touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

QPoint HistoryInner::mapPointToItem(QPoint p, HistoryItem *item) {
	int32 msgy = itemTop(item);
	if (msgy < 0) return QPoint(0, 0);

	p.setY(p.y() - msgy);
	return p;
}

void HistoryInner::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
}

void HistoryInner::mouseActionStart(const QPoint &screenPos, Qt::MouseButton button) {
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
	} else if (!_selected.empty()) {
		if (_selected.cbegin()->second == FullSelection) {
			if (_selected.find(_mouseActionItem) != _selected.cend() && App::hoveredItem()) {
				_mouseAction = MouseAction::PrepareDrag; // start items drag
			} else if (!_pressWasInactive) {
				_mouseAction = MouseAction::PrepareSelect; // start items select
			}
		}
	}
	if (_mouseAction == MouseAction::None && _mouseActionItem) {
		HistoryTextState dragState;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->getState(_dragStartPosition, request);
			if (dragState.cursor == HistoryInTextCursorState) {
				TextSelection selStatus = { dragState.symbol, dragState.symbol };
				if (selStatus != FullSelection && (_selected.empty() || _selected.cbegin()->second != FullSelection)) {
					if (!_selected.empty()) {
						repaintItem(_selected.cbegin()->first);
						_selected.clear();
					}
					_selected.emplace(_mouseActionItem, selStatus);
					_mouseTextSymbol = dragState.symbol;
					_mouseAction = MouseAction::Selecting;
					_mouseSelectType = TextSelectType::Paragraphs;
					mouseActionUpdate(_mousePosition);
					_trippleClickTimer.start(QApplication::doubleClickInterval());
				}
			}
		} else if (App::pressedItem()) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->getState(_dragStartPosition, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (App::pressedItem()) {
				_mouseTextSymbol = dragState.symbol;
				bool uponSelected = (dragState.cursor == HistoryInTextCursorState);
				if (uponSelected) {
					if (_selected.empty()
						|| _selected.cbegin()->second == FullSelection
						|| _selected.cbegin()->first != _mouseActionItem) {
						uponSelected = false;
					} else {
						uint16 selFrom = _selected.cbegin()->second.from, selTo = _selected.cbegin()->second.to;
						if (_mouseTextSymbol < selFrom || _mouseTextSymbol >= selTo) {
							uponSelected = false;
						}
					}
				}
				if (uponSelected) {
					_mouseAction = MouseAction::PrepareDrag; // start text drag
				} else if (!_pressWasInactive) {
					if (dynamic_cast<HistorySticker*>(App::pressedItem()->getMedia()) || _mouseCursorState == HistoryInDateCursorState) {
						_mouseAction = MouseAction::PrepareDrag; // start sticker drag or by-date drag
					} else {
						if (dragState.afterSymbol) ++_mouseTextSymbol;
						TextSelection selStatus = { _mouseTextSymbol, _mouseTextSymbol };
						if (selStatus != FullSelection && (_selected.empty() || _selected.cbegin()->second != FullSelection)) {
							if (!_selected.empty()) {
								repaintItem(_selected.cbegin()->first);
								_selected.clear();
							}
							_selected.emplace(_mouseActionItem, selStatus);
							_mouseAction = MouseAction::Selecting;
							repaintItem(_mouseActionItem);
						} else {
							_mouseAction = MouseAction::PrepareSelect;
						}
					}
				}
			} else if (!_pressWasInactive) {
				_mouseAction = MouseAction::PrepareSelect; // start items select
			}
		}
	}

	if (!_mouseActionItem) {
		_mouseAction = MouseAction::None;
	} else if (_mouseAction == MouseAction::None) {
		_mouseActionItem = nullptr;
	}
}

void HistoryInner::mouseActionCancel() {
	_mouseActionItem = nullptr;
	_mouseAction = MouseAction::None;
	_dragStartPosition = QPoint(0, 0);
	_dragSelFrom = _dragSelTo = nullptr;
	_wasSelectedText = false;
	_widget->noSelectingScroll();
}

void HistoryInner::performDrag() {
	if (_mouseAction != MouseAction::Dragging) return;

	bool uponSelected = false;
	if (_mouseActionItem) {
		if (!_selected.empty() && _selected.cbegin()->second == FullSelection) {
			uponSelected = (_selected.find(_mouseActionItem) != _selected.cend());
		} else {
			HistoryStateRequest request;
			request.flags |= Text::StateRequest::Flag::LookupSymbol;
			auto dragState = _mouseActionItem->getState(_dragStartPosition, request);
			uponSelected = (dragState.cursor == HistoryInTextCursorState);
			if (uponSelected) {
				if (_selected.empty()
					|| _selected.cbegin()->second == FullSelection
					|| _selected.cbegin()->first != _mouseActionItem) {
					uponSelected = false;
				} else {
					uint16 selFrom = _selected.cbegin()->second.from, selTo = _selected.cbegin()->second.to;
					if (dragState.symbol < selFrom || dragState.symbol >= selTo) {
						uponSelected = false;
					}
				}
			}
		}
	}
	auto pressedHandler = ClickHandler::getPressed();

	if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.data())) {
		return;
	}

	TextWithEntities sel;
	QList<QUrl> urls;
	if (uponSelected) {
		sel = getSelectedText();
	} else if (pressedHandler) {
		sel = { pressedHandler->dragText(), EntitiesInText() };
		//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
		//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
		//}
	}
	if (auto mimeData = MimeDataFromTextWithEntities(sel)) {
		updateDragSelection(0, 0, false);
		_widget->noSelectingScroll();

		if (!urls.isEmpty()) mimeData->setUrls(urls);
		if (uponSelected && !Adaptive::OneColumn()) {
			auto selectedState = getSelectionState();
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				mimeData->setData(qsl("application/x-td-forward-selected"), "1");
			}
		}
		_controller->window()->launchDrag(std::move(mimeData));
		return;
	} else {
		auto forwardMimeType = QString();
		auto pressedMedia = static_cast<HistoryMedia*>(nullptr);
		if (auto pressedItem = App::pressedItem()) {
			pressedMedia = pressedItem->getMedia();
			if (_mouseCursorState == HistoryInDateCursorState || (pressedMedia && pressedMedia->dragItem())) {
				forwardMimeType = qsl("application/x-td-forward-pressed");
			}
		}
		if (auto pressedLnkItem = App::pressedLinkItem()) {
			if ((pressedMedia = pressedLnkItem->getMedia())) {
				if (forwardMimeType.isEmpty() && pressedMedia->dragItemByHandler(pressedHandler)) {
					forwardMimeType = qsl("application/x-td-forward-pressed-link");
				}
			}
		}
		if (!forwardMimeType.isEmpty()) {
			auto mimeData = std::make_unique<QMimeData>();
			mimeData->setData(forwardMimeType, "1");
			if (auto document = (pressedMedia ? pressedMedia->getDocument() : nullptr)) {
				auto filepath = document->filepath(DocumentData::FilePathResolveChecked);
				if (!filepath.isEmpty()) {
					QList<QUrl> urls;
					urls.push_back(QUrl::fromLocalFile(filepath));
					mimeData->setUrls(urls);
				}
			}

			// This call enters event loop and can destroy any QObject.
			_controller->window()->launchDrag(std::move(mimeData));
			return;
		}
	}
}

void HistoryInner::itemRemoved(not_null<const HistoryItem*> item) {
	if (_history != item->history() && _migrated != item->history()) {
		return;
	}
	if (!App::main()) {
		return;
	}

	auto i = _selected.find(item);
	if (i != _selected.cend()) {
		_selected.erase(i);
		_widget->updateTopBarSelection();
	}

	if (_mouseActionItem == item) {
		mouseActionCancel();
	}

	if (_dragSelFrom == item || _dragSelTo == item) {
		_dragSelFrom = 0;
		_dragSelTo = 0;
		update();
	}
	onUpdateSelected();
}

void HistoryInner::mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);

	auto pressedLinkItem = App::pressedLinkItem();
	auto activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging) {
		activated.clear();
	} else if (auto pressed = pressedLinkItem) {
		// if we are in selecting items mode perhaps we want to
		// toggle selection instead of activating the pressed link
		if (_mouseAction == MouseAction::PrepareDrag && !_pressWasInactive && !_selected.empty() && _selected.cbegin()->second == FullSelection && button != Qt::RightButton) {
			if (auto media = pressed->getMedia()) {
				if (media->toggleSelectionByHandlerClick(activated)) {
					activated.clear();
				}
			}
		}
	}
	if (App::pressedItem()) {
		repaintItem(App::pressedItem());
		App::pressedItem(nullptr);
	}

	_wasSelectedText = false;

	if (activated) {
		mouseActionCancel();
		App::activateClickHandler(activated, button);
		return;
	}
	if (_mouseAction == MouseAction::PrepareSelect && !_pressWasInactive && !_selected.empty() && _selected.cbegin()->second == FullSelection) {
		auto i = _selected.find(_mouseActionItem);
		if (i == _selected.cend()) {
			if (!_mouseActionItem->serviceMsg()
				&& IsServerMsgId(_mouseActionItem->id)
				&& _selected.size() < MaxSelectedItems) {
				if (!_selected.empty() && _selected.cbegin()->second != FullSelection) {
					_selected.clear();
				}
				_selected.emplace(_mouseActionItem, FullSelection);
			}
		} else {
			_selected.erase(i);
		}
		repaintItem(_mouseActionItem);
	} else if (_mouseAction == MouseAction::PrepareDrag && !_pressWasInactive && button != Qt::RightButton) {
		auto i = _selected.find(_mouseActionItem);
		if (i != _selected.cend() && i->second == FullSelection) {
			_selected.erase(i);
			repaintItem(_mouseActionItem);
		} else if (i == _selected.cend() && !_mouseActionItem->serviceMsg() && _mouseActionItem->id > 0 && !_selected.empty() && _selected.cbegin()->second == FullSelection) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.emplace(_mouseActionItem, FullSelection);
				repaintItem(_mouseActionItem);
			}
		} else {
			_selected.clear();
			update();
		}
	} else if (_mouseAction == MouseAction::Selecting) {
		if (_dragSelFrom && _dragSelTo) {
			applyDragSelection();
			_dragSelFrom = _dragSelTo = 0;
		} else if (!_selected.empty() && !_pressWasInactive) {
			auto sel = _selected.cbegin()->second;
			if (sel != FullSelection && sel.from == sel.to) {
				_selected.clear();
				App::wnd()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseActionItem = nullptr;
	_mouseSelectType = TextSelectType::Letters;
	_widget->noSelectingScroll();
	_widget->updateTopBarSelection();

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (!_selected.empty() && _selected.cbegin()->second != FullSelection) {
		setToClipboard(_selected.cbegin()->first->selectedText(_selected.cbegin()->second), QClipboard::Selection);
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void HistoryInner::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void HistoryInner::mouseDoubleClickEvent(QMouseEvent *e) {
	if (!_history) return;

	mouseActionStart(e->globalPos(), e->button());
	if (((_mouseAction == MouseAction::Selecting && !_selected.empty() && _selected.cbegin()->second != FullSelection) || (_mouseAction == MouseAction::None && (_selected.empty() || _selected.cbegin()->second != FullSelection))) && _mouseSelectType == TextSelectType::Letters && _mouseActionItem) {
		HistoryStateRequest request;
		request.flags |= Text::StateRequest::Flag::LookupSymbol;
		auto dragState = _mouseActionItem->getState(_dragStartPosition, request);
		if (dragState.cursor == HistoryInTextCursorState) {
			_mouseTextSymbol = dragState.symbol;
			_mouseSelectType = TextSelectType::Words;
			if (_mouseAction == MouseAction::None) {
				_mouseAction = MouseAction::Selecting;
				TextSelection selStatus = { dragState.symbol, dragState.symbol };
				if (!_selected.empty()) {
					repaintItem(_selected.cbegin()->first);
					_selected.clear();
				}
				_selected.emplace(_mouseActionItem, selStatus);
			}
			mouseMoveEvent(e);

			_trippleClickPoint = e->globalPos();
			_trippleClickTimer.start(QApplication::doubleClickInterval());
		}
	}
}

void HistoryInner::contextMenuEvent(QContextMenuEvent *e) {
	showContextMenu(e);
}

void HistoryInner::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (_menu) {
		_menu->deleteLater();
		_menu = nullptr;
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	auto selectedState = getSelectionState();
	auto canSendMessages = _widget->canSendMessages(_peer);

	// -2 - has full selected items, but not over, -1 - has selection, but no over, 0 - no selection, 1 - over text, 2 - over full selected items
	auto isUponSelected = 0;
	auto hasSelected = 0;;
	if (!_selected.empty()) {
		isUponSelected = -1;
		if (_selected.cbegin()->second == FullSelection) {
			hasSelected = 2;
			if (App::hoveredItem() && _selected.find(App::hoveredItem()) != _selected.cend()) {
				isUponSelected = 2;
			} else {
				isUponSelected = -2;
			}
		} else {
			uint16 selFrom = _selected.cbegin()->second.from, selTo = _selected.cbegin()->second.to;
			hasSelected = (selTo > selFrom) ? 1 : 0;
			if (App::mousedItem() && App::mousedItem() == App::hoveredItem()) {
				auto mousePos = mapPointToItem(mapFromGlobal(_mousePosition), App::mousedItem());
				HistoryStateRequest request;
				request.flags |= Text::StateRequest::Flag::LookupSymbol;
				auto dragState = App::mousedItem()->getState(mousePos, request);
				if (dragState.cursor == HistoryInTextCursorState && dragState.symbol >= selFrom && dragState.symbol < selTo) {
					isUponSelected = 1;
				}
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_menu = new Ui::PopupMenu(nullptr);

	_contextMenuLink = ClickHandler::getActive();
	HistoryItem *item = App::hoveredItem() ? App::hoveredItem() : App::hoveredLinkItem();
	PhotoClickHandler *lnkPhoto = dynamic_cast<PhotoClickHandler*>(_contextMenuLink.data());
	DocumentClickHandler *lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLink.data());
	bool lnkIsVideo = lnkDocument ? lnkDocument->document()->isVideo() : false;
	bool lnkIsAudio = lnkDocument ? (lnkDocument->document()->voice() != nullptr) : false;
	bool lnkIsSong = lnkDocument ? (lnkDocument->document()->song() != nullptr) : false;
	if (lnkPhoto || lnkDocument) {
		if (isUponSelected > 0) {
			_menu->addAction(lang((isUponSelected > 1) ? lng_context_copy_selected_items : lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
		}
		if (item && item->id > 0 && isUponSelected != 2 && isUponSelected != -2) {
			if (canSendMessages) {
				_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
			}
			if (item->canEdit(::date(unixtime()))) {
				_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
			}
			if (item->canPin()) {
				auto isPinned = item->isPinned();
				_menu->addAction(lang(isPinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, isPinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
			}
		}
		if (lnkPhoto) {
			_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, photo = lnkPhoto->photo()] {
				savePhotoToFile(photo);
			}))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_image), [this, photo = lnkPhoto->photo()] {
				copyContextImage(photo);
			})->setEnabled(true);
		} else {
			auto document = lnkDocument->document();
			if (document->loading()) {
				_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
			} else {
				if (document->loaded() && document->isGifv()) {
					if (!cAutoPlayGif()) {
						_menu->addAction(lang(lng_context_open_gif), this, SLOT(openContextGif()))->setEnabled(true);
					}
					_menu->addAction(lang(lng_context_save_gif), this, SLOT(saveContextGif()))->setEnabled(true);
				}
				if (!document->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
					_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
				}
				_menu->addAction(lang(lnkIsVideo ? lng_context_save_video : (lnkIsAudio ? lng_context_save_audio : (lnkIsSong ? lng_context_save_audio_file : lng_context_save_file))), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
					saveDocumentToFile(document);
				}))->setEnabled(true);
			}
		}
		if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(lang(item->history()->peer->isMegagroup() ? lng_context_copy_link : lng_context_copy_post_link), _widget, SLOT(onCopyPostLink()));
		}
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.canForwardCount == selectedState.count) {
				_menu->addAction(lang(lng_context_forward_selected), _widget, SLOT(onForwardSelected()));
			}
			if (selectedState.count > 0 && selectedState.canDeleteCount == selectedState.count) {
				_menu->addAction(lang(lng_context_delete_selected), base::lambda_guarded(this, [this] {
					_widget->confirmDeleteSelectedItems();
				}));
			}
			_menu->addAction(lang(lng_context_clear_selection), _widget, SLOT(onClearSelected()));
		} else if (App::hoveredLinkItem()) {
			if (isUponSelected != -2) {
				if (App::hoveredLinkItem()->canForward()) {
					_menu->addAction(lang(lng_context_forward_msg), _widget, SLOT(forwardMessage()))->setEnabled(true);
				}
				if (App::hoveredLinkItem()->canDelete()) {
					_menu->addAction(lang(lng_context_delete_msg), base::lambda_guarded(this, [this] {
						_widget->confirmDeleteContextItem();
					}));
				}
			}
			if (App::hoveredLinkItem()->id > 0 && !App::hoveredLinkItem()->serviceMsg()) {
				_menu->addAction(lang(lng_context_select_msg), _widget, SLOT(selectMessage()))->setEnabled(true);
			}
			App::contextItem(App::hoveredLinkItem());
		}
	} else { // maybe cursor on some text history item?
		bool canDelete = item && item->canDelete() && (item->id > 0 || !item->serviceMsg());
		bool canForward = item && item->canForward();

		auto msg = dynamic_cast<HistoryMessage*>(item);
		if (isUponSelected > 0) {
			_menu->addAction(lang((isUponSelected > 1) ? lng_context_copy_selected_items : lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
			if (item && item->id > 0 && isUponSelected != 2) {
				if (canSendMessages) {
					_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
				}
				if (item->canEdit(::date(unixtime()))) {
					_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
				}
				if (item->canPin()) {
					auto isPinned = item->isPinned();
					_menu->addAction(lang(isPinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, isPinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
				}
			}
		} else {
			if (item && item->id > 0 && isUponSelected != -2) {
				if (canSendMessages) {
					_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
				}
				if (item->canEdit(::date(unixtime()))) {
					_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
				}
				if (item->canPin()) {
					auto isPinned = item->isPinned();
					_menu->addAction(lang(isPinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, isPinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
				}
			}
			if (item && !isUponSelected) {
				auto mediaHasTextForCopy = false;
				if (auto media = (msg ? msg->getMedia() : nullptr)) {
					mediaHasTextForCopy = media->hasTextForCopy();
					if (media->type() == MediaTypeWebPage && static_cast<HistoryWebPage*>(media)->attach()) {
						media = static_cast<HistoryWebPage*>(media)->attach();
					}
					if (media->type() == MediaTypeSticker) {
						if (auto document = media->getDocument()) {
							if (document->sticker() && document->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
								_menu->addAction(lang(document->sticker()->setInstalled() ? lng_context_pack_info : lng_context_pack_add), [this, document] { showStickerPackInfo(document); });
								_menu->addAction(lang(Stickers::IsFaved(document) ? lng_faved_stickers_remove : lng_faved_stickers_add), [this, document] { toggleFavedSticker(document); });
							}
							_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
								saveDocumentToFile(document);
							}))->setEnabled(true);
						}
					} else if (media->type() == MediaTypeGif && !_contextMenuLink) {
						if (auto document = media->getDocument()) {
							if (document->loading()) {
								_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
							} else {
								if (document->isGifv()) {
									if (!cAutoPlayGif()) {
										_menu->addAction(lang(lng_context_open_gif), this, SLOT(openContextGif()))->setEnabled(true);
									}
									_menu->addAction(lang(lng_context_save_gif), this, SLOT(saveContextGif()))->setEnabled(true);
								}
								if (!document->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
									_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
								}
								_menu->addAction(lang(lng_context_save_file), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
									saveDocumentToFile(document);
								}))->setEnabled(true);
							}
						}
					}
				}
				if (msg && !_contextMenuLink && (!msg->emptyText() || mediaHasTextForCopy)) {
					_menu->addAction(lang(lng_context_copy_text), this, SLOT(copyContextText()))->setEnabled(true);
				}
			}
		}

		auto linkCopyToClipboardText = _contextMenuLink ? _contextMenuLink->copyToClipboardContextItemText() : QString();
		if (!linkCopyToClipboardText.isEmpty()) {
			_menu->addAction(linkCopyToClipboardText, this, SLOT(copyContextUrl()))->setEnabled(true);
		}
		if (linkCopyToClipboardText.isEmpty()) {
			if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
				_menu->addAction(lang(item->history()->peer->isMegagroup() ? lng_context_copy_link : lng_context_copy_post_link), _widget, SLOT(onCopyPostLink()));
			}
		}
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				_menu->addAction(lang(lng_context_forward_selected), _widget, SLOT(onForwardSelected()));
			}
			if (selectedState.count > 0 && selectedState.count == selectedState.canDeleteCount) {
				_menu->addAction(lang(lng_context_delete_selected), base::lambda_guarded(this, [this] {
					_widget->confirmDeleteSelectedItems();
				}));
			}
			_menu->addAction(lang(lng_context_clear_selection), _widget, SLOT(onClearSelected()));
		} else if (item && ((isUponSelected != -2 && (canForward || canDelete)) || item->id > 0)) {
			if (isUponSelected != -2) {
				if (canForward) {
					_menu->addAction(lang(lng_context_forward_msg), _widget, SLOT(forwardMessage()))->setEnabled(true);
				}

				if (canDelete) {
					_menu->addAction(lang((msg && msg->uploading()) ? lng_context_cancel_upload : lng_context_delete_msg), base::lambda_guarded(this, [this] {
						_widget->confirmDeleteContextItem();
					}));
				}
			}
			if (item->id > 0 && !item->serviceMsg()) {
				_menu->addAction(lang(lng_context_select_msg), _widget, SLOT(selectMessage()))->setEnabled(true);
			}
		} else {
			if (App::mousedItem() && !App::mousedItem()->serviceMsg() && App::mousedItem()->id > 0) {
				_menu->addAction(lang(lng_context_select_msg), _widget, SLOT(selectMessage()))->setEnabled(true);
				item = App::mousedItem();
			}
		}
		App::contextItem(item);
	}

	if (_menu->actions().isEmpty()) {
		delete _menu;
		_menu = 0;
	} else {
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void HistoryInner::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = nullptr;
	}
}

void HistoryInner::copySelectedText() {
	setToClipboard(getSelectedText());
}

void HistoryInner::copyContextUrl() {
	if (_contextMenuLink) {
		_contextMenuLink->copyToClipboard();
	}
}

void HistoryInner::savePhotoToFile(PhotoData *photo) {
	if (!photo || !photo->date || !photo->loaded()) return;

	auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
	FileDialog::GetWritePath(lang(lng_save_photo), filter, filedialogDefaultName(qsl("photo"), qsl(".jpg")), base::lambda_guarded(this, [this, photo](const QString &result) {
		if (!result.isEmpty()) {
			photo->full->pix().toImage().save(result, "JPG");
		}
	}));
}

void HistoryInner::copyContextImage(PhotoData *photo) {
	if (!photo || !photo->date || !photo->loaded()) return;

	QApplication::clipboard()->setPixmap(photo->full->pix());
}

void HistoryInner::showStickerPackInfo(DocumentData *document) {
	if (auto sticker = document->sticker()) {
		if (sticker->set.type() != mtpc_inputStickerSetEmpty) {
			App::main()->stickersBox(sticker->set);
		}
	}
}

void HistoryInner::toggleFavedSticker(DocumentData *document) {
	auto unfave = Stickers::IsFaved(document);
	MTP::send(MTPmessages_FaveSticker(document->mtpInput(), MTP_bool(unfave)), rpcDone([document, unfave](const MTPBool &result) {
		Stickers::SetFaved(document, !unfave);
	}));
}

void HistoryInner::cancelContextDownload() {
	if (auto lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLink.data())) {
		lnkDocument->document()->cancel();
	} else if (auto item = App::contextItem()) {
		if (auto media = item->getMedia()) {
			if (auto doc = media->getDocument()) {
				doc->cancel();
			}
		}
	}
}

void HistoryInner::showContextInFolder() {
	QString filepath;
	if (auto lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLink.data())) {
		filepath = lnkDocument->document()->filepath(DocumentData::FilePathResolveChecked);
	} else if (auto item = App::contextItem()) {
		if (auto media = item->getMedia()) {
			if (auto doc = media->getDocument()) {
				filepath = doc->filepath(DocumentData::FilePathResolveChecked);
			}
		}
	}
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void HistoryInner::saveDocumentToFile(DocumentData *document) {
	DocumentSaveClickHandler::doSave(document, true);
}

void HistoryInner::openContextGif() {
	if (auto item = App::contextItem()) {
		if (auto media = item->getMedia()) {
			if (auto document = media->getDocument()) {
				Messenger::Instance().showDocument(document, item);
			}
		}
	}
}

void HistoryInner::saveContextGif() {
	if (auto item = App::contextItem()) {
		if (auto media = item->getMedia()) {
			if (auto document = media->getDocument()) {
				_widget->saveGif(document);
			}
		}
	}
}

void HistoryInner::copyContextText() {
	auto item = App::contextItem();
	if (!item || (item->getMedia() && item->getMedia()->type() == MediaTypeSticker)) {
		return;
	}

	setToClipboard(item->selectedText(FullSelection));
}

void HistoryInner::setToClipboard(const TextWithEntities &forClipboard, QClipboard::Mode mode) {
	if (auto data = MimeDataFromTextWithEntities(forClipboard)) {
		QApplication::clipboard()->setMimeData(data.release(), mode);
	}
}

void HistoryInner::resizeEvent(QResizeEvent *e) {
	onUpdateSelected();
}

TextWithEntities HistoryInner::getSelectedText() const {
	SelectedItems sel = _selected;

	if (_mouseAction == MouseAction::Selecting && _dragSelFrom && _dragSelTo) {
		applyDragSelection(&sel);
	}

	if (sel.empty()) {
		return TextWithEntities();
	}
	if (sel.cbegin()->second != FullSelection) {
		return sel.cbegin()->first->selectedText(sel.cbegin()->second);
	}

	int fullSize = 0;
	QString timeFormat(qsl(", [dd.MM.yy hh:mm]\n"));
	QMap<int, TextWithEntities> texts;
	for (auto &selected : sel) {
		auto item = selected.first;
		if (item->detached()) continue;

		auto time = item->date.toString(timeFormat);
		TextWithEntities part, unwrapped = item->selectedText(FullSelection);
		int size = item->author()->name.size() + time.size() + unwrapped.text.size();
		part.text.reserve(size);

		int y = itemTop(item);
		if (y >= 0) {
			part.text.append(item->author()->name).append(time);
			TextUtilities::Append(part, std::move(unwrapped));
			texts.insert(y, part);
			fullSize += size;
		}
	}

	TextWithEntities result;
	auto sep = qsl("\n\n");
	result.text.reserve(fullSize + (texts.size() - 1) * sep.size());
	for (auto i = texts.begin(), e = texts.end(); i != e; ++i) {
		TextUtilities::Append(result, std::move(i.value()));
		if (i + 1 != e) {
			result.text.append(sep);
		}
	}
	return result;
}

void HistoryInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		_widget->onListEscapePressed();
	} else if (e == QKeySequence::Copy && !_selected.empty()) {
		copySelectedText();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		setToClipboard(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else if (e == QKeySequence::Delete) {
		auto selectedState = getSelectionState();
		if (selectedState.count > 0 && selectedState.canDeleteCount == selectedState.count) {
			_widget->confirmDeleteSelectedItems();
		}
	} else {
		e->ignore();
	}
}

void HistoryInner::recountHistoryGeometry() {
	int visibleHeight = _scroll->height();
	int oldHistoryPaddingTop = qMax(visibleHeight - historyHeight() - st::historyPaddingBottom, 0);
	if (_botAbout && !_botAbout->info->text.isEmpty()) {
		accumulate_max(oldHistoryPaddingTop, st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + _botAbout->height);
	}

	_history->resizeGetHeight(_scroll->width());
	if (_migrated) {
		_migrated->resizeGetHeight(_scroll->width());
	}

	// with migrated history we perhaps do not need to display first _history message
	// (if last _migrated message and first _history message are both isGroupMigrate)
	// or at least we don't need to display first _history date (just skip it by height)
	_historySkipHeight = 0;
	if (_migrated) {
		if (!_migrated->isEmpty() && !_history->isEmpty() && _migrated->loadedAtBottom() && _history->loadedAtTop()) {
			if (_migrated->blocks.back()->items.back()->date.date() == _history->blocks.front()->items.front()->date.date()) {
				if (_migrated->blocks.back()->items.back()->isGroupMigrate() && _history->blocks.front()->items.front()->isGroupMigrate()) {
					_historySkipHeight += _history->blocks.front()->items.front()->height();
				} else {
					_historySkipHeight += _history->blocks.front()->items.front()->displayedDateHeight();
				}
			}
		}
	}

	updateBotInfo(false);
	if (_botAbout && !_botAbout->info->text.isEmpty()) {
		int32 tw = _scroll->width() - st::msgMargin.left() - st::msgMargin.right();
		if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
		tw -= st::msgPadding.left() + st::msgPadding.right();
		int32 mw = qMax(_botAbout->info->text.maxWidth(), st::msgNameFont->width(lang(lng_bot_description)));
		if (tw > mw) tw = mw;

		_botAbout->width = tw;
		_botAbout->height = _botAbout->info->text.countHeight(_botAbout->width);

		int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + _botAbout->height + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descMaxWidth = _scroll->width();
		if (Adaptive::ChatWide()) {
			descMaxWidth = qMin(descMaxWidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
		}
		int32 descAtX = (descMaxWidth - _botAbout->width) / 2 - st::msgPadding.left();
		int32 descAtY = qMin(_historyPaddingTop - descH, qMax(0, (_scroll->height() - descH) / 2)) + st::msgMargin.top();

		_botAbout->rect = QRect(descAtX, descAtY, _botAbout->width + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
	} else if (_botAbout) {
		_botAbout->width = _botAbout->height = 0;
		_botAbout->rect = QRect();
	}

	int newHistoryPaddingTop = qMax(visibleHeight - historyHeight() - st::historyPaddingBottom, 0);
	if (_botAbout && !_botAbout->info->text.isEmpty()) {
		accumulate_max(newHistoryPaddingTop, st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + _botAbout->height);
	}

	auto historyPaddingTopDelta = (newHistoryPaddingTop - oldHistoryPaddingTop);
	if (historyPaddingTopDelta != 0) {
		if (_history->scrollTopItem) {
			_history->scrollTopOffset += historyPaddingTopDelta;
		} else if (_migrated && _migrated->scrollTopItem) {
			_migrated->scrollTopOffset += historyPaddingTopDelta;
		}
	}
}

void HistoryInner::updateBotInfo(bool recount) {
	int newh = 0;
	if (_botAbout && !_botAbout->info->description.isEmpty()) {
		if (_botAbout->info->text.isEmpty()) {
			_botAbout->info->text.setText(st::messageTextStyle, _botAbout->info->description, _historyBotNoMonoOptions);
			if (recount) {
				int32 tw = _scroll->width() - st::msgMargin.left() - st::msgMargin.right();
				if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
				tw -= st::msgPadding.left() + st::msgPadding.right();
				int32 mw = qMax(_botAbout->info->text.maxWidth(), st::msgNameFont->width(lang(lng_bot_description)));
				if (tw > mw) tw = mw;

				_botAbout->width = tw;
				newh = _botAbout->info->text.countHeight(_botAbout->width);
			}
		} else if (recount) {
			newh = _botAbout->height;
		}
	}
	if (recount && _botAbout) {
		if (_botAbout->height != newh) {
			_botAbout->height = newh;
			updateSize();
		}
		if (_botAbout->height > 0) {
			int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + _botAbout->height + st::msgPadding.bottom() + st::msgMargin.bottom();
			int32 descAtX = (_scroll->width() - _botAbout->width) / 2 - st::msgPadding.left();
			int32 descAtY = qMin(_historyPaddingTop - descH, (_scroll->height() - descH) / 2) + st::msgMargin.top();

			_botAbout->rect = QRect(descAtX, descAtY, _botAbout->width + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
		} else {
			_botAbout->width = 0;
			_botAbout->rect = QRect();
		}
	}
}

bool HistoryInner::wasSelectedText() const {
	return _wasSelectedText;
}

void HistoryInner::setFirstLoading(bool loading) {
	_firstLoading = loading;
	update();
}

void HistoryInner::visibleAreaUpdated(int top, int bottom) {
	auto scrolledUp = (top < _visibleAreaTop);
	_visibleAreaTop = top;
	_visibleAreaBottom = bottom;

	// if history has pending resize events we should not update scrollTopItem
	if (hasPendingResizedItems()) {
		return;
	}

	if (bottom >= _historyPaddingTop + historyHeight() + st::historyPaddingBottom) {
		_history->forgetScrollState();
		if (_migrated) {
			_migrated->forgetScrollState();
		}
	} else {
		int htop = historyTop(), mtop = migratedTop();
		if ((htop >= 0 && top >= htop) || mtop < 0) {
			_history->countScrollState(top - htop);
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		} else if (mtop >= 0 && top >= mtop) {
			_history->forgetScrollState();
			_migrated->countScrollState(top - mtop);
		} else {
			_history->countScrollState(top - htop);
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		}
	}
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		onScrollDateHideByTimer();
	}
}

bool HistoryInner::displayScrollDate() const {
	return (_visibleAreaTop <= height() - 2 * (_visibleAreaBottom - _visibleAreaTop));
}

void HistoryInner::onScrollDateCheck() {
	if (!_history) return;

	auto newScrollDateItem = _history->scrollTopItem ? _history->scrollTopItem : (_migrated ? _migrated->scrollTopItem : nullptr);
	auto newScrollDateItemTop = _history->scrollTopItem ? _history->scrollTopOffset : (_migrated ? _migrated->scrollTopOffset : 0);
	//if (newScrollDateItem && !displayScrollDate()) {
	//	if (!_history->isEmpty() && newScrollDateItem->date.date() == _history->blocks.back()->items.back()->date.date()) {
	//		newScrollDateItem = nullptr;
	//	}
	//}
	if (!newScrollDateItem) {
		_scrollDateLastItem = nullptr;
		_scrollDateLastItemTop = 0;
		scrollDateHide();
	} else if (newScrollDateItem != _scrollDateLastItem || newScrollDateItemTop != _scrollDateLastItemTop) {
		// Show scroll date only if it is not the initial onScroll() event (with empty _scrollDateLastItem).
		if (_scrollDateLastItem && !_scrollDateShown) {
			toggleScrollDateShown();
		}
		_scrollDateLastItem = newScrollDateItem;
		_scrollDateLastItemTop = newScrollDateItemTop;
		_scrollDateHideTimer.start(kScrollDateHideTimeout);
	}
}

void HistoryInner::onScrollDateHideByTimer() {
	_scrollDateHideTimer.stop();
	if (!_scrollDateLink || ClickHandler::getPressed() != _scrollDateLink) {
		scrollDateHide();
	}
}

void HistoryInner::scrollDateHide() {
	if (_scrollDateShown) {
		toggleScrollDateShown();
	}
}

void HistoryInner::keepScrollDateForNow() {
	if (!_scrollDateShown && _scrollDateLastItem && _scrollDateOpacity.animating()) {
		toggleScrollDateShown();
	}
	_scrollDateHideTimer.start(kScrollDateHideTimeout);
}

void HistoryInner::toggleScrollDateShown() {
	_scrollDateShown = !_scrollDateShown;
	auto from = _scrollDateShown ? 0. : 1.;
	auto to = _scrollDateShown ? 1. : 0.;
	_scrollDateOpacity.start([this] { repaintScrollDateCallback(); }, from, to, st::historyDateFadeDuration);
}

void HistoryInner::repaintScrollDateCallback() {
	int updateTop = _visibleAreaTop;
	int updateHeight = st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	update(0, updateTop, width(), updateHeight);
}

void HistoryInner::updateSize() {
	int visibleHeight = _scroll->height();
	int newHistoryPaddingTop = qMax(visibleHeight - historyHeight() - st::historyPaddingBottom, 0);
	if (_botAbout && !_botAbout->info->text.isEmpty()) {
		accumulate_max(newHistoryPaddingTop, st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + _botAbout->height);
	}

	if (_botAbout && _botAbout->height > 0) {
		int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + _botAbout->height + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descMaxWidth = _scroll->width();
		if (Adaptive::ChatWide()) {
			descMaxWidth = qMin(descMaxWidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
		}
		int32 descAtX = (descMaxWidth - _botAbout->width) / 2 - st::msgPadding.left();
		int32 descAtY = qMin(newHistoryPaddingTop - descH, qMax(0, (_scroll->height() - descH) / 2)) + st::msgMargin.top();

		_botAbout->rect = QRect(descAtX, descAtY, _botAbout->width + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
	}

	_historyPaddingTop = newHistoryPaddingTop;

	int newHeight = _historyPaddingTop + historyHeight() + st::historyPaddingBottom;
	if (width() != _scroll->width() || height() != newHeight) {
		resize(_scroll->width(), newHeight);

		mouseActionUpdate(QCursor::pos());
	} else {
		update();
	}
}

void HistoryInner::enterEventHook(QEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return TWidget::enterEventHook(e);
}

void HistoryInner::leaveEventHook(QEvent *e) {
	if (auto item = App::hoveredItem()) {
		repaintItem(item);
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

HistoryInner::~HistoryInner() {
	delete _menu;
	_mouseAction = MouseAction::None;
}

bool HistoryInner::focusNextPrevChild(bool next) {
	if (_selected.empty()) {
		return TWidget::focusNextPrevChild(next);
	} else {
		clearSelectedItems();
		return true;
	}
}

void HistoryInner::adjustCurrent(int32 y) const {
	int32 htop = historyTop(), hdrawtop = historyDrawTop(), mtop = migratedTop();
	_curHistory = 0;
	if (mtop >= 0) {
		adjustCurrent(y - mtop, _migrated);
	}
	if (htop >= 0 && hdrawtop >= 0 && (mtop < 0 || y >= hdrawtop)) {
		adjustCurrent(y - htop, _history);
	}
}

void HistoryInner::adjustCurrent(int32 y, History *history) const {
	Assert(!history->isEmpty());
	_curHistory = history;
	if (_curBlock >= history->blocks.size()) {
		_curBlock = history->blocks.size() - 1;
		_curItem = 0;
	}
	while (history->blocks[_curBlock]->y() > y && _curBlock > 0) {
		--_curBlock;
		_curItem = 0;
	}
	while (history->blocks[_curBlock]->y() + history->blocks[_curBlock]->height() <= y && _curBlock + 1 < history->blocks.size()) {
		++_curBlock;
		_curItem = 0;
	}
	auto block = history->blocks[_curBlock];
	if (_curItem >= block->items.size()) {
		_curItem = block->items.size() - 1;
	}
	auto by = block->y();
	while (block->items[_curItem]->y() + by > y && _curItem > 0) {
		--_curItem;
	}
	while (block->items[_curItem]->y() + block->items[_curItem]->height() + by <= y && _curItem + 1 < block->items.size()) {
		++_curItem;
	}
}

HistoryItem *HistoryInner::prevItem(HistoryItem *item) {
	if (!item || item->detached()) return nullptr;

	HistoryBlock *block = item->block();
	int blockIndex = block->indexInHistory(), itemIndex = item->indexInBlock();
	if (itemIndex > 0) {
		return block->items.at(itemIndex - 1);
	}
	if (blockIndex > 0) {
		return item->history()->blocks.at(blockIndex - 1)->items.back();
	}
	if (item->history() == _history && _migrated && _history->loadedAtTop() && !_migrated->isEmpty() && _migrated->loadedAtBottom()) {
		return _migrated->blocks.back()->items.back();
	}
	return nullptr;
}

HistoryItem *HistoryInner::nextItem(HistoryItem *item) {
	if (!item || item->detached()) return nullptr;

	HistoryBlock *block = item->block();
	int blockIndex = block->indexInHistory(), itemIndex = item->indexInBlock();
	if (itemIndex + 1 < block->items.size()) {
		return block->items.at(itemIndex + 1);
	}
	if (blockIndex + 1 < item->history()->blocks.size()) {
		return item->history()->blocks.at(blockIndex + 1)->items.front();
	}
	if (item->history() == _migrated && _history && _migrated->loadedAtBottom() && _history->loadedAtTop() && !_history->isEmpty()) {
		return _history->blocks.front()->items.front();
	}
	return nullptr;
}

bool HistoryInner::canCopySelected() const {
	return !_selected.empty();
}

bool HistoryInner::canDeleteSelected() const {
	auto selectedState = getSelectionState();
	return (selectedState.count > 0) && (selectedState.count == selectedState.canDeleteCount);
}

HistoryTopBarWidget::SelectedState HistoryInner::getSelectionState() const {
	auto result = HistoryTopBarWidget::SelectedState {};
	for (auto &selected : _selected) {
		if (selected.second == FullSelection) {
			++result.count;
			if (selected.first->canDelete()) {
				++result.canDeleteCount;
			}
			if (selected.first->canForward()) {
				++result.canForwardCount;
			}
		} else {
			result.textSelected = true;
		}
	}
	return result;
}

void HistoryInner::clearSelectedItems(bool onlyTextSelection) {
	if (!_selected.empty() && (!onlyTextSelection || _selected.cbegin()->second != FullSelection)) {
		_selected.clear();
		_widget->updateTopBarSelection();
		_widget->update();
	}
}

SelectedItemSet HistoryInner::getSelectedItems() const {
	auto result = SelectedItemSet();
	if (_selected.empty() || _selected.cbegin()->second != FullSelection) {
		return result;
	}

	for (auto &selected : _selected) {
		auto item = selected.first;
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

void HistoryInner::selectItem(HistoryItem *item) {
	if (!_selected.empty() && _selected.cbegin()->second != FullSelection) {
		_selected.clear();
	} else if (_selected.size() == MaxSelectedItems && _selected.find(item) == _selected.cend()) {
		return;
	}
	_selected.emplace(item, FullSelection);
	_widget->updateTopBarSelection();
	_widget->update();
}

void HistoryInner::onTouchSelect() {
	_touchSelect = true;
	mouseActionStart(_touchPos, Qt::LeftButton);
}

void HistoryInner::onUpdateSelected() {
	if (!_history || hasPendingResizedItems()) {
		return;
	}

	auto mousePos = mapFromGlobal(_mousePosition);
	auto point = _widget->clampMousePosition(mousePos);

	HistoryBlock *block = 0;
	HistoryItem *item = 0;
	QPoint m;

	adjustCurrent(point.y());
	if (_curHistory && !_curHistory->isEmpty()) {
		block = _curHistory->blocks[_curBlock];
		item = block->items[_curItem];

		App::mousedItem(item);
		m = mapPointToItem(point, item);
		if (item->hasPoint(m)) {
			if (App::hoveredItem() != item) {
				repaintItem(App::hoveredItem());
				App::hoveredItem(item);
				repaintItem(App::hoveredItem());
			}
		} else if (App::hoveredItem()) {
			repaintItem(App::hoveredItem());
			App::hoveredItem(0);
		}
	}
	if (_mouseActionItem && _mouseActionItem->detached()) {
		mouseActionCancel();
	}

	HistoryTextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	bool selectingText = (item == _mouseActionItem && item == App::hoveredItem() && !_selected.empty() && _selected.cbegin()->second != FullSelection);
	if (point.y() < _historyPaddingTop) {
		if (_botAbout && !_botAbout->info->text.isEmpty() && _botAbout->height > 0) {
			dragState = _botAbout->info->text.getState(point - _botAbout->rect.topLeft() - QPoint(st::msgPadding.left(), st::msgPadding.top() + st::botDescSkip + st::msgNameFont->height), _botAbout->width);
			lnkhost = _botAbout.get();
		}
	} else if (item) {
		if (item != _mouseActionItem || (m - _dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [this] { performDrag(); });
			} else if (_mouseAction == MouseAction::PrepareSelect) {
				_mouseAction = MouseAction::Selecting;
			}
		}

		auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
		auto scrollDateOpacity = _scrollDateOpacity.current(_scrollDateShown ? 1. : 0.);
		enumerateDates([this, &dragState, &lnkhost, &point, scrollDateOpacity, dateHeight/*, lastDate, showFloatingBefore*/](not_null<HistoryItem*> item, int itemtop, int dateTop) {
			// stop enumeration if the date is above our point
			if (dateTop + dateHeight <= point.y()) {
				return false;
			}

			bool displayDate = item->displayDate();
			bool dateInPlace = displayDate;
			if (dateInPlace) {
				int correctDateTop = itemtop + st::msgServiceMargin.top();
				dateInPlace = (dateTop < correctDateTop + dateHeight);
			}

			// stop enumeration if we've found a date under the cursor
			if (dateTop <= point.y()) {
				auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
				if (opacity > 0.) {
					auto dateWidth = 0;
					if (auto date = item->Get<HistoryMessageDate>()) {
						dateWidth = date->_width;
					} else {
						dateWidth = st::msgServiceFont->width(langDayOfMonthFull(item->date.date()));
					}
					dateWidth += st::msgServicePadding.left() + st::msgServicePadding.right();
					auto dateLeft = st::msgServiceMargin.left();
					auto maxwidth = item->history()->width;
					if (Adaptive::ChatWide()) {
						maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
					}
					auto widthForDate = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

					dateLeft += (widthForDate - dateWidth) / 2;

					if (point.x() >= dateLeft && point.x() < dateLeft + dateWidth) {
						if (!_scrollDateLink) {
							_scrollDateLink = MakeShared<DateClickHandler>(item->history()->peer, item->date.date());
						} else {
							static_cast<DateClickHandler*>(_scrollDateLink.data())->setDate(item->date.date());
						}
						dragState.link = _scrollDateLink;
						lnkhost = item;
					}
				}
				return false;
			}
			return true;
		});
		if (!dragState.link) {
			HistoryStateRequest request;
			if (_mouseAction == MouseAction::Selecting) {
				request.flags |= Text::StateRequest::Flag::LookupSymbol;
			} else {
				selectingText = false;
			}
			dragState = item->getState(m, request);
			lnkhost = item;
			if (!dragState.link && m.x() >= st::historyPhotoLeft && m.x() < st::historyPhotoLeft + st::msgPhotoSize) {
				if (auto msg = item->toHistoryMessage()) {
					if (msg->hasFromPhoto()) {
						enumerateUserpics([&dragState, &lnkhost, &point](not_null<HistoryMessage*> message, int userpicTop) -> bool {
							// stop enumeration if the userpic is below our point
							if (userpicTop > point.y()) {
								return false;
							}

							// stop enumeration if we've found a userpic under the cursor
							if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
								dragState.link = message->from()->openLink();
								lnkhost = message;
								return false;
							}
							return true;
						});
					}
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

	Qt::CursorShape cur = style::cur_default;
	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		if (dragState.link) {
			cur = style::cur_pointer;
		} else if (_mouseCursorState == HistoryInTextCursorState && (_selected.empty() || _selected.cbegin()->second != FullSelection)) {
			cur = style::cur_text;
		} else if (_mouseCursorState == HistoryInDateCursorState) {
			//			cur = style::cur_cross;
		}
	} else if (item) {
		if (_mouseAction == MouseAction::Selecting) {
			auto canSelectMany = (_history != nullptr);
			if (selectingText) {
				uint16 second = dragState.symbol;
				if (dragState.afterSymbol && _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selState = TextSelection { qMin(second, _mouseTextSymbol), qMax(second, _mouseTextSymbol) };
				if (_mouseSelectType != TextSelectType::Letters) {
					selState = _mouseActionItem->adjustSelection(selState, _mouseSelectType);
				}
				if (_selected[_mouseActionItem] != selState) {
					_selected[_mouseActionItem] = selState;
					repaintItem(_mouseActionItem);
				}
				if (!_wasSelectedText && (selState == FullSelection || selState.from != selState.to)) {
					_wasSelectedText = true;
					setFocus();
				}
				updateDragSelection(0, 0, false);
			} else if (canSelectMany) {
				auto selectingDown = (itemTop(_mouseActionItem) < itemTop(item)) || (_mouseActionItem == item && _dragStartPosition.y() < m.y());
				auto dragSelFrom = _mouseActionItem, dragSelTo = item;
				if (!dragSelFrom->hasPoint(_dragStartPosition)) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_dragStartPosition.y() >= dragSelFrom->height() - dragSelFrom->marginBottom() || ((item == dragSelFrom) && (m.y() < _dragStartPosition.y() + QApplication::startDragDistance() || m.y() < dragSelFrom->marginTop()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelFrom);
						}
					} else {
						if (_dragStartPosition.y() < dragSelFrom->marginTop() || ((item == dragSelFrom) && (m.y() >= _dragStartPosition.y() - QApplication::startDragDistance() || m.y() >= dragSelFrom->height() - dragSelFrom->marginBottom()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : prevItem(dragSelFrom);
						}
					}
				}
				if (_mouseActionItem != item) { // maybe exclude dragSelTo
					if (selectingDown) {
						if (m.y() < dragSelTo->marginTop()) {
							dragSelTo = (dragSelFrom == dragSelTo) ? 0 : prevItem(dragSelTo);
						}
					} else {
						if (m.y() >= dragSelTo->height() - dragSelTo->marginBottom()) {
							dragSelTo = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelTo);
						}
					}
				}
				auto dragSelecting = false;
				auto dragFirstAffected = dragSelFrom;
				while (dragFirstAffected && (dragFirstAffected->id < 0 || dragFirstAffected->serviceMsg())) {
					dragFirstAffected = (dragFirstAffected == dragSelTo) ? 0 : (selectingDown ? nextItem(dragFirstAffected) : prevItem(dragFirstAffected));
				}
				if (dragFirstAffected) {
					auto i = _selected.find(dragFirstAffected);
					dragSelecting = (i == _selected.cend() || i->second != FullSelection);
				}
				updateDragSelection(dragSelFrom, dragSelTo, dragSelecting);
			}
		} else if (_mouseAction == MouseAction::Dragging) {
		}

		if (ClickHandler::getPressed()) {
			cur = style::cur_pointer;
		} else if (_mouseAction == MouseAction::Selecting && !_selected.empty() && _selected.cbegin()->second != FullSelection) {
			if (!_dragSelFrom || !_dragSelTo) {
				cur = style::cur_text;
			}
		}
	}

	// Voice message seek support.
	if (auto pressedItem = App::pressedLinkItem()) {
		if (!pressedItem->detached()) {
			if (pressedItem->history() == _history || pressedItem->history() == _migrated) {
				auto adjustedPoint = mapPointToItem(point, pressedItem);
				pressedItem->updatePressed(adjustedPoint);
			}
		}
	}

	if (_mouseAction == MouseAction::Selecting) {
		_widget->checkSelectingScroll(mousePos);
	} else {
		updateDragSelection(0, 0, false);
		_widget->noSelectingScroll();
	}

	if (_mouseAction == MouseAction::None && (lnkChanged || cur != _cursor)) {
		setCursor(_cursor = cur);
	}
}

void HistoryInner::updateDragSelection(HistoryItem *dragSelFrom, HistoryItem *dragSelTo, bool dragSelecting) {
	if (_dragSelFrom == dragSelFrom && _dragSelTo == dragSelTo && _dragSelecting == dragSelecting) {
		return;
	}
	_dragSelFrom = dragSelFrom;
	_dragSelTo = dragSelTo;
	int32 fromy = itemTop(_dragSelFrom), toy = itemTop(_dragSelTo);
	if (fromy >= 0 && toy >= 0 && fromy > toy) {
		qSwap(_dragSelFrom, _dragSelTo);
	}
	_dragSelecting = dragSelecting;
	if (!_wasSelectedText && _dragSelFrom && _dragSelTo && _dragSelecting) {
		_wasSelectedText = true;
		setFocus();
	}
	update();
}

void HistoryInner::BotAbout::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	_parent->update(rect);
}

void HistoryInner::BotAbout::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	_parent->update(rect);
}

int HistoryInner::historyHeight() const {
	int result = 0;
	if (!_history || _history->isEmpty()) {
		result += _migrated ? _migrated->height : 0;
	} else {
		result += _history->height - _historySkipHeight + (_migrated ? _migrated->height : 0);
	}
	return result;
}

int HistoryInner::historyScrollTop() const {
	auto htop = historyTop();
	auto mtop = migratedTop();
	if (htop >= 0 && _history->scrollTopItem) {
		Assert(!_history->scrollTopItem->detached());
		return htop + _history->scrollTopItem->block()->y() + _history->scrollTopItem->y() + _history->scrollTopOffset;
	}
	if (mtop >= 0 && _migrated->scrollTopItem) {
		Assert(!_migrated->scrollTopItem->detached());
		return mtop + _migrated->scrollTopItem->block()->y() + _migrated->scrollTopItem->y() + _migrated->scrollTopOffset;
	}
	return ScrollMax;
}

int HistoryInner::migratedTop() const {
	return (_migrated && !_migrated->isEmpty()) ? _historyPaddingTop : -1;
}

int HistoryInner::historyTop() const {
	int mig = migratedTop();
	return (_history && !_history->isEmpty()) ? (mig >= 0 ? (mig + _migrated->height - _historySkipHeight) : _historyPaddingTop) : -1;
}

int HistoryInner::historyDrawTop() const {
	auto top = historyTop();
	return (top >= 0) ? (top + _historySkipHeight) : -1;
}

int HistoryInner::itemTop(const HistoryItem *item) const { // -1 if should not be visible, -2 if bad history()
	if (!item) return -2;
	if (item->detached()) return -1;

	auto top = (item->history() == _history) ? historyTop() : (item->history() == _migrated ? migratedTop() : -2);
	return (top < 0) ? top : (top + item->y() + item->block()->y());
}

void HistoryInner::notifyIsBotChanged() {
	BotInfo *newinfo = (_history && _history->peer->isUser()) ? _history->peer->asUser()->botInfo.get() : nullptr;
	if ((!newinfo && !_botAbout) || (newinfo && _botAbout && _botAbout->info == newinfo)) {
		return;
	}

	if (newinfo) {
		_botAbout.reset(new BotAbout(this, newinfo));
		if (newinfo && !newinfo->inited) {
			Auth().api().requestFullPeer(_peer);
		}
	} else {
		_botAbout = nullptr;
	}
}

void HistoryInner::notifyMigrateUpdated() {
	_migrated = _history->migrateFrom();
}

int HistoryInner::moveScrollFollowingInlineKeyboard(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	if (item == App::mousedItem()) {
		int top = itemTop(item);
		if (top >= oldKeyboardTop) {
			return newKeyboardTop - oldKeyboardTop;
		}
	}
	return 0;
}

void HistoryInner::applyDragSelection() {
	applyDragSelection(&_selected);
}

void HistoryInner::addSelectionRange(SelectedItems *toItems, int32 fromblock, int32 fromitem, int32 toblock, int32 toitem, History *h) const {
	if (fromblock >= 0 && fromitem >= 0 && toblock >= 0 && toitem >= 0) {
		for (; fromblock <= toblock; ++fromblock) {
			auto block = h->blocks[fromblock];
			for (int32 cnt = (fromblock < toblock) ? block->items.size() : (toitem + 1); fromitem < cnt; ++fromitem) {
				auto item = block->items[fromitem];
				auto i = toItems->find(item);
				if (item->id > 0 && !item->serviceMsg()) {
					if (i == toItems->cend()) {
						if (toItems->size() >= MaxSelectedItems) break;
						toItems->emplace(item, FullSelection);
					} else if (i->second != FullSelection) {
						i->second = FullSelection;
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
}

void HistoryInner::applyDragSelection(SelectedItems *toItems) const {
	int32 selfromy = itemTop(_dragSelFrom), seltoy = itemTop(_dragSelTo);
	if (selfromy < 0 || seltoy < 0) {
		return;
	}
	seltoy += _dragSelTo->height();

	if (!toItems->empty() && toItems->cbegin()->second != FullSelection) {
		toItems->clear();
	}
	if (_dragSelecting) {
		int32 fromblock = _dragSelFrom->block()->indexInHistory(), fromitem = _dragSelFrom->indexInBlock();
		int32 toblock = _dragSelTo->block()->indexInHistory(), toitem = _dragSelTo->indexInBlock();
		if (_migrated) {
			if (_dragSelFrom->history() == _migrated) {
				if (_dragSelTo->history() == _migrated) {
					addSelectionRange(toItems, fromblock, fromitem, toblock, toitem, _migrated);
					toblock = -1;
					toitem = -1;
				} else {
					addSelectionRange(toItems, fromblock, fromitem, _migrated->blocks.size() - 1, _migrated->blocks.back()->items.size() - 1, _migrated);
				}
				fromblock = 0;
				fromitem = 0;
			} else if (_dragSelTo->history() == _migrated) { // wtf
				toblock = -1;
				toitem = -1;
			}
		}
		addSelectionRange(toItems, fromblock, fromitem, toblock, toitem, _history);
	} else {
		for (auto i = toItems->begin(); i != toItems->cend();) {
			auto iy = itemTop(i->first);
			if (iy < 0) {
				if (iy < -1) i = toItems->erase(i);
				continue;
			}
			if (iy >= selfromy && iy < seltoy) {
				i = toItems->erase(i);
			} else {
				++i;
			}
		}
	}
}

QString HistoryInner::tooltipText() const {
	if (_mouseCursorState == HistoryInDateCursorState && _mouseAction == MouseAction::None) {
		if (App::hoveredItem()) {
			auto dateText = App::hoveredItem()->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat));
			if (auto edited = App::hoveredItem()->Get<HistoryMessageEdited>()) {
				dateText += '\n' + lng_edited_date(lt_date, edited->_editDate.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat)));
			}
			if (auto forwarded = App::hoveredItem()->Get<HistoryMessageForwarded>()) {
				dateText += '\n' + lng_forwarded_date(lt_date, forwarded->_originalDate.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat)));
			}
			return dateText;
		}
	} else if (_mouseCursorState == HistoryInForwardedCursorState && _mouseAction == MouseAction::None) {
		if (App::hoveredItem()) {
			if (auto forwarded = App::hoveredItem()->Get<HistoryMessageForwarded>()) {
				return forwarded->_text.originalText(AllTextSelection, ExpandLinksNone);
			}
		}
	} else if (auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

QPoint HistoryInner::tooltipPos() const {
	return _mousePosition;
}

void HistoryInner::onParentGeometryChanged() {
	auto mousePos = QCursor::pos();
	auto mouseOver = _widget->rect().contains(_widget->mapFromGlobal(mousePos));
	auto needToUpdate = (_mouseAction != MouseAction::None || _touchScroll || mouseOver);
	if (needToUpdate) {
		mouseActionUpdate(mousePos);
	}
}
