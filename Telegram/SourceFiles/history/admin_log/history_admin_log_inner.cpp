/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_inner.h"

#include "history/history.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
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
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/application.h"
#include "apiwrap.h"
#include "api/api_attached_stickers.h"
#include "layout.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/inactive_press.h"
#include "ui/effects/path_shift_gradient.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_cloud_file.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "facades.h"
#include "app.h"
#include "styles/style_chat.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace AdminLog {
namespace {

// If we require to support more admins we'll have to rewrite this anyway.
constexpr auto kMaxChannelAdmins = 200;
constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kEventsFirstPage = 20;
constexpr auto kEventsPerPage = 50;
constexpr auto kClearUserpicsAfter = 50;

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
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel)
: RpWidget(parent)
, _controller(controller)
, _channel(channel)
, _history(channel->owner().history(channel))
, _api(&_channel->session().mtp())
, _pathGradient(HistoryView::MakePathShiftGradient([=] { update(); }))
, _scrollDateCheck([=] { scrollDateCheck(); })
, _emptyText(
		st::historyAdminLogEmptyWidth
		- st::historyAdminLogEmptyPadding.left()
		- st::historyAdminLogEmptyPadding.left()) {
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([=] { scrollDateHideByTimer(); });
	session().data().viewRepaintRequest(
	) | rpl::start_with_next([=](auto view) {
		if (view->delegate() == this) {
			repaintItem(view);
		}
	}, lifetime());
	session().data().viewResizeRequest(
	) | rpl::start_with_next([=](auto view) {
		if (view->delegate() == this) {
			resizeItem(view);
		}
	}, lifetime());
	session().data().itemViewRefreshRequest(
	) | rpl::start_with_next([=](auto item) {
		if (const auto view = viewForItem(item)) {
			refreshItem(view);
		}
	}, lifetime());
	session().data().viewLayoutChanged(
	) | rpl::start_with_next([=](auto view) {
		if (view->delegate() == this) {
			if (view->isUnderCursor()) {
				updateSelected();
			}
		}
	}, lifetime());
	session().data().animationPlayInlineRequest(
	) | rpl::start_with_next([=](auto item) {
		if (const auto view = viewForItem(item)) {
			if (const auto media = view->media()) {
				media->playAnimation();
			}
		}
	}, lifetime());
	subscribe(session().data().queryItemVisibility(), [=](
			const Data::Session::ItemVisibilityQuery &query) {
		if (_history != query.item->history()
			|| !query.item->isAdminLogEntry()
			|| !isVisible()) {
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

Main::Session &InnerWidget::session() const {
	return _controller->session();
}

rpl::producer<> InnerWidget::showSearchSignal() const {
	return _showSearchSignal.events();
}

rpl::producer<int> InnerWidget::scrollToSignal() const {
	return _scrollToSignal.events();
}

rpl::producer<> InnerWidget::cancelSignal() const {
	return _cancelSignal.events();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto scrolledUp = (visibleTop < _visibleTop);
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	// Unload userpics.
	if (_userpics.size() > kClearUserpicsAfter) {
		_userpicsCache = std::move(_userpics);
	}

	updateVisibleTopItem();
	checkPreloadMore();
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		scrollDateHideByTimer();
	}
	_controller->floatPlayerAreaUpdated();
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
	_api.request(MTPchannels_GetParticipants(
		_channel->inputChannel,
		MTP_channelParticipantsAdmins(),
		MTP_int(0),
		MTP_int(kMaxChannelAdmins),
		MTP_int(participantsHash)
	)).done([this](const MTPchannels_ChannelParticipants &result) {
		session().api().parseChannelParticipants(_channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			auto filtered = (
				list
			) | ranges::views::transform([&](const MTPChannelParticipant &p) {
				const auto participantId = p.match([](
						const MTPDchannelParticipantBanned &data) {
					return peerFromMTP(data.vpeer());
				}, [](const MTPDchannelParticipantLeft &data) {
					return peerFromMTP(data.vpeer());
				}, [](const auto &data) {
					return peerFromUser(data.vuser_id());
				});
				const auto canEdit = p.match([](
						const MTPDchannelParticipantAdmin &data) {
					return data.is_can_edit();
				}, [](const auto &) {
					return false;
				});
				return std::make_pair(participantId, canEdit);
			}) | ranges::views::transform([&](auto &&pair) {
				return std::make_pair(
					(peerIsUser(pair.first)
						? session().data().userLoaded(
							peerToUser(pair.first))
						: nullptr),
					pair.second);
			}) | ranges::views::filter([&](auto &&pair) {
				return (pair.first != nullptr);
			});

			for (auto [user, canEdit] : filtered) {
				_admins.emplace_back(user);
				if (canEdit) {
					_adminsCanEdit.emplace_back(user);
				}
			}
		});
		if (_admins.empty()) {
			_admins.push_back(session().user());
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
		_controller->show(
			Box<FilterBox>(_channel, _admins, _filter, std::move(callback)));
	}
}

void InnerWidget::clearAndRequestLog() {
	_api.request(base::take(_preloadUpRequestId)).cancel();
	_api.request(base::take(_preloadDownRequestId)).cancel();
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
	auto text = Ui::Text::Semibold((hasSearch || hasFilter)
		? tr::lng_admin_log_no_results_title(tr::now)
		: tr::lng_admin_log_no_events_title(tr::now));
	auto description = hasSearch
		? tr::lng_admin_log_no_results_search_text(
			tr::now,
			lt_query,
			TextUtilities::Clean(_searchQuery))
		: hasFilter
		? tr::lng_admin_log_no_results_text(tr::now)
		: _channel->isMegagroup()
		? tr::lng_admin_log_no_events_text(tr::now)
		: tr::lng_admin_log_no_events_text_channel(tr::now);
	text.text.append(qstr("\n\n") + description);
	_emptyText.setMarkedText(st::defaultTextStyle, text, options);
}

QString InnerWidget::tooltipText() const {
	if (_mouseCursorState == CursorState::Date
		&& _mouseAction == MouseAction::None) {
		if (const auto view = App::hoveredItem()) {
			const auto format = QLocale::system().dateTimeFormat(
				QLocale::LongFormat);
			auto dateText = HistoryView::DateTooltipText(view);

			const auto sentIt = _itemDates.find(view->data());
			if (sentIt != end(_itemDates)) {
				dateText += '\n' + tr::lng_sent_date(
					tr::now,
					lt_date,
					base::unixtime::parse(sentIt->second).toString(format));
			}
			return dateText;
		}
	} else if (_mouseCursorState == CursorState::Forwarded
		&& _mouseAction == MouseAction::None) {
		if (const auto view = App::hoveredItem()) {
			if (const auto forwarded = view->data()->Get<HistoryMessageForwarded>()) {
				return forwarded->text.toString();
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

bool InnerWidget::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

HistoryView::Context InnerWidget::elementContext() {
	return HistoryView::Context::AdminLog;
}

std::unique_ptr<HistoryView::Element> InnerWidget::elementCreate(
		not_null<HistoryMessage*> message,
		Element *replacing) {
	return std::make_unique<HistoryView::Message>(this, message, replacing);
}

std::unique_ptr<HistoryView::Element> InnerWidget::elementCreate(
		not_null<HistoryService*> message,
		Element *replacing) {
	return std::make_unique<HistoryView::Service>(this, message, replacing);
}

bool InnerWidget::elementUnderCursor(
		not_null<const HistoryView::Element*> view) {
	return (App::hoveredItem() == view);
}

crl::time InnerWidget::elementHighlightTime(
		not_null<const HistoryItem*> item) {
	return crl::time(0);
}

bool InnerWidget::elementInSelectionMode() {
	return false;
}

bool InnerWidget::elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) {
	Expects(view->delegate() == this);

	const auto top = itemTop(view);
	const auto bottom = top + view->height();
	return (top < till && bottom > from);
}

void InnerWidget::elementStartStickerLoop(not_null<const Element*> view) {
}

void InnerWidget::elementShowPollResults(
	not_null<PollData*> poll,
	FullMsgId context) {
}

void InnerWidget::elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	_controller->openPhoto(photo, context);
}

void InnerWidget::elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	_controller->openDocument(document, context, showInMediaView);
}

void InnerWidget::elementCancelUpload(const FullMsgId &context) {
	if (const auto item = session().data().message(context)) {
		_controller->cancelUploadLayer(item);
	}
}

void InnerWidget::elementShowTooltip(
	const TextWithEntities &text,
	Fn<void()> hiddenCallback) {
}

bool InnerWidget::elementIsGifPaused() {
	return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

bool InnerWidget::elementHideReply(not_null<const Element*> view) {
	return true;
}

bool InnerWidget::elementShownUnread(not_null<const Element*> view) {
	return view->data()->unread();
}

void InnerWidget::elementSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void InnerWidget::elementHandleViaClick(not_null<UserData*> bot) {
}

bool InnerWidget::elementIsChatWide() {
	return _controller->adaptive().isChatWide();
}

not_null<Ui::PathShiftGradient*> InnerWidget::elementPathShiftGradient() {
	return _pathGradient.get();
}

void InnerWidget::saveState(not_null<SectionMemento*> memento) {
	memento->setFilter(std::move(_filter));
	memento->setAdmins(std::move(_admins));
	memento->setAdminsCanEdit(std::move(_adminsCanEdit));
	memento->setSearchQuery(std::move(_searchQuery));
	if (!_filterChanged) {
		for (auto &item : _items) {
			item.clearView();
		}
		memento->setItems(
			base::take(_items),
			base::take(_eventIds),
			_upLoaded,
			_downLoaded);
		base::take(_itemsByData);
	}
	_upLoaded = _downLoaded = true; // Don't load or handle anything anymore.
}

void InnerWidget::restoreState(not_null<SectionMemento*> memento) {
	_items = memento->takeItems();
	for (auto &item : _items) {
		item.refreshView(this);
		_itemsByData.emplace(item->data(), item.get());
	}
	_eventIds = memento->takeEventIds();
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
	requestId = _api.request(MTPchannels_GetAdminLog(
		MTP_flags(flags),
		_channel->inputChannel,
		MTP_string(_searchQuery),
		filter,
		MTP_vector<MTPInputUser>(admins),
		MTP_long(maxId),
		MTP_long(minId),
		MTP_int(perPage)
	)).done([=, &requestId, &loadedFlag](const MTPchannels_AdminLogResults &result) {
		Expects(result.type() == mtpc_channels_adminLogResults);

		requestId = 0;

		auto &results = result.c_channels_adminLogResults();
		_channel->owner().processUsers(results.vusers());
		_channel->owner().processChats(results.vchats());
		if (!loadedFlag) {
			addEvents(direction, results.vevents().v);
		}
	}).fail([this, &requestId, &loadedFlag](const MTP::Error &error) {
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
	auto &addToItems = (direction == Direction::Up)
		? _items
		: newItemsForDownDirection;
	addToItems.reserve(oldItemsCount + events.size() * 2);
	for (const auto &event : events) {
		event.match([&](const MTPDchannelAdminLogEvent &data) {
			const auto id = data.vid().v;
			if (_eventIds.find(id) != _eventIds.end()) {
				return;
			}

			auto count = 0;
			const auto addOne = [&](OwnedItem item, TimeId sentDate) {
				if (sentDate) {
					_itemDates.emplace(item->data(), sentDate);
				}
				_eventIds.emplace(id);
				_itemsByData.emplace(item->data(), item.get());
				addToItems.push_back(std::move(item));
				++count;
			};
			GenerateItems(
				this,
				_history,
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
		});
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
	if (_eventIds.empty() || _filterChanged) {
		_maxId = _minId = 0;
	} else {
		_maxId = *_eventIds.rbegin();
		_minId = *_eventIds.begin();
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
	for (const auto &item : ranges::views::reverse(_items)) {
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
	const auto newVisibleTop = _visibleTopItem
		? (itemTop(_visibleTopItem) + _visibleTopFromItem)
		: ScrollMax;
	_scrollToSignal.fire_copy(newVisibleTop);
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	const auto guard = gsl::finally([&] {
		_userpicsCache.clear();
	});

	Painter p(this);

	auto ms = crl::now();
	auto clip = e->rect();

	if (_items.empty() && _upLoaded && _downLoaded) {
		paintEmpty(p);
	} else {
		_pathGradient->startFrame(
			0,
			width(),
			std::min(st::msgMaxWidth / 2, width() / 2));

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

					const auto from = message->from();
					from->paintUserpicLeft(
						p,
						_userpics[from],
						st::historyPhotoLeft,
						userpicTop,
						view->width(),
						st::msgPhotoSize);
				}
				return true;
			});

			auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
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
						const auto dateY = /*noFloatingDate ? itemtop :*/ (dateTop - st::msgServiceMargin.top());
						const auto width = view->width();
						const auto chatWide =
							_controller->adaptive().isChatWide();
						if (const auto date = view->Get<HistoryView::DateBadge>()) {
							date->paint(p, dateY, width, chatWide);
						} else {
							HistoryView::ServiceMessagePainter::paintDate(
								p,
								view->dateTime(),
								dateY,
								width,
								chatWide);
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
	_eventIds.clear();
	_itemsByData.clear();
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

TextForMimeData InnerWidget::getSelectedText() const {
	return _selectedItem
		? _selectedItem->selectedText(_selectedText)
		: TextForMimeData();
}

void InnerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		_cancelSignal.fire({});
	} else if (e == QKeySequence::Copy && _selectedItem != nullptr) {
		copySelectedText();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		TextUtilities::SetClipboardText(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else {
		e->ignore();
	}
}

void InnerWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());
	if (((_mouseAction == MouseAction::Selecting && _selectedItem != nullptr) || (_mouseAction == MouseAction::None)) && _mouseSelectType == TextSelectType::Letters && _mouseActionItem) {
		StateRequest request;
		request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
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
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
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
			_menu->addAction(tr::lng_context_copy_selected(tr::now), [=] {
				copySelectedText();
			});
		}
		if (lnkPhoto) {
			const auto photo = lnkPhoto->photo();
			const auto media = photo->activeMediaView();
			if (!photo->isNull() && media && media->loaded()) {
				_menu->addAction(tr::lng_context_save_image(tr::now), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
					savePhotoToFile(photo);
				}));
				_menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
					copyContextImage(photo);
				});
			}
			if (photo->hasAttachedStickers()) {
				const auto controller = _controller;
				auto callback = [=] {
					auto &attached = session().api().attachedStickers();
					attached.requestAttachedStickerSets(controller, photo);
				};
				_menu->addAction(
					tr::lng_context_attached_stickers(tr::now),
					std::move(callback));
			}
		} else {
			auto document = lnkDocument->document();
			if (document->loading()) {
				_menu->addAction(tr::lng_context_cancel_download(tr::now), [=] {
					cancelContextDownload(document);
				});
			} else {
				const auto itemId = view
					? view->data()->fullId()
					: FullMsgId();
				if (const auto item = document->session().data().message(itemId)) {
					const auto notAutoplayedGif = [&] {
						return document->isGifv()
							&& !Data::AutoDownload::ShouldAutoPlay(
								document->session().settings().autoDownload(),
								item->history()->peer,
								document);
					}();
					if (notAutoplayedGif) {
						_menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
							openContextGif(itemId);
						});
					}
				}
				if (!document->filepath(true).isEmpty()) {
					_menu->addAction(Platform::IsMac() ? tr::lng_context_show_in_finder(tr::now) : tr::lng_context_show_in_folder(tr::now), [=] {
						showContextInFolder(document);
					});
				}
				_menu->addAction(lnkIsVideo ? tr::lng_context_save_video(tr::now) : (lnkIsVoice ?  tr::lng_context_save_audio(tr::now) : (lnkIsAudio ?  tr::lng_context_save_audio_file(tr::now) :  tr::lng_context_save_file(tr::now))), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
					saveDocumentToFile(document);
				}));
				if (document->hasAttachedStickers()) {
					const auto controller = _controller;
					auto callback = [=, doc = document] {
						auto &attached = session().api().attachedStickers();
						attached.requestAttachedStickerSets(controller, doc);
					};
					_menu->addAction(
						tr::lng_context_attached_stickers(tr::now),
						std::move(callback));
				}
			}
		}
	} else if (lnkPeer) { // suggest to block
		if (auto user = lnkPeer->peer()->asUser()) {
			suggestRestrictUser(user);
		}
	} else { // maybe cursor on some text history item?
		const auto item = view ? view->data().get() : nullptr;
		const auto itemId = item ? item->fullId() : FullMsgId();

		auto msg = dynamic_cast<HistoryMessage*>(item);
		if (isUponSelected > 0) {
			_menu->addAction(tr::lng_context_copy_selected(tr::now), [this] { copySelectedText(); });
		} else {
			if (item && !isUponSelected) {
				const auto media = view->media();
				const auto mediaHasTextForCopy = media && media->hasTextForCopy();
				if (const auto document = media ? media->getDocument() : nullptr) {
					if (document->sticker()) {
						_menu->addAction(tr::lng_context_save_image(tr::now), App::LambdaDelayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
							saveDocumentToFile(document);
						}));
					}
				}
				if (msg && !link && (view->hasVisibleText() || mediaHasTextForCopy)) {
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
		}
	}

	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void InnerWidget::savePhotoToFile(not_null<PhotoData*> photo) {
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
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		crl::guard(this, [=](const QString &result) {
			if (!result.isEmpty()) {
				image.save(result, "JPG");
			}
		}));
}

void InnerWidget::saveDocumentToFile(not_null<DocumentData*> document) {
	DocumentSaveClickHandler::Save(
		Data::FileOrigin(),
		document,
		DocumentSaveClickHandler::Mode::ToNewFile);
}

void InnerWidget::copyContextImage(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	const auto image = media->image(Data::PhotoSize::Large)->original();
	QGuiApplication::clipboard()->setImage(image);
}

void InnerWidget::copySelectedText() {
	TextUtilities::SetClipboardText(getSelectedText());
}

void InnerWidget::showStickerPackInfo(not_null<DocumentData*> document) {
	StickerSetBox::Show(_controller, document);
}

void InnerWidget::cancelContextDownload(not_null<DocumentData*> document) {
	document->cancel();
}

void InnerWidget::showContextInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void InnerWidget::openContextGif(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				_controller->openDocument(document, itemId, true);
			}
		}
	}
}

void InnerWidget::copyContextText(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		TextUtilities::SetClipboardText(HistoryItemText(item));
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
	_menu->addAction(tr::lng_context_restrict_user(tr::now), [=] {
		auto editRestrictions = [=](bool hasAdminRights, ChatRestrictionsInfo currentRights) {
			auto weak = QPointer<InnerWidget>(this);
			auto weakBox = std::make_shared<QPointer<EditRestrictedBox>>();
			auto box = Box<EditRestrictedBox>(_channel, user, hasAdminRights, currentRights);
			box->setSaveCallback([=](
					ChatRestrictionsInfo oldRights,
					ChatRestrictionsInfo newRights) {
				if (weak) {
					weak->restrictUser(user, oldRights, newRights);
				}
				if (*weakBox) {
					(*weakBox)->closeBox();
				}
			});
			*weakBox = QPointer<EditRestrictedBox>(box.data());
			_controller->show(
				std::move(box),
				Ui::LayerOption::KeepOther);
		};
		if (base::contains(_admins, user)) {
			editRestrictions(true, ChatRestrictionsInfo());
		} else {
			_api.request(MTPchannels_GetParticipant(
				_channel->inputChannel,
				user->input
			)).done([=](const MTPchannels_ChannelParticipant &result) {
				Expects(result.type() == mtpc_channels_channelParticipant);

				auto &participant = result.c_channels_channelParticipant();
				_channel->owner().processUsers(participant.vusers());
				auto type = participant.vparticipant().type();
				if (type == mtpc_channelParticipantBanned) {
					auto &banned = participant.vparticipant().c_channelParticipantBanned();
					editRestrictions(
						false,
						ChatRestrictionsInfo(banned.vbanned_rights()));
				} else {
					auto hasAdminRights = (type == mtpc_channelParticipantAdmin)
						|| (type == mtpc_channelParticipantCreator);
					editRestrictions(hasAdminRights, ChatRestrictionsInfo());
				}
			}).fail([=](const MTP::Error &error) {
				editRestrictions(false, ChatRestrictionsInfo());
			}).send();
		}
	});
}

void InnerWidget::restrictUser(
		not_null<UserData*> user,
		ChatRestrictionsInfo oldRights,
		ChatRestrictionsInfo newRights) {
	const auto done = [=](ChatRestrictionsInfo newRights) {
		restrictUserDone(user, newRights);
	};
	const auto callback = SaveRestrictedCallback(
		_channel,
		user,
		crl::guard(this, done),
		nullptr);
	callback(oldRights, newRights);
}

void InnerWidget::restrictUserDone(
		not_null<UserData*> user,
		ChatRestrictionsInfo rights) {
	if (rights.flags) {
		_admins.erase(
			std::remove(_admins.begin(), _admins.end(), user),
			_admins.end());
		_adminsCanEdit.erase(
			std::remove(_adminsCanEdit.begin(), _adminsCanEdit.end(), user),
			_adminsCanEdit.end());
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
	_pressWasInactive = Ui::WasInactivePress(_controller->widget());
	if (_pressWasInactive) {
		Ui::MarkInactivePress(_controller->widget(), false);
	}

	if (ClickHandler::getPressed()) {
		_mouseAction = MouseAction::PrepareDrag;
	}
	if (_mouseAction == MouseAction::None && _mouseActionItem) {
		TextState dragState;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
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
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
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
		ActivateClickHandler(window(), activated, button);
		return;
	}
	if (_mouseAction == MouseAction::PrepareDrag && !_pressWasInactive && button != Qt::RightButton) {
		repaintItem(base::take(_selectedItem));
	} else if (_mouseAction == MouseAction::Selecting) {
		if (_selectedItem && !_pressWasInactive) {
			if (_selectedText.from == _selectedText.to) {
				_selectedItem = nullptr;
				_controller->widget()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseActionItem = nullptr;
	_mouseSelectType = TextSelectType::Letters;
	//_widget->noSelectingScroll(); // TODO

	if (QGuiApplication::clipboard()->supportsSelection()
		&& _selectedItem
		&& _selectedText.from != _selectedText.to) {
		TextUtilities::SetClipboardText(
			_selectedItem->selectedText(_selectedText),
			QClipboard::Selection);
	}
}

void InnerWidget::updateSelected() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(
		std::clamp(mousePosition.x(), 0, width()),
		std::clamp(mousePosition.y(), _visibleTop, _visibleBottom));

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
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
		} else {
			selectingText = false;
		}
		dragState = view->textState(itemPoint, request);
		lnkhost = view;
		if (!dragState.link && itemPoint.x() >= st::historyPhotoLeft && itemPoint.x() < st::historyPhotoLeft + st::msgPhotoSize) {
			if (item->toHistoryMessage()) {
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

	//auto uponSelected = false;
	//if (_mouseActionItem) {
	//	if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
	//		uponSelected = _selected.contains(_mouseActionItem);
	//	} else {
	//		StateRequest request;
	//		request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
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
	//			session().data().setMimeForwardIds(getSelectedItems());
	//			mimeData->setData(qsl("application/x-td-forward"), "1");
	//		}
	//	}
	//	_controller->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryView::Media*>(nullptr);
	//	if (auto pressedItem = App::pressedItem()) {
	//		pressedMedia = pressedItem->media();
	//		if (_mouseCursorState == CursorState::Date
	//			|| (pressedMedia && pressedMedia->dragItem())) {
	//			forwardMimeType = qsl("application/x-td-forward");
	//			session().data().setMimeForwardIds(
	//				session().data().itemOrItsGroup(pressedItem->data()));
	//		}
	//	}
	//	if (auto pressedLnkItem = App::pressedLinkItem()) {
	//		if ((pressedMedia = pressedLnkItem->media())) {
	//			if (forwardMimeType.isEmpty()
	//				&& pressedMedia->dragItemByHandler(pressedHandler)) {
	//				forwardMimeType = qsl("application/x-td-forward");
	//				session().data().setMimeForwardIds(
	//					{ 1, pressedLnkItem->fullId() });
	//			}
	//		}
	//	}
	//	if (!forwardMimeType.isEmpty()) {
	//		auto mimeData = std::make_unique<QMimeData>();
	//		mimeData->setData(forwardMimeType, "1");
	//		if (auto document = (pressedMedia ? pressedMedia->getDocument() : nullptr)) {
	//			auto filepath = document->filepath(true);
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
	const auto top = itemTop(view);
	const auto range = view->verticalRepaintRange();
	update(0, top + range.top, width(), range.height);
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
