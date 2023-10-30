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
#include "history/history_item_components.h"
#include "history/history_item_text.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/admin_log/history_admin_log_filter.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_cursor_state.h"
#include "chat_helpers/message_field.h"
#include "boxes/sticker_set_box.h"
#include "ui/boxes/confirm_box.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/application.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "api/api_attached_stickers.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/inactive_press.h"
#include "ui/painter.h"
#include "ui/effects/path_shift_gradient.h"
#include "core/click_handler_types.h"
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
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

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
, _scrollDateCheck([=] { scrollDateCheck(); })
, _emptyText(
		st::historyAdminLogEmptyWidth
		- st::historyAdminLogEmptyPadding.left()
		- st::historyAdminLogEmptyPadding.left())
, _antiSpamValidator(_controller, _channel) {
	Window::ChatThemeValueFromPeer(
		controller,
		channel
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

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
	session().data().itemDataChanges(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
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
	}) | rpl::start_with_next([=](
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
	) | rpl::start_with_next([=](bool wide) {
		_isChatWide = wide;
	}, lifetime());

	updateEmptyText();

	_antiSpamValidator.resolveUser(crl::guard(
		this,
		[=] { requestAdmins(); }));
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
	session().data().itemVisibilitiesUpdated();
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
	const auto offset = 0;
	const auto participantsHash = uint64(0);
	_api.request(MTPchannels_GetParticipants(
		_channel->inputChannel,
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
	auto hasSearch = !_searchQuery.isEmpty();
	auto hasFilter = (_filter.flags != 0) || !_filter.allUsers;
	auto text = Ui::Text::Semibold((hasSearch || hasFilter)
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
	if (_mouseCursorState == CursorState::Date
		&& _mouseAction == MouseAction::None) {
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
	_controller->openPhoto(photo, { context });
}

void InnerWidget::elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	_controller->openDocument(document, showInMediaView, { context });
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

bool InnerWidget::elementAnimationsPaused() {
	return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

bool InnerWidget::elementHideReply(not_null<const Element*> view) {
	return true;
}

bool InnerWidget::elementShownUnread(not_null<const Element*> view) {
	return false;
}

void InnerWidget::elementSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void InnerWidget::elementHandleViaClick(not_null<UserData*> bot) {
}

bool InnerWidget::elementIsChatWide() {
	return _isChatWide;
}

not_null<Ui::PathShiftGradient*> InnerWidget::elementPathShiftGradient() {
	return _pathGradient.get();
}

void InnerWidget::elementReplyTo(const FullReplyTo &to) {
}

void InnerWidget::elementStartInteraction(not_null<const Element*> view) {
}

void InnerWidget::elementStartPremium(
	not_null<const Element*> view,
	Element *replacing) {
}

void InnerWidget::elementCancelPremium(not_null<const Element*> view) {
}

QString InnerWidget::elementAuthorRank(not_null<const Element*> view) {
	return {};
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
	const auto filter = [&] {
		using Flag = MTPDchannelAdminLogEventsFilter::Flag;
		using LocalFlag = FilterValue::Flag;
		const auto empty = MTPDchannelAdminLogEventsFilter::Flags(0);
		const auto f = _filter.flags;
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
			| ((f & LocalFlag::Topics) ? Flag::f_forums : empty);
	}();
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

	const auto antiSpamUserId = _antiSpamValidator.userId();
	for (const auto &event : events) {
		const auto &data = event.data();
		const auto id = data.vid().v;
		if (_eventIds.find(id) != _eventIds.end()) {
			return;
		}
		const auto rememberRealMsgId = (antiSpamUserId
			== peerToUser(peerFromUser(data.vuser_id())));

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
			if (rememberRealMsgId && realId) {
				_antiSpamValidator.addEventMsgId(
					item->data()->fullId(),
					realId);
			}
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
				view->setAttachToPrevious(attach, previous);
				previous->setAttachToNext(attach, view);
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
	if (_controller->contentOverlapped(this, e)) {
		return;
	}

	const auto guard = gsl::finally([&] {
		_userpicsCache.clear();
	});

	Painter p(this);

	auto clip = e->rect();
	auto context = _controller->preparePaintContext({
		.theme = _theme.get(),
		.visibleAreaTop = _visibleTop,
		.visibleAreaTopGlobal = mapToGlobal(QPoint(0, _visibleTop)).y(),
		.visibleAreaWidth = width(),
		.clip = clip,
	});
	if (_items.empty() && _upLoaded && _downLoaded) {
		paintEmpty(p, context.st);
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
			context.translate(0, -top);
			p.translate(0, top);
			for (auto i = from; i != to; ++i) {
				const auto view = i->get();
				context.outbg = view->hasOutLayout();
				context.selection = (view == _selectedItem)
					? _selectedText
					: TextSelection();
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
		if (Element::Moused() && Element::Moused() == Element::Hovered()) {
			auto mousePos = mapPointToItem(
				mapFromGlobal(_mousePosition),
				Element::Moused());
			StateRequest request;
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			auto dragState = Element::Moused()->textState(mousePos, request);
			if (dragState.cursor == CursorState::Text
				&& base::in_range(dragState.symbol, selFrom, selTo)) {
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
				_menu->addAction(lnkIsVideo ? tr::lng_context_save_video(tr::now) : (lnkIsVoice ?  tr::lng_context_save_audio(tr::now) : (lnkIsAudio ?  tr::lng_context_save_audio_file(tr::now) :  tr::lng_context_save_file(tr::now))), base::fn_delayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [this, lnkDocument] {
					saveDocumentToFile(lnkDocument);
				}), &st::menuIconDownload);
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
			suggestRestrictParticipant(participant);
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
						|| item->Has<HistoryMessageLogEntryOriginal>())) {
					_menu->addAction(tr::lng_context_copy_text(tr::now), [=] {
						copyContextText(itemId);
					}, &st::menuIconCopy);
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
		not_null<PeerData*> participant) {
	Expects(_menu != nullptr);

	if (!_channel->isMegagroup()
		|| !_channel->canBanMembers()
		|| _admins.empty()) {
		return;
	}
	if (ranges::contains(_admins, participant)) {
		if (!ranges::contains(_adminsCanEdit, participant)) {
			return;
		}
	}
	_menu->addAction(tr::lng_context_restrict_user(tr::now), [=] {
		const auto user = participant->asUser();
		auto editRestrictions = [=](bool hasAdminRights, ChatRestrictionsInfo currentRights) {
			auto weak = QPointer<InnerWidget>(this);
			auto weakBox = std::make_shared<QPointer<Ui::BoxContent>>();
			auto box = Box<EditRestrictedBox>(_channel, user, hasAdminRights, currentRights);
			box->setSaveCallback([=](
					ChatRestrictionsInfo oldRights,
					ChatRestrictionsInfo newRights) {
				if (weak) {
					weak->restrictParticipant(participant, oldRights, newRights);
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
			auto weakBox = std::make_shared<QPointer<Ui::BoxContent>>();
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
			}).fail([=] {
				editRestrictions(false, ChatRestrictionsInfo());
			}).send();
		}
	}, &st::menuIconPermissions);
}

void InnerWidget::restrictParticipant(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo oldRights,
		ChatRestrictionsInfo newRights) {
	const auto done = [=](ChatRestrictionsInfo newRights) {
		restrictParticipantDone(participant, newRights);
	};
	const auto callback = SaveRestrictedCallback(
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
	return TWidget::enterEventHook(e);
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
	return TWidget::leaveEventHook(e);
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
				auto selection = TextSelection { dragState.symbol, dragState.symbol };
				repaintItem(std::exchange(_selectedItem, _mouseActionItem));
				_selectedText = selection;
				_mouseTextSymbol = dragState.symbol;
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
	if (const auto view = Element::Pressed()) {
		repaintItem(view);
		Element::Pressed(nullptr);
	}

	_wasSelectedText = false;

	if (activated) {
		mouseActionCancel();
		ActivateClickHandler(window(), activated, {
			button,
			QVariant::fromValue(ClickHandlerContext{
				.elementDelegate = [weak = Ui::MakeWeak(this)] {
					return weak
						? (ElementDelegate*)weak
						: nullptr;
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
						lnkhost = view;
						return false;
					}
					return true;
				});
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
