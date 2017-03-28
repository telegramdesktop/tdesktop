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
#include "historywidget.h"

#include "styles/style_history.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "boxes/confirmbox.h"
#include "boxes/send_files_box.h"
#include "boxes/sharebox.h"
#include "core/file_utilities.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/ripple_animation.h"
#include "inline_bots/inline_bot_result.h"
#include "data/data_drafts.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "history/history_drag_area.h"
#include "profile/profile_block_group_members.h"
#include "core/click_handler_types.h"
#include "stickers/emoji_pan.h"
#include "lang.h"
#include "application.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "passcodewidget.h"
#include "mainwindow.h"
#include "storage/file_upload.h"
#include "media/media_audio.h"
#include "media/media_audio_capture.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "window/top_bar_widget.h"
#include "window/themes/window_theme.h"
#include "observer_peer.h"
#include "core/qthelp_regex.h"
#include "ui/widgets/popup_menu.h"
#include "platform/platform_file_utilities.h"
#include "auth_session.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "inline_bots/inline_results_widget.h"

namespace {

constexpr auto kStickersUpdateTimeout = 3600000; // update not more than once in an hour

QString mimeTagFromTag(const QString &tagId) {
	if (tagId.startsWith(qstr("mention://"))) {
		return tagId + ':' + QString::number(AuthSession::CurrentUserId());
	}
	return tagId;
}

QMimeData *mimeDataFromTextWithEntities(const TextWithEntities &forClipboard) {
	if (forClipboard.text.isEmpty()) {
		return nullptr;
	}

	auto result = new QMimeData();
	result->setText(forClipboard.text);
	auto tags = textTagsFromEntities(forClipboard.entities);
	if (!tags.isEmpty()) {
		for (auto &tag : tags) {
			tag.id = mimeTagFromTag(tag.id);
		}
		result->setData(Ui::FlatTextarea::tagsMimeType(), Ui::FlatTextarea::serializeTagsList(tags));
	}
	return result;
}

// For mention tags save and validate userId, ignore tags for different userId.
class FieldTagMimeProcessor : public Ui::FlatTextarea::TagMimeProcessor {
public:
	QString mimeTagFromTag(const QString &tagId) override {
		return ::mimeTagFromTag(tagId);
	}

	QString tagFromMimeTag(const QString &mimeTag) override {
		if (mimeTag.startsWith(qstr("mention://"))) {
			auto match = QRegularExpression(":(\\d+)$").match(mimeTag);
			if (!match.hasMatch() || match.capturedRef(1).toInt() != AuthSession::CurrentUserId()) {
				return QString();
			}
			return mimeTag.mid(0, mimeTag.size() - match.capturedLength());
		}
		return mimeTag;
	}

};

constexpr auto kScrollDateHideTimeout = 1000;

ApiWrap::RequestMessageDataCallback replyEditMessageDataCallback() {
	return [](ChannelData *channel, MsgId msgId) {
		if (App::main()) {
			App::main()->messageDataReceived(channel, msgId);
		}
	};
}

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


} // namespace

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

HistoryInner::HistoryInner(HistoryWidget *historyWidget, Ui::ScrollArea *scroll, History *history) : TWidget(nullptr)
, _peer(history->peer)
, _migrated(history->peer->migrateFrom() ? App::history(history->peer->migrateFrom()->id) : nullptr)
, _history(history)
, _widget(historyWidget)
, _scroll(scroll) {
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
		auto top = list[middle]->y;
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
	auto blocktop = historytop + block->y;
	auto blockbottom = blocktop + block->height;
	auto itemIndex = binarySearchBlocksOrItems<TopToBottom>(block->items, searchEdge - blocktop);

	while (true) {
		while (true) {
			auto item = block->items.at(itemIndex);
			auto itemtop = blocktop + item->y;
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
					debugValue("block->height", block->height);
					debugValue("itemIndex", itemIndex);
					debugValue("block->items.size()", block->items.size());
					debugValue("itemtop", itemtop);
					debugValue("item->height()", item->height());
					debugValue("itembottom", itembottom);
					debugValue("_visibleAreaTop", _visibleAreaTop);
					debugValue("_visibleAreaBottom", _visibleAreaBottom);
					for (int i = 0; i != qMin(history->blocks.size(), 5); ++i) {
						debugValue("y[" + QString::number(i) + "]", history->blocks[i]->y);
						debugValue("h[" + QString::number(i) + "]", history->blocks[i]->height);
						for (int j = 0; j != qMin(history->blocks[i]->items.size(), 5); ++j) {
							debugValue("y[" + QString::number(i) + "][" + QString::number(j) + "]", history->blocks[i]->items[j]->y);
							debugValue("h[" + QString::number(i) + "][" + QString::number(j) + "]", history->blocks[i]->items[j]->height());
						}
					}
					auto valid = [history, &debugInfo] {
						auto y = 0;
						for (int i = 0; i != history->blocks.size(); ++i) {
							auto innery = 0;
							if (history->blocks[i]->y != y) {
								debugInfo.append("bad_block_y" + QString::number(i) + ":" + QString::number(history->blocks[i]->y) + "!=" + QString::number(y));
								return false;
							}
							for (int j = 0; j != history->blocks[i]->items.size(); ++j) {
								if (history->blocks[i]->items[j]->pendingInitDimensions()) {
									debugInfo.append("pending_item_init" + QString::number(i) + "," + QString::number(j));
								} else if (history->blocks[i]->items[j]->pendingResize()) {
									debugInfo.append("pending_resize" + QString::number(i) + "," + QString::number(j));
								}
								if (history->blocks[i]->items[j]->y != innery) {
									debugInfo.append("bad_item_y" + QString::number(i) + "," + QString::number(j) + ":" + QString::number(history->blocks[i]->items[j]->y) + "!=" + QString::number(innery));
									return false;
								}
								innery += history->blocks[i]->items[j]->height();
							}
							if (history->blocks[i]->height != innery) {
								debugInfo.append("bad_block_height" + QString::number(i) + ":" + QString::number(history->blocks[i]->height) + "!=" + QString::number(innery));
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
		block = history->blocks.at(blockIndex);
		blocktop = historytop + block->y;
		blockbottom = blocktop + block->height;
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

		int32 mtop = migratedTop(), htop = historyTop(), hdrawtop = historyDrawTop();
		if (mtop >= 0) {
			int32 iBlock = (_curHistory == _migrated ? _curBlock : (_migrated->blocks.size() - 1));
			HistoryBlock *block = _migrated->blocks[iBlock];
			int32 iItem = (_curHistory == _migrated ? _curItem : (block->items.size() - 1));
			HistoryItem *item = block->items[iItem];

			int32 y = mtop + block->y + item->y;
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
			int32 iBlock = (_curHistory == _history ? _curBlock : 0);
			HistoryBlock *block = _history->blocks[iBlock];
			int32 iItem = (_curHistory == _history ? _curItem : 0);
			HistoryItem *item = block->items[iItem];

			QRect historyRect = r.intersected(QRect(0, hdrawtop, width(), r.top() + r.height()));
			int32 y = htop + block->y + item->y;
			p.save();
			p.translate(0, y);
			while (y < drawToY) {
				int32 h = item->height();
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
		if (uponSelected && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection && !Adaptive::OneColumn()) {
			mimeData->setData(qsl("application/x-td-forward-selected"), "1");
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

void HistoryInner::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		dragActionUpdate(e->globalPos());
	}

	int32 selectedForForward, selectedForDelete;
	getSelectionState(selectedForForward, selectedForDelete);
	bool canSendMessages = _widget->canSendMessages(_peer);

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
			_menu->addAction(lang(lng_context_forward_selected), _widget, SLOT(onForwardSelected()));
			if (selectedForDelete == selectedForForward) {
				_menu->addAction(lang(lng_context_delete_selected), base::lambda_guarded(this, [this] {
					_widget->confirmDeleteSelectedItems();
				}));
			}
			_menu->addAction(lang(lng_context_clear_selection), _widget, SLOT(onClearSelected()));
		} else if (App::hoveredLinkItem()) {
			if (isUponSelected != -2) {
				if (dynamic_cast<HistoryMessage*>(App::hoveredLinkItem()) && App::hoveredLinkItem()->id > 0) {
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
		bool canForward = item && (item->id > 0) && !item->serviceMsg();

		HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
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

		QString linkCopyToClipboardText = _contextMenuLnk ? _contextMenuLnk->copyToClipboardContextItemText() : QString();
		if (!linkCopyToClipboardText.isEmpty()) {
			_menu->addAction(linkCopyToClipboardText, this, SLOT(copyContextUrl()))->setEnabled(true);
		}
		if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(lang(lng_context_copy_post_link), _widget, SLOT(onCopyPostLink()));
		}
		if (isUponSelected > 1) {
			_menu->addAction(lang(lng_context_forward_selected), _widget, SLOT(onForwardSelected()));
			if (selectedForDelete == selectedForForward) {
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
		int32 selectedForForward, selectedForDelete;
		getSelectionState(selectedForForward, selectedForDelete);
		if (!_selected.isEmpty() && selectedForDelete == selectedForForward) {
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

bool HistoryInner::displayScrollDate() const{
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
	while (history->blocks.at(_curBlock)->y > y && _curBlock > 0) {
		--_curBlock;
		_curItem = 0;
	}
	while (history->blocks.at(_curBlock)->y + history->blocks.at(_curBlock)->height <= y && _curBlock + 1 < history->blocks.size()) {
		++_curBlock;
		_curItem = 0;
	}
	HistoryBlock *block = history->blocks.at(_curBlock);
	if (_curItem >= block->items.size()) {
		_curItem = block->items.size() - 1;
	}
	int by = block->y;
	while (block->items.at(_curItem)->y + by > y && _curItem > 0) {
		--_curItem;
	}
	while (block->items.at(_curItem)->y + block->items.at(_curItem)->height() + by <= y && _curItem + 1 < block->items.size()) {
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
	if (_selected.isEmpty() || _selected.cbegin().value() != FullSelection) return false;
	int32 selectedForForward, selectedForDelete;
	getSelectionState(selectedForForward, selectedForDelete);
	return (selectedForForward == selectedForDelete);
}

void HistoryInner::getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const {
	selectedForForward = selectedForDelete = 0;
	for (auto i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		if (i.value() == FullSelection) {
			if (i.key()->canDelete()) {
				++selectedForDelete;
			}
			++selectedForForward;
		}
	}
	if (!selectedForDelete && !selectedForForward && !_selected.isEmpty()) { // text selection
		selectedForForward = -1;
	}
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

	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		HistoryItem *item = i.key();
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
	int htop = historyTop(), mtop = migratedTop();
	if (htop >= 0 && _history->scrollTopItem) {
		t_assert(!_history->scrollTopItem->detached());
		return htop + _history->scrollTopItem->block()->y + _history->scrollTopItem->y + _history->scrollTopOffset;
	}
	if (mtop >= 0 && _migrated->scrollTopItem) {
		t_assert(!_migrated->scrollTopItem->detached());
		return mtop + _migrated->scrollTopItem->block()->y + _migrated->scrollTopItem->y + _migrated->scrollTopOffset;
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
	return (top < 0) ? top : (top + item->y + item->block()->y);
}

void HistoryInner::notifyIsBotChanged() {
	BotInfo *newinfo = (_history && _history->peer->isUser()) ? _history->peer->asUser()->botInfo.get() : nullptr;
	if ((!newinfo && !_botAbout) || (newinfo && _botAbout && _botAbout->info == newinfo)) {
		return;
	}

	if (newinfo) {
		_botAbout.reset(new BotAbout(this, newinfo));
		if (newinfo && !newinfo->inited && App::api()) {
			App::api()->requestFullPeer(_peer);
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
			QString dateText = App::hoveredItem()->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat));
			if (auto edited = App::hoveredItem()->Get<HistoryMessageEdited>()) {
				dateText += '\n' + lng_edited_date(lt_date, edited->_editDate.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat)));
			}
			return dateText;
		}
	} else if (_dragCursorState == HistoryInForwardedCursorState && _dragAction == NoDrag) {
		if (App::hoveredItem()) {
			if (HistoryMessageForwarded *fwd = App::hoveredItem()->Get<HistoryMessageForwarded>()) {
				return fwd->_text.originalText(AllTextSelection, ExpandLinksNone);
			}
		}
	} else if (ClickHandlerPtr lnk = ClickHandler::getActive()) {
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

MessageField::MessageField(HistoryWidget *history, const style::FlatTextarea &st, const QString &ph, const QString &val) : Ui::FlatTextarea(history, st, ph, val), history(history) {
	setMinHeight(st::historySendSize.height() - 2 * st::historySendPadding);
	setMaxHeight(st::historyComposeFieldMaxHeight);
}

bool MessageField::hasSendText() const {
	auto &text = getTextWithTags().text;
	for (auto *ch = text.constData(), *e = ch + text.size(); ch != e; ++ch) {
		auto code = ch->unicode();
		if (code != ' ' && code != '\n' && code != '\r' && !chReplacedBySpace(code)) {
			return true;
		}
	}
	return false;
}

void MessageField::onEmojiInsert(EmojiPtr emoji) {
	if (isHidden()) return;
	insertEmoji(emoji, textCursor());
}

void MessageField::dropEvent(QDropEvent *e) {
	FlatTextarea::dropEvent(e);
	if (e->isAccepted()) {
		App::wnd()->activateWindow();
	}
}

bool MessageField::canInsertFromMimeData(const QMimeData *source) const {
	if (source->hasUrls()) {
		int32 files = 0;
		for (int32 i = 0; i < source->urls().size(); ++i) {
			if (source->urls().at(i).isLocalFile()) {
				++files;
			}
		}
		if (files > 1) return false; // multiple confirm with "compressed" checkbox
	}
	if (source->hasImage()) return true;
	return FlatTextarea::canInsertFromMimeData(source);
}

void MessageField::insertFromMimeData(const QMimeData *source) {
	if (history->confirmSendingFiles(source, CompressConfirm::Auto, source->text())) {
		return;
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
	resize(parent->width(), _hide->height() + st::lineWidth);

	connect(_report, SIGNAL(clicked()), this, SIGNAL(reportClicked()));
	connect(_hide, SIGNAL(clicked()), this, SIGNAL(hideClicked()));
	connect(_clear, SIGNAL(clicked()), this, SIGNAL(clearClicked()));

	_clear->hide();
}

void ReportSpamPanel::resizeEvent(QResizeEvent *e) {
	_report->resize(width() - (_hide->width() + st::reportSpamSeparator) * 2, _report->height());
	_report->moveToLeft(_hide->width() + st::reportSpamSeparator, 0);
	_hide->moveToRight(0, 0);
	_clear->move((width() - _clear->width()) / 2, height() - _clear->height() - ((height() - st::msgFont->height - _clear->height()) / 2));
}

void ReportSpamPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(QRect(0, 0, width(), height() - st::lineWidth), st::reportSpamBg);
	p.fillRect(Adaptive::OneColumn() ? 0 : st::lineWidth, height() - st::lineWidth, width() - (Adaptive::OneColumn() ? 0 : st::lineWidth), st::lineWidth, st::shadowFg);
	if (!_clear->isHidden()) {
		p.setPen(st::reportSpamFg);
		p.setFont(st::msgFont);
		p.drawText(QRect(_report->x(), (_clear->y() - st::msgFont->height) / 2, _report->width(), st::msgFont->height), lang(lng_report_spam_thanks), style::al_top);
	}
}

void ReportSpamPanel::setReported(bool reported, PeerData *onPeer) {
	if (reported) {
		_report->hide();
		_clear->setText(lang(onPeer->isChannel() ? (onPeer->isMegagroup() ? lng_profile_leave_group : lng_profile_leave_channel) : lng_profile_delete_conversation));
		_clear->show();
	} else {
		_report->show();
		_clear->hide();
	}
	update();
}

BotKeyboard::BotKeyboard(QWidget *parent) : TWidget(parent)
, _st(&st::botKbButton) {
	setGeometry(0, 0, _st->margin, st::botKbScroll.deltat);
	_height = st::botKbScroll.deltat;
	setMouseTracking(true);
}

void BotKeyboard::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	p.fillRect(clip, st::historyComposeAreaBg);

	if (_impl) {
		int x = rtl() ? st::botKbScroll.width : _st->margin;
		p.translate(x, st::botKbScroll.deltat);
		_impl->paint(p, width(), clip.translated(-x, -st::botKbScroll.deltat), getms());
	}
}

void BotKeyboard::Style::startPaint(Painter &p) const {
	p.setPen(st::botKbColor);
	p.setFont(st::botKbStyle.font);
}

const style::TextStyle &BotKeyboard::Style::textStyle() const {
	return st::botKbStyle;
}

void BotKeyboard::Style::repaint(const HistoryItem *item) const {
	_parent->update();
}

int BotKeyboard::Style::buttonRadius() const {
	return st::buttonRadius;
}

void BotKeyboard::Style::paintButtonBg(Painter &p, const QRect &rect, float64 howMuchOver) const {
	App::roundRect(p, rect, st::botKbBg, BotKeyboardCorners);
}

void BotKeyboard::Style::paintButtonIcon(Painter &p, const QRect &rect, int outerWidth, HistoryMessageReplyMarkup::Button::Type type) const {
	// Buttons with icons should not appear here.
}

void BotKeyboard::Style::paintButtonLoading(Painter &p, const QRect &rect) const {
	// Buttons with loading progress should not appear here.
}

int BotKeyboard::Style::minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const {
	int result = 2 * buttonPadding();
	return result;
}

void BotKeyboard::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	ClickHandler::pressed();
}

void BotKeyboard::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void BotKeyboard::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	if (ClickHandlerPtr activated = ClickHandler::unpressed()) {
		App::activateClickHandler(activated, e->button());
	}
}

void BotKeyboard::enterEventHook(QEvent *e) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void BotKeyboard::leaveEventHook(QEvent *e) {
	clearSelection();
}

bool BotKeyboard::moderateKeyActivate(int key) {
	if (auto item = App::histItemById(_wasForMsgId)) {
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (key >= Qt::Key_1 && key <= Qt::Key_9) {
				int index = (key - Qt::Key_1);
				if (!markup->rows.isEmpty() && index >= 0 && index < markup->rows.front().size()) {
					App::activateBotCommand(item, 0, index);
					return true;
				}
			} else if (key == Qt::Key_Q) {
				if (auto user = item->history()->peer->asUser()) {
					if (user->botInfo && item->from() == user) {
						App::sendBotCommand(user, user, qsl("/translate"));
						return true;
					}
				}
			}
		}
	}
	return false;
}

void BotKeyboard::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!_impl) return;
	_impl->clickHandlerActiveChanged(p, active);
}

void BotKeyboard::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (!_impl) return;
	_impl->clickHandlerPressedChanged(p, pressed);
}

bool BotKeyboard::updateMarkup(HistoryItem *to, bool force) {
	if (!to || !to->definesReplyKeyboard()) {
		if (_wasForMsgId.msg) {
			_maximizeSize = _singleUse = _forceReply = false;
			_wasForMsgId = FullMsgId();
			_impl = nullptr;
			return true;
		}
		return false;
	}

	if (_wasForMsgId == FullMsgId(to->channelId(), to->id) && !force) {
		return false;
	}

	_wasForMsgId = FullMsgId(to->channelId(), to->id);

	auto markupFlags = to->replyKeyboardFlags();
	_forceReply = markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_force_reply;
	_maximizeSize = !(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_resize);
	_singleUse = _forceReply || (markupFlags & MTPDreplyKeyboardMarkup::Flag::f_single_use);

	_impl = nullptr;
	if (auto markup = to->Get<HistoryMessageReplyMarkup>()) {
		if (!markup->rows.isEmpty()) {
			_impl.reset(new ReplyKeyboard(to, std::make_unique<Style>(this, *_st)));
		}
	}

	resizeToWidth(width(), _maxOuterHeight);

	return true;
}

bool BotKeyboard::hasMarkup() const {
	return _impl != nullptr;
}

bool BotKeyboard::forceReply() const {
	return _forceReply;
}

int BotKeyboard::resizeGetHeight(int newWidth) {
	updateStyle(newWidth);
	_height = st::botKbScroll.deltat + st::botKbScroll.deltab + (_impl ? _impl->naturalHeight() : 0);
	if (_maximizeSize) {
		accumulate_max(_height, _maxOuterHeight);
	}
	if (_impl) {
		int implWidth = newWidth - _st->margin - st::botKbScroll.width;
		int implHeight = _height - (st::botKbScroll.deltat + st::botKbScroll.deltab);
		_impl->resize(implWidth, implHeight);
	}
	return _height;
}

bool BotKeyboard::maximizeSize() const {
	return _maximizeSize;
}

bool BotKeyboard::singleUse() const {
	return _singleUse;
}

void BotKeyboard::updateStyle(int newWidth) {
	if (!_impl) return;

	int implWidth = newWidth - st::botKbButton.margin - st::botKbScroll.width;
	_st = _impl->isEnoughSpace(implWidth, st::botKbButton) ? &st::botKbButton : &st::botKbTinyButton;

	_impl->setStyle(std::make_unique<Style>(this, *_st));
}

void BotKeyboard::clearSelection() {
	if (_impl) {
		if (ClickHandler::setActive(ClickHandlerPtr(), this)) {
			Ui::Tooltip::Hide();
			setCursor(style::cur_default);
		}
	}
}

QPoint BotKeyboard::tooltipPos() const {
	return _lastMousePos;
}

QString BotKeyboard::tooltipText() const {
	if (ClickHandlerPtr lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

void BotKeyboard::updateSelected() {
	Ui::Tooltip::Show(1000, this);

	if (!_impl) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	int x = rtl() ? st::botKbScroll.width : _st->margin;

	auto link = _impl->getState(p.x() - x, p.y() - _st->margin);
	if (ClickHandler::setActive(link, this)) {
		Ui::Tooltip::Hide();
		setCursor(link ? style::cur_pointer : style::cur_default);
	}
}

HistoryHider::HistoryHider(MainWidget *parent, bool forwardSelected) : TWidget(parent)
, _forwardSelected(forwardSelected)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, UserData *sharedContact) : TWidget(parent)
, _sharedContact(sharedContact)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent) : TWidget(parent)
, _sendPath(true)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, const QString &botAndQuery) : TWidget(parent)
, _botAndQuery(botAndQuery)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, const QString &url, const QString &text) : TWidget(parent)
, _shareUrl(url)
, _shareText(text)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::defaultBoxButton) {
	init();
}

void HistoryHider::init() {
	connect(_send, SIGNAL(clicked()), this, SLOT(forward()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(startHide()));
	subscribe(Global::RefPeerChooseCancel(), [this] { startHide(); });

	_chooseWidth = st::historyForwardChooseFont->width(lang(_botAndQuery.isEmpty() ? lng_forward_choose : lng_inline_switch_choose));

	resizeEvent(0);
	_a_opacity.start([this] { update(); }, 0., 1., st::boxDuration);
}

bool HistoryHider::withConfirm() const {
	return _sharedContact || _sendPath;
}

void HistoryHider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto opacity = _a_opacity.current(getms(), _hiding ? 0. : 1.);
	if (opacity == 0.) {
		if (_hiding) {
			QTimer::singleShot(0, this, SLOT(deleteLater()));
		}
		return;
	}

	p.setOpacity(opacity);
	if (!_hiding || !_cacheForAnim.isNull() || !_offered) {
		p.fillRect(rect(), st::layerBg);
	}
	if (_cacheForAnim.isNull() || !_offered) {
		p.setFont(st::historyForwardChooseFont);
		if (_offered) {
			Ui::Shadow::paint(p, _box, width(), st::boxRoundShadow);
			App::roundRect(p, _box, st::boxBg, BoxCorners);

			p.setPen(st::boxTextFg);
			_toText.drawLeftElided(p, _box.left() + st::boxPadding.left(), _box.y() + st::boxTopMargin + st::boxPadding.top(), _toTextWidth + 2, width(), 1, style::al_left);
		} else {
			auto w = st::historyForwardChooseMargins.left() + _chooseWidth + st::historyForwardChooseMargins.right();
			auto h = st::historyForwardChooseMargins.top() + st::historyForwardChooseFont->height + st::historyForwardChooseMargins.bottom();
			App::roundRect(p, (width() - w) / 2, (height() - h) / 2, w, h, st::historyForwardChooseBg, ForwardCorners);

			p.setPen(st::historyForwardChooseFg);
			p.drawText(_box, lang(_botAndQuery.isEmpty() ? lng_forward_choose : lng_inline_switch_choose), QTextOption(style::al_center));
		}
	} else {
		p.drawPixmap(_box.left(), _box.top(), _cacheForAnim);
	}
}

void HistoryHider::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (_offered) {
			_offered = nullptr;
			resizeEvent(nullptr);
			update();
			App::main()->dialogsActivate();
		} else {
			startHide();
		}
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_offered) {
			forward();
		}
	}
}

void HistoryHider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!_box.contains(e->pos())) {
			startHide();
		}
	}
}

void HistoryHider::startHide() {
	if (_hiding) return;
	_hiding = true;
	if (Adaptive::OneColumn()) {
		QTimer::singleShot(0, this, SLOT(deleteLater()));
	} else {
		if (_offered) _cacheForAnim = myGrab(this, _box);
		if (_forwardRequest) MTP::cancel(_forwardRequest);
		_send->hide();
		_cancel->hide();
		_a_opacity.start([this] { animationCallback(); }, 1., 0., st::boxDuration);
	}
}

void HistoryHider::animationCallback() {
	update();
	if (!_a_opacity.animating() && _hiding) {
		QTimer::singleShot(0, this, SLOT(deleteLater()));
	}
}

void HistoryHider::forward() {
	if (!_hiding && _offered) {
		if (_sharedContact) {
			parent()->onShareContact(_offered->id, _sharedContact);
		} else if (_sendPath) {
			parent()->onSendPaths(_offered->id);
		} else if (!_shareUrl.isEmpty()) {
			parent()->onShareUrl(_offered->id, _shareUrl, _shareText);
		} else if (!_botAndQuery.isEmpty()) {
			parent()->onInlineSwitchChosen(_offered->id, _botAndQuery);
		} else {
			parent()->onForward(_offered->id, _forwardSelected ? ForwardSelectedMessages : ForwardContextMessage);
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
	auto w = st::boxWidth;
	auto h = st::boxPadding.top() + st::boxPadding.bottom();
	if (_offered) {
		if (!_hiding) {
			_send->show();
			_cancel->show();
		}
		h += st::boxTopMargin + qMax(st::boxTextFont->height, st::boxLabelStyle.lineHeight) + st::boxButtonPadding.top() + _send->height() + st::boxButtonPadding.bottom();
	} else {
		h += st::historyForwardChooseFont->height;
		_send->hide();
		_cancel->hide();
	}
	_box = QRect((width() - w) / 2, (height() - h) / 2, w, h);
	_send->moveToRight(width() - (_box.x() + _box.width()) + st::boxButtonPadding.right(), _box.y() + _box.height() - st::boxButtonPadding.bottom() - _send->height());
	_cancel->moveToRight(width() - (_box.x() + _box.width()) + st::boxButtonPadding.right() + _send->width() + st::boxButtonPadding.left(), _send->y());
}

bool HistoryHider::offerPeer(PeerId peer) {
	if (!peer) {
		_offered = nullptr;
		_toText.setText(st::boxLabelStyle, QString());
		_toTextWidth = 0;
		resizeEvent(nullptr);
		return false;
	}
	_offered = App::peer(peer);
	LangString phrase;
	QString recipient = _offered->isUser() ? _offered->name : '\xAB' + _offered->name + '\xBB';
	if (_sharedContact) {
		phrase = lng_forward_share_contact(lt_recipient, recipient);
	} else if (_sendPath) {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onSendPaths(toId)) {
			startHide();
		}
		return false;
	} else if (!_shareUrl.isEmpty()) {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onShareUrl(toId, _shareUrl, _shareText)) {
			startHide();
		}
		return false;
	} else if (!_botAndQuery.isEmpty()) {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onInlineSwitchChosen(toId, _botAndQuery)) {
			startHide();
		}
		return false;
	} else {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onForward(toId, _forwardSelected ? ForwardSelectedMessages : ForwardContextMessage)) {
			startHide();
		}
		return false;
	}

	_toText.setText(st::boxLabelStyle, phrase, _textNameOptions);
	_toTextWidth = _toText.maxWidth();
	if (_toTextWidth > _box.width() - st::boxPadding.left() - st::boxLayerButtonPadding.right()) {
		_toTextWidth = _box.width() - st::boxPadding.left() - st::boxLayerButtonPadding.right();
	}

	resizeEvent(nullptr);
	update();
	setFocus();

	return true;
}

QString HistoryHider::offeredText() const {
	return _toText.originalText();
}

bool HistoryHider::wasOffered() const {
	return _offered != nullptr;
}

HistoryHider::~HistoryHider() {
	if (_sendPath) cSetSendPaths(QStringList());
	parent()->noHider(this);
}

class SilentToggle : public Ui::IconButton, public Ui::AbstractTooltipShower {
public:
	SilentToggle(QWidget *parent);

	void setChecked(bool checked);
	bool checked() const {
		return _checked;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	bool _checked = false;

};

SilentToggle::SilentToggle(QWidget *parent) : IconButton(parent, st::historySilentToggle) {
	setMouseTracking(true);
}

void SilentToggle::mouseMoveEvent(QMouseEvent *e) {
	IconButton::mouseMoveEvent(e);
	if (rect().contains(e->pos())) {
		Ui::Tooltip::Show(1000, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void SilentToggle::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		setIconOverride(_checked ? &st::historySilentToggleOn : nullptr, _checked ? &st::historySilentToggleOnOver : nullptr);
	}
}

void SilentToggle::leaveEventHook(QEvent *e) {
	IconButton::leaveEventHook(e);
	Ui::Tooltip::Hide();
}

void SilentToggle::mouseReleaseEvent(QMouseEvent *e) {
	setChecked(!_checked);
	IconButton::mouseReleaseEvent(e);
	Ui::Tooltip::Show(0, this);
	auto p = App::main() ? App::main()->peer() : nullptr;
	if (p && p->isChannel() && p->notify != UnknownNotifySettings) {
		App::main()->updateNotifySetting(p, NotifySettingDontChange, _checked ? SilentNotifiesSetSilent : SilentNotifiesSetNotify);
	}
}

QString SilentToggle::tooltipText() const {
	return lang(_checked ? lng_wont_be_notified : lng_will_be_notified);
}

QPoint SilentToggle::tooltipPos() const {
	return QCursor::pos();
}

EntitiesInText entitiesFromTextTags(const TextWithTags::Tags &tags) {
	EntitiesInText result;
	if (tags.isEmpty()) {
		return result;
	}

	result.reserve(tags.size());
	auto mentionStart = qstr("mention://user.");
	for_const (auto &tag, tags) {
		if (tag.id.startsWith(mentionStart)) {
			if (auto match = qthelp::regex_match("^(\\d+\\.\\d+)(/|$)", tag.id.midRef(mentionStart.size()))) {
				result.push_back(EntityInText(EntityInTextMentionName, tag.offset, tag.length, match->captured(1)));
			}
		}
	}
	return result;
}

TextWithTags::Tags textTagsFromEntities(const EntitiesInText &entities) {
	TextWithTags::Tags result;
	if (entities.isEmpty()) {
		return result;
	}

	result.reserve(entities.size());
	for_const (auto &entity, entities) {
		if (entity.type() == EntityInTextMentionName) {
			auto match = QRegularExpression("^(\\d+\\.\\d+)$").match(entity.data());
			if (match.hasMatch()) {
				result.push_back({ entity.offset(), entity.length(), qstr("mention://user.") + entity.data() });
			}
		}
	}
	return result;
}

HistoryWidget::HistoryWidget(QWidget *parent, gsl::not_null<Window::Controller*> controller) : TWidget(parent)
, _controller(controller)
, _fieldBarCancel(this, st::historyReplyCancel)
, _topBar(this, _controller)
, _scroll(this, st::historyScroll, false)
, _historyDown(_scroll, st::historyToDown)
, _fieldAutocomplete(this)
, _reportSpamPanel(this)
, _send(this)
, _unblock(this, lang(lng_unblock_button).toUpper(), st::historyUnblock)
, _botStart(this, lang(lng_bot_start).toUpper(), st::historyComposeButton)
, _joinChannel(this, lang(lng_channel_join).toUpper(), st::historyComposeButton)
, _muteUnmute(this, lang(lng_channel_mute).toUpper(), st::historyComposeButton)
, _attachToggle(this, st::historyAttach)
, _attachEmoji(this, st::historyAttachEmoji)
, _botKeyboardShow(this, st::historyBotKeyboardShow)
, _botKeyboardHide(this, st::historyBotKeyboardHide)
, _botCommandStart(this, st::historyBotCommandStart)
, _silent(this)
, _field(this, st::historyComposeField, lang(lng_message_ph))
, _recordCancelWidth(st::historyRecordFont->width(lang(lng_record_cancel)))
, _a_recording(animation(this, &HistoryWidget::step_recording))
, _kbScroll(this, st::botKbScroll)
, _emojiPan(this)
, _attachDragDocument(this)
, _attachDragPhoto(this)
, _fileLoader(this, FileLoaderQueueStopTimeout)
, _topShadow(this, st::shadowFg) {
	setAcceptDrops(true);

	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });
	connect(_topBar, &Window::TopBarWidget::clicked, this, [this] { topBarClick(); });
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_reportSpamPanel, SIGNAL(reportClicked()), this, SLOT(onReportSpamClicked()));
	connect(_reportSpamPanel, SIGNAL(hideClicked()), this, SLOT(onReportSpamHide()));
	connect(_reportSpamPanel, SIGNAL(clearClicked()), this, SLOT(onReportSpamClear()));
	connect(_historyDown, SIGNAL(clicked()), this, SLOT(onHistoryToEnd()));
	connect(_fieldBarCancel, SIGNAL(clicked()), this, SLOT(onFieldBarCancel()));
	_send->setClickedCallback([this] { sendButtonClicked(); });
	connect(_unblock, SIGNAL(clicked()), this, SLOT(onUnblock()));
	connect(_botStart, SIGNAL(clicked()), this, SLOT(onBotStart()));
	connect(_joinChannel, SIGNAL(clicked()), this, SLOT(onJoinChannel()));
	connect(_muteUnmute, SIGNAL(clicked()), this, SLOT(onMuteUnmute()));
	connect(_silent, SIGNAL(clicked()), this, SLOT(onBroadcastSilentChange()));
	connect(_field, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
	connect(_field, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(_field, SIGNAL(tabbed()), this, SLOT(onFieldTabbed()));
	connect(_field, SIGNAL(resized()), this, SLOT(onFieldResize()));
	connect(_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(_field, SIGNAL(changed()), this, SLOT(onTextChange()));
	connect(_field, SIGNAL(spacedReturnedPasted()), this, SLOT(onPreviewParse()));
	connect(_field, SIGNAL(linksChanged()), this, SLOT(onPreviewCheck()));
	connect(App::wnd()->windowHandle(), SIGNAL(visibleChanged(bool)), this, SLOT(onWindowVisibleChanged()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(_emojiPan, SIGNAL(emojiSelected(EmojiPtr)), _field, SLOT(onEmojiInsert(EmojiPtr)));
	connect(_emojiPan, SIGNAL(stickerSelected(DocumentData*)), this, SLOT(onStickerSend(DocumentData*)));
	connect(_emojiPan, SIGNAL(photoSelected(PhotoData*)), this, SLOT(onPhotoSend(PhotoData*)));
	connect(_emojiPan, SIGNAL(inlineResultSelected(InlineBots::Result*,UserData*)), this, SLOT(onInlineResultSend(InlineBots::Result*,UserData*)));
	connect(_emojiPan, SIGNAL(updateStickers()), this, SLOT(updateStickers()));
	connect(&_sendActionStopTimer, SIGNAL(timeout()), this, SLOT(onCancelSendAction()));
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreviewTimeout()));
	connect(Media::Capture::instance(), SIGNAL(error()), this, SLOT(onRecordError()));
	connect(Media::Capture::instance(), SIGNAL(updated(quint16,qint32)), this, SLOT(onRecordUpdate(quint16,qint32)));
	connect(Media::Capture::instance(), SIGNAL(done(QByteArray,VoiceWaveform,qint32)), this, SLOT(onRecordDone(QByteArray,VoiceWaveform,qint32)));

	_attachToggle->setClickedCallback(App::LambdaDelayed(st::historyAttach.ripple.hideDuration, this, [this] {
		chooseAttach();
	}));

	_updateHistoryItems.setSingleShot(true);
	connect(&_updateHistoryItems, SIGNAL(timeout()), this, SLOT(onUpdateHistoryItems()));

	_scrollTimer.setSingleShot(false);

	_sendActionStopTimer.setSingleShot(true);

	_animActiveTimer.setSingleShot(false);
	connect(&_animActiveTimer, SIGNAL(timeout()), this, SLOT(onAnimActiveStep()));

	_membersDropdownShowTimer.setSingleShot(true);
	connect(&_membersDropdownShowTimer, SIGNAL(timeout()), this, SLOT(onMembersDropdownShow()));

	_saveDraftTimer.setSingleShot(true);
	connect(&_saveDraftTimer, SIGNAL(timeout()), this, SLOT(onDraftSave()));
	_saveCloudDraftTimer.setSingleShot(true);
	connect(&_saveCloudDraftTimer, SIGNAL(timeout()), this, SLOT(onCloudDraftSave()));
	connect(_field->verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onDraftSaveDelayed()));
	connect(_field, SIGNAL(cursorPositionChanged()), this, SLOT(onDraftSaveDelayed()));
	connect(_field, SIGNAL(cursorPositionChanged()), this, SLOT(onCheckFieldAutocomplete()), Qt::QueuedConnection);

	_fieldBarCancel->hide();

	_topBar->hide();
	_scroll->hide();

	_keyboard = _kbScroll->setOwnedWidget(object_ptr<BotKeyboard>(this));
	_kbScroll->hide();

	updateScrollColors();

	_historyDown->installEventFilter(this);

	_fieldAutocomplete->hide();
	connect(_fieldAutocomplete, SIGNAL(mentionChosen(UserData*,FieldAutocomplete::ChooseMethod)), this, SLOT(onMentionInsert(UserData*)));
	connect(_fieldAutocomplete, SIGNAL(hashtagChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, SIGNAL(botCommandChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, SIGNAL(stickerChosen(DocumentData*,FieldAutocomplete::ChooseMethod)), this, SLOT(onStickerSend(DocumentData*)));
	connect(_fieldAutocomplete, SIGNAL(moderateKeyActivate(int,bool*)), this, SLOT(onModerateKeyActivate(int,bool*)));
	_field->installEventFilter(_fieldAutocomplete);
	_field->setTagMimeProcessor(std::make_unique<FieldTagMimeProcessor>());
	updateFieldSubmitSettings();

	_field->hide();
	_send->hide();
	_unblock->hide();
	_botStart->hide();
	_joinChannel->hide();
	_muteUnmute->hide();

	_send->setRecordStartCallback([this] { recordStartCallback(); });
	_send->setRecordStopCallback([this](bool active) { recordStopCallback(active); });
	_send->setRecordUpdateCallback([this](QPoint globalPos) { recordUpdateCallback(globalPos); });
	_send->setRecordAnimationCallback([this] { updateField(); });

	_reportSpamPanel->move(0, 0);
	_reportSpamPanel->hide();

	_attachToggle->hide();
	_attachEmoji->hide();
	_botKeyboardShow->hide();
	_botKeyboardHide->hide();
	_silent->hide();
	_botCommandStart->hide();

	_attachEmoji->installEventFilter(_emojiPan);

	connect(_botKeyboardShow, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(_botKeyboardHide, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(_botCommandStart, SIGNAL(clicked()), this, SLOT(onCmdStart()));

	_emojiPan->hide();
	_attachDragDocument->hide();
	_attachDragPhoto->hide();

	_topShadow->hide();

	_attachDragDocument->setDroppedCallback([this](const QMimeData *data) { confirmSendingFiles(data, CompressConfirm::No); });
	_attachDragPhoto->setDroppedCallback([this](const QMimeData *data) { confirmSendingFiles(data, CompressConfirm::Yes); });

	connect(&_updateEditTimeLeftDisplay, SIGNAL(timeout()), this, SLOT(updateField()));

	subscribe(Adaptive::Changed(), [this] { update(); });
	subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
		itemRemoved(item);
	});
	subscribe(AuthSession::Current().data().contactsLoaded(), [this](bool) {
		if (_peer) {
			updateReportSpamStatus();
			updateControlsVisibility();
		}
	});
}

void HistoryWidget::start() {
	connect(App::main(), SIGNAL(stickersUpdated()), this, SLOT(onStickersUpdated()));
	connect(App::main(), SIGNAL(savedGifsUpdated()), _emojiPan, SLOT(refreshSavedGifs()));

	updateRecentStickers();
	if (App::main()) emit App::main()->savedGifsUpdated();

	connect(App::api(), SIGNAL(fullPeerUpdated(PeerData*)), this, SLOT(onFullPeerUpdated(PeerData*)));
}

void HistoryWidget::onStickersUpdated() {
	_emojiPan->refreshStickers();
	updateStickersByEmoji();
}

void HistoryWidget::onMentionInsert(UserData *user) {
	QString replacement, entityTag;
	if (user->username.isEmpty()) {
		replacement = user->firstName;
		if (replacement.isEmpty()) {
			replacement = App::peerName(user);
		}
		entityTag = qsl("mention://user.") + QString::number(user->bareId()) + '.' + QString::number(user->access);
	} else {
		replacement = '@' + user->username;
	}
	_field->insertTag(replacement, entityTag);
}

void HistoryWidget::onHashtagOrBotCommandInsert(QString str, FieldAutocomplete::ChooseMethod method) {
	// Send bot command at once, if it was not inserted by pressing Tab.
	if (str.at(0) == '/' && method != FieldAutocomplete::ChooseMethod::ByTab) {
		App::sendBotCommand(_peer, nullptr, str);
		setFieldText(_field->getTextWithTagsPart(_field->textCursor().position()));
	} else {
		_field->insertTag(str);
	}
}

void HistoryWidget::updateInlineBotQuery() {
	UserData *bot = nullptr;
	QString inlineBotUsername;
	QString query = _field->getInlineBotQuery(&bot, &inlineBotUsername);
	if (inlineBotUsername != _inlineBotUsername) {
		_inlineBotUsername = inlineBotUsername;
		if (_inlineBotResolveRequestId) {
//			Notify::inlineBotRequesting(false);
			MTP::cancel(_inlineBotResolveRequestId);
			_inlineBotResolveRequestId = 0;
		}
		if (bot == Ui::LookingUpInlineBot) {
			_inlineBot = Ui::LookingUpInlineBot;
//			Notify::inlineBotRequesting(true);
			_inlineBotResolveRequestId = MTP::send(MTPcontacts_ResolveUsername(MTP_string(_inlineBotUsername)), rpcDone(&HistoryWidget::inlineBotResolveDone), rpcFail(&HistoryWidget::inlineBotResolveFail, _inlineBotUsername));
			return;
		}
	} else if (bot == Ui::LookingUpInlineBot) {
		if (_inlineBot == Ui::LookingUpInlineBot) {
			return;
		}
		bot = _inlineBot;
	}

	applyInlineBotQuery(bot, query);
}

void HistoryWidget::applyInlineBotQuery(UserData *bot, const QString &query) {
	if (bot) {
		if (_inlineBot != bot) {
			_inlineBot = bot;
			inlineBotChanged();
		}
		if (!_inlineResults) {
			_inlineResults.create(this);
			_inlineResults->setResultSelectedCallback([this](InlineBots::Result *result, UserData *bot) {
				onInlineResultSend(result, bot);
			});
			updateControlsGeometry();
			orderWidgets();
		}
		_inlineResults->queryInlineBot(_inlineBot, _peer, query);
		if (!_fieldAutocomplete->isHidden()) {
			_fieldAutocomplete->hideAnimated();
		}
	} else {
		clearInlineBot();
	}
}

void HistoryWidget::orderWidgets() {
	_reportSpamPanel->raise();
	_topShadow->raise();
	if (_membersDropdown) {
		_membersDropdown->raise();
	}
	if (_inlineResults) {
		_inlineResults->raise();
	}
	_emojiPan->raise();
	_attachDragDocument->raise();
	_attachDragPhoto->raise();
}

void HistoryWidget::updateStickersByEmoji() {
	int len = 0;
	if (!_editMsgId) {
		auto &text = _field->getTextWithTags().text;
		if (auto emoji = Ui::Emoji::Find(text, &len)) {
			if (text.size() > len) {
				len = 0;
			} else {
				_fieldAutocomplete->showStickers(emoji);
			}
		}
	}
	if (!len) {
		_fieldAutocomplete->showStickers(nullptr);
	}
}

void HistoryWidget::onTextChange() {
	updateInlineBotQuery();
	updateStickersByEmoji();

	if (_peer && (!_peer->isChannel() || _peer->isMegagroup())) {
		if (!_inlineBot && !_editMsgId && (_textUpdateEvents.testFlag(TextUpdateEvent::SendTyping))) {
			updateSendAction(_history, SendAction::Type::Typing);
		}
	}

	updateSendButtonType();
	if (showRecordButton()) {
		_previewCancelled = false;
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		updateControlsGeometry();
	}

	_saveCloudDraftTimer.stop();
	if (!_peer || !(_textUpdateEvents.testFlag(TextUpdateEvent::SaveDraft))) return;

	_saveDraftText = true;
	onDraftSave(true);
}

void HistoryWidget::onDraftSaveDelayed() {
	if (!_peer || !(_textUpdateEvents.testFlag(TextUpdateEvent::SaveDraft))) return;
	if (!_field->textCursor().anchor() && !_field->textCursor().position() && !_field->verticalScrollBar()->value()) {
		if (!Local::hasDraftCursors(_peer->id)) {
			return;
		}
	}
	onDraftSave(true);
}

void HistoryWidget::onDraftSave(bool delayed) {
	if (!_peer) return;
	if (delayed) {
		auto ms = getms();
		if (!_saveDraftStart) {
			_saveDraftStart = ms;
			return _saveDraftTimer.start(SaveDraftTimeout);
		} else if (ms - _saveDraftStart < SaveDraftAnywayTimeout) {
			return _saveDraftTimer.start(SaveDraftTimeout);
		}
	}
	writeDrafts(nullptr, nullptr);
}

void HistoryWidget::saveFieldToHistoryLocalDraft() {
	if (!_history) return;

	if (_editMsgId) {
		_history->setEditDraft(std::make_unique<Data::Draft>(_field, _editMsgId, _previewCancelled, _saveEditMsgRequestId));
	} else {
		if (_replyToId || !_field->isEmpty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(_field, _replyToId, _previewCancelled));
		} else {
			_history->clearLocalDraft();
		}
		_history->clearEditDraft();
	}
}

void HistoryWidget::onCloudDraftSave() {
	if (App::main()) {
		App::main()->saveDraftToCloud();
	}
}

void HistoryWidget::writeDrafts(Data::Draft **localDraft, Data::Draft **editDraft) {
	Data::Draft *historyLocalDraft = _history ? _history->localDraft() : nullptr;
	if (!localDraft && _editMsgId) localDraft = &historyLocalDraft;

	bool save = _peer && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.stop();
	if (_saveDraftText) {
		if (save) {
			Local::MessageDraft storedLocalDraft, storedEditDraft;
			if (localDraft) {
				if (*localDraft) {
					storedLocalDraft = Local::MessageDraft((*localDraft)->msgId, (*localDraft)->textWithTags, (*localDraft)->previewCancelled);
				}
			} else {
				storedLocalDraft = Local::MessageDraft(_replyToId, _field->getTextWithTags(), _previewCancelled);
			}
			if (editDraft) {
				if (*editDraft) {
					storedEditDraft = Local::MessageDraft((*editDraft)->msgId, (*editDraft)->textWithTags, (*editDraft)->previewCancelled);
				}
			} else if (_editMsgId) {
				storedEditDraft = Local::MessageDraft(_editMsgId, _field->getTextWithTags(), _previewCancelled);
			}
			Local::writeDrafts(_peer->id, storedLocalDraft, storedEditDraft);
			if (_migrated) {
				Local::writeDrafts(_migrated->peer->id, Local::MessageDraft(), Local::MessageDraft());
			}
		}
		_saveDraftText = false;
	}
	if (save) {
		MessageCursor localCursor, editCursor;
		if (localDraft) {
			if (*localDraft) {
				localCursor = (*localDraft)->cursor;
			}
		} else {
			localCursor = MessageCursor(_field);
		}
		if (editDraft) {
			if (*editDraft) {
				editCursor = (*editDraft)->cursor;
			}
		} else if (_editMsgId) {
			editCursor = MessageCursor(_field);
		}
		Local::writeDraftCursors(_peer->id, localCursor, editCursor);
		if (_migrated) {
			Local::writeDraftCursors(_migrated->peer->id, MessageCursor(), MessageCursor());
		}
	}

	if (!_editMsgId && !_inlineBot) {
		_saveCloudDraftTimer.start(SaveCloudDraftIdleTimeout);
	}
}

void HistoryWidget::cancelSendAction(History *history, SendAction::Type type) {
	auto i = _sendActionRequests.find(qMakePair(history, type));
	if (i != _sendActionRequests.cend()) {
		MTP::cancel(i.value());
		_sendActionRequests.erase(i);
	}
}

void HistoryWidget::onCancelSendAction() {
	cancelSendAction(_history, SendAction::Type::Typing);
}

void HistoryWidget::updateSendAction(History *history, SendAction::Type type, int32 progress) {
	if (!history) return;

	auto doing = (progress >= 0);
	if (history->mySendActionUpdated(type, doing)) {
		cancelSendAction(history, type);
		if (doing) {
			using Type = SendAction::Type;
			MTPsendMessageAction action;
			switch (type) {
			case Type::Typing: action = MTP_sendMessageTypingAction(); break;
			case Type::RecordVideo: action = MTP_sendMessageRecordVideoAction(); break;
			case Type::UploadVideo: action = MTP_sendMessageUploadVideoAction(MTP_int(progress)); break;
			case Type::RecordVoice: action = MTP_sendMessageRecordAudioAction(); break;
			case Type::UploadVoice: action = MTP_sendMessageUploadAudioAction(MTP_int(progress)); break;
			case Type::UploadPhoto: action = MTP_sendMessageUploadPhotoAction(MTP_int(progress)); break;
			case Type::UploadFile: action = MTP_sendMessageUploadDocumentAction(MTP_int(progress)); break;
			case Type::ChooseLocation: action = MTP_sendMessageGeoLocationAction(); break;
			case Type::ChooseContact: action = MTP_sendMessageChooseContactAction(); break;
			case Type::PlayGame: action = MTP_sendMessageGamePlayAction(); break;
			}
			_sendActionRequests.insert(qMakePair(history, type), MTP::send(MTPmessages_SetTyping(history->peer->input, action), rpcDone(&HistoryWidget::sendActionDone)));
			if (type == Type::Typing) _sendActionStopTimer.start(5000);
		}
	}
}

void HistoryWidget::updateRecentStickers() {
	_emojiPan->refreshStickers();
}

void HistoryWidget::stickersInstalled(uint64 setId) {
	_emojiPan->stickersInstalled(setId);
}

void HistoryWidget::sendActionDone(const MTPBool &result, mtpRequestId req) {
	for (auto i = _sendActionRequests.begin(), e = _sendActionRequests.end(); i != e; ++i) {
		if (i.value() == req) {
			_sendActionRequests.erase(i);
			break;
		}
	}
}

void HistoryWidget::activate() {
	if (_history) {
		if (!_histInited) {
			updateListSize(true);
		} else if (hasPendingResizedItems()) {
			updateListSize();
		}
	}
	if (App::wnd()) App::wnd()->setInnerFocus();
}

void HistoryWidget::setInnerFocus() {
	if (_scroll->isHidden()) {
		setFocus();
	} else if (_list) {
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked() || !_canSendMessages) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
}

void HistoryWidget::onRecordError() {
	stopRecording(false);
}

void HistoryWidget::onRecordDone(QByteArray result, VoiceWaveform waveform, qint32 samples) {
	if (!canWriteMessage() || result.isEmpty()) return;

	App::wnd()->activateWindow();
	auto duration = samples / Media::Player::kDefaultFrequency;
	auto to = FileLoadTo(_peer->id, _silent->checked(), replyToId());
	auto caption = QString();
	_fileLoader.addTask(MakeShared<FileLoadTask>(result, duration, waveform, to, caption));
	cancelReplyAfterMediaSend(lastForceReplyReplied());
}

void HistoryWidget::onRecordUpdate(quint16 level, qint32 samples) {
	if (!_recording) {
		return;
	}

	a_recordingLevel.start(level);
	_a_recording.start();
	_recordingSamples = samples;
	if (samples < 0 || samples >= Media::Player::kDefaultFrequency * AudioVoiceMsgMaxLength) {
		stopRecording(_peer && samples > 0 && _inField);
	}
	updateField();
	if (_peer && (!_peer->isChannel() || _peer->isMegagroup())) {
		updateSendAction(_history, SendAction::Type::RecordVoice);
	}
}

void HistoryWidget::updateStickers() {
	auto now = getms(true);
	if (!Global::LastStickersUpdate() || now >= Global::LastStickersUpdate() + kStickersUpdateTimeout) {
		if (!_stickersUpdateRequest) {
			_stickersUpdateRequest = MTP::send(MTPmessages_GetAllStickers(MTP_int(Local::countStickersHash(true))), rpcDone(&HistoryWidget::stickersGot), rpcFail(&HistoryWidget::stickersFailed));
		}
	}
	if (!Global::LastRecentStickersUpdate() || now >= Global::LastRecentStickersUpdate() + kStickersUpdateTimeout) {
		if (!_recentStickersUpdateRequest) {
			_recentStickersUpdateRequest = MTP::send(MTPmessages_GetRecentStickers(MTP_flags(0), MTP_int(Local::countRecentStickersHash())), rpcDone(&HistoryWidget::recentStickersGot), rpcFail(&HistoryWidget::recentStickersFailed));
		}
	}
	if (!Global::LastFeaturedStickersUpdate() || now >= Global::LastFeaturedStickersUpdate() + kStickersUpdateTimeout) {
		if (!_featuredStickersUpdateRequest) {
			_featuredStickersUpdateRequest = MTP::send(MTPmessages_GetFeaturedStickers(MTP_int(Local::countFeaturedStickersHash())), rpcDone(&HistoryWidget::featuredStickersGot), rpcFail(&HistoryWidget::featuredStickersFailed));
		}
	}
	if (!cLastSavedGifsUpdate() || now >= cLastSavedGifsUpdate() + kStickersUpdateTimeout) {
		if (!_savedGifsUpdateRequest) {
			_savedGifsUpdateRequest = MTP::send(MTPmessages_GetSavedGifs(MTP_int(Local::countSavedGifsHash())), rpcDone(&HistoryWidget::savedGifsGot), rpcFail(&HistoryWidget::savedGifsFailed));
		}
	}
}

void HistoryWidget::notify_botCommandsChanged(UserData *user) {
	if (_peer && (_peer == user || !_peer->isUser())) {
		if (_fieldAutocomplete->clearFilteredBotCommands()) {
			onCheckFieldAutocomplete();
		}
	}
}

void HistoryWidget::notify_inlineBotRequesting(bool requesting) {
	_attachEmoji->setLoading(requesting);
}

void HistoryWidget::notify_replyMarkupUpdated(const HistoryItem *item) {
	if (_keyboard->forMsgId() == item->fullId()) {
		updateBotKeyboard(item->history(), true);
	}
}

void HistoryWidget::notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	if (_history == item->history() || _migrated == item->history()) {
		if (int move = _list->moveScrollFollowingInlineKeyboard(item, oldKeyboardTop, newKeyboardTop)) {
			_addToScroll = move;
		}
	}
}

bool HistoryWidget::notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	if (samePeerBot) {
		if (_history) {
			TextWithTags textWithTags = { '@' + samePeerBot->username + ' ' + query, TextWithTags::Tags() };
			MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
			auto replyTo = _history->peer->isUser() ? 0 : samePeerReplyTo;
			_history->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, replyTo, cursor, false));
			applyDraft();
			return true;
		}
	} else if (auto bot = _peer ? _peer->asUser() : nullptr) {
		PeerId toPeerId = bot->botInfo ? bot->botInfo->inlineReturnPeerId : 0;
		if (!toPeerId) {
			return false;
		}
		bot->botInfo->inlineReturnPeerId = 0;
		History *h = App::history(toPeerId);
		TextWithTags textWithTags = { '@' + bot->username + ' ' + query, TextWithTags::Tags() };
		MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
		h->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
		if (h == _history) {
			applyDraft();
		} else {
			Ui::showPeerHistory(toPeerId, ShowAtUnreadMsgId);
		}
		return true;
	}
	return false;
}

void HistoryWidget::notify_userIsBotChanged(UserData *user) {
	if (_peer && _peer == user) {
		_list->notifyIsBotChanged();
		_list->updateBotInfo();
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::notify_migrateUpdated(PeerData *peer) {
	if (_peer) {
		if (_peer == peer) {
			if (peer->migrateTo()) {
				showHistory(peer->migrateTo()->id, (_showAtMsgId > 0) ? (-_showAtMsgId) : _showAtMsgId, true);
			} else if ((_migrated ? _migrated->peer : 0) != peer->migrateFrom()) {
				History *migrated = peer->migrateFrom() ? App::history(peer->migrateFrom()->id) : 0;
				if (_migrated || (migrated && migrated->unreadCount() > 0)) {
					showHistory(peer->id, peer->migrateFrom() ? _showAtMsgId : ((_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId) ? ShowAtUnreadMsgId : _showAtMsgId), true);
				} else {
					_migrated = migrated;
					_list->notifyMigrateUpdated();
					updateListSize();
				}
			}
		} else if (_migrated && _migrated->peer == peer && peer->migrateTo() != _peer) {
			showHistory(_peer->id, _showAtMsgId, true);
		}
	}
}

void HistoryWidget::notify_clipStopperHidden(ClipStopperType type) {
	if (_list) _list->update();
}

bool HistoryWidget::cmd_search() {
	if (!inFocusChain() || !_peer) return false;

	App::main()->searchInPeer(_peer);
	return true;
}

bool HistoryWidget::cmd_next_chat() {
	PeerData *p = 0;
	MsgId m = 0;
	App::main()->peerAfter(_peer, qMax(_showAtMsgId, 0), p, m);
	if (p) {
		Ui::showPeerHistory(p, m);
		return true;
	}
	return false;
}

bool HistoryWidget::cmd_previous_chat() {
	PeerData *p = 0;
	MsgId m = 0;
	App::main()->peerBefore(_peer, qMax(_showAtMsgId, 0), p, m);
	if (p) {
		Ui::showPeerHistory(p, m);
		return true;
	}
	return false;
}

void HistoryWidget::stickersGot(const MTPmessages_AllStickers &stickers) {
	Global::SetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_allStickers) return;
	auto &d = stickers.c_messages_allStickers();

	auto &d_sets = d.vsets.v;

	auto &setsOrder = Global::RefStickerSetsOrder();
	setsOrder.clear();

	auto &sets = Global::RefStickerSets();
	QMap<uint64, uint64> setsToRequest;
	for (auto &set : sets) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived)) {
			set.flags &= ~MTPDstickerSet::Flag::f_installed; // mark for removing
		}
	}
	for_const (auto &setData, d_sets) {
		if (setData.type() == mtpc_stickerSet) {
			auto set = Stickers::feedSet(setData.c_stickerSet());
			if (!(set->flags & MTPDstickerSet::Flag::f_archived) || (set->flags & MTPDstickerSet::Flag::f_official)) {
				setsOrder.push_back(set->id);
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					setsToRequest.insert(set->id, set->access);
				}
			}
		}
	}
	bool writeRecent = false;
	RecentStickerPack &recent(cGetRecentStickers());
	for (Stickers::Sets::iterator it = sets.begin(), e = sets.end(); it != e;) {
		bool installed = (it->flags & MTPDstickerSet::Flag::f_installed);
		bool featured = (it->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (it->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (it->flags & MTPDstickerSet::Flag::f_archived);
		if (!installed) { // remove not mine sets from recent stickers
			for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
		}
		if (installed || featured || special || archived) {
			++it;
		} else {
			it = sets.erase(it);
		}
	}

	if (Local::countStickersHash() != d.vhash.v) {
		LOG(("API Error: received stickers hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countStickersHash()));
	}

	if (!setsToRequest.isEmpty() && App::api()) {
		for (QMap<uint64, uint64>::const_iterator i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			App::api()->scheduleStickerSetRequest(i.key(), i.value());
		}
		App::api()->requestStickerSets();
	}

	Local::writeInstalledStickers();
	if (writeRecent) Local::writeUserSettings();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::stickersFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get stickers!"));

	Global::SetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::recentStickersGot(const MTPmessages_RecentStickers &stickers) {
	Global::SetLastRecentStickersUpdate(getms(true));
	_recentStickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_recentStickers) return;
	auto &d = stickers.c_messages_recentStickers();

	auto &sets = Global::RefStickerSets();
	auto it = sets.find(Stickers::CloudRecentSetId);

	auto &d_docs = d.vstickers.v;
	if (d_docs.isEmpty()) {
		if (it != sets.cend()) {
			sets.erase(it);
		}
	} else {
		if (it == sets.cend()) {
			it = sets.insert(Stickers::CloudRecentSetId, Stickers::Set(Stickers::CloudRecentSetId, 0, lang(lng_recent_stickers), QString(), 0, 0, qFlags(MTPDstickerSet_ClientFlag::f_special)));
		} else {
			it->title = lang(lng_recent_stickers);
		}
		it->hash = d.vhash.v;

		auto custom = sets.find(Stickers::CustomSetId);

		StickerPack pack;
		pack.reserve(d_docs.size());
		for (int32 i = 0, l = d_docs.size(); i != l; ++i) {
			DocumentData *doc = App::feedDocument(d_docs.at(i));
			if (!doc || !doc->sticker()) continue;

			pack.push_back(doc);
			if (custom != sets.cend()) {
				int32 index = custom->stickers.indexOf(doc);
				if (index >= 0) {
					custom->stickers.removeAt(index);
				}
			}
		}
		if (custom != sets.cend() && custom->stickers.isEmpty()) {
			sets.erase(custom);
			custom = sets.end();
		}

		bool writeRecent = false;
		RecentStickerPack &recent(cGetRecentStickers());
		for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
			if (it->stickers.indexOf(i->first) >= 0 && pack.indexOf(i->first) < 0) {
				i = recent.erase(i);
				writeRecent = true;
			} else {
				++i;
			}
		}

		if (pack.isEmpty()) {
			sets.erase(it);
		} else {
			it->stickers = pack;
			it->emoji.clear();
		}

		if (writeRecent) {
			Local::writeUserSettings();
		}
	}

	if (Local::countRecentStickersHash() != d.vhash.v) {
		LOG(("API Error: received stickers hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countRecentStickersHash()));
	}

	Local::writeRecentStickers();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::recentStickersFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get recent stickers!"));

	Global::SetLastRecentStickersUpdate(getms(true));
	_recentStickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::featuredStickersGot(const MTPmessages_FeaturedStickers &stickers) {
	Global::SetLastFeaturedStickersUpdate(getms(true));
	_featuredStickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_featuredStickers) return;
	auto &d = stickers.c_messages_featuredStickers();

	OrderedSet<uint64> unread;
	for_const (auto &unreadSetId, d.vunread.v) {
		unread.insert(unreadSetId.v);
	}

	auto &d_sets = d.vsets.v;

	auto &setsOrder = Global::RefFeaturedStickerSetsOrder();
	setsOrder.clear();

	auto &sets = Global::RefStickerSets();
	QMap<uint64, uint64> setsToRequest;
	for (auto &set : sets) {
		set.flags &= ~MTPDstickerSet_ClientFlag::f_featured; // mark for removing
	}
	for (int i = 0, l = d_sets.size(); i != l; ++i) {
		auto &setData = d_sets[i];
		const MTPDstickerSet *set = nullptr;
		switch (setData.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = setData.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				set = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = setData.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				set = &d.vset.c_stickerSet();
			}
		} break;
		}

		if (set) {
			auto it = sets.find(set->vid.v);
			auto title = stickerSetTitle(*set);
			if (it == sets.cend()) {
				auto setClientFlags = MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_not_loaded;
				if (unread.contains(set->vid.v)) {
					setClientFlags |= MTPDstickerSet_ClientFlag::f_unread;
				}
				it = sets.insert(set->vid.v, Stickers::Set(set->vid.v, set->vaccess_hash.v, title, qs(set->vshort_name), set->vcount.v, set->vhash.v, set->vflags.v | setClientFlags));
			} else {
				it->access = set->vaccess_hash.v;
				it->title = title;
				it->shortName = qs(set->vshort_name);
				auto clientFlags = it->flags & (MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_unread | MTPDstickerSet_ClientFlag::f_not_loaded | MTPDstickerSet_ClientFlag::f_special);
				it->flags = set->vflags.v | clientFlags;
				it->flags |= MTPDstickerSet_ClientFlag::f_featured;
				if (unread.contains(it->id)) {
					it->flags |= MTPDstickerSet_ClientFlag::f_unread;
				} else {
					it->flags &= ~MTPDstickerSet_ClientFlag::f_unread;
				}
				if (it->count != set->vcount.v || it->hash != set->vhash.v || it->emoji.isEmpty()) {
					it->count = set->vcount.v;
					it->hash = set->vhash.v;
					it->flags |= MTPDstickerSet_ClientFlag::f_not_loaded; // need to request this set
				}
			}
			setsOrder.push_back(set->vid.v);
			if (it->stickers.isEmpty() || (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
				setsToRequest.insert(set->vid.v, set->vaccess_hash.v);
			}
		}
	}

	int unreadCount = 0;
	for (auto it = sets.begin(), e = sets.end(); it != e;) {
		bool installed = (it->flags & MTPDstickerSet::Flag::f_installed);
		bool featured = (it->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (it->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (it->flags & MTPDstickerSet::Flag::f_archived);
		if (installed || featured || special || archived) {
			if (featured && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
				++unreadCount;
			}
			++it;
		} else {
			it = sets.erase(it);
		}
	}
	if (Global::FeaturedStickerSetsUnreadCount() != unreadCount) {
		Global::SetFeaturedStickerSetsUnreadCount(unreadCount);
		Global::RefFeaturedStickerSetsUnreadCountChanged().notify();
	}

	if (Local::countFeaturedStickersHash() != d.vhash.v) {
		LOG(("API Error: received featured stickers hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countFeaturedStickersHash()));
	}

	if (!setsToRequest.isEmpty() && App::api()) {
		for (QMap<uint64, uint64>::const_iterator i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			App::api()->scheduleStickerSetRequest(i.key(), i.value());
		}
		App::api()->requestStickerSets();
	}

	Local::writeFeaturedStickers();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::featuredStickersFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get featured stickers!"));

	Global::SetLastFeaturedStickersUpdate(getms(true));
	_featuredStickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::savedGifsGot(const MTPmessages_SavedGifs &gifs) {
	cSetLastSavedGifsUpdate(getms(true));
	_savedGifsUpdateRequest = 0;

	if (gifs.type() != mtpc_messages_savedGifs) return;
	auto &d = gifs.c_messages_savedGifs();

	auto &d_gifs = d.vgifs.v;

	SavedGifs &saved(cRefSavedGifs());
	saved.clear();

	saved.reserve(d_gifs.size());
	for (int32 i = 0, l = d_gifs.size(); i != l; ++i) {
		DocumentData *doc = App::feedDocument(d_gifs.at(i));
		if (!doc || !doc->isAnimation()) {
			LOG(("API Error: bad document returned in HistoryWidget::savedGifsGot!"));
			continue;
		}

		saved.push_back(doc);
	}
	if (Local::countSavedGifsHash() != d.vhash.v) {
		LOG(("API Error: received saved gifs hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countSavedGifsHash()));
	}

	Local::writeSavedGifs();

	if (App::main()) emit App::main()->savedGifsUpdated();
}

void HistoryWidget::saveGif(DocumentData *doc) {
	if (doc->isGifv() && cSavedGifs().indexOf(doc) != 0) {
		MTPInputDocument mtpInput = doc->mtpInput();
		if (mtpInput.type() != mtpc_inputDocumentEmpty) {
			MTP::send(MTPmessages_SaveGif(mtpInput, MTP_bool(false)), rpcDone(&HistoryWidget::saveGifDone, doc));
		}
	}
}

void HistoryWidget::saveGifDone(DocumentData *doc, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		App::addSavedGif(doc);
	}
}

bool HistoryWidget::savedGifsFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get saved gifs!"));

	cSetLastSavedGifsUpdate(getms(true));
	_savedGifsUpdateRequest = 0;
	return true;
}

void HistoryWidget::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = 0;
}

void HistoryWidget::pushReplyReturn(HistoryItem *item) {
	if (!item) return;
	if (item->history() == _history) {
		_replyReturns.push_back(item->id);
	} else if (item->history() == _migrated) {
		_replyReturns.push_back(-item->id);
	} else {
		return;
	}
	_replyReturn = item;
	updateControlsVisibility();
}

QList<MsgId> HistoryWidget::replyReturns() {
	return _replyReturns;
}

void HistoryWidget::setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns) {
	if (!_peer || _peer->id != peer) return;

	_replyReturns = replyReturns;
	if (_replyReturns.isEmpty()) {
		_replyReturn = 0;
	} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
		_replyReturn = App::histItemById(0, -_replyReturns.back());
	} else {
		_replyReturn = App::histItemById(_channel, _replyReturns.back());
	}
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = 0;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = App::histItemById(0, -_replyReturns.back());
		} else {
			_replyReturn = App::histItemById(_channel, _replyReturns.back());
		}
	}
	updateControlsVisibility();
}

void HistoryWidget::calcNextReplyReturn() {
	_replyReturn = 0;
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = 0;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = App::histItemById(0, -_replyReturns.back());
		} else {
			_replyReturn = App::histItemById(_channel, _replyReturns.back());
		}
	}
	if (!_replyReturn) updateControlsVisibility();
}

void HistoryWidget::fastShowAtEnd(History *h) {
	if (h == _history) {
		h->getReadyFor(ShowAtTheEndMsgId);

		clearAllLoadRequests();

		setMsgId(ShowAtUnreadMsgId);
		_histInited = false;

		if (h->isReadyFor(_showAtMsgId)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}
	} else if (h) {
		h->getReadyFor(ShowAtTheEndMsgId);
	}
}

void HistoryWidget::applyDraft(bool parseLinks) {
	auto draft = _history ? _history->draft() : nullptr;
	if (!draft || !canWriteMessage()) {
		clearFieldText();
		_field->setFocus();
		_replyEditMsg = nullptr;
		_editMsgId = _replyToId = 0;
		return;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags);
	_field->setFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;
	_previewCancelled = draft->previewCancelled;
	_replyEditMsg = nullptr;
	if (auto editDraft = _history->editDraft()) {
		_editMsgId = editDraft->msgId;
		_replyToId = 0;
	} else {
		_editMsgId = 0;
		_replyToId = readyToForward() ? 0 : _history->localDraft()->msgId;
	}

	if (parseLinks) {
		onPreviewParse();
	}
	if (_editMsgId || _replyToId) {
		updateReplyEditTexts();
		if (!_replyEditMsg && App::api()) {
			App::api()->requestMessageData(_peer->asChannel(), _editMsgId ? _editMsgId : _replyToId, replyEditMessageDataCallback());
		}
	}
}

void HistoryWidget::applyCloudDraft(History *history) {
	if (_history == history && !_editMsgId) {
		applyDraft();

		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::showHistory(const PeerId &peerId, MsgId showAtMsgId, bool reload) {
	MsgId wasMsgId = _showAtMsgId;
	History *wasHistory = _history;

	bool startBot = (showAtMsgId == ShowAndStartBotMsgId);
	if (startBot) {
		showAtMsgId = ShowAtTheEndMsgId;
	}

	if (_history) {
		if (_peer->id == peerId && !reload) {
			bool canShowNow = _history->isReadyFor(showAtMsgId);
			if (!canShowNow) {
				delayedShowAt(showAtMsgId);
			} else {
				_history->forgetScrollState();
				if (_migrated) {
					_migrated->forgetScrollState();
				}

				clearDelayedShowAt();
				if (_replyReturn) {
					if (_replyReturn->history() == _history && _replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					} else if (_replyReturn->history() == _migrated && -_replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					}
				}

				_showAtMsgId = showAtMsgId;
				_histInited = false;

				historyLoaded();
			}
			App::main()->dlgUpdated(wasHistory ? wasHistory->peer : nullptr, wasMsgId);
			emit historyShown(_history, _showAtMsgId);

			_topBar->update();
			update();

			if (startBot && _peer->isUser() && _peer->asUser()->botInfo) {
				if (wasHistory) _peer->asUser()->botInfo->inlineReturnPeerId = wasHistory->peer->id;
				onBotStart();
				_history->clearLocalDraft();
				applyDraft();
				_send->finishAnimation();
			}
			return;
		}
		updateSendAction(_history, SendAction::Type::Typing, -1);
	}

	if (!cAutoPlayGif()) {
		App::stopGifItems();
	}
	clearReplyReturns();

	clearAllLoadRequests();

	if (_history) {
		if (App::main()) App::main()->saveDraftToCloud();
		if (_migrated) {
			_migrated->clearLocalDraft(); // use migrated draft only once
			_migrated->clearEditDraft();
		}

		_history->showAtMsgId = _showAtMsgId;

		destroyUnreadBar();
		destroyPinnedBar();
		_history = _migrated = nullptr;
		_peer = nullptr;
		_channel = NoChannel;
		_canSendMessages = false;
		updateBotKeyboard();
	}

	App::clearMousedItems();

	_addToScroll = 0;
	_saveEditMsgRequestId = 0;
	_replyEditMsg = nullptr;
	_editMsgId = _replyToId = 0;
	_previewData = 0;
	_previewCache.clear();
	_fieldBarCancel->hide();

	_membersDropdownShowTimer.stop();
	_scroll->takeWidget<HistoryInner>().destroyDelayed();
	_list = nullptr;

	clearInlineBot();

	_showAtMsgId = showAtMsgId;
	_histInited = false;

	if (peerId) {
		_peer = App::peer(peerId);
		_channel = peerToChannel(_peer->id);
		_canSendMessages = canSendMessages(_peer);
	}
	updateTopBarSelection();

	if (_peer && _peer->isChannel()) {
		_peer->asChannel()->updateFull();
		_joinChannel->setText(lang(_peer->isMegagroup() ? lng_group_invite_join : lng_channel_join).toUpper());
	}

	_unblockRequest = _reportSpamRequest = 0;
	if (_reportSpamSettingRequestId > 0) {
		MTP::cancel(_reportSpamSettingRequestId);
	}
	_reportSpamSettingRequestId = ReportSpamRequestNeeded;

	_titlePeerText = QString();
	_titlePeerTextWidth = 0;

	noSelectingScroll();
	_selCount = 0;
	_topBar->showSelected(0);

	App::hoveredItem(nullptr);
	App::pressedItem(nullptr);
	App::hoveredLinkItem(nullptr);
	App::pressedLinkItem(nullptr);
	App::contextItem(nullptr);
	App::mousedItem(nullptr);

	if (_peer) {
		App::forgetMedia();
		_serviceImageCacheSize = imageCacheSize();
		AuthSession::Current().downloader().clearPriorities();

		_history = App::history(_peer->id);
		_migrated = _peer->migrateFrom() ? App::history(_peer->migrateFrom()->id) : 0;

		if (_channel) {
			updateNotifySettings();
			if (_peer->notify == UnknownNotifySettings) {
				App::api()->requestNotifySetting(_peer);
			}
		}

		if (_showAtMsgId == ShowAtUnreadMsgId) {
			if (_history->scrollTopItem) {
				_showAtMsgId = _history->showAtMsgId;
			}
		} else {
			_history->forgetScrollState();
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		}

		_scroll->hide();
		_list = _scroll->setOwnedWidget(object_ptr<HistoryInner>(this, _scroll, _history));
		_list->show();

		_updateHistoryItems.stop();

		pinnedMsgVisibilityUpdated();
		if (_history->scrollTopItem || (_migrated && _migrated->scrollTopItem) || _history->isReadyFor(_showAtMsgId)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}

		emit App::main()->peerUpdated(_peer);

		Local::readDraftsWithCursors(_history);
		if (_migrated) {
			Local::readDraftsWithCursors(_migrated);
			_migrated->clearEditDraft();
			_history->takeLocalDraft(_migrated);
		}
		applyDraft(false);
		_send->finishAnimation();

		resizeEvent(nullptr);
		if (!_previewCancelled) {
			onPreviewParse();
		}

		connect(_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));

		if (startBot && _peer->isUser() && _peer->asUser()->botInfo) {
			if (wasHistory) _peer->asUser()->botInfo->inlineReturnPeerId = wasHistory->peer->id;
			onBotStart();
		}
		unreadCountChanged(_history); // set _historyDown badge.
	} else {
		clearFieldText();
		doneShow();
	}

	if (App::wnd()) QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));

	App::main()->dlgUpdated(wasHistory ? wasHistory->peer : nullptr, wasMsgId);
	emit historyShown(_history, _showAtMsgId);

	_controller->historyPeerChanged().notify(_peer, true);
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

void HistoryWidget::updateAfterDrag() {
	if (_list) _list->dragActionUpdate(QCursor::pos());
}

void HistoryWidget::updateFieldSubmitSettings() {
	auto settings = Ui::FlatTextarea::SubmitSettings::Enter;
	if (_isInlineBot) {
		settings = Ui::FlatTextarea::SubmitSettings::None;
	} else if (cCtrlEnter()) {
		settings = Ui::FlatTextarea::SubmitSettings::CtrlEnter;
	}
	_field->setSubmitSettings(settings);
}

void HistoryWidget::updateNotifySettings() {
	if (!_peer || !_peer->isChannel()) return;

	_muteUnmute->setText(lang(_history->mute() ? lng_channel_unmute : lng_channel_mute).toUpper());
	if (_peer->notify != UnknownNotifySettings) {
		_silent->setChecked(_peer->notify != EmptyNotifySettings && (_peer->notify->flags & MTPDpeerNotifySettings::Flag::f_silent));
		if (_silent->isHidden() && hasSilentToggle()) {
			updateControlsVisibility();
		}
	}
}

bool HistoryWidget::contentOverlapped(const QRect &globalRect) {
	return (_attachDragDocument->overlaps(globalRect)
			|| _attachDragPhoto->overlaps(globalRect)
			|| _fieldAutocomplete->overlaps(globalRect)
			|| _emojiPan->overlaps(globalRect)
			|| (_inlineResults && _inlineResults->overlaps(globalRect)));
}

void HistoryWidget::updateReportSpamStatus() {
	if (!_peer || (_peer->isUser() && (_peer->id == AuthSession::CurrentUserPeerId() || isNotificationsUser(_peer->id) || isServiceUser(_peer->id) || _peer->asUser()->botInfo))) {
		_reportSpamStatus = dbiprsHidden;
		return;
	} else if (!_firstLoadRequest && _history->isEmpty()) {
		_reportSpamStatus = dbiprsNoButton;
		if (cReportSpamStatuses().contains(_peer->id)) {
			cRefReportSpamStatuses().remove(_peer->id);
			Local::writeReportSpamStatuses();
		}
		return;
	} else {
		ReportSpamStatuses::const_iterator i = cReportSpamStatuses().constFind(_peer->id);
		if (i != cReportSpamStatuses().cend()) {
			_reportSpamStatus = i.value();
			if (_reportSpamStatus == dbiprsNoButton) {
				_reportSpamStatus = dbiprsHidden;
				if (!_peer->isUser() || _peer->asUser()->contact < 1) {
					MTP::send(MTPmessages_HideReportSpam(_peer->input));
				}

				cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
				Local::writeReportSpamStatuses();
			} else if (_reportSpamStatus == dbiprsShowButton) {
				requestReportSpamSetting();
			}
			_reportSpamPanel->setReported(_reportSpamStatus == dbiprsReportSent, _peer);
			return;
		} else if (_peer->migrateFrom()) { // migrate report status
			i = cReportSpamStatuses().constFind(_peer->migrateFrom()->id);
			if (i != cReportSpamStatuses().cend()) {
				_reportSpamStatus = i.value();
				if (_reportSpamStatus == dbiprsNoButton) {
					_reportSpamStatus = dbiprsHidden;
					if (!_peer->isUser() || _peer->asUser()->contact < 1) {
						MTP::send(MTPmessages_HideReportSpam(_peer->input));
					}
				} else if (_reportSpamStatus == dbiprsShowButton) {
					requestReportSpamSetting();
				}
				cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
				Local::writeReportSpamStatuses();

				_reportSpamPanel->setReported(_reportSpamStatus == dbiprsReportSent, _peer);
				return;
			}
		}
	}
	if (!AuthSession::Current().data().contactsLoaded().value() || _firstLoadRequest) {
		_reportSpamStatus = dbiprsUnknown;
	} else if (_peer->isUser() && _peer->asUser()->contact > 0) {
		_reportSpamStatus = dbiprsHidden;
	} else {
		_reportSpamStatus = dbiprsRequesting;
		requestReportSpamSetting();
	}
	if (_reportSpamStatus == dbiprsHidden) {
		_reportSpamPanel->setReported(false, _peer);
		cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
		Local::writeReportSpamStatuses();
	}
}

void HistoryWidget::requestReportSpamSetting() {
	if (_reportSpamSettingRequestId >= 0 || !_peer) return;

	_reportSpamSettingRequestId = MTP::send(MTPmessages_GetPeerSettings(_peer->input), rpcDone(&HistoryWidget::reportSpamSettingDone), rpcFail(&HistoryWidget::reportSpamSettingFail));
}

void HistoryWidget::reportSpamSettingDone(const MTPPeerSettings &result, mtpRequestId req) {
	if (req != _reportSpamSettingRequestId) return;

	_reportSpamSettingRequestId = 0;
	if (result.type() == mtpc_peerSettings) {
		const auto &d(result.c_peerSettings());
		DBIPeerReportSpamStatus status = d.is_report_spam() ? dbiprsShowButton : dbiprsHidden;
		if (status != _reportSpamStatus) {
			_reportSpamStatus = status;
			_reportSpamPanel->setReported(false, _peer);

			cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
			Local::writeReportSpamStatuses();

			updateControlsVisibility();
		}
	}
}

bool HistoryWidget::reportSpamSettingFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (req == _reportSpamSettingRequestId) {
		req = 0;
	}
	return true;
}

bool HistoryWidget::canWriteMessage() const {
	if (!_history || !_canSendMessages) return false;
	if (isBlocked() || isJoinChannel() || isMuteUnmute() || isBotStart()) return false;
	return true;
}

void HistoryWidget::updateControlsVisibility() {
	if (!_a_show.animating()) {
		_topShadow->setVisible(_peer != nullptr);
		_topBar->setVisible(_peer != nullptr);
	}
	updateHistoryDownVisibility();
	if (!_history || _a_show.animating()) {
		_reportSpamPanel->hide();
		_scroll->hide();
		_kbScroll->hide();
		_send->hide();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_fieldAutocomplete->hide();
		_field->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		_attachEmoji->hide();
		_silent->hide();
		_historyDown->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		_emojiPan->hide();
		if (_inlineResults) {
			_inlineResults->hide();
		}
		if (_pinnedBar) {
			_pinnedBar->cancel->hide();
			_pinnedBar->shadow->hide();
		}
		return;
	}

	if (_pinnedBar) {
		_pinnedBar->cancel->show();
		_pinnedBar->shadow->show();
	}
	if (_firstLoadRequest && !_scroll->isHidden()) {
		_scroll->hide();
	} else if (!_firstLoadRequest && _scroll->isHidden()) {
		_scroll->show();
	}
	if (_reportSpamStatus == dbiprsShowButton || _reportSpamStatus == dbiprsReportSent) {
		_reportSpamPanel->show();
	} else {
		_reportSpamPanel->hide();
	}
	if (isBlocked() || isJoinChannel() || isMuteUnmute()) {
		if (isBlocked()) {
			_joinChannel->hide();
			_muteUnmute->hide();
			if (_unblock->isHidden()) {
				_unblock->clearState();
				_unblock->show();
			}
		} else if (isJoinChannel()) {
			_unblock->hide();
			_muteUnmute->hide();
			if (_joinChannel->isHidden()) {
				_joinChannel->clearState();
				_joinChannel->show();
			}
		} else if (isMuteUnmute()) {
			_unblock->hide();
			_joinChannel->hide();
			if (_muteUnmute->isHidden()) {
				_muteUnmute->clearState();
				_muteUnmute->show();
			}
		}
		_kbShown = false;
		_fieldAutocomplete->hide();
		_send->hide();
		_botStart->hide();
		_attachToggle->hide();
		_silent->hide();
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		_attachEmoji->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		_emojiPan->hide();
		if (_inlineResults) {
			_inlineResults->hide();
		}
		if (!_field->isHidden()) {
			_field->hide();
			resizeEvent(0);
			update();
		}
	} else if (_canSendMessages) {
		onCheckFieldAutocomplete();
		if (isBotStart()) {
			_unblock->hide();
			_joinChannel->hide();
			_muteUnmute->hide();
			if (_botStart->isHidden()) {
				_botStart->clearState();
				_botStart->show();
			}
			_kbShown = false;
			_send->hide();
			_field->hide();
			_attachEmoji->hide();
			_botKeyboardShow->hide();
			_botKeyboardHide->hide();
			_botCommandStart->hide();
			_attachToggle->hide();
			_silent->hide();
			_kbScroll->hide();
			_fieldBarCancel->hide();
		} else {
			_unblock->hide();
			_botStart->hide();
			_joinChannel->hide();
			_muteUnmute->hide();
			_send->show();
			updateSendButtonType();
			if (_recording) {
				_field->hide();
				_attachEmoji->hide();
				_botKeyboardShow->hide();
				_botKeyboardHide->hide();
				_botCommandStart->hide();
				_attachToggle->hide();
				_silent->hide();
				if (_kbShown) {
					_kbScroll->show();
				} else {
					_kbScroll->hide();
				}
			} else {
				_field->show();
				if (_kbShown) {
					_kbScroll->show();
					_attachEmoji->hide();
					_botKeyboardHide->show();
					_botKeyboardShow->hide();
					_botCommandStart->hide();
				} else if (_kbReplyTo) {
					_kbScroll->hide();
					_attachEmoji->show();
					_botKeyboardHide->hide();
					_botKeyboardShow->hide();
					_botCommandStart->hide();
				} else {
					_kbScroll->hide();
					_attachEmoji->show();
					_botKeyboardHide->hide();
					if (_keyboard->hasMarkup()) {
						_botKeyboardShow->show();
						_botCommandStart->hide();
					} else {
						_botKeyboardShow->hide();
						if (_cmdStartShown) {
							_botCommandStart->show();
						} else {
							_botCommandStart->hide();
						}
					}
				}
				_attachToggle->show();
				if (hasSilentToggle()) {
					_silent->show();
				} else {
					_silent->hide();
				}
				updateFieldPlaceholder();
			}
			if (_editMsgId || _replyToId || readyToForward() || (_previewData && _previewData->pendingTill >= 0) || _kbReplyTo) {
				if (_fieldBarCancel->isHidden()) {
					_fieldBarCancel->show();
					resizeEvent(0);
					update();
				}
			} else {
				_fieldBarCancel->hide();
			}
		}
	} else {
		_fieldAutocomplete->hide();
		_send->hide();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_attachToggle->hide();
		_silent->hide();
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		_attachEmoji->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		_emojiPan->hide();
		if (_inlineResults) {
			_inlineResults->hide();
		}
		_kbScroll->hide();
		if (!_field->isHidden()) {
			_field->hide();
			resizeEvent(0);
			update();
		}
	}
	updateMouseTracking();
}

void HistoryWidget::updateMouseTracking() {
	bool trackMouse = !_fieldBarCancel->isHidden() || _pinnedBar;
	setMouseTracking(trackMouse);
}

void HistoryWidget::destroyUnreadBar() {
	if (_history) _history->destroyUnreadBar();
	if (_migrated) _migrated->destroyUnreadBar();
}

void HistoryWidget::newUnreadMsg(History *history, HistoryItem *item) {
	if (_history == history) {
		if (_scroll->scrollTop() + 1 > _scroll->scrollTopMax()) {
			destroyUnreadBar();
		}
		if (App::wnd()->doWeReadServerHistory()) {
			historyWasRead(ReadServerHistoryChecks::ForceRequest);
			return;
		}
	}
	AuthSession::Current().notifications().schedule(history, item);
	history->setUnreadCount(history->unreadCount() + 1);
}

void HistoryWidget::historyToDown(History *history) {
	history->forgetScrollState();
	if (auto migrated = App::historyLoaded(history->peer->migrateFrom())) {
		migrated->forgetScrollState();
	}
	if (history == _history) {
		_scroll->scrollToY(_scroll->scrollTopMax());
	}
}

void HistoryWidget::historyWasRead(ReadServerHistoryChecks checks) {
	App::main()->readServerHistory(_history, checks);
	if (_migrated) {
		App::main()->readServerHistory(_migrated, ReadServerHistoryChecks::OnlyIfUnread);
	}
}

void HistoryWidget::unreadCountChanged(History *history) {
	if (history == _history || history == _migrated) {
		updateHistoryDownVisibility();
		if (_historyDown) {
			_historyDown->setUnreadCount(_history->unreadCount() + (_migrated ? _migrated->unreadCount() : 0));
		}
	}
}

void HistoryWidget::historyCleared(History *history) {
	if (history == _history) {
		_list->dragActionCancel();
	}
}

bool HistoryWidget::messagesFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("CHANNEL_PRIVATE") || error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA") || error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		PeerData *was = _peer;
		App::main()->showBackFromStack();
		Ui::show(Box<InformBox>(lang((was && was->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
		return true;
	}

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_preloadRequest == requestId) {
		_preloadRequest = 0;
	} else if (_preloadDownRequest == requestId) {
		_preloadDownRequest = 0;
	} else if (_firstLoadRequest == requestId) {
		_firstLoadRequest = 0;
		App::main()->showBackFromStack();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
	}
	return true;
}

void HistoryWidget::messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, mtpRequestId requestId) {
	if (!_history) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	bool toMigrated = (peer == _peer->migrateFrom());
	if (peer != _peer && !toMigrated) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	int32 count = 0;
	const QVector<MTPMessage> emptyList, *histList = &emptyList;
	switch (messages.type()) {
	case mtpc_messages_messages: {
		auto &d(messages.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.v;
		count = histList->size();
	} break;
	case mtpc_messages_messagesSlice: {
		auto &d(messages.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.v;
		count = d.vcount.v;
	} break;
	case mtpc_messages_channelMessages: {
		auto &d(messages.c_messages_channelMessages());
		if (peer && peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.v;
		count = d.vcount.v;
	} break;
	}

	if (_preloadRequest == requestId) {
		addMessagesToFront(peer, *histList);
		_preloadRequest = 0;
		preloadHistoryIfNeeded();
		if (_reportSpamStatus == dbiprsUnknown) {
			updateReportSpamStatus();
			if (_reportSpamStatus != dbiprsUnknown) updateControlsVisibility();
		}
	} else if (_preloadDownRequest == requestId) {
		addMessagesToBack(peer, *histList);
		_preloadDownRequest = 0;
		preloadHistoryIfNeeded();
		if (_history->loadedAtBottom() && App::wnd()) App::wnd()->checkHistoryActivation();
	} else if (_firstLoadRequest == requestId) {
		if (toMigrated) {
			_history->clear(true);
		} else if (_migrated) {
			_migrated->clear(true);
		}
		addMessagesToFront(peer, *histList);
		_firstLoadRequest = 0;
		if (_history->loadedAtTop()) {
			if (_history->unreadCount() > count) {
				_history->setUnreadCount(count);
			}
			if (_history->isEmpty() && count > 0) {
				firstLoadMessages();
				return;
			}
		}

		historyLoaded();
	} else if (_delayedShowAtRequest == requestId) {
		if (toMigrated) {
			_history->clear(true);
		} else if (_migrated) {
			_migrated->clear(true);
		}

		_delayedShowAtRequest = 0;
		_history->getReadyFor(_delayedShowAtMsgId);
		if (_history->isEmpty()) {
			if (_preloadRequest) MTP::cancel(_preloadRequest);
			if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
			if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
			_preloadRequest = _preloadDownRequest = 0;
			_firstLoadRequest = -1; // hack - don't updateListSize yet
			addMessagesToFront(peer, *histList);
			_firstLoadRequest = 0;
			if (_history->loadedAtTop()) {
				if (_history->unreadCount() > count) {
					_history->setUnreadCount(count);
				}
				if (_history->isEmpty() && count > 0) {
					firstLoadMessages();
					return;
				}
			}
		}
		if (_replyReturn) {
			if (_replyReturn->history() == _history && _replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			} else if (_replyReturn->history() == _migrated && -_replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			}
		}

		setMsgId(_delayedShowAtMsgId);

		_histInited = false;

		historyLoaded();
	}
}

void HistoryWidget::historyLoaded() {
	countHistoryShowFrom();
	destroyUnreadBar();
	doneShow();
}

void HistoryWidget::windowShown() {
	resizeEvent(0);
}

bool HistoryWidget::doWeReadServerHistory() const {
	if (!_history || !_list) return true;
	if (_firstLoadRequest || _a_show.animating()) return false;
	if (_history->loadedAtBottom()) {
		int scrollTop = _scroll->scrollTop();
		if (scrollTop + 1 > _scroll->scrollTopMax()) return true;

		auto showFrom = (_migrated && _migrated->showFrom) ? _migrated->showFrom : (_history ? _history->showFrom : nullptr);
		if (showFrom && !showFrom->detached()) {
			int scrollBottom = scrollTop + _scroll->height();
			if (scrollBottom > _list->itemTop(showFrom)) return true;
		}
	}
	if (historyHasNotFreezedUnreadBar(_history)) {
		return true;
	}
	if (historyHasNotFreezedUnreadBar(_migrated)) {
		return true;
	}
	return false;
}

bool HistoryWidget::historyHasNotFreezedUnreadBar(History *history) const {
	if (history && history->showFrom && !history->showFrom->detached() && history->unreadBar) {
		if (auto unreadBar = history->unreadBar->Get<HistoryMessageUnreadBar>()) {
			return !unreadBar->_freezed;
		}
	}
	return false;
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) return;

	PeerData *from = _peer;
	int32 offset_id = 0, offset = 0, loadCount = MessagesPerPage;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (_migrated && _migrated->unreadCount()) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = _migrated->inboxReadBefore;
		} else if (_history->unreadCount()) {
			_history->getReadyFor(_showAtMsgId);
			offset = -loadCount / 2;
			offset_id = _history->inboxReadBefore;
		} else {
			_history->getReadyFor(ShowAtTheEndMsgId);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		_history->getReadyFor(_showAtMsgId);
		loadCount = MessagesFirstLoad;
	} else if (_showAtMsgId > 0) {
		_history->getReadyFor(_showAtMsgId);
		offset = -loadCount / 2;
		offset_id = _showAtMsgId;
	} else if (_showAtMsgId < 0 && _history->isChannel()) {
		if (_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId && _migrated) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = -_showAtMsgId;
		} else if (_showAtMsgId == SwitchAtTopMsgId) {
			_history->getReadyFor(_showAtMsgId);
		}
	}

	_firstLoadRequest = MTP::send(MTPmessages_GetHistory(from->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessages() {
	if (!_history || _preloadRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	auto loadMigrated = _migrated && (_history->isEmpty() || _history->loadedAtTop() || (!_migrated->isEmpty() && !_migrated->loadedAtBottom()));
	auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtTop()) {
		return;
	}

	MsgId offset_id = from->minMsgId();
	int32 offset = 0, loadCount = offset_id ? MessagesPerPage : MessagesFirstLoad;

	_preloadRequest = MTP::send(MTPmessages_GetHistory(from->peer->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _preloadDownRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	bool loadMigrated = _migrated && !(_migrated->isEmpty() || _migrated->loadedAtBottom() || (!_history->isEmpty() && !_history->loadedAtTop()));
	History *from = loadMigrated ? _migrated : _history;
	if (from->loadedAtBottom()) {
		return;
	}

	int32 loadCount = MessagesPerPage, offset = -loadCount;

	MsgId offset_id = from->maxMsgId();
	if (!offset_id) {
		if (loadMigrated || !_migrated) return;
		++offset_id;
		++offset;
	}

	_preloadDownRequest = MTP::send(MTPmessages_GetHistory(from->peer->input, MTP_int(offset_id + 1), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::delayedShowAt(MsgId showAtMsgId) {
	if (!_history || (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId)) return;

	clearDelayedShowAt();
	_delayedShowAtMsgId = showAtMsgId;

	PeerData *from = _peer;
	int32 offset_id = 0, offset = 0, loadCount = MessagesPerPage;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		if (_migrated && _migrated->unreadCount()) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = _migrated->inboxReadBefore;
		} else if (_history->unreadCount()) {
			offset = -loadCount / 2;
			offset_id = _history->inboxReadBefore;
		} else {
			loadCount = MessagesFirstLoad;
		}
	} else if (_delayedShowAtMsgId == ShowAtTheEndMsgId) {
		loadCount = MessagesFirstLoad;
	} else if (_delayedShowAtMsgId > 0) {
		offset = -loadCount / 2;
		offset_id = _delayedShowAtMsgId;
	} else if (_delayedShowAtMsgId < 0 && _history->isChannel()) {
		if (_delayedShowAtMsgId < 0 && -_delayedShowAtMsgId < ServerMaxMsgId && _migrated) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = -_delayedShowAtMsgId;
		}
	}

	_delayedShowAtRequest = MTP::send(MTPmessages_GetHistory(from->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::onScroll() {
	App::checkImageCacheSize();
	preloadHistoryIfNeeded();
	visibleAreaUpdated();
}

void HistoryWidget::visibleAreaUpdated() {
	if (_list && !_scroll->isHidden()) {
		int scrollTop = _scroll->scrollTop();
		int scrollBottom = scrollTop + _scroll->height();
		_list->visibleAreaUpdated(scrollTop, scrollBottom);
		if (_history->loadedAtBottom() && (_history->unreadCount() > 0 || (_migrated && _migrated->unreadCount() > 0))) {
			auto showFrom = (_migrated && _migrated->showFrom) ? _migrated->showFrom : (_history ? _history->showFrom : nullptr);
			if (showFrom && !showFrom->detached() && scrollBottom > _list->itemTop(showFrom) && App::wnd()->doWeReadServerHistory()) {
				historyWasRead(ReadServerHistoryChecks::OnlyIfUnread);
			}
		}
	}
}

void HistoryWidget::preloadHistoryIfNeeded() {
	if (_firstLoadRequest || _scroll->isHidden() || !_peer) return;

	updateHistoryDownVisibility();

	int st = _scroll->scrollTop(), stm = _scroll->scrollTopMax(), sh = _scroll->height();
	if (st + PreloadHeightsCount * sh > stm) {
		loadMessagesDown();
	}

	if (st < PreloadHeightsCount * sh) {
		loadMessages();
	}

	while (_replyReturn) {
		bool below = (_replyReturn->detached() && _replyReturn->history() == _history && !_history->isEmpty() && _replyReturn->id < _history->blocks.back()->items.back()->id);
		if (!below) below = (_replyReturn->detached() && _replyReturn->history() == _migrated && !_history->isEmpty());
		if (!below) below = (_replyReturn->detached() && _migrated && _replyReturn->history() == _migrated && !_migrated->isEmpty() && _replyReturn->id < _migrated->blocks.back()->items.back()->id);
		if (!below && !_replyReturn->detached()) below = (st >= stm) || (_list->itemTop(_replyReturn) < st + sh / 2);
		if (below) {
			calcNextReplyReturn();
		} else {
			break;
		}
	}

	if (st != _lastScroll) {
		_lastScrolled = getms();
		_lastScroll = st;
	}
}

void HistoryWidget::onInlineBotCancel() {
	auto &textWithTags = _field->getTextWithTags();
	if (textWithTags.text.size() > _inlineBotUsername.size() + 2) {
		setFieldText({ '@' + _inlineBotUsername + ' ', TextWithTags::Tags() }, TextUpdateEvent::SaveDraft, Ui::FlatTextarea::AddToUndoHistory);
	} else {
		clearFieldText(TextUpdateEvent::SaveDraft, Ui::FlatTextarea::AddToUndoHistory);
	}
}

void HistoryWidget::onWindowVisibleChanged() {
	QTimer::singleShot(0, this, SLOT(preloadHistoryIfNeeded()));
}

void HistoryWidget::onHistoryToEnd() {
	if (_replyReturn && _replyReturn->history() == _history) {
		showHistory(_peer->id, _replyReturn->id);
	} else if (_replyReturn && _replyReturn->history() == _migrated) {
		showHistory(_peer->id, -_replyReturn->id);
	} else if (_peer) {
		showHistory(_peer->id, ShowAtUnreadMsgId);
	}
}

void HistoryWidget::saveEditMsg() {
	if (_saveEditMsgRequestId) return;

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	auto &textWithTags = _field->getTextWithTags();
	auto prepareFlags = itemTextOptions(_history, App::self()).flags;
	EntitiesInText sendingEntities, leftEntities = entitiesFromTextTags(textWithTags.tags);
	QString sendingText, leftText = prepareTextWithEntities(textWithTags.text, prepareFlags, &leftEntities);

	if (!textSplit(sendingText, sendingEntities, leftText, leftEntities, MaxMessageSize)) {
		_field->selectAll();
		_field->setFocus();
		return;
	} else if (!leftText.isEmpty()) {
		Ui::show(Box<InformBox>(lang(lng_edit_too_long)));
		return;
	}

	auto sendFlags = qFlags(MTPmessages_EditMessage::Flag::f_message);
	if (webPageId == CancelledWebPageId) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	auto localEntities = linksToMTP(sendingEntities);
	auto sentEntities = linksToMTP(sendingEntities, true);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_entities;
	}
	_saveEditMsgRequestId = MTP::send(MTPmessages_EditMessage(MTP_flags(sendFlags), _history->peer->input, MTP_int(_editMsgId), MTP_string(sendingText), MTPnullMarkup, sentEntities), rpcDone(&HistoryWidget::saveEditMsgDone, _history), rpcFail(&HistoryWidget::saveEditMsgFail, _history));
}

void HistoryWidget::saveEditMsgDone(History *history, const MTPUpdates &updates, mtpRequestId req) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
		cancelEdit();
	}
	if (auto editDraft = history->editDraft()) {
		if (editDraft->saveRequestId == req) {
			history->clearEditDraft();
			if (App::main()) App::main()->writeDrafts(history);
		}
	}
}

bool HistoryWidget::saveEditMsgFail(History *history, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
	}
	if (auto editDraft = history->editDraft()) {
		if (editDraft->saveRequestId == req) {
			editDraft->saveRequestId = 0;
		}
	}

	QString err = error.type();
	if (err == qstr("MESSAGE_ID_INVALID") || err == qstr("CHAT_ADMIN_REQUIRED") || err == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		Ui::show(Box<InformBox>(lang(lng_edit_error)));
	} else if (err == qstr("MESSAGE_NOT_MODIFIED")) {
		cancelEdit();
	} else if (err == qstr("MESSAGE_EMPTY")) {
		_field->selectAll();
		_field->setFocus();
	} else {
		Ui::show(Box<InformBox>(lang(lng_edit_error)));
	}
	update();
	return true;
}

void HistoryWidget::hideSelectorControlsAnimated() {
	_fieldAutocomplete->hideAnimated();
	_emojiPan->hideAnimated();
	if (_inlineResults) {
		_inlineResults->hideAnimated();
	}
}

void HistoryWidget::onSend(bool ctrlShiftEnter, MsgId replyTo) {
	if (!_history) return;

	if (_editMsgId) {
		saveEditMsg();
		return;
	}

	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(_channel, replyTo));

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	MainWidget::MessageToSend message;
	message.history = _history;
	message.textWithTags = _field->getTextWithTags();
	message.replyTo = replyTo;
	message.silent = _silent->checked();
	message.webPageId = webPageId;
	App::main()->sendMessage(message);

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	hideSelectorControlsAnimated();

	if (replyTo < 0) cancelReply(lastKeyboardUsed);
	if (_previewData && _previewData->pendingTill) previewCancel();
	_field->setFocus();

	if (!_keyboard->hasMarkup() && _keyboard->forceReply() && !_kbReplyTo) onKbToggle();
}

void HistoryWidget::onUnblock() {
	if (_unblockRequest) return;
	if (!_peer || !_peer->isUser() || !_peer->asUser()->isBlocked()) {
		updateControlsVisibility();
		return;
	}

	_unblockRequest = MTP::send(MTPcontacts_Unblock(_peer->asUser()->inputUser), rpcDone(&HistoryWidget::unblockDone, _peer), rpcFail(&HistoryWidget::unblockFail));
}

void HistoryWidget::unblockDone(PeerData *peer, const MTPBool &result, mtpRequestId req) {
	if (!peer->isUser()) return;
	if (_unblockRequest == req) _unblockRequest = 0;
	peer->asUser()->setBlockStatus(UserData::BlockStatus::NotBlocked);
	emit App::main()->peerUpdated(peer);
}

bool HistoryWidget::unblockFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_unblockRequest == req) _unblockRequest = 0;
	return false;
}

void HistoryWidget::blockDone(PeerData *peer, const MTPBool &result) {
	if (!peer->isUser()) return;

	peer->asUser()->setBlockStatus(UserData::BlockStatus::Blocked);
	emit App::main()->peerUpdated(peer);
}

void HistoryWidget::onBotStart() {
	if (!_peer || !_peer->isUser() || !_peer->asUser()->botInfo || !_canSendMessages) {
		updateControlsVisibility();
		return;
	}

	QString token = _peer->asUser()->botInfo->startToken;
	if (token.isEmpty()) {
		sendBotCommand(_peer, _peer->asUser(), qsl("/start"), 0);
	} else {
		uint64 randomId = rand_value<uint64>();
		MTP::send(MTPmessages_StartBot(_peer->asUser()->inputUser, MTP_inputPeerEmpty(), MTP_long(randomId), MTP_string(token)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, { _peer->asUser(), (PeerData*)nullptr }));

		_peer->asUser()->botInfo->startToken = QString();
		if (_keyboard->hasMarkup()) {
			if (_keyboard->singleUse() && _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _history->lastKeyboardUsed) {
				_history->lastKeyboardHiddenId = _history->lastKeyboardId;
			}
			if (!kbWasHidden()) _kbShown = _keyboard->hasMarkup();
		}
	}
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::onJoinChannel() {
	if (_unblockRequest) return;
	if (!_peer || !_peer->isChannel() || !isJoinChannel()) {
		updateControlsVisibility();
		return;
	}

	_unblockRequest = MTP::send(MTPchannels_JoinChannel(_peer->asChannel()->inputChannel), rpcDone(&HistoryWidget::joinDone), rpcFail(&HistoryWidget::joinFail));
}

void HistoryWidget::joinDone(const MTPUpdates &result, mtpRequestId req) {
	if (_unblockRequest == req) _unblockRequest = 0;
	if (App::main()) App::main()->sentUpdatesReceived(result);
}

bool HistoryWidget::joinFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_unblockRequest == req) _unblockRequest = 0;
	if (error.type() == qstr("CHANNEL_PRIVATE") || error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA") || error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		Ui::show(Box<InformBox>(lang((_peer && _peer->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
		return true;
	} else if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
		Ui::show(Box<InformBox>(lang(lng_join_channel_error)));
	}

	return false;
}

void HistoryWidget::onMuteUnmute() {
	App::main()->updateNotifySetting(_peer, _history->mute() ? NotifySettingSetNotify : NotifySettingSetMuted);
}

void HistoryWidget::onBroadcastSilentChange() {
	updateFieldPlaceholder();
}

void HistoryWidget::onShareContact(const PeerId &peer, UserData *contact) {
	auto phone = contact->phone();
	if (phone.isEmpty()) phone = App::phoneFromSharedContact(peerToUser(contact->id));
	if (!contact || phone.isEmpty()) return;

	Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
	if (!_history) return;

	shareContact(peer, phone, contact->firstName, contact->lastName, replyToId(), peerToUser(contact->id));
}

void HistoryWidget::shareContact(const PeerId &peer, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, int32 userId) {
	auto history = App::history(peer);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(peerToChannel(peer), clientMsgId());

	App::main()->readServerHistory(history);
	fastShowAtEnd(history);

	PeerData *p = App::peer(peer);
	auto flags = newMessageFlags(p) | MTPDmessage::Flag::f_media; // unread, out

	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(peerToChannel(peer), replyTo));

	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}

	bool channelPost = p->isChannel() && !p->isMegagroup();
	bool showFromName = !channelPost || p->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent->checked();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::Flag::f_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = showFromName ? AuthSession::CurrentUserId() : 0;
	history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(peer), MTPnullFwdHeader, MTPint(), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname), MTP_int(userId)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), p->input, MTP_int(replyTo), MTP_inputMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, history->sendRequestId);

	App::historyRegRandom(randomId, newId);

	App::main()->finishForwarding(history, _silent->checked());
	cancelReplyAfterMediaSend(lastKeyboardUsed);
}

History *HistoryWidget::history() const {
	return _history;
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

void HistoryWidget::setMsgId(MsgId showAtMsgId) { // sometimes _showAtMsgId is set directly
	if (_showAtMsgId != showAtMsgId) {
		auto wasMsgId = _showAtMsgId;
		_showAtMsgId = showAtMsgId;
		App::main()->dlgUpdated(_history ? _history->peer : nullptr, wasMsgId);
		emit historyShown(_history, _showAtMsgId);
	}
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

void HistoryWidget::showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params) {
	_showDirection = direction;

	_a_show.finish();

	_cacheUnder = params.oldContentCache;
	show();
	_topBar->showAll();
	historyDownAnimationFinish();
	_topShadow->setVisible(params.withTopBarShadow ? false : true);
	_cacheOver = App::main()->grabForShowAnimation(params);
	_topShadow->setVisible(params.withTopBarShadow ? true : false);

	_topBar->hide();
	_scroll->hide();
	_kbScroll->hide();
	_reportSpamPanel->hide();
	_historyDown->hide();
	_attachToggle->hide();
	_attachEmoji->hide();
	_fieldAutocomplete->hide();
	_silent->hide();
	_botKeyboardShow->hide();
	_botKeyboardHide->hide();
	_botCommandStart->hide();
	_field->hide();
	_fieldBarCancel->hide();
	_send->hide();
	_unblock->hide();
	_botStart->hide();
	_joinChannel->hide();
	_muteUnmute->hide();
	if (_pinnedBar) {
		_pinnedBar->shadow->hide();
		_pinnedBar->cancel->hide();
	}

	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());
	if (_history) {
		_backAnimationButton.create(this);
		_backAnimationButton->setClickedCallback([this] { topBarClick(); });
		_backAnimationButton->setGeometry(_topBar->geometry());
		_backAnimationButton->show();
	}

	activate();
}

void HistoryWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		_topShadow->setVisible(_peer != nullptr);
		_topBar->setVisible(_peer != nullptr);
		historyDownAnimationFinish();
		_cacheUnder = _cacheOver = QPixmap();
		doneShow();
	}
}

void HistoryWidget::doneShow() {
	_topBar->animationFinished();
	_backAnimationButton.destroy();
	updateReportSpamStatus();
	updateBotKeyboard();
	updateControlsVisibility();
	if (!_histInited) {
		updateListSize(true);
	} else if (hasPendingResizedItems()) {
		updateListSize();
	}
	preloadHistoryIfNeeded();
	if (App::wnd()) {
		App::wnd()->checkHistoryActivation();
		App::wnd()->setInnerFocus();
	}
}

void HistoryWidget::finishAnimation() {
	if (!_a_show.animating()) return;
	_a_show.finish();
	_topShadow->setVisible(_peer != nullptr);
	_topBar->setVisible(_peer != nullptr);
	historyDownAnimationFinish();
}

void HistoryWidget::historyDownAnimationFinish() {
	_historyDownShown.finish();
	updateHistoryDownPosition();
}

void HistoryWidget::step_recording(float64 ms, bool timer) {
	float64 dt = ms / AudioVoiceMsgUpdateView;
	if (dt >= 1) {
		_a_recording.stop();
		a_recordingLevel.finish();
	} else {
		a_recordingLevel.update(dt, anim::linear);
	}
	if (timer) update(_attachToggle->geometry());
}

void HistoryWidget::chooseAttach() {
	if (!_history) return;

	auto filter = FileDialog::AllFilesFilter() + qsl(";;Image files (*") + cImgExtensions().join(qsl(" *")) + qsl(")");

	FileDialog::GetOpenPaths(lang(lng_choose_files), filter, base::lambda_guarded(this, [this](const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto animated = false;
			auto image = App::readImage(result.remoteContent, nullptr, false, &animated);
			if (!image.isNull() && !animated) {
				confirmSendingFiles(image, result.remoteContent);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto lists = getSendingFilesLists(result.paths);
			if (lists.allFilesForCompress) {
				confirmSendingFiles(lists);
			} else {
				validateSendingFiles(lists, [this](const QStringList &files) {
					uploadFiles(files, SendMediaType::File);
					return true;
				});
			}
		}
	}));
}

void HistoryWidget::sendButtonClicked() {
	auto type = _send->type();
	if (type == Ui::SendButton::Type::Cancel) {
		onInlineBotCancel();
	} else if (type != Ui::SendButton::Type::Record) {
		onSend();
	}
}

void HistoryWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!_history || !_canSendMessages) return;

	_attachDrag = getDragState(e->mimeData());
	updateDragAreas();

	if (_attachDrag) {
		e->setDropAction(Qt::IgnoreAction);
		e->accept();
	}
}

void HistoryWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
}

void HistoryWidget::leaveEventHook(QEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
	if (hasMouseTracking()) mouseMoveEvent(0);
}

void HistoryWidget::mouseMoveEvent(QMouseEvent *e) {
	auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
	updateOverStates(pos);
}

void HistoryWidget::updateOverStates(QPoint pos) {
	auto inField = pos.y() >= (_scroll->y() + _scroll->height()) && pos.y() < height() && pos.x() >= 0 && pos.x() < width();
	auto inReplyEdit = QRect(st::historyReplySkip, _field->y() - st::historySendPadding - st::historyReplyHeight, width() - st::historyReplySkip - _fieldBarCancel->width(), st::historyReplyHeight).contains(pos) && (_editMsgId || replyToId());
	auto inPinnedMsg = QRect(0, _topBar->bottomNoMargins(), width(), st::historyReplyHeight).contains(pos) && _pinnedBar;
	auto inClickable = inReplyEdit || inPinnedMsg;
	if (inField != _inField && _recording) {
		_inField = inField;
		_send->setRecordActive(_inField);
	}
	_inReplyEdit = inReplyEdit;
	_inPinnedMsg = inPinnedMsg;
	if (inClickable != _inClickable) {
		_inClickable = inClickable;
		setCursor(_inClickable ? style::cur_pointer : style::cur_default);
	}
}

void HistoryWidget::leaveToChildEvent(QEvent *e, QWidget *child) { // e -- from enterEvent() of child TWidget
	if (hasMouseTracking()) {
		updateOverStates(mapFromGlobal(QCursor::pos()));
	}
}

void HistoryWidget::recordStartCallback() {
	if (!Media::Capture::instance()->available()) {
		return;
	}
	emit Media::Capture::instance()->start();

	_recording = _inField = true;
	updateControlsVisibility();
	activate();

	updateField();

	_send->setRecordActive(true);
}

void HistoryWidget::recordStopCallback(bool active) {
	stopRecording(_peer && active);
}

void HistoryWidget::recordUpdateCallback(QPoint globalPos) {
	updateOverStates(mapFromGlobal(globalPos));
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_replyForwardPressed) {
		_replyForwardPressed = false;
		update(0, _field->y() - st::historySendPadding - st::historyReplyHeight, width(), st::historyReplyHeight);
	}
	if (_attachDrag != DragStateNone || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
	if (_recording) {
		stopRecording(_peer && _inField);
	}
}

void HistoryWidget::stopRecording(bool send) {
	emit Media::Capture::instance()->stop(send);

	a_recordingLevel = anim::value();
	_a_recording.stop();

	_recording = false;
	_recordingSamples = 0;
	if (_peer && (!_peer->isChannel() || _peer->isMegagroup())) {
		updateSendAction(_history, SendAction::Type::RecordVoice, -1);
	}

	updateControlsVisibility();
	activate();

	updateField();
	_send->setRecordActive(false);
}

void HistoryWidget::sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) { // replyTo != 0 from ReplyKeyboardMarkup, == 0 from cmd links
	if (!_peer || _peer != peer) return;

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, replyTo));

	QString toSend = cmd;
	if (bot && (!bot->isUser() || !bot->asUser()->botInfo)) {
		bot = nullptr;
	}
	QString username = bot ? bot->asUser()->username : QString();
	int32 botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
	if (!replyTo && toSend.indexOf('@') < 2 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
		toSend += '@' + username;
	}

	MainWidget::MessageToSend message;
	message.history = _history;
	message.textWithTags = { toSend, TextWithTags::Tags() };
	message.replyTo = replyTo ? ((!_peer->isUser()/* && (botStatus == 0 || botStatus == 2)*/) ? replyTo : -1) : 0;
	message.silent = false;
	App::main()->sendMessage(message);
	if (replyTo) {
		if (_replyToId == replyTo) {
			cancelReply();
			onCloudDraftSave();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) onKbToggle(false);
			_history->lastKeyboardUsed = true;
		}
	}

	_field->setFocus();
}

void HistoryWidget::hideSingleUseKeyboard(PeerData *peer, MsgId replyTo) {
	if (!_peer || _peer != peer) return;

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, replyTo));
	if (replyTo) {
		if (_replyToId == replyTo) {
			cancelReply();
			onCloudDraftSave();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) onKbToggle(false);
			_history->lastKeyboardUsed = true;
		}
	}
}

void HistoryWidget::app_sendBotCallback(const HistoryMessageReplyMarkup::Button *button, const HistoryItem *msg, int row, int col) {
	if (msg->id < 0 || _peer != msg->history()->peer) {
		return;
	}

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, msg->id));

	auto bot = msg->getMessageBot();

	using ButtonType = HistoryMessageReplyMarkup::Button::Type;
	BotCallbackInfo info = { bot, msg->fullId(), row, col, (button->type == ButtonType::Game) };
	auto flags = MTPmessages_GetBotCallbackAnswer::Flags(0);
	QByteArray sendData;
	if (info.game) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_game;
	} else if (button->type == ButtonType::Callback) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_data;
		sendData = button->data;
	}
	button->requestId = MTP::send(MTPmessages_GetBotCallbackAnswer(MTP_flags(flags), _peer->input, MTP_int(msg->id), MTP_bytes(sendData)), rpcDone(&HistoryWidget::botCallbackDone, info), rpcFail(&HistoryWidget::botCallbackFail, info));
	Ui::repaintHistoryItem(msg);

	if (_replyToId == msg->id) {
		cancelReply();
	}
	if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
		if (_kbShown) onKbToggle(false);
		_history->lastKeyboardUsed = true;
	}
}

void HistoryWidget::botCallbackDone(BotCallbackInfo info, const MTPmessages_BotCallbackAnswer &answer, mtpRequestId req) {
	auto item = App::histItemById(info.msgId);
	if (item) {
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (info.row < markup->rows.size() && info.col < markup->rows.at(info.row).size()) {
				if (markup->rows.at(info.row).at(info.col).requestId == req) {
					markup->rows.at(info.row).at(info.col).requestId = 0;
					Ui::repaintHistoryItem(item);
				}
			}
		}
	}
	if (answer.type() == mtpc_messages_botCallbackAnswer) {
		auto &answerData = answer.c_messages_botCallbackAnswer();
		if (answerData.has_message()) {
			if (answerData.is_alert()) {
				Ui::show(Box<InformBox>(qs(answerData.vmessage)));
			} else {
				Ui::Toast::Show(qs(answerData.vmessage));
			}
		} else if (answerData.has_url()) {
			auto url = qs(answerData.vurl);
			if (info.game) {
				url = appendShareGameScoreUrl(url, info.msgId);
				BotGameUrlClickHandler(info.bot, url).onClick(Qt::LeftButton);
				if (item && (!item->history()->peer->isChannel() || item->history()->peer->isMegagroup())) {
					updateSendAction(item->history(), SendAction::Type::PlayGame);
				}
			} else {
				UrlClickHandler(url).onClick(Qt::LeftButton);
			}
		}
	}
}

bool HistoryWidget::botCallbackFail(BotCallbackInfo info, const RPCError &error, mtpRequestId req) {
	// show error?
	if (HistoryItem *item = App::histItemById(info.msgId)) {
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (info.row < markup->rows.size() && info.col < markup->rows.at(info.row).size()) {
				if (markup->rows.at(info.row).at(info.col).requestId == req) {
					markup->rows.at(info.row).at(info.col).requestId = 0;
					Ui::repaintHistoryItem(item);
				}
			}
		}
	}
	return true;
}

bool HistoryWidget::insertBotCommand(const QString &cmd) {
	if (!canWriteMessage()) return false;

	bool insertingInlineBot = !cmd.isEmpty() && (cmd.at(0) == '@');
	QString toInsert = cmd;
	if (!toInsert.isEmpty() && !insertingInlineBot) {
		PeerData *bot = _peer->isUser() ? _peer : (App::hoveredLinkItem() ? App::hoveredLinkItem()->fromOriginal() : 0);
		if (!bot->isUser() || !bot->asUser()->botInfo) bot = 0;
		QString username = bot ? bot->asUser()->username : QString();
		int32 botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
		if (toInsert.indexOf('@') < 0 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
			toInsert += '@' + username;
		}
	}
	toInsert += ' ';

	if (!insertingInlineBot) {
		auto &textWithTags = _field->getTextWithTags();
		TextWithTags textWithTagsToSet;
		QRegularExpressionMatch m = QRegularExpression(qsl("^/[A-Za-z_0-9]{0,64}(@[A-Za-z_0-9]{0,32})?(\\s|$)")).match(textWithTags.text);
		if (m.hasMatch()) {
			textWithTagsToSet = _field->getTextWithTagsPart(m.capturedLength());
		} else {
			textWithTagsToSet = textWithTags;
		}
		textWithTagsToSet.text = toInsert + textWithTagsToSet.text;
		for (auto &tag : textWithTagsToSet.tags) {
			tag.offset += toInsert.size();
		}
		_field->setTextWithTags(textWithTagsToSet);

		QTextCursor cur(_field->textCursor());
		cur.movePosition(QTextCursor::End);
		_field->setTextCursor(cur);
	} else {
		setFieldText({ toInsert, TextWithTags::Tags() }, TextUpdateEvent::SaveDraft, Ui::FlatTextarea::AddToUndoHistory);
		_field->setFocus();
		return true;
	}
	return false;
}

bool HistoryWidget::eventFilter(QObject *obj, QEvent *e) {
	if (obj == _historyDown && e->type() == QEvent::Wheel) {
		return _scroll->viewportEvent(e);
	}
	return TWidget::eventFilter(obj, e);
}

DragState HistoryWidget::getDragState(const QMimeData *d) {
	if (!d
		|| d->hasFormat(qsl("application/x-td-forward-selected"))
		|| d->hasFormat(qsl("application/x-td-forward-pressed"))
		|| d->hasFormat(qsl("application/x-td-forward-pressed-link"))) return DragStateNone;

	if (d->hasImage()) return DragStateImage;

	QString uriListFormat(qsl("text/uri-list"));
	if (!d->hasFormat(uriListFormat)) return DragStateNone;

	QStringList imgExtensions(cImgExtensions()), files;

	const QList<QUrl> &urls(d->urls());
	if (urls.isEmpty()) return DragStateNone;

	bool allAreSmallImages = true;
	for (QList<QUrl>::const_iterator i = urls.cbegin(), en = urls.cend(); i != en; ++i) {
		if (!i->isLocalFile()) return DragStateNone;

		auto file = Platform::File::UrlToLocal(*i);

		QFileInfo info(file);
		if (info.isDir()) return DragStateNone;

		quint64 s = info.size();
		if (s > App::kFileSizeLimit) {
			return DragStateNone;
		}
		if (allAreSmallImages) {
			if (s > App::kImageSizeLimit) {
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
	_field->setAcceptDrops(!_attachDrag);
	updateControlsGeometry();

	switch (_attachDrag) {
	case DragStateNone:
		_attachDragDocument->otherLeave();
		_attachDragPhoto->otherLeave();
	break;
	case DragStateFiles:
		_attachDragDocument->setText(lang(lng_drag_files_here), lang(lng_drag_to_send_files));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->hideFast();
	break;
	case DragStatePhotoFiles:
		_attachDragDocument->setText(lang(lng_drag_images_here), lang(lng_drag_to_send_no_compression));
		_attachDragPhoto->setText(lang(lng_drag_photos_here), lang(lng_drag_to_send_quick));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->otherEnter();
	break;
	case DragStateImage:
		_attachDragPhoto->setText(lang(lng_drag_images_here), lang(lng_drag_to_send_quick));
		_attachDragDocument->hideFast();
		_attachDragPhoto->otherEnter();
	break;
	};
}

bool HistoryWidget::canSendMessages(PeerData *peer) const {
	return peer && peer->canWrite();
}

bool HistoryWidget::readyToForward() const {
	return _canSendMessages && App::main()->hasForwardingItems();
}

bool HistoryWidget::hasSilentToggle() const {
	return _peer && _peer->isChannel() && !_peer->isMegagroup() && _peer->asChannel()->canPublish() && _peer->notify != UnknownNotifySettings;
}

void HistoryWidget::inlineBotResolveDone(const MTPcontacts_ResolvedPeer &result) {
	_inlineBotResolveRequestId = 0;
//	Notify::inlineBotRequesting(false);
	UserData *resolvedBot = nullptr;
	if (result.type() == mtpc_contacts_resolvedPeer) {
		const auto &d(result.c_contacts_resolvedPeer());
		resolvedBot = App::feedUsers(d.vusers);
		if (resolvedBot) {
			if (!resolvedBot->botInfo || resolvedBot->botInfo->inlinePlaceholder.isEmpty()) {
				resolvedBot = nullptr;
			}
		}
		App::feedChats(d.vchats);
	}

	UserData *bot = nullptr;
	QString inlineBotUsername;
	auto query = _field->getInlineBotQuery(&bot, &inlineBotUsername);
	if (inlineBotUsername == _inlineBotUsername) {
		if (bot == Ui::LookingUpInlineBot) {
			bot = resolvedBot;
		}
	} else {
		bot = nullptr;
	}
	if (bot) {
		applyInlineBotQuery(bot, query);
	} else {
		clearInlineBot();
	}
}

bool HistoryWidget::inlineBotResolveFail(QString name, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_inlineBotResolveRequestId = 0;
//	Notify::inlineBotRequesting(false);
	if (name == _inlineBotUsername) {
		clearInlineBot();
	}
	return true;
}

bool HistoryWidget::isBotStart() const {
	if (!_peer || !_peer->isUser() || !_peer->asUser()->botInfo || !_canSendMessages) return false;
	return !_peer->asUser()->botInfo->startToken.isEmpty() || (_history->isEmpty() && !_history->lastMsg);
}

bool HistoryWidget::isBlocked() const {
	return _peer && _peer->isUser() && _peer->asUser()->isBlocked();
}

bool HistoryWidget::isJoinChannel() const {
	return _peer && _peer->isChannel() && !_peer->asChannel()->amIn();
}

bool HistoryWidget::isMuteUnmute() const {
	return _peer && _peer->isChannel() && _peer->asChannel()->isBroadcast() && !_peer->asChannel()->canPublish();
}

bool HistoryWidget::showRecordButton() const {
	return Media::Capture::instance()->available() && !_field->hasSendText() && !readyToForward() && !_editMsgId;
}

bool HistoryWidget::showInlineBotCancel() const {
	return _inlineBot && (_inlineBot != Ui::LookingUpInlineBot);
}

void HistoryWidget::updateSendButtonType() {
	auto type = [this] {
		using Type = Ui::SendButton::Type;
		if (_editMsgId) {
			return Type::Save;
		} else if (_isInlineBot) {
			return Type::Cancel;
		} else if (showRecordButton()) {
			return Type::Record;
		}
		return Type::Send;
	};
	_send->setType(type());
}

bool HistoryWidget::updateCmdStartShown() {
	bool cmdStartShown = false;
	if (_history && _peer && ((_peer->isChat() && _peer->asChat()->botStatus > 0) || (_peer->isMegagroup() && _peer->asChannel()->mgInfo->botStatus > 0) || (_peer->isUser() && _peer->asUser()->botInfo))) {
		if (!isBotStart() && !isBlocked() && !_keyboard->hasMarkup() && !_keyboard->forceReply()) {
			if (!_field->hasSendText()) {
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

bool HistoryWidget::kbWasHidden() const {
	return _history && (_keyboard->forMsgId() == FullMsgId(_history->channelId(), _history->lastKeyboardHiddenId));
}

void HistoryWidget::dropEvent(QDropEvent *e) {
	_attachDrag = DragStateNone;
	updateDragAreas();
	e->acceptProposedAction();
}

void HistoryWidget::onKbToggle(bool manual) {
	auto fieldEnabled = canWriteMessage() && !_a_show.animating();
	if (_kbShown || _kbReplyTo) {
		_botKeyboardHide->hide();
		if (_kbShown) {
			if (fieldEnabled) {
				_botKeyboardShow->show();
			}
			if (manual && _history) {
				_history->lastKeyboardHiddenId = _keyboard->forMsgId().msg;
			}

			_kbScroll->hide();
			_kbShown = false;

			_field->setMaxHeight(st::historyComposeFieldMaxHeight);

			_kbReplyTo = 0;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_editMsgId && !_replyToId) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		} else {
			if (_history) {
				_history->clearLastKeyboard();
			} else {
				updateBotKeyboard();
			}
		}
	} else if (!_keyboard->hasMarkup() && _keyboard->forceReply()) {
		_botKeyboardHide->hide();
		_botKeyboardShow->hide();
		if (fieldEnabled) {
			_botCommandStart->show();
		}
		_kbScroll->hide();
		_kbShown = false;

		_field->setMaxHeight(st::historyComposeFieldMaxHeight);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply()) ? App::histItemById(_keyboard->forMsgId()) : 0;
		if (_kbReplyTo && !_editMsgId && !_replyToId && fieldEnabled) {
			updateReplyToName();
			_replyEditMsgText.setText(st::messageTextStyle, textClean(_kbReplyTo->inReplyText()), _textDlgOptions);
			_fieldBarCancel->show();
			updateMouseTracking();
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	} else if (fieldEnabled) {
		_botKeyboardHide->show();
		_botKeyboardShow->hide();
		_kbScroll->show();
		_kbShown = true;

		int32 maxh = qMin(_keyboard->height(), st::historyComposeFieldMaxHeight - (st::historyComposeFieldMaxHeight / 2));
		_field->setMaxHeight(st::historyComposeFieldMaxHeight - maxh);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply()) ? App::histItemById(_keyboard->forMsgId()) : 0;
		if (_kbReplyTo && !_editMsgId && !_replyToId) {
			updateReplyToName();
			_replyEditMsgText.setText(st::messageTextStyle, textClean(_kbReplyTo->inReplyText()), _textDlgOptions);
			_fieldBarCancel->show();
			updateMouseTracking();
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	}
	resizeEvent(0);
	if (_botKeyboardHide->isHidden() && canWriteMessage() && !_a_show.animating()) {
		_attachEmoji->show();
	} else {
		_attachEmoji->hide();
	}
	updateField();
}

void HistoryWidget::onCmdStart() {
	setFieldText({ qsl("/"), TextWithTags::Tags() }, 0, Ui::FlatTextarea::AddToUndoHistory);
}

void HistoryWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (!_list) return;

	return _list->showContextMenu(e);
}

void HistoryWidget::forwardMessage() {
	auto item = App::contextItem();
	if (!item || item->id < 0 || item->serviceMsg()) return;

	App::main()->forwardLayer();
}

void HistoryWidget::selectMessage() {
	auto item = App::contextItem();
	if (!item || item->id < 0 || item->serviceMsg()) return;

	if (_list) _list->selectItem(item);
}

bool HistoryWidget::paintTopBar(Painter &p, int decreaseWidth, TimeMs ms) {
	if (!_history) return false;

	auto increaseLeft = (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) ? (st::topBarArrowPadding.left() - st::topBarArrowPadding.right()) : 0;
	auto nameleft = st::topBarArrowPadding.right() + increaseLeft;
	auto nametop = st::topBarArrowPadding.top();
	auto statustop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto namewidth = width() - decreaseWidth - nameleft - st::topBarArrowPadding.right();
	p.setFont(st::dialogsTextFont);
	if (!_history->paintSendAction(p, nameleft, statustop, namewidth, width(), st::historyStatusFgTyping, ms)) {
		p.setPen(_titlePeerTextOnline ? st::historyStatusFgActive : st::historyStatusFg);
		p.drawText(nameleft, statustop + st::dialogsTextFont->ascent, _titlePeerText);
	}

	p.setPen(st::dialogsNameFg);
	_peer->dialogName().drawElided(p, nameleft, nametop, namewidth);

	if (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) {
		st::topBarBackward.paint(p, (st::topBarArrowPadding.left() - st::topBarBackward.width()) / 2, (st::topBarHeight - st::topBarBackward.height()) / 2, width());
	}
	return true;
}

QRect HistoryWidget::getMembersShowAreaGeometry() const {
	int increaseLeft = (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) ? (st::topBarArrowPadding.left() - st::topBarArrowPadding.right()) : 0;
	int membersTextLeft = st::topBarArrowPadding.right() + increaseLeft;
	int membersTextTop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	int membersTextWidth = _titlePeerTextWidth;
	int membersTextHeight = st::topBarHeight - membersTextTop;

	return rtlrect(membersTextLeft, membersTextTop, membersTextWidth, membersTextHeight, width());
}

void HistoryWidget::setMembersShowAreaActive(bool active) {
	if (!active) {
		_membersDropdownShowTimer.stop();
	}
	if (active && _peer && (_peer->isChat() || _peer->isMegagroup())) {
		if (_membersDropdown) {
			_membersDropdown->otherEnter();
		} else if (!_membersDropdownShowTimer.isActive()) {
			_membersDropdownShowTimer.start(300);
		}
	} else if (_membersDropdown) {
		_membersDropdown->otherLeave();
	}
}

void HistoryWidget::onMembersDropdownShow() {
	if (!_membersDropdown) {
		_membersDropdown.create(this, st::membersInnerDropdown);
		_membersDropdown->setOwnedWidget(new Profile::GroupMembersWidget(_membersDropdown, _peer, Profile::GroupMembersWidget::TitleVisibility::Hidden, st::membersInnerItem));
		_membersDropdown->resizeToWidth(st::membersInnerWidth);

		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
		_membersDropdown->moveToLeft(0, 0);
		_membersDropdown->setHiddenCallback([this] { _membersDropdown.destroyDelayed(); });
	}
	_membersDropdown->otherEnter();
}

void HistoryWidget::onModerateKeyActivate(int index, bool *outHandled) {
	*outHandled = _keyboard->isHidden() ? false : _keyboard->moderateKeyActivate(index);
}

void HistoryWidget::topBarClick() {
	if (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) {
		App::main()->showBackFromStack();
	} else {
		if (_history) Ui::showPeerProfile(_peer);
	}
}

void HistoryWidget::updateOnlineDisplay() {
	if (!_history) return;

	QString text;
	int32 t = unixtime();
	bool titlePeerTextOnline = false;
	if (auto user = _peer->asUser()) {
		text = App::onlineText(user, t);
		titlePeerTextOnline = App::onlineColorUse(user, t);
	} else if (_peer->isChat()) {
		ChatData *chat = _peer->asChat();
		if (!chat->amIn()) {
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
	} else if (_peer->isChannel()) {
		if (_peer->isMegagroup() && _peer->asChannel()->membersCount() > 0 && _peer->asChannel()->membersCount() <= Global::ChatSizeMax()) {
			if (_peer->asChannel()->mgInfo->lastParticipants.size() < _peer->asChannel()->membersCount() || _peer->asChannel()->lastParticipantsCountOutdated()) {
				if (App::api()) App::api()->requestLastParticipants(_peer->asChannel());
			}
			int32 onlineCount = 0;
			bool onlyMe = true;
			for (auto i = _peer->asChannel()->mgInfo->lastParticipants.cbegin(), e = _peer->asChannel()->mgInfo->lastParticipants.cend(); i != e; ++i) {
				if ((*i)->onlineTill > t) {
					++onlineCount;
					if (onlyMe && (*i) != App::self()) onlyMe = false;
				}
			}
			if (onlineCount && !onlyMe) {
				text = lng_chat_status_members_online(lt_count, _peer->asChannel()->membersCount(), lt_count_online, onlineCount);
			} else {
				text = lng_chat_status_members(lt_count, _peer->asChannel()->membersCount());
			}
		} else {
			text = _peer->asChannel()->membersCount() ? lng_chat_status_members(lt_count, _peer->asChannel()->membersCount()) : lang(_peer->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	}
	if (_titlePeerText != text) {
		_titlePeerText = text;
		_titlePeerTextOnline = titlePeerTextOnline;
		_titlePeerTextWidth = st::dialogsTextFont->width(_titlePeerText);
		if (App::main()) {
			_topBar->updateMembersShowArea();
			_topBar->update();
		}
	}
	updateOnlineDisplayTimer();
}

void HistoryWidget::updateOnlineDisplayTimer() {
	if (!_history) return;

	int32 t = unixtime(), minIn = 86400;
	if (_peer->isUser()) {
		minIn = App::onlineWillChangeIn(_peer->asUser(), t);
	} else if (_peer->isChat()) {
		ChatData *chat = _peer->asChat();
		if (chat->participants.isEmpty()) return;

		for (ChatData::Participants::const_iterator i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key(), t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else if (_peer->isChannel()) {
	}
	App::main()->updateOnlineDisplayIn(minIn * 1000);
}

void HistoryWidget::moveFieldControls() {
	auto keyboardHeight = 0;
	auto bottom = height();
	auto maxKeyboardHeight = st::historyComposeFieldMaxHeight - _field->height();
	_keyboard->resizeToWidth(width(), maxKeyboardHeight);
	if (_kbShown) {
		keyboardHeight = qMin(_keyboard->height(), maxKeyboardHeight);
		bottom -= keyboardHeight;
		_kbScroll->setGeometry(0, bottom, width(), keyboardHeight);
	}

// _attachToggle -------- _inlineResults ---------------------------------- _emojiPan -------- _fieldBarCancel
// (_attachDocument|_attachPhoto) _field (_silent|_cmdStart|_kbShow) (_kbHide|_attachEmoji) [_broadcast] _send
// (_botStart|_unblock|_joinChannel|_muteUnmute)

	auto buttonsBottom = bottom - _attachToggle->height();
	auto left = 0;
	_attachToggle->moveToLeft(left, buttonsBottom); left += _attachToggle->width();
	_field->moveToLeft(left, bottom - _field->height() - st::historySendPadding);
	auto right = st::historySendRight;
	_send->moveToRight(right, buttonsBottom); right += _send->width();
	_attachEmoji->moveToRight(right, buttonsBottom);
	_botKeyboardHide->moveToRight(right, buttonsBottom); right += _botKeyboardHide->width();
	_botKeyboardShow->moveToRight(right, buttonsBottom);
	_botCommandStart->moveToRight(right, buttonsBottom);
	_silent->moveToRight(right, buttonsBottom);

	_fieldBarCancel->moveToRight(0, _field->y() - st::historySendPadding - _fieldBarCancel->height());
	if (_inlineResults) {
		_inlineResults->moveBottom(_field->y() - st::historySendPadding);
	}
	_emojiPan->moveBottom(_field->y() - st::historySendPadding);

	auto fullWidthButtonRect = QRect(0, bottom - _botStart->height(), width(), _botStart->height());
	_botStart->setGeometry(fullWidthButtonRect);
	_unblock->setGeometry(fullWidthButtonRect);
	_joinChannel->setGeometry(fullWidthButtonRect);
	_muteUnmute->setGeometry(fullWidthButtonRect);
}

void HistoryWidget::updateFieldSize() {
	auto kbShowShown = _history && !_kbShown && _keyboard->hasMarkup();
	auto fieldWidth = width() - _attachToggle->width() - st::historySendRight;
	fieldWidth -= _send->width();
	fieldWidth -= _attachEmoji->width();
	if (kbShowShown) fieldWidth -= _botKeyboardShow->width();
	if (_cmdStartShown) fieldWidth -= _botCommandStart->width();
	if (hasSilentToggle()) fieldWidth -= _silent->width();

	if (_field->width() != fieldWidth) {
		_field->resize(fieldWidth, _field->height());
	} else {
		moveFieldControls();
	}
}

void HistoryWidget::clearInlineBot() {
	if (_inlineBot) {
		_inlineBot = nullptr;
		inlineBotChanged();
		_field->finishPlaceholder();
	}
	if (_inlineResults) {
		_inlineResults->clearInlineBot();
	}
	onCheckFieldAutocomplete();
}

void HistoryWidget::inlineBotChanged() {
	bool isInlineBot = showInlineBotCancel();
	if (_isInlineBot != isInlineBot) {
		_isInlineBot = isInlineBot;
		updateFieldPlaceholder();
		updateFieldSubmitSettings();
		updateControlsVisibility();
	}
}

void HistoryWidget::onFieldResize() {
	moveFieldControls();
	updateListSize();
	updateField();
}

void HistoryWidget::onFieldFocused() {
	if (_list) _list->clearSelectedItems(true);
}

void HistoryWidget::onCheckFieldAutocomplete() {
	if (!_history || _a_show.animating()) return;

	bool start = false;
	bool isInlineBot = _inlineBot && (_inlineBot != Ui::LookingUpInlineBot);
	QString query = isInlineBot ? QString() : _field->getMentionHashtagBotCommandPart(start);
	if (!query.isEmpty()) {
		if (query.at(0) == '#' && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) Local::readRecentHashtagsAndBots();
		if (query.at(0) == '@' && cRecentInlineBots().isEmpty()) Local::readRecentHashtagsAndBots();
		if (query.at(0) == '/' && _peer->isUser() && !_peer->asUser()->botInfo) return;
	}
	_fieldAutocomplete->showFiltered(_peer, query, start);
}

void HistoryWidget::updateFieldPlaceholder() {
	if (_editMsgId) {
		_field->setPlaceholder(lang(lng_edit_message_text));
	} else {
		if (_inlineBot && _inlineBot != Ui::LookingUpInlineBot) {
			_field->setPlaceholder(_inlineBot->botInfo->inlinePlaceholder.mid(1), _inlineBot->username.size() + 2);
		} else {
			_field->setPlaceholder(lang((_history && _history->isChannel() && !_history->isMegagroup()) ? (_silent->checked() ? lng_broadcast_silent_ph : lng_broadcast_ph) : lng_message_ph));
		}
	}
	updateSendButtonType();
}

template <typename SendCallback>
bool HistoryWidget::showSendFilesBox(object_ptr<SendFilesBox> box, const QString &insertTextOnCancel, const QString *addedComment, SendCallback callback) {
	App::wnd()->activateWindow();

	auto withComment = (addedComment != nullptr);
	box->setConfirmedCallback(base::lambda_guarded(this, [this, withComment, sendCallback = std::move(callback)](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, bool ctrlShiftEnter) {
		if (!canWriteMessage()) return;

		auto replyTo = replyToId();
		if (withComment) {
			onSend(ctrlShiftEnter, replyTo);
		}
		sendCallback(files, image, std::move(information), compressed, caption, replyTo);
	}));

	if (withComment) {
		auto was = _field->getTextWithTags();
		setFieldText({ *addedComment, TextWithTags::Tags() });
		box->setCancelledCallback(base::lambda_guarded(this, [this, was] {
			setFieldText(was);
		}));
	} else if (!insertTextOnCancel.isEmpty()) {
		box->setCancelledCallback(base::lambda_guarded(this, [this, insertTextOnCancel] {
			_field->textCursor().insertText(insertTextOnCancel);
		}));
	}

	Ui::show(std::move(box));
	return true;
}

template <typename Callback>
bool HistoryWidget::validateSendingFiles(const SendingFilesLists &lists, Callback callback) {
	if (!canWriteMessage()) return false;

	App::wnd()->activateWindow();
	if (!lists.nonLocalUrls.isEmpty()) {
		Ui::show(Box<InformBox>(lng_send_image_non_local(lt_name, lists.nonLocalUrls.front().toDisplayString())));
	} else if (!lists.emptyFiles.isEmpty()) {
		Ui::show(Box<InformBox>(lng_send_image_empty(lt_name, lists.emptyFiles.front())));
	} else if (!lists.tooLargeFiles.isEmpty()) {
		Ui::show(Box<InformBox>(lng_send_image_too_large(lt_name, lists.tooLargeFiles.front())));
	} else if (!lists.filesToSend.isEmpty()) {
		return callback(lists.filesToSend);
	}
	return false;
}

bool HistoryWidget::confirmSendingFiles(const QList<QUrl> &files, CompressConfirm compressed, const QString *addedComment) {
	return confirmSendingFiles(getSendingFilesLists(files), compressed, addedComment);
}

bool HistoryWidget::confirmSendingFiles(const QStringList &files, CompressConfirm compressed, const QString *addedComment) {
	return confirmSendingFiles(getSendingFilesLists(files), compressed, addedComment);
}

bool HistoryWidget::confirmSendingFiles(const SendingFilesLists &lists, CompressConfirm compressed, const QString *addedComment) {
	return validateSendingFiles(lists, [this, &lists, compressed, addedComment](const QStringList &files) {
		auto insertTextOnCancel = QString();
		auto sendCallback = [this](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, MsgId replyTo) {
			auto type = compressed ? SendMediaType::Photo : SendMediaType::File;
			uploadFilesAfterConfirmation(files, QByteArray(), image, std::move(information), type, caption);
		};
		auto boxCompressConfirm = compressed;
		if (files.size() > 1 && !lists.allFilesForCompress) {
			boxCompressConfirm = CompressConfirm::None;
		}
		auto box = Box<SendFilesBox>(files, boxCompressConfirm);
		return showSendFilesBox(std::move(box), insertTextOnCancel, addedComment, std::move(sendCallback));
	});
}

bool HistoryWidget::confirmSendingFiles(const QImage &image, const QByteArray &content, CompressConfirm compressed, const QString &insertTextOnCancel) {
	if (!canWriteMessage() || image.isNull()) return false;

	App::wnd()->activateWindow();
	auto sendCallback = [this, content](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, MsgId replyTo) {
		auto type = compressed ? SendMediaType::Photo : SendMediaType::File;
		uploadFilesAfterConfirmation(files, content, image, std::move(information), type, caption);
	};
	auto box = Box<SendFilesBox>(image, compressed);
	return showSendFilesBox(std::move(box), insertTextOnCancel, nullptr, std::move(sendCallback));
}

bool HistoryWidget::confirmSendingFiles(const QMimeData *data, CompressConfirm compressed, const QString &insertTextOnCancel) {
	if (!canWriteMessage()) {
		return false;
	}

	auto urls = data->urls();
	if (!urls.isEmpty()) {
		for_const (auto &url, urls) {
			if (url.isLocalFile()) {
				confirmSendingFiles(urls, compressed);
				return true;
			}
		}
	}
	if (data->hasImage()) {
		auto image = qvariant_cast<QImage>(data->imageData());
		if (!image.isNull()) {
			confirmSendingFiles(image, QByteArray(), compressed, insertTextOnCancel);
			return true;
		}
	}
	return false;
}

bool HistoryWidget::confirmShareContact(const QString &phone, const QString &fname, const QString &lname, const QString *addedComment) {
	if (!canWriteMessage()) return false;

	auto box = Box<SendFilesBox>(phone, fname, lname);
	auto sendCallback = [this, phone, fname, lname](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, MsgId replyTo) {
		shareContact(_peer->id, phone, fname, lname, replyTo);
	};
	auto insertTextOnCancel = QString();
	return showSendFilesBox(std::move(box), insertTextOnCancel, addedComment, std::move(sendCallback));
}

HistoryWidget::SendingFilesLists HistoryWidget::getSendingFilesLists(const QList<QUrl> &files) {
	auto result = SendingFilesLists();
	for_const (auto &url, files) {
		if (!url.isLocalFile()) {
			result.nonLocalUrls.push_back(url);
		} else {
			auto filepath = Platform::File::UrlToLocal(url);
			getSendingLocalFileInfo(result, filepath);
		}
	}
	return result;
}

HistoryWidget::SendingFilesLists HistoryWidget::getSendingFilesLists(const QStringList &files) {
	auto result = SendingFilesLists();
	for_const (auto &filepath, files) {
		getSendingLocalFileInfo(result, filepath);
	}
	return result;
}

void HistoryWidget::getSendingLocalFileInfo(SendingFilesLists &result, const QString &filepath) {
	auto hasExtensionForCompress = [](const QString &filepath) {
		for_const (auto extension, cExtensionsForCompress()) {
			if (filepath.right(extension.size()).compare(extension, Qt::CaseInsensitive) == 0) {
				return true;
			}
		}
		return false;
	};
	auto fileinfo = QFileInfo(filepath);
	if (fileinfo.isDir()) {
		result.directories.push_back(filepath);
	} else {
		auto filesize = fileinfo.size();
		if (filesize <= 0) {
			result.emptyFiles.push_back(filepath);
		} else if (filesize > App::kFileSizeLimit) {
			result.tooLargeFiles.push_back(filepath);
		} else {
			result.filesToSend.push_back(filepath);
			if (result.allFilesForCompress) {
				if (filesize > App::kImageSizeLimit || !hasExtensionForCompress(filepath)) {
					result.allFilesForCompress = false;
				}
			}
		}
	}
}

void HistoryWidget::uploadFiles(const QStringList &files, SendMediaType type) {
	if (!canWriteMessage()) return;

	auto caption = QString();
	uploadFilesAfterConfirmation(files, QByteArray(), QImage(), std::unique_ptr<FileLoadTask::MediaInformation>(), type, caption);
}

void HistoryWidget::uploadFilesAfterConfirmation(const QStringList &files, const QByteArray &content, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, SendMediaType type, QString caption) {
	t_assert(canWriteMessage());

	auto to = FileLoadTo(_peer->id, _silent->checked(), replyToId());
	if (files.size() > 1 && !caption.isEmpty()) {
		MainWidget::MessageToSend message;
		message.history = _history;
		message.textWithTags = { caption, TextWithTags::Tags() };
		message.replyTo = to.replyTo;
		message.silent = to.silent;
		message.clearDraft = false;
		App::main()->sendMessage(message);
		caption = QString();
	}
	auto tasks = TasksList();
	tasks.reserve(files.size());
	for_const (auto &filepath, files) {
		if (filepath.isEmpty() && (!image.isNull() || !content.isNull())) {
			tasks.push_back(MakeShared<FileLoadTask>(content, image, type, to, caption));
		} else {
			tasks.push_back(MakeShared<FileLoadTask>(filepath, std::move(information), type, to, caption));
		}
	}
	_fileLoader.addTasks(tasks);

	cancelReplyAfterMediaSend(lastForceReplyReplied());
}

void HistoryWidget::uploadFile(const QByteArray &fileContent, SendMediaType type) {
	if (!canWriteMessage()) return;

	auto to = FileLoadTo(_peer->id, _silent->checked(), replyToId());
	auto caption = QString();
	_fileLoader.addTask(MakeShared<FileLoadTask>(fileContent, QImage(), type, to, caption));

	cancelReplyAfterMediaSend(lastForceReplyReplied());
}

void HistoryWidget::sendFileConfirmed(const FileLoadResultPtr &file) {
	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(peerToChannel(file->to.peer), file->to.replyTo));

	FullMsgId newId(peerToChannel(file->to.peer), clientMsgId());

	connect(App::uploader(), SIGNAL(photoReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onPhotoUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(thumbDocumentReady(const FullMsgId&,bool,const MTPInputFile&,const MTPInputFile&)), this, SLOT(onThumbDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&, const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoProgress(const FullMsgId&)), this, SLOT(onPhotoProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentProgress(const FullMsgId&)), this, SLOT(onDocumentProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoFailed(const FullMsgId&)), this, SLOT(onPhotoFailed(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentFailed(const FullMsgId&)), this, SLOT(onDocumentFailed(const FullMsgId&)), Qt::UniqueConnection);

	App::uploader()->upload(newId, file);

	History *h = App::history(file->to.peer);

	fastShowAtEnd(h);

	auto flags = newMessageFlags(h->peer) | MTPDmessage::Flag::f_media; // unread, out
	if (file->to.replyTo) flags |= MTPDmessage::Flag::f_reply_to_msg_id;
	bool channelPost = h->peer->isChannel() && !h->peer->isMegagroup();
	bool showFromName = !channelPost || h->peer->asChannel()->addsSignature();
	bool silentPost = channelPost && file->to.silent;
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::Flag::f_from_id;
	}
	if (silentPost) {
		flags |= MTPDmessage::Flag::f_silent;
	}
	auto messageFromId = showFromName ? AuthSession::CurrentUserId() : 0;
	if (file->type == SendMediaType::Photo) {
		h->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaPhoto(file->photo, MTP_string(file->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	} else if (file->type == SendMediaType::File) {
		h->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(file->document, MTP_string(file->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	} else if (file->type == SendMediaType::Audio) {
		if (!h->peer->isChannel()) {
			flags |= MTPDmessage::Flag::f_media_unread;
		}
		h->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(file->document, MTP_string(file->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	}

	if (_peer && file->to.peer == _peer->id) {
		App::main()->historyToDown(_history);
	}
	App::main()->dialogsToUp();
	peerMessagesUpdated(file->to.peer);

	cancelReplyAfterMediaSend(lastKeyboardUsed);
}

void HistoryWidget::onPhotoUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file) {
	if (auto item = App::histItemById(newId)) {
		uint64 randomId = rand_value<uint64>();
		App::historyRegRandom(randomId, newId);
		History *hist = item->history();
		MsgId replyTo = item->replyToId();
		auto sendFlags = MTPmessages_SendMedia::Flags(0);
		if (replyTo) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
		}

		bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup();
		bool silentPost = channelPost && silent;
		if (silentPost) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
		}
		auto caption = item->getMedia() ? item->getMedia()->getCaption() : TextWithEntities();
		auto media = MTP_inputMediaUploadedPhoto(MTP_flags(0), file, MTP_string(caption.text), MTPVector<MTPInputDocument>());
		hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), item->history()->peer->input, MTP_int(replyTo), media, MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
	}
}

namespace {
	MTPVector<MTPDocumentAttribute> _composeDocumentAttributes(DocumentData *document) {
		QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(document->name)));
		if (document->dimensions.width() > 0 && document->dimensions.height() > 0) {
			int32 duration = document->duration();
			if (duration >= 0) {
				attributes.push_back(MTP_documentAttributeVideo(MTP_int(duration), MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
			} else {
				attributes.push_back(MTP_documentAttributeImageSize(MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
			}
		}
		if (document->type == AnimatedDocument) {
			attributes.push_back(MTP_documentAttributeAnimated());
		} else if (document->type == StickerDocument && document->sticker()) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(document->sticker()->alt), document->sticker()->set, MTPMaskCoords()));
		} else if (document->type == SongDocument && document->song()) {
			auto flags = MTPDdocumentAttributeAudio::Flag::f_title | MTPDdocumentAttributeAudio::Flag::f_performer;
			attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(document->song()->duration), MTP_string(document->song()->title), MTP_string(document->song()->performer), MTPstring()));
		} else if (document->type == VoiceDocument && document->voice()) {
			auto flags = MTPDdocumentAttributeAudio::Flag::f_voice | MTPDdocumentAttributeAudio::Flag::f_waveform;
			attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(document->voice()->duration), MTPstring(), MTPstring(), MTP_bytes(documentWaveformEncode5bit(document->voice()->waveform))));
		}
		return MTP_vector<MTPDocumentAttribute>(attributes);
	}
}

void HistoryWidget::onDocumentUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file) {
	if (auto item = dynamic_cast<HistoryMessage*>(App::histItemById(newId))) {
		auto media = item->getMedia();
		if (auto document = media ? media->getDocument() : nullptr) {
			auto randomId = rand_value<uint64>();
			App::historyRegRandom(randomId, newId);
			auto hist = item->history();
			auto replyTo = item->replyToId();
			auto sendFlags = MTPmessages_SendMedia::Flags(0);
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
			}

			bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup();
			bool silentPost = channelPost && silent;
			if (silentPost) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
			}
			auto caption = item->getMedia() ? item->getMedia()->getCaption() : TextWithEntities();
			auto media = MTP_inputMediaUploadedDocument(MTP_flags(0), file, MTP_string(document->mime), _composeDocumentAttributes(document), MTP_string(caption.text), MTPVector<MTPInputDocument>());
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), item->history()->peer->input, MTP_int(replyTo), media, MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onThumbDocumentUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file, const MTPInputFile &thumb) {
	if (auto item = dynamic_cast<HistoryMessage*>(App::histItemById(newId))) {
		auto media = item->getMedia();
		if (auto document = media ? media->getDocument() : nullptr) {
			auto randomId = rand_value<uint64>();
			App::historyRegRandom(randomId, newId);
			auto hist = item->history();
			auto replyTo = item->replyToId();
			auto sendFlags = MTPmessages_SendMedia::Flags(0);
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
			}

			bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup();
			bool silentPost = channelPost && silent;
			if (silentPost) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
			}
			auto caption = media ? media->getCaption() : TextWithEntities();
			auto media = MTP_inputMediaUploadedThumbDocument(MTP_flags(0), file, thumb, MTP_string(document->mime), _composeDocumentAttributes(document), MTP_string(caption.text), MTPVector<MTPInputDocument>());
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), item->history()->peer->input, MTP_int(replyTo), media, MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onPhotoProgress(const FullMsgId &newId) {
	if (auto item = App::histItemById(newId)) {
		auto photo = (item->getMedia() && item->getMedia()->type() == MediaTypePhoto) ? static_cast<HistoryPhoto*>(item->getMedia())->photo() : nullptr;
		if (!item->isPost()) {
			updateSendAction(item->history(), SendAction::Type::UploadPhoto, 0);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onDocumentProgress(const FullMsgId &newId) {
	if (auto item = App::histItemById(newId)) {
		auto media = item->getMedia();
		auto document = media ? media->getDocument() : nullptr;
		if (!item->isPost()) {
			updateSendAction(item->history(), (document && document->voice()) ? SendAction::Type::UploadVoice : SendAction::Type::UploadFile, document ? document->uploadOffset : 0);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onPhotoFailed(const FullMsgId &newId) {
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		if (!item->isPost()) {
			updateSendAction(item->history(), SendAction::Type::UploadPhoto, -1);
		}
//		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onDocumentFailed(const FullMsgId &newId) {
	if (auto item = App::histItemById(newId)) {
		auto media = item->getMedia();
		auto document = media ? media->getDocument() : nullptr;
		if (!item->isPost()) {
			updateSendAction(item->history(), (document && document->voice()) ? SendAction::Type::UploadVoice : SendAction::Type::UploadFile, -1);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onReportSpamClicked() {
	_clearPeer = _peer;
	auto text = lang(_peer->isUser() ? lng_report_spam_sure : ((_peer->isChat() || _peer->isMegagroup()) ? lng_report_spam_sure_group : lng_report_spam_sure_channel));
	Ui::show(Box<ConfirmBox>(text, lang(lng_report_spam_ok), st::attentionBoxButton, base::lambda_guarded(this, [this] {
		if (_reportSpamRequest) return;

		Ui::hideLayer();
		if (_clearPeer->isUser()) MTP::send(MTPcontacts_Block(_clearPeer->asUser()->inputUser), rpcDone(&HistoryWidget::blockDone, _clearPeer), RPCFailHandlerPtr(), 0, 5);
		_reportSpamRequest = MTP::send(MTPmessages_ReportSpam(_clearPeer->input), rpcDone(&HistoryWidget::reportSpamDone, _clearPeer), rpcFail(&HistoryWidget::reportSpamFail));
	})));
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
	_reportSpamPanel->setReported(_reportSpamStatus == dbiprsReportSent, peer);
}

bool HistoryWidget::reportSpamFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (req == _reportSpamRequest) {
		_reportSpamRequest = 0;
	}
	return false;
}

void HistoryWidget::onReportSpamHide() {
	if (_peer) {
		cRefReportSpamStatuses().insert(_peer->id, dbiprsHidden);
		Local::writeReportSpamStatuses();

		MTP::send(MTPmessages_HideReportSpam(_peer->input));
	}
	_reportSpamStatus = dbiprsHidden;
	updateControlsVisibility();
}

void HistoryWidget::onReportSpamClear() {
	_clearPeer = _peer;
	if (_clearPeer->isUser()) {
		App::main()->deleteConversation(_clearPeer);
	} else if (_clearPeer->isChat()) {
		App::main()->showBackFromStack();
		MTP::send(MTPmessages_DeleteChatUser(_clearPeer->asChat()->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, _clearPeer), App::main()->rpcFail(&MainWidget::leaveChatFailed, _clearPeer));
	} else if (_clearPeer->isChannel()) {
		App::main()->showBackFromStack();
		if (_clearPeer->migrateFrom()) {
			App::main()->deleteConversation(_clearPeer->migrateFrom());
		}
		MTP::send(MTPchannels_LeaveChannel(_clearPeer->asChannel()->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
	}
}

void HistoryWidget::peerMessagesUpdated(PeerId peer) {
	if (_peer && _list && peer == _peer->id) {
		updateListSize();
		updateBotKeyboard();
		if (!_scroll->isHidden()) {
			bool unblock = isBlocked(), botStart = isBotStart(), joinChannel = isJoinChannel(), muteUnmute = isMuteUnmute();
			bool upd = (_unblock->isHidden() == unblock);
			if (!upd && !unblock) upd = (_botStart->isHidden() == botStart);
			if (!upd && !unblock && !botStart) upd = (_joinChannel->isHidden() == joinChannel);
			if (!upd && !unblock && !botStart && !joinChannel) upd = (_muteUnmute->isHidden() == muteUnmute);
			if (upd) {
				updateControlsVisibility();
				updateControlsGeometry();
			}
		}
	}
}

void HistoryWidget::peerMessagesUpdated() {
	if (_list) peerMessagesUpdated(_peer->id);
}

void HistoryWidget::grapWithoutTopBarShadow() {
	grabStart();
	_topShadow->hide();
}

void HistoryWidget::grabFinish() {
	_inGrab = false;
	resizeEvent(0);
	_topShadow->show();
}

bool HistoryWidget::isItemVisible(HistoryItem *item) {
	if (isHidden() || _a_show.animating() || !_list) {
		return false;
	}
	int32 top = _list->itemTop(item), st = _scroll->scrollTop();
	if (top < 0 || top + item->height() <= st || top >= st + _scroll->height()) {
		return false;
	}
	return true;
}

void HistoryWidget::ui_repaintHistoryItem(const HistoryItem *item) {
	if (_peer && _list && (item->history() == _history || (_migrated && item->history() == _migrated))) {
		auto ms = getms();
		if (_lastScrolled + 100 <= ms) {
			_list->repaintItem(item);
		} else {
			_updateHistoryItems.start(_lastScrolled + 100 - ms);
		}
	}
}

void HistoryWidget::onUpdateHistoryItems() {
	if (!_list) return;

	auto ms = getms();
	if (_lastScrolled + 100 <= ms) {
		_list->update();
	} else {
		_updateHistoryItems.start(_lastScrolled + 100 - ms);
	}
}

bool HistoryWidget::ui_isInlineItemBeingChosen() {
	return _emojiPan->ui_isInlineItemBeingChosen()
		|| (_inlineResults && _inlineResults->ui_isInlineItemBeingChosen());
}

PeerData *HistoryWidget::ui_getPeerForMouseAction() {
	return _peer;
}

void HistoryWidget::notify_historyItemLayoutChanged(const HistoryItem *item) {
	if (_peer && _list && (item == App::mousedItem() || item == App::hoveredItem() || item == App::hoveredLinkItem())) {
		_list->onUpdateSelected();
	}
}

void HistoryWidget::notify_handlePendingHistoryUpdate() {
	if (hasPendingResizedItems()) {
		updateListSize();
		_list->update();
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	auto layout = (width() < st::adaptiveChatWideWidth) ? Adaptive::ChatLayout::Normal : Adaptive::ChatLayout::Wide;
	if (layout != Global::AdaptiveChatLayout()) {
		Global::SetAdaptiveChatLayout(layout);
		Adaptive::Changed().notify(true);
	}
	updateControlsGeometry();
}

void HistoryWidget::updateControlsGeometry() {
	_topBar->setGeometryToLeft(0, 0, width(), st::topBarHeight);
	_reportSpamPanel->resize(width(), _reportSpamPanel->height());

	moveFieldControls();

	auto scrollAreaTop = _topBar->bottomNoMargins();
	if (_pinnedBar) {
		_pinnedBar->cancel->move(width() - _pinnedBar->cancel->width(), scrollAreaTop);
		scrollAreaTop += st::historyReplyHeight;
		_pinnedBar->shadow->setGeometry(0, scrollAreaTop, width(), st::lineWidth);
	}
	if (_scroll->y() != scrollAreaTop) {
		_scroll->move(0, scrollAreaTop);
		_reportSpamPanel->move(0, scrollAreaTop);
		_fieldAutocomplete->setBoundings(_scroll->geometry());
	}

	updateListSize(false, false, { ScrollChangeAdd, App::main() ? App::main()->contentScrollAddToY() : 0 });

	updateFieldSize();

	updateHistoryDownPosition();

	_emojiPan->setMinTop(0);
	_emojiPan->setMinBottom(_attachEmoji->height());
	if (_inlineResults) {
		_inlineResults->setMinTop(0);
		_inlineResults->setMinBottom(_attachEmoji->height());
	}
	if (_membersDropdown) {
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}

	switch (_attachDrag) {
	case DragStateFiles:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
	break;
	case DragStatePhotoFiles:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), (height() - st::dragMargin.top() - st::dragMargin.bottom()) / 2);
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
		_attachDragPhoto->resize(_attachDragDocument->width(), _attachDragDocument->height());
		_attachDragPhoto->move(st::dragMargin.left(), height() - _attachDragPhoto->height() - st::dragMargin.bottom());
	break;
	case DragStateImage:
		_attachDragPhoto->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragPhoto->move(st::dragMargin.left(), st::dragMargin.top());
	break;
	}

	_topShadow->resize(width() - ((!Adaptive::OneColumn() && !_inGrab) ? st::lineWidth : 0), st::lineWidth);
	_topShadow->moveToLeft((!Adaptive::OneColumn() && !_inGrab) ? st::lineWidth : 0, _topBar->bottomNoMargins());
}

void HistoryWidget::itemRemoved(HistoryItem *item) {
	if (item == _replyEditMsg) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
	if (item == _replyReturn) {
		calcNextReplyReturn();
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		pinnedMsgVisibilityUpdated();
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		onKbToggle();
		_kbReplyTo = 0;
	}
}

void HistoryWidget::itemEdited(HistoryItem *item) {
	if (item == _replyEditMsg) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateScrollColors() {
	_scroll->updateBars();
}

MsgId HistoryWidget::replyToId() const {
	return _replyToId ? _replyToId : (_kbReplyTo ? _kbReplyTo->id : 0);
}

void HistoryWidget::updateListSize(bool initial, bool loadedDown, const ScrollChange &change) {
	if (!_history || (initial && _histInited) || (!initial && !_histInited)) return;
	if (_firstLoadRequest || _a_show.animating()) {
		return; // scrollTopMax etc are not working after recountHeight()
	}

	int newScrollHeight = height() - _topBar->height();
	if (isBlocked() || isBotStart() || isJoinChannel() || isMuteUnmute()) {
		newScrollHeight -= _unblock->height();
	} else {
		if (_canSendMessages) {
			newScrollHeight -= (_field->height() + 2 * st::historySendPadding);
		}
		if (_editMsgId || replyToId() || readyToForward() || (_previewData && _previewData->pendingTill >= 0)) {
			newScrollHeight -= st::historyReplyHeight;
		}
		if (_kbShown) {
			newScrollHeight -= _kbScroll->height();
		}
	}
	if (_pinnedBar) {
		newScrollHeight -= st::historyReplyHeight;
	}
	int wasScrollTop = _scroll->scrollTop();
	bool wasAtBottom = wasScrollTop + 1 > _scroll->scrollTopMax();
	bool needResize = (_scroll->width() != width()) || (_scroll->height() != newScrollHeight);
	if (needResize) {
		_scroll->resize(width(), newScrollHeight);
		// on initial updateListSize we didn't put the _scroll->scrollTop correctly yet
		// so visibleAreaUpdated() call will erase it with the new (undefined) value
		if (!initial) {
			visibleAreaUpdated();
		}

		_fieldAutocomplete->setBoundings(_scroll->geometry());
		_historyDown->moveToRight(st::historyToDownPosition.x(), _scroll->y() + _scroll->height() - _historyDown->height() - st::historyToDownPosition.y());
	}

	_list->recountHeight();

	bool washidden = _scroll->isHidden();
	if (washidden) {
		_scroll->show();
	}
	_list->updateSize();
	if (washidden) {
		_scroll->hide();
	}

	if ((!initial && !wasAtBottom) || (loadedDown && (!_history->showFrom || _history->unreadBar || _history->loadedAtBottom()) && (!_migrated || !_migrated->showFrom || _migrated->unreadBar || _history->loadedAtBottom()))) {
		int toY = _list->historyScrollTop();
		if (change.type == ScrollChangeAdd) {
			toY += change.value;
		} else if (change.type == ScrollChangeNoJumpToBottom) {
			toY = wasScrollTop;
		} else if (_addToScroll) {
			toY += _addToScroll;
			_addToScroll = 0;
		}
		if (toY > _scroll->scrollTopMax()) {
			toY = _scroll->scrollTopMax();
		}
		if (_scroll->scrollTop() == toY) {
			visibleAreaUpdated();
		} else {
			_scroll->scrollToY(toY);
		}
		return;
	}

	if (initial) {
		_histInited = true;
	}

	int32 toY = ScrollMax;
	if (initial && (_history->scrollTopItem || (_migrated && _migrated->scrollTopItem))) {
		toY = _list->historyScrollTop();
	} else if (initial && _migrated && _showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId) {
		HistoryItem *item = App::histItemById(0, -_showAtMsgId);
		int32 iy = _list->itemTop(item);
		if (iy < 0) {
			setMsgId(0);
			_histInited = false;
			return updateListSize(initial, false, change);
		} else {
			toY = (_scroll->height() > item->height()) ? qMax(iy - (_scroll->height() - item->height()) / 2, 0) : iy;
			_animActiveStart = getms();
			_animActiveTimer.start(AnimationTimerDelta);
			_activeAnimMsgId = _showAtMsgId;
		}
	} else if (initial && _showAtMsgId > 0) {
		HistoryItem *item = App::histItemById(_channel, _showAtMsgId);
		int32 iy = _list->itemTop(item);
		if (iy < 0) {
			setMsgId(0);
			_histInited = false;
			return updateListSize(initial, false, change);
		} else {
			toY = (_scroll->height() > item->height()) ? qMax(iy - (_scroll->height() - item->height()) / 2, 0) : iy;
			_animActiveStart = getms();
			_animActiveTimer.start(AnimationTimerDelta);
			_activeAnimMsgId = _showAtMsgId;
			if (item->isGroupMigrate() && _migrated && !_migrated->isEmpty() && _migrated->loadedAtBottom() && _migrated->blocks.back()->items.back()->isGroupMigrate() && _list->historyTop() != _list->historyDrawTop()) {
				_activeAnimMsgId = -_migrated->blocks.back()->items.back()->id;
			}
		}
	} else if (initial && (_history->unreadBar || (_migrated && _migrated->unreadBar))) {
		toY = unreadBarTop();
	} else if (_migrated && _migrated->showFrom) {
		toY = _list->itemTop(_migrated->showFrom);
		if (toY < _scroll->scrollTopMax() + HistoryMessageUnreadBar::height() - HistoryMessageUnreadBar::marginTop()) {
			_migrated->addUnreadBar();
			if (_migrated->unreadBar) {
				setMsgId(ShowAtUnreadMsgId);
				_histInited = false;
				updateListSize(true);
				App::wnd()->checkHistoryActivation();
				return;
			}
		}
	} else if (_history->showFrom) {
		toY = _list->itemTop(_history->showFrom);
		if (toY < _scroll->scrollTopMax() + st::historyUnreadBarHeight) {
			_history->addUnreadBar();
			if (_history->unreadBar) {
				setMsgId(ShowAtUnreadMsgId);
				_histInited = false;
				updateListSize(true);
				App::wnd()->checkHistoryActivation();
				return;
			}
		}
	} else {
	}
	auto scrollMax = _scroll->scrollTopMax();
	accumulate_min(toY, scrollMax);
	if (_scroll->scrollTop() == toY) {
		visibleAreaUpdated();
	} else {
		_scroll->scrollToY(toY);
	}
}

int HistoryWidget::unreadBarTop() const {
	auto getUnreadBar = [this]() -> HistoryItem* {
		if (_migrated && _migrated->unreadBar) {
			return _migrated->unreadBar;
		}
		if (_history->unreadBar) {
			return _history->unreadBar;
		}
		return nullptr;
	};
	if (HistoryItem *bar = getUnreadBar()) {
		int result = _list->itemTop(bar) + HistoryMessageUnreadBar::marginTop();
		if (bar->Has<HistoryMessageDate>()) {
			result += bar->Get<HistoryMessageDate>()->height();
		}
		return result;
	}
	return -1;
}

void HistoryWidget::addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceived(peer, messages);
	if (!_firstLoadRequest) {
		updateListSize();
		if (_animActiveTimer.isActive() && _activeAnimMsgId > 0 && _migrated && !_migrated->isEmpty() && _migrated->loadedAtBottom() && _migrated->blocks.back()->items.back()->isGroupMigrate() && _list->historyTop() != _list->historyDrawTop() && _history) {
			HistoryItem *animActiveItem = App::histItemById(_history->channelId(), _activeAnimMsgId);
			if (animActiveItem && animActiveItem->isGroupMigrate()) {
				_activeAnimMsgId = -_migrated->blocks.back()->items.back()->id;
			}
		}
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceivedDown(peer, messages);
	if (!_firstLoadRequest) {
		updateListSize(false, true, { ScrollChangeNoJumpToBottom, 0 });
	}
}

void HistoryWidget::countHistoryShowFrom() {
	if (_migrated && _showAtMsgId == ShowAtUnreadMsgId && _migrated->unreadCount()) {
		_migrated->updateShowFrom();
	}
	if ((_migrated && _migrated->showFrom) || _showAtMsgId != ShowAtUnreadMsgId || !_history->unreadCount()) {
		_history->showFrom = nullptr;
		return;
	}
	_history->updateShowFrom();
}

void HistoryWidget::updateBotKeyboard(History *h, bool force) {
	if (h && h != _history && h != _migrated) {
		return;
	}

	bool changed = false;
	bool wasVisible = _kbShown || _kbReplyTo;
	if ((_replyToId && !_replyEditMsg) || _editMsgId || !_history) {
		changed = _keyboard->updateMarkup(nullptr, force);
	} else if (_replyToId && _replyEditMsg) {
		changed = _keyboard->updateMarkup(_replyEditMsg, force);
	} else {
		HistoryItem *keyboardItem = _history->lastKeyboardId ? App::histItemById(_channel, _history->lastKeyboardId) : nullptr;
		changed = _keyboard->updateMarkup(keyboardItem, force);
	}
	updateCmdStartShown();
	if (!changed) return;

	bool hasMarkup = _keyboard->hasMarkup(), forceReply = _keyboard->forceReply() && (!_replyToId || !_replyEditMsg);
	if (hasMarkup || forceReply) {
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _history->lastKeyboardUsed) {
			_history->lastKeyboardHiddenId = _history->lastKeyboardId;
		}
		if (!isBotStart() && !isBlocked() && _canSendMessages && (wasVisible || (_replyToId && _replyEditMsg) || (!_field->hasSendText() && !kbWasHidden()))) {
			if (!_a_show.animating()) {
				if (hasMarkup) {
					_kbScroll->show();
					_attachEmoji->hide();
					_botKeyboardHide->show();
				} else {
					_kbScroll->hide();
					_attachEmoji->show();
					_botKeyboardHide->hide();
				}
				_botKeyboardShow->hide();
				_botCommandStart->hide();
			}
			int32 maxh = hasMarkup ? qMin(_keyboard->height(), st::historyComposeFieldMaxHeight - (st::historyComposeFieldMaxHeight / 2)) : 0;
			_field->setMaxHeight(st::historyComposeFieldMaxHeight - maxh);
			_kbShown = hasMarkup;
			_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply()) ? App::histItemById(_keyboard->forMsgId()) : 0;
			if (_kbReplyTo && !_replyToId) {
				updateReplyToName();
				_replyEditMsgText.setText(st::messageTextStyle, textClean(_kbReplyTo->inReplyText()), _textDlgOptions);
				_fieldBarCancel->show();
				updateMouseTracking();
			}
		} else {
			if (!_a_show.animating()) {
				_kbScroll->hide();
				_attachEmoji->show();
				_botKeyboardHide->hide();
				_botKeyboardShow->show();
				_botCommandStart->hide();
			}
			_field->setMaxHeight(st::historyComposeFieldMaxHeight);
			_kbShown = false;
			_kbReplyTo = 0;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		}
	} else {
		if (!_scroll->isHidden()) {
			_kbScroll->hide();
			_attachEmoji->show();
			_botKeyboardHide->hide();
			_botKeyboardShow->hide();
			_botCommandStart->show();
		}
		_field->setMaxHeight(st::historyComposeFieldMaxHeight);
		_kbShown = false;
		_kbReplyTo = 0;
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId && !_editMsgId) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
	}
	resizeEvent(0);
	update();
}

void HistoryWidget::updateHistoryDownPosition() {
	auto top = anim::interpolate(0, _historyDown->height() + st::historyToDownPosition.y(), _historyDownShown.current(_historyDownIsShown ? 1. : 0.));
	_historyDown->moveToRight(st::historyToDownPosition.x(), _scroll->height() - top);
	auto shouldBeHidden = !_historyDownIsShown && !_historyDownShown.animating();
	if (shouldBeHidden != _historyDown->isHidden()) {
		_historyDown->setVisible(!shouldBeHidden);
	}
}

void HistoryWidget::updateHistoryDownVisibility() {
	if (_a_show.animating()) return;

	auto haveUnreadBelowBottom = [this](History *history) {
		if (!_list || !history || history->unreadCount() <= 0) {
			return false;
		}
		if (!history->showFrom || history->showFrom->detached()) {
			return false;
		}
		return (_list->itemTop(history->showFrom) >= _scroll->scrollTop() + _scroll->height());
	};
	auto historyDownIsVisible = [this, &haveUnreadBelowBottom]() {
		if (!_history || _firstLoadRequest) {
			return false;
		}
		if (!_history->loadedAtBottom() || _replyReturn) {
			return true;
		}
		if (_scroll->scrollTop() + st::historyToDownShownAfter < _scroll->scrollTopMax()) {
			return true;
		}
		if (haveUnreadBelowBottom(_history) || haveUnreadBelowBottom(_migrated)) {
			return true;
		}
		return false;
	};
	auto historyDownIsShown = historyDownIsVisible();
	if (_historyDownIsShown != historyDownIsShown) {
		_historyDownIsShown = historyDownIsShown;
		_historyDownShown.start([this] { updateHistoryDownPosition(); }, _historyDownIsShown ? 0. : 1., _historyDownIsShown ? 1. : 0., st::historyToDownDuration);
	}
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	_replyForwardPressed = QRect(0, _field->y() - st::historySendPadding - st::historyReplyHeight, st::historyReplySkip, st::historyReplyHeight).contains(e->pos());
	if (_replyForwardPressed && !_fieldBarCancel->isHidden()) {
		updateField();
	} else if (_inReplyEdit) {
		Ui::showPeerHistory(_peer, _editMsgId ? _editMsgId : replyToId());
	} else if (_inPinnedMsg) {
		t_assert(_pinnedBar != nullptr);
		Ui::showPeerHistory(_peer, _pinnedBar->msgId);
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		App::main()->showBackFromStack();
		emit cancelled();
	} else if (e->key() == Qt::Key_PageDown) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_PageUp) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Down) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll->keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Up) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			if (_history && _history->lastSentMsg && _history->lastSentMsg->canEdit(::date(unixtime()))) {
				if (_field->isEmpty() && !_editMsgId && !_replyToId) {
					App::contextItem(_history->lastSentMsg);
					onEditMessage();
					return;
				}
			}
			_scroll->keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		onListEnterPressed();
	} else {
		e->ignore();
	}
}

void HistoryWidget::onFieldTabbed() {
	if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
	}
}

bool HistoryWidget::onStickerSend(DocumentData *sticker) {
	return sendExistingDocument(sticker, QString());
}

void HistoryWidget::onPhotoSend(PhotoData *photo) {
	sendExistingPhoto(photo, QString());
}

void HistoryWidget::onInlineResultSend(InlineBots::Result *result, UserData *bot) {
	if (!_history || !result || !canSendMessages(_peer)) return;

	App::main()->readServerHistory(_history);
	fastShowAtEnd(_history);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	auto flags = newMessageFlags(_peer) | MTPDmessage::Flag::f_media; // unread, out
	auto sendFlags = qFlags(MTPmessages_SendInlineBotResult::Flag::f_clear_draft);
	if (replyToId()) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool showFromName = !channelPost || _peer->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent->checked();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::Flag::f_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_silent;
	}
	if (bot) {
		flags |= MTPDmessage::Flag::f_via_bot_id;
	}

	auto messageFromId = showFromName ? AuthSession::CurrentUserId() : 0;
	MTPint messageDate = MTP_int(unixtime());
	UserId messageViaBotId = bot ? peerToUser(bot->id) : 0;
	MsgId messageId = newId.msg;

	result->addToHistory(_history, flags, messageId, messageFromId, messageDate, messageViaBotId, replyToId());

	_history->sendRequestId = MTP::send(MTPmessages_SendInlineBotResult(MTP_flags(sendFlags), _peer->input, MTP_int(replyToId()), MTP_long(randomId), MTP_long(result->getQueryId()), MTP_string(result->getId())), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _silent->checked());
	cancelReply(lastKeyboardUsed);

	App::historyRegRandom(randomId, newId);

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	RecentInlineBots &bots(cRefRecentInlineBots());
	int32 index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		Local::writeRecentHashtagsAndBots();
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
}

HistoryWidget::PinnedBar::PinnedBar(MsgId msgId, HistoryWidget *parent)
: msgId(msgId)
, cancel(parent, st::historyReplyCancel)
, shadow(parent, st::shadowFg) {
}

HistoryWidget::PinnedBar::~PinnedBar() {
	cancel.destroyDelayed();
	shadow.destroyDelayed();
}

void HistoryWidget::updatePinnedBar(bool force) {
	update();
	if (!_pinnedBar) {
		return;
	}
	if (!force) {
		if (_pinnedBar->msg) {
			return;
		}
	}

	t_assert(_history != nullptr);
	if (!_pinnedBar->msg) {
		_pinnedBar->msg = App::histItemById(_history->channelId(), _pinnedBar->msgId);
	}
	if (_pinnedBar->msg) {
		_pinnedBar->text.setText(st::messageTextStyle, textClean(_pinnedBar->msg->notificationText()), _textDlgOptions);
		update();
	} else if (force) {
		if (_peer && _peer->isMegagroup()) {
			_peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
		destroyPinnedBar();
		resizeEvent(0);
	}
}

bool HistoryWidget::pinnedMsgVisibilityUpdated() {
	bool result = false;
	MsgId pinnedMsgId = (_peer && _peer->isMegagroup()) ? _peer->asChannel()->mgInfo->pinnedMsgId : 0;
	if (pinnedMsgId && !_peer->asChannel()->amCreator() && !_peer->asChannel()->amEditor()) {
		Global::HiddenPinnedMessagesMap::const_iterator it = Global::HiddenPinnedMessages().constFind(_peer->id);
		if (it != Global::HiddenPinnedMessages().cend()) {
			if (it.value() == pinnedMsgId) {
				pinnedMsgId = 0;
			} else {
				Global::RefHiddenPinnedMessages().remove(_peer->id);
				Local::writeUserSettings();
			}
		}
	}
	if (pinnedMsgId) {
		if (!_pinnedBar) {
			_pinnedBar = std::make_unique<PinnedBar>(pinnedMsgId, this);
			if (_a_show.animating()) {
				_pinnedBar->cancel->hide();
				_pinnedBar->shadow->hide();
			} else {
				_pinnedBar->cancel->show();
				_pinnedBar->shadow->show();
			}
			connect(_pinnedBar->cancel, SIGNAL(clicked()), this, SLOT(onPinnedHide()));
			orderWidgets();

			updatePinnedBar();
			result = true;

			if (_scroll->scrollTop() != unreadBarTop()) {
				_scroll->scrollToY(_scroll->scrollTop() + st::historyReplyHeight);
			}
		} else if (_pinnedBar->msgId != pinnedMsgId) {
			_pinnedBar->msgId = pinnedMsgId;
			_pinnedBar->msg = 0;
			_pinnedBar->text.clear();
			updatePinnedBar();
		}
		if (!_pinnedBar->msg && App::api()) {
			App::api()->requestMessageData(_peer->asChannel(), _pinnedBar->msgId, replyEditMessageDataCallback());
		}
	} else if (_pinnedBar) {
		destroyPinnedBar();
		result = true;
		if (_scroll->scrollTop() != unreadBarTop()) {
			_scroll->scrollToY(_scroll->scrollTop() - st::historyReplyHeight);
		}
		resizeEvent(0);
	}
	return result;
}

void HistoryWidget::destroyPinnedBar() {
	_pinnedBar.reset();
	_inPinnedMsg = false;
}

bool HistoryWidget::sendExistingDocument(DocumentData *doc, const QString &caption) {
	if (!_history || !doc || !canSendMessages(_peer)) {
		return false;
	}

	MTPInputDocument mtpInput = doc->mtpInput();
	if (mtpInput.type() == mtpc_inputDocumentEmpty) {
		return false;
	}

	App::main()->readServerHistory(_history);
	fastShowAtEnd(_history);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	auto flags = newMessageFlags(_peer) | MTPDmessage::Flag::f_media; // unread, out
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (replyToId()) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool showFromName = !channelPost || _peer->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent->checked();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::Flag::f_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = showFromName ? AuthSession::CurrentUserId() : 0;
	_history->addNewDocument(newId.msg, flags, 0, replyToId(), date(MTP_int(unixtime())), messageFromId, doc, caption, MTPnullMarkup);

	_history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), _peer->input, MTP_int(replyToId()), MTP_inputMediaDocument(mtpInput, MTP_string(caption)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _silent->checked());
	cancelReplyAfterMediaSend(lastKeyboardUsed);

	if (doc->sticker()) App::main()->incrementSticker(doc);

	App::historyRegRandom(randomId, newId);

	if (_fieldAutocomplete->stickersShown()) {
		clearFieldText();
		//_saveDraftText = true;
		//_saveDraftStart = getms();
		//onDraftSave();
		onCloudDraftSave(); // won't be needed if SendInlineBotResult will clear the cloud draft
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
	return true;
}

void HistoryWidget::sendExistingPhoto(PhotoData *photo, const QString &caption) {
	if (!_history || !photo || !canSendMessages(_peer)) return;

	App::main()->readServerHistory(_history);
	fastShowAtEnd(_history);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	auto flags = newMessageFlags(_peer) | MTPDmessage::Flag::f_media; // unread, out
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (replyToId()) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool showFromName = !channelPost || _peer->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent->checked();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::Flag::f_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = showFromName ? AuthSession::CurrentUserId() : 0;
	_history->addNewPhoto(newId.msg, flags, 0, replyToId(), date(MTP_int(unixtime())), messageFromId, photo, caption, MTPnullMarkup);

	_history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), _peer->input, MTP_int(replyToId()), MTP_inputMediaPhoto(MTP_inputPhoto(MTP_long(photo->id), MTP_long(photo->access)), MTP_string(caption)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _silent->checked());
	cancelReplyAfterMediaSend(lastKeyboardUsed);

	App::historyRegRandom(randomId, newId);

	hideSelectorControlsAnimated();

	_field->setFocus();
}

void HistoryWidget::setFieldText(const TextWithTags &textWithTags, TextUpdateEvents events, Ui::FlatTextarea::UndoHistoryAction undoHistoryAction) {
	_textUpdateEvents = events;
	_field->setTextWithTags(textWithTags, undoHistoryAction);
	_field->moveCursor(QTextCursor::End);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;

	_previewCancelled = false;
	_previewData = nullptr;
	if (_previewRequest) {
		MTP::cancel(_previewRequest);
		_previewRequest = 0;
	}
	_previewLinks.clear();
}

void HistoryWidget::onReplyToMessage() {
	auto to = App::contextItem();
	if (!to || to->id <= 0 || !_canSendMessages) return;

	if (to->history() == _migrated) {
		if (to->isGroupMigrate() && !_history->isEmpty() && _history->blocks.front()->items.front()->isGroupMigrate() && _history != _migrated) {
			App::contextItem(_history->blocks.front()->items.front());
			onReplyToMessage();
			App::contextItem(to);
		} else {
			if (to->id < 0 || to->serviceMsg()) {
				Ui::show(Box<InformBox>(lang(lng_reply_cant)));
			} else {
				Ui::show(Box<ConfirmBox>(lang(lng_reply_cant_forward), lang(lng_selected_forward), base::lambda_guarded(this, [this] {
					auto item = App::contextItem();
					if (!item || item->id < 0 || item->serviceMsg()) return;

					App::forward(_peer->id, ForwardContextMessage);
				})));
			}
		}
		return;
	}

	App::main()->cancelForwarding();

	if (_editMsgId) {
		if (auto localDraft = _history->localDraft()) {
			localDraft->msgId = to->id;
		} else {
			_history->setLocalDraft(std::make_unique<Data::Draft>(TextWithTags(), to->id, MessageCursor(), false));
		}
	} else {
		_replyEditMsg = to;
		_replyToId = to->id;
		_replyEditMsgText.setText(st::messageTextStyle, textClean(_replyEditMsg->inReplyText()), _textDlgOptions);

		updateBotKeyboard();

		if (!_field->isHidden()) _fieldBarCancel->show();
		updateMouseTracking();
		updateReplyToName();
		resizeEvent(0);
		updateField();
	}

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	_field->setFocus();
}

void HistoryWidget::onEditMessage() {
	auto to = App::contextItem();
	if (!to) return;

	if (EditCaptionBox::canEdit(to)) {
		Ui::show(Box<EditCaptionBox>(to));
	} else {
		if (_recording) {
			// Just fix some strange inconsistency.
			_send->clearState();
		}
		if (!_editMsgId) {
			if (_replyToId || !_field->isEmpty()) {
				_history->setLocalDraft(std::make_unique<Data::Draft>(_field, _replyToId, _previewCancelled));
			} else {
				_history->clearLocalDraft();
			}
		}

		auto original = to->originalText();
		auto editText = textApplyEntities(original.text, original.entities);
		auto editTags = textTagsFromEntities(original.entities);
		TextWithTags editData = { editText, editTags };
		MessageCursor cursor = { editText.size(), editText.size(), QFIXED_MAX };
		_history->setEditDraft(std::make_unique<Data::Draft>(editData, to->id, cursor, false));
		applyDraft(false);

		_previewData = nullptr;
		if (auto media = to->getMedia()) {
			if (media->type() == MediaTypeWebPage) {
				_previewData = static_cast<HistoryWebPage*>(media)->webpage();
				updatePreview();
			}
		}
		if (!_previewData) {
			onPreviewParse();
		}

		updateBotKeyboard();

		if (!_field->isHidden()) _fieldBarCancel->show();
		updateFieldPlaceholder();
		updateMouseTracking();
		updateReplyToName();
		resizeEvent(nullptr);
		updateField();

		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();

		_field->setFocus();
	}
}

void HistoryWidget::onPinMessage() {
	HistoryItem *to = App::contextItem();
	if (!to || !to->canPin() || !_peer || !_peer->isMegagroup()) return;

	Ui::show(Box<PinMessageBox>(_peer->asChannel(), to->id));
}

void HistoryWidget::onUnpinMessage() {
	if (!_peer || !_peer->isMegagroup()) return;

	Ui::show(Box<ConfirmBox>(lang(lng_pinned_unpin_sure), lang(lng_pinned_unpin), base::lambda_guarded(this, [this] {
		if (!_peer || !_peer->isMegagroup()) return;

		_peer->asChannel()->mgInfo->pinnedMsgId = 0;
		if (pinnedMsgVisibilityUpdated()) {
			resizeEvent(0);
			update();
		}

		Ui::hideLayer();
		MTP::send(MTPchannels_UpdatePinnedMessage(MTP_flags(0), _peer->asChannel()->inputChannel, MTP_int(0)), rpcDone(&HistoryWidget::unpinDone));
	})));
}

void HistoryWidget::unpinDone(const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
}

void HistoryWidget::onPinnedHide() {
	if (!_peer || !_peer->isMegagroup()) return;
	if (!_peer->asChannel()->mgInfo->pinnedMsgId) {
		if (pinnedMsgVisibilityUpdated()) {
			resizeEvent(0);
			update();
		}
		return;
	}

	if (_peer->asChannel()->amCreator() || _peer->asChannel()->amEditor()) {
		onUnpinMessage();
	} else {
		Global::RefHiddenPinnedMessages().insert(_peer->id, _peer->asChannel()->mgInfo->pinnedMsgId);
		Local::writeUserSettings();
		if (pinnedMsgVisibilityUpdated()) {
			resizeEvent(0);
			update();
		}
	}
}

void HistoryWidget::onCopyPostLink() {
	HistoryItem *to = App::contextItem();
	if (!to || !to->hasDirectLink()) return;

	QApplication::clipboard()->setText(to->directLink());
}

bool HistoryWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	if (replyTo.msg > 0 && replyTo.channel != _channel) return false;
	return _keyboard->forceReply() && _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _keyboard->forMsgId().msg == (replyTo.msg < 0 ? replyToId() : replyTo.msg);
}

bool HistoryWidget::cancelReply(bool lastKeyboardUsed) {
	bool wasReply = false;
	if (_replyToId) {
		wasReply = true;

		_replyEditMsg = nullptr;
		_replyToId = 0;
		mouseMoveEvent(0);
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_kbReplyTo) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}

		updateBotKeyboard();

		resizeEvent(0);
		update();
	} else if (auto localDraft = (_history ? _history->localDraft() : nullptr)) {
		if (localDraft->msgId) {
			if (localDraft->textWithTags.text.isEmpty()) {
				_history->clearLocalDraft();
			} else {
				localDraft->msgId = 0;
			}
		}
	}
	if (wasReply) {
		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	}
	if (!_editMsgId && _keyboard->singleUse() && _keyboard->forceReply() && lastKeyboardUsed) {
		if (_kbReplyTo) {
			onKbToggle(false);
		}
	}
	return wasReply;
}

void HistoryWidget::cancelReplyAfterMediaSend(bool lastKeyboardUsed) {
	if (cancelReply(lastKeyboardUsed)) {
		onCloudDraftSave();
	}
}

int HistoryWidget::countMembersDropdownHeightMax() const {
	int result = height() - st::membersInnerDropdown.padding.top() - st::membersInnerDropdown.padding.bottom();
	result -= _attachEmoji->height();
	accumulate_min(result, st::membersInnerHeightMax);
	return result;
}

void HistoryWidget::cancelEdit() {
	if (!_editMsgId) return;

	_replyEditMsg = nullptr;
	_editMsgId = 0;
	_history->clearEditDraft();
	applyDraft();

	if (_saveEditMsgRequestId) {
		MTP::cancel(_saveEditMsgRequestId);
		_saveEditMsgRequestId = 0;
	}

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	mouseMoveEvent(nullptr);
	if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !replyToId()) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}

	auto old = _textUpdateEvents;
	_textUpdateEvents = 0;
	onTextChange();
	_textUpdateEvents = old;

	updateBotKeyboard();
	updateFieldPlaceholder();

	resizeEvent(nullptr);
	update();
}

void HistoryWidget::cancelForwarding() {
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::onFieldBarCancel() {
	Ui::hideLayer();
	_replyForwardPressed = false;
	if (_previewData && _previewData->pendingTill >= 0) {
		_previewCancelled = true;
		previewCancel();

		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	} else if (_editMsgId) {
		cancelEdit();
	} else if (readyToForward()) {
		App::main()->cancelForwarding();
	} else if (_replyToId) {
		cancelReply();
	} else if (_kbReplyTo) {
		onKbToggle();
	}
}

void HistoryWidget::onStickerPackInfo() {
	if (!App::contextItem()) return;

	if (HistoryMedia *media = App::contextItem()->getMedia()) {
		if (DocumentData *doc = media->getDocument()) {
			if (StickerData *sticker = doc->sticker()) {
				if (sticker->set.type() != mtpc_inputStickerSetEmpty) {
					App::main()->stickersBox(sticker->set);
				}
			}
		}
	}
}

void HistoryWidget::previewCancel() {
	MTP::cancel(_previewRequest);
	_previewRequest = 0;
	_previewData = nullptr;
	_previewLinks.clear();
	updatePreview();
	if (!_editMsgId && !_replyToId && !readyToForward() && !_kbReplyTo) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}
}

void HistoryWidget::onPreviewParse() {
	if (_previewCancelled) return;
	_field->parseLinks();
}

void HistoryWidget::onPreviewCheck() {
	if (_previewCancelled) return;
	QStringList linksList = _field->linksList();
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
		auto data = App::feedWebPage(result.c_messageMediaWebPage().vwebpage);
		_previewCache.insert(links, data->id);
		if (data->pendingTill > 0 && data->pendingTill <= unixtime()) {
			data->pendingTill = -1;
		}
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = (data->id && data->pendingTill >= 0) ? data : 0;
			updatePreview();
		}
		if (App::main()) App::main()->webPagesOrGamesUpdate();
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
		_fieldBarCancel->show();
		updateMouseTracking();
		if (_previewData->pendingTill) {
			_previewTitle.setText(st::msgNameStyle, lang(lng_preview_loading), _textNameOptions);
#ifndef OS_MAC_OLD
			auto linkText = _previewLinks.splitRef(' ').at(0).toString();
#else // OS_MAC_OLD
			auto linkText = _previewLinks.split(' ').at(0);
#endif // OS_MAC_OLD
			_previewDescription.setText(st::messageTextStyle, textClean(linkText), _textDlgOptions);

			int32 t = (_previewData->pendingTill - unixtime()) * 1000;
			if (t <= 0) t = 1;
			_previewTimer.start(t);
		} else {
			QString title, desc;
			if (_previewData->siteName.isEmpty()) {
				if (_previewData->title.isEmpty()) {
					if (_previewData->description.isEmpty()) {
						title = _previewData->author;
						desc = ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url);
					} else {
						title = _previewData->description;
						desc = _previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url) : _previewData->author;
					}
				} else {
					title = _previewData->title;
					desc = _previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url) : _previewData->author) : _previewData->description;
				}
			} else {
				title = _previewData->siteName;
				desc = _previewData->title.isEmpty() ? (_previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url) : _previewData->author) : _previewData->description) : _previewData->title;
			}
			if (title.isEmpty()) {
				if (_previewData->document) {
					title = lang(lng_attach_file);
				} else if (_previewData->photo) {
					title = lang(lng_attach_photo);
				}
			}
			_previewTitle.setText(st::msgNameStyle, title, _textNameOptions);
			_previewDescription.setText(st::messageTextStyle, textClean(desc), _textDlgOptions);
		}
	} else if (!readyToForward() && !replyToId() && !_editMsgId) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}
	resizeEvent(0);
	update();
}

void HistoryWidget::onCancel() {
	if (_isInlineBot) {
		onInlineBotCancel();
	} else if (_editMsgId) {
		auto original = _replyEditMsg ? _replyEditMsg->originalText() : TextWithEntities();
		auto editText = textApplyEntities(original.text, original.entities);
		auto editTags = textTagsFromEntities(original.entities);
		TextWithTags editData = { editText, editTags };
		if (_replyEditMsg && editData != _field->getTextWithTags()) {
			Ui::show(Box<ConfirmBox>(
				lang(lng_cancel_edit_post_sure),
				lang(lng_cancel_edit_post_yes),
				lang(lng_cancel_edit_post_no),
				base::lambda_guarded(this, [this] {
				onFieldBarCancel();
			})));
		} else {
			onFieldBarCancel();
		}
	} else if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->hideAnimated();
	} else  {
		App::main()->showBackFromStack();
		emit cancelled();
	}
}

void HistoryWidget::onFullPeerUpdated(PeerData *data) {
	if (_list && data == _peer) {
		bool newCanSendMessages = canSendMessages(_peer);
		if (newCanSendMessages != _canSendMessages) {
			_canSendMessages = newCanSendMessages;
			if (!_canSendMessages) {
				cancelReply();
			}
			updateControlsVisibility();
		}
		onCheckFieldAutocomplete();
		updateReportSpamStatus();
		_list->updateBotInfo();
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		updateControlsGeometry();
	} else if (!_scroll->isHidden() && _unblock->isHidden() == isBlocked()) {
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::peerUpdated(PeerData *data) {
	if (data && data == _peer) {
		if (data->migrateTo()) {
			Ui::showPeerHistory(data->migrateTo(), ShowAtUnreadMsgId);
			QTimer::singleShot(ReloadChannelMembersTimeout, App::api(), SLOT(delayedRequestParticipantsCount()));
			return;
		}
		QString restriction = _peer->restrictionReason();
		if (!restriction.isEmpty()) {
			App::main()->showBackFromStack();
			Ui::show(Box<InformBox>(restriction));
			return;
		}
		bool resize = false;
		if (pinnedMsgVisibilityUpdated()) {
			resize = true;
		}
		updateListSize();
		if (_peer->isChannel()) updateReportSpamStatus();
		if (App::api()) {
			if (data->isChat() && data->asChat()->noParticipantInfo()) {
				App::api()->requestFullPeer(data);
			} else if (data->isUser() && data->asUser()->blockStatus() == UserData::BlockStatus::Unknown) {
				App::api()->requestFullPeer(data);
			} else if (data->isMegagroup() && !data->asChannel()->mgInfo->botStatus) {
				App::api()->requestBots(data->asChannel());
			}
		}
		if (!_a_show.animating()) {
			if (_unblock->isHidden() == isBlocked() || (!isBlocked() && _joinChannel->isHidden() == isJoinChannel())) {
				resize = true;
			}
			bool newCanSendMessages = canSendMessages(_peer);
			if (newCanSendMessages != _canSendMessages) {
				_canSendMessages = newCanSendMessages;
				if (!_canSendMessages) {
					cancelReply();
				}
				resize = true;
			}
			updateControlsVisibility();
			if (resize) {
				updateControlsGeometry();
			}
		}
		App::main()->updateOnlineDisplay();
	}
}

void HistoryWidget::onForwardSelected() {
	if (!_list) return;
	App::main()->forwardLayer(true);
}

void HistoryWidget::confirmDeleteContextItem() {
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

void HistoryWidget::confirmDeleteSelectedItems() {
	if (!_list) return;

	SelectedItemSet selected;
	_list->fillSelectedItems(selected);
	if (selected.isEmpty()) return;

	App::main()->deleteLayer(selected.size());
}

void HistoryWidget::deleteContextItem(bool forEveryone) {
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

void HistoryWidget::deleteSelectedItems(bool forEveryone) {
	Ui::hideLayer();
	if (!_list) return;

	SelectedItemSet selected;
	_list->fillSelectedItems(selected);
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

void HistoryWidget::onListEscapePressed() {
	if (_selCount && _list) {
		onClearSelected();
	} else {
		onCancel();
	}
}

void HistoryWidget::onListEnterPressed() {
	if (!_botStart->isHidden()) {
		onBotStart();
	}
}

void HistoryWidget::onClearSelected() {
	if (_list) _list->clearSelectedItems();
}

void HistoryWidget::onAnimActiveStep() {
	if (!_history || !_activeAnimMsgId || (_activeAnimMsgId < 0 && (!_migrated || -_activeAnimMsgId >= ServerMaxMsgId))) {
		return _animActiveTimer.stop();
	}

	HistoryItem *item = (_activeAnimMsgId < 0 && -_activeAnimMsgId < ServerMaxMsgId && _migrated) ? App::histItemById(_migrated->channelId(), -_activeAnimMsgId) : App::histItemById(_channel, _activeAnimMsgId);
	if (!item || item->detached()) return _animActiveTimer.stop();

	if (getms() - _animActiveStart > st::activeFadeInDuration + st::activeFadeOutDuration) {
		stopAnimActive();
	} else {
		Ui::repaintHistoryItem(item);
	}
}

uint64 HistoryWidget::animActiveTimeStart(const HistoryItem *msg) const {
	if (!msg) return 0;
	if ((msg->history() == _history && msg->id == _activeAnimMsgId) || (_migrated && msg->history() == _migrated && msg->id == -_activeAnimMsgId)) {
		return _animActiveTimer.isActive() ? _animActiveStart : 0;
	}
	return 0;
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
		_topBar->showSelected(0);
		return;
	}

	int32 selectedForForward, selectedForDelete;
	_list->getSelectionState(selectedForForward, selectedForDelete);
	_selCount = selectedForForward ? selectedForForward : selectedForDelete;
	_topBar->showSelected(_selCount > 0 ? _selCount : 0, (selectedForDelete == selectedForForward));
	updateControlsVisibility();
	updateListSize();
	if (!Ui::isLayerShown() && !App::passcoded()) {
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked() || !_canSendMessages) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
	_topBar->update();
	update();
}

void HistoryWidget::messageDataReceived(ChannelData *channel, MsgId msgId) {
	if (!_peer || _peer->asChannel() != channel || !msgId) return;
	if (_editMsgId == msgId || _replyToId == msgId) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && _pinnedBar->msgId == msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateReplyEditTexts(bool force) {
	if (!force) {
		if (_replyEditMsg || (!_editMsgId && !_replyToId)) {
			return;
		}
	}
	if (!_replyEditMsg) {
		_replyEditMsg = App::histItemById(_channel, _editMsgId ? _editMsgId : _replyToId);
	}
	if (_replyEditMsg) {
		_replyEditMsgText.setText(st::messageTextStyle, textClean(_replyEditMsg->inReplyText()), _textDlgOptions);

		updateBotKeyboard();

		if (!_field->isHidden() || _recording) {
			_fieldBarCancel->show();
			updateMouseTracking();
		}
		updateReplyToName();
		updateField();
	} else if (force) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
}

void HistoryWidget::updateForwarding(bool force) {
	if (readyToForward()) {
		updateControlsVisibility();
	} else {
		updateControlsGeometry();
	}
}

void HistoryWidget::updateReplyToName() {
	if (_editMsgId) return;
	if (!_replyEditMsg && (_replyToId || !_kbReplyTo)) return;
	_replyToName.setText(st::msgNameStyle, App::peerName((_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()), _textNameOptions);
	_replyToNameVersion = (_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()->nameVersion;
}

void HistoryWidget::updateField() {
	int32 fy = _scroll->y() + _scroll->height();
	update(0, fy, width(), height() - fy);
}

void HistoryWidget::drawField(Painter &p, const QRect &rect) {
	int32 backy = _field->y() - st::historySendPadding, backh = _field->height() + 2 * st::historySendPadding;
	Text *from = 0, *text = 0;
	bool serviceColor = false, hasForward = readyToForward();
	ImagePtr preview;
	auto drawMsgText = (_editMsgId || _replyToId) ? _replyEditMsg : _kbReplyTo;
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		if (!_editMsgId && drawMsgText && drawMsgText->author()->nameVersion > _replyToNameVersion) {
			updateReplyToName();
		}
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (hasForward) {
		App::main()->fillForwardingInfo(from, text, serviceColor, preview);
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (_previewData && _previewData->pendingTill >= 0) {
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	}
	bool drawPreview = (_previewData && _previewData->pendingTill >= 0) && !_replyForwardPressed;
	p.fillRect(0, backy, width(), backh, st::historyReplyBg);
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		int32 replyLeft = st::historyReplySkip;
		(_editMsgId ? st::historyEditIcon : st::historyReplyIcon).paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		if (!drawPreview) {
			if (drawMsgText) {
				if (drawMsgText->getMedia() && drawMsgText->getMedia()->hasReplyPreview()) {
					ImagePtr replyPreview = drawMsgText->getMedia()->replyPreview();
					if (!replyPreview->isNull()) {
						QRect to(replyLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
						p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
					}
					replyLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
				}
				p.setPen(st::historyReplyNameFg);
				if (_editMsgId) {
					paintEditHeader(p, rect, replyLeft, backy);
				} else {
					_replyToName.drawElided(p, replyLeft, backy + st::msgReplyPadding.top(), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
				}
				p.setPen(((drawMsgText->toHistoryMessage() && drawMsgText->toHistoryMessage()->emptyText()) || drawMsgText->serviceMsg()) ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
				_replyEditMsgText.drawElided(p, replyLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			} else {
				p.setFont(st::msgDateFont);
				p.setPen(st::historyComposeAreaFgService);
				p.drawText(replyLeft, backy + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right()));
			}
		}
	} else if (from && text) {
		int forwardLeft = st::historyReplySkip;
		st::historyForwardIcon.paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
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
			p.setPen(st::historyReplyNameFg);
			from->drawElided(p, forwardLeft, backy + st::msgReplyPadding.top(), width() - forwardLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			p.setPen(serviceColor ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
			text->drawElided(p, forwardLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - forwardLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
		}
	}
	if (drawPreview) {
		int32 previewLeft = st::historyReplySkip + st::webPageLeft;
		p.fillRect(st::historyReplySkip, backy + st::msgReplyPadding.top(), st::webPageBar, st::msgReplyBarSize.height(), st::msgInReplyBarColor);
		if ((_previewData->photo && !_previewData->photo->thumb->isNull()) || (_previewData->document && !_previewData->document->thumb->isNull())) {
			ImagePtr replyPreview = _previewData->photo ? _previewData->photo->makeReplyPreview() : _previewData->document->makeReplyPreview();
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
		p.setPen(st::historyReplyNameFg);
		_previewTitle.drawElided(p, previewLeft, backy + st::msgReplyPadding.top(), width() - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
		p.setPen(st::historyComposeAreaFg);
		_previewDescription.drawElided(p, previewLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
	}
}

namespace {

constexpr int DisplayEditTimeWarningMs = 300 * 1000;
constexpr int FullDayInMs = 86400 * 1000;

} // namespace

void HistoryWidget::paintEditHeader(Painter &p, const QRect &rect, int left, int top) const {
	if (!rect.intersects(QRect(left, top, width() - left, st::normalFont->height))) {
		return;
	}

	p.setFont(st::msgServiceNameFont);
	p.drawText(left, top + st::msgReplyPadding.top() + st::msgServiceNameFont->ascent, lang(lng_edit_message));

	if (!_replyEditMsg) return;

	QString editTimeLeftText;
	int updateIn = -1;
	auto tmp = ::date(unixtime());
	auto timeSinceMessage = _replyEditMsg->date.msecsTo(QDateTime::currentDateTime());
	auto editTimeLeft = (Global::EditTimeLimit() * 1000LL) - timeSinceMessage;
	if (editTimeLeft < 2) {
		editTimeLeftText = qsl("0:00");
	} else if (editTimeLeft > DisplayEditTimeWarningMs) {
		updateIn = static_cast<int>(qMin(editTimeLeft - DisplayEditTimeWarningMs, qint64(FullDayInMs)));
	} else {
		updateIn = static_cast<int>(editTimeLeft % 1000);
		if (!updateIn) {
			updateIn = 1000;
		}
		++updateIn;

		editTimeLeft = (editTimeLeft - 1) / 1000; // seconds
		editTimeLeftText = qsl("%1:%2").arg(editTimeLeft / 60).arg(editTimeLeft % 60, 2, 10, QChar('0'));
	}

	// Restart timer only if we are sure that we've painted the whole timer.
	if (rect.contains(QRect(left, top, width() - left, st::normalFont->height)) && updateIn > 0) {
		_updateEditTimeLeftDisplay.start(updateIn);
	}

	if (!editTimeLeftText.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(left + st::msgServiceNameFont->width(lang(lng_edit_message)) + st::normalFont->spacew, top + st::msgReplyPadding.top() + st::msgServiceNameFont->ascent, editTimeLeftText);
	}
}

void HistoryWidget::drawRecording(Painter &p, float64 recordActive) {
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	auto delta = qMin(a_recordingLevel.current() / 0x4000, 1.);
	auto d = 2 * qRound(st::historyRecordSignalMin + (delta * (st::historyRecordSignalMax - st::historyRecordSignalMin)));
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(_attachToggle->x() + (_attachEmoji->width() - d) / 2, _attachToggle->y() + (_attachToggle->height() - d) / 2, d, d);
	}

	auto duration = formatDurationText(_recordingSamples / Media::Player::kDefaultFrequency);
	p.setFont(st::historyRecordFont);

	p.setPen(st::historyRecordDurationFg);
	p.drawText(_attachToggle->x() + _attachEmoji->width(), _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, duration);

	int32 left = _attachToggle->x() + _attachEmoji->width() + st::historyRecordFont->width(duration) + ((_send->width() - st::historyRecordVoice.width()) / 2);
	int32 right = width() - _send->width();

	p.setPen(anim::pen(st::historyRecordCancel, st::historyRecordCancelActive, 1. - recordActive));
	p.drawText(left + (right - left - _recordCancelWidth) / 2, _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, lang(lng_record_cancel));
}

void HistoryWidget::drawPinnedBar(Painter &p) {
	Expects(_pinnedBar != nullptr);

	auto top = _topBar->bottomNoMargins();
	Text *from = 0, *text = 0;
	bool serviceColor = false, hasForward = readyToForward();
	ImagePtr preview;
	p.fillRect(0, top, width(), st::historyReplyHeight, st::historyPinnedBg);

	top += st::msgReplyPadding.top();
	QRect rbar(rtlrect(st::msgReplyBarSkip + st::msgReplyBarPos.x(), top + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), width()));
	p.fillRect(rbar, st::msgInReplyBarColor);

	int32 left = st::msgReplyBarSkip + st::msgReplyBarSkip;
	if (_pinnedBar->msg) {
		if (_pinnedBar->msg->getMedia() && _pinnedBar->msg->getMedia()->hasReplyPreview()) {
			ImagePtr replyPreview = _pinnedBar->msg->getMedia()->replyPreview();
			if (!replyPreview->isNull()) {
				QRect to(left, top, st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
			}
			left += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::historyReplyNameFg);
		p.setFont(st::msgServiceNameFont);
		p.drawText(left, top + st::msgServiceNameFont->ascent, lang(lng_pinned_message));

		p.setPen(((_pinnedBar->msg->toHistoryMessage() && _pinnedBar->msg->toHistoryMessage()->emptyText()) || _pinnedBar->msg->serviceMsg()) ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
		_pinnedBar->text.drawElided(p, left, top + st::msgServiceNameFont->height, width() - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right());
	} else {
		p.setFont(st::msgDateFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(left, top + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), width() - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right()));
	}
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	if (!App::main() || (App::wnd() && App::wnd()->contentOverlapped(this, e))) {
		return;
	}
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}

	auto ms = getms();
	_historyDownShown.step(ms);
	auto progress = _a_show.current(ms, 1.);
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

	QRect fill(0, 0, width(), App::main()->height());
	auto fromy = App::main()->backgroundFromY();
	auto x = 0, y = 0;
	QPixmap cached = App::main()->cachedBackground(fill, x, y);
	if (cached.isNull()) {
		if (Window::Theme::Background()->tile()) {
			auto &pix = Window::Theme::Background()->pixmapForTiled();
			auto left = r.left();
			auto top = r.top();
			auto right = r.left() + r.width();
			auto bottom = r.top() + r.height();
			auto w = pix.width() / cRetinaFactor();
			auto h = pix.height() / cRetinaFactor();
			auto sx = qFloor(left / w);
			auto sy = qFloor((top - fromy) / h);
			auto cx = qCeil(right / w);
			auto cy = qCeil((bottom - fromy) / h);
			for (auto i = sx; i < cx; ++i) {
				for (auto j = sy; j < cy; ++j) {
					p.drawPixmap(QPointF(i * w, fromy + j * h), pix);
				}
			}
		} else {
			PainterHighQualityEnabler hq(p);

			auto &pix = Window::Theme::Background()->pixmap();
			QRect to, from;
			Window::Theme::ComputeBackgroundRects(fill, pix.size(), to, from);
			to.moveTop(to.top() + fromy);
			p.drawPixmap(to, pix, from);
		}
	} else {
		p.drawPixmap(x, fromy + y, cached);
	}

	if (_list) {
		if (!_field->isHidden() || _recording) {
			drawField(p, r);
			if (!_send->isHidden() && _recording) {
				drawRecording(p, _send->recordActiveRatio());
			}
		}
		if (_pinnedBar && !_pinnedBar->cancel->isHidden()) {
			drawPinnedBar(p);
		}
		if (_scroll->isHidden()) {
			p.setClipRect(_scroll->geometry());
			HistoryLayout::paintEmpty(p, width(), height() - _field->height() - 2 * st::historySendPadding);
		}
	} else {
		style::font font(st::msgServiceFont);
		int32 w = font->width(lang(lng_willbe_history)) + st::msgPadding.left() + st::msgPadding.right(), h = font->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + 2;
		QRect tr((width() - w) / 2, (height() - _field->height() - 2 * st::historySendPadding - h) / 2, w, h);
		HistoryLayout::ServiceMessagePainter::paintBubble(p, tr.x(), tr.y(), tr.width(), tr.height());

		p.setPen(st::msgServiceFg);
		p.setFont(font->f);
		p.drawText(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top() + 1 + font->ascent, lang(lng_willbe_history));
	}
}

QRect HistoryWidget::historyRect() const {
	return _scroll->geometry();
}

void HistoryWidget::destroyData() {
	showHistory(0, 0);
}

QPoint HistoryWidget::clampMousePosition(QPoint point) {
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

void HistoryWidget::onScrollTimer() {
	int32 d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + d);
}

void HistoryWidget::checkSelectingScroll(QPoint point) {
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

void HistoryWidget::noSelectingScroll() {
	_scrollTimer.stop();
}

bool HistoryWidget::touchScroll(const QPoint &delta) {
	int32 scTop = _scroll->scrollTop(), scMax = _scroll->scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	_scroll->scrollToY(scNew);
	return true;
}

HistoryWidget::~HistoryWidget() = default;
