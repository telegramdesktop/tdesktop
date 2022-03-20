/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_search.h"

#include "api/api_messages_search.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/effects/show_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"

namespace HistoryView {
namespace {

class TopBar final : public Ui::RpWidget {
public:
	TopBar(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] rpl::producer<QString> searchRequests() const;

private:
	base::unique_qptr<Ui::IconButton> _cancel;
	base::unique_qptr<Ui::MultiSelect> _select;

	base::Timer _searchTimer;

	rpl::event_stream<QString> _searchRequests;
};

TopBar::TopBar(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _cancel(base::make_unique_q<Ui::IconButton>(this, st::historyTopBarBack))
, _select(base::make_unique_q<Ui::MultiSelect>(
	this,
	st::searchInChatMultiSelect,
	tr::lng_dlg_filter()))
, _searchTimer([=] { _searchRequests.fire(_select->getQuery()); }) {

	parent->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		moveToLeft(0, 0);
		resize(r.width(), st::topBarHeight);
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_cancel->moveToLeft(0, (s.height() - _cancel->height()) / 2);

		const auto selectLeft = _cancel->x() + _cancel->width();
		_select->resizeToWidth(s.width() - selectLeft);
		_select->moveToLeft(selectLeft, (s.height() - _select->height()) / 2);

	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		Painter p(this);

		p.fillRect(r, st::dialogsBg);
	}, lifetime());

	_select->setQueryChangedCallback([=](const QString &query) {
		_searchTimer.callOnce(AutoSearchTimeout);
	});

	_select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		_searchRequests.fire(_select->getQuery());
	});

	_select->setCancelledCallback([=] {

	});
}

rpl::producer<QString> TopBar::searchRequests() const {
	return _searchRequests.events();
}

class BottomBar final : public Ui::RpWidget {
public:
	using Index = int;
	BottomBar(not_null<Ui::RpWidget*> parent);

	void setTotal(int total);

	[[nodiscard]] rpl::producer<Index> showItemRequests() const;

private:
	void updateText(int current);

	base::unique_qptr<Ui::IconButton> _previous;
	base::unique_qptr<Ui::IconButton> _next;
	base::unique_qptr<Ui::FlatLabel> _counter;

	int _total = -1;
	rpl::variable<int> _current = 0;
};

BottomBar::BottomBar(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
// Icons are swaped.
, _previous(base::make_unique_q<Ui::IconButton>(this, st::calendarNext))
, _next(base::make_unique_q<Ui::IconButton>(this, st::calendarPrevious))
, _counter(base::make_unique_q<Ui::FlatLabel>(
	this,
	st::defaultSettingsRightLabel)) {

	parent->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		const auto height = st::historyComposeButton.height;
		resize(r.width(), height);
		moveToLeft(0, r.height() - height);
	}, lifetime());

	rpl::merge(
		_counter->sizeValue() | rpl::map([=] { return size(); }),
		sizeValue()
	) | rpl::start_with_next([=](const QSize &s) {
		_previous->moveToRight(0, (s.height() - _previous->height()) / 2);
		_next->moveToRight(
			_previous->width(),
			(s.height() - _next->height()) / 2);

		const auto left = st::topBarActionSkip;
		_counter->moveToLeft(left, (s.height() - _counter->height()) / 2);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		Painter p(this);

		p.fillRect(r, st::dialogsBg);
	}, lifetime());

	_current.value(
	) | rpl::start_with_next([=](int current) {
		const auto nextDisabled = (current <= 0) || (current >= _total);
		const auto prevDisabled = (current <= 1);
		_next->setAttribute(Qt::WA_TransparentForMouseEvents, nextDisabled);
		_previous->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			prevDisabled);
		_next->setIconOverride(nextDisabled
			? &st::calendarPreviousDisabled
			: nullptr);
		_previous->setIconOverride(prevDisabled
			? &st::calendarNextDisabled
			: nullptr);

		updateText(current);
	}, lifetime());

	rpl::merge(
		_next->clicks() | rpl::map_to(1),
		_previous->clicks() | rpl::map_to(-1)
	) | rpl::start_with_next([=](int way) {
		_current = _current.current() + way;
	}, lifetime());
}

void BottomBar::setTotal(int total) {
	_total = total;
	_current.force_assign(1);
}

void BottomBar::updateText(int current) {
	if (_total < 0) {
		_counter->setText(QString());
	} else if (_total) {
		_counter->setText(tr::lng_search_messages_n_of_amount(
			tr::now,
			lt_n,
			QString::number(current),
			lt_amount,
			QString::number(_total)));
	} else {
		_counter->setText(tr::lng_search_messages_none(tr::now));
	}
}

rpl::producer<BottomBar::Index> BottomBar::showItemRequests() const {
	return _current.changes() | rpl::map(rpl::mappers::_1 - 1);
}

} // namespace

class ComposeSearch::Inner final {
public:
	Inner(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<History*> history);
	~Inner();

private:
	void showAnimated();
	void hideAnimated();

	const not_null<Window::SessionController*> _window;
	const not_null<History*> _history;
	const base::unique_qptr<TopBar> _topBar;
	const base::unique_qptr<BottomBar> _bottomBar;

	Api::MessagesSearch _apiSearch;
	Api::FoundMessages _apiFound;

	struct {
		struct {
			QString token;
			BottomBar::Index index = -1;
		} data;
		rpl::event_stream<BottomBar::Index> jumps;
	} _pendingJump;

};

ComposeSearch::Inner::Inner(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history)
: _window(window)
, _history(history)
, _topBar(base::make_unique_q<TopBar>(parent))
, _bottomBar(base::make_unique_q<BottomBar>(parent))
, _apiSearch(&window->session(), history) {
	showAnimated();

	_topBar->searchRequests(
	) | rpl::start_with_next([=](const QString &query) {
		if (query.isEmpty()) {
			return;
		}
		_apiFound = {};
		_apiSearch.searchMessages(query, nullptr);
	}, _topBar->lifetime());

	_apiSearch.messagesFounds(
	) | rpl::start_with_next([=](const Api::FoundMessages &data) {
		if (data.nextToken == _apiFound.nextToken) {
			for (const auto &message : data.messages) {
				_apiFound.messages.push_back(message);
			}
			if (_pendingJump.data.token == data.nextToken) {
				_pendingJump.jumps.fire_copy(_pendingJump.data.index);
			}
		} else {
			_apiFound = data;
			_bottomBar->setTotal(data.total);
		}
	}, _topBar->lifetime());

	rpl::merge(
		_pendingJump.jumps.events() | rpl::filter(rpl::mappers::_1 >= 0),
		_bottomBar->showItemRequests()
	) | rpl::start_with_next([=](BottomBar::Index index) {
		const auto &messages = _apiFound.messages;
		const auto size = int(messages.size());
		if (index >= (size - 1) && size != _apiFound.total) {
			_apiSearch.searchMore();
		}
		if (index >= size || index < 0) {
			_pendingJump.data = { _apiFound.nextToken, index };
			return;
		}
		_pendingJump.data = {};
		const auto itemId = _apiFound.messages[index];
		const auto item = _history->owner().message(itemId);
		if (item) {
			_window->jumpToChatListEntry({
				{ item->history() },
				item->fullId(),
			});
		}
	}, _bottomBar->lifetime());
}

void ComposeSearch::Inner::showAnimated() {
	// Don't animate bottom bar.
	_bottomBar->show();
	Ui::Animations::ShowWidgets({ _topBar.get() });
}

void ComposeSearch::Inner::hideAnimated() {
	Ui::Animations::HideWidgets({ _topBar.get(), _bottomBar.get() });
}

ComposeSearch::Inner::~Inner() {
}

ComposeSearch::ComposeSearch(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history)
: _inner(std::make_unique<Inner>(parent, window, history)) {
}

ComposeSearch::~ComposeSearch() {
}

} // namespace HistoryView
