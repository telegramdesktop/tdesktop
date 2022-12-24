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
#include "history/history_item.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_requests_bar.h"
#include "history/view/history_view_group_call_bar.h"
#include "boxes/peers/edit_peer_requests_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/radial_animation.h"
#include "ui/chat/requests_bar.h"
#include "ui/chat/group_call_bar.h"
#include "ui/controls/download_bar.h"
#include "ui/painter.h"
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
#include "core/shortcuts.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "window/window_adaptive.h"
#include "window/window_controller.h"
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
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/data_download_manager.h"
#include "data/data_chat_filters.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/info_memento.h"
#include "facades.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_window.h"
#include "base/qt/qt_common_adapters.h"

#include <QtCore/QMimeData>

namespace Dialogs {
namespace {

constexpr auto kSearchPerPage = 50;

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
	auto p = QPainter(this);

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
: Window::AbstractSectionWidget(parent, controller, nullptr)
, _api(&controller->session().mtp())
, _chooseByDragTimer([=] { _inner->chooseRow(); })
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
, _searchTimer([=] { searchMessages(); })
, _singleMessageSearch(&controller->session()) {
	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(this, controller));

	_inner->updated(
	) | rpl::start_with_next([=] {
		listScrollUpdated();
	}, lifetime());

	rpl::combine(
		session().api().dialogsLoadMayBlockByDate(),
		session().api().dialogsLoadBlockedByDate()
	) | rpl::start_with_next([=](bool mayBlock, bool isBlocked) {
		refreshLoadMoreButton(mayBlock, isBlocked);
	}, lifetime());

	session().changes().historyUpdates(
		Data::HistoryUpdate::Flag::MessageSent
	) | rpl::filter([=](const Data::HistoryUpdate &update) {
		if (_openedForum) {
			return (update.history->peer == _openedForum);
		} else if (_openedFolder) {
			return (update.history->folder() == _openedFolder)
				&& !update.history->isPinnedDialog(FilterId());
		} else {
			return !update.history->folder()
				&& !update.history->isPinnedDialog(
					controller->activeChatsFilterCurrent());
		}
	}) | rpl::start_with_next([=](const Data::HistoryUpdate &update) {
		jumpToTop(true);
	}, lifetime());

	fullSearchRefreshOn(session().settings().skipArchiveInSearchChanges(
	) | rpl::to_empty);

	_inner->scrollByDeltaRequests(
	) | rpl::start_with_next([=](int delta) {
		if (_scroll) {
			_scroll->scrollToY(_scroll->scrollTop() + delta);
		}
	}, lifetime());

	_inner->mustScrollTo(
	) | rpl::start_with_next([=](const Ui::ScrollToRequest &data) {
		if (_scroll) {
			_scroll->scrollToY(data.ymin, data.ymax);
		}
	}, lifetime());
	_inner->dialogMoved(
	) | rpl::start_with_next([=](const Ui::ScrollToRequest &data) {
		const auto movedFrom = data.ymin;
		const auto movedTo = data.ymax;
		const auto st = _scroll->scrollTop();
		if (st > movedTo && st < movedFrom) {
			_scroll->scrollToY(st + _inner->st()->height);
		}
	}, lifetime());
	_inner->searchMessages(
	) | rpl::start_with_next([=] {
		needSearchMessages();
	}, lifetime());
	_inner->cancelSearchInChatRequests(
	) | rpl::start_with_next([=] {
		cancelSearchInChat();
	}, lifetime());
	_inner->completeHashtagRequests(
	) | rpl::start_with_next([=](const QString &tag) {
		completeHashtag(tag);
	}, lifetime());
	_inner->refreshHashtagsRequests(
	) | rpl::start_with_next([=] {
		filterCursorMoved();
	}, lifetime());
	_inner->cancelSearchFromUserRequests(
	) | rpl::start_with_next([=] {
		setSearchInChat((_openedForum && !_searchInChat)
			? Key(_openedForum->forum()->history())
			: _searchInChat, nullptr);
		applyFilterUpdate(true);
	}, lifetime());
	_inner->chosenRow(
	) | rpl::start_with_next([=](const ChosenRow &row) {
		chosenRow(row);
	}, lifetime());

	_scroll->geometryChanged(
	) | rpl::start_with_next(crl::guard(_inner, [=] {
		_inner->parentGeometryChanged();
	}), lifetime());
	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		listScrollUpdated();
	}, lifetime());

	session().data().chatsListChanges(
	) | rpl::filter([=](Data::Folder *folder) {
		return (folder == _inner->shownFolder());
	}) | rpl::start_with_next([=] {
		Ui::PostponeCall(this, [=] { listScrollUpdated(); });
	}, lifetime());

	QObject::connect(_filter, &Ui::InputField::changed, [=] {
		applyFilterUpdate();
	});
	QObject::connect(_filter, &Ui::InputField::submitted, [=] {
		submit();
	});
	QObject::connect(
		_filter->rawTextEdit().get(),
		&QTextEdit::cursorPositionChanged,
		this,
		[=] { filterCursorMoved(); },
		Qt::QueuedConnection); // So getLastText() works already.

	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker checker;
		rpl::merge(
			rpl::single(rpl::empty),
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

	_cancelSearch->setClickedCallback([this] { cancelSearch(); });
	_jumpToDate->entity()->setClickedCallback([this] { showCalendar(); });
	_chooseFromUser->entity()->setClickedCallback([this] { showSearchFrom(); });
	rpl::single(rpl::empty) | rpl::then(
		session().domain().local().localPasscodeChanged()
	) | rpl::start_with_next([=] {
		updateLockUnlockVisibility();
	}, lifetime());
	_lockUnlock->setClickedCallback([this] {
		_lockUnlock->setIconOverride(&st::dialogsUnlockIcon, &st::dialogsUnlockIconOver);
		Core::App().maybeLockByPasscode();
		_lockUnlock->setIconOverride(nullptr);
	});

	setupMainMenuToggle();
	setupShortcuts();

	_searchForNarrowFilters->setClickedCallback([=] {
		Ui::showChatsList(&session());
	});

	setAcceptDrops(true);

	_inner->setLoadMoreFilteredCallback([=] {
		const auto state = _inner->state();
		if (state == WidgetState::Filtered
			&& !_topicSearchFull
			&& searchForTopicsRequired(_topicSearchQuery)) {
			searchTopics();
		}
	});
	_inner->setLoadMoreCallback([=] {
		const auto state = _inner->state();
		if (state == WidgetState::Filtered
			&& (!_inner->waitingForSearch()
				|| (_searchInMigrated
					&& _searchFull
					&& !_searchFullMigrated))) {
			searchMore();
		} else if (_openedForum && state == WidgetState::Default) {
			_openedForum->forum()->requestTopics();
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

	changeOpenedForum(
		controller->openedForum().current(),
		anim::type::instant);

	controller->openedForum().changes(
	) | rpl::start_with_next([=](ChannelData *forum) {
		changeOpenedForum(forum, anim::type::normal);
	}, lifetime());

	setupDownloadBar();
}

void Widget::chosenRow(const ChosenRow &row) {
	const auto openSearchResult = !controller()->selectingPeer()
		&& row.filteredRow;
	const auto history = row.key.history();
	if (const auto topic = row.key.topic()) {
		controller()->content()->chooseThread(topic, row.message.fullId.msg);
	} else if (history && history->peer->isForum() && !row.message.fullId) {
		controller()->openForum(history->peer->asChannel());
		return;
	} else if (history) {
		const auto peer = history->peer;
		const auto showAtMsgId = controller()->uniqueChatsInSearchResults()
			? ShowAtUnreadMsgId
			: row.message.fullId.msg;
		if (row.newWindow && controller()->canShowSeparateWindow(peer)) {
			const auto active = controller()->activeChatCurrent();
			const auto fromActive = active.history()
				? (active.history()->peer == peer)
				: false;
			const auto toSeparate = [=] {
				Core::App().ensureSeparateWindowForPeer(
					peer,
					showAtMsgId);
			};
			if (fromActive) {
				controller()->window().preventOrInvoke([=] {
					controller()->content()->ui_showPeerHistory(
						0,
						Window::SectionShow::Way::ClearStack,
						0);
					toSeparate();
				});
			} else {
				toSeparate();
			}
		} else {
			controller()->content()->chooseThread(history, showAtMsgId);
		}
	} else if (const auto folder = row.key.folder()) {
		controller()->openFolder(folder);
	}
	if (openSearchResult && !session().supportMode()) {
		if (_subsectionTopBar) {
			_subsectionTopBar->toggleSearch(false, anim::type::instant);
		} else {
			escape();
		}
	}
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

void Widget::setupDownloadBar() {
	Data::MakeDownloadBarContent(
	) | rpl::start_with_next([=](Ui::DownloadBarContent &&content) {
		const auto create = (content.count && !_downloadBar);
		if (create) {
			_downloadBar = std::make_unique<Ui::DownloadBar>(
				this,
				Data::MakeDownloadBarProgress());
		}
		if (_downloadBar) {
			_downloadBar->show(std::move(content));
		}
		if (create) {
			_downloadBar->heightValue(
			) | rpl::start_with_next([=] {
				updateControlsGeometry();
			}, _downloadBar->lifetime());

			_downloadBar->shownValue(
			) | rpl::filter(
				!rpl::mappers::_1
			) | rpl::start_with_next([=] {
				_downloadBar = nullptr;
				updateControlsGeometry();
			}, _downloadBar->lifetime());

			_downloadBar->clicks(
			) | rpl::start_with_next([=] {
				auto &&list = Core::App().downloadManager().loadingList();
				const auto guard = gsl::finally([] {
					Core::App().downloadManager().clearIfFinished();
				});
				auto first = (HistoryItem*)nullptr;
				for (const auto id : list) {
					if (!first) {
						first = id->object.item;
					} else {
						controller()->showSection(
							Info::Downloads::Make(
								controller()->session().user()));
						return;
					}
				}
				if (first) {
					controller()->showMessage(first);
				}
			}, _downloadBar->lifetime());

			if (_connecting) {
				_connecting->raise();
			}
		}
	}, lifetime());
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

	rpl::single(rpl::empty) | rpl::then(
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

void Widget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return isActiveWindow()
			&& Ui::InFocusChain(this)
			&& !Ui::isLayerShown()
			&& !controller()->window().locked();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		if (controller()->selectingPeer()) {
			return;
		}
		if (_openedForum && !controller()->activeChatCurrent()) {
			request->check(Command::Search) && request->handle([=] {
				const auto history = _openedForum->forum()->history();
				controller()->content()->searchInChat(history);
				return true;
			});
		}
	}, lifetime());
}

void Widget::fullSearchRefreshOn(rpl::producer<> events) {
	std::move(
		events
	) | rpl::filter([=] {
		return !_searchQuery.isEmpty();
	}) | rpl::start_with_next([=] {
		_searchTimer.cancel();
		_searchCache.clear();
		_singleMessageSearch.clear();
		for (const auto &[requestId, query] : base::take(_searchQueries)) {
			session().api().request(requestId).cancel();
		}
		_searchQuery = QString();
		_scroll->scrollToY(0);
		cancelSearchRequest();
		searchMessages();
	}, lifetime());
}

void Widget::updateControlsVisibility(bool fast) {
	updateLoadMoreChatsVisibility();
	_scroll->show();
	if (_forwardCancel) {
		_forwardCancel->show();
	}
	if ((_openedFolder || _openedForum) && _filter->hasFocus()) {
		setInnerFocus();
	}
	if (_updateTelegram) {
		_updateTelegram->show();
	}
	_searchControls->setVisible(!_openedFolder && !_openedForum);
	if (_openedFolder || _openedForum) {
		_subsectionTopBar->show();
		if (_forumTopShadow) {
			_forumTopShadow->show();
		}
		if (_forumGroupCallBar) {
			_forumGroupCallBar->show();
		}
		if (_forumRequestsBar) {
			_forumRequestsBar->show();
		}
		if (_forumReportBar) {
			_forumReportBar->show();
		}
	} else {
		if (hasFocus()) {
			_filter->setFocus();
			_filter->finishAnimating();
		}
		updateLockUnlockVisibility();
		updateJumpToDateVisibility(fast);
		updateSearchFromVisibility(fast);
	}
	_connecting->setForceHidden(false);
}

void Widget::changeOpenedSubsection(
		FnMut<void()> change,
		bool fromRight,
		anim::type animated) {
	if (isHidden()) {
		animated = anim::type::instant;
	}
	if (animated == anim::type::normal) {
		_connecting->setForceHidden(true);
		_cacheUnder = grabForFolderSlideAnimation();
		_showDirection = fromRight
			? Window::SlideDirection::FromRight
			: Window::SlideDirection::FromLeft;
		_showAnimationType = ShowAnimation::Internal;
	}
	_a_show.stop();
	change();
	refreshTopBars();
	updateControlsVisibility(true);
	_peerSearchRequest = 0;
	_api.request(base::take(_topicSearchRequest)).cancel();
	if (animated == anim::type::normal) {
		_connecting->setForceHidden(true);
		_cacheOver = grabForFolderSlideAnimation();
		_connecting->setForceHidden(false);
		startSlideAnimation();
	}
}

void Widget::changeOpenedFolder(Data::Folder *folder, anim::type animated) {
	changeOpenedSubsection([&] {
		_openedFolder = folder;
		_inner->changeOpenedFolder(folder);
	}, (folder != nullptr), animated);
}

void Widget::changeOpenedForum(ChannelData *forum, anim::type animated) {
	changeOpenedSubsection([&] {
		cancelSearch();
		_openedForum = forum;
		_api.request(base::take(_topicSearchRequest)).cancel();
		_inner->changeOpenedForum(forum);
	}, (forum != nullptr), animated);
}

void Widget::refreshTopBars() {
	if (_openedFolder || _openedForum) {
		if (!_subsectionTopBar) {
			_subsectionTopBar.create(this, controller());
			_subsectionTopBar->searchCancelled(
			) | rpl::start_with_next([=] {
				escape();
			}, _subsectionTopBar->lifetime());
			_subsectionTopBar->searchSubmitted(
			) | rpl::start_with_next([=] {
				submit();
			}, _subsectionTopBar->lifetime());
			_subsectionTopBar->searchQuery(
			) | rpl::start_with_next([=](QString query) {
				applyFilterUpdate();
			}, _subsectionTopBar->lifetime());
			_subsectionTopBar->jumpToDateRequest(
			) | rpl::start_with_next([=] {
				showCalendar();
			}, _subsectionTopBar->lifetime());
			_subsectionTopBar->chooseFromUserRequest(
			) | rpl::start_with_next([=] {
				showSearchFrom();
			}, _subsectionTopBar->lifetime());
			updateControlsGeometry();
		}
		const auto history = _openedForum
			? session().data().history(_openedForum).get()
			: nullptr;
		_subsectionTopBar->setActiveChat(
			HistoryView::TopBarWidget::ActiveChat{
				.key = (_openedForum
					? Dialogs::Key(history)
					: Dialogs::Key(_openedFolder)),
				.section = Dialogs::EntryState::Section::ChatsList,
			}, history ? history->sendActionPainter().get() : nullptr);
		if (_forumSearchRequested) {
			showSearchInTopBar(anim::type::instant);
		}
	} else if (_subsectionTopBar) {
		if (_subsectionTopBar->searchHasFocus()) {
			setFocus();
		}
		_subsectionTopBar.destroy();
	}
	_forumSearchRequested = false;
	if (_openedForum) {
		_openedForum->updateFull();

		_forumReportBar = std::make_unique<HistoryView::ContactStatus>(
			controller(),
			this,
			_openedForum,
			true);
		_forumRequestsBar = std::make_unique<Ui::RequestsBar>(
			this,
			HistoryView::RequestsBarContentByPeer(
				_openedForum,
				st::historyRequestsUserpics.size,
				true));
		_forumGroupCallBar = std::make_unique<Ui::GroupCallBar>(
			this,
			HistoryView::GroupCallBarContentByPeer(
				_openedForum,
				st::historyGroupCallUserpics.size,
				true),
			Core::App().appDeactivatedValue());
		_forumTopShadow = std::make_unique<Ui::PlainShadow>(this);

		_forumRequestsBar->barClicks(
		) | rpl::start_with_next([=] {
			RequestsBoxController::Start(controller(), _openedForum);
		}, _forumRequestsBar->lifetime());

		rpl::merge(
			_forumGroupCallBar->barClicks(),
			_forumGroupCallBar->joinClicks()
		) | rpl::start_with_next([=] {
			if (_openedForum->groupCall()) {
				controller()->startOrJoinGroupCall(_openedForum);
			}
		}, _forumGroupCallBar->lifetime());

		if (_a_show.animating()) {
			_forumTopShadow->hide();
			_forumGroupCallBar->hide();
			_forumRequestsBar->hide();
			_forumReportBar->bar().hide();
		} else {
			_forumTopShadow->show();
			_forumGroupCallBar->show();
			_forumRequestsBar->show();
			_forumReportBar->show();
			_forumGroupCallBar->finishAnimating();
			_forumRequestsBar->finishAnimating();
		}

		rpl::combine(
			_forumGroupCallBar->heightValue(),
			_forumRequestsBar->heightValue(),
			_forumReportBar->bar().heightValue()
		) | rpl::start_with_next([=] {
			updateControlsGeometry();
		}, _forumRequestsBar->lifetime());
	} else {
		_forumTopShadow = nullptr;
		_forumGroupCallBar = nullptr;
		_forumRequestsBar = nullptr;
		_forumReportBar = nullptr;
		updateControlsGeometry();
	}
}

void Widget::showSearchInTopBar(anim::type animated) {
	Expects(_subsectionTopBar != nullptr);

	_subsectionTopBar->toggleSearch(true, animated);
	_subsectionTopBar->searchEnableChooseFromUser(
		true,
		!_searchFromAuthor);
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
		if (_updateTelegram) {
			return;
		}
		_updateTelegram.create(
			this,
			tr::lng_update_telegram(tr::now),
			st::dialogsUpdateButton,
			st::dialogsInstallUpdate,
			st::dialogsInstallUpdateOver);
		_updateTelegram->show();
		_updateTelegram->setClickedCallback([] {
			Core::checkReadyUpdate();
			Core::Restart();
		});
		if (_connecting) {
			_connecting->raise();
		}
	} else {
		if (!_updateTelegram) {
			return;
		}
		_updateTelegram.destroy();
	}
	updateControlsGeometry();
}

void Widget::setInnerFocus() {
	if (!_openedFolder && !_openedForum) {
		_filter->setFocus();
	} else if (!_subsectionTopBar->searchSetFocus()) {
		setFocus();
	}
}

void Widget::jumpToTop(bool belowPinned) {
	if (session().supportMode()) {
		return;
	}
	if ((currentSearchQuery().trimmed().isEmpty() && !_searchInChat)) {
		auto to = 0;
		if (belowPinned) {
			const auto list = _openedForum
				? _openedForum->forum()->topicsList()
				: controller()->activeChatsFilterCurrent()
				? session().data().chatsFilters().chatsList(
					controller()->activeChatsFilterCurrent())
				: session().data().chatsList(_openedFolder);
			const auto count = int(list->pinned()->order().size());
			const auto row = _inner->st()->height;
			const auto min = (row * (count * 2 + 1) - _scroll->height()) / 2;
			if (_scroll->scrollTop() <= min) {
				return;
			}
			// Don't jump too high up, below the pinned chats.
			to = std::max(min, to);
		}
		_scrollToAnimation.stop();
		_scroll->scrollToY(to);
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
	if (_subsectionTopBar) {
		_subsectionTopBar->hide();
	}
	if (_forumTopShadow) {
		_forumTopShadow->hide();
	}
	if (_forumGroupCallBar) {
		_forumGroupCallBar->hide();
	}
	if (_forumRequestsBar) {
		_forumRequestsBar->hide();
	}
	if (_forumReportBar) {
		_forumReportBar->bar().hide();
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

		if ((!_subsectionTopBar || !_subsectionTopBar->searchHasFocus())
			&& !_filter->hasFocus()) {
			controller()->widget()->setInnerFocus();
		}
	}
}

void Widget::escape() {
	if (!cancelSearch()) {
		if (controller()->openedForum().current()) {
			controller()->closeForum();
		} else if (controller()->openedFolder().current()) {
			controller()->closeFolder();
		} else if (controller()->activeChatEntryCurrent().key) {
			controller()->content()->dialogsCancelled();
		} else {
			const auto filters = &session().data().chatsFilters();
			const auto &list = filters->list();
			const auto first = list.empty() ? FilterId() : list.front().id();
			if (controller()->activeChatsFilterCurrent() != first) {
				controller()->setActiveChatsFilter(first);
			}
		}
	} else if (!_searchInChat && !controller()->selectingPeer()) {
		if (controller()->activeChatEntryCurrent().key) {
			controller()->content()->dialogsCancelled();
		}
	}
}

void Widget::submit() {
	if (_inner->chooseRow()) {
		return;
	}
	const auto state = _inner->state();
	if (state == WidgetState::Default
		|| (state == WidgetState::Filtered
			&& (!_inner->waitingForSearch() || _inner->hasFilteredResults()))) {
		_inner->selectSkip(1);
		_inner->chooseRow();
	} else {
		searchMessages();
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

bool Widget::searchMessages(bool searchCache) {
	auto result = false;
	auto q = currentSearchQuery().trimmed();
	if (q.isEmpty() && !_searchFromAuthor) {
		cancelSearchRequest();
		_api.request(base::take(_peerSearchRequest)).cancel();
		_api.request(base::take(_topicSearchRequest)).cancel();
		return true;
	}
	if (searchCache) {
		const auto success = _singleMessageSearch.lookup(q, [=] {
			needSearchMessages();
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
				((_searchInChat || _openedForum)
					? SearchRequestType::PeerFromStart
					: SearchRequestType::FromStart),
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
		if (const auto peer = searchInPeer()) {
			const auto topic = searchInTopic();
			auto &histories = session().data().histories();
			const auto type = Data::Histories::RequestType::History;
			const auto history = session().data().history(peer);
			_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
				const auto type = SearchRequestType::PeerFromStart;
				using Flag = MTPmessages_Search::Flag;
				_searchRequest = session().api().request(MTPmessages_Search(
					MTP_flags((topic ? Flag::f_top_msg_id : Flag())
						| (_searchQueryFrom ? Flag::f_from_id : Flag())),
					peer->input,
					MTP_string(_searchQuery),
					(_searchQueryFrom
						? _searchQueryFrom->input
						: MTP_inputPeerEmpty()),
					MTP_int(topic ? topic->rootId() : 0),
					MTP_inputMessagesFilterEmpty(),
					MTP_int(0), // min_date
					MTP_int(0), // max_date
					MTP_int(0), // offset_id
					MTP_int(0), // add_offset
					MTP_int(kSearchPerPage),
					MTP_int(0), // max_id
					MTP_int(0), // min_id
					MTP_long(0) // hash
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
				MTP_int(kSearchPerPage)
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
		_api.request(base::take(_peerSearchRequest)).cancel();
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
	if (searchForTopicsRequired(query)) {
		if (searchCache) {
			if (_topicSearchQuery != query) {
				result = false;
			}
		} else if (_topicSearchQuery != query) {
			_topicSearchQuery = query;
			_topicSearchFull = false;
			searchTopics();
		}
	} else {
		_api.request(base::take(_topicSearchRequest)).cancel();
		_topicSearchQuery = query;
		_topicSearchFull = true;
	}
	return result;
}

bool Widget::searchForPeersRequired(const QString &query) const {
	return !_searchInChat
		&& !_searchFromAuthor
		&& !_openedForum
		&& !query.isEmpty()
		&& (query[0] != '#');
}

bool Widget::searchForTopicsRequired(const QString &query) const {
	return !_searchInChat
		&& !_searchFromAuthor
		&& _openedForum
		&& !query.isEmpty()
		&& (query[0] != '#')
		&& !_openedForum->forum()->topicsList()->loaded();
}

void Widget::needSearchMessages() {
	if (!searchMessages(true)) {
		_searchTimer.callOnce(AutoSearchTimeout);
	}
}

void Widget::showMainMenu() {
	controller()->widget()->showMainMenu();
}

void Widget::searchMessages(
		const QString &query,
		Key inChat) {
	const auto inChatChanged = [&] {
		const auto inPeer = inChat.peer();
		const auto inTopic = inChat.topic();
		if (!inTopic && inPeer == _openedForum) {
			return false;
		} else if ((inTopic || (inPeer && !inPeer->isForum()))
			&& (inChat == _searchInChat)) {
			return false;
		} else if (const auto inPeer = inChat.peer()) {
			if (inPeer->migrateTo() == _searchInChat.peer()
				&& !_searchInChat.topic()) {
				return false;
			}
		}
		return true;
	}();
	if ((_filter->getLastText() != query) || inChatChanged) {
		if (inChat) {
			cancelSearch();
			setSearchInChat(inChat);
		}
		_filter->setText(query);
		applyFilterUpdate(true);
		_searchTimer.cancel();
		searchMessages();

		session().local().saveRecentSearchHashtags(query);
	}
}

void Widget::searchTopics() {
	if (_topicSearchRequest || _topicSearchFull) {
		return;
	}
	_api.request(base::take(_topicSearchRequest)).cancel();
	_topicSearchRequest = _api.request(MTPchannels_GetForumTopics(
		MTP_flags(MTPchannels_GetForumTopics::Flag::f_q),
		_openedForum->inputChannel,
		MTP_string(_topicSearchQuery),
		MTP_int(_topicSearchOffsetDate),
		MTP_int(_topicSearchOffsetId),
		MTP_int(_topicSearchOffsetTopicId),
		MTP_int(kSearchPerPage)
	)).done([=](const MTPmessages_ForumTopics &result) {
		_topicSearchRequest = 0;
		const auto savedTopicId = _topicSearchOffsetTopicId;
		const auto byCreation = result.data().is_order_by_create_date();
		_openedForum->forum()->applyReceivedTopics(result, [&](
				not_null<Data::ForumTopic*> topic) {
			_topicSearchOffsetTopicId = topic->rootId();
			if (byCreation) {
				_topicSearchOffsetDate = topic->creationDate();
				if (const auto last = topic->lastServerMessage()) {
					_topicSearchOffsetId = last->id;
				}
			} else if (const auto last = topic->lastServerMessage()) {
				_topicSearchOffsetId = last->id;
				_topicSearchOffsetDate = last->date();
			}
			_inner->appendToFiltered(topic);
		});
		if (_topicSearchOffsetTopicId != savedTopicId) {
			_inner->refresh();
		} else {
			_topicSearchFull = true;
		}
	}).fail([=] {
		_topicSearchFull = true;
	}).send();
}

void Widget::searchMore() {
	if (_searchRequest || _searchInHistoryRequest) {
		return;
	}
	if (!_searchFull) {
		if (const auto peer = searchInPeer()) {
			auto &histories = session().data().histories();
			const auto topic = searchInTopic();
			const auto type = Data::Histories::RequestType::History;
			const auto history = session().data().history(peer);
			_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
				const auto type = _lastSearchId
					? SearchRequestType::PeerFromOffset
					: SearchRequestType::PeerFromStart;
				using Flag = MTPmessages_Search::Flag;
				_searchRequest = session().api().request(MTPmessages_Search(
					MTP_flags((topic ? Flag::f_top_msg_id : Flag())
						| (_searchQueryFrom ? Flag::f_from_id : Flag())),
					peer->input,
					MTP_string(_searchQuery),
					(_searchQueryFrom
						? _searchQueryFrom->input
						: MTP_inputPeerEmpty()),
					MTP_int(topic ? topic->rootId() : 0),
					MTP_inputMessagesFilterEmpty(),
					MTP_int(0), // min_date
					MTP_int(0), // max_date
					MTP_int(_lastSearchId),
					MTP_int(0), // add_offset
					MTP_int(kSearchPerPage),
					MTP_int(0), // max_id
					MTP_int(0), // min_id
					MTP_long(0) // hash
				)).done([=](const MTPmessages_Messages &result) {
					searchReceived(type, result, _searchRequest);
					_searchInHistoryRequest = 0;
					finish();
				}).fail([=](const MTP::Error &error) {
					searchFailed(type, error, _searchRequest);
					_searchInHistoryRequest = 0;
					finish();
				}).send();
				if (!_lastSearchId) {
					_searchQueries.emplace(_searchRequest, _searchQuery);
				}
				return _searchRequest;
			});
		} else {
			const auto type = _lastSearchId
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
				(_lastSearchPeer
					? _lastSearchPeer->input
					: MTP_inputPeerEmpty()),
				MTP_int(_lastSearchId),
				MTP_int(kSearchPerPage)
			)).done([=](const MTPmessages_Messages &result) {
				searchReceived(type, result, _searchRequest);
			}).fail([=](const MTP::Error &error) {
				searchFailed(type, error, _searchRequest);
			}).send();
			if (!_lastSearchId) {
				_searchQueries.emplace(_searchRequest, _searchQuery);
			}
		}
	} else if (_searchInMigrated && !_searchFullMigrated) {
		auto &histories = session().data().histories();
		const auto type = Data::Histories::RequestType::History;
		const auto history = _searchInMigrated;
		_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
			const auto type = _lastSearchMigratedId
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
				MTP_int(0), // min_date
				MTP_int(0), // max_date
				MTP_int(_lastSearchMigratedId),
				MTP_int(0), // add_offset
				MTP_int(kSearchPerPage),
				MTP_int(0), // max_id
				MTP_int(0), // min_id
				MTP_long(0) // hash
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
	if (type == SearchRequestType::FromStart
		|| type == SearchRequestType::PeerFromStart) {
		_lastSearchPeer = nullptr;
		_lastSearchId = _lastSearchMigratedId = 0;
	}
	const auto isMigratedSearch = (type == SearchRequestType::MigratedFromStart)
		|| (type == SearchRequestType::MigratedFromOffset);
	const auto process = [&](const MTPVector<MTPMessage> &messages) {
		auto result = std::vector<not_null<HistoryItem*>>();
		for (const auto &message : messages.v) {
			const auto msgId = IdFromMessage(message);
			const auto peerId = PeerFromMessage(message);
			const auto lastDate = DateFromMessage(message);
			if (const auto peer = session().data().peerLoaded(peerId)) {
				if (lastDate) {
					const auto item = session().data().addNewMessage(
						message,
						MessageFlags(),
						NewMessageType::Existing);
					result.push_back(item);
				}
				_lastSearchPeer = peer;
			} else {
				LOG(("API Error: a search results with not loaded peer %1"
					).arg(peerId.value));
			}
			if (isMigratedSearch) {
				_lastSearchMigratedId = msgId;
			} else {
				_lastSearchId = msgId;
			}
		}
		return result;
	};
	auto fullCount = 0;
	auto messages = result.match([&](const MTPDmessages_messages &data) {
		if (_searchRequest != 0) {
			// Don't apply cached data!
			session().data().processUsers(data.vusers());
			session().data().processChats(data.vchats());
		}
		if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
		auto list = process(data.vmessages());
		fullCount = list.size();
		return list;
	}, [&](const MTPDmessages_messagesSlice &data) {
		if (_searchRequest != 0) {
			// Don't apply cached data!
			session().data().processUsers(data.vusers());
			session().data().processChats(data.vchats());
		}
		auto list = process(data.vmessages());
		const auto nextRate = data.vnext_rate();
		const auto rateUpdated = nextRate && (nextRate->v != _searchNextRate);
		const auto finished = (type == SearchRequestType::FromStart || type == SearchRequestType::FromOffset)
			? !rateUpdated
			: list.empty();
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
		fullCount = data.vcount().v;
		return list;
	}, [&](const MTPDmessages_channelMessages &data) {
		if (const auto peer = searchInPeer()) {
			if (const auto channel = peer->asChannel()) {
				channel->ptsReceived(data.vpts().v);
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
			session().data().processUsers(data.vusers());
			session().data().processChats(data.vchats());
		}
		auto list = process(data.vmessages());
		if (list.empty()) {
			if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
				_searchFullMigrated = true;
			} else {
				_searchFull = true;
			}
		}
		fullCount = data.vcount().v;
		return list;
	}, [&](const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! (Widget::searchReceived)"));
		if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
		return std::vector<not_null<HistoryItem*>>();
	});
	_inner->searchReceived(messages, inject, type, fullCount);

	_searchRequest = 0;
	listScrollUpdated();
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
		listScrollUpdated();
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
	_chooseByDragTimer.cancel();
}

void Widget::dragMoveEvent(QDragMoveEvent *e) {
	if (_scroll->geometry().contains(e->pos())) {
		if (_dragForward) {
			updateDragInScroll(true);
		} else {
			_chooseByDragTimer.callOnce(ChoosePeerByDragTimeout);
		}
		if (_inner->updateFromParentDrag(mapToGlobal(e->pos()))) {
			e->setDropAction(Qt::CopyAction);
		} else {
			e->setDropAction(Qt::IgnoreAction);
		}
	} else {
		if (_dragForward) {
			updateDragInScroll(false);
		}
		_inner->dragLeft();
		e->setDropAction(Qt::IgnoreAction);
	}
	e->accept();
}

void Widget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dragForward) {
		updateDragInScroll(false);
	} else {
		_chooseByDragTimer.cancel();
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
	_chooseByDragTimer.cancel();
	if (_scroll->geometry().contains(e->pos())) {
		const auto point = mapToGlobal(e->pos());
		if (const auto thread = _inner->updateFromParentDrag(point)) {
			e->acceptProposedAction();
			controller()->content()->onFilesOrForwardDrop(
				thread,
				e->mimeData());
			controller()->widget()->raise();
			controller()->widget()->activateWindow();
		}
	}
}

void Widget::listScrollUpdated() {
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

	const auto filterText = currentSearchQuery();
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
		auto switchToChooseFrom = HistoryView::SwitchToChooseFromQuery();
		if (_lastFilterText != switchToChooseFrom
			&& switchToChooseFrom.startsWith(_lastFilterText)
			&& filterText == switchToChooseFrom) {
			showSearchFrom();
		}
	}
	_lastFilterText = filterText;
}

void Widget::searchInChat(Key chat) {
	if (_openedForum && !chat.peer()->forum()) {
		controller()->closeForum();
	}
	if (_openedFolder) {
		controller()->closeFolder();
	}
	cancelSearch();
	setSearchInChat(chat);
	applyFilterUpdate(true);
}

void Widget::setSearchInChat(Key chat, PeerData *from) {
	const auto peer = chat.peer();
	const auto topic = chat.topic();
	const auto forum = peer ? peer->forum() : nullptr;
	if (chat.folder() || (forum && !topic)) {
		chat = Key();
	}
	const auto searchInPeerUpdated = (_searchInChat != chat);
	if (searchInPeerUpdated) {
		from = nullptr;
	} else if (!chat && !forum) {
		from = nullptr;
	}
	const auto searchFromUpdated = searchInPeerUpdated
		|| (_searchFromAuthor != from);
	_searchFromAuthor = from;

	if (forum) {
		if (controller()->openedForum().current() == peer) {
			showSearchInTopBar(anim::type::normal);
		} else {
			_forumSearchRequested = true;
			controller()->openForum(forum->channel());
		}
	}
	_searchInMigrated = nullptr;
	if (peer) {
		if (const auto migrateTo = peer->migrateTo()) {
			return setSearchInChat(peer->owner().history(migrateTo), from);
		} else if (const auto migrateFrom = peer->migrateFrom()) {
			if (!forum) {
				_searchInMigrated = peer->owner().history(migrateFrom);
			}
		}
	}
	if (searchInPeerUpdated) {
		_searchInChat = chat;
		controller()->searchInChat = _searchInChat;
		updateJumpToDateVisibility();
	}
	if (searchFromUpdated) {
		updateSearchFromVisibility();
		clearSearchCache();
	}
	_inner->searchInChat(_searchInChat, _searchFromAuthor);
	if (_subsectionTopBar) {
		_subsectionTopBar->searchEnableJumpToDate(
			_openedForum && _searchInChat);
	}
	if (_searchFromAuthor
		&& _lastFilterText == HistoryView::SwitchToChooseFromQuery()) {
		cancelSearch();
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
	_topicSearchQuery = QString();
	_topicSearchOffsetDate = 0;
	_topicSearchOffsetId = _topicSearchOffsetTopicId = 0;
	_api.request(base::take(_peerSearchRequest)).cancel();
	_api.request(base::take(_topicSearchRequest)).cancel();
	cancelSearchRequest();
}

void Widget::showCalendar() {
	if (_searchInChat) {
		controller()->showCalendar(_searchInChat, QDate());
	}
}

void Widget::showSearchFrom() {
	if (const auto peer = searchInPeer()) {
		const auto weak = base::make_weak(_searchInChat.topic());
		const auto chat = (!_searchInChat && _openedForum)
			? Key(_openedForum->forum()->history())
			: _searchInChat;
		auto box = SearchFromBox(
			peer,
			crl::guard(this, [=](not_null<PeerData*> from) {
				Ui::hideLayer();
				if (!chat.topic()) {
					setSearchInChat(chat, from);
				} else if (const auto strong = weak.get()) {
					setSearchInChat(strong, from);
				}
				applyFilterUpdate(true);
			}),
			crl::guard(this, [=] { _filter->setFocus(); }));
		if (box) {
			Window::Show(controller()).showBox(std::move(box));
		}
	}
}

void Widget::filterCursorMoved() {
	const auto to = _filter->textCursor().position();
	const auto text = _filter->getLastText();
	auto hashtag = QStringView();
	for (int start = to; start > 0;) {
		--start;
		if (text.size() <= start) {
			break;
		}
		const auto ch = text[start];
		if (ch == '#') {
			hashtag = base::StringViewMid(text, start, to - start);
			break;
		} else if (!ch.isLetterOrNumber() && ch != '_') {
			break;
		}
	}
	_inner->onHashtagFilterUpdate(hashtag);
}

void Widget::completeHashtag(QString tag) {
	const auto t = _filter->getLastText();;
	auto cur = _filter->textCursor().position();
	auto hashtag = QString();
	for (int start = cur; start > 0;) {
		--start;
		if (t.size() <= start) {
			break;
		} else if (t.at(start) == '#') {
			if (cur == start + 1
				|| base::StringViewMid(t, start + 1, cur - start - 1)
					== base::StringViewMid(tag, 0, cur - start - 1)) {
				for (; cur < t.size() && cur - start - 1 < tag.size(); ++cur) {
					if (t.at(cur) != tag.at(cur - start - 1)) break;
				}
				if (cur - start - 1 == tag.size() && cur < t.size() && t.at(cur) == ' ') ++cur;
				hashtag = t.mid(0, start + 1) + tag + ' ' + t.mid(cur);
				_filter->setText(hashtag);
				_filter->setCursorPosition(start + 1 + tag.size() + 1);
				applyFilterUpdate(true);
				return;
			}
			break;
		} else if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') {
			break;
		}
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
		|| (_openedForum != nullptr)
		|| !currentSearchQuery().isEmpty();
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
		if (const auto peer = searchInPeer()) {
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
		auto additional = QMargins();
		if (visible) {
			additional.setRight(_chooseFromUser->width());
		}
		_filter->setAdditionalMargins(additional);
	}
}

void Widget::updateControlsGeometry() {
	auto filterAreaTop = 0;
	if (_forwardCancel) {
		_forwardCancel->moveToLeft(0, filterAreaTop);
		filterAreaTop += st::dialogsForwardHeight;
	}
	auto smallLayoutWidth = (st::defaultDialogRow.padding.left() + st::defaultDialogRow.photoSize + st::defaultDialogRow.padding.left());
	auto smallLayoutRatio = (width() < st::columnMinimalWidthLeft) ? (st::columnMinimalWidthLeft - width()) / float64(st::columnMinimalWidthLeft - smallLayoutWidth) : 0.;
	auto filterLeft = (controller()->filtersWidth() ? st::dialogsFilterSkip : st::dialogsFilterPadding.x() + _mainMenuToggle->width()) + st::dialogsFilterPadding.x();
	auto filterRight = (session().domain().local().hasLocalPasscode() ? (st::dialogsFilterPadding.x() + _lockUnlock->width()) : st::dialogsFilterSkip) + st::dialogsFilterPadding.x();
	auto filterWidth = qMax(width(), st::columnMinimalWidthLeft) - filterLeft - filterRight;
	auto filterAreaHeight = st::topBarHeight;
	_searchControls->setGeometry(0, filterAreaTop, width(), filterAreaHeight);
	if (_subsectionTopBar) {
		_subsectionTopBar->setGeometry(_searchControls->geometry());
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

	if (_forumTopShadow) {
		_forumTopShadow->setGeometry(
			0,
			filterAreaTop + filterAreaHeight,
			width(),
			st::lineWidth);
	}
	const auto forumGroupCallTop = filterAreaTop + filterAreaHeight;
	if (_forumGroupCallBar) {
		_forumGroupCallBar->move(0, forumGroupCallTop);
		_forumGroupCallBar->resizeToWidth(width());
	}
	const auto forumRequestsTop = forumGroupCallTop
		+ (_forumGroupCallBar ? _forumGroupCallBar->height() : 0);
	if (_forumRequestsBar) {
		_forumRequestsBar->move(0, forumRequestsTop);
		_forumRequestsBar->resizeToWidth(width());
	}
	const auto forumReportTop = forumRequestsTop
		+ (_forumRequestsBar ? _forumRequestsBar->height() : 0);
	if (_forumReportBar) {
		_forumReportBar->bar().move(0, forumReportTop);
	}
	auto scrollTop = forumReportTop
		+ (_forumReportBar ? _forumReportBar->bar().height() : 0);
	auto newScrollTop = _scroll->scrollTop() + _topDelta;
	auto scrollHeight = height() - scrollTop;
	const auto putBottomButton = [&](auto &button) {
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
	putBottomButton(_downloadBar);
	putBottomButton(_loadMoreChats);
	const auto bottomSkip = (height() - scrollTop) - scrollHeight;
	if (_connecting) {
		_connecting->setBottomSkip(bottomSkip);
	}
	controller()->setConnectingBottomSkip(bottomSkip);
	auto wasScrollHeight = _scroll->height();
	_scroll->setGeometry(0, scrollTop, width(), scrollHeight);
	_inner->resize(width(), _inner->height());
	if (scrollHeight != wasScrollHeight) {
		controller()->floatPlayerAreaUpdated();
	}
	if (_topDelta) {
		_scroll->scrollToY(newScrollTop);
	} else {
		listScrollUpdated();
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

RowDescriptor Widget::resolveChatNext(RowDescriptor from) const {
	return _inner->resolveChatNext(from);
}

RowDescriptor Widget::resolveChatPrevious(RowDescriptor from) const {
   return _inner->resolveChatPrevious(from);
}

void Widget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		escape();
		//if (_openedForum) {
		//	controller()->closeForum();
		//} else if (_openedFolder) {
		//	controller()->closeFolder();
		//} else {
		//	e->ignore();
		//}
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		submit();
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

PeerData *Widget::searchInPeer() const {
	return _openedForum ? _openedForum : _searchInChat.peer();
}

Data::ForumTopic *Widget::searchInTopic() const {
	return _searchInChat.topic();
}

QString Widget::currentSearchQuery() const {
	return _subsectionTopBar
		? _subsectionTopBar->searchQueryCurrent()
		: _filter->getLastText();
}

void Widget::clearSearchField() {
	if (_subsectionTopBar) {
		_subsectionTopBar->searchClear();
	} else {
		_filter->clear();
	}
}

bool Widget::cancelSearch() {
	auto clearingQuery = !currentSearchQuery().isEmpty();
	auto clearingInChat = false;
	cancelSearchRequest();
	if (!clearingQuery && (_searchInChat || _searchFromAuthor)) {
		if (controller()->adaptive().isOneColumn()) {
			if (const auto thread = _searchInChat.thread()) {
				controller()->showThread(thread);
			} else {
				Unexpected("Empty key in cancelSearch().");
			}
		}
		setSearchInChat(Key());
		clearingInChat = true;
	}
	if (!clearingQuery
		&& _subsectionTopBar
		&& _subsectionTopBar->toggleSearch(false, anim::type::normal)) {
		setFocus();
		clearingInChat = true;
	}
	_lastSearchPeer = nullptr;
	_lastSearchId = _lastSearchMigratedId = 0;
	_inner->clearFilter();
	clearSearchField();
	applyFilterUpdate();
	return clearingQuery || clearingInChat;
}

void Widget::cancelSearchInChat() {
	cancelSearchRequest();
	const auto isOneColumn = controller()->adaptive().isOneColumn();
	if (_searchInChat) {
		if (isOneColumn
			&& !controller()->selectingPeer()
			&& currentSearchQuery().trimmed().isEmpty()) {
			if (const auto thread = _searchInChat.thread()) {
				controller()->showThread(thread);
			} else {
				Unexpected("Empty key in cancelSearchInPeer().");
			}
		}
		setSearchInChat(Key());
	}
	applyFilterUpdate(true);
	if (!isOneColumn && !controller()->selectingPeer()) {
		controller()->content()->dialogsCancelled();
	}
}

Widget::~Widget() {
	cancelSearchRequest();

	// Destructor may hide the bar and attempt to double-destroy it.
	base::take(_downloadBar);
}

} // namespace Dialogs
