/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_widget.h"

#include "dialogs/dialogs_inner_widget.h"
#include "dialogs/dialogs_search_from_controllers.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "history/view/history_view_top_bar_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/radial_animation.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/update_checker.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_connecting_widget.h"
#include "window/window_main_menu.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "facades.h"
#include "app.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_window.h"

#include <QtCore/QMimeData>

namespace Dialogs {
namespace {

QString SwitchToChooseFromQuery() {
	return qsl("from:");
}

} // namespace

class Widget::BottomButton : public Ui::RippleButton {
public:
	BottomButton(
		QWidget *parent,
		const QString &text,
		const style::FlatButton &st,
		const style::icon &icon,
		const style::icon &iconOver);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	void radialAnimationCallback();

	QString _text;
	const style::FlatButton &_st;
	const style::icon &_icon;
	const style::icon &_iconOver;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _loading;

};

Widget::BottomButton::BottomButton(
	QWidget *parent,
	const QString &text,
	const style::FlatButton &st,
	const style::icon &icon,
	const style::icon &iconOver)
: RippleButton(parent, st.ripple)
, _text(text.toUpper())
, _st(st)
, _icon(icon)
, _iconOver(iconOver) {
	resize(st::columnMinimalWidthLeft, _st.height);
}

void Widget::BottomButton::setText(const QString &text) {
	_text = text.toUpper();
	update();
}

void Widget::BottomButton::radialAnimationCallback() {
	if (!anim::Disabled() && width() < st::columnMinimalWidthLeft) {
		update();
	}
}

void Widget::BottomButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if ((was & StateFlag::Disabled) != (state() & StateFlag::Disabled)) {
		_loading = isDisabled()
			? std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
				st::dialogsLoadMoreLoading)
			: nullptr;
		if (_loading) {
			_loading->start();
		}
	}
	update();
}

void Widget::BottomButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto over = isOver() && !isDisabled();

	QRect r(0, height() - _st.height, width(), _st.height);
	p.fillRect(r, over ? _st.overBgColor : _st.bgColor);

	if (!isDisabled()) {
		paintRipple(p, 0, 0);
	}

	p.setFont(over ? _st.overFont : _st.font);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(over ? _st.overColor : _st.color);

	if (width() >= st::columnMinimalWidthLeft) {
		r.setTop(_st.textTop);
		p.drawText(r, _text, style::al_top);
	} else if (isDisabled() && _loading) {
		_loading->draw(
			p,
			QPoint(
				(width() - st::dialogsLoadMoreLoading.size.width()) / 2,
				(height() - st::dialogsLoadMoreLoading.size.height()) / 2),
			width());
	} else {
		(over ? _iconOver : _icon).paintInCenter(p, r);
	}
}

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
	: Window::AbstractSectionWidget(parent, controller)
, _api(&controller->session().mtp())
, _searchControls(this)
, _mainMenuToggle(_searchControls, st::dialogsMenuToggle)
, _searchForNarrowFilters(_searchControls, st::dialogsSearchForNarrowFilters)
, _filter(_searchControls, st::dialogsFilter, tr::lng_dlg_filter())
, _chooseFromUser(
	_searchControls,
	object_ptr<Ui::IconButton>(this, st::dialogsSearchFrom))
, _jumpToDate(
	_searchControls,
	object_ptr<Ui::IconButton>(this, st::dialogsCalendar))
, _cancelSearch(_searchControls, st::dialogsCancelSearch)
, _lockUnlock(_searchControls, st::dialogsLock)
, _scroll(this)
, _scrollToTop(_scroll, st::dialogsToUp)
, _singleMessageSearch(&controller->session()) {
	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(this, controller));

	_inner->updated(
	) | rpl::start_with_next([=] {
		onListScroll();
	}, lifetime());

	rpl::combine(
		session().api().dialogsLoadMayBlockByDate(),
		session().api().dialogsLoadBlockedByDate()
	) | rpl::start_with_next([=](bool mayBlock, bool isBlocked) {
		refreshLoadMoreButton(mayBlock, isBlocked);
	}, lifetime());

	session().changes().historyUpdates(
		Data::HistoryUpdate::Flag::MessageSent
	) | rpl::start_with_next([=] {
		jumpToTop();
	}, lifetime());

	fullSearchRefreshOn(session().settings().skipArchiveInSearchChanges(
	) | rpl::to_empty);

	connect(_inner, SIGNAL(draggingScrollDelta(int)), this, SLOT(onDraggingScrollDelta(int)));
	connect(_inner, SIGNAL(mustScrollTo(int,int)), _scroll, SLOT(scrollToY(int,int)));
	connect(_inner, SIGNAL(dialogMoved(int,int)), this, SLOT(onDialogMoved(int,int)));
	connect(_inner, SIGNAL(searchMessages()), this, SLOT(onNeedSearchMessages()));
	connect(_inner, SIGNAL(completeHashtag(QString)), this, SLOT(onCompleteHashtag(QString)));
	connect(_inner, SIGNAL(refreshHashtags()), this, SLOT(onFilterCursorMoved()));
	connect(_inner, SIGNAL(cancelSearchInChat()), this, SLOT(onCancelSearchInChat()));
	_inner->cancelSearchFromUserRequests(
	) | rpl::start_with_next([=] {
		setSearchInChat(_searchInChat, nullptr);
		applyFilterUpdate(true);
	}, lifetime());
	_inner->chosenRow(
	) | rpl::start_with_next([=](const ChosenRow &row) {
		const auto openSearchResult = !controller->selectingPeer()
			&& row.filteredRow;
		if (const auto history = row.key.history()) {
			controller->content()->choosePeer(
				history->peer->id,
				(controller->uniqueChatsInSearchResults()
					? ShowAtUnreadMsgId
					: row.message.fullId.msg));
		} else if (const auto folder = row.key.folder()) {
			controller->openFolder(folder);
		}
		if (openSearchResult && !session().supportMode()) {
			escape();
		}
	}, lifetime());

	connect(_scroll, SIGNAL(geometryChanged()), _inner, SLOT(onParentGeometryChanged()));
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));

	session().data().chatsListChanges(
	) | rpl::filter([=](Data::Folder *folder) {
		return (folder == _inner->shownFolder());
	}) | rpl::start_with_next([=] {
		Ui::PostponeCall(this, [=] { onListScroll(); });
	}, lifetime());

	connect(_filter, &Ui::FlatInput::cancelled, [=] {
		escape();
	});
	connect(_filter, &Ui::FlatInput::changed, [=] {
		applyFilterUpdate();
	});
	connect(
		_filter,
		&Ui::FlatInput::cursorPositionChanged,
		[=](int from, int to) { onFilterCursorMoved(from, to); });

	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker checker;
		rpl::merge(
			rpl::single(rpl::empty_value()),
			checker.isLatest(),
			checker.failed(),
			checker.ready()
		) | rpl::start_with_next([=] {
			checkUpdateStatus();
		}, lifetime());
	}

	controller->adaptive().changes(
	) | rpl::start_with_next([=] {
		updateForwardBar();
	}, lifetime());

	_cancelSearch->setClickedCallback([this] { onCancelSearch(); });
	_jumpToDate->entity()->setClickedCallback([this] { showJumpToDate(); });
	_chooseFromUser->entity()->setClickedCallback([this] { showSearchFrom(); });
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		session().domain().local().localPasscodeChanged()
	) | rpl::start_with_next([=] {
		updateLockUnlockVisibility();
	}, lifetime());
	_lockUnlock->setClickedCallback([this] {
		_lockUnlock->setIconOverride(&st::dialogsUnlockIcon, &st::dialogsUnlockIconOver);
		Core::App().lockByPasscode();
		_lockUnlock->setIconOverride(nullptr);
	});

	setupMainMenuToggle();

	_searchForNarrowFilters->setClickedCallback([=] { Ui::showChatsList(&session()); });

	_chooseByDragTimer.setSingleShot(true);
	connect(&_chooseByDragTimer, SIGNAL(timeout()), this, SLOT(onChooseByDrag()));

	setAcceptDrops(true);

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	_inner->setLoadMoreCallback([=] {
		const auto state = _inner->state();
		if (state == WidgetState::Filtered
			&& (!_inner->waitingForSearch()
				|| (_searchInMigrated
					&& _searchFull
					&& !_searchFullMigrated))) {
			onSearchMore();
		} else {
			const auto folder = _inner->shownFolder();
			if (!folder || !folder->chatsList()->loaded()) {
				session().api().requestDialogs(folder);
			}
		}
	});
	_inner->listBottomReached(
	) | rpl::start_with_next([=] {
		loadMoreBlockedByDate();
	}, lifetime());

	_filter->setFocusPolicy(Qt::StrongFocus);
	_filter->customUpDown(true);

	updateJumpToDateVisibility(true);
	updateSearchFromVisibility(true);
	setupConnectingWidget();
	setupSupportMode();
	setupScrollUpButton();

	changeOpenedFolder(
		controller->openedFolder().current(),
		anim::type::instant);

	controller->openedFolder().changes(
	) | rpl::start_with_next([=](Data::Folder *folder) {
		changeOpenedFolder(folder, anim::type::normal);
	}, lifetime());
}

void Widget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		auto weak = Ui::MakeWeak(this);
		setGeometry(newGeometry);
		if (!weak) {
			return;
		}
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

void Widget::setupScrollUpButton() {
	_scrollToTop->setClickedCallback([=] {
		if (_scrollToAnimation.animating()) {
			return;
		}
		scrollToTop();
	});
	base::install_event_filter(_scrollToTop, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		return _scroll->viewportEvent(event)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	updateScrollUpVisibility();
}

void Widget::updateScrollUpVisibility() {
	if (_scrollToAnimation.animating()) {
		return;
	}

	startScrollUpButtonAnimation(
		(_scroll->scrollTop() > st::historyToDownShownAfter)
		&& (_scroll->scrollTop() < _scroll->scrollTopMax()));
}

void Widget::startScrollUpButtonAnimation(bool shown) {
	const auto smallColumn = (width() < st::columnMinimalWidthLeft);
	shown &= !smallColumn;
	if (_scrollToTopIsShown == shown) {
		return;
	}
	_scrollToTopIsShown = shown;
	_scrollToTopShown.start(
		[=] { updateScrollUpPosition(); },
		_scrollToTopIsShown ? 0. : 1.,
		_scrollToTopIsShown ? 1. : 0.,
		smallColumn ? 0 : st::historyToDownDuration);
}

void Widget::updateScrollUpPosition() {
	// _scrollToTop is a child widget of _scroll, not me.
	auto top = anim::interpolate(
		0,
		_scrollToTop->height() + st::connectingMargin.top(),
		_scrollToTopShown.value(_scrollToTopIsShown ? 1. : 0.));
	_scrollToTop->moveToRight(
		st::historyToDownPosition.x(),
		_scroll->height() - top);
	const auto shouldBeHidden =
		!_scrollToTopIsShown && !_scrollToTopShown.animating();
	if (shouldBeHidden != _scrollToTop->isHidden()) {
		_scrollToTop->setVisible(!shouldBeHidden);
	}
}

void Widget::setupConnectingWidget() {
	_connecting = std::make_unique<Window::ConnectionState>(
		this,
		&session().account(),
		controller()->adaptive().oneColumnValue());
}

void Widget::setupSupportMode() {
	if (!session().supportMode()) {
		return;
	}

	fullSearchRefreshOn(session().settings().supportAllSearchResultsValue(
	) | rpl::to_empty);
}

void Widget::setupMainMenuToggle() {
	_mainMenuToggle->setClickedCallback([=] { showMainMenu(); });

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		controller()->filtersMenuChanged()
	) | rpl::start_with_next([=] {
		const auto filtersHidden = !controller()->filtersWidth();
		_mainMenuToggle->setVisible(filtersHidden);
		_searchForNarrowFilters->setVisible(!filtersHidden);
		updateControlsGeometry();
	}, lifetime());

	Window::OtherAccountsUnreadState(
	) | rpl::start_with_next([=](const Window::OthersUnreadState &state) {
		const auto icon = !state.count
			? nullptr
			: !state.allMuted
			? &st::dialogsMenuToggleUnread
			: &st::dialogsMenuToggleUnreadMuted;
		_mainMenuToggle->setIconOverride(icon, icon);
	}, _mainMenuToggle->lifetime());
}

void Widget::fullSearchRefreshOn(rpl::producer<> events) {
	std::move(
		events
	) | rpl::filter([=] {
		return !_searchQuery.isEmpty();
	}) | rpl::start_with_next([=] {
		_searchTimer.stop();
		_searchCache.clear();
		_singleMessageSearch.clear();
		for (const auto &[requestId, query] : base::take(_searchQueries)) {
			session().api().request(requestId).cancel();
		}
		_searchQuery = QString();
		_scroll->scrollToY(0);
		cancelSearchRequest();
		onSearchMessages();
	}, lifetime());
}

void Widget::updateControlsVisibility(bool fast) {
	updateLoadMoreChatsVisibility();
	_scroll->show();
	if (_forwardCancel) {
		_forwardCancel->show();
	}
	if (_openedFolder && _filter->hasFocus()) {
		setFocus();
	}
	if (_updateTelegram) {
		_updateTelegram->show();
	}
	_searchControls->setVisible(!_openedFolder);
	if (_openedFolder) {
		_folderTopBar->show();
	} else {
		if (hasFocus()) {
			_filter->setFocus();
			_filter->finishAnimations();
		}
		updateLockUnlockVisibility();
		updateJumpToDateVisibility(fast);
		updateSearchFromVisibility(fast);
	}
	_connecting->setForceHidden(false);
}

void Widget::changeOpenedFolder(Data::Folder *folder, anim::type animated) {
	_a_show.stop();

	if (isHidden()) {
		animated = anim::type::instant;
	}
	if (animated == anim::type::normal) {
		_showDirection = folder
			? Window::SlideDirection::FromRight
			: Window::SlideDirection::FromLeft;
		_showAnimationType = ShowAnimation::Internal;
		_connecting->setForceHidden(true);
		_cacheUnder = grabForFolderSlideAnimation();
	}
	_openedFolder = folder;
	refreshFolderTopBar();
	updateControlsVisibility(true);
	_inner->changeOpenedFolder(folder);
	if (animated == anim::type::normal) {
		_connecting->setForceHidden(true);
		_cacheOver = grabForFolderSlideAnimation();
		_connecting->setForceHidden(false);
		startSlideAnimation();
	}
}

void Widget::refreshFolderTopBar() {
	if (_openedFolder) {
		if (!_folderTopBar) {
			_folderTopBar.create(this, controller());
			updateControlsGeometry();
		}
		_folderTopBar->setActiveChat(
			HistoryView::TopBarWidget::ActiveChat{
				.key = _openedFolder,
				.section = Dialogs::EntryState::Section::ChatsList,
			},
			nullptr);
	} else {
		_folderTopBar.destroy();
	}
}

QPixmap Widget::grabForFolderSlideAnimation() {
	const auto hidden = _scrollToTop->isHidden();
	if (!hidden) {
		_scrollToTop->hide();
	}

	const auto top = _forwardCancel ? _forwardCancel->height() : 0;
	const auto rect = QRect(
		0,
		top,
		width(),
		(_updateTelegram ? _updateTelegram->y() : height()) - top);
	auto result = Ui::GrabWidget(this, rect);

	if (!hidden) {
		_scrollToTop->show();
	}
	return result;
}

void Widget::checkUpdateStatus() {
	Expects(!Core::UpdaterDisabled());

	using Checker = Core::UpdateChecker;
	if (Checker().state() == Checker::State::Ready) {
		if (_updateTelegram) return;
		_updateTelegram.create(
			this,
			tr::lng_update_telegram(tr::now),
			st::dialogsUpdateButton,
			st::dialogsInstallUpdate,
			st::dialogsInstallUpdateOver);
		_updateTelegram->show();
		_updateTelegram->setClickedCallback([] {
			Core::checkReadyUpdate();
			App::restart();
		});
	} else {
		if (!_updateTelegram) return;
		_updateTelegram.destroy();
	}
	updateControlsGeometry();
}

void Widget::setInnerFocus() {
	if (_openedFolder) {
		setFocus();
	} else {
		_filter->setFocus();
	}
}

void Widget::jumpToTop() {
	if (session().supportMode()) {
		return;
	}
	if ((_filter->getLastText().trimmed().isEmpty() && !_searchInChat)) {
		_scrollToAnimation.stop();
		_scroll->scrollToY(0);
	}
}

void Widget::scrollToTop() {
	_scrollToAnimation.stop();
	auto scrollTop = _scroll->scrollTop();
	const auto scrollTo = 0;
	const auto maxAnimatedDelta = _scroll->height();
	if (scrollTo + maxAnimatedDelta < scrollTop) {
		scrollTop = scrollTo + maxAnimatedDelta;
		_scroll->scrollToY(scrollTop);
	}

	startScrollUpButtonAnimation(false);

	const auto scroll = [=] {
		_scroll->scrollToY(qRound(_scrollToAnimation.value(scrollTo)));
	};

	_scrollToAnimation.start(
		scroll,
		scrollTop,
		scrollTo,
		st::slideDuration,
		anim::sineInOut);
}

void Widget::startWidthAnimation() {
	if (!_widthAnimationCache.isNull()) {
		return;
	}
	auto scrollGeometry = _scroll->geometry();
	auto grabGeometry = QRect(
		scrollGeometry.x(),
		scrollGeometry.y(),
		st::columnMinimalWidthLeft,
		scrollGeometry.height());
	_scroll->setGeometry(grabGeometry);
	Ui::SendPendingMoveResizeEvents(_scroll);
	auto image = QImage(
		grabGeometry.size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(Qt::transparent);
	{
		QPainter p(&image);
		Ui::RenderWidget(p, _scroll);
	}
	_widthAnimationCache = Ui::PixmapFromImage(std::move(image));
	_scroll->setGeometry(scrollGeometry);
	_scroll->hide();
}

void Widget::stopWidthAnimation() {
	_widthAnimationCache = QPixmap();
	if (!_a_show.animating()) {
		_scroll->show();
	}
	update();
}

void Widget::showFast() {
	if (isHidden()) {
		_inner->clearSelection();
	}
	show();
	updateForwardBar();
}

void Widget::showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params) {
	_showDirection = direction;
	_showAnimationType = ShowAnimation::External;

	_a_show.stop();

	_cacheUnder = params.oldContentCache;
	showFast();
	_cacheOver = controller()->content()->grabForShowAnimation(params);

	if (_updateTelegram) {
		_updateTelegram->hide();
	}
	_connecting->setForceHidden(true);
	startSlideAnimation();
}

void Widget::startSlideAnimation() {
	_scroll->hide();
	if (_forwardCancel) {
		_forwardCancel->hide();
	}
	_searchControls->hide();
	if (_folderTopBar) {
		_folderTopBar->hide();
	}

	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([=] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());
}

bool Widget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect Widget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

void Widget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		_cacheUnder = _cacheOver = QPixmap();

		updateControlsVisibility(true);

		if (!_filter->hasFocus()) {
			controller()->widget()->setInnerFocus();
		}
	}
}

void Widget::escape() {
	if (controller()->openedFolder().current()) {
		controller()->closeFolder();
	} else if (!onCancelSearch()) {
		if (controller()->activeChatEntryCurrent().key) {
			cancelled();
		} else if (controller()->activeChatsFilterCurrent()) {
			controller()->setActiveChatsFilter(FilterId(0));
		}
	} else if (!_searchInChat && !controller()->selectingPeer()) {
		if (controller()->activeChatEntryCurrent().key) {
			cancelled();
		}
	}
}

void Widget::refreshLoadMoreButton(bool mayBlock, bool isBlocked) {
	if (!mayBlock) {
		if (_loadMoreChats) {
			_loadMoreChats.destroy();
			updateControlsGeometry();
		}
		return;
	}
	if (!_loadMoreChats) {
		_loadMoreChats.create(
			this,
			"Load more",
			st::dialogsLoadMoreButton,
			st::dialogsLoadMore,
			st::dialogsLoadMore);
		_loadMoreChats->show();
		_loadMoreChats->addClickHandler([=] {
			loadMoreBlockedByDate();
		});
		updateControlsGeometry();
	}
	const auto loading = !isBlocked;
	_loadMoreChats->setDisabled(loading);
	_loadMoreChats->setText(loading ? "Loading..." : "Load more");
}

void Widget::loadMoreBlockedByDate() {
	if (!_loadMoreChats
		|| _loadMoreChats->isDisabled()
		|| _loadMoreChats->isHidden()) {
		return;
	}
	session().api().requestMoreBlockedByDateDialogs();
}

void Widget::onDraggingScrollDelta(int delta) {
	_draggingScrollDelta = _scroll ? delta : 0;
	if (_draggingScrollDelta) {
		if (!_draggingScrollTimer) {
			_draggingScrollTimer.create(this);
			_draggingScrollTimer->setSingleShot(false);
			connect(_draggingScrollTimer, SIGNAL(timeout()), this, SLOT(onDraggingScrollTimer()));
		}
		_draggingScrollTimer->start(15);
	} else {
		_draggingScrollTimer.destroy();
	}
}

void Widget::onDraggingScrollTimer() {
	const auto delta = (_draggingScrollDelta > 0)
		? qMin(_draggingScrollDelta * 3 / 20 + 1, Ui::kMaxScrollSpeed)
		: qMax(_draggingScrollDelta * 3 / 20 - 1, -Ui::kMaxScrollSpeed);
	_scroll->scrollToY(_scroll->scrollTop() + delta);
}

bool Widget::onSearchMessages(bool searchCache) {
	auto result = false;
	auto q = _filter->getLastText().trimmed();
	if (q.isEmpty() && !_searchFromAuthor) {
		cancelSearchRequest();
		_api.request(base::take(_peerSearchRequest)).cancel();
		return true;
	}
	if (searchCache) {
		const auto success = _singleMessageSearch.lookup(q, [=] {
			onNeedSearchMessages();
		});
		if (!success) {
			return false;
		}
		const auto i = _searchCache.find(q);
		if (i != _searchCache.end()) {
			_searchQuery = q;
			_searchQueryFrom = _searchFromAuthor;
			_searchNextRate = 0;
			_searchFull = _searchFullMigrated = false;
			cancelSearchRequest();
			searchReceived(
				_searchInChat
					? SearchRequestType::PeerFromStart
					: SearchRequestType::FromStart,
				i->second,
				0);
			result = true;
		}
	} else if (_searchQuery != q || _searchQueryFrom != _searchFromAuthor) {
		_searchQuery = q;
		_searchQueryFrom = _searchFromAuthor;
		_searchNextRate = 0;
		_searchFull = _searchFullMigrated = false;
		cancelSearchRequest();
		if (const auto peer = _searchInChat.peer()) {
			auto &histories = session().data().histories();
			const auto type = Data::Histories::RequestType::History;
			const auto history = session().data().history(peer);
			_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
				const auto type = SearchRequestType::PeerFromStart;
				const auto flags = _searchQueryFrom
					? MTP_flags(MTPmessages_Search::Flag::f_from_id)
					: MTP_flags(0);
				_searchRequest = session().api().request(MTPmessages_Search(
					flags,
					peer->input,
					MTP_string(_searchQuery),
					(_searchQueryFrom
						? _searchQueryFrom->input
						: MTP_inputPeerEmpty()),
					MTPint(), // top_msg_id
					MTP_inputMessagesFilterEmpty(),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0),
					MTP_int(SearchPerPage),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0)
				)).done([=](const MTPmessages_Messages &result) {
					_searchInHistoryRequest = 0;
					searchReceived(type, result, _searchRequest);
					finish();
				}).fail([=](const MTP::Error &error) {
					_searchInHistoryRequest = 0;
					searchFailed(type, error, _searchRequest);
					finish();
				}).send();
				_searchQueries.emplace(_searchRequest, _searchQuery);
				return _searchRequest;
			});
		} else {
			const auto type = SearchRequestType::FromStart;
			const auto flags = session().settings().skipArchiveInSearch()
				? MTPmessages_SearchGlobal::Flag::f_folder_id
				: MTPmessages_SearchGlobal::Flag(0);
			const auto folderId = 0;
			_searchRequest = session().api().request(MTPmessages_SearchGlobal(
				MTP_flags(flags),
				MTP_int(folderId),
				MTP_string(_searchQuery),
				MTP_inputMessagesFilterEmpty(),
				MTP_int(0), // min_date
				MTP_int(0), // max_date
				MTP_int(0),
				MTP_inputPeerEmpty(),
				MTP_int(0),
				MTP_int(SearchPerPage)
			)).done([=](const MTPmessages_Messages &result) {
				searchReceived(type, result, _searchRequest);
			}).fail([=](const MTP::Error &error) {
				searchFailed(type, error, _searchRequest);
			}).send();
			_searchQueries.emplace(_searchRequest, _searchQuery);
		}
	}
	const auto query = Api::ConvertPeerSearchQuery(q);
	if (searchForPeersRequired(query)) {
		if (searchCache) {
			auto i = _peerSearchCache.find(query);
			if (i != _peerSearchCache.end()) {
				_peerSearchQuery = query;
				_peerSearchRequest = 0;
				peerSearchReceived(i->second, 0);
				result = true;
			}
		} else if (_peerSearchQuery != query) {
			_peerSearchQuery = query;
			_peerSearchFull = false;
			_peerSearchRequest = _api.request(MTPcontacts_Search(
				MTP_string(_peerSearchQuery),
				MTP_int(SearchPeopleLimit)
			)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
				peerSearchReceived(result, requestId);
			}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
				peopleFailed(error, requestId);
			}).send();
			_peerSearchQueries.emplace(_peerSearchRequest, _peerSearchQuery);
		}
	} else {
		_peerSearchQuery = query;
		_peerSearchFull = true;
		peerSearchReceived(
			MTP_contacts_found(
				MTP_vector<MTPPeer>(0),
				MTP_vector<MTPPeer>(0),
				MTP_vector<MTPChat>(0),
				MTP_vector<MTPUser>(0)),
			0);
	}
	return result;
}

bool Widget::searchForPeersRequired(const QString &query) const {
	if (_searchInChat || query.isEmpty()) {
		return false;
	}
	return (query[0] != '#');
}

void Widget::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void Widget::onChooseByDrag() {
	_inner->chooseRow();
}

void Widget::showMainMenu() {
	controller()->widget()->showMainMenu();
}

void Widget::searchMessages(
		const QString &query,
		Key inChat) {
	auto inChatChanged = [&] {
		if (inChat == _searchInChat) {
			return false;
		} else if (const auto inPeer = inChat.peer()) {
			if (inPeer->migrateTo() == _searchInChat.peer()) {
				return false;
			}
		}
		return true;
	}();
	if ((_filter->getLastText() != query) || inChatChanged) {
		if (inChat) {
			onCancelSearch();
			setSearchInChat(inChat);
		}
		_filter->setText(query);
		_filter->updatePlaceholder();
		applyFilterUpdate(true);
		_searchTimer.stop();
		onSearchMessages();

		session().local().saveRecentSearchHashtags(query);
	}
}

void Widget::onSearchMore() {
	if (_searchRequest || _searchInHistoryRequest) {
		return;
	}
	if (!_searchFull) {
		auto offsetPeer = _inner->lastSearchPeer();
		auto offsetId = _inner->lastSearchId();
		if (const auto peer = _searchInChat.peer()) {
			auto &histories = session().data().histories();
			const auto type = Data::Histories::RequestType::History;
			const auto history = session().data().history(peer);
			_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
				const auto type = offsetId
					? SearchRequestType::PeerFromOffset
					: SearchRequestType::PeerFromStart;
				auto flags = _searchQueryFrom
					? MTP_flags(MTPmessages_Search::Flag::f_from_id)
					: MTP_flags(0);
				_searchRequest = session().api().request(MTPmessages_Search(
					flags,
					peer->input,
					MTP_string(_searchQuery),
					(_searchQueryFrom
						? _searchQueryFrom->input
						: MTP_inputPeerEmpty()),
					MTPint(), // top_msg_id
					MTP_inputMessagesFilterEmpty(),
					MTP_int(0),
					MTP_int(0),
					MTP_int(offsetId),
					MTP_int(0),
					MTP_int(SearchPerPage),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0)
				)).done([=](const MTPmessages_Messages &result) {
					searchReceived(type, result, _searchRequest);
					_searchInHistoryRequest = 0;
					finish();
				}).fail([=](const MTP::Error &error) {
					searchFailed(type, error, _searchRequest);
					_searchInHistoryRequest = 0;
					finish();
				}).send();
				if (!offsetId) {
					_searchQueries.emplace(_searchRequest, _searchQuery);
				}
				return _searchRequest;
			});
		} else {
			const auto type = offsetId
				? SearchRequestType::FromOffset
				: SearchRequestType::FromStart;
			const auto flags = session().settings().skipArchiveInSearch()
				? MTPmessages_SearchGlobal::Flag::f_folder_id
				: MTPmessages_SearchGlobal::Flag(0);
			const auto folderId = 0;
			_searchRequest = session().api().request(MTPmessages_SearchGlobal(
				MTP_flags(flags),
				MTP_int(folderId),
				MTP_string(_searchQuery),
				MTP_inputMessagesFilterEmpty(),
				MTP_int(0), // min_date
				MTP_int(0), // max_date
				MTP_int(_searchNextRate),
				offsetPeer
					? offsetPeer->input
					: MTP_inputPeerEmpty(),
				MTP_int(offsetId),
				MTP_int(SearchPerPage)
			)).done([=](const MTPmessages_Messages &result) {
				searchReceived(type, result, _searchRequest);
			}).fail([=](const MTP::Error &error) {
				searchFailed(type, error, _searchRequest);
			}).send();
			if (!offsetId) {
				_searchQueries.emplace(_searchRequest, _searchQuery);
			}
		}
	} else if (_searchInMigrated && !_searchFullMigrated) {
		auto offsetMigratedId = _inner->lastSearchMigratedId();
		auto &histories = session().data().histories();
		const auto type = Data::Histories::RequestType::History;
		const auto history = _searchInMigrated;
		_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
			const auto type = offsetMigratedId
				? SearchRequestType::MigratedFromOffset
				: SearchRequestType::MigratedFromStart;
			const auto flags = _searchQueryFrom
				? MTP_flags(MTPmessages_Search::Flag::f_from_id)
				: MTP_flags(0);
			_searchRequest = session().api().request(MTPmessages_Search(
				flags,
				_searchInMigrated->peer->input,
				MTP_string(_searchQuery),
				(_searchQueryFrom
					? _searchQueryFrom->input
					: MTP_inputPeerEmpty()),
				MTPint(), // top_msg_id
				MTP_inputMessagesFilterEmpty(),
				MTP_int(0),
				MTP_int(0),
				MTP_int(offsetMigratedId),
				MTP_int(0),
				MTP_int(SearchPerPage),
				MTP_int(0),
				MTP_int(0),
				MTP_int(0)
			)).done([=](const MTPmessages_Messages &result) {
				searchReceived(type, result, _searchRequest);
				_searchInHistoryRequest = 0;
				finish();
			}).fail([=](const MTP::Error &error) {
				searchFailed(type, error, _searchRequest);
				_searchInHistoryRequest = 0;
				finish();
			}).send();
			return _searchRequest;
		});
	}
}

void Widget::searchReceived(
		SearchRequestType type,
		const MTPmessages_Messages &result,
		mtpRequestId requestId) {
	const auto state = _inner->state();
	if (state == WidgetState::Filtered) {
		if (type == SearchRequestType::FromStart || type == SearchRequestType::PeerFromStart) {
			auto i = _searchQueries.find(requestId);
			if (i != _searchQueries.end()) {
				_searchCache[i->second] = result;
				_searchQueries.erase(i);
			}
		}
	}
	const auto inject = (type == SearchRequestType::FromStart
		|| type == SearchRequestType::PeerFromStart)
		? *_singleMessageSearch.lookup(_searchQuery)
		: nullptr;

	if (_searchRequest != requestId) {
		return;
	}
	switch (result.type()) {
	case mtpc_messages_messages: {
		auto &d = result.c_messages_messages();
		if (_searchRequest != 0) {
			// Don't apply cached data!
			session().data().processUsers(d.vusers());
			session().data().processChats(d.vchats());
		}
		auto &msgs = d.vmessages().v;
		_inner->searchReceived(msgs, inject, type, msgs.size());
		if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d = result.c_messages_messagesSlice();
		if (_searchRequest != 0) {
			// Don't apply cached data!
			session().data().processUsers(d.vusers());
			session().data().processChats(d.vchats());
		}
		auto &msgs = d.vmessages().v;
		const auto someAdded = _inner->searchReceived(msgs, inject, type, d.vcount().v);
		const auto nextRate = d.vnext_rate();
		const auto rateUpdated = nextRate && (nextRate->v != _searchNextRate);
		const auto finished = (type == SearchRequestType::FromStart || type == SearchRequestType::FromOffset)
			? !rateUpdated
			: !someAdded;
		if (rateUpdated) {
			_searchNextRate = nextRate->v;
		}
		if (finished) {
			if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
				_searchFullMigrated = true;
			} else {
				_searchFull = true;
			}
		}
	} break;

	case mtpc_messages_channelMessages: {
		auto &d = result.c_messages_channelMessages();
		if (const auto peer = _searchInChat.peer()) {
			if (const auto channel = peer->asChannel()) {
				channel->ptsReceived(d.vpts().v);
			} else {
				LOG(("API Error: "
					"received messages.channelMessages when no channel "
					"was passed! (Widget::searchReceived)"));
			}
		} else {
			LOG(("API Error: "
				"received messages.channelMessages when no channel "
				"was passed! (Widget::searchReceived)"));
		}
		if (_searchRequest != 0) {
			// Don't apply cached data!
			session().data().processUsers(d.vusers());
			session().data().processChats(d.vchats());
		}
		auto &msgs = d.vmessages().v;
		if (!_inner->searchReceived(msgs, inject, type, d.vcount().v)) {
			if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
				_searchFullMigrated = true;
			} else {
				_searchFull = true;
			}
		}
	} break;

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (Widget::searchReceived)"));
		if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
	} break;
	}

	_searchRequest = 0;
	onListScroll();
	update();
}

void Widget::peerSearchReceived(
		const MTPcontacts_Found &result,
		mtpRequestId requestId) {
	const auto state = _inner->state();
	auto q = _peerSearchQuery;
	if (state == WidgetState::Filtered) {
		auto i = _peerSearchQueries.find(requestId);
		if (i != _peerSearchQueries.end()) {
			_peerSearchCache[i->second] = result;
			_peerSearchQueries.erase(i);
		}
	}
	if (_peerSearchRequest == requestId) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			auto &d = result.c_contacts_found();
			session().data().processUsers(d.vusers());
			session().data().processChats(d.vchats());
			_inner->peerSearchReceived(q, d.vmy_results().v, d.vresults().v);
		} break;
		}

		_peerSearchRequest = 0;
		onListScroll();
	}
}

void Widget::searchFailed(
		SearchRequestType type,
		const MTP::Error &error,
		mtpRequestId requestId) {
	if (error.type() == qstr("SEARCH_QUERY_EMPTY")) {
		searchReceived(
			type,
			MTP_messages_messages(
				MTP_vector<MTPMessage>(),
				MTP_vector<MTPChat>(),
				MTP_vector<MTPUser>()),
			requestId);
	} else if (_searchRequest == requestId) {
		_searchRequest = 0;
		if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
	}
}

void Widget::peopleFailed(const MTP::Error &error, mtpRequestId requestId) {
	if (_peerSearchRequest == requestId) {
		_peerSearchRequest = 0;
		_peerSearchFull = true;
	}
}

void Widget::dragEnterEvent(QDragEnterEvent *e) {
	using namespace Storage;

	if (controller()->selectingPeer()) {
		return;
	}

	const auto data = e->mimeData();
	_dragInScroll = false;
	_dragForward = controller()->adaptive().isOneColumn()
		? false
		: data->hasFormat(qsl("application/x-td-forward"));
	if (_dragForward) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
		updateDragInScroll(_scroll->geometry().contains(e->pos()));
	} else if (ComputeMimeDataState(data) != MimeDataState::None) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
	}
	_chooseByDragTimer.stop();
}

void Widget::dragMoveEvent(QDragMoveEvent *e) {
	if (_scroll->geometry().contains(e->pos())) {
		if (_dragForward) {
			updateDragInScroll(true);
		} else {
			_chooseByDragTimer.start(ChoosePeerByDragTimeout);
		}
		if (_inner->updateFromParentDrag(mapToGlobal(e->pos()))) {
			e->setDropAction(Qt::CopyAction);
		} else {
			e->setDropAction(Qt::IgnoreAction);
		}
	} else {
		if (_dragForward) updateDragInScroll(false);
		_inner->dragLeft();
		e->setDropAction(Qt::IgnoreAction);
	}
	e->accept();
}

void Widget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dragForward) {
		updateDragInScroll(false);
	} else {
		_chooseByDragTimer.stop();
	}
	_inner->dragLeft();
	e->accept();
}

void Widget::updateDragInScroll(bool inScroll) {
	if (_dragInScroll != inScroll) {
		_dragInScroll = inScroll;
		if (_dragInScroll) {
			controller()->content()->showForwardLayer({});
		} else {
			controller()->content()->dialogsCancelled();
		}
	}
}

void Widget::dropEvent(QDropEvent *e) {
	_chooseByDragTimer.stop();
	if (_scroll->geometry().contains(e->pos())) {
		if (auto peer = _inner->updateFromParentDrag(mapToGlobal(e->pos()))) {
			e->acceptProposedAction();
			controller()->content()->onFilesOrForwardDrop(
				peer->id,
				e->mimeData());
			controller()->widget()->raise();
			controller()->widget()->activateWindow();
		}
	}
}

void Widget::onListScroll() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updateScrollUpVisibility();

	// Fix button rendering glitch, Qt bug with WA_OpaquePaintEvent widgets.
	_scrollToTop->update();
}

void Widget::applyFilterUpdate(bool force) {
	if (_a_show.animating() && !force) {
		return;
	}

	auto filterText = _filter->getLastText();
	_inner->applyFilterUpdate(filterText, force);
	if (filterText.isEmpty() && !_searchFromAuthor) {
		clearSearchCache();
	}
	_cancelSearch->toggle(!filterText.isEmpty(), anim::type::normal);
	updateLoadMoreChatsVisibility();
	updateJumpToDateVisibility();

	if (filterText.isEmpty()) {
		_peerSearchCache.clear();
		for (const auto &[requestId, query] : base::take(_peerSearchQueries)) {
			_api.request(requestId).cancel();
		}
		_peerSearchQuery = QString();
	}

	if (_chooseFromUser->toggled() || _searchFromAuthor) {
		auto switchToChooseFrom = SwitchToChooseFromQuery();
		if (_lastFilterText != switchToChooseFrom
			&& switchToChooseFrom.startsWith(_lastFilterText)
			&& filterText == switchToChooseFrom) {
			showSearchFrom();
		}
	}
	_lastFilterText = filterText;
}

void Widget::searchInChat(Key chat) {
	onCancelSearch();
	setSearchInChat(chat);
	applyFilterUpdate(true);
}

void Widget::setSearchInChat(Key chat, PeerData *from) {
	if (chat.folder()) {
		chat = Key();
	}
	_searchInMigrated = nullptr;
	if (const auto peer = chat.peer()) {
		if (const auto migrateTo = peer->migrateTo()) {
			return setSearchInChat(peer->owner().history(migrateTo), from);
		} else if (const auto migrateFrom = peer->migrateFrom()) {
			_searchInMigrated = peer->owner().history(migrateFrom);
		}
	}
	const auto searchInPeerUpdated = (_searchInChat != chat);
	if (searchInPeerUpdated) {
		_searchInChat = chat;
		from = nullptr;
		controller()->searchInChat = _searchInChat;
		updateJumpToDateVisibility();
	} else if (!_searchInChat) {
		from = nullptr;
	}
	if (_searchFromAuthor != from || searchInPeerUpdated) {
		_searchFromAuthor = from;
		updateSearchFromVisibility();
		clearSearchCache();
	}
	_inner->searchInChat(_searchInChat, _searchFromAuthor);
	if (_searchFromAuthor && _lastFilterText == SwitchToChooseFromQuery()) {
		onCancelSearch();
	}
	_filter->setFocus();
}

void Widget::clearSearchCache() {
	_searchCache.clear();
	_singleMessageSearch.clear();
	for (const auto &[requestId, query] : base::take(_searchQueries)) {
		session().api().request(requestId).cancel();
	}
	_searchQuery = QString();
	_searchQueryFrom = nullptr;
	cancelSearchRequest();
}

void Widget::showJumpToDate() {
	if (_searchInChat) {
		controller()->showJumpToDate(_searchInChat, QDate());
	}
}

void Widget::showSearchFrom() {
	if (const auto peer = _searchInChat.peer()) {
		const auto chat = _searchInChat;
		ShowSearchFromBox(
			peer,
			crl::guard(this, [=](not_null<PeerData*> from) {
				Ui::hideLayer();
				setSearchInChat(chat, from);
				applyFilterUpdate(true);
			}),
			crl::guard(this, [=] { _filter->setFocus(); }));
	}
}

void Widget::onFilterCursorMoved(int from, int to) {
	if (to < 0) to = _filter->cursorPosition();
	QString t = _filter->getLastText();
	QStringRef r;
	for (int start = to; start > 0;) {
		--start;
		if (t.size() <= start) break;
		if (t.at(start) == '#') {
			r = t.midRef(start, to - start);
			break;
		}
		if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') break;
	}
	_inner->onHashtagFilterUpdate(r);
}

void Widget::onCompleteHashtag(QString tag) {
	QString t = _filter->getLastText(), r;
	int cur = _filter->cursorPosition();
	for (int start = cur; start > 0;) {
		--start;
		if (t.size() <= start) break;
		if (t.at(start) == '#') {
			if (cur == start + 1 || t.midRef(start + 1, cur - start - 1) == tag.midRef(0, cur - start - 1)) {
				for (; cur < t.size() && cur - start - 1 < tag.size(); ++cur) {
					if (t.at(cur) != tag.at(cur - start - 1)) break;
				}
				if (cur - start - 1 == tag.size() && cur < t.size() && t.at(cur) == ' ') ++cur;
				r = t.mid(0, start + 1) + tag + ' ' + t.mid(cur);
				_filter->setText(r);
				_filter->setCursorPosition(start + 1 + tag.size() + 1);
				applyFilterUpdate(true);
				return;
			}
			break;
		}
		if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') break;
	}
	_filter->setText(t.mid(0, cur) + '#' + tag + ' ' + t.mid(cur));
	_filter->setCursorPosition(cur + 1 + tag.size() + 1);
	applyFilterUpdate(true);
}

void Widget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Widget::updateLockUnlockVisibility() {
	if (_a_show.animating()) {
		return;
	}
	const auto hidden = !session().domain().local().hasLocalPasscode();
	if (_lockUnlock->isHidden() != hidden) {
		_lockUnlock->setVisible(!hidden);
		updateControlsGeometry();
	}
}

void Widget::updateLoadMoreChatsVisibility() {
	if (_a_show.animating() || !_loadMoreChats) {
		return;
	}
	const auto hidden = (_openedFolder != nullptr)
		|| !_filter->getLastText().isEmpty();
	if (_loadMoreChats->isHidden() != hidden) {
		_loadMoreChats->setVisible(!hidden);
		updateControlsGeometry();
	}
}

void Widget::updateJumpToDateVisibility(bool fast) {
	if (_a_show.animating()) return;

	_jumpToDate->toggle(
		(_searchInChat && _filter->getLastText().isEmpty()),
		fast ? anim::type::instant : anim::type::normal);
}

void Widget::updateSearchFromVisibility(bool fast) {
	auto visible = [&] {
		if (const auto peer = _searchInChat.peer()) {
			if (peer->isChat() || peer->isMegagroup()) {
				return !_searchFromAuthor;
			}
		}
		return false;
	}();
	auto changed = (visible == !_chooseFromUser->toggled());
	_chooseFromUser->toggle(
		visible,
		fast ? anim::type::instant : anim::type::normal);
	if (changed) {
		auto margins = st::dialogsFilter.textMrg;
		if (visible) {
			margins.setRight(margins.right() + _chooseFromUser->width());
		}
		_filter->setTextMrg(margins);
	}
}

void Widget::updateControlsGeometry() {
	auto filterAreaTop = 0;
	if (_forwardCancel) {
		_forwardCancel->moveToLeft(0, filterAreaTop);
		filterAreaTop += st::dialogsForwardHeight;
	}
	auto smallLayoutWidth = (st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x());
	auto smallLayoutRatio = (width() < st::columnMinimalWidthLeft) ? (st::columnMinimalWidthLeft - width()) / float64(st::columnMinimalWidthLeft - smallLayoutWidth) : 0.;
	auto filterLeft = (controller()->filtersWidth() ? st::dialogsFilterSkip : st::dialogsFilterPadding.x() + _mainMenuToggle->width()) + st::dialogsFilterPadding.x();
	auto filterRight = (session().domain().local().hasLocalPasscode() ? (st::dialogsFilterPadding.x() + _lockUnlock->width()) : st::dialogsFilterSkip) + st::dialogsFilterPadding.x();
	auto filterWidth = qMax(width(), st::columnMinimalWidthLeft) - filterLeft - filterRight;
	auto filterAreaHeight = st::topBarHeight;
	_searchControls->setGeometry(0, filterAreaTop, width(), filterAreaHeight);
	if (_folderTopBar) {
		_folderTopBar->setGeometry(_searchControls->geometry());
	}

	auto filterTop = (filterAreaHeight - _filter->height()) / 2;
	filterLeft = anim::interpolate(filterLeft, smallLayoutWidth, smallLayoutRatio);
	_filter->setGeometryToLeft(filterLeft, filterTop, filterWidth, _filter->height());
	auto mainMenuLeft = anim::interpolate(st::dialogsFilterPadding.x(), (smallLayoutWidth - _mainMenuToggle->width()) / 2, smallLayoutRatio);
	_mainMenuToggle->moveToLeft(mainMenuLeft, st::dialogsFilterPadding.y());
	const auto searchLeft = anim::interpolate(
		-_searchForNarrowFilters->width(),
		(smallLayoutWidth - _searchForNarrowFilters->width()) / 2,
		smallLayoutRatio);
	_searchForNarrowFilters->moveToLeft(searchLeft, st::dialogsFilterPadding.y());

	auto right = filterLeft + filterWidth;
	_lockUnlock->moveToLeft(right + st::dialogsFilterPadding.x(), st::dialogsFilterPadding.y());
	_cancelSearch->moveToLeft(right - _cancelSearch->width(), _filter->y());
	right -= _jumpToDate->width(); _jumpToDate->moveToLeft(right, _filter->y());
	right -= _chooseFromUser->width(); _chooseFromUser->moveToLeft(right, _filter->y());

	auto scrollTop = filterAreaTop + filterAreaHeight;
	auto newScrollTop = _scroll->scrollTop() + _topDelta;
	auto scrollHeight = height() - scrollTop;
	const auto putBottomButton = [&](object_ptr<BottomButton> &button) {
		if (button && !button->isHidden()) {
			const auto buttonHeight = button->height();
			scrollHeight -= buttonHeight;
			button->setGeometry(
				0,
				scrollTop + scrollHeight,
				width(),
				buttonHeight);
		}
	};
	putBottomButton(_updateTelegram);
	putBottomButton(_loadMoreChats);
	auto wasScrollHeight = _scroll->height();
	_scroll->setGeometry(0, scrollTop, width(), scrollHeight);
	_inner->resize(width(), _inner->height());
	if (scrollHeight != wasScrollHeight) {
		controller()->floatPlayerAreaUpdated();
	}
	if (_topDelta) {
		_scroll->scrollToY(newScrollTop);
	} else {
		onListScroll();
	}
	if (_scrollToTopIsShown) {
		updateScrollUpPosition();
	}
}

rpl::producer<> Widget::closeForwardBarRequests() const {
	return _closeForwardBarRequests.events();
}

void Widget::updateForwardBar() {
	auto selecting = controller()->selectingPeer();
	auto oneColumnSelecting = (controller()->adaptive().isOneColumn()
		&& selecting);
	if (!oneColumnSelecting == !_forwardCancel) {
		return;
	}
	if (oneColumnSelecting) {
		_forwardCancel.create(this, st::dialogsForwardCancel);
		_forwardCancel->setClickedCallback([=] {
			_closeForwardBarRequests.fire({});
		});
		if (!_a_show.animating()) _forwardCancel->show();
	} else {
		_forwardCancel.destroyDelayed();
	}
	updateControlsGeometry();
	update();
}

void Widget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (_openedFolder) {
			controller()->closeFolder();
		} else {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!_inner->chooseRow()) {
			const auto state = _inner->state();
			if (state == WidgetState::Default
				|| (state == WidgetState::Filtered
					&& (!_inner->waitingForSearch() ||  _inner->hasFilteredResults()))) {
				_inner->selectSkip(1);
				_inner->chooseRow();
			} else {
				onSearchMessages();
			}
		}
	} else if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(_scroll->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(_scroll->height(), -1);
	} else {
		e->ignore();
	}
}

void Widget::paintEvent(QPaintEvent *e) {
	if (controller()->widget()->contentOverlapped(this, e)) {
		return;
	}

	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	if (_a_show.animating()) {
		const auto progress = _a_show.value(1.);
		const auto top = (_showAnimationType == ShowAnimation::Internal)
			? (_forwardCancel ? _forwardCancel->height() : 0)
			: 0;
		const auto shift = std::min(st::slideShift, width() / 2);
		const auto retina = cIntRetinaFactor();
		const auto fromLeft = (_showDirection == Window::SlideDirection::FromLeft);
		const auto coordUnder = fromLeft ? anim::interpolate(-shift, 0, progress) : anim::interpolate(0, -shift, progress);
		const auto coordOver = fromLeft ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		const auto shadow = fromLeft ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, top, coordOver, _cacheUnder.height() / retina), _cacheUnder, QRect(-coordUnder * retina, 0, coordOver * retina, _cacheUnder.height()));
			p.setOpacity(shadow);
			p.fillRect(0, top, coordOver, _cacheUnder.height() / retina, st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(QRect(coordOver, top, _cacheOver.width() / retina, _cacheOver.height() / retina), _cacheOver, QRect(0, 0, _cacheOver.width(), _cacheOver.height()));
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), top, st::slideShadow.width(), _cacheOver.height() / retina));
		return;
	}
	auto aboveTop = 0;
	if (_forwardCancel) {
		p.fillRect(0, aboveTop, width(), st::dialogsForwardHeight, st::dialogsForwardBg);
		p.setPen(st::dialogsForwardFg);
		p.setFont(st::dialogsForwardFont);
		p.drawTextLeft(st::dialogsForwardTextLeft, st::dialogsForwardTextTop, width(), tr::lng_forward_choose(tr::now));
		aboveTop += st::dialogsForwardHeight;
	}
	auto above = QRect(0, aboveTop, width(), _scroll->y() - aboveTop);
	if (above.intersects(r)) {
		p.fillRect(above.intersected(r), st::dialogsBg);
	}

	auto belowTop = _scroll->y() + qMin(_scroll->height(), _inner->height());
	if (!_widthAnimationCache.isNull()) {
		p.drawPixmapLeft(0, _scroll->y(), width(), _widthAnimationCache);
		belowTop = _scroll->y() + (_widthAnimationCache.height() / cIntRetinaFactor());
	}

	auto below = QRect(0, belowTop, width(), height() - belowTop);
	if (below.intersects(r)) {
		p.fillRect(below.intersected(r), st::dialogsBg);
	}
}

void Widget::scrollToEntry(const RowDescriptor &entry) {
	_inner->scrollToEntry(entry);
}

void Widget::cancelSearchRequest() {
	session().api().request(base::take(_searchRequest)).cancel();
	session().data().histories().cancelRequest(
		base::take(_searchInHistoryRequest));
}

bool Widget::onCancelSearch() {
	bool clearing = !_filter->getLastText().isEmpty();
	cancelSearchRequest();
	if (_searchInChat && !clearing) {
		if (controller()->adaptive().isOneColumn()) {
			if (const auto peer = _searchInChat.peer()) {
				Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
			} else {
				Unexpected("Empty key in onCancelSearch().");
			}
		}
		setSearchInChat(Key());
		clearing = true;
	}
	_inner->clearFilter();
	_filter->clear();
	_filter->updatePlaceholder();
	applyFilterUpdate();
	return clearing;
}

void Widget::onCancelSearchInChat() {
	cancelSearchRequest();
	const auto isOneColumn = controller()->adaptive().isOneColumn();
	if (_searchInChat) {
		if (isOneColumn
			&& !controller()->selectingPeer()
			&& _filter->getLastText().trimmed().isEmpty()) {
			if (const auto peer = _searchInChat.peer()) {
				Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
			} else {
				Unexpected("Empty key in onCancelSearchInPeer().");
			}
		}
		setSearchInChat(Key());
	}
	applyFilterUpdate(true);
	if (!isOneColumn && !controller()->selectingPeer()) {
		cancelled();
	}
}

void Widget::onDialogMoved(int movedFrom, int movedTo) {
	int32 st = _scroll->scrollTop();
	if (st > movedTo && st < movedFrom) {
		_scroll->scrollToY(st + st::dialogsRowHeight);
	}
}

Widget::~Widget() {
	cancelSearchRequest();
}

} // namespace Dialogs
