/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_inner.h"

#include "styles/style_history.h"
#include "history/history.h"
#include "history/media/history_media.h"
#include "history/media/history_media_web_page.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
#include "history/history_item_text.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/admin_log/history_admin_log_filter.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_cursor_state.h"
#include "chat_helpers/message_field.h"
#include "boxes/sticker_set_box.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "messenger.h"
#include "apiwrap.h"
#include "layout.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "core/file_utilities.h"
#include "core/tl_help.h"
#include "base/overload.h"
#include "lang/lang_keys.h"
#include "boxes/edit_participant_box.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_media_types.h"

namespace AdminLog {
namespace {

// If we require to support more admins we'll have to rewrite this anyway.
constexpr auto kMaxChannelAdmins = 200;
constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kEventsFirstPage = 20;
constexpr auto kEventsPerPage = 50;

} // namespace

template <InnerWidget::EnumItemsDirection direction, typename Method>
void InnerWidget::enumerateItems(Method method) {
	constexpr auto TopToBottom = (direction == EnumItemsDirection::TopToBottom);

	// No displayed messages in this history.
	if (_items.empty()) {
		return;
	}
	if (_visibleBottom <= _itemsTop || _itemsTop + _itemsHeight <= _visibleTop) {
		return;
	}

	auto begin = std::rbegin(_items), end = std::rend(_items);
	auto from = TopToBottom ? std::lower_bound(begin, end, _visibleTop, [this](auto &elem, int top) {
		return this->itemTop(elem) + elem->height() <= top;
	}) : std::upper_bound(begin, end, _visibleBottom, [this](int bottom, auto &elem) {
		return this->itemTop(elem) + elem->height() >= bottom;
	});
	auto wasEnd = (from == end);
	if (wasEnd) {
		--from;
	}
	if (TopToBottom) {
		Assert(itemTop(from->get()) + from->get()->height() > _visibleTop);
	} else {
		Assert(itemTop(from->get()) < _visibleBottom);
	}

	while (true) {
		auto item = from->get();
		auto itemtop = itemTop(item);
		auto itembottom = itemtop + item->height();

		// Binary search should've skipped all the items that are above / below the visible area.
		if (TopToBottom) {
			Assert(itembottom > _visibleTop);
		} else {
			Assert(itemtop < _visibleBottom);
		}

		if (!method(item, itemtop, itembottom)) {
			return;
		}

		// Skip all the items that are below / above the visible area.
		if (TopToBottom) {
			if (itembottom >= _visibleBottom) {
				return;
			}
		} else {
			if (itemtop <= _visibleTop) {
				return;
			}
		}

		if (TopToBottom) {
			if (++from == end) {
				break;
			}
		} else {
			if (from == begin) {
				break;
			}
			--from;
		}
	}
}

template <typename Method>
void InnerWidget::enumerateUserpics(Method method) {
	// Find and remember the top of an attached messages pack
	// -1 means we didn't find an attached to next message yet.
	int lowestAttachedItemTop = -1;

	auto userpicCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		// Skip all service messages.
		const auto message = view->data()->toHistoryMessage();
		if (!message) return true;

		if (lowestAttachedItemTop < 0 && view->isAttachedToNext()) {
			lowestAttachedItemTop = itemtop + view->marginTop();
		}

		// Call method on a userpic for all messages that have it and for those who are not showing it
		// because of their attachment to the next message if they are bottom-most visible.
		if (view->displayFromPhoto() || (view->hasFromPhoto() && itembottom >= _visibleBottom)) {
			if (lowestAttachedItemTop < 0) {
				lowestAttachedItemTop = itemtop + view->marginTop();
			}
			// Attach userpic to the bottom of the visible area with the same margin as the last message.
			auto userpicMinBottomSkip = st::historyPaddingBottom + st::msgMargin.bottom();
			auto userpicBottom = qMin(itembottom - view->marginBottom(), _visibleBottom - userpicMinBottomSkip);

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
void InnerWidget::enumerateDates(Method method) {
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
		if (view->displayDate() || (!item->isEmpty() && itemtop <= _visibleTop)) {
			if (lowestInOneDayItemBottom < 0) {
				lowestInOneDayItemBottom = itembottom - view->marginBottom();
			}
			// Attach date to the top of the visible area with the same margin as it has in service message.
			auto dateTop = qMax(itemtop, _visibleTop) + st::msgServiceMargin.top();

			// Do not let the date go below the single-day messages pack bottom line.
			auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
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

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<ChannelData*> channel)
: RpWidget(parent)
, _controller(controller)
, _channel(channel)
, _history(App::history(channel))
, _scrollDateCheck([this] { scrollDateCheck(); })
, _emptyText(
	st::historyAdminLogEmptyWidth
	- st::historyAdminLogEmptyPadding.left()
	- st::historyAdminLogEmptyPadding.left())
, _idManager(_history->adminLogIdManager()) {
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([this] { scrollDateHideByTimer(); });
	Auth().data().viewRepaintRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			repaintItem(view);
		}
	}, lifetime());
	Auth().data().viewResizeRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			resizeItem(view);
		}
	}, lifetime());
	Auth().data().itemViewRefreshRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			refreshItem(view);
		}
	}, lifetime());
	Auth().data().viewLayoutChanged(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			if (view->isUnderCursor()) {
				updateSelected();
			}
		}
	}, lifetime());
	Auth().data().animationPlayInlineRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			if (const auto media = view->media()) {
				media->playAnimation();
			}
		}
	}, lifetime());
	subscribe(Auth().data().queryItemVisibility(), [this](const Data::Session::ItemVisibilityQuery &query) {
		if (_history != query.item->history() || !query.item->isLogEntry() || !isVisible()) {
			return;
		}
		if (const auto view = viewForItem(query.item)) {
			auto top = itemTop(view);
			if (top >= 0 && top + view->height() > _visibleTop && top < _visibleBottom) {
				*query.isVisible = true;
			}
		}
	});
	updateEmptyText();

	requestAdmins();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto scrolledUp = (visibleTop < _visibleTop);
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	updateVisibleTopItem();
	checkPreloadMore();
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		scrollDateHideByTimer();
	}
	_controller->floatPlayerAreaUpdated().notify(true);
}

void InnerWidget::updateVisibleTopItem() {
	if (_visibleBottom == height()) {
		_visibleTopItem = nullptr;
	} else {
		auto begin = std::rbegin(_items), end = std::rend(_items);
		auto from = std::lower_bound(begin, end, _visibleTop, [this](auto &&elem, int top) {
			return this->itemTop(elem) + elem->height() <= top;
		});
		if (from != end) {
			_visibleTopItem = *from;
			_visibleTopFromItem = _visibleTop - _visibleTopItem->y();
		} else {
			_visibleTopItem = nullptr;
			_visibleTopFromItem = _visibleTop;
		}
	}
}

bool InnerWidget::displayScrollDate() const {
	return (_visibleTop <= height() - 2 * (_visibleBottom - _visibleTop));
}

void InnerWidget::scrollDateCheck() {
	if (!_visibleTopItem) {
		_scrollDateLastItem = nullptr;
		_scrollDateLastItemTop = 0;
		scrollDateHide();
	} else if (_visibleTopItem != _scrollDateLastItem || _visibleTopFromItem != _scrollDateLastItemTop) {
		// Show scroll date only if it is not the initial onScroll() event (with empty _scrollDateLastItem).
		if (_scrollDateLastItem && !_scrollDateShown) {
			toggleScrollDateShown();
		}
		_scrollDateLastItem = _visibleTopItem;
		_scrollDateLastItemTop = _visibleTopFromItem;
		_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
	}
}

void InnerWidget::scrollDateHideByTimer() {
	_scrollDateHideTimer.cancel();
	scrollDateHide();
}

void InnerWidget::scrollDateHide() {
	if (_scrollDateShown) {
		toggleScrollDateShown();
	}
}

void InnerWidget::toggleScrollDateShown() {
	_scrollDateShown = !_scrollDateShown;
	auto from = _scrollDateShown ? 0. : 1.;
	auto to = _scrollDateShown ? 1. : 0.;
	_scrollDateOpacity.start([this] { repaintScrollDateCallback(); }, from, to, st::historyDateFadeDuration);
}

void InnerWidget::repaintScrollDateCallback() {
	auto updateTop = _visibleTop;
	auto updateHeight = st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	update(0, updateTop, width(), updateHeight);
}

void InnerWidget::checkPreloadMore() {
	if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
		preloadMore(Direction::Down);
	}
	if (_visibleTop < PreloadHeightsCount * (_visibleBottom - _visibleTop)) {
		preloadMore(Direction::Up);
	}
}

void InnerWidget::applyFilter(FilterValue &&value) {
	if (_filter != value) {
		_filter = value;
		clearAndRequestLog();
	}
}

void InnerWidget::applySearch(const QString &query) {
	auto clearQuery = query.trimmed();
	if (_searchQuery != query) {
		_searchQuery = query;
		clearAndRequestLog();
	}
}

void InnerWidget::requestAdmins() {
	auto participantsHash = 0;
	request(MTPchannels_GetParticipants(
		_channel->inputChannel,
		MTP_channelParticipantsAdmins(),
		MTP_int(0),
		MTP_int(kMaxChannelAdmins),
		MTP_int(participantsHash)
	)).done([this](const MTPchannels_ChannelParticipants &result) {
		auto readCanEdit = base::overload([](const MTPDchannelParticipantAdmin &v) {
			return v.is_can_edit();
		}, [](auto &&) {
			return false;
		});
		Auth().api().parseChannelParticipants(_channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			auto filtered = (
				list
			) | ranges::view::transform([&](const MTPChannelParticipant &p) {
				return std::make_pair(
					TLHelp::ReadChannelParticipantUserId(p),
					TLHelp::VisitChannelParticipant(p, readCanEdit));
			}) | ranges::view::transform([&](auto &&pair) {
				return std::make_pair(
					App::userLoaded(pair.first),
					pair.second);
			}) | ranges::view::filter([&](auto &&pair) {
				return (pair.first != nullptr);
			});

			for (auto [user, canEdit] : filtered) {
				_admins.push_back(user);
				if (canEdit) {
					_adminsCanEdit.push_back(user);
				}
			}
		});
		if (_admins.empty()) {
			_admins.push_back(Auth().user());
		}
		if (_showFilterCallback) {
			showFilter(std::move(_showFilterCallback));
		}
	}).send();
}

void InnerWidget::showFilter(Fn<void(FilterValue &&filter)> callback) {
	if (_admins.empty()) {
		_showFilterCallback = std::move(callback);
	} else {
		Ui::show(Box<FilterBox>(_channel, _admins, _filter, std::move(callback)));
	}
}

void InnerWidget::clearAndRequestLog() {
	request(base::take(_preloadUpRequestId)).cancel();
	request(base::take(_preloadDownRequestId)).cancel();
	_filterChanged = true;
	_upLoaded = false;
	_downLoaded = true;
	updateMinMaxIds();
	preloadMore(Direction::Up);
}

void InnerWidget::updateEmptyText() {
	auto options = _defaultOptions;
	options.flags |= TextParseMarkdown;
	auto hasSearch = !_searchQuery.isEmpty();
	auto hasFilter = (_filter.flags != 0) || !_filter.allUsers;
	auto text = TextWithEntities { lang((hasSearch || hasFilter) ? lng_admin_log_no_results_title : lng_admin_log_no_events_title) };
	text.entities.append(EntityInText(EntityInTextBold, 0, text.text.size()));
	auto description = hasSearch
		? lng_admin_log_no_results_search_text(lt_query, TextUtilities::Clean(_searchQuery))
		: lang(hasFilter ? lng_admin_log_no_results_text : lng_admin_log_no_events_text);
	text.text.append(qstr("\n\n") + description);
	_emptyText.setMarkedText(st::defaultTextStyle, text, options);
}

QString InnerWidget::tooltipText() const {
	if (_mouseCursorState == CursorState::Date
		&& _mouseAction == MouseAction::None) {
		if (const auto view = App::hoveredItem()) {
			auto dateText = view->dateTime().toString(
				QLocale::system().dateTimeFormat(QLocale::LongFormat));
			return dateText;
		}
	} else if (_mouseCursorState == CursorState::Forwarded
		&& _mouseAction == MouseAction::None) {
		if (const auto view = App::hoveredItem()) {
			if (const auto forwarded = view->data()->Get<HistoryMessageForwarded>()) {
				return forwarded->text.originalText(AllTextSelection, ExpandLinksNone);
			}
		}
	} else if (const auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

QPoint InnerWidget::tooltipPos() const {
	return _mousePosition;
}

HistoryView::Context InnerWidget::elementContext() {
	return HistoryView::Context::AdminLog;
}

std::unique_ptr<HistoryView::Element> InnerWidget::elementCreate(
		not_null<HistoryMessage*> message) {
	return std::make_unique<HistoryView::Message>(this, message);
}

std::unique_ptr<HistoryView::Element> InnerWidget::elementCreate(
		not_null<HistoryService*> message) {
	return std::make_unique<HistoryView::Service>(this, message);
}

bool InnerWidget::elementUnderCursor(
		not_null<const HistoryView::Element*> view) {
	return (App::hoveredItem() == view);
}

void InnerWidget::elementAnimationAutoplayAsync(
		not_null<const HistoryView::Element*> view) {
	crl::on_main(this, [this, msgId = view->data()->fullId()] {
		if (const auto item = App::histItemById(msgId)) {
			if (const auto view = viewForItem(item)) {
				if (const auto media = view->media()) {
					media->autoplayAnimation();
				}
			}
		}
	});
}

TimeMs InnerWidget::elementHighlightTime(
		not_null<const HistoryView::Element*> element) {
	return TimeMs(0);
}

bool InnerWidget::elementInSelectionMode() {
	return false;
}

void InnerWidget::saveState(not_null<SectionMemento*> memento) {
	memento->setFilter(std::move(_filter));
	memento->setAdmins(std::move(_admins));
	memento->setAdminsCanEdit(std::move(_adminsCanEdit));
	memento->setSearchQuery(std::move(_searchQuery));
	if (!_filterChanged) {
		memento->setItems(std::move(_items), std::move(_itemsByIds), _upLoaded, _downLoaded);
		memento->setIdManager(std::move(_idManager));
	}
	_upLoaded = _downLoaded = true; // Don't load or handle anything anymore.
}

void InnerWidget::restoreState(not_null<SectionMemento*> memento) {
	_items = memento->takeItems();
	for (auto &item : _items) {
		item.refreshView(this);
	}
	_itemsByIds = memento->takeItemsByIds();
	if (auto manager = memento->takeIdManager()) {
		_idManager = std::move(manager);
	}
	_admins = memento->takeAdmins();
	_adminsCanEdit = memento->takeAdminsCanEdit();
	_filter = memento->takeFilter();
	_searchQuery = memento->takeSearchQuery();
	_upLoaded = memento->upLoaded();
	_downLoaded = memento->downLoaded();
	_filterChanged = false;
	updateMinMaxIds();
	updateSize();
}

void InnerWidget::preloadMore(Direction direction) {
	auto &requestId = (direction == Direction::Up) ? _preloadUpRequestId : _preloadDownRequestId;
	auto &loadedFlag = (direction == Direction::Up) ? _upLoaded : _downLoaded;
	if (requestId != 0 || loadedFlag) {
		return;
	}

	auto flags = MTPchannels_GetAdminLog::Flags(0);
	auto filter = MTP_channelAdminLogEventsFilter(MTP_flags(_filter.flags));
	if (_filter.flags != 0) {
		flags |= MTPchannels_GetAdminLog::Flag::f_events_filter;
	}
	auto admins = QVector<MTPInputUser>(0);
	if (!_filter.allUsers) {
		if (!_filter.admins.empty()) {
			admins.reserve(_filter.admins.size());
			for (auto &admin : _filter.admins) {
				admins.push_back(admin->inputUser);
			}
		}
		flags |= MTPchannels_GetAdminLog::Flag::f_admins;
	}
	auto maxId = (direction == Direction::Up) ? _minId : 0;
	auto minId = (direction == Direction::Up) ? 0 : _maxId;
	auto perPage = _items.empty() ? kEventsFirstPage : kEventsPerPage;
	requestId = request(MTPchannels_GetAdminLog(MTP_flags(flags), _channel->inputChannel, MTP_string(_searchQuery), filter, MTP_vector<MTPInputUser>(admins), MTP_long(maxId), MTP_long(minId), MTP_int(perPage))).done([this, &requestId, &loadedFlag, direction](const MTPchannels_AdminLogResults &result) {
		Expects(result.type() == mtpc_channels_adminLogResults);
		requestId = 0;

		auto &results = result.c_channels_adminLogResults();
		App::feedUsers(results.vusers);
		App::feedChats(results.vchats);
		if (!loadedFlag) {
			addEvents(direction, results.vevents.v);
		}
	}).fail([this, &requestId, &loadedFlag](const RPCError &error) {
		requestId = 0;
		loadedFlag = true;
		update();
	}).send();
}

void InnerWidget::addEvents(Direction direction, const QVector<MTPChannelAdminLogEvent> &events) {
	if (_filterChanged) {
		clearAfterFilterChange();
	}

	auto up = (direction == Direction::Up);
	if (events.empty()) {
		(up ? _upLoaded : _downLoaded) = true;
		update();
		return;
	}

	// When loading items up we just add them to the back of the _items vector.
	// When loading items down we add them to a new vector and copy _items after them.
	auto newItemsForDownDirection = std::vector<OwnedItem>();
	auto oldItemsCount = _items.size();
	auto &addToItems = (direction == Direction::Up) ? _items : newItemsForDownDirection;
	addToItems.reserve(oldItemsCount + events.size() * 2);
	for_const (auto &event, events) {
		Assert(event.type() == mtpc_channelAdminLogEvent);
		const auto &data = event.c_channelAdminLogEvent();
		const auto id = data.vid.v;
		if (_itemsByIds.find(id) != _itemsByIds.cend()) {
			continue;
		}

		auto count = 0;
		const auto addOne = [&](OwnedItem item) {
			_itemsByIds.emplace(id, item.get());
			_itemsByData.emplace(item->data(), item.get());
			addToItems.push_back(std::move(item));
			++count;
		};
		GenerateItems(
			this,
			_history,
			_idManager.get(),
			data,
			addOne);
		if (count > 1) {
			// Reverse the inner order of the added messages, because we load events
			// from bottom to top but inside one event they go from top to bottom.
			auto full = addToItems.size();
			auto from = full - count;
			for (auto i = 0, toReverse = count / 2; i != toReverse; ++i) {
				std::swap(addToItems[from + i], addToItems[full - i - 1]);
			}
		}
	}
	auto newItemsCount = _items.size() + ((direction == Direction::Up) ? 0 : newItemsForDownDirection.size());
	if (newItemsCount != oldItemsCount) {
		if (direction == Direction::Down) {
			for (auto &item : _items) {
				newItemsForDownDirection.push_back(std::move(item));
			}
			_items = std::move(newItemsForDownDirection);
		}
		updateMinMaxIds();
		itemsAdded(direction, newItemsCount - oldItemsCount);
	}
	update();
}

void InnerWidget::updateMinMaxIds() {
	if (_itemsByIds.empty() || _filterChanged) {
		_maxId = _minId = 0;
	} else {
		_maxId = (--_itemsByIds.end())->first;
		_minId = _itemsByIds.begin()->first;
		if (_minId == 1) {
			_upLoaded = true;
		}
	}
}

void InnerWidget::itemsAdded(Direction direction, int addedCount) {
	Expects(addedCount >= 0);
	auto checkFrom = (direction == Direction::Up)
		? (_items.size() - addedCount)
		: 1; // Should be ": 0", but zero is skipped anyway.
	auto checkTo = (direction == Direction::Up) ? (_items.size() + 1) : (addedCount + 1);
	for (auto i = checkFrom; i != checkTo; ++i) {
		if (i > 0) {
			const auto view = _items[i - 1].get();
			if (i < _items.size()) {
				const auto previous = _items[i].get();
				view->setDisplayDate(view->dateTime().date() != previous->dateTime().date());
				const auto attach = view->computeIsAttachToPrevious(previous);
				view->setAttachToPrevious(attach);
				previous->setAttachToNext(attach);
			} else {
				view->setDisplayDate(true);
			}
		}
	}
	updateSize();
}

void InnerWidget::updateSize() {
	TWidget::resizeToWidth(width());
	restoreScrollPosition();
	updateVisibleTopItem();
	checkPreloadMore();
}

int InnerWidget::resizeGetHeight(int newWidth) {
	update();

	const auto resizeAllItems = (_itemsWidth != newWidth);
	auto newHeight = 0;
	for (auto &item : base::reversed(_items)) {
		item->setY(newHeight);
		if (item->pendingResize() || resizeAllItems) {
			newHeight += item->resizeGetHeight(newWidth);
		} else {
			newHeight += item->height();
		}
	}
	_itemsWidth = newWidth;
	_itemsHeight = newHeight;
	_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom) ? (_minHeight - _itemsHeight - st::historyPaddingBottom) : 0;
	return _itemsTop + _itemsHeight + st::historyPaddingBottom;
}

void InnerWidget::restoreScrollPosition() {
	auto newVisibleTop = _visibleTopItem
		? (itemTop(_visibleTopItem) + _visibleTopFromItem)
		: ScrollMax;
	scrollToSignal.notify(newVisibleTop, true);
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	Painter p(this);

	auto ms = getms();
	auto clip = e->rect();

	if (_items.empty() && _upLoaded && _downLoaded) {
		paintEmpty(p);
	} else {
		auto begin = std::rbegin(_items), end = std::rend(_items);
		auto from = std::lower_bound(begin, end, clip.top(), [this](auto &elem, int top) {
			return this->itemTop(elem) + elem->height() <= top;
		});
		auto to = std::lower_bound(begin, end, clip.top() + clip.height(), [this](auto &elem, int bottom) {
			return this->itemTop(elem) < bottom;
		});
		if (from != end) {
			auto top = itemTop(from->get());
			p.translate(0, top);
			for (auto i = from; i != to; ++i) {
				const auto view = i->get();
				const auto selection = (view == _selectedItem)
					? _selectedText
					: TextSelection();
				view->draw(p, clip.translated(0, -top), selection, ms);
				const auto item = view->data();
				auto height = view->height();
				top += height;
				p.translate(0, height);
			}
			p.translate(0, -top);

			enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
				// stop the enumeration if the userpic is below the painted rect
				if (userpicTop >= clip.top() + clip.height()) {
					return false;
				}

				// paint the userpic if it intersects the painted rect
				if (userpicTop + st::msgPhotoSize > clip.top()) {
					const auto message = view->data()->toHistoryMessage();
					Assert(message != nullptr);

					message->from()->paintUserpicLeft(p, st::historyPhotoLeft, userpicTop, view->width(), st::msgPhotoSize);
				}
				return true;
			});

			auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			auto scrollDateOpacity = _scrollDateOpacity.current(ms, _scrollDateShown ? 1. : 0.);
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
						const auto dateY = /*noFloatingDate ? itemtop :*/ (dateTop - st::msgServiceMargin.top());
						const auto width = view->width();
						if (const auto date = view->Get<HistoryView::DateBadge>()) {
							date->paint(p, dateY, width);
						} else {
							HistoryView::ServiceMessagePainter::paintDate(
								p,
								view->dateTime(),
								dateY,
								width);
						}
					}
				}
				return true;
			});
		}
	}
}

void InnerWidget::clearAfterFilterChange() {
	_visibleTopItem = nullptr;
	_visibleTopFromItem = 0;
	_scrollDateLastItem = nullptr;
	_scrollDateLastItemTop = 0;
	_mouseActionItem = nullptr;
	_selectedItem = nullptr;
	_selectedText = TextSelection();
	_filterChanged = false;
	_items.clear();
	_itemsByIds.clear();
	_idManager = nullptr;
	_idManager = _history->adminLogIdManager();
	updateEmptyText();
	updateSize();
}

auto InnerWidget::viewForItem(const HistoryItem *item) -> Element* {
	if (item) {
		const auto i = _itemsByData.find(item);
		if (i != _itemsByData.end()) {
			return i->second;
		}
	}
	return nullptr;
}

void InnerWidget::paintEmpty(Painter &p) {
	style::font font(st::msgServiceFont);
	auto rectWidth = st::historyAdminLogEmptyWidth;
	auto innerWidth = rectWidth - st::historyAdminLogEmptyPadding.left() - st::historyAdminLogEmptyPadding.right();
	auto rectHeight = st::historyAdminLogEmptyPadding.top() + _emptyText.countHeight(innerWidth) + st::historyAdminLogEmptyPadding.bottom();
	auto rect = QRect((width() - rectWidth) / 2, (height() - rectHeight) / 3, rectWidth, rectHeight);
	HistoryView::ServiceMessagePainter::paintBubble(
		p,
		rect.x(),
		rect.y(),
		rect.width(),
		rect.height());

	p.setPen(st::msgServiceFg);
	_emptyText.draw(p, rect.x() + st::historyAdminLogEmptyPadding.left(), rect.y() + st::historyAdminLogEmptyPadding.top(), innerWidth, style::al_top);
}

TextWithEntities InnerWidget::getSelectedText() const {
	return _selectedItem
		? _selectedItem->selectedText(_selectedText)
		: TextWithEntities();
}

void InnerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		cancelledSignal.notify(true);
	} else if (e == QKeySequence::Copy && _selectedItem != nullptr) {
		copySelectedText();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		SetClipboardWithEntities(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else {
		e->ignore();
	}
}

void InnerWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());
	if (((_mouseAction == MouseAction::Selecting && _selectedItem != nullptr) || (_mouseAction == MouseAction::None)) && _mouseSelectType == TextSelectType::Letters && _mouseActionItem) {
		StateRequest request;
		request.flags |= Text::StateRequest::Flag::LookupSymbol;
		auto dragState = _mouseActionItem->textState(_dragStartPosition, request);
		if (dragState.cursor == CursorState::Text) {
			_mouseTextSymbol = dragState.symbol;
			_mouseSelectType = TextSelectType::Words;
			if (_mouseAction == MouseAction::None) {
				_mouseAction = MouseAction::Selecting;
				auto selection = TextSelection { dragState.symbol, dragState.symbol };
				repaintItem(std::exchange(_selectedItem, _mouseActionItem));
				_selectedText = selection;
			}
			mouseMoveEvent(e);

			_trippleClickPoint = e->globalPos();
			_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
		}
	}
}

void InnerWidget::contextMenuEvent(QContextMenuEvent *e) {
	showContextMenu(e);
}

void InnerWidget::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	// -1 - has selection, but no over, 0 - no selection, 1 - over text
	auto isUponSelected = 0;
	auto hasSelected = 0;
	if (_selectedItem) {
		isUponSelected = -1;

		auto selFrom = _selectedText.from;
		auto selTo = _selectedText.to;
		hasSelected = (selTo > selFrom) ? 1 : 0;
		if (App::mousedItem() && App::mousedItem() == App::hoveredItem()) {
			auto mousePos = mapPointToItem(
				mapFromGlobal(_mousePosition),
				App::mousedItem());
			StateRequest request;
			request.flags |= Text::StateRequest::Flag::LookupSymbol;
			auto dragState = App::mousedItem()->textState(mousePos, request);
			if (dragState.cursor == CursorState::Text
				&& base::in_range(dragState.symbol, selFrom, selTo)) {
				isUponSelected = 1;
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_menu = base::make_unique_q<Ui::PopupMenu>(this);

	const auto link = ClickHandler::getActive();
	auto view = App::hoveredItem()
		? App::hoveredItem()
		: App::hoveredLinkItem();
	auto lnkPhoto = dynamic_cast<PhotoClickHandler*>(link.get());
	auto lnkDocument = dynamic_cast<DocumentClickHandler*>(link.get());
	auto lnkPeer = dynamic_cast<PeerClickHandler*>(link.get());
	auto lnkIsVideo = lnkDocument ? lnkDocument->document()->isVideoFile() : false;
	auto lnkIsVoice = lnkDocument ? lnkDocument->document()->isVoiceMessage() : false;
	auto lnkIsAudio = lnkDocument ? lnkDocument->document()->isAudioFile() : false;
	if (lnkPhoto || lnkDocument) {
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), [=] {
				copySelectedText();
			});
		}
		if (lnkPhoto) {
			const auto photo = lnkPhoto->photo();
			_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
				savePhotoToFile(photo);
			}));
			_menu->addAction(lang(lng_context_copy_image), [=] {
				copyContextImage(photo);
			});
		} else {
			auto document = lnkDocument->document();
			if (document->loading()) {
				_menu->addAction(lang(lng_context_cancel_download), [=] {
					cancelContextDownload(document);
				});
			} else {
				if (document->loaded() && document->isGifv()) {
					if (!cAutoPlayGif()) {
						const auto itemId = view
							? view->data()->fullId()
							: FullMsgId();
						_menu->addAction(lang(lng_context_open_gif), [=] {
							openContextGif(itemId);
						});
					}
				}
				if (!document->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
					_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), [=] {
						showContextInFolder(document);
					});
				}
				_menu->addAction(lang(lnkIsVideo ? lng_context_save_video : (lnkIsVoice ? lng_context_save_audio : (lnkIsAudio ? lng_context_save_audio_file : lng_context_save_file))), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
					saveDocumentToFile(document);
				}));
			}
		}
	} else if (lnkPeer) { // suggest to block
		if (auto user = lnkPeer->peer()->asUser()) {
			suggestRestrictUser(user);
		}
	} else { // maybe cursor on some text history item?
		const auto item = view ? view->data().get() : nullptr;
		const auto itemId = item ? item->fullId() : FullMsgId();
		bool canDelete = item && item->canDelete() && (item->id > 0 || !item->serviceMsg());
		bool canForward = item && item->allowsForward();

		auto msg = dynamic_cast<HistoryMessage*>(item);
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), [this] { copySelectedText(); });
		} else {
			if (item && !isUponSelected) {
				const auto media = view->media();
				const auto mediaHasTextForCopy = media && media->hasTextForCopy();
				if (const auto document = media ? media->getDocument() : nullptr) {
					if (document->sticker()) {
						_menu->addAction(lang(lng_context_save_image), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
							saveDocumentToFile(document);
						}));
					}
				}
				if (msg && !link && (view->hasVisibleText() || mediaHasTextForCopy)) {
					_menu->addAction(lang(lng_context_copy_text), [=] {
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
					QApplication::clipboard()->setText(text);
				});
		}
	}

	if (_menu->actions().isEmpty()) {
		_menu = nullptr;
	} else {
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void InnerWidget::savePhotoToFile(PhotoData *photo) {
	if (!photo || !photo->date || !photo->loaded()) {
		return;
	}

	auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
	FileDialog::GetWritePath(
		this,
		lang(lng_save_photo),
		filter,
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		crl::guard(this, [=](const QString &result) {
			if (!result.isEmpty()) {
				photo->full->pix(Data::FileOrigin()).toImage().save(result, "JPG");
			}
		}));
}

void InnerWidget::saveDocumentToFile(DocumentData *document) {
	DocumentSaveClickHandler::Save(Data::FileOrigin(), document, true);
}

void InnerWidget::copyContextImage(PhotoData *photo) {
	if (!photo || !photo->date || !photo->loaded()) return;

	QApplication::clipboard()->setPixmap(photo->full->pix(Data::FileOrigin()));
}

void InnerWidget::copySelectedText() {
	SetClipboardWithEntities(getSelectedText());
}

void InnerWidget::showStickerPackInfo(not_null<DocumentData*> document) {
	StickerSetBox::Show(document);
}

void InnerWidget::cancelContextDownload(not_null<DocumentData*> document) {
	document->cancel();
}

void InnerWidget::showContextInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(
		DocumentData::FilePathResolveChecked);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void InnerWidget::openContextGif(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		if (auto media = item->media()) {
			if (auto document = media->document()) {
				Messenger::Instance().showDocument(document, item);
			}
		}
	}
}

void InnerWidget::copyContextText(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		SetClipboardWithEntities(HistoryItemText(item));
	}
}

void InnerWidget::suggestRestrictUser(not_null<UserData*> user) {
	Expects(_menu != nullptr);

	if (!_channel->isMegagroup() || !_channel->canBanMembers() || _admins.empty()) {
		return;
	}
	if (base::contains(_admins, user)) {
		if (!base::contains(_adminsCanEdit, user)) {
			return;
		}
	}
	_menu->addAction(lang(lng_context_restrict_user), [=] {
		auto editRestrictions = [=](bool hasAdminRights, const MTPChannelBannedRights &currentRights) {
			auto weak = QPointer<InnerWidget>(this);
			auto weakBox = std::make_shared<QPointer<EditRestrictedBox>>();
			auto box = Box<EditRestrictedBox>(_channel, user, hasAdminRights, currentRights);
			box->setSaveCallback([user, weak, weakBox](const MTPChannelBannedRights &oldRights, const MTPChannelBannedRights &newRights) {
				if (weak) {
					weak->restrictUser(user, oldRights, newRights);
				}
				if (*weakBox) {
					(*weakBox)->closeBox();
				}
			});
			*weakBox = Ui::show(
				std::move(box),
				LayerOption::KeepOther);
		};
		if (base::contains(_admins, user)) {
			editRestrictions(true, MTP_channelBannedRights(MTP_flags(0), MTP_int(0)));
		} else {
			request(MTPchannels_GetParticipant(_channel->inputChannel, user->inputUser)).done([=](const MTPchannels_ChannelParticipant &result) {
				Expects(result.type() == mtpc_channels_channelParticipant);

				auto &participant = result.c_channels_channelParticipant();
				App::feedUsers(participant.vusers);
				auto type = participant.vparticipant.type();
				if (type == mtpc_channelParticipantBanned) {
					auto &banned = participant.vparticipant.c_channelParticipantBanned();
					editRestrictions(false, banned.vbanned_rights);
				} else {
					auto hasAdminRights = (type == mtpc_channelParticipantAdmin)
						|| (type == mtpc_channelParticipantCreator);
					auto bannedRights = MTP_channelBannedRights(
						MTP_flags(0),
						MTP_int(0));
					editRestrictions(hasAdminRights, bannedRights);
				}
			}).fail([=](const RPCError &error) {
				auto bannedRights = MTP_channelBannedRights(
					MTP_flags(0),
					MTP_int(0));
				editRestrictions(false, bannedRights);
			}).send();
		}
	});
}

void InnerWidget::restrictUser(not_null<UserData*> user, const MTPChannelBannedRights &oldRights, const MTPChannelBannedRights &newRights) {
	auto weak = QPointer<InnerWidget>(this);
	MTP::send(MTPchannels_EditBanned(_channel->inputChannel, user->inputUser, newRights), rpcDone([megagroup = _channel.get(), user, weak, oldRights, newRights](const MTPUpdates &result) {
		Auth().api().applyUpdates(result);
		megagroup->applyEditBanned(user, oldRights, newRights);
		if (weak) {
			weak->restrictUserDone(user, newRights);
		}
	}));
}

void InnerWidget::restrictUserDone(not_null<UserData*> user, const MTPChannelBannedRights &rights) {
	Expects(rights.type() == mtpc_channelBannedRights);
	if (rights.c_channelBannedRights().vflags.v) {
		_admins.erase(std::remove(_admins.begin(), _admins.end(), user), _admins.end());
		_adminsCanEdit.erase(std::remove(_adminsCanEdit.begin(), _adminsCanEdit.end(), user), _adminsCanEdit.end());
	}
	_downLoaded = false;
	checkPreloadMore();
}

void InnerWidget::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
}

void InnerWidget::mouseMoveEvent(QMouseEvent *e) {
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _mouseAction != MouseAction::None) {
		mouseReleaseEvent(e);
	}
	mouseActionUpdate(e->globalPos());
}

void InnerWidget::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void InnerWidget::enterEventHook(QEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return TWidget::enterEventHook(e);
}

void InnerWidget::leaveEventHook(QEvent *e) {
	if (const auto view = App::hoveredItem()) {
		repaintItem(view);
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

void InnerWidget::mouseActionStart(const QPoint &screenPos, Qt::MouseButton button) {
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
	_dragStartPosition = mapPointToItem(
		mapFromGlobal(screenPos),
		_mouseActionItem);
	_pressWasInactive = _controller->window()->wasInactivePress();
	if (_pressWasInactive) _controller->window()->setInactivePress(false);

	if (ClickHandler::getPressed()) {
		_mouseAction = MouseAction::PrepareDrag;
	}
	if (_mouseAction == MouseAction::None && _mouseActionItem) {
		TextState dragState;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			StateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->textState(_dragStartPosition, request);
			if (dragState.cursor == CursorState::Text) {
				auto selection = TextSelection { dragState.symbol, dragState.symbol };
				repaintItem(std::exchange(_selectedItem, _mouseActionItem));
				_selectedText = selection;
				_mouseTextSymbol = dragState.symbol;
				_mouseAction = MouseAction::Selecting;
				_mouseSelectType = TextSelectType::Paragraphs;
				mouseActionUpdate(_mousePosition);
				_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
			}
		} else if (App::pressedItem()) {
			StateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->textState(_dragStartPosition, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (App::pressedItem()) {
				_mouseTextSymbol = dragState.symbol;
				auto uponSelected = (dragState.cursor == CursorState::Text);
				if (uponSelected) {
					if (!_selectedItem || _selectedItem != _mouseActionItem) {
						uponSelected = false;
					} else if (_mouseTextSymbol < _selectedText.from || _mouseTextSymbol >= _selectedText.to) {
						uponSelected = false;
					}
				}
				if (uponSelected) {
					_mouseAction = MouseAction::PrepareDrag; // start text drag
				} else if (!_pressWasInactive) {
					if (dragState.afterSymbol) ++_mouseTextSymbol;
					auto selection = TextSelection { _mouseTextSymbol, _mouseTextSymbol };
					repaintItem(std::exchange(_selectedItem, _mouseActionItem));
					_selectedText = selection;
					_mouseAction = MouseAction::Selecting;
					repaintItem(_mouseActionItem);
				}
			}
		}
	}

	if (!_mouseActionItem) {
		_mouseAction = MouseAction::None;
	} else if (_mouseAction == MouseAction::None) {
		_mouseActionItem = nullptr;
	}
}

void InnerWidget::mouseActionUpdate(const QPoint &screenPos) {
	_mousePosition = screenPos;
	updateSelected();
}

void InnerWidget::mouseActionCancel() {
	_mouseActionItem = nullptr;
	_mouseAction = MouseAction::None;
	_dragStartPosition = QPoint(0, 0);
	_wasSelectedText = false;
	//_widget->noSelectingScroll(); // TODO
}

void InnerWidget::mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);

	auto activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging) {
		activated = nullptr;
	}
	if (const auto view = App::pressedItem()) {
		repaintItem(view);
		App::pressedItem(nullptr);
	}

	_wasSelectedText = false;

	if (activated) {
		mouseActionCancel();
		App::activateClickHandler(activated, button);
		return;
	}
	if (_mouseAction == MouseAction::PrepareDrag && !_pressWasInactive && button != Qt::RightButton) {
		repaintItem(base::take(_selectedItem));
	} else if (_mouseAction == MouseAction::Selecting) {
		if (_selectedItem && !_pressWasInactive) {
			if (_selectedText.from == _selectedText.to) {
				_selectedItem = nullptr;
				App::wnd()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseActionItem = nullptr;
	_mouseSelectType = TextSelectType::Letters;
	//_widget->noSelectingScroll(); // TODO

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (_selectedItem && _selectedText.from != _selectedText.to) {
		SetClipboardWithEntities(
			_selectedItem->selectedText(_selectedText),
			QClipboard::Selection);
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void InnerWidget::updateSelected() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(snap(mousePosition.x(), 0, width()), snap(mousePosition.y(), _visibleTop, _visibleBottom));

	auto itemPoint = QPoint();
	auto begin = std::rbegin(_items), end = std::rend(_items);
	auto from = (point.y() >= _itemsTop && point.y() < _itemsTop + _itemsHeight)
		? std::lower_bound(begin, end, point.y(), [this](auto &elem, int top) {
			return this->itemTop(elem) + elem->height() <= top;
		})
		: end;
	const auto view = (from != end) ? from->get() : nullptr;
	const auto item = view ? view->data().get() : nullptr;
	if (item) {
		App::mousedItem(view);
		itemPoint = mapPointToItem(point, view);
		if (view->pointState(itemPoint) != PointState::Outside) {
			if (App::hoveredItem() != view) {
				repaintItem(App::hoveredItem());
				App::hoveredItem(view);
				repaintItem(view);
			}
		} else if (const auto view = App::hoveredItem()) {
			repaintItem(view);
			App::hoveredItem(nullptr);
		}
	}

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto selectingText = _selectedItem
		&& (view == _mouseActionItem)
		&& (view == App::hoveredItem());
	if (view) {
		if (view != _mouseActionItem || (itemPoint - _dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [this] { performDrag(); });
			}
		}
		StateRequest request;
		if (_mouseAction == MouseAction::Selecting) {
			request.flags |= Text::StateRequest::Flag::LookupSymbol;
		} else {
			selectingText = false;
		}
		dragState = view->textState(itemPoint, request);
		lnkhost = view;
		if (!dragState.link && itemPoint.x() >= st::historyPhotoLeft && itemPoint.x() < st::historyPhotoLeft + st::msgPhotoSize) {
			if (auto message = item->toHistoryMessage()) {
				if (view->hasFromPhoto()) {
					enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
						// stop enumeration if the userpic is below our point
						if (userpicTop > point.y()) {
							return false;
						}

						// stop enumeration if we've found a userpic under the cursor
						if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
							const auto message = view->data()->toHistoryMessage();
							Assert(message != nullptr);

							dragState.link = message->from()->openLink();
							lnkhost = view;
							return false;
						}
						return true;
					});
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
		|| dragState.cursor == CursorState::Forwarded) {
		Ui::Tooltip::Show(1000, this);
	}

	auto cursor = style::cur_default;
	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		if (dragState.link) {
			cursor = style::cur_pointer;
		} else if (_mouseCursorState == CursorState::Text) {
			cursor = style::cur_text;
		} else if (_mouseCursorState == CursorState::Date) {
//			cursor = style::cur_cross;
		}
	} else if (item) {
		if (_mouseAction == MouseAction::Selecting) {
			if (selectingText) {
				auto second = dragState.symbol;
				if (dragState.afterSymbol && _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selection = TextSelection { qMin(second, _mouseTextSymbol), qMax(second, _mouseTextSymbol) };
				if (_mouseSelectType != TextSelectType::Letters) {
					selection = _mouseActionItem->adjustSelection(
						selection,
						_mouseSelectType);
				}
				if (_selectedText != selection) {
					_selectedText = selection;
					repaintItem(_mouseActionItem);
				}
				if (!_wasSelectedText && (selection.from != selection.to)) {
					_wasSelectedText = true;
					setFocus();
				}
			}
		} else if (_mouseAction == MouseAction::Dragging) {
		}

		if (ClickHandler::getPressed()) {
			cursor = style::cur_pointer;
		} else if (_mouseAction == MouseAction::Selecting && _selectedItem) {
			cursor = style::cur_text;
		}
	}

	// Voice message seek support.
	if (const auto pressedView = App::pressedLinkItem()) {
		const auto adjustedPoint = mapPointToItem(point, pressedView);
		pressedView->updatePressed(adjustedPoint);
	}

	//if (_mouseAction == MouseAction::Selecting) {
	//	_widget->checkSelectingScroll(mousePos);
	//} else {
	//	_widget->noSelectingScroll();
	//} // TODO

	if (_mouseAction == MouseAction::None && (lnkChanged || cursor != _cursor)) {
		setCursor(_cursor = cursor);
	}
}

void InnerWidget::performDrag() {
	if (_mouseAction != MouseAction::Dragging) return;

	auto uponSelected = false;
	//if (_mouseActionItem) {
	//	if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
	//		uponSelected = _selected.contains(_mouseActionItem);
	//	} else {
	//		StateRequest request;
	//		request.flags |= Text::StateRequest::Flag::LookupSymbol;
	//		auto dragState = _mouseActionItem->textState(_dragStartPosition.x(), _dragStartPosition.y(), request);
	//		uponSelected = (dragState.cursor == CursorState::Text);
	//		if (uponSelected) {
	//			if (_selected.isEmpty() ||
	//				_selected.cbegin().value() == FullSelection ||
	//				_selected.cbegin().key() != _mouseActionItem
	//				) {
	//				uponSelected = false;
	//			} else {
	//				uint16 selFrom = _selected.cbegin().value().from, selTo = _selected.cbegin().value().to;
	//				if (dragState.symbol < selFrom || dragState.symbol >= selTo) {
	//					uponSelected = false;
	//				}
	//			}
	//		}
	//	}
	//}
	//auto pressedHandler = ClickHandler::getPressed();

	//if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.data())) {
	//	return;
	//}

	//TextWithEntities sel;
	//QList<QUrl> urls;
	//if (uponSelected) {
	//	sel = getSelectedText();
	//} else if (pressedHandler) {
	//	sel = { pressedHandler->dragText(), EntitiesInText() };
	//	//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
	//	//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
	//	//}
	//}
	//if (auto mimeData = mimeDataFromTextWithEntities(sel)) {
	//	updateDragSelection(0, 0, false);
	//	_widget->noSelectingScroll();

	//	if (!urls.isEmpty()) mimeData->setUrls(urls);
	//	if (uponSelected && !Adaptive::OneColumn()) {
	//		auto selectedState = getSelectionState();
	//		if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
	//			Auth().data().setMimeForwardIds(getSelectedItems());
	//			mimeData->setData(qsl("application/x-td-forward"), "1");
	//		}
	//	}
	//	_controller->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryMedia*>(nullptr);
	//	if (auto pressedItem = App::pressedItem()) {
	//		pressedMedia = pressedItem->media();
	//		if (_mouseCursorState == CursorState::Date
	//			|| (pressedMedia && pressedMedia->dragItem())) {
	//			forwardMimeType = qsl("application/x-td-forward");
	//			Auth().data().setMimeForwardIds(
	//				Auth().data().itemOrItsGroup(pressedItem->data()));
	//		}
	//	}
	//	if (auto pressedLnkItem = App::pressedLinkItem()) {
	//		if ((pressedMedia = pressedLnkItem->media())) {
	//			if (forwardMimeType.isEmpty()
	//				&& pressedMedia->dragItemByHandler(pressedHandler)) {
	//				forwardMimeType = qsl("application/x-td-forward");
	//				Auth().data().setMimeForwardIds(
	//					{ 1, pressedLnkItem->fullId() });
	//			}
	//		}
	//	}
	//	if (!forwardMimeType.isEmpty()) {
	//		auto mimeData = std::make_unique<QMimeData>();
	//		mimeData->setData(forwardMimeType, "1");
	//		if (auto document = (pressedMedia ? pressedMedia->getDocument() : nullptr)) {
	//			auto filepath = document->filepath(DocumentData::FilePathResolveChecked);
	//			if (!filepath.isEmpty()) {
	//				QList<QUrl> urls;
	//				urls.push_back(QUrl::fromLocalFile(filepath));
	//				mimeData->setUrls(urls);
	//			}
	//		}

	//		// This call enters event loop and can destroy any QObject.
	//		_controller->window()->launchDrag(std::move(mimeData));
	//		return;
	//	}
	//} // TODO
}

int InnerWidget::itemTop(not_null<const Element*> view) const {
	return _itemsTop + view->y();
}

void InnerWidget::repaintItem(const Element *view) {
	if (!view) {
		return;
	}
	update(0, itemTop(view), width(), view->height());
}

void InnerWidget::resizeItem(not_null<Element*> view) {
	updateSize();
}

void InnerWidget::refreshItem(not_null<const Element*> view) {
	// No need to refresh views in admin log.
}

QPoint InnerWidget::mapPointToItem(QPoint point, const Element *view) const {
	if (!view) {
		return QPoint();
	}
	return point - QPoint(0, itemTop(view));
}

InnerWidget::~InnerWidget() = default;

} // namespace AdminLog
