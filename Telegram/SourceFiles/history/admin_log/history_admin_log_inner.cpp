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
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_components.h"
#include "history/history_item_reply_markup.h"
#include "history/history_item_text.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/admin_log/history_admin_log_filter.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_cursor_state.h"
#include "chat_helpers/message_field.h"
#include "boxes/sticker_set_box.h"
#include "boxes/translate_box.h"
#include "ui/boxes/confirm_box.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_common_adapters.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/application.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "api/api_attached_stickers.h"
#include "api/api_report.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/expandable_peer_list.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/inactive_press.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/ui_utility.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "data/data_changes.h"
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
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace AdminLog {
namespace {

// If we require to support more admins we'll have to rewrite this anyway.
constexpr auto kMaxChannelAdmins = 200;
constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kScrollDateHideOnDayCrossingTimeout = crl::time(3000);
constexpr auto kEventsFirstPage = 100;
constexpr auto kEventsPerPage = 100;
constexpr auto kClearUserpicsAfter = 50;
constexpr auto kNewEventsCheckInterval = crl::time(5000);
constexpr auto kNewEventsLimit = 100;

// Trigger an Up preload when fewer than this many display rows sit above
// the visible top.
constexpr auto kPreloadUpDisplayItemsBuffer = 4;

// Max span between a delete group's first event and any later one.
constexpr auto kDeleteGroupTimeWindowSeconds = TimeId(5);

} // namespace

template <InnerWidget::EnumItemsDirection direction, typename Method>
void InnerWidget::enumerateItems(Method method) {
	constexpr auto TopToBottom = (direction == EnumItemsDirection::TopToBottom);

	// No displayed messages in this history.
	if (_displayItems.empty()) {
		return;
	}
	if (_visibleBottom <= _itemsTop || _itemsTop + _itemsHeight <= _visibleTop) {
		return;
	}

	auto begin = std::rbegin(_displayItems), end = std::rend(_displayItems);
	auto from = TopToBottom ? std::lower_bound(begin, end, _visibleTop, [this](const DisplayEntry &elem, int top) {
		return this->itemTop(elem.view) + elem.view->height() <= top;
	}) : std::upper_bound(begin, end, _visibleBottom, [this](int bottom, const DisplayEntry &elem) {
		return this->itemTop(elem.view) + elem.view->height() >= bottom;
	});
	auto wasEnd = (from == end);
	if (wasEnd) {
		--from;
	}
	if (TopToBottom) {
		Assert(itemTop(from->view) + from->view->height() > _visibleTop);
	} else {
		Assert(itemTop(from->view) < _visibleBottom);
	}

	while (true) {
		auto item = from->view;
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
		if (view->data()->isService()) {
			return true;
		}

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
, _pathGradient(
	HistoryView::MakePathShiftGradient(
		controller->chatStyle(),
		[=] { update(); }))
, _highlighter(
	&_history->owner(),
	[=](const HistoryItem *item) { return viewForItem(item); },
	[=](const Element *view) { repaintItem(view); })
, _scrollDateCheck([=] { scrollDateCheck(); })
, _emptyText(
		st::historyAdminLogEmptyWidth
		- st::historyAdminLogEmptyPadding.left()
		- st::historyAdminLogEmptyPadding.left())
, _antiSpamValidator(_controller, _channel)
, _touchSelectTimer([=] { onTouchSelect(); })
, _touchScrollTimer([=] { onTouchScrollTimer(); }) {
	Window::ChatThemeValueFromPeer(
		controller,
		channel
	) | rpl::on_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	setAttribute(Qt::WA_AcceptTouchEvents);
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([=] { scrollDateHideByTimer(); });
	session().data().viewRepaintRequest(
	) | rpl::on_next([=](Data::RequestViewRepaint data) {
		if (myView(data.view)) {
			repaintItem(data.view, data.rect);
		}
	}, lifetime());
	session().data().viewResizeRequest(
	) | rpl::on_next([=](auto view) {
		if (myView(view)) {
			resizeItem(view);
		}
	}, lifetime());
	session().data().itemViewRefreshRequest(
	) | rpl::on_next([=](auto item) {
		if (const auto view = viewForItem(item)) {
			refreshItem(view);
		}
	}, lifetime());
	session().data().viewLayoutChanged(
	) | rpl::on_next([=](auto view) {
		if (myView(view)) {
			if (view->isUnderCursor()) {
				updateSelected();
			}
		}
	}, lifetime());
	session().data().itemDataChanges(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (const auto view = viewForItem(item)) {
			view->itemDataChanged();
		}
	}, lifetime());
	session().data().itemVisibilityQueries(
	) | rpl::filter([=](
			const Data::Session::ItemVisibilityQuery &query) {
		return (_history == query.item->history())
			&& query.item->isAdminLogEntry()
			&& isVisible();
	}) | rpl::on_next([=](
			const Data::Session::ItemVisibilityQuery &query) {
		if (const auto view = viewForItem(query.item)) {
			auto top = itemTop(view);
			if (top >= 0
				&& top + view->height() > _visibleTop
				&& top < _visibleBottom) {
				*query.isVisible = true;
			}
		}
	}, lifetime());

	controller->adaptive().chatWideValue(
	) | rpl::on_next([=](bool wide) {
		_isChatWide = wide;
	}, lifetime());

	updateEmptyText();

	_antiSpamValidator.resolveUser(crl::guard(
		this,
		[=] { requestAdmins(); }));

	_newEventsTimer.setCallback([=] { requestNewEvents(); });
	_newEventsTimer.callEach(kNewEventsCheckInterval);
}

bool InnerWidget::myView(not_null<const HistoryView::Element*> view) const {
	return !_beingDestroyed
		&& !_items.empty()
		&& (view->delegate().get() == this);
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

rpl::producer<int> InnerWidget::newEventsCountValue() const {
	return _newEventsCount.value();
}

void InnerWidget::resetNewEventsCount() {
	_unreadEventIds.clear();
	_newEventsCount = 0;
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto scrolledUp = (visibleTop < _visibleTop);
	auto scrolledDown = (visibleTop > _visibleTop);
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	// Unload userpics.
	if (_userpics.size() > kClearUserpicsAfter) {
		_userpicsCache = std::move(_userpics);
	}

	updateVisibleTopItem();
	checkPreloadMore();
	if (scrolledUp) {
		_scrollDateAfterDayCrossing = false;
		_scrollDateCheck.call();
	} else {
		scrollDateCheckDownward();
	}
	const auto pruneUnreadEvents = !_skipUnreadEventPrune && scrolledDown;
	if (pruneUnreadEvents && _visibleBottom == height()) {
		resetNewEventsCount();
	} else if (pruneUnreadEvents && !_unreadEventIds.empty()) {
		const auto before = _unreadEventIds.size();
		enumerateItems<EnumItemsDirection::TopToBottom>([&](
				not_null<Element*> view,
				int,
				int) {
			const auto it = _itemEventIds.find(view->data());
			if (it != _itemEventIds.end()) {
				_unreadEventIds.remove(it->second);
			}
			return true;
		});
		if (_unreadEventIds.size() != before) {
			_newEventsCount = int(_unreadEventIds.size());
		}
	}
	_controller->floatPlayerAreaUpdated();
	session().data().itemVisibilitiesUpdated();
}

void InnerWidget::updateVisibleTopItem() {
	if (_visibleBottom == height()) {
		_visibleTopItem = nullptr;
		_visibleTopDisplayIndex = -1;
	} else {
		auto begin = std::rbegin(_displayItems), end = std::rend(_displayItems);
		auto from = std::lower_bound(begin, end, _visibleTop, [this](const DisplayEntry &elem, int top) {
			return this->itemTop(elem.view) + elem.view->height() <= top;
		});
		if (from != end) {
			_visibleTopItem = from->view;
			_visibleTopFromItem = _visibleTop - _visibleTopItem->y();
			_visibleTopDisplayIndex = int(_displayItems.size())
				- 1
				- int(from - begin);
		} else {
			_visibleTopItem = nullptr;
			_visibleTopFromItem = _visibleTop;
			_visibleTopDisplayIndex = -1;
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

void InnerWidget::scrollDateCheckDownward() {
	const auto previousDay = _scrollDateLastItem
		? _scrollDateLastItem->dateTime().date()
		: QDate();
	const auto currentDay = _visibleTopItem
		? _visibleTopItem->dateTime().date()
		: QDate();
	const auto crossedDay = previousDay.isValid()
		&& currentDay.isValid()
		&& (previousDay != currentDay);
	_scrollDateLastItem = _visibleTopItem;
	_scrollDateLastItemTop = _visibleTopFromItem;
	if (crossedDay) {
		if (!_scrollDateShown) {
			toggleScrollDateShown();
		}
		_scrollDateAfterDayCrossing = true;
		_scrollDateHideTimer.callOnce(
			kScrollDateHideOnDayCrossingTimeout);
	} else if (!_scrollDateAfterDayCrossing) {
		scrollDateHideByTimer();
	}
}

void InnerWidget::scrollDateHideByTimer() {
	_scrollDateHideTimer.cancel();
	_scrollDateAfterDayCrossing = false;
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
	if (_visibleTop < PreloadHeightsCount * (_visibleBottom - _visibleTop)
			&& displayItemsAboveVisibleTop() < kPreloadUpDisplayItemsBuffer) {
		preloadMore(Direction::Up);
	}
}

int InnerWidget::displayItemsAboveVisibleTop() const {
	if (!_visibleTopItem) {
		// Either nothing is loaded yet (we want a preload to kick off the
		// first request) or the whole content fits in viewport. Both states
		// should let Up preload fire if other conditions agree.
		return 0;
	}
	Assert(_visibleTopDisplayIndex >= 0
		&& _visibleTopDisplayIndex < int(_displayItems.size()));
	// Larger storage index = higher visually (reverse layout).
	return int(_displayItems.size()) - 1 - _visibleTopDisplayIndex;
}

void InnerWidget::applyFilter(FilterValue &&value) {
	if (_filter != value) {
		_filter = value;
		clearAndRequestLog();
	}
}

void InnerWidget::applySearch(const QString &query) {
	if (_searchQuery != query) {
		_searchQuery = query;
		clearAndRequestLog();
	}
}

void InnerWidget::requestAdmins() {
	const auto offset = 0;
	const auto participantsHash = uint64(0);
	_api.request(MTPchannels_GetParticipants(
		_channel->inputChannel(),
		MTP_channelParticipantsAdmins(),
		MTP_int(offset),
		MTP_int(kMaxChannelAdmins),
		MTP_long(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		result.match([&](const MTPDchannels_channelParticipants &data) {
			const auto &[availableCount, list] = Api::ChatParticipants::Parse(
				_channel,
				data);
			_admins.clear();
			_adminsCanEdit.clear();
			if (const auto user = _antiSpamValidator.maybeAppendUser()) {
				_admins.emplace_back(user);
			}
			for (const auto &parsed : list) {
				if (parsed.isUser()) {
					const auto user = _channel->owner().userLoaded(
						parsed.userId());
					if (user) {
						_admins.emplace_back(user);
						if (parsed.canBeEdited() && !parsed.isCreator()) {
							_adminsCanEdit.emplace_back(user);
						}
					}
				}
			}
		}, [&](const MTPDchannels_channelParticipantsNotModified &) {
			LOG(("API Error: c"
				"hannels.channelParticipantsNotModified received!"));
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
		return;
	}
	const auto isChannel = !_channel->isMegagroup();
	const auto filter = _filter;
	const auto admins = _admins;
	_controller->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_manage_peer_recent_actions());
		Ui::AddSubsectionTitle(
			box->verticalLayout(),
			tr::lng_admin_log_filter_actions_type_subtitle());
		const auto collectFlags = FillFilterValueList(
			box->verticalLayout(),
			isChannel,
			filter);
		Ui::AddSkip(box->verticalLayout());
		Ui::AddDivider(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout());
		Ui::AddSubsectionTitle(
			box->verticalLayout(),
			tr::lng_admin_log_filter_actions_admins_subtitle());
		Ui::AddSkip(box->verticalLayout());

		auto checkedPeerId = std::vector<PeerId>();
		{
			checkedPeerId.reserve(admins.size());
			for (const auto &user : admins) {
				if (!filter.admins
					|| ranges::contains(
						(*filter.admins),
						user->id,
						&UserData::id)) {
					checkedPeerId.push_back(user->id);
				}
			}
		}

		const auto checkbox = box->addRow(
			object_ptr<Ui::Checkbox>(
				box->verticalLayout(),
				tr::lng_admin_log_filter_actions_admins_section(
					tr::now,
					tr::marked),
				checkedPeerId.size() == admins.size(),
				st::defaultBoxCheckbox));
		using Controller = Ui::ExpandablePeerListController;
		using Data = Ui::ExpandablePeerListController::Data;
		const auto controller = box->lifetime().make_state<Controller>(Data{
			.participants = ranges::views::all(
				admins
			) | ranges::views::transform([](
					not_null<UserData*> user) -> not_null<PeerData*> {
				return not_null{ user };
			}) | ranges::to_vector,
			.checked = std::move(checkedPeerId),
			.skipSingle = true,
			.hideRightButton = true,
			.checkTopOnAllInner = true,
		});
		Ui::AddExpandablePeerList(
			checkbox,
			controller,
			box->verticalLayout());

		box->addButton(tr::lng_settings_save(), [=] {
			const auto peers = controller->collectRequests();
			const auto users = ranges::views::all(
				peers
			) | ranges::views::transform([](not_null<PeerData*> p) {
				return not_null{ p->asUser() };
			}) | ranges::to_vector;
			callback(FilterValue{
				.flags = collectFlags(),
				.admins = (admins.size() == users.size())
					? std::nullopt
					: std::optional(users),
			});
		});
		box->addButton(tr::lng_cancel(), [box] { box->closeBox(); });
	}));
}

void InnerWidget::clearAndRequestLog() {
	_api.request(base::take(_preloadUpRequestId)).cancel();
	_api.request(base::take(_preloadDownRequestId)).cancel();
	_api.request(base::take(_newEventsRequestId)).cancel();
	_filterChanged = true;
	_upLoaded = false;
	_downLoaded = true;
	resetNewEventsCount();
	updateMinMaxIds();
	preloadMore(Direction::Up);
}

void InnerWidget::updateEmptyText() {
	auto hasSearch = !_searchQuery.isEmpty();
	auto hasFilter = _filter.flags || _filter.admins;
	auto text = tr::semibold((hasSearch || hasFilter)
		? tr::lng_admin_log_no_results_title(tr::now)
		: tr::lng_admin_log_no_events_title(tr::now));
	auto description = hasSearch
		? tr::lng_admin_log_no_results_search_text(
			tr::now,
			lt_query,
			_searchQuery)
		: hasFilter
		? tr::lng_admin_log_no_results_text(tr::now)
		: _channel->isMegagroup()
		? tr::lng_admin_log_no_events_text(tr::now)
		: tr::lng_admin_log_no_events_text_channel(tr::now);
	text.text.append(u"\n\n"_q + description);
	_emptyText.setMarkedText(st::defaultTextStyle, text);
}

QString InnerWidget::tooltipText() const {
	if (_mouseAction == MouseAction::None
		&& (_mouseCursorState == CursorState::Date
			|| _mouseCursorState == CursorState::LogAdminService)) {
		if (const auto view = Element::Hovered()) {
			auto dateText = HistoryView::DateTooltipText(view);

			const auto sentIt = _itemDates.find(view->data());
			if (sentIt != end(_itemDates)) {
				dateText += '\n' + tr::lng_sent_date(
					tr::now,
					lt_date,
					QLocale().toString(
						base::unixtime::parse(sentIt->second),
						QLocale::LongFormat));
			}
			return dateText;
		}
	} else if (_mouseCursorState == CursorState::Forwarded
		&& _mouseAction == MouseAction::None) {
		if (const auto view = Element::Hovered()) {
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

bool InnerWidget::elementUnderCursor(
		not_null<const HistoryView::Element*> view) {
	return (Element::Hovered() == view);
}

HistoryView::SelectionModeResult InnerWidget::elementInSelectionMode(
		const HistoryView::Element *) {
	return {};
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

void InnerWidget::elementShowAddPollOption(
	not_null<HistoryView::Element*> view,
	not_null<PollData*> poll,
	FullMsgId context,
	QRect optionRect) {
}

void InnerWidget::elementSubmitAddPollOption(FullMsgId context) {
}

void InnerWidget::elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	_controller->openPhoto(photo, { context });
}

void InnerWidget::elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	_controller->openDocument(document, showInMediaView, { context });
}

bool InnerWidget::elementScrollToLocalY(
		not_null<const Element*> view,
		int localTop) {
	return false;
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

void InnerWidget::elementShowHiddenSenderTooltip(
		FullMsgId itemId,
		const TextWithEntities &text) {
	_controller->showToast(TextWithEntities(text));
}

bool InnerWidget::elementAnimationsPaused() {
	return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

bool InnerWidget::elementHideReply(not_null<const Element*> view) {
	return false;
}

bool InnerWidget::elementShownUnread(not_null<const Element*> view) {
	return false;
}

void InnerWidget::elementSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void InnerWidget::elementSearchInList(
	const QString &query,
	const FullMsgId &context) {

}

void InnerWidget::elementHandleViaClick(not_null<UserData*> bot) {
}

HistoryView::ElementChatMode InnerWidget::elementChatMode() {
	using Mode = HistoryView::ElementChatMode;
	return _isChatWide ? Mode::Wide : Mode::Default;
}

not_null<Ui::PathShiftGradient*> InnerWidget::elementPathShiftGradient() {
	return _pathGradient.get();
}

void InnerWidget::elementReplyTo(const FullReplyTo &to) {
}

void InnerWidget::jumpToMessageInLog(
		not_null<HistoryItem*> item,
		MessageHighlightId highlight) {
	if (!viewForItem(item)) {
		expandGroupContaining(item);
	}
	const auto view = viewForItem(item);
	if (!view) {
		return;
	}
	_highlighter.highlight({ item, highlight });

	const auto top = itemTop(view);
	const auto height = view->height();
	const auto viewport = _visibleBottom - _visibleTop;
	if (top >= _visibleTop && top + height <= _visibleBottom) {
		return;
	}
	const auto from = _visibleTop;
	const auto target = std::clamp(
		top - std::max(0, (viewport - height) / 2),
		0,
		std::max(0, this->height() - viewport));
	if (from == target) {
		return;
	}
	_scrollToAnimation.stop();
	_scrollToAnimation.start(
		[=] {
			_scrollToSignal.fire_copy(
				anim::interpolate(
					from,
					target,
					_scrollToAnimation.value(1.)));
		},
		0.,
		1.,
		st::slideDuration,
		anim::easeOutCubic);
}

void InnerWidget::expandGroupContaining(not_null<HistoryItem*> item) {
	auto index = -1;
	for (auto i = 0, count = int(_items.size()); i != count; ++i) {
		if (_items[i]->data() == item) {
			index = i;
			break;
		}
	}
	if (index < 0) {
		return;
	}
	for (const auto &group : _deleteGroups) {
		if (index < group.startIndex || index >= group.endIndex) {
			continue;
		}
		if (group.eventCount > 3
			&& !_expandedGroups.contains(group.eventId)) {
			_expandedGroups.insert(group.eventId);
			clearDisplayPointers(DisplayPointerScope::All);
			_skipScrollRestore = true;
			rebuildDisplayItems();
			_skipScrollRestore = false;
		}
		return;
	}
}

void InnerWidget::elementStartInteraction(not_null<const Element*> view) {
}

void InnerWidget::elementStartPremium(
	not_null<const Element*> view,
	Element *replacing) {
}

void InnerWidget::elementCancelPremium(not_null<const Element*> view) {
}

void InnerWidget::elementStartEffect(
	not_null<const Element*> view,
	Element *replacing) {
}

QString InnerWidget::elementAuthorRank(not_null<const Element*> view) {
	return {};
}

bool InnerWidget::elementHideTopicButton(not_null<const Element*> view) {
	return false;
}

void InnerWidget::saveState(not_null<SectionMemento*> memento) {
	memento->setFilter(std::move(_filter));
	memento->setAdmins(std::move(_admins));
	memento->setAdminsCanEdit(std::move(_adminsCanEdit));
	memento->setSearchQuery(std::move(_searchQuery));
	memento->setExpandedGroups(std::move(_expandedGroups));
	if (!_filterChanged) {
		clearDisplayItems(DisplayPointerScope::All);
		for (auto &item : _items) {
			item.clearView();
		}
		memento->setItems(
			base::take(_items),
			base::take(_eventIds),
			_upLoaded,
			_downLoaded);
		memento->setDeleteEventMeta(
			base::take(_itemEventIds),
			base::take(_eventAdminIds),
			base::take(_eventDates));
		base::take(_itemsByData);
	}
	_upLoaded = _downLoaded = true; // Don't load or handle anything anymore.
}

void InnerWidget::restoreState(not_null<SectionMemento*> memento) {
	// OwnedItem::refreshView may call requestItemResize.
	// So we postpone resizing until all views are created.
	_items.clear();
	auto items = memento->takeItems();
	for (auto &item : items) {
		item.refreshView(this);
	}
	_items = std::move(items);

	_eventIds = memento->takeEventIds();
	_admins = memento->takeAdmins();
	_adminsCanEdit = memento->takeAdminsCanEdit();
	_filter = memento->takeFilter();
	_searchQuery = memento->takeSearchQuery();
	_expandedGroups = memento->takeExpandedGroups();
	_itemEventIds = memento->takeItemEventIds();
	_eventAdminIds = memento->takeEventAdminIds();
	_eventDates = memento->takeEventDates();
	_upLoaded = memento->upLoaded();
	_downLoaded = memento->downLoaded();
	_filterChanged = false;
	updateMinMaxIds();
	computeDeleteGroups();
	rebuildDisplayItems();
}

void InnerWidget::preloadMore(Direction direction) {
	auto &requestId = (direction == Direction::Up) ? _preloadUpRequestId : _preloadDownRequestId;
	auto &loadedFlag = (direction == Direction::Up) ? _upLoaded : _downLoaded;
	if (requestId != 0 || loadedFlag) {
		return;
	}

	auto flags = MTPchannels_GetAdminLog::Flags(0);
	const auto filter = [&] {
		using Flag = MTPDchannelAdminLogEventsFilter::Flag;
		using LocalFlag = FilterValue::Flag;
		const auto empty = MTPDchannelAdminLogEventsFilter::Flags(0);
		const auto f = _filter.flags.value_or(LocalFlag());
		return empty
			| ((f & LocalFlag::Join) ? Flag::f_join : empty)
			| ((f & LocalFlag::Leave) ? Flag::f_leave : empty)
			| ((f & LocalFlag::Invite) ? Flag::f_invite : empty)
			| ((f & LocalFlag::Ban) ? Flag::f_ban : empty)
			| ((f & LocalFlag::Unban) ? Flag::f_unban : empty)
			| ((f & LocalFlag::Kick) ? Flag::f_kick : empty)
			| ((f & LocalFlag::Unkick) ? Flag::f_unkick : empty)
			| ((f & LocalFlag::Promote) ? Flag::f_promote : empty)
			| ((f & LocalFlag::Demote) ? Flag::f_demote : empty)
			| ((f & LocalFlag::Info) ? Flag::f_info : empty)
			| ((f & LocalFlag::Settings) ? Flag::f_settings : empty)
			| ((f & LocalFlag::Pinned) ? Flag::f_pinned : empty)
			| ((f & LocalFlag::Edit) ? Flag::f_edit : empty)
			| ((f & LocalFlag::Delete) ? Flag::f_delete : empty)
			| ((f & LocalFlag::GroupCall) ? Flag::f_group_call : empty)
			| ((f & LocalFlag::Invites) ? Flag::f_invites : empty)
			| ((f & LocalFlag::Topics) ? Flag::f_forums : empty)
			| ((f & LocalFlag::SubExtend) ? Flag::f_sub_extend : empty)
			| ((f & LocalFlag::EditRank) ? Flag::f_edit_rank : empty);
	}();
	if (_filter.flags != 0) {
		flags |= MTPchannels_GetAdminLog::Flag::f_events_filter;
	}
	auto admins = QVector<MTPInputUser>(0);
	if (_filter.admins) {
		if (!_filter.admins->empty()) {
			admins.reserve(_filter.admins->size());
			for (const auto &admin : (*_filter.admins)) {
				admins.push_back(admin->inputUser());
			}
		}
		flags |= MTPchannels_GetAdminLog::Flag::f_admins;
	}
	auto maxId = (direction == Direction::Up) ? _minId : 0;
	auto minId = (direction == Direction::Up) ? 0 : _maxId;
	auto perPage = _items.empty() ? kEventsFirstPage : kEventsPerPage;
	requestId = _api.request(MTPchannels_GetAdminLog(
		MTP_flags(flags),
		_channel->inputChannel(),
		MTP_string(_searchQuery),
		MTP_channelAdminLogEventsFilter(MTP_flags(filter)),
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
	}).fail([this, &requestId, &loadedFlag] {
		requestId = 0;
		loadedFlag = true;
		update();
	}).send();
}

void InnerWidget::requestNewEvents() {
	if (_newEventsRequestId
		|| _filterChanged
		|| _preloadUpRequestId
		|| _preloadDownRequestId
		|| _eventIds.empty()) {
		return;
	}
	fetchNewEventsBatch(
		_maxId,
		0,
		std::make_shared<QVector<MTPChannelAdminLogEvent>>());
}

void InnerWidget::fetchNewEventsBatch(
		uint64 pollMinId,
		uint64 maxId,
		std::shared_ptr<QVector<MTPChannelAdminLogEvent>> accumulated) {
	auto flags = MTPchannels_GetAdminLog::Flags(0);
	const auto filter = [&] {
		using Flag = MTPDchannelAdminLogEventsFilter::Flag;
		using LocalFlag = FilterValue::Flag;
		const auto empty = MTPDchannelAdminLogEventsFilter::Flags(0);
		const auto f = _filter.flags.value_or(LocalFlag());
		return empty
			| ((f & LocalFlag::Join) ? Flag::f_join : empty)
			| ((f & LocalFlag::Leave) ? Flag::f_leave : empty)
			| ((f & LocalFlag::Invite) ? Flag::f_invite : empty)
			| ((f & LocalFlag::Ban) ? Flag::f_ban : empty)
			| ((f & LocalFlag::Unban) ? Flag::f_unban : empty)
			| ((f & LocalFlag::Kick) ? Flag::f_kick : empty)
			| ((f & LocalFlag::Unkick) ? Flag::f_unkick : empty)
			| ((f & LocalFlag::Promote) ? Flag::f_promote : empty)
			| ((f & LocalFlag::Demote) ? Flag::f_demote : empty)
			| ((f & LocalFlag::Info) ? Flag::f_info : empty)
			| ((f & LocalFlag::Settings) ? Flag::f_settings : empty)
			| ((f & LocalFlag::Pinned) ? Flag::f_pinned : empty)
			| ((f & LocalFlag::Edit) ? Flag::f_edit : empty)
			| ((f & LocalFlag::Delete) ? Flag::f_delete : empty)
			| ((f & LocalFlag::GroupCall) ? Flag::f_group_call : empty)
			| ((f & LocalFlag::Invites) ? Flag::f_invites : empty)
			| ((f & LocalFlag::Topics) ? Flag::f_forums : empty)
			| ((f & LocalFlag::SubExtend) ? Flag::f_sub_extend : empty)
			| ((f & LocalFlag::EditRank) ? Flag::f_edit_rank : empty);
	}();
	if (_filter.flags != 0) {
		flags |= MTPchannels_GetAdminLog::Flag::f_events_filter;
	}
	auto admins = QVector<MTPInputUser>(0);
	if (_filter.admins) {
		if (!_filter.admins->empty()) {
			admins.reserve(_filter.admins->size());
			for (const auto &admin : (*_filter.admins)) {
				admins.push_back(admin->inputUser());
			}
		}
		flags |= MTPchannels_GetAdminLog::Flag::f_admins;
	}
	_newEventsRequestId = _api.request(MTPchannels_GetAdminLog(
		MTP_flags(flags),
		_channel->inputChannel(),
		MTP_string(_searchQuery),
		MTP_channelAdminLogEventsFilter(MTP_flags(filter)),
		MTP_vector<MTPInputUser>(admins),
		MTP_long(maxId),
		MTP_long(pollMinId),
		MTP_int(kNewEventsLimit)
	)).done([=](const MTPchannels_AdminLogResults &result) {
		Expects(result.type() == mtpc_channels_adminLogResults);

		_newEventsRequestId = 0;
		if (_filterChanged) {
			return;
		}
		const auto &results = result.c_channels_adminLogResults();
		_channel->owner().processUsers(results.vusers());
		_channel->owner().processChats(results.vchats());

		const auto &events = results.vevents().v;
		for (const auto &event : events) {
			if (!_eventIds.contains(event.data().vid().v)) {
				accumulated->push_back(event);
			}
		}

		if (events.size() == kNewEventsLimit && !events.isEmpty()) {
			const auto nextMaxId = events.back().data().vid().v;
			fetchNewEventsBatch(pollMinId, nextMaxId, accumulated);
			return;
		}
		flushNewEvents(*accumulated);
	}).fail([=] {
		_newEventsRequestId = 0;
		flushNewEvents(*accumulated);
	}).send();
}

void InnerWidget::flushNewEvents(
		const QVector<MTPChannelAdminLogEvent> &events) {
	if (_filterChanged || events.isEmpty()) {
		return;
	}
	auto fresh = QVector<MTPChannelAdminLogEvent>();
	fresh.reserve(events.size());
	for (const auto &event : events) {
		if (!_eventIds.contains(event.data().vid().v)) {
			fresh.push_back(event);
		}
	}
	if (fresh.isEmpty()) {
		return;
	}
	addEvents(Direction::Down, fresh);
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

	const auto canRestrict = InnerWidget::canRestrict();
	const auto antiSpamUserId = _antiSpamValidator.userId();
	auto newItems = std::vector<not_null<HistoryItem*>>();
	for (const auto &event : events) {
		const auto &data = event.data();
		const auto id = data.vid().v;
		if (_eventIds.find(id) != _eventIds.end()) {
			return;
		}
		const auto rememberRealMsgId = (antiSpamUserId
			== peerToUser(peerFromUser(data.vuser_id())));
		const auto isDeleteAction = (data.vaction().type()
			== mtpc_channelAdminLogEventActionDeleteMessage);
		if (isDeleteAction) {
			_eventAdminIds.emplace(
				id,
				peerToUser(peerFromUser(data.vuser_id())));
			_eventDates.emplace(id, data.vdate().v);
		}

		auto count = 0;
		const auto addOne = [&](
				OwnedItem item,
				TimeId sentDate,
				MsgId realId) {
			if (sentDate) {
				_itemDates.emplace(item->data(), sentDate);
			}
			_eventIds.emplace(id);
			_itemsByData.emplace(item->data(), item.get());
			_itemEventIds.emplace(item->data(), id);
			if (realId) {
				if (rememberRealMsgId) {
					_antiSpamValidator.addEventMsgId(
						item->data()->fullId(),
						realId);
				}
				if (canRestrict) {
					_realIdsForReport[item->data()->fullId()] = realId;
				}
				if (!item->data()->isService()) {
					_itemsByRealMsgId.insert_or_assign(realId, item->data());
				}
			}
			newItems.push_back(item->data());
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
	}

	for (const auto &item : newItems) {
		const auto replyTo = item->replyToFullId();
		if (replyTo.peer != _history->peer->id) {
			continue;
		}
		const auto i = _itemsByRealMsgId.find(replyTo.msg);
		if (i != _itemsByRealMsgId.end() && i->second != item) {
			item->resolveAdminLogReplyTo(i->second);
		}
	}

	auto newItemsCount = _items.size() + ((direction == Direction::Up) ? 0 : newItemsForDownDirection.size());
	if (newItemsCount != oldItemsCount) {
		// _visibleTopItem may end up absorbed and have a stale y() after
		// rebuild; pin to a captured real-item anchor instead.
		const auto anchor = captureScrollAnchor();

		if (direction == Direction::Down) {
			for (auto &item : _items) {
				newItemsForDownDirection.push_back(std::move(item));
			}
			_items = std::move(newItemsForDownDirection);
		}
		updateMinMaxIds();
		computeDeleteGroups();
		_skipScrollRestore = true;
		rebuildDisplayItems();
		_skipScrollRestore = false;

		if (!up) {
			for (const auto &event : events) {
				_unreadEventIds.emplace(event.data().vid().v);
			}
			_newEventsCount = int(_unreadEventIds.size());
		}

		const auto skipUnreadEventPrune = gsl::finally([&] {
			_skipUnreadEventPrune = false;
		});
		_skipUnreadEventPrune = !up;
		_scrollToSignal.fire_copy(computeScrollFromAnchor(anchor));
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


void InnerWidget::computeDeleteGroups() {
	_deleteGroups.clear();

	if (_items.empty()) {
		return;
	}

	// Groups break on: different admin, time span over the window, or a
	// sticky boundary marker from the previous pass.
	auto previousAnchors = std::move(_previousDeleteGroupAnchors);
	_previousDeleteGroupAnchors.clear();

	auto groupStart = -1;
	auto groupAdmin = UserId();
	auto groupEventId = uint64(0);
	auto groupEventCount = 0;
	auto groupFirstDate = TimeId(0);
	auto currentEventId = uint64(0);
	auto closeAfterCurrentEvent = false;

	const auto finalizeGroup = [&](int endIndex) {
		if (groupEventCount > 0) {
			Assert(endIndex - groupStart >= groupEventCount * 2);
			_deleteGroups.push_back({
				.eventId = groupEventId,
				.adminId = groupAdmin,
				.startIndex = groupStart,
				.endIndex = endIndex,
				.eventCount = groupEventCount,
			});
			// Items run newest→oldest, so groupEventId is the old edge.
			_previousDeleteGroupAnchors.insert(groupEventId);
		}
		groupStart = -1;
		groupAdmin = UserId();
		groupEventId = 0;
		groupEventCount = 0;
		groupFirstDate = 0;
		closeAfterCurrentEvent = false;
	};

	for (auto i = 0, count = int(_items.size()); i < count; ++i) {
		const auto item = _items[i]->data();
		const auto eit = _itemEventIds.find(item);
		if (eit == _itemEventIds.end()) {
			finalizeGroup(i);
			continue;
		}
		const auto eventId = eit->second;
		const auto adminIt = _eventAdminIds.find(eventId);
		if (adminIt == _eventAdminIds.end()) {
			finalizeGroup(i);
			continue;
		}
		const auto adminId = adminIt->second;

		if (eventId != currentEventId) {
			const auto dateIt = _eventDates.find(eventId);
			const auto thisDate = (dateIt != _eventDates.end())
				? dateIt->second
				: TimeId(0);

			const auto sameAdmin = (groupStart >= 0)
				&& (adminId == groupAdmin);
			const auto withinTimeWindow = sameAdmin
				&& groupFirstDate
				&& thisDate
				&& (std::abs(thisDate - groupFirstDate)
					<= kDeleteGroupTimeWindowSeconds);
			const auto canExtend = sameAdmin
				&& withinTimeWindow
				&& !closeAfterCurrentEvent;
			if (!canExtend) {
				finalizeGroup(i);
				groupStart = i;
				groupAdmin = adminId;
				groupFirstDate = thisDate;
			}
			currentEventId = eventId;
			groupEventId = eventId;
			++groupEventCount;

			// Sticky: a previous old edge keeps its boundary across rebuilds.
			if (previousAnchors.contains(eventId)) {
				closeAfterCurrentEvent = true;
			}
		}
	}
	finalizeGroup(int(_items.size()));
}

OwnedItem InnerWidget::createGroupSummaryItem(
		const DeleteGroup &group,
		bool expanded) {
	const auto admin = _history->owner().user(group.adminId);
	const auto fromLink = admin->createOpenLink();
	const auto fromLinkText = tr::link(admin->name(), QString());

	// Collect unique author names from content messages in the group.
	constexpr auto kMaxNames = 4;
	auto authorNames = QStringList();
	auto seenAuthors = base::flat_set<PeerId>();
	auto totalAuthors = 0;
	for (auto i = group.startIndex; i < group.endIndex; ++i) {
		const auto item = _items[i]->data();
		if (!item->isService()) {
			const auto authorId = item->from()->id;
			if (!seenAuthors.contains(authorId)) {
				seenAuthors.emplace(authorId);
				++totalAuthors;
				if (authorNames.size() < kMaxNames) {
					authorNames.push_back(item->from()->name());
				}
			}
		}
	}
	auto names = authorNames.join(u", "_q);
	if (totalAuthors > kMaxNames) {
		names += u", \u2026"_q;
	}

	const auto toggleText = expanded
		? tr::lng_admin_log_hide_all(tr::now)
		: tr::lng_admin_log_show_all(tr::now);

	const auto groupEventId = group.eventId;
	const auto toggleLink = std::make_shared<LambdaClickHandler>(
		[weak = QPointer<InnerWidget>(this), groupEventId] {
			if (const auto strong = weak.data()) {
				strong->toggleDeleteGroup(groupEventId);
			}
		});

	auto text = tr::lng_admin_log_deleted_messages_collapsed(
		tr::now,
		lt_count,
		group.eventCount,
		lt_from,
		fromLinkText,
		lt_names,
		{ names },
		lt_link,
		tr::link(toggleText, QString()),
		tr::marked);

	auto message = PreparedServiceText{ text };
	message.links.push_back(fromLink);
	message.links.push_back(toggleLink);

	const auto date = (group.endIndex > group.startIndex)
		? _items[group.endIndex - 1]->dateTime().toSecsSinceEpoch()
		: 0;

	return OwnedItem(this, _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = MessageFlag::AdminLogEntry,
		.from = peerFromUser(group.adminId),
		.date = TimeId(date),
	}, std::move(message)));
}

void InnerWidget::setupExpandButton(
		not_null<HistoryItem*> item,
		int hiddenCount,
		uint64 groupEventId) {
	const auto text = tr::lng_admin_log_expand_more(
		tr::now,
		lt_count,
		hiddenCount);

	auto markup = HistoryMessageMarkupData();
	markup.flags = ReplyMarkupFlag::Inline;
	markup.rows.push_back({
		HistoryMessageMarkupButton(
			HistoryMessageMarkupButton::Type::Callback,
			text,
			{},
			QByteArray()),
	});
	item->updateReplyMarkup(std::move(markup));
	_expandMarkupItems.emplace(item);
}

void InnerWidget::clearExpandButtons() {
	const auto hasExpandButton = [&](const Element *view) {
		if (!view) {
			return false;
		}
		for (const auto &item : _items) {
			if (item.get() == view) {
				return _expandMarkupItems.contains(item->data());
			}
		}
		return false;
	};
	if (hasExpandButton(_mouseActionItem)) {
		_mouseActionItem = nullptr;
		_mouseAction = MouseAction::None;
	}
	if (hasExpandButton(Element::Hovered())) {
		Element::Hovered(nullptr);
		ClickHandler::clearActive();
	}
	if (hasExpandButton(Element::Pressed())) {
		Element::Pressed(nullptr);
		ClickHandler::unpressed();
	}
	if (hasExpandButton(Element::HoveredLink())) {
		Element::HoveredLink(nullptr);
		ClickHandler::clearActive();
	}
	if (hasExpandButton(Element::PressedLink())) {
		Element::PressedLink(nullptr);
		ClickHandler::unpressed();
	}
	if (hasExpandButton(Element::Moused())) {
		Element::Moused(nullptr);
	}
	for (const auto &item : _expandMarkupItems) {
		item->updateReplyMarkup({});
	}
	_expandMarkupItems.clear();
}

void InnerWidget::toggleDeleteGroup(uint64 groupEventId) {
	_toggleAnimation.stop();

	const auto scrollBefore = _visibleTop;
	const auto anchor = captureScrollAnchor();

	if (_expandedGroups.contains(groupEventId)) {
		_expandedGroups.erase(groupEventId);
	} else {
		_expandedGroups.insert(groupEventId);
	}

	clearDisplayPointers(DisplayPointerScope::All);

	_skipScrollRestore = true;
	rebuildDisplayItems();
	_skipScrollRestore = false;

	const auto scrollTarget = anchor.view
		? computeScrollFromAnchor(anchor)
		: scrollBefore;

	_scrollToSignal.fire_copy(scrollBefore);
	if (scrollBefore != scrollTarget) {
		const auto from = scrollBefore;
		const auto to = scrollTarget;
		_toggleAnimation.start(
			[=] { _scrollToSignal.fire_copy(anim::interpolate(
				from, to, _toggleAnimation.value(1.))); },
			0.,
			1.,
			st::slideDuration,
			anim::easeOutCubic);
	}
}

bool InnerWidget::displayPointerMatches(
		const Element *view,
		DisplayPointerScope pointerScope) const {
	if (!view) {
		return false;
	}
	for (const auto &item : _summaryItems) {
		if (item.get() == view) {
			return true;
		}
	}
	if (pointerScope == DisplayPointerScope::All) {
		for (const auto &item : _items) {
			if (item.get() == view) {
				return true;
			}
		}
	}
	return false;
}

void InnerWidget::clearDisplayPointers(DisplayPointerScope pointerScope) {
	const auto clearAll = (pointerScope == DisplayPointerScope::All);
	const auto clearMember = [&](const Element *view) {
		return clearAll || displayPointerMatches(view, pointerScope);
	};
	if (clearMember(_visibleTopItem)) {
		_visibleTopItem = nullptr;
		_visibleTopFromItem = 0;
		_visibleTopDisplayIndex = -1;
	}
	if (clearMember(_scrollDateLastItem)) {
		_scrollDateLastItem = nullptr;
		_scrollDateLastItemTop = 0;
	}
	if (clearMember(_mouseActionItem)) {
		_mouseActionItem = nullptr;
		_mouseAction = MouseAction::None;
	}
	if (clearMember(_selectedItem)) {
		_selectedItem = nullptr;
		_selectedTextSelection = MessageSelection();
	}
	if (displayPointerMatches(Element::Hovered(), pointerScope)) {
		Element::Hovered(nullptr);
		ClickHandler::clearActive();
	}
	if (displayPointerMatches(Element::Pressed(), pointerScope)) {
		Element::Pressed(nullptr);
		ClickHandler::unpressed();
	}
	if (displayPointerMatches(Element::HoveredLink(), pointerScope)) {
		Element::HoveredLink(nullptr);
		ClickHandler::clearActive();
	}
	if (displayPointerMatches(Element::PressedLink(), pointerScope)) {
		Element::PressedLink(nullptr);
		ClickHandler::unpressed();
	}
	if (displayPointerMatches(Element::Moused(), pointerScope)) {
		Element::Moused(nullptr);
	}
}

void InnerWidget::clearDisplayItems(DisplayPointerScope pointerScope) {
	clearExpandButtons();
	clearDisplayPointers(pointerScope);
	_summaryItems.clear();
	_displayItems.clear();
	_itemsByData.clear();
}

void InnerWidget::rebuildDisplayItems() {
	clearDisplayItems(DisplayPointerScope::Transient);

	const auto groupDisplayEnabled = _searchQuery.isEmpty();

	// Build a set of group start indices for quick lookup.
	auto groupByStart = base::flat_map<int, int>(); // startIndex -> group index
	for (auto g = 0, gc = int(_deleteGroups.size()); g < gc; ++g) {
		groupByStart.emplace(_deleteGroups[g].startIndex, g);
	}

	const auto append = [&](Element *view, int topItemsIndex) {
		_displayItems.push_back({ view, topItemsIndex });
		_itemsByData.emplace(view->data(), view);
	};

	auto i = 0;
	const auto count = int(_items.size());
	while (i < count) {
		const auto git = groupByStart.find(i);
		if (groupDisplayEnabled && git != groupByStart.end()) {
			const auto &group = _deleteGroups[git->second];
			if (group.eventCount > 3) {
				const auto expanded = _expandedGroups.contains(group.eventId);
				if (expanded) {
					for (auto j = group.startIndex; j < group.endIndex; ++j) {
						append(_items[j].get(), j);
					}
				} else if (group.endIndex >= group.startIndex + 2) {
					// Collapsed: show only the content, skip the header.
					const auto contentItem = _items[group.endIndex - 2]->data();
					setupExpandButton(
						contentItem,
						group.eventCount - 1,
						group.eventId);
					append(
						_items[group.endIndex - 2].get(),
						group.endIndex - 2);
				}
				auto summary = createGroupSummaryItem(group, expanded);
				append(summary.get(), group.endIndex - 1);
				_summaryItems.push_back(std::move(summary));

				i = group.endIndex;
				continue;
			}
		}
		append(_items[i].get(), i);
		++i;
	}

	for (const auto &entry : _displayItems) {
		entry.view->setAttachToPrevious(false);
		entry.view->setAttachToNext(false);
	}
	for (auto d = 0, dc = int(_displayItems.size()); d < dc; ++d) {
		const auto view = _displayItems[d].view;
		if (d + 1 < dc) {
			const auto previous = _displayItems[d + 1].view;
			view->setDisplayDate(
				view->dateTime().date() != previous->dateTime().date());
			const auto attach = view->computeIsAttachToPrevious(previous);
			view->setAttachToPrevious(attach, previous);
			previous->setAttachToNext(attach, view);
		} else {
			view->setDisplayDate(true);
		}
	}

	updateSize();
}

void InnerWidget::updateSize() {
	RpWidget::resizeToWidth(width());
	restoreScrollPosition();
	updateVisibleTopItem();
	checkPreloadMore();
}

int InnerWidget::resizeGetHeight(int newWidth) {
	update();

	const auto resizeAllItems = (_itemsWidth != newWidth);
	auto newHeight = 0;
	for (auto it = _displayItems.rbegin(); it != _displayItems.rend(); ++it) {
		const auto view = it->view;
		view->setY(newHeight);
		if (view->pendingResize() || resizeAllItems) {
			newHeight += view->resizeGetHeight(newWidth);
		} else {
			newHeight += view->height();
		}
	}
	_itemsWidth = newWidth;
	_itemsHeight = newHeight;
	_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom) ? (_minHeight - _itemsHeight - st::historyPaddingBottom) : 0;
	return _itemsTop + _itemsHeight + st::historyPaddingBottom;
}

void InnerWidget::restoreScrollPosition() {
	if (_skipScrollRestore) {
		return;
	}
	const auto newVisibleTop = _visibleTopItem
		? (itemTop(_visibleTopItem) + _visibleTopFromItem)
		: ScrollMax;
	_scrollToSignal.fire_copy(newVisibleTop);
}

auto InnerWidget::captureScrollAnchor() const -> ScrollAnchor {
	if (_displayItems.empty()) {
		return {};
	}
	// Skip summaries — they get a fresh HistoryItem on every rebuild.
	auto begin = std::rbegin(_displayItems);
	auto end = std::rend(_displayItems);
	auto from = std::lower_bound(begin, end, _visibleTop,
		[this](const DisplayEntry &elem, int top) {
			return this->itemTop(elem.view) + elem.view->height() <= top;
		});
	for (auto it = from; it != end; ++it) {
		const auto view = it->view;
		if (_itemEventIds.contains(view->data())) {
			return { view, _visibleTop - itemTop(view) };
		}
	}
	auto it = from;
	while (it != begin) {
		--it;
		const auto view = it->view;
		if (_itemEventIds.contains(view->data())) {
			return { view, _visibleTop - itemTop(view) };
		}
	}
	return {};
}

int InnerWidget::computeScrollFromAnchor(ScrollAnchor anchor) const {
	if (!anchor.view) {
		return ScrollMax;
	}
	const auto it = _itemsByData.find(anchor.view->data());
	if (it != _itemsByData.end() && it->second == anchor.view) {
		return itemTop(anchor.view) + anchor.delta;
	}
	// Anchor was absorbed; snap to its group's summary instead.
	auto anchorItemsIndex = -1;
	for (auto i = 0, n = int(_items.size()); i != n; ++i) {
		if (_items[i].get() == anchor.view) {
			anchorItemsIndex = i;
			break;
		}
	}
	if (anchorItemsIndex >= 0) {
		for (const auto &group : _deleteGroups) {
			if (anchorItemsIndex >= group.startIndex
					&& anchorItemsIndex < group.endIndex
					&& group.endIndex >= group.startIndex + 2) {
				const auto contentData =
					_items[group.endIndex - 2]->data();
				const auto contentIt = _itemsByData.find(contentData);
				if (contentIt == _itemsByData.end()) {
					break;
				}
				// Collapsed pair is [content, summary] — summary follows it.
				const auto contentView = contentIt->second;
				const auto displayIt = std::find_if(
					_displayItems.begin(),
					_displayItems.end(),
					[&](const DisplayEntry &e) {
						return e.view == contentView;
					});
				if (displayIt != _displayItems.end()
						&& std::next(displayIt) != _displayItems.end()) {
					return itemTop(std::next(displayIt)->view)
						+ anchor.delta;
				}
				return itemTop(contentView) + anchor.delta;
			}
		}
	}
	return ScrollMax;
}

Ui::ChatPaintContext InnerWidget::preparePaintContext(QRect clip) const {
	return _controller->preparePaintContext({
		.theme = _theme.get(),
		.clip = clip,
		.visibleAreaPositionGlobal = mapToGlobal(QPoint(0, _visibleTop)),
		.visibleAreaTop = _visibleTop,
		.visibleAreaWidth = width(),
		.visibleAreaHeight = _visibleBottom - _visibleTop,
	});
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	if (_controller->contentOverlapped(this, e)) {
		return;
	}

	const auto guard = gsl::finally([&] {
		_userpicsCache.clear();
	});

	Painter p(this);

	auto clip = e->rect();
	auto context = preparePaintContext(clip);
	context.highlightPathCache = &_highlightPathCache;
	if (_items.empty() && _upLoaded && _downLoaded) {
		paintEmpty(p, context.st);
	} else {
		_pathGradient->startFrame(
			0,
			width(),
			std::min(st::msgMaxWidth / 2, width() / 2));

		auto begin = std::rbegin(_displayItems), end = std::rend(_displayItems);
		auto from = std::lower_bound(begin, end, clip.top(), [this](const DisplayEntry &elem, int top) {
			return this->itemTop(elem.view) + elem.view->height() <= top;
		});
		auto to = std::lower_bound(begin, end, clip.top() + clip.height(), [this](const DisplayEntry &elem, int bottom) {
			return this->itemTop(elem.view) < bottom;
		});
		if (from != end) {
			auto top = itemTop(from->view);
			context.translate(0, -top);
			p.translate(0, top);
			for (auto i = from; i != to; ++i) {
				const auto view = i->view;
				context.outbg = view->hasOutLayout();
				context.selection = (view == _selectedItem)
					? _selectedTextSelection.flatSelection()
					: TextSelection();
				context.fullMessageSelected = false;
				context.messageSelection = ((view == _selectedItem)
					&& !_selectedTextSelection.empty())
					? &_selectedTextSelection
					: nullptr;
				context.highlight = _highlighter.state(view->data());
				view->draw(p, context);

				const auto height = view->height();
				top += height;
				context.translate(0, -height);
				p.translate(0, height);
			}
			context.translate(0, top);
			p.translate(0, -top);

			enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
				// stop the enumeration if the userpic is below the painted rect
				if (userpicTop >= clip.top() + clip.height()) {
					return false;
				}

				// paint the userpic if it intersects the painted rect
				if (userpicTop + st::msgPhotoSize > clip.top()) {
					const auto from = view->data()->from();
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
						if (const auto date = view->Get<HistoryView::DateBadge>()) {
							date->paint(p, context.st, dateY, width, _isChatWide);
						} else {
							HistoryView::ServiceMessagePainter::PaintDate(
								p,
								context.st,
								view->dateTime(),
								dateY,
								width,
								_isChatWide);
						}
					}
				}
				return true;
			});
		}
	}
}

void InnerWidget::clearAfterFilterChange() {
	_filterChanged = false;
	clearDisplayItems(DisplayPointerScope::All);
	_deleteGroups.clear();
	_expandedGroups.clear();
	_itemEventIds.clear();
	_eventAdminIds.clear();
	_eventDates.clear();
	_previousDeleteGroupAnchors.clear();
	_items.clear();
	_eventIds.clear();
	_itemsByData.clear();
	_itemsByRealMsgId.clear();
	_highlighter.clear();
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

void InnerWidget::paintEmpty(Painter &p, not_null<const Ui::ChatStyle*> st) {
	auto rectWidth = st::historyAdminLogEmptyWidth;
	auto innerWidth = rectWidth - st::historyAdminLogEmptyPadding.left() - st::historyAdminLogEmptyPadding.right();
	auto rectHeight = st::historyAdminLogEmptyPadding.top() + _emptyText.countHeight(innerWidth) + st::historyAdminLogEmptyPadding.bottom();
	auto rect = QRect((width() - rectWidth) / 2, (height() - rectHeight) / 3, rectWidth, rectHeight);
	HistoryView::ServiceMessagePainter::PaintBubble(p, st, rect);

	p.setPen(st->msgServiceFg());
	_emptyText.draw(p, rect.x() + st::historyAdminLogEmptyPadding.left(), rect.y() + st::historyAdminLogEmptyPadding.top(), innerWidth, style::al_top);
}

TextForMimeData InnerWidget::getSelectedText() const {
	return _selectedItem
		? _selectedItem->selectedText(_selectedTextSelection)
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
			_mouseTextAnchor = dragState;
			_mouseSelectType = TextSelectType::Words;
			if (_mouseAction == MouseAction::None) {
				_mouseAction = MouseAction::Selecting;
				repaintItem(std::exchange(_selectedItem, _mouseActionItem));
				_selectedTextSelection = _mouseActionItem->selectionFromStates(
					_mouseTextAnchor,
					dragState,
					_mouseSelectType);
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
		hasSelected = _selectedTextSelection.empty() ? 0 : 1;
		if (Element::Moused() && Element::Moused() == Element::Hovered()) {
			auto mousePos = mapPointToItem(
				mapFromGlobal(_mousePosition),
				Element::Moused());
			StateRequest request;
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			auto dragState = Element::Moused()->textState(mousePos, request);
			if (Element::Moused()->selectionContains(
					_selectedTextSelection,
					dragState)) {
				isUponSelected = 1;
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuExpandedSeparator);

	const auto link = ClickHandler::getActive();
	auto view = Element::Hovered()
		? Element::Hovered()
		: Element::HoveredLink();
	const auto lnkPhoto = link
		? reinterpret_cast<PhotoData*>(
			link->property(kPhotoLinkMediaProperty).toULongLong())
		: nullptr;
	const auto lnkDocument = link
		? reinterpret_cast<DocumentData*>(
			link->property(kDocumentLinkMediaProperty).toULongLong())
		: nullptr;
	auto lnkIsVideo = lnkDocument ? lnkDocument->isVideoFile() : false;
	auto lnkIsVoice = lnkDocument ? lnkDocument->isVoiceMessage() : false;
	auto lnkIsAudio = lnkDocument ? lnkDocument->isAudioFile() : false;
	const auto fromId = PeerId(link
		? link->property(kPeerLinkPeerIdProperty).toULongLong()
		: 0);
	if (lnkPhoto || lnkDocument) {
		if (isUponSelected > 0) {
			_menu->addAction(tr::lng_context_copy_selected(tr::now), [=] {
				copySelectedText();
			}, &st::menuIconCopy);
		}
		if (lnkPhoto) {
			const auto media = lnkPhoto->activeMediaView();
			if (!lnkPhoto->isNull() && media && media->loaded()) {
				_menu->addAction(tr::lng_context_save_image(tr::now), base::fn_delayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
					savePhotoToFile(lnkPhoto);
				}), &st::menuIconSaveImage);
				_menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
					copyContextImage(lnkPhoto);
				}, &st::menuIconCopy);
			}
			if (lnkPhoto->hasAttachedStickers()) {
				const auto controller = _controller;
				auto callback = [=] {
					auto &attached = session().api().attachedStickers();
					attached.requestAttachedStickerSets(controller, lnkPhoto);
				};
				_menu->addAction(
					tr::lng_context_attached_stickers(tr::now),
					std::move(callback),
					&st::menuIconStickers);
			}
		} else {
			if (lnkDocument->loading()) {
				_menu->addAction(tr::lng_context_cancel_download(tr::now), [=] {
					cancelContextDownload(lnkDocument);
				}, &st::menuIconCancel);
			} else {
				const auto itemId = view
					? view->data()->fullId()
					: FullMsgId();
				if (const auto item = session().data().message(itemId)) {
					const auto notAutoplayedGif = [&] {
						return lnkDocument->isGifv()
							&& !Data::AutoDownload::ShouldAutoPlay(
								session().settings().autoDownload(),
								item->history()->peer,
								lnkDocument);
					}();
					if (notAutoplayedGif) {
						_menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
							openContextGif(itemId);
						}, &st::menuIconShowInChat);
					}
				}
				if (!lnkDocument->filepath(true).isEmpty()) {
					_menu->addAction(Platform::IsMac() ? tr::lng_context_show_in_finder(tr::now) : tr::lng_context_show_in_folder(tr::now), [=] {
						showContextInFolder(lnkDocument);
					}, &st::menuIconShowInFolder);
				}
				_menu->addAction(lnkIsVideo ? tr::lng_context_save_video(tr::now) : (lnkIsVoice ? tr::lng_context_save_audio(tr::now) : (lnkIsAudio ?  tr::lng_context_save_audio_file(tr::now) :  tr::lng_context_save_file(tr::now))), base::fn_delayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, lnkDocument] {
					saveDocumentToFile(lnkDocument);
				}), &st::menuIconDownload);

				HistoryView::AddCopyFilename(
					_menu,
					lnkDocument,
					[] { return false; });

				if (lnkDocument->hasAttachedStickers()) {
					const auto controller = _controller;
					auto callback = [=] {
						auto &attached = session().api().attachedStickers();
						attached.requestAttachedStickerSets(controller, lnkDocument);
					};
					_menu->addAction(
						tr::lng_context_attached_stickers(tr::now),
						std::move(callback),
						&st::menuIconStickers);
				}
			}
		}
	} else if (fromId) { // suggest to block
		if (const auto participant = session().data().peer(fromId)) {
			const auto item = view ? view->data().get() : nullptr;
			auto realId = FullMsgId();
			if (const auto itemId = item ? item->fullId() : FullMsgId()) {
				const auto it = _realIdsForReport.find(itemId);
				if (it != _realIdsForReport.end()) {
					realId = FullMsgId(_channel->id, it->second);
				}
			}
			suggestRestrictParticipant(participant, realId);
			if (_overSenderUserpic) {
				_menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
			}
		}
	} else { // maybe cursor on some text history item?
		const auto item = view ? view->data().get() : nullptr;
		const auto itemId = item ? item->fullId() : FullMsgId();

		_antiSpamValidator.addAction(_menu, itemId);

		if (isUponSelected > 0) {
			_menu->addAction(
				tr::lng_context_copy_selected(tr::now),
				[this] { copySelectedText(); },
				&st::menuIconCopy);
			if (item && !Ui::SkipTranslate(getSelectedText().rich)) {
				const auto peer = item->history()->peer;
				_menu->addAction(tr::lng_context_translate_selected({}), [=] {
					_controller->show(Box(
						Ui::TranslateBox,
						peer,
						MsgId(),
						getSelectedText().rich,
						false));
				}, &st::menuIconTranslate);
			}
		} else {
			if (item && !isUponSelected) {
				const auto media = view->media();
				const auto mediaHasTextForCopy = media && media->hasTextForCopy();
				if (const auto document = media ? media->getDocument() : nullptr) {
					if (document->sticker()) {
						_menu->addAction(tr::lng_context_save_image(tr::now), base::fn_delayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, document] {
							saveDocumentToFile(document);
						}), &st::menuIconDownload);
					}
				}
				if (!item->isService()
					&& !link
					&& (view->hasVisibleText()
						|| mediaHasTextForCopy
						|| !item->factcheckText().empty()
						|| item->Has<HistoryMessageLogEntryOriginal>())) {
					_menu->addAction(tr::lng_context_copy_text(tr::now), [=] {
						copyContextText(itemId);
					}, &st::menuIconCopy);
				}
				if (!item->isService() && !Ui::SkipTranslate(item->originalText())) {
					const auto peer = item->history()->peer;
					_menu->addAction(tr::lng_context_translate({}), [=] {
						_controller->show(Box(
							Ui::TranslateBox,
							peer,
							MsgId(),
							item->originalText(),
							false));
					}, &st::menuIconTranslate);
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
				},
				&st::menuIconCopy);
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

	auto filter = u"JPEG Image (*.jpg);;"_q + FileDialog::AllFilesFilter();
	FileDialog::GetWritePath(
		this,
		tr::lng_save_photo(tr::now),
		filter,
		filedialogDefaultName(u"photo"_q, u".jpg"_q),
		crl::guard(this, [=](const QString &result) {
			if (!result.isEmpty()) {
				media->saveToFile(result);
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
	media->setToClipboard();
}

void InnerWidget::copySelectedText() {
	TextUtilities::SetClipboardText(getSelectedText());
}

void InnerWidget::showStickerPackInfo(not_null<DocumentData*> document) {
	StickerSetBox::Show(_controller->uiShow(), document);
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
				_controller->openDocument(document, true, { itemId });
			}
		}
	}
}

void InnerWidget::copyContextText(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		TextUtilities::SetClipboardText(HistoryItemText(item));
	}
}

void InnerWidget::suggestRestrictParticipant(
		not_null<PeerData*> participant,
		FullMsgId realId) {
	Expects(_menu != nullptr);

	if (!canRestrict()) {
		return;
	}
	if (ranges::contains(_admins, participant)) {
		if (!ranges::contains(_adminsCanEdit, participant)) {
			return;
		}
	}
	_menu->addAction(tr::lng_context_restrict_user(tr::now), [=] {
		const auto user = participant->asUser();
		auto editRestrictions = [=](
				bool hasAdminRights,
				ChatRestrictionsInfo currentRights,
				UserData *by,
				TimeId since) {
			auto weak = QPointer<InnerWidget>(this);
			auto weakBox = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
			auto box = Box<EditRestrictedBox>(
				_channel,
				user,
				hasAdminRights,
				currentRights,
				QString(),
				by,
				since);
			box->setSaveCallback([=](
					ChatRestrictionsInfo oldRights,
					ChatRestrictionsInfo newRights) {
				if (weak) {
					weak->restrictParticipant(
						participant,
						oldRights,
						newRights);
				}
				if (*weakBox) {
					(*weakBox)->closeBox();
				}
			});
			*weakBox = _controller->show(std::move(box));
		};
		if (!user) {
			const auto text = (_channel->isBroadcast()
				? tr::lng_profile_sure_kick_channel
				: tr::lng_profile_sure_kick)(
					tr::now,
					lt_user,
					participant->name());
			auto weakBox = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
			const auto sure = crl::guard(this, [=] {
				restrictParticipant(
					participant,
					ChatRestrictionsInfo(),
					ChannelData::KickedRestrictedRights(participant));
				if (*weakBox) {
					(*weakBox)->closeBox();
				}
			});
			*weakBox = _controller->show(Ui::MakeConfirmBox({ text, sure }));
		} else if (base::contains(_admins, user)) {
			editRestrictions(true, {}, nullptr, 0);
		} else {
			_api.request(MTPchannels_GetParticipant(
				_channel->inputChannel(),
				user->input()
			)).done([=](const MTPchannels_ChannelParticipant &result) {
				user->owner().processUsers(result.data().vusers());

				const auto participant = Api::ChatParticipant(
					result.data().vparticipant(),
					user);
				using Type = Api::ChatParticipant::Type;
				if (participant.type() == Type::Creator
					|| participant.type() == Type::Admin) {
					editRestrictions(true, {}, nullptr, 0);
				} else {
					const auto since = participant.restrictedSince();
					editRestrictions(
						false,
						participant.restrictions(),
						user->owner().user(participant.by()),
						since);
				}
			}).fail([=] {
				editRestrictions(false, {}, nullptr, 0);
			}).send();
		}
	}, &st::menuIconPermissions);

	{
		const auto lifetime = std::make_shared<rpl::lifetime>();
		auto handler = [=, this] {
			participant->session().changes().peerUpdates(
				_channel,
				Data::PeerUpdate::Flag::Members
			) | rpl::on_next([=](const Data::PeerUpdate &update) {
				_downLoaded = false;
				preloadMore(Direction::Down);
				lifetime->destroy();
			}, *lifetime);
			participant->session().api().chatParticipants().kick(
				_channel,
				participant,
				{ _channel->restrictions(), 0 });
		};
		const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
		addAction({
			.text = tr::lng_context_ban_user(tr::now),
			.handler = handler,
			.icon = &st::menuIconBlockAttention,
			.isAttention = true,
		});

		if (realId) {
			addAction({
				.text = tr::lng_report_and_ban(tr::now),
				.handler = [=, show = _controller->uiShow()] {
					Api::ReportSpam(participant, { realId });
					handler();
					show->showToast(tr::lng_report_spam_done(tr::now));
				},
				.icon = &st::menuIconReportAttention,
				.isAttention = true,
			});
		}
	}
}

void InnerWidget::restrictParticipant(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo oldRights,
		ChatRestrictionsInfo newRights) {
	const auto done = [=](ChatRestrictionsInfo newRights) {
		restrictParticipantDone(participant, newRights);
	};
	const auto callback = SaveRestrictedCallback(
		_controller->uiShow(),
		_channel,
		participant,
		crl::guard(this, done),
		nullptr);
	callback(oldRights, newRights);
}

void InnerWidget::restrictParticipantDone(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo rights) {
	if (rights.flags) {
		_admins.erase(
			std::remove(_admins.begin(), _admins.end(), participant),
			_admins.end());
		_adminsCanEdit.erase(
			std::remove(
				_adminsCanEdit.begin(),
				_adminsCanEdit.end(),
				participant),
			_adminsCanEdit.end());
	}
	_downLoaded = false;
	checkPreloadMore();
}

bool InnerWidget::canRestrict() const {
	return _channel->isMegagroup()
		&& _channel->canBanMembers()
		&& !_admins.empty();
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

void InnerWidget::enterEventHook(QEnterEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return RpWidget::enterEventHook(e);
}

void InnerWidget::leaveEventHook(QEvent *e) {
	if (const auto view = Element::Hovered()) {
		repaintItem(view);
		Element::Hovered(nullptr);
	}
	ClickHandler::clearActive();
	Ui::Tooltip::Hide();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return RpWidget::leaveEventHook(e);
}

void InnerWidget::mouseActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	ClickHandler::pressed();
	if (Element::Pressed() != Element::Hovered()) {
		repaintItem(Element::Pressed());
		Element::Pressed(Element::Hovered());
		repaintItem(Element::Pressed());
	}

	_mouseAction = MouseAction::None;
	_mouseActionItem = Element::Moused();
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
				repaintItem(std::exchange(_selectedItem, _mouseActionItem));
				_selectedTextSelection = _mouseActionItem->selectionFromStates(
					dragState,
					dragState,
					TextSelectType::Paragraphs);
				_mouseTextAnchor = dragState;
				_mouseAction = MouseAction::Selecting;
				_mouseSelectType = TextSelectType::Paragraphs;
				mouseActionUpdate(_mousePosition);
				_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
			}
		} else if (Element::Pressed()) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = _mouseActionItem->textState(_dragStartPosition, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (Element::Pressed()) {
				auto uponSelected = _mouseActionItem
					&& (_selectedItem == _mouseActionItem)
					&& _mouseActionItem->selectionContains(
						_selectedTextSelection,
						dragState);
				if (uponSelected) {
					_mouseAction = MouseAction::PrepareDrag; // start text drag
				} else if (!_pressWasInactive) {
					repaintItem(std::exchange(_selectedItem, _mouseActionItem));
					_mouseTextAnchor = dragState;
					_selectedTextSelection = _mouseActionItem->selectionFromStates(
						_mouseTextAnchor,
						dragState,
						_mouseSelectType);
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
	_mouseTextAnchor = TextState();
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
	if (const auto view = Element::Pressed()) {
		repaintItem(view);
		Element::Pressed(nullptr);
	}

	_wasSelectedText = false;

	if (activated) {
		// Intercept inline keyboard button clicks on items
		// with our expand button markup.
		if (_mouseActionItem) {
			const auto view = _mouseActionItem;
			const auto item = view->data();
			if (dynamic_cast<ReplyMarkupClickHandler*>(activated.get())
				&& _expandMarkupItems.contains(item)) {
				const auto it = _itemEventIds.find(item);
				if (it != _itemEventIds.end()) {
					mouseActionCancel();
					toggleDeleteGroup(it->second);
					return;
				}
			}
			if (const auto reply = view->Get<HistoryView::Reply>()
				; reply && (activated == reply->link())) {
				if (const auto data = item->Get<HistoryMessageReply>()) {
					const auto to = data->resolvedMessage.get();
					if (to && to->isAdminLogEntry()) {
						const auto &fields = data->fields();
						mouseActionCancel();
						jumpToMessageInLog(to, {
							.quote = (fields.manualQuote
								? fields.quote
								: TextWithEntities()),
							.quoteOffset = int(fields.quoteOffset),
							.todoItemId = fields.todoItemId,
							.pollOption = fields.pollOption,
						});
						return;
					}
				}
			}
		}
		mouseActionCancel();
		ActivateClickHandler(window(), activated, {
			button,
			QVariant::fromValue(ClickHandlerContext{
				.elementDelegate = [weak = base::make_weak(this)] {
					return (ElementDelegate*)weak.get();
				},
				.sessionWindow = base::make_weak(_controller),
			})
		});
		return;
	}
	if (_mouseAction == MouseAction::PrepareDrag && !_pressWasInactive && button != Qt::RightButton) {
		repaintItem(base::take(_selectedItem));
	} else if (_mouseAction == MouseAction::Selecting) {
		if (_selectedItem && !_pressWasInactive) {
			if (_selectedTextSelection.empty()) {
				_selectedItem = nullptr;
				_controller->widget()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseActionItem = nullptr;
	_mouseSelectType = TextSelectType::Letters;
	_mouseTextAnchor = TextState();
	//_widget->noSelectingScroll(); // TODO

	if (QGuiApplication::clipboard()->supportsSelection()
		&& _selectedItem
		&& !_selectedTextSelection.empty()) {
		TextUtilities::SetClipboardText(
			_selectedItem->selectedText(_selectedTextSelection),
			QClipboard::Selection);
	}
}

void InnerWidget::updateSelected() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(
		std::clamp(mousePosition.x(), 0, width()),
		std::clamp(mousePosition.y(), _visibleTop, _visibleBottom));

	auto itemPoint = QPoint();
	auto begin = std::rbegin(_displayItems), end = std::rend(_displayItems);
	auto from = (point.y() >= _itemsTop && point.y() < _itemsTop + _itemsHeight)
		? std::lower_bound(begin, end, point.y(), [this](const DisplayEntry &elem, int top) {
			return this->itemTop(elem.view) + elem.view->height() <= top;
		})
		: end;
	const auto view = (from != end) ? from->view : nullptr;
	const auto item = view ? view->data().get() : nullptr;
	if (item) {
		Element::Moused(view);
		itemPoint = mapPointToItem(point, view);
		if (view->pointState(itemPoint) != PointState::Outside) {
			if (Element::Hovered() != view) {
				repaintItem(Element::Hovered());
				Element::Hovered(view);
				repaintItem(view);
			}
		} else if (const auto view = Element::Hovered()) {
			repaintItem(view);
			Element::Hovered(nullptr);
		}
	}

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto dragStateUserpic = false;
	auto selectingText = _selectedItem
		&& (view == _mouseActionItem)
		&& (view == Element::Hovered());
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
		if (base::IsAltPressed()) {
			request.flags &= ~Ui::Text::StateRequest::Flag::LookupLink;
		}
		dragState = view->textState(itemPoint, request);
		lnkhost = view;
		if (item->isService()) {
			dragState.cursor = CursorState::LogAdminService;
		}
		if (!dragState.link && itemPoint.x() >= st::historyPhotoLeft && itemPoint.x() < st::historyPhotoLeft + st::msgPhotoSize) {
			if (!item->isService() && view->hasFromPhoto()) {
				enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
					// stop enumeration if the userpic is below our point
					if (userpicTop > point.y()) {
						return false;
					}

					// stop enumeration if we've found a userpic under the cursor
					if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
						dragState.link = view->data()->from()->openLink();
						dragStateUserpic = true;
						lnkhost = view;
						return false;
					}
					return true;
				});
			}
		}
	}
	_overSenderUserpic = dragStateUserpic;
	auto lnkChanged = ClickHandler::setActive(dragState.link, lnkhost);
	if (lnkChanged || dragState.cursor != _mouseCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.link
		|| dragState.cursor == CursorState::Date
		|| dragState.cursor == CursorState::Forwarded
		|| dragState.cursor == CursorState::LogAdminService) {
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
				_selectedTextSelection = _mouseActionItem->selectionFromStates(
					_mouseTextAnchor,
					dragState,
					_mouseSelectType);
				repaintItem(_mouseActionItem);
				if (!_wasSelectedText && !_selectedTextSelection.empty()) {
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
	if (const auto pressedView = Element::PressedLink()) {
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
	//			mimeData->setData(u"application/x-td-forward"_q, "1");
	//		}
	//	}
	//	_controller->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryView::Media*>(nullptr);
	//	if (auto pressedItem = Element::Pressed()) {
	//		pressedMedia = pressedItem->media();
	//		if (_mouseCursorState == CursorState::Date) {
	//			forwardMimeType = u"application/x-td-forward"_q;
	//			session().data().setMimeForwardIds(
	//				session().data().itemOrItsGroup(pressedItem->data()));
	//		}
	//	}
	//	if (auto pressedLnkItem = Element::PressedLink()) {
	//		if ((pressedMedia = pressedLnkItem->media())) {
	//			if (forwardMimeType.isEmpty()
	//				&& pressedMedia->dragItemByHandler(pressedHandler)) {
	//				forwardMimeType = u"application/x-td-forward"_q;
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

void InnerWidget::repaintItem(const Element *view, QRect rect) {
	if (rect.isNull()) {
		return repaintItem(view);
	}
	if (!view) {
		return;
	}
	const auto top = itemTop(view);
	update(rect.translated(0, top));
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

bool InnerWidget::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return RpWidget::eventHook(e);
}

void InnerWidget::onTouchSelect() {
	_touchSelect = true;
	mouseActionStart(_touchPos, Qt::LeftButton);
}

void InnerWidget::onTouchScrollTimer() {
	auto nowTime = crl::now();
	if (_touchScrollState == Ui::TouchScrollState::Acceleration
		&& _touchWaitingAcceleration
		&& (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = Ui::TouchScrollState::Manual;
		touchResetSpeed();
	} else if (_touchScrollState == Ui::TouchScrollState::Auto
		|| _touchScrollState == Ui::TouchScrollState::Acceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = !delta.isNull();
		if (hasScrolled) {
			_scrollToSignal.fire_copy(_visibleTop - delta.y());
		}

		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = Ui::TouchScrollState::Manual;
			_touchScroll = false;
			_touchScrollTimer.cancel();
		} else {
			_touchTime = nowTime;
		}
		touchDeaccelerate(elapsed);
	}
}

void InnerWidget::touchUpdateSpeed() {
	const auto nowTime = crl::now();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			const int newSpeedY = (qAbs(pixelsPerSecond.y())
					> Ui::kFingerAccuracyThreshold)
				? pixelsPerSecond.y()
				: 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x())
					> Ui::kFingerAccuracyThreshold)
				? pixelsPerSecond.x()
				: 0;
			if (_touchScrollState == Ui::TouchScrollState::Auto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(std::clamp(
						(oldSpeedY + (newSpeedY / 4)),
						-Ui::kMaxScrollAccelerated,
						+Ui::kMaxScrollAccelerated));
					_touchSpeed.setX(std::clamp(
						(oldSpeedX + (newSpeedX / 4)),
						-Ui::kMaxScrollAccelerated,
						+Ui::kMaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(std::clamp(
						(_touchSpeed.x() / 4) + (newSpeedX * 3 / 4),
						-Ui::kMaxScrollFlick,
						+Ui::kMaxScrollFlick));
					_touchSpeed.setY(std::clamp(
						(_touchSpeed.y() / 4) + (newSpeedY * 3 / 4),
						-Ui::kMaxScrollFlick,
						+Ui::kMaxScrollFlick));
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

void InnerWidget::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

void InnerWidget::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0)
		? x
		: (x > 0)
		? qMax(0, x - elapsed)
		: qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0)
		? y
		: (y > 0)
		? qMax(0, y - elapsed)
		: qMin(0, y + elapsed));
}

void InnerWidget::touchEvent(QTouchEvent *e) {
	if (e->type() == QEvent::TouchCancel) {
		if (!_touchInProgress) {
			return;
		}
		_touchInProgress = false;
		_touchSelectTimer.cancel();
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
			return;
		}
		if (_touchInProgress || e->touchPoints().isEmpty()) {
			return;
		}

		_touchInProgress = true;
		if (_touchScrollState == Ui::TouchScrollState::Auto) {
			_touchScrollState = Ui::TouchScrollState::Acceleration;
			_touchWaitingAcceleration = true;
			_touchAccelerationTime = crl::now();
			touchUpdateSpeed();
			_touchStart = _touchPos;
		} else {
			_touchScroll = false;
			_touchSelectTimer.callOnce(QApplication::startDragTime());
		}
		_touchSelect = false;
		_touchStart = _touchPrevPos = _touchPos;
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchInProgress) {
			return;
		} else if (_touchSelect) {
			mouseActionUpdate(_touchPos);
		} else if (!_touchScroll
				&& (_touchPos - _touchStart).manhattanLength()
					>= QApplication::startDragDistance()) {
			_touchSelectTimer.cancel();
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
		if (!_touchInProgress) {
			return;
		}
		_touchInProgress = false;
		const auto notMoved = (_touchPos - _touchStart).manhattanLength()
			< QApplication::startDragDistance();
		auto weak = base::make_weak(this);
		if (_touchSelect) {
			if (notMoved) {
				mouseActionFinish(_touchPos, Qt::RightButton);
				auto contextMenu = QContextMenuEvent(
					QContextMenuEvent::Mouse,
					mapFromGlobal(_touchPos),
					_touchPos);
				showContextMenu(&contextMenu, true);
			}
			_touchScroll = false;
		} else if (_touchScroll) {
			if (_touchScrollState == Ui::TouchScrollState::Manual) {
				_touchScrollState = Ui::TouchScrollState::Auto;
				_touchPrevPosValid = false;
				_touchScrollTimer.callEach(15);
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
		} else if (notMoved) {
			mouseActionStart(_touchPos, Qt::LeftButton);
			mouseActionFinish(_touchPos, Qt::LeftButton);
		}
		if (weak) {
			_touchSelectTimer.cancel();
			_touchSelect = false;
		}
	} break;
	}
}

void InnerWidget::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	_scrollToSignal.fire_copy(_visibleTop - (_touchPos - _touchPrevPos).y());
	touchUpdateSpeed();
}

InnerWidget::~InnerWidget() {
	_beingDestroyed = true;
	clearDisplayItems(DisplayPointerScope::All);
	base::take(_items);
}

} // namespace AdminLog
