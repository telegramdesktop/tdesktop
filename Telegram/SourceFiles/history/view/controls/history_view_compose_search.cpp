/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_search.h"

#include "api/api_messages_search_merged.h"
#include "boxes/peer_list_box.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/data_message_reactions.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_search_from_controllers.h" // SearchFromBox
#include "dialogs/dialogs_search_tags.h"
#include "dialogs/ui/dialogs_layout.h"
#include "history/view/history_view_context_menu.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/show_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/scroll_area.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"

namespace HistoryView {
namespace {

using Activation = ComposeSearch::Activation;
using SearchRequest = Api::MessagesSearchMerged::Request;

[[nodiscard]] inline bool HasChooseFrom(not_null<History*> history) {
	if (const auto peer = history->peer) {
		return (peer->isChat() || peer->isMegagroup());
	}
	return false;
}

class Row final : public PeerListRow {
public:
	explicit Row(std::unique_ptr<Dialogs::FakeRow> fakeRow);

	[[nodiscard]] FullMsgId fullId() const;

	QRect elementGeometry(int element, int outerWidth) const override;
	void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) override;
	void elementsStopLastRipple() override;
	void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) override;

private:
	const std::unique_ptr<Dialogs::FakeRow> _fakeRow;

	int _outerWidth = 0;

};

Row::Row(std::unique_ptr<Dialogs::FakeRow> fakeRow)
: PeerListRow(
	fakeRow->searchInChat().history()->peer,
	fakeRow->item()->fullId().msg.bare)
, _fakeRow(std::move(fakeRow)) {
}

FullMsgId Row::fullId() const {
	return _fakeRow->item()->fullId();
}

QRect Row::elementGeometry(int element, int outerWidth) const {
	return QRect(0, 0, outerWidth, st::dialogsRowHeight);
}

void Row::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	_fakeRow->addRipple(
		point,
		{ _outerWidth, st::dialogsRowHeight },
		std::move(updateCallback));
}

void Row::elementsStopLastRipple() {
	_fakeRow->stopLastRipple();
}

void Row::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	_outerWidth = outerWidth;
	Dialogs::Ui::RowPainter::Paint(p, _fakeRow.get(), {
		.st = &st::defaultDialogRow,
		.currentBg = st::dialogsBg,
		.now = crl::now(),
		.width = outerWidth,
		.selected = selected,
		.paused = p.inactive(),
	});
}

class ListController final : public PeerListController {
public:
	explicit ListController(not_null<History*> history);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowElementClicked(not_null<PeerListRow*> row, int element) override;

	void loadMoreRows() override;

	void addItems(const MessageIdsList &ids, bool clear);

	[[nodiscard]] rpl::producer<FullMsgId> showItemRequests() const;
	[[nodiscard]] rpl::producer<> searchMoreRequests() const;
	[[nodiscard]] rpl::producer<> resetScrollRequests() const;

private:
	const not_null<History*> _history;
	rpl::event_stream<FullMsgId> _showItemRequests;
	rpl::event_stream<> _searchMoreRequests;
	rpl::event_stream<> _resetScrollRequests;

};

ListController::ListController(not_null<History*> history)
: _history(history) {
}

Main::Session &ListController::session() const {
	return _history->owner().session();
}

void ListController::prepare() {
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	_showItemRequests.fire_copy(static_cast<Row*>(row.get())->fullId());
}

void ListController::rowElementClicked(
		not_null<PeerListRow*> row,
		int element) {
	ListController::rowClicked(row);
}

void ListController::loadMoreRows() {
	_searchMoreRequests.fire({});
}

rpl::producer<FullMsgId> ListController::showItemRequests() const {
	return _showItemRequests.events();
}

rpl::producer<> ListController::searchMoreRequests() const {
	return _searchMoreRequests.events();
}

rpl::producer<> ListController::resetScrollRequests() const {
	return _resetScrollRequests.events();
}

void ListController::addItems(const MessageIdsList &ids, bool clear) {
	if (clear) {
		_resetScrollRequests.fire({});
		for (auto i = 0; i != delegate()->peerListFullRowsCount();) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(i));
		}
	}

	const auto &owner = _history->owner();
	const auto key = Dialogs::Key{ _history };
	for (const auto &id : ids) {
		if (const auto item = owner.message(id)) {
			const auto shared = std::make_shared<Row*>(nullptr);
			auto row = std::make_unique<Row>(
				std::make_unique<Dialogs::FakeRow>(
					key,
					item,
					[=] { delegate()->peerListUpdateRow(*shared); }));
			*shared = row.get();
			delegate()->peerListAppendRow(std::move(row));
		}
	}

	delegate()->peerListRefreshRows();

	if (!delegate()->peerListFullRowsCount()) {
		_showItemRequests.fire({});
	}
}

struct List {
	base::unique_qptr<Ui::RpWidget> container;
	std::unique_ptr<ListController> controller;
};

List CreateList(
		not_null<Ui::RpWidget*> parent,
		not_null<History*> history) {
	auto list = List{
		base::make_unique_q<Ui::RpWidget>(parent),
		std::make_unique<ListController>(history),
	};
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(list.container.get());

	using Delegate = PeerListContentDelegateSimple;
	const auto delegate = scroll->lifetime().make_state<Delegate>();
	list.controller->setStyleOverrides(&st::searchInChatPeerList);

	const auto content = scroll->setOwnedWidget(
		object_ptr<PeerListContent>(scroll, list.controller.get()));

	list.controller->resetScrollRequests(
	) | rpl::start_with_next([=] {
		scroll->scrollToY(0);
	}, scroll->lifetime());

	scroll->scrolls(
	) | rpl::start_with_next([=] {
		const auto top = scroll->scrollTop();
		content->setVisibleTopBottom(top, top + scroll->height());
	}, scroll->lifetime());

	delegate->setContent(content);
	list.controller->setDelegate(delegate);

	list.container->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		content->resize(size.width(), content->height());
		scroll->resize(size);
	}, list.container->lifetime());

	list.container->paintRequest(
	) | rpl::start_with_next([weak = Ui::MakeWeak(list.container.get())](
			const QRect &r) {
		auto p = QPainter(weak);
		p.fillRect(r, st::dialogsBg);
	}, list.container->lifetime());

	return list;
}

class TopBar final : public Ui::RpWidget {
public:
	TopBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<History*> history,
		PeerData *from,
		const QString &query);

	void setInnerFocus();
	void setQuery(const QString &query);

	[[nodiscard]] rpl::producer<SearchRequest> searchRequests() const;
	[[nodiscard]] rpl::producer<PeerData*> fromValue() const;
	[[nodiscard]] rpl::producer<> queryChanges() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<not_null<QKeyEvent*>> keyEvents() const;

	void setFrom(PeerData *peer);
	bool handleKeyPress(not_null<QKeyEvent*> e);

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void clearItems();
	void refreshTags();
	void updateSize();
	void requestSearch(bool cache = true);
	void requestSearchDelayed();

	base::unique_qptr<Ui::IconButton> _cancel;
	std::vector<Data::ReactionId> _searchTagsSelected;
	base::unique_qptr<Ui::MultiSelect> _select;
	std::unique_ptr<Dialogs::SearchTags> _searchTags;
	base::unique_qptr<Ui::PopupMenu> _menu;
	std::optional<QPoint> _mouseGlobalPosition;

	const not_null<Window::SessionController*> _window;
	const not_null<History*> _history;
	rpl::variable<PeerData*> _from = nullptr;

	base::Timer _searchTimer;

	Api::MessagesSearchMerged::CachedRequests _typedRequests;

	rpl::event_stream<SearchRequest> _searchRequests;
	rpl::event_stream<> _queryChanges;
	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<not_null<QKeyEvent*>> _keyEvents;
};

TopBar::TopBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history,
	PeerData *from,
	const QString &query)
: Ui::RpWidget(parent)
, _cancel(base::make_unique_q<Ui::IconButton>(this, st::historyTopBarBack))
, _searchTagsSelected(Data::SearchTagsFromQuery(query))
, _select(base::make_unique_q<Ui::MultiSelect>(
	this,
	st::searchInChatMultiSelect,
	tr::lng_dlg_filter(),
	_searchTagsSelected.empty() ? query : QString()))
, _window(window)
, _history(history)
, _searchTimer([=] { requestSearch(); }) {
	if (from) {
		setFrom(from);
	}
	refreshTags();

	moveToLeft(0, 0);

	parent->geometryValue(
	) | rpl::start_with_next([=] {
		updateSize();
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto height = st::topBarHeight;
		_cancel->moveToLeft(0, (height - _cancel->height()) / 2);

		const auto selectLeft = _cancel->x() + _cancel->width();
		_select->resizeToWidth(s.width() - selectLeft);
		_select->moveToLeft(selectLeft, (height - _select->height()) / 2);

	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(this);
		p.fillRect(r, st::dialogsBg);
	}, lifetime());

	_select->setQueryChangedCallback([=](const QString &) {
		requestSearchDelayed();
		_queryChanges.fire({});
	});

	_select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		requestSearch();
	});

	_select->setCancelledCallback([=] {
		_cancelRequests.fire({});
	});
}

void TopBar::keyPressEvent(QKeyEvent *e) {
	_keyEvents.fire_copy(e);
}

bool TopBar::handleKeyPress(not_null<QKeyEvent*> e) {
	return false;
}

rpl::producer<not_null<QKeyEvent*>> TopBar::keyEvents() const {
	return _keyEvents.events();
}

void TopBar::setInnerFocus() {
	if (Ui::AppInFocus() && Ui::InFocusChain(_select->window())) {
		_select->setInnerFocus();
	}
}

void TopBar::updateSize() {
	const auto height = st::topBarHeight
		+ (_searchTags ? _searchTags->height() : 0);
	resize(parentWidget()->width(), height);
}

void TopBar::setQuery(const QString &query) {
	if (auto tags = Data::SearchTagsFromQuery(query); !tags.empty()) {
		if (_searchTagsSelected != tags) {
			_searchTagsSelected = std::move(tags);
			refreshTags();
		}
		_select->setQuery(QString());
	} else {
		_select->setQuery(query);
	}
}

void TopBar::clearItems() {
	_select->setItemRemovedCallback(nullptr);

	for (const auto &id : _select->getItems()) {
		_select->removeItem(id);
	}

	_select->setItemRemovedCallback([=](uint64) {
		_from = nullptr;
		requestSearchDelayed();
	});
}

void TopBar::refreshTags() {
	if (!_history->peer->isSelf()) {
		_searchTags = nullptr;
		return;
	}
	auto fullTagsList = _from.value() | rpl::map([=](PeerData *from) {
		const auto sublist = from
			? _history->owner().savedMessages().sublist(from).get()
			: nullptr;
		return _history->owner().reactions().myTagsValue(sublist);
	}) | rpl::flatten_latest();
	_searchTags = std::make_unique<Dialogs::SearchTags>(
		&_history->owner(),
		std::move(fullTagsList),
		_searchTagsSelected);

	const auto parent = _searchTags->lifetime().make_state<Ui::RpWidget>(
		this);
	const auto shadow = _searchTags->lifetime().make_state<Ui::PlainShadow>(
		parentWidget());
	parent->show();

	_searchTags->heightValue(
	) | rpl::start_with_next([=](int height) {
		updateSize();
		shadow->setVisible(height > 0);
	}, _searchTags->lifetime());

	geometryValue() | rpl::start_with_next([=](QRect geometry) {
		shadow->setGeometry(
			geometry.x(),
			geometry.y() + geometry.height(),
			geometry.width(),
			st::lineWidth);
	}, shadow->lifetime());

	_searchTags->selectedChanges(
	) | rpl::start_with_next([=](std::vector<Data::ReactionId> &&list) {
		_searchTagsSelected = std::move(list);
		requestSearch(false);
	}, _searchTags->lifetime());

	_searchTags->menuRequests(
	) | rpl::start_with_next([=](Data::ReactionId id) {
		ShowTagInListMenu(
			&_menu,
			_mouseGlobalPosition.value_or(QCursor::pos()),
			this,
			id,
			_window);
	}, _searchTags->lifetime());

	if (!_searchTagsSelected.empty()) {
		crl::on_main(this, [=] {
			requestSearch(false);
		});
	}

	const auto padding = st::searchInChatTagsPadding;
	const auto position = QPoint(padding.left(), padding.top());

	_searchTags->repaintRequests() | rpl::start_with_next([=] {
		parent->update();
	}, _searchTags->lifetime());

	widthValue() | rpl::start_with_next([=](int width) {
		width -= padding.left() + padding.right();
		_searchTags->resizeToWidth(width);
	}, _searchTags->lifetime());

	rpl::combine(
		widthValue(),
		_searchTags->heightValue()
	) | rpl::start_with_next([=](int width, int height) {
		height += padding.top() + padding.bottom();
		parent->setGeometry(0, st::topBarHeight, width, height);
	}, _searchTags->lifetime());

	parent->paintRequest() | rpl::start_with_next([=](const QRect &r) {
		auto p = Painter(parent);
		p.fillRect(r, st::dialogsBg);
		_searchTags->paint(p, position, crl::now(), false);
	}, parent->lifetime());

	parent->setMouseTracking(true);
	parent->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			_mouseGlobalPosition = mouse->globalPos();
			const auto point = mouse->pos() - position;
			const auto handler = _searchTags->lookupHandler(point);
			ClickHandler::setActive(handler);
			parent->setCursor(handler
				? style::cur_pointer
				: style::cur_default);
		} else if (e->type() == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				ClickHandler::pressed();
			}
		} else if (e->type() == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				const auto handler = ClickHandler::unpressed();
				ActivateClickHandler(parent, handler, ClickContext{
					.button = mouse->button(),
					.other = QVariant::fromValue(ClickHandlerContext{
						.sessionWindow = _window,
					}),
				});
			}
		}
	}, parent->lifetime());
}

void TopBar::requestSearch(bool cache) {
	const auto search = SearchRequest{
		_select->getQuery(),
		_from.current(),
		_searchTagsSelected
	};
	if (cache) {
		_typedRequests.insert(search);
	}
	_searchRequests.fire_copy(search);
}

void TopBar::requestSearchDelayed() {
	// Check cached queries.
	const auto search = SearchRequest{
		_select->getQuery(),
		_from.current(),
		_searchTagsSelected
	};
	if (_typedRequests.contains(search)) {
		requestSearch(false);
		return;
	}

	_searchTimer.callOnce(AutoSearchTimeout);
}

rpl::producer<SearchRequest> TopBar::searchRequests() const {
	return _searchRequests.events();
}

rpl::producer<> TopBar::queryChanges() const {
	return _queryChanges.events();
}

rpl::producer<> TopBar::closeRequests() const {
	return _cancel->clicks() | rpl::to_empty;
}

rpl::producer<> TopBar::cancelRequests() const {
	return _cancelRequests.events();
}

rpl::producer<PeerData*> TopBar::fromValue() const {
	return _from.value();
}

void TopBar::setFrom(PeerData *peer) {
	clearItems();

	const auto guard = gsl::finally([&] {
		_from = peer;
		requestSearchDelayed();
	});
	if (!peer || _history->peer->isSelf()) {
		return;
	}

	_select->addItem(
		peer->id.value,
		tr::lng_dlg_search_from(tr::now, lt_user, peer->shortName()),
		st::activeButtonBg,
		PaintUserpicCallback(peer, false),
		Ui::MultiSelect::AddItemWay::Default);
}

class BottomBar final : public Ui::RpWidget {
public:
	using Index = int;
	BottomBar(not_null<Ui::RpWidget*> parent, bool fastShowChooseFrom);

	void setTotal(int total);
	void setCurrent(int current);

	[[nodiscard]] rpl::producer<Index> showItemRequests() const;
	[[nodiscard]] rpl::producer<> showCalendarRequests() const;
	[[nodiscard]] rpl::producer<> showBoxFromRequests() const;
	[[nodiscard]] rpl::producer<> showListRequests() const;

	void buttonFromToggleOn(rpl::producer<bool> &&visible);
	void buttonCalendarToggleOn(rpl::producer<bool> &&visible);

	bool handleKeyPress(not_null<QKeyEvent*> e);

private:
	void updateText(int current);

	base::unique_qptr<Ui::FlatButton> _showList;

	struct Navigation {
		base::unique_qptr<Ui::IconButton> button;
		bool enabled = false;

		Ui::IconButton *operator->() const {
			return button.get();
		}
	};

	Navigation _previous;
	Navigation _next;

	base::unique_qptr<Ui::IconButton> _jumpToDate;
	base::unique_qptr<Ui::IconButton> _chooseFromUser;
	base::unique_qptr<Ui::FlatLabel> _counter;

	int _total = -1;
	rpl::variable<int> _current = 0;
};

BottomBar::BottomBar(not_null<Ui::RpWidget*> parent, bool fastShowChooseFrom)
: Ui::RpWidget(parent)
, _showList(base::make_unique_q<Ui::FlatButton>(
	this,
	QString(),
	st::historyComposeButton))
// Icons are swaped.
, _previous({ base::make_unique_q<Ui::IconButton>(this, st::calendarNext) })
, _next({ base::make_unique_q<Ui::IconButton>(this, st::calendarPrevious) })
, _jumpToDate(base::make_unique_q<Ui::IconButton>(this, st::dialogCalendar))
, _chooseFromUser(
	base::make_unique_q<Ui::IconButton>(this, st::dialogSearchFrom))
, _counter(base::make_unique_q<Ui::FlatLabel>(
	this,
	st::defaultSettingsRightLabel)) {

	_counter->setAttribute(Qt::WA_TransparentForMouseEvents);
	_chooseFromUser->setVisible(fastShowChooseFrom);

	parent->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		const auto height = st::historyComposeButton.height;
		resize(r.width(), height);
		moveToLeft(0, r.height() - height);
	}, lifetime());

	auto mapSize = rpl::map([=] { return size(); });
	rpl::merge(
		_jumpToDate->shownValue() | mapSize,
		_chooseFromUser->shownValue() | mapSize,
		_counter->sizeValue() | mapSize,
		sizeValue()
	) | rpl::start_with_next([=](const QSize &s) {
		_showList->setGeometry(QRect(QPoint(), s));
		_previous->moveToRight(0, (s.height() - _previous->height()) / 2);
		_next->moveToRight(
			_previous->width(),
			(s.height() - _next->height()) / 2);

		auto left = st::topBarActionSkip;
		const auto list = std::vector<not_null<Ui::RpWidget*>>{
			_jumpToDate.get(),
			_chooseFromUser.get(),
			_counter.get() };
		for (const auto &w : list) {
			if (w->isHidden()) {
				continue;
			}
			w->moveToLeft(left, (s.height() - w->height()) / 2);
			left += w->width();
		}
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(this);
		p.fillRect(r, st::dialogsBg);
	}, lifetime());

	_current.value(
	) | rpl::start_with_next([=](int current) {
		const auto nextDisabled = (current <= 0) || (current >= _total);
		const auto prevDisabled = (current <= 1);
		_next.enabled = !nextDisabled;
		_previous.enabled = !prevDisabled;
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

		_showList->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			nextDisabled && prevDisabled);
		updateText(current);
	}, lifetime());

	rpl::merge(
		_next->clicks() | rpl::map_to(1),
		_previous->clicks() | rpl::map_to(-1)
	) | rpl::start_with_next([=](int way) {
		_current = _current.current() + way;
	}, lifetime());
}

bool BottomBar::handleKeyPress(not_null<QKeyEvent*> e) {
	if (e->key() == Qt::Key_F3) {
		const auto modifiers = e->modifiers();
		if (modifiers == Qt::NoModifier && _next.enabled) {
			_next->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
			return true;
		} else if (modifiers == Qt::ShiftModifier && _previous.enabled) {
			_previous->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
			return true;
		}
	}
#ifdef Q_OS_MAC
	if (e->key() == Qt::Key_G) {
		const auto modifiers = e->modifiers();
		if (modifiers.testFlag(Qt::ControlModifier)) {
			const auto &navigation = (modifiers.testFlag(Qt::ShiftModifier)
				? _previous
				: _next);
			if (navigation.enabled) {
				navigation->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
				return true;
			}
		}
	}
#endif
	return false;
}

void BottomBar::setTotal(int total) {
	_total = total;
	setCurrent(1);
}

void BottomBar::setCurrent(int current) {
	_current.force_assign(current);
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

rpl::producer<> BottomBar::showCalendarRequests() const {
	return _jumpToDate->clicks() | rpl::to_empty;
}

rpl::producer<> BottomBar::showBoxFromRequests() const {
	return _chooseFromUser->clicks() | rpl::to_empty;
}

rpl::producer<> BottomBar::showListRequests() const {
	return _showList->clicks() | rpl::to_empty;
}

void BottomBar::buttonFromToggleOn(rpl::producer<bool> &&visible) {
	std::move(
		visible
	) | rpl::start_with_next([=](bool value) {
		_chooseFromUser->setVisible(value);
	}, _chooseFromUser->lifetime());
}

void BottomBar::buttonCalendarToggleOn(rpl::producer<bool> &&visible) {
	std::move(
		visible
	) | rpl::start_with_next([=](bool value) {
		_jumpToDate->setVisible(value);
	}, _jumpToDate->lifetime());
}

} // namespace

class ComposeSearch::Inner final {
public:
	Inner(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<History*> history,
		PeerData *from,
		const QString &query);
	~Inner();

	void hideAnimated();
	void setInnerFocus();
	void setQuery(const QString &query);
	void setTopMsgId(MsgId topMsgId);

	[[nodiscard]] rpl::producer<Activation> activations() const;
	[[nodiscard]] rpl::producer<> destroyRequests() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void showAnimated();
	void hideList();

	const not_null<Window::SessionController*> _window;
	const not_null<History*> _history;
	const base::unique_qptr<TopBar> _topBar;
	const base::unique_qptr<BottomBar> _bottomBar;
	const List _list;

	Api::MessagesSearchMerged _apiSearch;

	struct {
		struct {
			QString token;
			BottomBar::Index index = -1;
		} data;
		rpl::event_stream<BottomBar::Index> jumps;
	} _pendingJump;

	MsgId _topMsgId;

	rpl::event_stream<Activation> _activations;
	rpl::event_stream<> _destroyRequests;

};

ComposeSearch::Inner::Inner(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history,
	PeerData *from,
	const QString &query)
: _window(window)
, _history(history)
, _topBar(base::make_unique_q<TopBar>(parent, window, history, from, query))
, _bottomBar(base::make_unique_q<BottomBar>(parent, HasChooseFrom(history)))
, _list(CreateList(parent, history))
, _apiSearch(history) {
	showAnimated();

	rpl::combine(
		_topBar->geometryValue(),
		_bottomBar->geometryValue()
	) | rpl::start_with_next([=](const QRect &top, const QRect &bottom) {
		_list.container->setGeometry(QRect(
			top.topLeft() + QPoint(0, top.height()),
			bottom.topLeft() + QPoint(bottom.width(), 0)));
	}, _list.container->lifetime());

	_topBar->keyEvents(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		if (!_bottomBar->handleKeyPress(e)) {
			_topBar->handleKeyPress(e);
		}
	}, _topBar->lifetime());

	_topBar->searchRequests(
	) | rpl::start_with_next([=](SearchRequest search) {
		if (search.query.isEmpty() && search.tags.empty()) {
			if (!search.from || _history->peer->isSelf()) {
				return;
			}
		}
		search.topMsgId = _topMsgId;
		_apiSearch.clear();
		_apiSearch.search(search);
	}, _topBar->lifetime());

	_topBar->queryChanges(
	) | rpl::start_with_next([=] {
		hideList();
	}, _topBar->lifetime());

	_topBar->closeRequests(
	) | rpl::start_with_next([=] {
		hideAnimated();
	}, _topBar->lifetime());

	_topBar->cancelRequests(
	) | rpl::start_with_next([=] {
		if (!_list.container->isHidden()) {
			Ui::Animations::HideWidgets({ _list.container.get() });
		} else {
			hideAnimated();
		}
	}, _topBar->lifetime());

	_apiSearch.newFounds(
	) | rpl::start_with_next([=] {
		const auto &apiData = _apiSearch.messages();
		const auto weak = Ui::MakeWeak(_bottomBar.get());
		_bottomBar->setTotal(apiData.total);
		if (weak) {
			// Activating the first search result may switch the chat.
			_list.controller->addItems(apiData.messages, true);
		}
	}, _topBar->lifetime());

	_apiSearch.nextFounds(
	) | rpl::start_with_next([=] {
		if (_pendingJump.data.token == _apiSearch.messages().nextToken) {
			_pendingJump.jumps.fire_copy(_pendingJump.data.index);
		}
		_list.controller->addItems(_apiSearch.messages().messages, false);
	}, _topBar->lifetime());

	rpl::merge(
		_pendingJump.jumps.events() | rpl::filter(rpl::mappers::_1 >= 0),
		_bottomBar->showItemRequests()
	) | rpl::start_with_next([=](BottomBar::Index index) {
		const auto &apiData = _apiSearch.messages();
		const auto &messages = apiData.messages;
		const auto size = int(messages.size());
		if (index >= (size - 1) && size != apiData.total) {
			_apiSearch.searchMore();
		}
		if (index >= size || index < 0) {
			_pendingJump.data = { apiData.nextToken, index };
			return;
		}
		_pendingJump.data = {};
		const auto item = _history->owner().message(messages[index]);
		if (item) {
			const auto weak = Ui::MakeWeak(_topBar.get());
			_activations.fire_copy({ item, _apiSearch.request().query });
			if (weak) {
				hideList();
			}
		}
	}, _bottomBar->lifetime());

	_list.controller->showItemRequests(
	) | rpl::start_with_next([=](const FullMsgId &id) {
		const auto &messages = _apiSearch.messages().messages;
		const auto it = ranges::find(messages, id);
		if (it != end(messages)) {
			_bottomBar->setCurrent(std::distance(begin(messages), it) + 1);
		}
	}, _list.container->lifetime());

	_list.controller->searchMoreRequests(
	) | rpl::start_with_next([=] {
		const auto &apiData = _apiSearch.messages();
		if (int(apiData.messages.size()) != apiData.total) {
			_apiSearch.searchMore();
		}
	}, _list.container->lifetime());

	_bottomBar->showCalendarRequests(
	) | rpl::start_with_next([=] {
		hideList();
		_window->showCalendar({ _history }, QDate());
	}, _bottomBar->lifetime());

	_bottomBar->showBoxFromRequests(
	) | rpl::start_with_next([=] {
		const auto peer = _history->peer;
		auto box = Dialogs::SearchFromBox(
			peer,
			crl::guard(_bottomBar.get(), [=](not_null<PeerData*> from) {
				_window->hideLayer();
				_topBar->setFrom(from);
			}),
			crl::guard(_bottomBar.get(), [=] { setInnerFocus(); }));

		_window->show(std::move(box));
	}, _bottomBar->lifetime());

	_bottomBar->showListRequests(
	) | rpl::start_with_next([=] {
		if (_list.container->isHidden()) {
			Ui::Animations::ShowWidgets({ _list.container.get() });
		} else {
			hideList();
		}
	}, _bottomBar->lifetime());

	_bottomBar->buttonCalendarToggleOn(_topBar->fromValue(
	) | rpl::map([=](PeerData *from) {
		return !from;
	}));

	_bottomBar->buttonFromToggleOn(_topBar->fromValue(
	) | rpl::map([=](PeerData *from) {
		return HasChooseFrom(_history) && !from;
	}));

	if (!query.isEmpty()) {
		_apiSearch.search({ query });
	}
}

void ComposeSearch::Inner::setInnerFocus() {
	if (Ui::AppInFocus() && Ui::InFocusChain(_topBar->window())) {
		_topBar->setInnerFocus();
	}
}

void ComposeSearch::Inner::setQuery(const QString &query) {
	_topBar->setQuery(query);
}

void ComposeSearch::Inner::setTopMsgId(MsgId topMsgId) {
	if (topMsgId) {
		_apiSearch.disableMigrated();
	}
	_topMsgId = topMsgId;
}

void ComposeSearch::Inner::showAnimated() {
	// Don't animate bottom bar.
	_bottomBar->show();
	Ui::Animations::ShowWidgets({ _topBar.get() });
}

void ComposeSearch::Inner::hideAnimated() {
	hideList();
	Ui::Animations::HideWidgets({ _topBar.get(), _bottomBar.get() });

	_destroyRequests.fire({});
}

void ComposeSearch::Inner::hideList() {
	if (!_list.container->isHidden()) {
		Ui::Animations::HideWidgets({ _list.container.get() });
	}
}

rpl::producer<Activation> ComposeSearch::Inner::activations() const {
	return _activations.events();
}

rpl::producer<> ComposeSearch::Inner::destroyRequests() const {
	return _destroyRequests.events();
}

rpl::lifetime &ComposeSearch::Inner::lifetime() {
	return _topBar->lifetime();
}

ComposeSearch::Inner::~Inner() {
}

ComposeSearch::ComposeSearch(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history,
	PeerData *from,
	const QString &query)
: _inner(std::make_unique<Inner>(parent, window, history, from, query)) {
}

ComposeSearch::~ComposeSearch() {
}

void ComposeSearch::hideAnimated() {
	_inner->hideAnimated();
}

void ComposeSearch::setInnerFocus() {
	_inner->setInnerFocus();
}

void ComposeSearch::setQuery(const QString &query) {
	_inner->setQuery(query);
}

void ComposeSearch::setTopMsgId(MsgId topMsgId) {
	_inner->setTopMsgId(topMsgId);
}

rpl::producer<ComposeSearch::Activation> ComposeSearch::activations() const {
	return _inner->activations();
}

rpl::producer<> ComposeSearch::destroyRequests() const {
	return _inner->destroyRequests();
}

rpl::lifetime &ComposeSearch::lifetime() {
	return _inner->lifetime();
}

} // namespace HistoryView
