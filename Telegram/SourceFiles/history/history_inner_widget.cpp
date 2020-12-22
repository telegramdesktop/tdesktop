/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_inner_widget.h"

#include <rpl/merge.h>
#include "core/file_utilities.h"
#include "core/crash_reports.h"
#include "core/click_handler_types.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_web_page.h"
#include "history/history_item_components.h"
#include "history/history_item_text.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_context_menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "ui/text/text_options.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "ui/inactive_press.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/notifications_manager.h"
#include "boxes/confirm_box.h"
#include "boxes/report_box.h"
#include "boxes/sticker_set_box.h"
#include "chat_helpers/message_field.h"
#include "history/history_widget.h"
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "layout.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "core/application.h"
#include "apiwrap.h"
#include "api/api_attached_stickers.h"
#include "api/api_toggling_media.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_document.h"
#include "data/data_channel.h"
#include "data/data_poll.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/stickers/data_stickers.h"
#include "facades.h"
#include "app.h"
#include "styles/style_chat.h"
#include "styles/style_window.h" // st::windowMinWidth

#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtCore/QMimeData>

namespace {

constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kUnloadHeavyPartsPages = 2;
constexpr auto kClearUserpicsAfter = 50;

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

HistoryInner *HistoryInner::Instance = nullptr;

class HistoryInner::BotAbout : public ClickHandlerHost {
public:
	BotAbout(not_null<HistoryInner*> parent, not_null<BotInfo*> info);

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	not_null<BotInfo*> info;
	int width = 0;
	int height = 0;
	QRect rect;

private:
	not_null<HistoryInner*>  _parent;

};

HistoryInner::BotAbout::BotAbout(
	not_null<HistoryInner*> parent,
	not_null<BotInfo*> info)
: info(info)
, _parent(parent) {
}

void HistoryInner::BotAbout::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	_parent->update(rect);
}

void HistoryInner::BotAbout::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	_parent->update(rect);
}

HistoryInner::HistoryInner(
	not_null<HistoryWidget*> historyWidget,
	not_null<Ui::ScrollArea*> scroll,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: RpWidget(nullptr)
, _widget(historyWidget)
, _scroll(scroll)
, _controller(controller)
, _peer(history->peer)
, _history(history)
, _migrated(history->migrateFrom())
, _scrollDateCheck([this] { scrollDateCheck(); })
, _scrollDateHideTimer([this] { scrollDateHideByTimer(); }) {
	Instance = this;

	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));

	_trippleClickTimer.setSingleShot(true);

	notifyIsBotChanged();

	setMouseTracking(true);
	subscribe(_controller->gifPauseLevelChanged(), [this] {
		if (!_controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any)) {
			update();
		}
	});
	subscribe(_controller->widget()->dragFinished(), [this] {
		mouseActionUpdate(QCursor::pos());
	});
	session().data().itemRemoved(
	) | rpl::start_with_next(
		[this](auto item) { itemRemoved(item); },
		lifetime());
	session().data().viewRemoved(
	) | rpl::start_with_next(
		[this](auto view) { viewRemoved(view); },
		lifetime());
	rpl::merge(
		session().data().historyUnloaded(),
		session().data().historyCleared()
	) | rpl::filter([this](not_null<const History*> history) {
		return (_history == history);
	}) | rpl::start_with_next([this] {
		mouseActionCancel();
	}, lifetime());
	session().data().viewRepaintRequest(
	) | rpl::start_with_next([this](not_null<const Element*> view) {
		repaintItem(view);
	}, lifetime());
	session().data().viewLayoutChanged(
	) | rpl::filter([](not_null<const Element*> view) {
		return (view == view->data()->mainView()) && view->isUnderCursor();
	}) | rpl::start_with_next([this](not_null<const Element*> view) {
		mouseActionUpdate();
	}, lifetime());
	session().changes().historyUpdates(
		_history,
		Data::HistoryUpdate::Flag::OutboxRead
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());
}

Main::Session &HistoryInner::session() const {
	return _controller->session();
}

void HistoryInner::messagesReceived(
		PeerData *peer,
		const QVector<MTPMessage> &messages) {
	if (_history->peer == peer) {
		_history->addOlderSlice(messages);
	} else if (_migrated && _migrated->peer == peer) {
		const auto newLoaded = _migrated
			&& _migrated->isEmpty()
			&& !_history->isEmpty();
		_migrated->addOlderSlice(messages);
		if (newLoaded) {
			_migrated->addNewerSlice(QVector<MTPMessage>());
		}
	}
}

void HistoryInner::messagesReceivedDown(PeerData *peer, const QVector<MTPMessage> &messages) {
	if (_history->peer == peer) {
		const auto oldLoaded = _migrated
			&& _history->isEmpty()
			&& !_migrated->isEmpty();
		_history->addNewerSlice(messages);
		if (oldLoaded) {
			_history->addOlderSlice(QVector<MTPMessage>());
		}
	} else if (_migrated && _migrated->peer == peer) {
		_migrated->addNewerSlice(messages);
	}
}

void HistoryInner::repaintItem(const HistoryItem *item) {
	if (!item) {
		return;
	}
	repaintItem(item->mainView());
}

void HistoryInner::repaintItem(const Element *view) {
	if (_widget->skipItemRepaint()) {
		return;
	}
	const auto top = itemTop(view);
	if (top >= 0) {
		const auto range = view->verticalRepaintRange();
		update(0, top + range.top, width(), range.height);
	}
}

template <bool TopToBottom, typename Method>
void HistoryInner::enumerateItemsInHistory(History *history, int historytop, Method method) {
	// No displayed messages in this history.
	if (historytop < 0 || history->isEmpty()) {
		return;
	}
	if (_visibleAreaBottom <= historytop || historytop + history->height() <= _visibleAreaTop) {
		return;
	}

	auto searchEdge = TopToBottom ? _visibleAreaTop : _visibleAreaBottom;

	// Binary search for blockIndex of the first block that is not completely below the visible area.
	auto blockIndex = BinarySearchBlocksOrItems<TopToBottom>(history->blocks, searchEdge - historytop);

	// Binary search for itemIndex of the first item that is not completely below the visible area.
	auto block = history->blocks[blockIndex].get();
	auto blocktop = historytop + block->y();
	auto blockbottom = blocktop + block->height();
	auto itemIndex = BinarySearchBlocksOrItems<TopToBottom>(block->messages, searchEdge - blocktop);

	while (true) {
		while (true) {
			auto view = block->messages[itemIndex].get();
			auto itemtop = blocktop + view->y();
			auto itembottom = itemtop + view->height();

			// Binary search should've skipped all the items that are above / below the visible area.
			if (TopToBottom) {
				Assert(itembottom > _visibleAreaTop);
			} else {
				Assert(itemtop < _visibleAreaBottom);
			}

			if (!method(view, itemtop, itembottom)) {
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
				if (++itemIndex >= block->messages.size()) {
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
		block = history->blocks[blockIndex].get();
		blocktop = historytop + block->y();
		blockbottom = blocktop + block->height();
		if (TopToBottom) {
			itemIndex = 0;
		} else {
			itemIndex = block->messages.size() - 1;
		}
	}
}

bool HistoryInner::canHaveFromUserpics() const {
	if (_peer->isUser()
		&& !_peer->isSelf()
		&& !_peer->isRepliesChat()
		&& !Core::App().settings().chatWide()) {
		return false;
	} else if (_peer->isChannel() && !_peer->isMegagroup()) {
		return false;
	}
	return true;
}

template <typename Method>
void HistoryInner::enumerateUserpics(Method method) {
	if (!canHaveFromUserpics()) {
		return;
	}

	// Find and remember the top of an attached messages pack
	// -1 means we didn't find an attached to next message yet.
	int lowestAttachedItemTop = -1;

	auto userpicCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		// Skip all service messages.
		const auto item = view->data();
		if (view->isHidden() || !item->toHistoryMessage()) return true;

		if (lowestAttachedItemTop < 0 && view->isAttachedToNext()) {
			lowestAttachedItemTop = itemtop + view->marginTop();
		}

		// Call method on a userpic for all messages that have it and for those who are not showing it
		// because of their attachment to the next message if they are bottom-most visible.
		if (view->displayFromPhoto() || (view->hasFromPhoto() && itembottom >= _visibleAreaBottom)) {
			if (lowestAttachedItemTop < 0) {
				lowestAttachedItemTop = itemtop + view->marginTop();
			}
			// Attach userpic to the bottom of the visible area with the same margin as the last message.
			auto userpicMinBottomSkip = st::historyPaddingBottom + st::msgMargin.bottom();
			auto userpicBottom = qMin(itembottom - view->marginBottom(), _visibleAreaBottom - userpicMinBottomSkip);

			// Do not let the userpic go above the attached messages pack top line.
			userpicBottom = qMax(userpicBottom, lowestAttachedItemTop + st::msgPhotoSize);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, userpicBottom - st::msgPhotoSize)) {
				return false;
			}
		}

		// Forget the found top of the pack, search for the next one from scratch.
		if (!view->isAttachedToNext()) {
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

	auto dateCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		const auto item = view->data();
		if (lowestInOneDayItemBottom < 0 && view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = itembottom - view->marginBottom();
		}

		// Call method on a date for all messages that have it and for those who are not showing it
		// because they are in a one day together with the previous message if they are top-most visible.
		if (view->displayDate() || (!item->isEmpty() && itemtop <= _visibleAreaTop)) {
			// skip the date of history migrate item if it will be in migrated
			if (itemtop < drawtop && item->history() == _history) {
				if (itemtop > _visibleAreaTop) {
					// Previous item (from the _migrated history) is drawing date now.
					return false;
				}
			}

			if (lowestInOneDayItemBottom < 0) {
				lowestInOneDayItemBottom = itembottom - view->marginBottom();
			}
			// Attach date to the top of the visible area with the same margin as it has in service message.
			int dateTop = qMax(itemtop, _visibleAreaTop) + st::msgServiceMargin.top();

			// Do not let the date go below the single-day messages pack bottom line.
			int dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			dateTop = qMin(dateTop, lowestInOneDayItemBottom - dateHeight);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, itemtop, dateTop)) {
				return false;
			}
		}

		// Forget the found bottom of the pack, search for the next one from scratch.
		if (!view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::BottomToTop>(dateCallback);
}

TextSelection HistoryInner::computeRenderSelection(
		not_null<const SelectedItems*> selected,
		not_null<Element*> view) const {
	if (view->isHiddenByGroup()) {
		return TextSelection();
	}
	const auto item = view->data();
	const auto itemSelection = [&](not_null<HistoryItem*> item) {
		auto i = selected->find(item);
		if (i != selected->end()) {
			return i->second;
		}
		return TextSelection();
	};
	const auto result = itemSelection(item);
	if (result != TextSelection() && result != FullSelection) {
		return result;
	}
	if (const auto group = session().data().groups().find(item)) {
		auto parts = TextSelection();
		auto allFullSelected = true;
		const auto count = int(group->items.size());
		for (auto i = 0; i != count; ++i) {
			const auto part = group->items[i];
			const auto selection = itemSelection(part);
			if (part == item
				&& selection != FullSelection
				&& selection != TextSelection()) {
				return selection;
			} else if (selection == FullSelection) {
				parts = AddGroupItemSelection(parts, i);
			} else {
				allFullSelected = false;
			}
		}
		return allFullSelected ? FullSelection : parts;
	}
	return itemSelection(item);
}

TextSelection HistoryInner::itemRenderSelection(
		not_null<Element*> view,
		int selfromy,
		int seltoy) const {
	const auto item = view->data();
	const auto y = view->block()->y() + view->y();
	if (y >= selfromy && y < seltoy) {
		if (_dragSelecting && !item->serviceMsg() && item->id > 0) {
			return FullSelection;
		}
	} else if (!_selected.empty()) {
		return computeRenderSelection(&_selected, view);
	}
	return TextSelection();
}

void HistoryInner::paintEmpty(Painter &p, int width, int height) {
	if (!_emptyPainter) {
		_emptyPainter = std::make_unique<HistoryView::EmptyPainter>(
			_history);
	}
	_emptyPainter->paint(p, width, height);
}

void HistoryInner::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	if (hasPendingResizedItems()) {
		return;
	}

	const auto guard = gsl::finally([&] {
		_userpicsCache.clear();
	});

	Painter p(this);
	auto clip = e->rect();
	auto ms = crl::now();

	const auto historyDisplayedEmpty = _history->isDisplayedEmpty()
		&& (!_migrated || _migrated->isDisplayedEmpty());
	bool noHistoryDisplayed = _firstLoading || historyDisplayedEmpty;
	if (!_firstLoading && _botAbout && !_botAbout->info->text.isEmpty() && _botAbout->height > 0) {
		if (clip.y() < _botAbout->rect.y() + _botAbout->rect.height() && clip.y() + clip.height() > _botAbout->rect.y()) {
			p.setTextPalette(st::inTextPalette);
			Ui::FillRoundRect(p, _botAbout->rect, st::msgInBg, Ui::MessageInCorners, &st::msgInShadow);

			auto top = _botAbout->rect.top() + st::msgPadding.top();
			if (!_history->peer->isRepliesChat()) {
				p.setFont(st::msgNameFont);
				p.setPen(st::dialogsNameFg);
				p.drawText(_botAbout->rect.left() + st::msgPadding.left(), top + st::msgNameFont->ascent, tr::lng_bot_description(tr::now));
				top += +st::msgNameFont->height + st::botDescSkip;
			}

			p.setPen(st::historyTextInFg);
			_botAbout->info->text.draw(p, _botAbout->rect.left() + st::msgPadding.left(), top, _botAbout->width);

			p.restoreTextPalette();
		}
	} else if (historyDisplayedEmpty) {
		paintEmpty(p, width(), height());
	} else {
		_emptyPainter = nullptr;
	}
	if (!noHistoryDisplayed) {
		auto readMentions = base::flat_set<not_null<HistoryItem*>>();

		adjustCurrent(clip.top());

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
			auto block = _migrated->blocks[iBlock].get();
			auto iItem = (_curHistory == _migrated ? _curItem : (block->messages.size() - 1));
			auto view = block->messages[iItem].get();
			auto item = view->data();

			auto y = mtop + block->y() + view->y();
			p.save();
			p.translate(0, y);
			if (clip.y() < y + view->height()) while (y < drawToY) {
				const auto selection = itemRenderSelection(
					view,
					selfromy - mtop,
					seltoy - mtop);
				view->draw(p, clip.translated(0, -y), selection, ms);

				if (item->hasViews()) {
					_controller->content()->scheduleViewIncrement(item);
				}
				if (item->isUnreadMention() && !item->isUnreadMedia()) {
					readMentions.insert(item);
					_widget->enqueueMessageHighlight(view);
				}

				int32 h = view->height();
				p.translate(0, h);
				y += h;

				++iItem;
				if (iItem == block->messages.size()) {
					iItem = 0;
					++iBlock;
					if (iBlock == _migrated->blocks.size()) {
						break;
					}
					block = _migrated->blocks[iBlock].get();
				}
				view = block->messages[iItem].get();
				item = view->data();
			}
			p.restore();
		}
		if (htop >= 0) {
			auto iBlock = (_curHistory == _history ? _curBlock : 0);
			auto block = _history->blocks[iBlock].get();
			auto iItem = (_curHistory == _history ? _curItem : 0);
			auto view = block->messages[iItem].get();
			auto item = view->data();
			auto readTill = (HistoryItem*)nullptr;
			auto hclip = clip.intersected(QRect(0, hdrawtop, width(), clip.top() + clip.height()));
			auto y = htop + block->y() + view->y();
			p.save();
			p.translate(0, y);
			while (y < drawToY) {
				const auto h = view->height();
				if (hclip.y() < y + h && hdrawtop < y + h) {
					const auto selection = itemRenderSelection(
						view,
						selfromy - htop,
						seltoy - htop);
					view->draw(p, hclip.translated(0, -y), selection, ms);

					const auto middle = y + h / 2;
					const auto bottom = y + h;
					if (_visibleAreaBottom >= bottom) {
						const auto item = view->data();
						if (!item->out() && item->unread()) {
							readTill = item;
						}
					}
					if (_visibleAreaBottom >= middle
						&& _visibleAreaTop <= middle) {
						if (item->hasViews()) {
							_controller->content()->scheduleViewIncrement(item);
						}
						if (item->isUnreadMention() && !item->isUnreadMedia()) {
							readMentions.insert(item);
							_widget->enqueueMessageHighlight(view);
						}
					}
				}
				p.translate(0, h);
				y += h;

				++iItem;
				if (iItem == block->messages.size()) {
					iItem = 0;
					++iBlock;
					if (iBlock == _history->blocks.size()) {
						break;
					}
					block = _history->blocks[iBlock].get();
				}
				view = block->messages[iItem].get();
				item = view->data();
			}
			p.restore();

			if (readTill && _widget->doWeReadServerHistory()) {
				session().data().histories().readInboxTill(readTill);
			}
		}

		if (!readMentions.empty() && _widget->doWeReadMentions()) {
			session().api().markMediaRead(readMentions);
		}

		if (mtop >= 0 || htop >= 0) {
			enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
				// stop the enumeration if the userpic is below the painted rect
				if (userpicTop >= clip.top() + clip.height()) {
					return false;
				}

				// paint the userpic if it intersects the painted rect
				if (userpicTop + st::msgPhotoSize > clip.top()) {
					const auto message = view->data()->toHistoryMessage();
					if (const auto from = message->displayFrom()) {
						from->paintUserpicLeft(
							p,
							_userpics[from],
							st::historyPhotoLeft,
							userpicTop,
							width(),
							st::msgPhotoSize);
					} else if (const auto info = message->hiddenForwardedInfo()) {
						info->userpic.paint(
							p,
							st::historyPhotoLeft,
							userpicTop,
							width(),
							st::msgPhotoSize);
					} else {
						Unexpected("Corrupt forwarded information in message.");
					}
				}
				return true;
			});

			int dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			//QDate lastDate;
			//if (!_history->isEmpty()) {
			//	lastDate = _history->blocks.back()->messages.back()->data()->date.date();
			//}

			//// if item top is before this value always show date as a floating date
			//int showFloatingBefore = height() - 2 * (_visibleAreaBottom - _visibleAreaTop) - dateHeight;


			auto scrollDateOpacity = _scrollDateOpacity.value(_scrollDateShown ? 1. : 0.);
			enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
				// stop the enumeration if the date is above the painted rect
				if (dateTop + dateHeight <= clip.top()) {
					return false;
				}

				const auto displayDate = view->displayDate();
				auto dateInPlace = displayDate;
				if (dateInPlace) {
					const auto correctDateTop = itemtop + st::msgServiceMargin.top();
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
						const auto dateY = false // noFloatingDate
							? itemtop
							: (dateTop - st::msgServiceMargin.top());
						if (const auto date = view->Get<HistoryView::DateBadge>()) {
							date->paint(p, dateY, _contentWidth);
						} else {
							HistoryView::ServiceMessagePainter::paintDate(
								p,
								view->dateTime(),
								dateY,
								_contentWidth);
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
	auto nowTime = crl::now();
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
	const auto nowTime = crl::now();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > Ui::kFingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > Ui::kFingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == Ui::TouchScrollState::Auto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(snap((oldSpeedY + (newSpeedY / 4)), -Ui::kMaxScrollAccelerated, +Ui::kMaxScrollAccelerated));
					_touchSpeed.setX(snap((oldSpeedX + (newSpeedX / 4)), -Ui::kMaxScrollAccelerated, +Ui::kMaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				// we average the speed to avoid strange effects with the last delta
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(snap((_touchSpeed.x() / 4) + (newSpeedX * 3 / 4), -Ui::kMaxScrollFlick, +Ui::kMaxScrollFlick));
					_touchSpeed.setY(snap((_touchSpeed.y() / 4) + (newSpeedY * 3 / 4), -Ui::kMaxScrollFlick, +Ui::kMaxScrollFlick));
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
	case QEvent::TouchBegin: {
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
			_touchAccelerationTime = crl::now();
			touchUpdateSpeed();
			_touchStart = _touchPos;
		} else {
			_touchScroll = false;
			_touchSelectTimer.start(QApplication::startDragTime());
		}
		_touchSelect = false;
		_touchStart = _touchPrevPos = _touchPos;
	} break;

	case QEvent::TouchUpdate: {
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
				_touchAccelerationTime = crl::now();
				if (_touchSpeed.isNull()) {
					_touchScrollState = Ui::TouchScrollState::Manual;
				}
			}
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchInProgress) return;
		_touchInProgress = false;
		auto weak = Ui::MakeWeak(this);
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
				_touchTime = crl::now();
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
		if (weak) {
			_touchSelectTimer.stop();
			_touchSelect = false;
		}
	} break;
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
	mouseActionUpdate();
}

void HistoryInner::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	_widget->touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

QPoint HistoryInner::mapPointToItem(QPoint p, const Element *view) const {
	if (view) {
		const auto top = itemTop(view);
		p.setY(p.y() - top);
		return p;
	}
	return QPoint();
}

QPoint HistoryInner::mapPointToItem(
		QPoint p,
		const HistoryItem *item) const {
	return item ? mapPointToItem(p, item->mainView()) : QPoint();
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

	const auto mouseActionView = App::mousedItem();
	_mouseAction = MouseAction::None;
	_mouseActionItem = mouseActionView
		? mouseActionView->data().get()
		: nullptr;
	_dragStartPosition = mapPointToItem(mapFromGlobal(screenPos), mouseActionView);
	_pressWasInactive = Ui::WasInactivePress(_controller->widget());
	if (_pressWasInactive) {
		Ui::MarkInactivePress(_controller->widget(), false);
	}

	if (ClickHandler::getPressed()) {
		_mouseAction = MouseAction::PrepareDrag;
	} else if (!_selected.empty()) {
		if (_selected.cbegin()->second == FullSelection) {
			if (_dragStateItem
				&& _selected.find(_dragStateItem) != _selected.cend()
				&& App::hoveredItem()) {
				_mouseAction = MouseAction::PrepareDrag; // start items drag
			} else if (!_pressWasInactive) {
				_mouseAction = MouseAction::PrepareSelect; // start items select
			}
		}
	}
	if (_mouseAction == MouseAction::None && mouseActionView) {
		TextState dragState;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = mouseActionView->textState(_dragStartPosition, request);
			if (dragState.cursor == CursorState::Text) {
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
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = mouseActionView->textState(_dragStartPosition, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (App::pressedItem()) {
				_mouseTextSymbol = dragState.symbol;
				bool uponSelected = (dragState.cursor == CursorState::Text);
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
					const auto media = App::pressedItem()->media();
					if ((media && media->dragItem())
						|| _mouseCursorState == CursorState::Date) {
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
	_dragStateItem = nullptr;
	_mouseAction = MouseAction::None;
	_dragStartPosition = QPoint(0, 0);
	_dragSelFrom = _dragSelTo = nullptr;
	_wasSelectedText = false;
	_widget->noSelectingScroll();
}

std::unique_ptr<QMimeData> HistoryInner::prepareDrag() {
	if (_mouseAction != MouseAction::Dragging) {
		return nullptr;
	}

	const auto pressedHandler = ClickHandler::getPressed();
	if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.get())) {
		return nullptr;
	}

	const auto mouseActionView = _mouseActionItem
		? _mouseActionItem->mainView()
		: nullptr;
	bool uponSelected = false;
	if (mouseActionView) {
		if (!_selected.empty() && _selected.cbegin()->second == FullSelection) {
			uponSelected = _dragStateItem
				&& (_selected.find(_dragStateItem) != _selected.cend());
		} else {
			StateRequest request;
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			auto dragState = mouseActionView->textState(_dragStartPosition, request);
			uponSelected = (dragState.cursor == CursorState::Text);
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

	auto urls = QList<QUrl>();
	const auto selectedText = [&] {
		if (uponSelected) {
			return getSelectedText();
		} else if (pressedHandler) {
			//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
			//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
			//}
			return TextForMimeData::Simple(pressedHandler->dragText());
		}
		return TextForMimeData();
	}();
	if (auto mimeData = TextUtilities::MimeDataFromText(selectedText)) {
		updateDragSelection(nullptr, nullptr, false);
		_widget->noSelectingScroll();

		if (!urls.isEmpty()) mimeData->setUrls(urls);
		if (uponSelected && !Adaptive::OneColumn()) {
			auto selectedState = getSelectionState();
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				session().data().setMimeForwardIds(getSelectedItems());
				mimeData->setData(qsl("application/x-td-forward"), "1");
			}
		}
		return mimeData;
	} else if (_dragStateItem) {
		const auto view = _dragStateItem->mainView();
		if (!view) {
			return nullptr;
		}
		auto forwardIds = MessageIdsList();
		if (_mouseCursorState == CursorState::Date) {
			forwardIds = session().data().itemOrItsGroup(_dragStateItem);
		} else if (view->isHiddenByGroup() && pressedHandler) {
			forwardIds = MessageIdsList(1, _dragStateItem->fullId());
		} else if (const auto media = view->media()) {
			if (media->dragItemByHandler(pressedHandler)
				|| media->dragItem()) {
				forwardIds = MessageIdsList(1, _dragStateItem->fullId());
			}
		}
		if (forwardIds.empty()) {
			return nullptr;
		}
		session().data().setMimeForwardIds(std::move(forwardIds));
		auto result = std::make_unique<QMimeData>();
		result->setData(qsl("application/x-td-forward"), "1");
		if (const auto media = view->media()) {
			if (const auto document = media->getDocument()) {
				const auto filepath = document->filepath(true);
				if (!filepath.isEmpty()) {
					QList<QUrl> urls;
					urls.push_back(QUrl::fromLocalFile(filepath));
					result->setUrls(urls);
				}
			}
		}
		return result;
	}
	return nullptr;
}

void HistoryInner::performDrag() {
	if (auto mimeData = prepareDrag()) {
		// This call enters event loop and can destroy any QObject.
		_controller->widget()->launchDrag(std::move(mimeData));
	}
}

void HistoryInner::itemRemoved(not_null<const HistoryItem*> item) {
	if (_history != item->history() && _migrated != item->history()) {
		return;
	}

	_animatedStickersPlayed.remove(item);

	auto i = _selected.find(item);
	if (i != _selected.cend()) {
		_selected.erase(i);
		_widget->updateTopBarSelection();
	}

	if (_mouseActionItem == item) {
		mouseActionCancel();
	}
	if (_dragStateItem == item) {
		_dragStateItem = nullptr;
	}

	if ((_dragSelFrom && _dragSelFrom->data() == item)
		|| (_dragSelTo && _dragSelTo->data() == item)) {
		_dragSelFrom = nullptr;
		_dragSelTo = nullptr;
		update();
	}
	if (_scrollDateLastItem && _scrollDateLastItem->data() == item) {
		_scrollDateLastItem = nullptr;
	}
	mouseActionUpdate();
}

void HistoryInner::viewRemoved(not_null<const Element*> view) {
	const auto refresh = [&](auto &saved) {
		if (saved == view) {
			const auto now = view->data()->mainView();
			saved = (now && now != view) ? now : nullptr;
		}
	};
	refresh(_dragSelFrom);
	refresh(_dragSelTo);
	refresh(_scrollDateLastItem);
}

void HistoryInner::mouseActionFinish(
		const QPoint &screenPos,
		Qt::MouseButton button) {
	mouseActionUpdate(screenPos);

	auto activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging) {
		activated = nullptr;
	} else if (_mouseActionItem) {
		// if we are in selecting items mode perhaps we want to
		// toggle selection instead of activating the pressed link
		if (_mouseAction == MouseAction::PrepareDrag && !_pressWasInactive && !_selected.empty() && _selected.cbegin()->second == FullSelection && button != Qt::RightButton) {
			if (const auto view = _mouseActionItem->mainView()) {
				if (const auto media = view->media()) {
					if (media->toggleSelectionByHandlerClick(activated)) {
						activated = nullptr;
					}
				}
			}
		}
	}
	const auto pressedItemView = App::pressedItem();
	if (pressedItemView) {
		repaintItem(pressedItemView);
		App::pressedItem(nullptr);
	}

	_wasSelectedText = false;

	if (activated) {
		mouseActionCancel();
		const auto pressedItemId = pressedItemView
			? pressedItemView->data()->fullId()
			: FullMsgId();
		ActivateClickHandler(window(), activated, {
			button,
			QVariant::fromValue(ClickHandlerContext{
				.itemId = pressedItemId,
				.elementDelegate = [weak = Ui::MakeWeak(this)] {
					return weak
						? HistoryInner::ElementDelegate().get()
						: nullptr;
				},
			})
		});
		return;
	}
	if ((_mouseAction == MouseAction::PrepareSelect)
		&& !_pressWasInactive
		&& !_selected.empty()
		&& (_selected.cbegin()->second == FullSelection)) {
		changeSelectionAsGroup(
			&_selected,
			_mouseActionItem,
			SelectAction::Invert);
		repaintItem(_mouseActionItem);
	} else if ((_mouseAction == MouseAction::PrepareDrag)
		&& !_pressWasInactive
		&& _dragStateItem
		&& (button != Qt::RightButton)) {
		auto i = _selected.find(_dragStateItem);
		if (i != _selected.cend() && i->second == FullSelection) {
			_selected.erase(i);
			repaintItem(_mouseActionItem);
		} else if ((i == _selected.cend())
			&& !_dragStateItem->serviceMsg()
			&& (_dragStateItem->id > 0)
			&& !_selected.empty()
			&& _selected.cbegin()->second == FullSelection) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.emplace(_dragStateItem, FullSelection);
				repaintItem(_mouseActionItem);
			}
		} else {
			_selected.clear();
			update();
		}
	} else if (_mouseAction == MouseAction::Selecting) {
		if (_dragSelFrom && _dragSelTo) {
			applyDragSelection();
			_dragSelFrom = _dragSelTo = nullptr;
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

	if (QGuiApplication::clipboard()->supportsSelection()
		&& !_selected.empty()
		&& _selected.cbegin()->second != FullSelection) {
		const auto [item, selection] = *_selected.cbegin();
		if (const auto view = item->mainView()) {
			TextUtilities::SetClipboardText(
				view->selectedText(selection),
				QClipboard::Selection);
		}
	}
}

void HistoryInner::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void HistoryInner::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());

	const auto mouseActionView = _mouseActionItem
		? _mouseActionItem->mainView()
		: nullptr;
	if (_mouseSelectType == TextSelectType::Letters
		&& mouseActionView
		&& ((_mouseAction == MouseAction::Selecting
			&& !_selected.empty()
			&& _selected.cbegin()->second != FullSelection)
			|| (_mouseAction == MouseAction::None
				&& (_selected.empty()
					|| _selected.cbegin()->second != FullSelection)))) {
		StateRequest request;
		request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
		auto dragState = mouseActionView->textState(_dragStartPosition, request);
		if (dragState.cursor == CursorState::Text) {
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
	if (!ClickHandler::getActive()
		&& !ClickHandler::getPressed()
		&& (_mouseCursorState == CursorState::None
			|| _mouseCursorState == CursorState::Date)
		&& !inSelectionMode()
		&& !_emptyPainter) {
		if (const auto item = _mouseActionItem) {
			mouseActionCancel();
			_widget->replyToMessage(item);
		}
	}
}

void HistoryInner::contextMenuEvent(QContextMenuEvent *e) {
	showContextMenu(e);
}

void HistoryInner::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	auto selectedState = getSelectionState();
	auto canSendMessages = _peer->canWrite();

	// -2 - has full selected items, but not over, -1 - has selection, but no over, 0 - no selection, 1 - over text, 2 - over full selected items
	auto isUponSelected = 0;
	auto hasSelected = 0;
	if (!_selected.empty()) {
		isUponSelected = -1;
		if (_selected.cbegin()->second == FullSelection) {
			hasSelected = 2;
			if (_dragStateItem && _selected.find(_dragStateItem) != _selected.cend()) {
				isUponSelected = 2;
			} else {
				isUponSelected = -2;
			}
		} else {
			uint16 selFrom = _selected.cbegin()->second.from, selTo = _selected.cbegin()->second.to;
			hasSelected = (selTo > selFrom) ? 1 : 0;
			if (App::mousedItem() && App::mousedItem() == App::hoveredItem()) {
				auto mousePos = mapPointToItem(mapFromGlobal(_mousePosition), App::mousedItem());
				StateRequest request;
				request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
				auto dragState = App::mousedItem()->textState(mousePos, request);
				if (dragState.cursor == CursorState::Text
					&& dragState.symbol >= selFrom
					&& dragState.symbol < selTo) {
					isUponSelected = 1;
				}
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_menu = base::make_unique_q<Ui::PopupMenu>(this);
	const auto session = &this->session();
	const auto controller = _controller;
	const auto groupLeaderOrSelf = [](HistoryItem *item) -> HistoryItem* {
		if (!item) {
			return nullptr;
		} else if (const auto group = item->history()->owner().groups().find(item)) {
			return group->items.front();
		}
		return item;
	};
	const auto addItemActions = [&](
			HistoryItem *item,
			HistoryItem *albumPartItem) {
		if (!item
			|| !IsServerMsgId(item->id)
			|| isUponSelected == 2
			|| isUponSelected == -2) {
			return;
		}
		const auto itemId = item->fullId();
		if (canSendMessages) {
			_menu->addAction(tr::lng_context_reply_msg(tr::now), [=] {
				_widget->replyToMessage(itemId);
			});
		}
		const auto repliesCount = item->repliesCount();
		const auto withReplies = IsServerMsgId(item->id)
			&& (repliesCount > 0);
		if (withReplies && item->history()->peer->isMegagroup()) {
			const auto rootId = repliesCount ? item->id : item->replyToTop();
			const auto phrase = (repliesCount > 0)
				? tr::lng_replies_view(
					tr::now,
					lt_count,
					repliesCount)
				: tr::lng_replies_view_thread(tr::now);
			_menu->addAction(phrase, [=] {
				controller->showRepliesForMessage(_history, rootId);
			});
		}
		const auto t = base::unixtime::now();
		const auto editItem = (albumPartItem && albumPartItem->allowsEdit(t))
			? albumPartItem
			: item->allowsEdit(t)
			? item
			: nullptr;
		if (editItem) {
			const auto editItemId = editItem->fullId();
			_menu->addAction(tr::lng_context_edit_msg(tr::now), [=] {
				_widget->editMessage(editItemId);
			});
		}
		const auto pinItem = (item->canPin() && item->isPinned())
			? item
			: groupLeaderOrSelf(item);
		if (pinItem->canPin()) {
			const auto isPinned = pinItem->isPinned();
			const auto pinItemId = pinItem->fullId();
			const auto controller = _controller;
			_menu->addAction(isPinned ? tr::lng_context_unpin_msg(tr::now) : tr::lng_context_pin_msg(tr::now), crl::guard(controller, [=] {
				Window::ToggleMessagePinned(controller, pinItemId, !isPinned);
			}));
		}
	};
	const auto addPhotoActions = [&](not_null<PhotoData*> photo) {
		_menu->addAction(tr::lng_context_save_image(tr::now), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
			savePhotoToFile(photo);
		}));
		_menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
			copyContextImage(photo);
		});
		if (photo->hasAttachedStickers()) {
			_menu->addAction(tr::lng_context_attached_stickers(tr::now), [=] {
				session->api().attachedStickers().requestAttachedStickerSets(
					controller,
					photo);
			});
		}
	};
	const auto addDocumentActions = [&](not_null<DocumentData*> document) {
		if (document->loading()) {
			_menu->addAction(tr::lng_context_cancel_download(tr::now), [=] {
				cancelContextDownload(document);
			});
			return;
		}
		const auto item = _dragStateItem;
		const auto itemId = item ? item->fullId() : FullMsgId();
		const auto lnkIsVideo = document->isVideoFile();
		const auto lnkIsVoice = document->isVoiceMessage();
		const auto lnkIsAudio = document->isAudioFile();
		if (document->isGifv()) {
			const auto notAutoplayedGif = [&] {
				return item
					&& document->isGifv()
					&& !Data::AutoDownload::ShouldAutoPlay(
						session->settings().autoDownload(),
						item->history()->peer,
						document);
			}();
			if (notAutoplayedGif) {
				_menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
					openContextGif(itemId);
				});
			}
			_menu->addAction(tr::lng_context_save_gif(tr::now), [=] {
				saveContextGif(itemId);
			});
		}
		if (!document->filepath(true).isEmpty()) {
			_menu->addAction(Platform::IsMac() ? tr::lng_context_show_in_finder(tr::now) : tr::lng_context_show_in_folder(tr::now), [=] {
				showContextInFolder(document);
			});
		}
		_menu->addAction(lnkIsVideo ? tr::lng_context_save_video(tr::now) : (lnkIsVoice ? tr::lng_context_save_audio(tr::now) : (lnkIsAudio ? tr::lng_context_save_audio_file(tr::now) : tr::lng_context_save_file(tr::now))), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
			saveDocumentToFile(itemId, document);
		}));
		if (document->hasAttachedStickers()) {
			_menu->addAction(tr::lng_context_attached_stickers(tr::now), [=] {
				session->api().attachedStickers().requestAttachedStickerSets(
					controller,
					document);
			});
		}
	};
	const auto link = ClickHandler::getActive();
	auto lnkPhoto = dynamic_cast<PhotoClickHandler*>(link.get());
	auto lnkDocument = dynamic_cast<DocumentClickHandler*>(link.get());
	if (lnkPhoto || lnkDocument) {
		const auto item = _dragStateItem;
		const auto itemId = item ? item->fullId() : FullMsgId();
		if (isUponSelected > 0) {
			_menu->addAction(
				(isUponSelected > 1
					? tr::lng_context_copy_selected_items(tr::now)
					: tr::lng_context_copy_selected(tr::now)),
				[=] { copySelectedText(); });
		}
		addItemActions(item, item);
		if (lnkPhoto) {
			addPhotoActions(lnkPhoto->photo());
		} else {
			addDocumentActions(lnkDocument->document());
		}
		if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(item->history()->peer->isMegagroup() ? tr::lng_context_copy_message_link(tr::now) : tr::lng_context_copy_post_link(tr::now), [=] {
				HistoryView::CopyPostLink(session, itemId, HistoryView::Context::History);
			});
		}
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.canForwardCount == selectedState.count) {
				_menu->addAction(tr::lng_context_forward_selected(tr::now), [=] {
					_widget->forwardSelected();
				});
			}
			if (selectedState.count > 0 && selectedState.canDeleteCount == selectedState.count) {
				_menu->addAction(tr::lng_context_delete_selected(tr::now), [=] {
					_widget->confirmDeleteSelected();
				});
			}
			_menu->addAction(tr::lng_context_clear_selection(tr::now), [=] {
				_widget->clearSelected();
			});
		} else if (item) {
			const auto itemId = item->fullId();
			const auto blockSender = item->history()->peer->isRepliesChat();
			if (isUponSelected != -2) {
				if (item->allowsForward()) {
					_menu->addAction(tr::lng_context_forward_msg(tr::now), [=] {
						forwardItem(itemId);
					});
				}
				if (item->canDelete()) {
					_menu->addAction(tr::lng_context_delete_msg(tr::now), [=] {
						deleteItem(itemId);
					});
				}
				if (!blockSender && item->suggestReport()) {
					_menu->addAction(tr::lng_context_report_msg(tr::now), [=] {
						reportItem(itemId);
					});
				}
			}
			if (IsServerMsgId(item->id) && !item->serviceMsg()) {
				_menu->addAction(tr::lng_context_select_msg(tr::now), [=] {
					if (const auto item = session->data().message(itemId)) {
						if (const auto view = item->mainView()) {
							changeSelection(&_selected, item, SelectAction::Select);
							repaintItem(item);
							_widget->updateTopBarSelection();
						}
					}
				});
			}
			if (isUponSelected != -2 && blockSender) {
				_menu->addAction(tr::lng_profile_block_user(tr::now), [=] {
					blockSenderItem(itemId);
				});
			}
		}
	} else { // maybe cursor on some text history item?
		const auto albumPartItem = _dragStateItem;
		const auto item = [&] {
			const auto result = App::hoveredItem()
				? App::hoveredItem()->data().get()
				: App::hoveredLinkItem()
				? App::hoveredLinkItem()->data().get()
				: nullptr;
			return result ? groupLeaderOrSelf(result) : nullptr;
		}();
		const auto itemId = item ? item->fullId() : FullMsgId();
		const auto canDelete = item
			&& item->canDelete()
			&& (item->id > 0 || !item->serviceMsg());
		const auto canForward = item && item->allowsForward();
		const auto canReport = item && item->suggestReport();
		const auto canBlockSender = item && item->history()->peer->isRepliesChat();
		const auto view = item ? item->mainView() : nullptr;

		const auto msg = dynamic_cast<HistoryMessage*>(item);
		if (isUponSelected > 0) {
			_menu->addAction(
				((isUponSelected > 1)
					? tr::lng_context_copy_selected_items(tr::now)
					: tr::lng_context_copy_selected(tr::now)),
				[=] { copySelectedText(); });
			addItemActions(item, item);
		} else {
			addItemActions(item, albumPartItem);
			if (item && !isUponSelected) {
				const auto media = (view ? view->media() : nullptr);
				const auto mediaHasTextForCopy = media && media->hasTextForCopy();
				if (const auto document = media ? media->getDocument() : nullptr) {
					if (!item->isIsolatedEmoji() && document->sticker()) {
						if (document->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
							_menu->addAction(document->isStickerSetInstalled() ? tr::lng_context_pack_info(tr::now) : tr::lng_context_pack_add(tr::now), [=] {
								showStickerPackInfo(document);
							});
							_menu->addAction(session->data().stickers().isFaved(document) ? tr::lng_faved_stickers_remove(tr::now) : tr::lng_faved_stickers_add(tr::now), [=] {
								Api::ToggleFavedSticker(document, itemId);
							});
						}
						_menu->addAction(tr::lng_context_save_image(tr::now), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
							saveDocumentToFile(itemId, document);
						}));
					}
				}
				if (const auto media = item->media()) {
					if (const auto poll = media->poll()) {
						if (!poll->closed()) {
							if (poll->voted() && !poll->quiz()) {
								_menu->addAction(tr::lng_polls_retract(tr::now), [=] {
									session->api().sendPollVotes(itemId, {});
								});
							}
							if (item->canStopPoll()) {
								_menu->addAction(tr::lng_polls_stop(tr::now), [=] {
									HistoryView::StopPoll(session, itemId);
								});
							}
						}
					} else if (const auto contact = media->sharedContact()) {
						const auto phone = contact->phoneNumber;
						_menu->addAction(tr::lng_profile_copy_phone(tr::now), [=] {
							QGuiApplication::clipboard()->setText(phone);
						});
					}
				}
				if (msg && view && !link && (view->hasVisibleText() || mediaHasTextForCopy)) {
					_menu->addAction(tr::lng_context_copy_text(tr::now), [=] {
						copyContextText(itemId);
					});
				}
			}
		}

		const auto actionText = link
			? link->copyToClipboardContextItemText()
			: QString();
		if (!actionText.isEmpty()) {
			_menu->addAction(
				actionText,
				[text = link->copyToClipboardText()] {
					QGuiApplication::clipboard()->setText(text);
				});
		} else if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(item->history()->peer->isMegagroup() ? tr::lng_context_copy_message_link(tr::now) : tr::lng_context_copy_post_link(tr::now), [=] {
				HistoryView::CopyPostLink(session, itemId, HistoryView::Context::History);
			});
		}
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				_menu->addAction(tr::lng_context_forward_selected(tr::now), [=] {
					_widget->forwardSelected();
				});
			}
			if (selectedState.count > 0 && selectedState.count == selectedState.canDeleteCount) {
				_menu->addAction(tr::lng_context_delete_selected(tr::now), [=] {
					_widget->confirmDeleteSelected();
				});
			}
			_menu->addAction(tr::lng_context_clear_selection(tr::now), [=] {
				_widget->clearSelected();
			});
		} else if (item && ((isUponSelected != -2 && (canForward || canDelete)) || item->id > 0)) {
			if (isUponSelected != -2) {
				if (canForward) {
					_menu->addAction(tr::lng_context_forward_msg(tr::now), [=] {
						forwardAsGroup(itemId);
					});
				}
				if (canDelete) {
					_menu->addAction((msg && msg->uploading()) ? tr::lng_context_cancel_upload(tr::now) : tr::lng_context_delete_msg(tr::now), [=] {
						deleteAsGroup(itemId);
					});
				}
				if (!canBlockSender && canReport) {
					_menu->addAction(tr::lng_context_report_msg(tr::now), [=] {
						reportAsGroup(itemId);
					});
				}
			}
			if (item->id > 0 && !item->serviceMsg()) {
				_menu->addAction(tr::lng_context_select_msg(tr::now), [=] {
					if (const auto item = session->data().message(itemId)) {
						if (const auto view = item->mainView()) {
							changeSelectionAsGroup(&_selected, item, SelectAction::Select);
							repaintItem(view);
							_widget->updateTopBarSelection();
						}
					}
				});
			}
			if (isUponSelected != -2 && canBlockSender) {
				_menu->addAction(tr::lng_profile_block_user(tr::now), [=] {
					blockSenderAsGroup(itemId);
				});
			}
		} else {
			if (App::mousedItem()
				&& IsServerMsgId(App::mousedItem()->data()->id)
				&& !App::mousedItem()->data()->serviceMsg()) {
				const auto itemId = App::mousedItem()->data()->fullId();
				_menu->addAction(tr::lng_context_select_msg(tr::now), [=] {
					if (const auto item = session->data().message(itemId)) {
						if (const auto view = item->mainView()) {
							changeSelectionAsGroup(&_selected, item, SelectAction::Select);
							repaintItem(item);
							_widget->updateTopBarSelection();
						}
					}
				});
			}
		}
	}

	if (_menu->actions().empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void HistoryInner::copySelectedText() {
	TextUtilities::SetClipboardText(getSelectedText());
}

void HistoryInner::savePhotoToFile(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	const auto image = media->image(Data::PhotoSize::Large)->original();
	auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
	FileDialog::GetWritePath(
		this,
		tr::lng_save_photo(tr::now),
		filter,
		filedialogDefaultName(
			qsl("photo"),
			qsl(".jpg")),
		crl::guard(this, [=](const QString &result) {
			if (!result.isEmpty()) {
				image.save(result, "JPG");
			}
		}));
}

void HistoryInner::copyContextImage(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	const auto image = media->image(Data::PhotoSize::Large)->original();
	QGuiApplication::clipboard()->setImage(image);
}

void HistoryInner::showStickerPackInfo(not_null<DocumentData*> document) {
	StickerSetBox::Show(_controller, document);
}

void HistoryInner::cancelContextDownload(not_null<DocumentData*> document) {
	document->cancel();
}

void HistoryInner::showContextInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void HistoryInner::saveDocumentToFile(
		FullMsgId contextId,
		not_null<DocumentData*> document) {
	DocumentSaveClickHandler::Save(
		contextId,
		document,
		DocumentSaveClickHandler::Mode::ToNewFile);
}

void HistoryInner::openContextGif(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				Core::App().showDocument(document, item);
			}
		}
	}
}

void HistoryInner::saveContextGif(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				Api::ToggleSavedGif(document, item->fullId(), true);
			}
		}
	}
}

void HistoryInner::copyContextText(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (const auto group = session().data().groups().find(item)) {
			TextUtilities::SetClipboardText(HistoryGroupText(group));
		} else {
			TextUtilities::SetClipboardText(HistoryItemText(item));
		}
	}
}

void HistoryInner::resizeEvent(QResizeEvent *e) {
	mouseActionUpdate();
}

TextForMimeData HistoryInner::getSelectedText() const {
	auto selected = _selected;

	if (_mouseAction == MouseAction::Selecting && _dragSelFrom && _dragSelTo) {
		applyDragSelection(&selected);
	}

	if (selected.empty()) {
		return TextForMimeData();
	}
	if (selected.cbegin()->second != FullSelection) {
		const auto [item, selection] = *selected.cbegin();
		if (const auto view = item->mainView()) {
			return view->selectedText(selection);
		}
		return TextForMimeData();
	}

	const auto timeFormat = qsl(", [dd.MM.yy hh:mm]\n");
	auto groups = base::flat_set<not_null<const Data::Group*>>();
	auto fullSize = 0;
	auto texts = base::flat_map<Data::MessagePosition, TextForMimeData>();

	const auto wrapItem = [&](
			not_null<HistoryItem*> item,
			TextForMimeData &&unwrapped) {
		auto time = ItemDateTime(item).toString(timeFormat);
		auto part = TextForMimeData();
		auto size = item->author()->name.size()
			+ time.size()
			+ unwrapped.expanded.size();
		part.reserve(size);
		part.append(item->author()->name).append(time);
		part.append(std::move(unwrapped));
		texts.emplace(item->position(), part);
		fullSize += size;
	};
	const auto addItem = [&](not_null<HistoryItem*> item) {
		wrapItem(item, HistoryItemText(item));
	};
	const auto addGroup = [&](not_null<const Data::Group*> group) {
		Expects(!group->items.empty());

		wrapItem(group->items.back(), HistoryGroupText(group));
	};

	for (const auto [item, selection] : selected) {
		if (const auto group = session().data().groups().find(item)) {
			if (groups.contains(group)) {
				continue;
			}
			if (isSelectedGroup(&selected, group)) {
				groups.emplace(group);
				addGroup(group);
			} else {
				addItem(item);
			}
		} else {
			addItem(item);
		}
	}

	auto result = TextForMimeData();
	const auto sep = qstr("\n\n");
	result.reserve(fullSize + (texts.size() - 1) * sep.size());
	for (auto i = texts.begin(), e = texts.end(); i != e;) {
		result.append(std::move(i->second));
		if (++i != e) {
			result.append(sep);
		}
	}
	return result;
}

void HistoryInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		_widget->escape();
	} else if (e == QKeySequence::Copy && !_selected.empty()) {
		copySelectedText();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E
		&& e->modifiers().testFlag(Qt::ControlModifier)) {
		TextUtilities::SetClipboardText(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else if (e == QKeySequence::Delete) {
		auto selectedState = getSelectionState();
		if (selectedState.count > 0
			&& selectedState.canDeleteCount == selectedState.count) {
			_widget->confirmDeleteSelected();
		}
	} else {
		e->ignore();
	}
}

void HistoryInner::checkHistoryActivation() {
	if (!_widget->doWeReadServerHistory()) {
		return;
	}
	adjustCurrent(_visibleAreaBottom);
	if (_history->loadedAtBottom() && _visibleAreaBottom >= height()) {
		// Clear possible scheduled messages notifications.
		Core::App().notifications().clearFromHistory(_history);
	}
	if (_curHistory != _history || _history->isEmpty()) {
		return;
	}
	auto block = _history->blocks[_curBlock].get();
	auto view = block->messages[_curItem].get();
	while (_curBlock > 0 || _curItem > 0) {
		const auto top = itemTop(view);
		const auto bottom = itemTop(view) + view->height();
		if (_visibleAreaBottom >= bottom) {
			break;
		}
		if (_curItem > 0) {
			view = block->messages[--_curItem].get();
		} else {
			while (_curBlock > 0) {
				block = _history->blocks[--_curBlock].get();
				_curItem = block->messages.size();
				if (_curItem > 0) {
					view = block->messages[--_curItem].get();
					break;
				}
			}
		}
	}
	session().data().histories().readInboxTill(view->data());
}

void HistoryInner::recountHistoryGeometry() {
	_contentWidth = _scroll->width();

	const auto visibleHeight = _scroll->height();
	int oldHistoryPaddingTop = qMax(visibleHeight - historyHeight() - st::historyPaddingBottom, 0);
	if (_botAbout && !_botAbout->info->text.isEmpty()) {
		accumulate_max(oldHistoryPaddingTop, st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + _botAbout->height);
	}

	_history->resizeToWidth(_contentWidth);
	if (_migrated) {
		_migrated->resizeToWidth(_contentWidth);
	}

	// With migrated history we perhaps do not need to display
	// the first _history message date (just skip it by height).
	_historySkipHeight = 0;
	if (_migrated
		&& _migrated->loadedAtBottom()
		&& _history->loadedAtTop()) {
		if (const auto first = _history->findFirstNonEmpty()) {
			if (const auto last = _migrated->findLastNonEmpty()) {
				if (first->dateTime().date() == last->dateTime().date()) {
					const auto dateHeight = first->displayedDateHeight();
					if (_migrated->height() > dateHeight) {
						_historySkipHeight += dateHeight;
					}
				}
			}
		}
	}

	updateBotInfo(false);
	if (_botAbout && !_botAbout->info->text.isEmpty()) {
		int32 tw = _scroll->width() - st::msgMargin.left() - st::msgMargin.right();
		if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
		tw -= st::msgPadding.left() + st::msgPadding.right();
		const auto descriptionWidth = _history->peer->isRepliesChat()
			? 0
			: st::msgNameFont->width(tr::lng_bot_description(tr::now));
		int32 mw = qMax(_botAbout->info->text.maxWidth(), descriptionWidth);
		if (tw > mw) tw = mw;

		_botAbout->width = tw;
		_botAbout->height = _botAbout->info->text.countHeight(_botAbout->width);

		const auto descriptionHeight = _history->peer->isRepliesChat()
			? 0
			: (st::msgNameFont->height + st::botDescSkip);
		int32 descH = st::msgMargin.top() + st::msgPadding.top() + descriptionHeight + _botAbout->height + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descMaxWidth = _scroll->width();
		if (Core::App().settings().chatWide()) {
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
			_botAbout->info->text.setText(
				st::messageTextStyle,
				_botAbout->info->description,
				Ui::ItemTextBotNoMonoOptions());
			if (recount) {
				int32 tw = _scroll->width() - st::msgMargin.left() - st::msgMargin.right();
				if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
				tw -= st::msgPadding.left() + st::msgPadding.right();
				const auto descriptionWidth = _history->peer->isRepliesChat()
					? 0
					: st::msgNameFont->width(tr::lng_bot_description(tr::now));
				int32 mw = qMax(_botAbout->info->text.maxWidth(), descriptionWidth);
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
			const auto descriptionHeight = _history->peer->isRepliesChat()
				? 0
				: (st::msgNameFont->height + st::botDescSkip);
			int32 descH = st::msgMargin.top() + st::msgPadding.top() + descriptionHeight + _botAbout->height + st::msgPadding.bottom() + st::msgMargin.bottom();
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
	const auto visibleAreaHeight = bottom - top;

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
		scrollDateHideByTimer();
	}

	// Unload userpics.
	if (_userpics.size() > kClearUserpicsAfter) {
		_userpicsCache = std::move(_userpics);
	}

	// Unload lottie animations.
	const auto pages = kUnloadHeavyPartsPages;
	const auto from = _visibleAreaTop - pages * visibleAreaHeight;
	const auto till = _visibleAreaBottom + pages * visibleAreaHeight;
	session().data().unloadHeavyViewParts(ElementDelegate(), from, till);
	checkHistoryActivation();
}

bool HistoryInner::displayScrollDate() const {
	return (_visibleAreaTop <= height() - 2 * (_visibleAreaBottom - _visibleAreaTop));
}

void HistoryInner::scrollDateCheck() {
	auto newScrollDateItem = _history->scrollTopItem ? _history->scrollTopItem : (_migrated ? _migrated->scrollTopItem : nullptr);
	auto newScrollDateItemTop = _history->scrollTopItem ? _history->scrollTopOffset : (_migrated ? _migrated->scrollTopOffset : 0);
	//if (newScrollDateItem && !displayScrollDate()) {
	//	if (!_history->isEmpty() && newScrollDateItem->date.date() == _history->blocks.back()->messages.back()->data()->date.date()) {
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
		_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
	}
}

void HistoryInner::scrollDateHideByTimer() {
	_scrollDateHideTimer.cancel();
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
	_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
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
		const auto descriptionHeight = _history->peer->isRepliesChat()
			? 0
			: (st::msgNameFont->height + st::botDescSkip);
		int32 descH = st::msgMargin.top() + st::msgPadding.top() + descriptionHeight + _botAbout->height + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descMaxWidth = _scroll->width();
		if (Core::App().settings().chatWide()) {
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
	for (const auto &item : _animatedStickersPlayed) {
		if (const auto view = item->mainView()) {
			if (const auto media = view->media()) {
				media->stickerClearLoopPlayed();
			}
		}
	}
	if (Instance == this) {
		Instance = nullptr;
	}
	delete _menu;
	_mouseAction = MouseAction::None;
}

bool HistoryInner::focusNextPrevChild(bool next) {
	if (_selected.empty()) {
		return TWidget::focusNextPrevChild(next);
	} else {
		clearSelected();
		return true;
	}
}

void HistoryInner::adjustCurrent(int32 y) const {
	int32 htop = historyTop(), hdrawtop = historyDrawTop(), mtop = migratedTop();
	_curHistory = nullptr;
	if (mtop >= 0) {
		adjustCurrent(y - mtop, _migrated);
	}
	if (htop >= 0 && hdrawtop >= 0 && (mtop < 0 || y >= hdrawtop)) {
		adjustCurrent(y - htop, _history);
	}
}

void HistoryInner::adjustCurrent(int32 y, History *history) const {
	Expects(!history->isEmpty());

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
	auto block = history->blocks[_curBlock].get();
	if (_curItem >= block->messages.size()) {
		_curItem = block->messages.size() - 1;
	}
	auto by = block->y();
	while (block->messages[_curItem]->y() + by > y && _curItem > 0) {
		--_curItem;
	}
	while (block->messages[_curItem]->y() + block->messages[_curItem]->height() + by <= y && _curItem + 1 < block->messages.size()) {
		++_curItem;
	}
}

auto HistoryInner::prevItem(Element *view) -> Element* {
	if (!view) {
		return nullptr;
	} else if (const auto result = view->previousDisplayedInBlocks()) {
		return result;
	} else if (view->data()->history() == _history
		&& _migrated
		&& _history->loadedAtTop()
		&& !_migrated->isEmpty()
		&& _migrated->loadedAtBottom()) {
		return _migrated->findLastDisplayed();
	}
	return nullptr;
}

auto HistoryInner::nextItem(Element *view) -> Element* {
	if (!view) {
		return nullptr;
	} else if (const auto result = view->nextDisplayedInBlocks()) {
		return result;
	} else if (view->data()->history() == _migrated
		&& _migrated->loadedAtBottom()
		&& _history->loadedAtTop()
		&& !_history->isEmpty()) {
		return _history->findFirstDisplayed();
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

bool HistoryInner::inSelectionMode() const {
	if (!_selected.empty()
		&& (_selected.begin()->second == FullSelection)) {
		return true;
	} else if (_mouseAction == MouseAction::Selecting
		&& _dragSelFrom
		&& _dragSelTo) {
		return true;
	}
	return false;
}

bool HistoryInner::elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) const {
	const auto top = itemTop(view);
	if (top < 0) {
		return false;
	}
	const auto bottom = top + view->height();
	return (top < till && bottom > from);
}

void HistoryInner::elementStartStickerLoop(
		not_null<const Element*> view) {
	_animatedStickersPlayed.emplace(view->data());
}

crl::time HistoryInner::elementHighlightTime(not_null<const Element*> view) {
	const auto fullAnimMs = _controller->content()->highlightStartTime(
		view->data());
	if (fullAnimMs > 0) {
		const auto now = crl::now();
		if (fullAnimMs < now) {
			return now - fullAnimMs;
		}
	}
	return 0;
}

void HistoryInner::elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) {
	_controller->showPollResults(poll, context);
}

void HistoryInner::elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	_widget->showInfoTooltip(text, std::move(hiddenCallback));
}

bool HistoryInner::elementIsGifPaused() {
	return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

void HistoryInner::elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
	if (auto peer = Ui::getPeerForMouseAction()) { // old way
		auto bot = peer->isUser() ? peer->asUser() : nullptr;
		if (!bot) {
			if (const auto view = App::hoveredLinkItem()) {
				// may return nullptr
				bot = view->data()->fromOriginal()->asUser();
			}
		}
		Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
		App::sendBotCommand(peer, bot, command);
	} else {
		App::insertBotCommand(command);
	}
}

void HistoryInner::elementHandleViaClick(not_null<UserData*> bot) {
	App::insertBotCommand('@' + bot->username);
}

auto HistoryInner::getSelectionState() const
-> HistoryView::TopBarWidget::SelectedState {
	auto result = HistoryView::TopBarWidget::SelectedState {};
	for (auto &selected : _selected) {
		if (selected.second == FullSelection) {
			++result.count;
			if (selected.first->canDelete()) {
				++result.canDeleteCount;
			}
			if (selected.first->allowsForward()) {
				++result.canForwardCount;
			}
		} else if (selected.second.from != selected.second.to) {
			result.textSelected = true;
		}
	}
	return result;
}

void HistoryInner::clearSelected(bool onlyTextSelection) {
	if (!_selected.empty() && (!onlyTextSelection || _selected.cbegin()->second != FullSelection)) {
		_selected.clear();
		_widget->updateTopBarSelection();
		_widget->update();
	}
}

MessageIdsList HistoryInner::getSelectedItems() const {
	using namespace ranges;

	if (_selected.empty() || _selected.cbegin()->second != FullSelection) {
		return {};
	}

	auto result = ranges::make_subrange(
		_selected.begin(),
		_selected.end()
	) | view::filter([](const auto &selected) {
		const auto item = selected.first;
		return item && item->toHistoryMessage() && (item->id > 0);
	}) | view::transform([](const auto &selected) {
		return selected.first->fullId();
	}) | to_vector;

	result |= action::sort(ordered_less{}, [](const FullMsgId &msgId) {
		return msgId.channel ? msgId.msg : (msgId.msg - ServerMaxMsgId);
	});
	return result;
}

void HistoryInner::selectItem(not_null<HistoryItem*> item) {
	if (!_selected.empty() && _selected.cbegin()->second != FullSelection) {
		_selected.clear();
	} else if (_selected.size() == MaxSelectedItems
		&& _selected.find(item) == _selected.cend()) {
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

void HistoryInner::mouseActionUpdate() {
	if (hasPendingResizedItems()) {
		return;
	}

	auto mousePos = mapFromGlobal(_mousePosition);
	auto point = _widget->clampMousePosition(mousePos);

	auto block = (HistoryBlock*)nullptr;
	auto item = (HistoryItem*)nullptr;
	auto view = (Element*)nullptr;
	QPoint m;

	adjustCurrent(point.y());
	if (_curHistory && !_curHistory->isEmpty()) {
		block = _curHistory->blocks[_curBlock].get();
		view = block->messages[_curItem].get();
		item = view->data();

		App::mousedItem(view);
		m = mapPointToItem(point, view);
		if (view->pointState(m) != PointState::Outside) {
			if (App::hoveredItem() != view) {
				repaintItem(App::hoveredItem());
				App::hoveredItem(view);
				repaintItem(App::hoveredItem());
			}
		} else if (App::hoveredItem()) {
			repaintItem(App::hoveredItem());
			App::hoveredItem(nullptr);
		}
	}
	if (_mouseActionItem && !_mouseActionItem->mainView()) {
		mouseActionCancel();
	}

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto selectingText = (item == _mouseActionItem)
		&& (view == App::hoveredItem())
		&& !_selected.empty()
		&& (_selected.cbegin()->second != FullSelection);
	if (point.y() < _historyPaddingTop) {
		if (_botAbout && !_botAbout->info->text.isEmpty() && _botAbout->height > 0) {
			dragState = TextState(nullptr, _botAbout->info->text.getState(
				point - _botAbout->rect.topLeft() - QPoint(st::msgPadding.left(), st::msgPadding.top() + st::botDescSkip + st::msgNameFont->height),
				_botAbout->width));
			_dragStateItem = session().data().message(dragState.itemId);
			lnkhost = _botAbout.get();
		}
	} else if (item) {
		if (item != _mouseActionItem || (m - _dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				crl::on_main(this, [=] { performDrag(); });
			} else if (_mouseAction == MouseAction::PrepareSelect) {
				_mouseAction = MouseAction::Selecting;
			}
		}

		auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
		auto scrollDateOpacity = _scrollDateOpacity.value(_scrollDateShown ? 1. : 0.);
		enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
			// stop enumeration if the date is above our point
			if (dateTop + dateHeight <= point.y()) {
				return false;
			}

			const auto displayDate = view->displayDate();
			auto dateInPlace = displayDate;
			if (dateInPlace) {
				const auto correctDateTop = itemtop + st::msgServiceMargin.top();
				dateInPlace = (dateTop < correctDateTop + dateHeight);
			}

			// stop enumeration if we've found a date under the cursor
			if (dateTop <= point.y()) {
				auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
				if (opacity > 0.) {
					const auto item = view->data();
					auto dateWidth = 0;
					if (const auto date = view->Get<HistoryView::DateBadge>()) {
						dateWidth = date->width;
					} else {
						dateWidth = st::msgServiceFont->width(langDayOfMonthFull(view->dateTime().date()));
					}
					dateWidth += st::msgServicePadding.left() + st::msgServicePadding.right();
					auto dateLeft = st::msgServiceMargin.left();
					auto maxwidth = _contentWidth;
					if (Core::App().settings().chatWide()) {
						maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
					}
					auto widthForDate = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

					dateLeft += (widthForDate - dateWidth) / 2;

					if (point.x() >= dateLeft && point.x() < dateLeft + dateWidth) {
						if (!_scrollDateLink) {
							_scrollDateLink = std::make_shared<Window::DateClickHandler>(item->history(), view->dateTime().date());
						} else {
							static_cast<Window::DateClickHandler*>(_scrollDateLink.get())->setDate(view->dateTime().date());
						}
						dragState = TextState(
							nullptr,
							_scrollDateLink);
						_dragStateItem = session().data().message(dragState.itemId);
						lnkhost = view;
					}
				}
				return false;
			}
			return true;
		});
		if (!dragState.link) {
			StateRequest request;
			if (_mouseAction == MouseAction::Selecting) {
				request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			} else {
				selectingText = false;
			}
			dragState = view->textState(m, request);
			_dragStateItem = session().data().message(dragState.itemId);
			lnkhost = view;
			if (!dragState.link && m.x() >= st::historyPhotoLeft && m.x() < st::historyPhotoLeft + st::msgPhotoSize) {
				if (auto msg = item->toHistoryMessage()) {
					if (view->hasFromPhoto()) {
						enumerateUserpics([&](not_null<Element*> view, int userpicTop) -> bool {
							// stop enumeration if the userpic is below our point
							if (userpicTop > point.y()) {
								return false;
							}

							// stop enumeration if we've found a userpic under the cursor
							if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
								const auto message = view->data()->toHistoryMessage();
								Assert(message != nullptr);

								const auto from = message->displayFrom();
								dragState = TextState(nullptr, from
									? from->openLink()
									: hiddenUserpicLink(message->fullId()));
								_dragStateItem = session().data().message(dragState.itemId);
								lnkhost = view;
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
	if (dragState.link
		|| dragState.cursor == CursorState::Date
		|| dragState.cursor == CursorState::Forwarded
		|| dragState.customTooltip) {
		Ui::Tooltip::Show(1000, this);
	}

	Qt::CursorShape cur = style::cur_default;
	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		if (dragState.link) {
			cur = style::cur_pointer;
		} else if (_mouseCursorState == CursorState::Text && (_selected.empty() || _selected.cbegin()->second != FullSelection)) {
			cur = style::cur_text;
		} else if (_mouseCursorState == CursorState::Date) {
			//cur = style::cur_cross;
		}
	} else if (item) {
		if (_mouseAction == MouseAction::Selecting) {
			if (selectingText) {
				uint16 second = dragState.symbol;
				if (dragState.afterSymbol && _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selState = TextSelection { qMin(second, _mouseTextSymbol), qMax(second, _mouseTextSymbol) };
				if (_mouseSelectType != TextSelectType::Letters) {
					if (const auto view = _mouseActionItem->mainView()) {
						selState = view->adjustSelection(selState, _mouseSelectType);
					}
				}
				if (_selected[_mouseActionItem] != selState) {
					_selected[_mouseActionItem] = selState;
					repaintItem(_mouseActionItem);
				}
				if (!_wasSelectedText && (selState == FullSelection || selState.from != selState.to)) {
					_wasSelectedText = true;
					setFocus();
				}
				updateDragSelection(nullptr, nullptr, false);
			} else {
				auto selectingDown = (itemTop(_mouseActionItem) < itemTop(item)) || (_mouseActionItem == item && _dragStartPosition.y() < m.y());
				auto dragSelFrom = _mouseActionItem->mainView();
				auto dragSelTo = view;
				// Maybe exclude dragSelFrom.
				if (dragSelFrom->pointState(_dragStartPosition) == PointState::Outside) {
					if (selectingDown) {
						if (_dragStartPosition.y() >= dragSelFrom->height() - dragSelFrom->marginBottom() || ((view == dragSelFrom) && (m.y() < _dragStartPosition.y() + QApplication::startDragDistance() || m.y() < dragSelFrom->marginTop()))) {
							dragSelFrom = (dragSelFrom != dragSelTo)
								? nextItem(dragSelFrom)
								: nullptr;
						}
					} else {
						if (_dragStartPosition.y() < dragSelFrom->marginTop() || ((view == dragSelFrom) && (m.y() >= _dragStartPosition.y() - QApplication::startDragDistance() || m.y() >= dragSelFrom->height() - dragSelFrom->marginBottom()))) {
							dragSelFrom = (dragSelFrom != dragSelTo)
								? prevItem(dragSelFrom)
								: nullptr;
						}
					}
				}
				if (_mouseActionItem != item) { // maybe exclude dragSelTo
					if (selectingDown) {
						if (m.y() < dragSelTo->marginTop()) {
							dragSelTo = (dragSelFrom != dragSelTo)
								? prevItem(dragSelTo)
								: nullptr;
						}
					} else {
						if (m.y() >= dragSelTo->height() - dragSelTo->marginBottom()) {
							dragSelTo = (dragSelFrom != dragSelTo)
								? nextItem(dragSelTo)
								: nullptr;
						}
					}
				}
				auto dragSelecting = false;
				auto dragFirstAffected = dragSelFrom;
				while (dragFirstAffected && (dragFirstAffected->data()->id < 0 || dragFirstAffected->data()->serviceMsg())) {
					dragFirstAffected = (dragFirstAffected != dragSelTo)
						? (selectingDown
							? nextItem(dragFirstAffected)
							: prevItem(dragFirstAffected))
						: nullptr;
				}
				if (dragFirstAffected) {
					auto i = _selected.find(dragFirstAffected->data());
					dragSelecting = (i == _selected.cend() || i->second != FullSelection);
				}
				updateDragSelection(dragSelFrom, dragSelTo, dragSelecting);
			}
		} else if (_mouseAction == MouseAction::Dragging) {
		}

		if (ClickHandler::getPressed()) {
			cur = style::cur_pointer;
		} else if ((_mouseAction == MouseAction::Selecting)
			&& !_selected.empty()
			&& (_selected.cbegin()->second != FullSelection)) {
			if (!_dragSelFrom || !_dragSelTo) {
				cur = style::cur_text;
			}
		}
	}

	// Voice message seek support.
	if (const auto pressedItem = _dragStateItem) {
		if (const auto pressedView = pressedItem->mainView()) {
			if (pressedItem->history() == _history || pressedItem->history() == _migrated) {
				auto adjustedPoint = mapPointToItem(point, pressedView);
				pressedView->updatePressed(adjustedPoint);
			}
		}
	}

	if (_mouseAction == MouseAction::Selecting) {
		_widget->checkSelectingScroll(mousePos);
	} else {
		updateDragSelection(nullptr, nullptr, false);
		_widget->noSelectingScroll();
	}

	if (_mouseAction == MouseAction::None && (lnkChanged || cur != _cursor)) {
		setCursor(_cursor = cur);
	}
}

ClickHandlerPtr HistoryInner::hiddenUserpicLink(FullMsgId id) {
	static const auto result = std::make_shared<LambdaClickHandler>([] {
		Ui::Toast::Show(tr::lng_forwarded_hidden(tr::now));
	});
	return result;
}

void HistoryInner::updateDragSelection(Element *dragSelFrom, Element *dragSelTo, bool dragSelecting) {
	if (_dragSelFrom == dragSelFrom && _dragSelTo == dragSelTo && _dragSelecting == dragSelecting) {
		return;
	}
	_dragSelFrom = dragSelFrom;
	_dragSelTo = dragSelTo;
	int32 fromy = itemTop(_dragSelFrom), toy = itemTop(_dragSelTo);
	if (fromy >= 0 && toy >= 0 && fromy > toy) {
		std::swap(_dragSelFrom, _dragSelTo);
	}
	_dragSelecting = dragSelecting;
	if (!_wasSelectedText && _dragSelFrom && _dragSelTo && _dragSelecting) {
		_wasSelectedText = true;
		setFocus();
	}
	update();
}

int HistoryInner::historyHeight() const {
	int result = 0;
	if (_history->isEmpty()) {
		result += _migrated ? _migrated->height() : 0;
	} else {
		result += _history->height() - _historySkipHeight + (_migrated ? _migrated->height() : 0);
	}
	return result;
}

int HistoryInner::historyScrollTop() const {
	auto htop = historyTop();
	auto mtop = migratedTop();
	if (htop >= 0 && _history->scrollTopItem) {
		return htop + _history->scrollTopItem->block()->y() + _history->scrollTopItem->y() + _history->scrollTopOffset;
	}
	if (mtop >= 0 && _migrated->scrollTopItem) {
		return mtop + _migrated->scrollTopItem->block()->y() + _migrated->scrollTopItem->y() + _migrated->scrollTopOffset;
	}
	return ScrollMax;
}

int HistoryInner::migratedTop() const {
	return (_migrated && !_migrated->isEmpty()) ? _historyPaddingTop : -1;
}

int HistoryInner::historyTop() const {
	int mig = migratedTop();
	return !_history->isEmpty()
		? (mig >= 0
			? (mig + _migrated->height() - _historySkipHeight)
			: _historyPaddingTop)
		: -1;
}

int HistoryInner::historyDrawTop() const {
	auto top = historyTop();
	return (top >= 0) ? (top + _historySkipHeight) : -1;
}

// -1 if should not be visible, -2 if bad history()
int HistoryInner::itemTop(const HistoryItem *item) const {
	if (!item) {
		return -2;
	}
	return itemTop(item->mainView());
}

int HistoryInner::itemTop(const Element *view) const {
	if (!view || view->data()->mainView() != view) {
		return -1;
	}

	auto top = (view->data()->history() == _history)
		? historyTop()
		: (view->data()->history() == _migrated
			? migratedTop()
			: -2);
	return (top < 0) ? top : (top + view->y() + view->block()->y());
}

auto HistoryInner::findViewForPinnedTracking(int top) const
-> std::pair<Element*, int> {
	const auto normalTop = historyTop();
	const auto oldTop = migratedTop();
	const auto fromHistory = [&](not_null<History*> history, int historyTop)
	-> std::pair<Element*, int> {
		auto [view, offset] = history->findItemAndOffset(top - historyTop);
		while (view && !IsServerMsgId(view->data()->id)) {
			offset -= view->height();
			view = view->nextInBlocks();
		}
		return { view, offset };
	};
	if (normalTop >= 0 && (oldTop < 0 || top >= normalTop)) {
		return fromHistory(_history, normalTop);
	} else if (oldTop >= 0) {
		auto [view, offset] = fromHistory(_migrated, oldTop);
		if (!view && normalTop >= 0) {
			return fromHistory(_history, normalTop);
		}
		return { view, offset };
	}
	return { nullptr, 0 };
}

void HistoryInner::notifyIsBotChanged() {
	const auto newinfo = _peer->isUser()
		? _peer->asUser()->botInfo.get()
		: nullptr;
	if ((!newinfo && !_botAbout)
		|| (newinfo && _botAbout && _botAbout->info == newinfo)) {
		return;
	}

	if (newinfo) {
		_botAbout = std::make_unique<BotAbout>(this, newinfo);
		if (newinfo && !newinfo->inited) {
			session().api().requestFullPeer(_peer);
		}
	} else {
		_botAbout = nullptr;
	}
}

void HistoryInner::notifyMigrateUpdated() {
	_migrated = _history->migrateFrom();
}

void HistoryInner::applyDragSelection() {
	applyDragSelection(&_selected);
}

bool HistoryInner::isSelected(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	const auto i = toItems->find(item);
	return (i != toItems->cend()) && (i->second == FullSelection);
}

bool HistoryInner::isSelectedGroup(
		not_null<SelectedItems*> toItems,
		not_null<const Data::Group*> group) const {
	for (const auto other : group->items) {
		if (!isSelected(toItems, other)) {
			return false;
		}
	}
	return true;
}

bool HistoryInner::isSelectedAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	if (const auto group = session().data().groups().find(item)) {
		return isSelectedGroup(toItems, group);
	}
	return isSelected(toItems, item);
}

bool HistoryInner::goodForSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		int &totalCount) const {
	if (item->id <= 0 || item->serviceMsg()) {
		return false;
	}
	if (toItems->find(item) == toItems->end()) {
		++totalCount;
	}
	return true;
}

void HistoryInner::addToSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	const auto i = toItems->find(item);
	if (i == toItems->cend()) {
		toItems->emplace(item, FullSelection);
	} else if (i->second != FullSelection) {
		i->second = FullSelection;
	}
}

void HistoryInner::removeFromSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	const auto i = toItems->find(item);
	if (i != toItems->cend()) {
		toItems->erase(i);
	}
}

void HistoryInner::changeSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const {
	if (action == SelectAction::Invert) {
		action = isSelected(toItems, item)
			? SelectAction::Deselect
			: SelectAction::Select;
	}
	auto total = int(toItems->size());
	const auto add = (action == SelectAction::Select);
	if (add
		&& goodForSelection(toItems, item, total)
		&& total <= MaxSelectedItems) {
		addToSelection(toItems, item);
	} else {
		removeFromSelection(toItems, item);
	}
}

void HistoryInner::changeSelectionAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const {
	const auto group = session().data().groups().find(item);
	if (!group) {
		return changeSelection(toItems, item, action);
	}
	if (action == SelectAction::Invert) {
		action = isSelectedAsGroup(toItems, item)
			? SelectAction::Deselect
			: SelectAction::Select;
	}
	auto total = int(toItems->size());
	const auto canSelect = [&] {
		for (const auto other : group->items) {
			if (!goodForSelection(toItems, other, total)) {
				return false;
			}
		}
		return (total <= MaxSelectedItems);
	}();
	if (action == SelectAction::Select && canSelect) {
		for (const auto other : group->items) {
			addToSelection(toItems, other);
		}
	} else {
		for (const auto other : group->items) {
			removeFromSelection(toItems, other);
		}
	}
}

void HistoryInner::forwardItem(FullMsgId itemId) {
	Window::ShowForwardMessagesBox(_controller, { 1, itemId });
}

void HistoryInner::forwardAsGroup(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		Window::ShowForwardMessagesBox(
			_controller,
			session().data().itemOrItsGroup(item));
	}
}

void HistoryInner::deleteItem(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		deleteItem(item);
	}
}

void HistoryInner::deleteItem(not_null<HistoryItem*> item) {
	if (auto message = item->toHistoryMessage()) {
		if (message->uploading()) {
			_controller->content()->cancelUploadLayer(item);
			return;
		}
	}
	const auto suggestModerateActions = true;
	Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
}

bool HistoryInner::hasPendingResizedItems() const {
	return _history->hasPendingResizedItems()
		|| (_migrated && _migrated->hasPendingResizedItems());
}

void HistoryInner::deleteAsGroup(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		const auto group = session().data().groups().find(item);
		if (!group) {
			return deleteItem(item);
		}
		Ui::show(Box<DeleteMessagesBox>(
			&session(),
			session().data().itemsToIds(group->items)));
	}
}

void HistoryInner::reportItem(FullMsgId itemId) {
	Ui::show(Box<ReportBox>(_peer, MessageIdsList(1, itemId)));
}

void HistoryInner::reportAsGroup(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		const auto group = session().data().groups().find(item);
		if (!group) {
			return reportItem(itemId);
		}
		Ui::show(Box<ReportBox>(
			_peer,
			session().data().itemsToIds(group->items)));
	}
}

void HistoryInner::blockSenderItem(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		Ui::show(Box(
			BlockSenderFromRepliesBox,
			_controller,
			itemId));
	}
}

void HistoryInner::blockSenderAsGroup(FullMsgId itemId) {
	blockSenderItem(itemId);
}

void HistoryInner::addSelectionRange(
		not_null<SelectedItems*> toItems,
		not_null<History*> history,
		int fromblock,
		int fromitem,
		int toblock,
		int toitem) const {
	if (fromblock >= 0 && fromitem >= 0 && toblock >= 0 && toitem >= 0) {
		for (; fromblock <= toblock; ++fromblock) {
			auto block = history->blocks[fromblock].get();
			for (int cnt = (fromblock < toblock) ? block->messages.size() : (toitem + 1); fromitem < cnt; ++fromitem) {
				auto item = block->messages[fromitem]->data();
				changeSelectionAsGroup(toItems, item, SelectAction::Select);
			}
			if (toItems->size() >= MaxSelectedItems) break;
			fromitem = 0;
		}
	}
}

void HistoryInner::applyDragSelection(
		not_null<SelectedItems*> toItems) const {
	const auto selfromy = itemTop(_dragSelFrom);
	const auto seltoy = [&] {
		auto result = itemTop(_dragSelTo);
		return (result < 0) ? result : (result + _dragSelTo->height());
	}();
	if (selfromy < 0 || seltoy < 0) {
		return;
	}

	if (!toItems->empty() && toItems->cbegin()->second != FullSelection) {
		toItems->clear();
	}
	if (_dragSelecting) {
		auto fromblock = _dragSelFrom->block()->indexInHistory();
		auto fromitem = _dragSelFrom->indexInBlock();
		auto toblock = _dragSelTo->block()->indexInHistory();
		auto toitem = _dragSelTo->indexInBlock();
		if (_migrated) {
			if (_dragSelFrom->data()->history() == _migrated) {
				if (_dragSelTo->data()->history() == _migrated) {
					addSelectionRange(toItems, _migrated, fromblock, fromitem, toblock, toitem);
					toblock = -1;
					toitem = -1;
				} else {
					addSelectionRange(toItems, _migrated, fromblock, fromitem, _migrated->blocks.size() - 1, _migrated->blocks.back()->messages.size() - 1);
				}
				fromblock = 0;
				fromitem = 0;
			} else if (_dragSelTo->data()->history() == _migrated) { // wtf
				toblock = -1;
				toitem = -1;
			}
		}
		addSelectionRange(toItems, _history, fromblock, fromitem, toblock, toitem);
	} else {
		auto toRemove = std::vector<not_null<HistoryItem*>>();
		for (const auto &item : *toItems) {
			auto iy = itemTop(item.first);
			if (iy < -1) {
				toRemove.emplace_back(item.first);
			} else if (iy >= 0 && iy >= selfromy && iy < seltoy) {
				toRemove.emplace_back(item.first);
			}
		}
		for (const auto item : toRemove) {
			changeSelectionAsGroup(toItems, item, SelectAction::Deselect);
		}
	}
}

QString HistoryInner::tooltipText() const {
	if (_mouseCursorState == CursorState::Date
		&& _mouseAction == MouseAction::None) {
		if (const auto view = App::hoveredItem()) {
			auto dateText = view->dateTime().toString(
				QLocale::system().dateTimeFormat(QLocale::LongFormat));
			if (const auto editedDate = view->displayedEditDate()) {
				dateText += '\n' + tr::lng_edited_date(
					tr::now,
					lt_date,
					base::unixtime::parse(editedDate).toString(
						QLocale::system().dateTimeFormat(
							QLocale::LongFormat)));
			}
			if (const auto forwarded = view->data()->Get<HistoryMessageForwarded>()) {
				dateText += '\n' + tr::lng_forwarded_date(
					tr::now,
					lt_date,
					base::unixtime::parse(forwarded->originalDate).toString(
						QLocale::system().dateTimeFormat(
							QLocale::LongFormat)));
				if (const auto media = view->media()) {
					if (media->hidesForwardedInfo()) {
						dateText += "\n" + tr::lng_forwarded(
							tr::now,
							lt_user,
							(forwarded->originalSender
								? forwarded->originalSender->shortName()
								: forwarded->hiddenSenderInfo->firstName));
					}
				}
			}
			if (const auto msgsigned = view->data()->Get<HistoryMessageSigned>()) {
				if (msgsigned->isElided && !msgsigned->isAnonymousRank) {
					dateText += '\n' + tr::lng_signed_author(tr::now, lt_user, msgsigned->author);
				}
			}
			return dateText;
		}
	} else if (_mouseCursorState == CursorState::Forwarded
		&& _mouseAction == MouseAction::None) {
		if (const auto view = App::mousedItem()) {
			if (const auto forwarded = view->data()->Get<HistoryMessageForwarded>()) {
				return forwarded->text.toString();
			}
		}
	} else if (const auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	} else if (const auto view = App::mousedItem()) {
		StateRequest request;
		const auto local = mapFromGlobal(_mousePosition);
		const auto point = _widget->clampMousePosition(local);
		request.flags |= Ui::Text::StateRequest::Flag::LookupCustomTooltip;
		const auto state = view->textState(
			mapPointToItem(point, view),
			request);
		return state.customTooltipText;
	}
	return QString();
}

QPoint HistoryInner::tooltipPos() const {
	return _mousePosition;
}

bool HistoryInner::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

void HistoryInner::onParentGeometryChanged() {
	auto mousePos = QCursor::pos();
	auto mouseOver = _widget->rect().contains(_widget->mapFromGlobal(mousePos));
	auto needToUpdate = (_mouseAction != MouseAction::None || _touchScroll || mouseOver);
	if (needToUpdate) {
		mouseActionUpdate(mousePos);
	}
}

not_null<HistoryView::ElementDelegate*> HistoryInner::ElementDelegate() {
	class Result : public HistoryView::ElementDelegate {
	public:
		HistoryView::Context elementContext() override {
			return HistoryView::Context::History;
		}
		std::unique_ptr<HistoryView::Element> elementCreate(
				not_null<HistoryMessage*> message,
				Element *replacing = nullptr) override {
			return std::make_unique<HistoryView::Message>(
				this,
				message,
				replacing);
		}
		std::unique_ptr<HistoryView::Element> elementCreate(
				not_null<HistoryService*> message,
				Element *replacing = nullptr) override {
			return std::make_unique<HistoryView::Service>(
				this,
				message,
				replacing);
		}
		bool elementUnderCursor(
				not_null<const Element*> view) override {
			return (App::hoveredItem() == view);
		}
		crl::time elementHighlightTime(
				not_null<const Element*> view) override {
			return Instance ? Instance->elementHighlightTime(view) : 0;
		}
		bool elementInSelectionMode() override {
			return Instance ? Instance->inSelectionMode() : false;
		}
		bool elementIntersectsRange(
				not_null<const Element*> view,
				int from,
				int till) override {
			return Instance
				? Instance->elementIntersectsRange(view, from, till)
				: false;
		}
		void elementStartStickerLoop(
				not_null<const Element*> view) override {
			if (Instance) {
				Instance->elementStartStickerLoop(view);
			}
		}
		void elementShowPollResults(
				not_null<PollData*> poll,
				FullMsgId context) override {
			if (Instance) {
				Instance->elementShowPollResults(poll, context);
			}
		}
		void elementShowTooltip(
				const TextWithEntities &text,
				Fn<void()> hiddenCallback) override {
			if (Instance) {
				Instance->elementShowTooltip(text, hiddenCallback);
			}
		}
		bool elementIsGifPaused() override {
			return Instance ? Instance->elementIsGifPaused() : false;
		}
		bool elementHideReply(not_null<const Element*> view) override {
			return false;
		}
		bool elementShownUnread(not_null<const Element*> view) override {
			return view->data()->unread();
		}
		void elementSendBotCommand(
				const QString &command,
				const FullMsgId &context) override {
			if (Instance) {
				Instance->elementSendBotCommand(command, context);
			}
		}
		void elementHandleViaClick(not_null<UserData*> bot) override {
			if (Instance) {
				Instance->elementHandleViaClick(bot);
			}
		}
	};

	static Result result;
	return &result;
}
