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

#include "styles/style_history.h"
#include "core/file_utilities.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "chat_helpers/message_field.h"
#include "historywidget.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "lang.h"

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
		App::main()->showJumpToDate(_peer, _date);
	}

private:
	PeerData *_peer = nullptr;
	QDate _date;

};

QMimeData *mimeDataFromTextWithEntities(const TextWithEntities &forClipboard) {
	if (forClipboard.text.isEmpty()) {
		return nullptr;
	}

	auto result = new QMimeData();
	result->setText(forClipboard.text);
	auto tags = ConvertEntitiesToTextTags(forClipboard.entities);
	if (!tags.isEmpty()) {
		for (auto &tag : tags) {
			tag.id = ConvertTagToMimeTag(tag.id);
		}
		result->setData(Ui::FlatTextarea::tagsMimeType(), Ui::FlatTextarea::serializeTagsList(tags));
	}
	return result;
}

} // namespace

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

HistoryInner::HistoryInner(HistoryWidget *historyWidget, gsl::not_null<Window::Controller*> controller, Ui::ScrollArea *scroll, History *history) : TWidget(nullptr)
, _controller(controller)
, _peer(history->peer)
, _migrated(history->peer->migrateFrom() ? App::history(history->peer->migrateFrom()->id) : nullptr)
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
	subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
		itemRemoved(item);
	});
	subscribe(_controller->gifPauseLevelChanged(), [this] {
		if (!_controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any)) {
			update();
		}
	});
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

namespace {

// helper binary search for an item in a list that is not completely
// above the given top of the visible area or below the given bottom of the visible area
// is applied once for blocks list in a history and once for items list in the found block
template <bool TopToBottom, typename T>
int binarySearchBlocksOrItems(const T &list, int edge) {
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

template <bool TopToBottom, typename Method>
void HistoryInner::enumerateItemsInHistory(History *history, int historytop, Method method) {
	// no displayed messages in this history
	if (historytop < 0 || history->isEmpty()) {
		return;
	}
	if (_visibleAreaBottom <= historytop || historytop + history->height <= _visibleAreaTop) {
		return;
	}

	auto searchEdge = TopToBottom ? _visibleAreaTop : _visibleAreaBottom;

	// binary search for blockIndex of the first block that is not completely below the visible area
	auto blockIndex = binarySearchBlocksOrItems<TopToBottom>(history->blocks, searchEdge - historytop);

	// binary search for itemIndex of the first item that is not completely below the visible area
	auto block = history->blocks.at(blockIndex);
	auto blocktop = historytop + block->y();
	auto blockbottom = blocktop + block->height();
	auto itemIndex = binarySearchBlocksOrItems<TopToBottom>(block->items, searchEdge - blocktop);

	while (true) {
		while (true) {
			auto item = block->items.at(itemIndex);
			auto itemtop = blocktop + item->y();
			auto itembottom = itemtop + item->height();

			// binary search should've skipped all the items that are above / below the visible area
			if (TopToBottom) {
				if (itembottom <= _visibleAreaTop && (cAlphaVersion() || cBetaVersion())) {
					// Debugging a crash
					auto debugInfo = QStringList();
					auto debugValue = [&debugInfo](const QString &name, int value) {
						debugInfo.append(name + ":" + QString::number(value));
					};
					debugValue("historytop", historytop);
					debugValue("history->height", history->height);
					debugValue("blockIndex", blockIndex);
					debugValue("history->blocks.size()", history->blocks.size());
					debugValue("blocktop", blocktop);
					debugValue("block->height", block->height());
					debugValue("itemIndex", itemIndex);
					debugValue("block->items.size()", block->items.size());
					debugValue("itemtop", itemtop);
					debugValue("item->height()", item->height());
					debugValue("itembottom", itembottom);
					debugValue("_visibleAreaTop", _visibleAreaTop);
					debugValue("_visibleAreaBottom", _visibleAreaBottom);
					for (int i = 0; i != qMin(history->blocks.size(), 5); ++i) {
						debugValue("y[" + QString::number(i) + "]", history->blocks[i]->y());
						debugValue("h[" + QString::number(i) + "]", history->blocks[i]->height());
						for (int j = 0; j != qMin(history->blocks[i]->items.size(), 5); ++j) {
							debugValue("y[" + QString::number(i) + "][" + QString::number(j) + "]", history->blocks[i]->items[j]->y());
							debugValue("h[" + QString::number(i) + "][" + QString::number(j) + "]", history->blocks[i]->items[j]->height());
						}
					}
					auto valid = [history, &debugInfo] {
						auto y = 0;
						for (int i = 0; i != history->blocks.size(); ++i) {
							auto innery = 0;
							if (history->blocks[i]->y() != y) {
								debugInfo.append("bad_block_y" + QString::number(i) + ":" + QString::number(history->blocks[i]->y()) + "!=" + QString::number(y));
								return false;
							}
							for (int j = 0; j != history->blocks[i]->items.size(); ++j) {
								if (history->blocks[i]->items[j]->pendingInitDimensions()) {
									debugInfo.append("pending_item_init" + QString::number(i) + "," + QString::number(j));
								} else if (history->blocks[i]->items[j]->pendingResize()) {
									debugInfo.append("pending_resize" + QString::number(i) + "," + QString::number(j));
								}
								if (history->blocks[i]->items[j]->y() != innery) {
									debugInfo.append("bad_item_y" + QString::number(i) + "," + QString::number(j) + ":" + QString::number(history->blocks[i]->items[j]->y()) + "!=" + QString::number(innery));
									return false;
								}
								innery += history->blocks[i]->items[j]->height();
							}
							if (history->blocks[i]->height() != innery) {
								debugInfo.append("bad_block_height" + QString::number(i) + ":" + QString::number(history->blocks[i]->height()) + "!=" + QString::number(innery));
								return false;
							}
							y += innery;
						}
						return true;
					};
					if (!valid()) {
						debugValue("pending_init", history->hasPendingResizedItems() ? 1 : 0);
					}
					SignalHandlers::setCrashAnnotation("DebugInfo", debugInfo.join(','));
				}
				t_assert(itembottom > _visibleAreaTop);
			} else {
				t_assert(itemtop < _visibleAreaBottom);
			}

			if (!method(item, itemtop, itembottom)) {
				return;
			}

			// skip all the items that are below / above the visible area
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

		// skip all the rest blocks that are below / above the visible area
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

	// find and remember the top of an attached messages pack
	// -1 means we didn't find an attached to next message yet
	int lowestAttachedItemTop = -1;

	auto userpicCallback = [this, &lowestAttachedItemTop, &method](HistoryItem *item, int itemtop, int itembottom) {
		// skip all service messages
		auto message = item->toHistoryMessage();
		if (!message) return true;

		if (lowestAttachedItemTop < 0 && message->isAttachedToNext()) {
			lowestAttachedItemTop = itemtop + message->marginTop();
		}

		// call method on a userpic for all messages that have it and for those who are not showing it
		// because of their attachment to the next message if they are bottom-most visible
		if (message->displayFromPhoto() || (message->hasFromPhoto() && itembottom >= _visibleAreaBottom)) {
			if (lowestAttachedItemTop < 0) {
				lowestAttachedItemTop = itemtop + message->marginTop();
			}
			// attach userpic to the bottom of the visible area with the same margin as the last message
			auto userpicMinBottomSkip = st::historyPaddingBottom + st::msgMargin.bottom();
			auto userpicBottom = qMin(itembottom - message->marginBottom(), _visibleAreaBottom - userpicMinBottomSkip);

			// do not let the userpic go above the attached messages pack top line
			userpicBottom = qMax(userpicBottom, lowestAttachedItemTop + st::msgPhotoSize);

			// call the template callback function that was passed
			// and return if it finished everything it needed
			if (!method(message, userpicBottom - st::msgPhotoSize)) {
				return false;
			}
		}

		// forget the found top of the pack, search for the next one from scratch
		if (!message->isAttachedToNext()) {
			lowestAttachedItemTop = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::TopToBottom>(userpicCallback);
}

template <typename Method>
void HistoryInner::enumerateDates(Method method) {
	int drawtop = historyDrawTop();

	// find and remember the bottom of an single-day messages pack
	// -1 means we didn't find a same-day with previous message yet
	int lowestInOneDayItemBottom = -1;

	auto dateCallback = [this, &lowestInOneDayItemBottom, &method, drawtop](HistoryItem *item, int itemtop, int itembottom) {
		if (lowestInOneDayItemBottom < 0 && item->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = itembottom - item->marginBottom();
		}

		// call method on a date for all messages that have it and for those who are not showing it
		// because they are in a one day together with the previous message if they are top-most visible
		if (item->displayDate() || (!item->isEmpty() && itemtop <= _visibleAreaTop)) {
			// skip the date of history migrate item if it will be in migrated
			if (itemtop < drawtop && item->history() == _history) {
				if (itemtop > _visibleAreaTop) {
					// previous item (from the _migrated history) is drawing date now
					return false;
				} else if (item == _history->blocks.front()->items.front() && item->isGroupMigrate()
					&& _migrated->blocks.back()->items.back()->isGroupMigrate()) {
					// this item is completely invisible and should be completely ignored
					return false;
				}
			}

			if (lowestInOneDayItemBottom < 0) {
				lowestInOneDayItemBottom = itembottom - item->marginBottom();
			}
			// attach date to the top of the visible area with the same margin as it has in service message
			int dateTop = qMax(itemtop, _visibleAreaTop) + st::msgServiceMargin.top();

			// do not let the date go below the single-day messages pack bottom line
			int dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			dateTop = qMin(dateTop, lowestInOneDayItemBottom - dateHeight);

			// call the template callback function that was passed
			// and return if it finished everything it needed
			if (!method(item, itemtop, dateTop)) {
				return false;
			}
		}

		// forget the found bottom of the pack, search for the next one from scratch
		if (!item->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::BottomToTop>(dateCallback);
}

void HistoryInner::paintEvent(QPaintEvent *e) {
	if (!App::main() || (App::wnd() && App::wnd()->contentOverlapped(this, e))) {
		return;
	}
	if (hasPendingResizedItems()) {
		return;
	}

	Painter p(this);
	QRect r(e->rect());
	bool trivial = (rect() == r);
	if (!trivial) {
		p.setClipRect(r);
	}
	auto ms = getms();

	bool historyDisplayedEmpty = (_history->isDisplayedEmpty() && (!_migrated || _migrated->isDisplayedEmpty()));
	bool noHistoryDisplayed = _firstLoading || historyDisplayedEmpty;
	if (!_firstLoading && _botAbout && !_botAbout->info->text.isEmpty() && _botAbout->height > 0) {
		if (r.y() < _botAbout->rect.y() + _botAbout->rect.height() && r.y() + r.height() > _botAbout->rect.y()) {
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
		adjustCurrent(r.top());

		SelectedItems::const_iterator selEnd = _selected.cend();
		bool hasSel = !_selected.isEmpty();

		int32 drawToY = r.y() + r.height();

		int32 selfromy = itemTop(_dragSelFrom), seltoy = itemTop(_dragSelTo);
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
			if (r.y() < y + item->height()) while (y < drawToY) {
				TextSelection sel;
				if (y >= selfromy && y < seltoy) {
					if (_dragSelecting && !item->serviceMsg() && item->id > 0) {
						sel = FullSelection;
					}
				} else if (hasSel) {
					auto i = _selected.constFind(item);
					if (i != selEnd) {
						sel = i.value();
					}
				}
				item->draw(p, r.translated(0, -y), sel, ms);

				if (item->hasViews()) {
					App::main()->scheduleViewIncrement(item);
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

			auto historyRect = r.intersected(QRect(0, hdrawtop, width(), r.top() + r.height()));
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
						auto i = _selected.constFind(item);
						if (i != selEnd) {
							sel = i.value();
						}
					}
					item->draw(p, historyRect.translated(0, -y), sel, ms);

					if (item->hasViews()) {
						App::main()->scheduleViewIncrement(item);
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

		if (mtop >= 0 || htop >= 0) {
			enumerateUserpics([&p, &r](HistoryMessage *message, int userpicTop) {
				// stop the enumeration if the userpic is below the painted rect
				if (userpicTop >= r.top() + r.height()) {
					return false;
				}

				// paint the userpic if it intersects the painted rect
				if (userpicTop + st::msgPhotoSize > r.top()) {
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
			enumerateDates([&p, &r, scrollDateOpacity, dateHeight/*, lastDate, showFloatingBefore*/](HistoryItem *item, int itemtop, int dateTop) {
				// stop the enumeration if the date is above the painted rect
				if (dateTop + dateHeight <= r.top()) {
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
				if (dateTop < r.top() + r.height()) {
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

bool HistoryInner::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return QWidget::event(e);
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

void HistoryInner::mouseMoveEvent(QMouseEvent *e) {
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _dragAction != NoDrag) {
		mouseReleaseEvent(e);
	}
	if (!buttonsPressed || ClickHandler::getPressed() == _scrollDateLink) {
		keepScrollDateForNow();
	}
	dragActionUpdate(e->globalPos());
}

void HistoryInner::dragActionUpdate(const QPoint &screenPos) {
	_dragPos = screenPos;
	onUpdateSelected();
}

void HistoryInner::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	_widget->touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

QPoint HistoryInner::mapMouseToItem(QPoint p, HistoryItem *item) {
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
	dragActionStart(e->globalPos(), e->button());
}

void HistoryInner::dragActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	dragActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	ClickHandler::pressed();
	if (App::pressedItem() != App::hoveredItem()) {
		repaintItem(App::pressedItem());
		App::pressedItem(App::hoveredItem());
		repaintItem(App::pressedItem());
	}

	_dragAction = NoDrag;
	_dragItem = App::mousedItem();
	_dragStartPos = mapMouseToItem(mapFromGlobal(screenPos), _dragItem);
	_dragWasInactive = App::wnd()->inactivePress();
	if (_dragWasInactive) App::wnd()->inactivePress(false);

	if (ClickHandler::getPressed()) {
		_dragAction = PrepareDrag;
	} else if (!_selected.isEmpty()) {
		if (_selected.cbegin().value() == FullSelection) {
			if (_selected.constFind(_dragItem) != _selected.cend() && App::hoveredItem()) {
				_dragAction = PrepareDrag; // start items drag
			} else if (!_dragWasInactive) {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}
	if (_dragAction == NoDrag && _dragItem) {
		HistoryTextState dragState;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _dragItem->getState(_dragStartPos.x(), _dragStartPos.y(), request);
			if (dragState.cursor == HistoryInTextCursorState) {
				TextSelection selStatus = { dragState.symbol, dragState.symbol };
				if (selStatus != FullSelection && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection)) {
					if (!_selected.isEmpty()) {
						repaintItem(_selected.cbegin().key());
						_selected.clear();
					}
					_selected.insert(_dragItem, selStatus);
					_dragSymbol = dragState.symbol;
					_dragAction = Selecting;
					_dragSelType = TextSelectType::Paragraphs;
					dragActionUpdate(_dragPos);
					_trippleClickTimer.start(QApplication::doubleClickInterval());
				}
			}
		} else if (App::pressedItem()) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _dragItem->getState(_dragStartPos.x(), _dragStartPos.y(), request);
		}
		if (_dragSelType != TextSelectType::Paragraphs) {
			if (App::pressedItem()) {
				_dragSymbol = dragState.symbol;
				bool uponSelected = (dragState.cursor == HistoryInTextCursorState);
				if (uponSelected) {
					if (_selected.isEmpty() ||
						_selected.cbegin().value() == FullSelection ||
						_selected.cbegin().key() != _dragItem
						) {
						uponSelected = false;
					} else {
						uint16 selFrom = _selected.cbegin().value().from, selTo = _selected.cbegin().value().to;
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
						if (dragState.afterSymbol) ++_dragSymbol;
						TextSelection selStatus = { _dragSymbol, _dragSymbol };
						if (selStatus != FullSelection && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection)) {
							if (!_selected.isEmpty()) {
								repaintItem(_selected.cbegin().key());
								_selected.clear();
							}
							_selected.insert(_dragItem, selStatus);
							_dragAction = Selecting;
							repaintItem(_dragItem);
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
		_dragItem = nullptr;
	}
}

void HistoryInner::dragActionCancel() {
	_dragItem = 0;
	_dragAction = NoDrag;
	_dragStartPos = QPoint(0, 0);
	_dragSelFrom = _dragSelTo = 0;
	_wasSelectedText = false;
	_widget->noSelectingScroll();
}

void HistoryInner::onDragExec() {
	if (_dragAction != Dragging) return;

	bool uponSelected = false;
	if (_dragItem) {
		if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
			uponSelected = _selected.contains(_dragItem);
		} else {
			HistoryStateRequest request;
			request.flags |= Text::StateRequest::Flag::LookupSymbol;
			auto dragState = _dragItem->getState(_dragStartPos.x(), _dragStartPos.y(), request);
			uponSelected = (dragState.cursor == HistoryInTextCursorState);
			if (uponSelected) {
				if (_selected.isEmpty() ||
					_selected.cbegin().value() == FullSelection ||
					_selected.cbegin().key() != _dragItem
					) {
					uponSelected = false;
				} else {
					uint16 selFrom = _selected.cbegin().value().from, selTo = _selected.cbegin().value().to;
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
	if (auto mimeData = mimeDataFromTextWithEntities(sel)) {
		updateDragSelection(0, 0, false);
		_widget->noSelectingScroll();

		auto drag = std::make_unique<QDrag>(App::wnd());
		if (!urls.isEmpty()) mimeData->setUrls(urls);
		if (uponSelected && !Adaptive::OneColumn()) {
			auto selectedState = getSelectionState();
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				mimeData->setData(qsl("application/x-td-forward-selected"), "1");
			}
		}
		drag->setMimeData(mimeData);
		drag->exec(Qt::CopyAction);

		// We don't receive mouseReleaseEvent when drag is finished.
		ClickHandler::unpressed();
		if (App::main()) App::main()->updateAfterDrag();
		return;
	} else {
		auto forwardMimeType = QString();
		auto pressedMedia = static_cast<HistoryMedia*>(nullptr);
		if (auto pressedItem = App::pressedItem()) {
			pressedMedia = pressedItem->getMedia();
			if (_dragCursorState == HistoryInDateCursorState || (pressedMedia && pressedMedia->dragItem())) {
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
			auto drag = std::make_unique<QDrag>(App::wnd());
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

			drag->setMimeData(mimeData.release());
			drag->exec(Qt::CopyAction);

			// We don't receive mouseReleaseEvent when drag is finished.
			ClickHandler::unpressed();
			if (App::main()) App::main()->updateAfterDrag();
			return;
		}
	}
}

void HistoryInner::itemRemoved(HistoryItem *item) {
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

	if (_dragItem == item) {
		dragActionCancel();
	}

	if (_dragSelFrom == item || _dragSelTo == item) {
		_dragSelFrom = 0;
		_dragSelTo = 0;
		update();
	}
	onUpdateSelected();
}

void HistoryInner::dragActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	dragActionUpdate(screenPos);

	ClickHandlerPtr activated = ClickHandler::unpressed();
	if (_dragAction == Dragging) {
		activated.clear();
	} else if (HistoryItem *pressed = App::pressedLinkItem()) {
		// if we are in selecting items mode perhaps we want to
		// toggle selection instead of activating the pressed link
		if (_dragAction == PrepareDrag && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection && button != Qt::RightButton) {
			if (HistoryMedia *media = pressed->getMedia()) {
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
		dragActionCancel();
		App::activateClickHandler(activated, button);
		return;
	}
	if (_dragAction == PrepareSelect && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i == _selected.cend() && !_dragItem->serviceMsg() && _dragItem->id > 0) {
			if (_selected.size() < MaxSelectedItems) {
				if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
					_selected.clear();
				}
				_selected.insert(_dragItem, FullSelection);
			}
		} else {
			_selected.erase(i);
		}
		repaintItem(_dragItem);
	} else if (_dragAction == PrepareDrag && !_dragWasInactive && button != Qt::RightButton) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i != _selected.cend() && i.value() == FullSelection) {
			_selected.erase(i);
			repaintItem(_dragItem);
		} else if (i == _selected.cend() && !_dragItem->serviceMsg() && _dragItem->id > 0 && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.insert(_dragItem, FullSelection);
				repaintItem(_dragItem);
			}
		} else {
			_selected.clear();
			update();
		}
	} else if (_dragAction == Selecting) {
		if (_dragSelFrom && _dragSelTo) {
			applyDragSelection();
			_dragSelFrom = _dragSelTo = 0;
		} else if (!_selected.isEmpty() && !_dragWasInactive) {
			auto sel = _selected.cbegin().value();
			if (sel != FullSelection && sel.from == sel.to) {
				_selected.clear();
				if (App::wnd()) App::wnd()->setInnerFocus();
			}
		}
	}
	_dragAction = NoDrag;
	_dragItem = 0;
	_dragSelType = TextSelectType::Letters;
	_widget->noSelectingScroll();
	_widget->updateTopBarSelection();

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
		setToClipboard(_selected.cbegin().key()->selectedText(_selected.cbegin().value()), QClipboard::Selection);
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void HistoryInner::mouseReleaseEvent(QMouseEvent *e) {
	dragActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void HistoryInner::mouseDoubleClickEvent(QMouseEvent *e) {
	if (!_history) return;

	dragActionStart(e->globalPos(), e->button());
	if (((_dragAction == Selecting && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection) || (_dragAction == NoDrag && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection))) && _dragSelType == TextSelectType::Letters && _dragItem) {
		HistoryStateRequest request;
		request.flags |= Text::StateRequest::Flag::LookupSymbol;
		auto dragState = _dragItem->getState(_dragStartPos.x(), _dragStartPos.y(), request);
		if (dragState.cursor == HistoryInTextCursorState) {
			_dragSymbol = dragState.symbol;
			_dragSelType = TextSelectType::Words;
			if (_dragAction == NoDrag) {
				_dragAction = Selecting;
				TextSelection selStatus = { dragState.symbol, dragState.symbol };
				if (!_selected.isEmpty()) {
					repaintItem(_selected.cbegin().key());
					_selected.clear();
				}
				_selected.insert(_dragItem, selStatus);
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
		_menu = 0;
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		dragActionUpdate(e->globalPos());
	}

	auto selectedState = getSelectionState();
	auto canSendMessages = _widget->canSendMessages(_peer);

	// -2 - has full selected items, but not over, -1 - has selection, but no over, 0 - no selection, 1 - over text, 2 - over full selected items
	int32 isUponSelected = 0, hasSelected = 0;;
	if (!_selected.isEmpty()) {
		isUponSelected = -1;
		if (_selected.cbegin().value() == FullSelection) {
			hasSelected = 2;
			if (App::hoveredItem() && _selected.constFind(App::hoveredItem()) != _selected.cend()) {
				isUponSelected = 2;
			} else {
				isUponSelected = -2;
			}
		} else {
			uint16 selFrom = _selected.cbegin().value().from, selTo = _selected.cbegin().value().to;
			hasSelected = (selTo > selFrom) ? 1 : 0;
			if (App::mousedItem() && App::mousedItem() == App::hoveredItem()) {
				QPoint mousePos(mapMouseToItem(mapFromGlobal(_dragPos), App::mousedItem()));
				HistoryStateRequest request;
				request.flags |= Text::StateRequest::Flag::LookupSymbol;
				auto dragState = App::mousedItem()->getState(mousePos.x(), mousePos.y(), request);
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

	_contextMenuLnk = ClickHandler::getActive();
	HistoryItem *item = App::hoveredItem() ? App::hoveredItem() : App::hoveredLinkItem();
	PhotoClickHandler *lnkPhoto = dynamic_cast<PhotoClickHandler*>(_contextMenuLnk.data());
	DocumentClickHandler *lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLnk.data());
	bool lnkIsVideo = lnkDocument ? lnkDocument->document()->isVideo() : false;
	bool lnkIsAudio = lnkDocument ? (lnkDocument->document()->voice() != nullptr) : false;
	bool lnkIsSong = lnkDocument ? (lnkDocument->document()->song() != nullptr) : false;
	if (lnkPhoto || lnkDocument) {
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
		}
		if (item && item->id > 0 && isUponSelected != 2 && isUponSelected != -2) {
			if (canSendMessages) {
				_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
			}
			if (item->canEdit(::date(unixtime()))) {
				_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
			}
			if (item->canPin()) {
				bool ispinned = (item->history()->peer->asChannel()->mgInfo->pinnedMsgId == item->id);
				_menu->addAction(lang(ispinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, ispinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
			}
		}
		if (lnkPhoto) {
			_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, photo = lnkPhoto->photo()] {
				savePhotoToFile(photo);
			}))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_image), this, SLOT(copyContextImage()))->setEnabled(true);
		} else {
			auto document = lnkDocument->document();
			if (document->loading()) {
				_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
			} else {
				if (document->loaded() && document->isGifv()) {
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
			_menu->addAction(lang(lng_context_copy_post_link), _widget, SLOT(onCopyPostLink()));
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
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
			if (item && item->id > 0 && isUponSelected != 2) {
				if (canSendMessages) {
					_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
				}
				if (item->canEdit(::date(unixtime()))) {
					_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
				}
				if (item->canPin()) {
					bool ispinned = (item->history()->peer->asChannel()->mgInfo->pinnedMsgId == item->id);
					_menu->addAction(lang(ispinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, ispinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
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
					bool ispinned = (item->history()->peer->asChannel()->mgInfo->pinnedMsgId == item->id);
					_menu->addAction(lang(ispinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, ispinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
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
								_menu->addAction(lang(document->sticker()->setInstalled() ? lng_context_pack_info : lng_context_pack_add), _widget, SLOT(onStickerPackInfo()));
							}
							_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
								saveDocumentToFile(document);
							}))->setEnabled(true);
						}
					} else if (media->type() == MediaTypeGif && !_contextMenuLnk) {
						if (auto document = media->getDocument()) {
							if (document->loading()) {
								_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
							} else {
								if (document->isGifv()) {
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
				if (msg && !_contextMenuLnk && (!msg->emptyText() || mediaHasTextForCopy)) {
					_menu->addAction(lang(lng_context_copy_text), this, SLOT(copyContextText()))->setEnabled(true);
				}
			}
		}

		auto linkCopyToClipboardText = _contextMenuLnk ? _contextMenuLnk->copyToClipboardContextItemText() : QString();
		if (!linkCopyToClipboardText.isEmpty()) {
			_menu->addAction(linkCopyToClipboardText, this, SLOT(copyContextUrl()))->setEnabled(true);
		}
		if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(lang(lng_context_copy_post_link), _widget, SLOT(onCopyPostLink()));
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
	if (_contextMenuLnk) {
		_contextMenuLnk->copyToClipboard();
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

void HistoryInner::copyContextImage() {
	PhotoClickHandler *lnk = dynamic_cast<PhotoClickHandler*>(_contextMenuLnk.data());
	if (!lnk) return;

	PhotoData *photo = lnk->photo();
	if (!photo || !photo->date || !photo->loaded()) return;

	QApplication::clipboard()->setPixmap(photo->full->pix());
}

void HistoryInner::cancelContextDownload() {
	if (DocumentClickHandler *lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLnk.data())) {
		lnkDocument->document()->cancel();
	} else if (HistoryItem *item = App::contextItem()) {
		if (HistoryMedia *media = item->getMedia()) {
			if (DocumentData *doc = media->getDocument()) {
				doc->cancel();
			}
		}
	}
}

void HistoryInner::showContextInFolder() {
	QString filepath;
	if (DocumentClickHandler *lnkDocument = dynamic_cast<DocumentClickHandler*>(_contextMenuLnk.data())) {
		filepath = lnkDocument->document()->filepath(DocumentData::FilePathResolveChecked);
	} else if (HistoryItem *item = App::contextItem()) {
		if (HistoryMedia *media = item->getMedia()) {
			if (DocumentData *doc = media->getDocument()) {
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
	if (auto data = mimeDataFromTextWithEntities(forClipboard)) {
		QApplication::clipboard()->setMimeData(data, mode);
	}
}

void HistoryInner::resizeEvent(QResizeEvent *e) {
	onUpdateSelected();
}

TextWithEntities HistoryInner::getSelectedText() const {
	SelectedItems sel = _selected;

	if (_dragAction == Selecting && _dragSelFrom && _dragSelTo) {
		applyDragSelection(&sel);
	}

	if (sel.isEmpty()) {
		return TextWithEntities();
	}
	if (sel.cbegin().value() != FullSelection) {
		return sel.cbegin().key()->selectedText(sel.cbegin().value());
	}

	int fullSize = 0;
	QString timeFormat(qsl(", [dd.MM.yy hh:mm]\n"));
	QMap<int, TextWithEntities> texts;
	for (auto i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		HistoryItem *item = i.key();
		if (item->detached()) continue;

		QString time = item->date.toString(timeFormat);
		TextWithEntities part, unwrapped = item->selectedText(FullSelection);
		int size = item->author()->name.size() + time.size() + unwrapped.text.size();
		part.text.reserve(size);

		int y = itemTop(item);
		if (y >= 0) {
			part.text.append(item->author()->name).append(time);
			appendTextWithEntities(part, std::move(unwrapped));
			texts.insert(y, part);
			fullSize += size;
		}
	}

	TextWithEntities result;
	auto sep = qsl("\n\n");
	result.text.reserve(fullSize + (texts.size() - 1) * sep.size());
	for (auto i = texts.begin(), e = texts.end(); i != e; ++i) {
		appendTextWithEntities(result, std::move(i.value()));
		if (i + 1 != e) {
			result.text.append(sep);
		}
	}
	return result;
}

void HistoryInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		_widget->onListEscapePressed();
	} else if (e == QKeySequence::Copy && !_selected.isEmpty()) {
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

void HistoryInner::recountHeight() {
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
	_scrollDateCheck.call();
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
	if (ClickHandler::getPressed() != _scrollDateLink) {
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

		dragActionUpdate(QCursor::pos());
	} else {
		update();
	}
}

void HistoryInner::enterEventHook(QEvent *e) {
	dragActionUpdate(QCursor::pos());
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
	_dragAction = NoDrag;
}

bool HistoryInner::focusNextPrevChild(bool next) {
	if (_selected.isEmpty()) {
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
	t_assert(!history->isEmpty());
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
	return !_selected.isEmpty();
}

bool HistoryInner::canDeleteSelected() const {
	auto selectedState = getSelectionState();
	return (selectedState.count > 0) && (selectedState.count == selectedState.canDeleteCount);
}

Window::TopBarWidget::SelectedState HistoryInner::getSelectionState() const {
	auto result = Window::TopBarWidget::SelectedState {};
	for (auto i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		if (i.value() == FullSelection) {
			++result.count;
			if (i.key()->canDelete()) {
				++result.canDeleteCount;
			}
			if (i.key()->canForward()) {
				++result.canForwardCount;
			}
		} else {
			result.textSelected = true;
		}
	}
	return result;
}

void HistoryInner::clearSelectedItems(bool onlyTextSelection) {
	if (!_selected.isEmpty() && (!onlyTextSelection || _selected.cbegin().value() != FullSelection)) {
		_selected.clear();
		_widget->updateTopBarSelection();
		_widget->update();
	}
}

void HistoryInner::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	if (_selected.isEmpty() || _selected.cbegin().value() != FullSelection) return;

	for (auto i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		auto item = i.key();
		if (item && item->toHistoryMessage() && item->id > 0) {
			if (item->history() == _migrated) {
				sel.insert(item->id - ServerMaxMsgId, item);
			} else {
				sel.insert(item->id, item);
			}
		}
	}
}

void HistoryInner::selectItem(HistoryItem *item) {
	if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
		_selected.clear();
	} else if (_selected.size() == MaxSelectedItems && _selected.constFind(item) == _selected.cend()) {
		return;
	}
	_selected.insert(item, FullSelection);
	_widget->updateTopBarSelection();
	_widget->update();
}

void HistoryInner::onTouchSelect() {
	_touchSelect = true;
	dragActionStart(_touchPos);
}

void HistoryInner::onUpdateSelected() {
	if (!_history || hasPendingResizedItems()) {
		return;
	}

	auto mousePos = mapFromGlobal(_dragPos);
	auto point = _widget->clampMousePosition(mousePos);

	HistoryBlock *block = 0;
	HistoryItem *item = 0;
	QPoint m;

	adjustCurrent(point.y());
	if (_curHistory && !_curHistory->isEmpty()) {
		block = _curHistory->blocks[_curBlock];
		item = block->items[_curItem];

		App::mousedItem(item);
		m = mapMouseToItem(point, item);
		if (item->hasPoint(m.x(), m.y())) {
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
	if (_dragItem && _dragItem->detached()) {
		dragActionCancel();
	}

	HistoryTextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	bool selectingText = (item == _dragItem && item == App::hoveredItem() && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection);
	if (point.y() < _historyPaddingTop) {
		if (_botAbout && !_botAbout->info->text.isEmpty() && _botAbout->height > 0) {
			dragState = _botAbout->info->text.getState(point.x() - _botAbout->rect.left() - st::msgPadding.left(), point.y() - _botAbout->rect.top() - st::msgPadding.top() - st::botDescSkip - st::msgNameFont->height, _botAbout->width);
			lnkhost = _botAbout.get();
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

		auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
		auto scrollDateOpacity = _scrollDateOpacity.current(_scrollDateShown ? 1. : 0.);
		enumerateDates([this, &dragState, &lnkhost, &point, scrollDateOpacity, dateHeight/*, lastDate, showFloatingBefore*/](HistoryItem *item, int itemtop, int dateTop) {
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
			if (_dragAction == Selecting) {
				request.flags |= Text::StateRequest::Flag::LookupSymbol;
			} else {
				selectingText = false;
			}
			dragState = item->getState(m.x(), m.y(), request);
			lnkhost = item;
			if (!dragState.link && m.x() >= st::historyPhotoLeft && m.x() < st::historyPhotoLeft + st::msgPhotoSize) {
				if (auto msg = item->toHistoryMessage()) {
					if (msg->hasFromPhoto()) {
						enumerateUserpics([&dragState, &lnkhost, &point](HistoryMessage *message, int userpicTop) -> bool {
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
	if (lnkChanged || dragState.cursor != _dragCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.link || dragState.cursor == HistoryInDateCursorState || dragState.cursor == HistoryInForwardedCursorState) {
		Ui::Tooltip::Show(1000, this);
	}

	Qt::CursorShape cur = style::cur_default;
	if (_dragAction == NoDrag) {
		_dragCursorState = dragState.cursor;
		if (dragState.link) {
			cur = style::cur_pointer;
		} else if (_dragCursorState == HistoryInTextCursorState && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection)) {
			cur = style::cur_text;
		} else if (_dragCursorState == HistoryInDateCursorState) {
			//			cur = style::cur_cross;
		}
	} else if (item) {
		if (_dragAction == Selecting) {
			auto canSelectMany = (_history != nullptr);
			if (selectingText) {
				uint16 second = dragState.symbol;
				if (dragState.afterSymbol && _dragSelType == TextSelectType::Letters) {
					++second;
				}
				auto selState = _dragItem->adjustSelection({ qMin(second, _dragSymbol), qMax(second, _dragSymbol) }, _dragSelType);
				if (_selected[_dragItem] != selState) {
					_selected[_dragItem] = selState;
					repaintItem(_dragItem);
				}
				if (!_wasSelectedText && (selState == FullSelection || selState.from != selState.to)) {
					_wasSelectedText = true;
					setFocus();
				}
				updateDragSelection(0, 0, false);
			} else if (canSelectMany) {
				auto selectingDown = (itemTop(_dragItem) < itemTop(item)) || (_dragItem == item && _dragStartPos.y() < m.y());
				auto dragSelFrom = _dragItem, dragSelTo = item;
				if (!dragSelFrom->hasPoint(_dragStartPos.x(), _dragStartPos.y())) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_dragStartPos.y() >= dragSelFrom->height() - dragSelFrom->marginBottom() || ((item == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance() || m.y() < dragSelFrom->marginTop()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelFrom);
						}
					} else {
						if (_dragStartPos.y() < dragSelFrom->marginTop() || ((item == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance() || m.y() >= dragSelFrom->height() - dragSelFrom->marginBottom()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : prevItem(dragSelFrom);
						}
					}
				}
				if (_dragItem != item) { // maybe exclude dragSelTo
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
					auto i = _selected.constFind(dragFirstAffected);
					dragSelecting = (i == _selected.cend() || i.value() != FullSelection);
				}
				updateDragSelection(dragSelFrom, dragSelTo, dragSelecting);
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

	// Voice message seek support.
	if (auto pressedItem = App::pressedLinkItem()) {
		if (!pressedItem->detached()) {
			if (pressedItem->history() == _history || pressedItem->history() == _migrated) {
				auto adjustedPoint = mapMouseToItem(point, pressedItem);
				pressedItem->updatePressed(adjustedPoint.x(), adjustedPoint.y());
			}
		}
	}

	if (_dragAction == Selecting) {
		_widget->checkSelectingScroll(mousePos);
	} else {
		updateDragSelection(0, 0, false);
		_widget->noSelectingScroll();
	}

	if (_dragAction == NoDrag && (lnkChanged || cur != _cursor)) {
		setCursor(_cursor = cur);
	}
}

void HistoryInner::updateDragSelection(HistoryItem *dragSelFrom, HistoryItem *dragSelTo, bool dragSelecting, bool force) {
	if (_dragSelFrom != dragSelFrom || _dragSelTo != dragSelTo || _dragSelecting != dragSelecting) {
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
		force = true;
	}
	if (!force) return;

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
		t_assert(!_history->scrollTopItem->detached());
		return htop + _history->scrollTopItem->block()->y() + _history->scrollTopItem->y() + _history->scrollTopOffset;
	}
	if (mtop >= 0 && _migrated->scrollTopItem) {
		t_assert(!_migrated->scrollTopItem->detached());
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
	int his = historyTop();
	return (his >= 0) ? (his + _historySkipHeight) : -1;
}

int HistoryInner::itemTop(const HistoryItem *item) const { // -1 if should not be visible, -2 if bad history()
	if (!item) return -2;
	if (item->detached()) return -1;

	int top = (item->history() == _history) ? historyTop() : (item->history() == _migrated ? migratedTop() : -2);
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
			AuthSession::Current().api().requestFullPeer(_peer);
		}
	} else {
		_botAbout = nullptr;
	}
}

void HistoryInner::notifyMigrateUpdated() {
	_migrated = _peer->migrateFrom() ? App::history(_peer->migrateFrom()->id) : 0;
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
			HistoryBlock *block = h->blocks[fromblock];
			for (int32 cnt = (fromblock < toblock) ? block->items.size() : (toitem + 1); fromitem < cnt; ++fromitem) {
				HistoryItem *item = block->items[fromitem];
				SelectedItems::iterator i = toItems->find(item);
				if (item->id > 0 && !item->serviceMsg()) {
					if (i == toItems->cend()) {
						if (toItems->size() >= MaxSelectedItems) break;
						toItems->insert(item, FullSelection);
					} else if (i.value() != FullSelection) {
						*i = FullSelection;
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

	if (!toItems->isEmpty() && toItems->cbegin().value() != FullSelection) {
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
		for (SelectedItems::iterator i = toItems->begin(); i != toItems->cend();) {
			int32 iy = itemTop(i.key());
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
	if (_dragCursorState == HistoryInDateCursorState && _dragAction == NoDrag) {
		if (App::hoveredItem()) {
			auto dateText = App::hoveredItem()->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat));
			if (auto edited = App::hoveredItem()->Get<HistoryMessageEdited>()) {
				dateText += '\n' + lng_edited_date(lt_date, edited->_editDate.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat)));
			}
			return dateText;
		}
	} else if (_dragCursorState == HistoryInForwardedCursorState && _dragAction == NoDrag) {
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
	return _dragPos;
}

void HistoryInner::onParentGeometryChanged() {
	auto mousePos = QCursor::pos();
	auto mouseOver = _widget->rect().contains(_widget->mapFromGlobal(mousePos));
	auto needToUpdate = (_dragAction != NoDrag || _touchScroll || mouseOver);
	if (needToUpdate) {
		dragActionUpdate(mousePos);
	}
}
