/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_widget.h"

#include "base/qt/qt_key_modifiers.h"
#include "base/options.h"
#include "dialogs/ui/chat_search_in.h"
#include "dialogs/ui/dialogs_stories_content.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "dialogs/ui/dialogs_suggestions.h"
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
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/radial_animation.h"
#include "ui/chat/requests_bar.h"
#include "ui/chat/group_call_bar.h"
#include "ui/chat/more_chats_bar.h"
#include "ui/controls/download_bar.h"
#include "ui/controls/jump_down_button.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "api/api_chat_filters.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/ui_integration.h"
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
#include "data/components/recent_peers.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_user.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/data_download_manager.h"
#include "data/data_chat_filters.h"
#include "data/data_saved_sublist.h"
#include "data/data_stories.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/info_memento.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_window.h"
#include "base/qt/qt_common_adapters.h"

#include <QtCore/QMimeData>
#include <QtGui/QTextBlock>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QTextEdit>

namespace Dialogs {
namespace {

constexpr auto kSearchPerPage = 50;
constexpr auto kStoriesExpandDuration = crl::time(200);

base::options::toggle OptionForumHideChatsList({
	.id = kOptionForumHideChatsList,
	.name = "Hide chats list in forums",
	.description = "Don't keep a narrow column of chats list.",
});

[[nodiscard]] bool RedirectTextToSearch(const QString &text) {
	for (const auto &ch : text) {
		if (ch.unicode() >= 32) {
			return true;
		}
	}
	return false;
}

} // namespace

const char kOptionForumHideChatsList[] = "forum-hide-chats-list";

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
, _text(text)
, _st(st)
, _icon(icon)
, _iconOver(iconOver) {
	resize(st::columnMinimalWidthLeft, _st.height);
}

void Widget::BottomButton::setText(const QString &text) {
	_text = text;
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
	not_null<Window::SessionController*> controller,
	Layout layout)
: Window::AbstractSectionWidget(parent, controller, nullptr)
, _api(&controller->session().mtp())
, _chooseByDragTimer([=] { _inner->chooseRow(); })
, _layout(layout)
, _narrowWidth(st::defaultDialogRow.padding.left()
	+ st::defaultDialogRow.photoSize
	+ st::defaultDialogRow.padding.left())
, _searchControls(this)
, _mainMenu({
	.toggle = object_ptr<Ui::IconButton>(
		_searchControls,
		st::dialogsMenuToggle),
	.under = object_ptr<Ui::AbstractButton>(_searchControls),
})
, _searchForNarrowLayout(_searchControls, st::dialogsSearchForNarrowFilters)
, _search(_searchControls, st::dialogsFilter, tr::lng_dlg_filter())
, _chooseFromUser(
	_searchControls,
	object_ptr<Ui::IconButton>(this, st::dialogsSearchFrom))
, _jumpToDate(
	_searchControls,
	object_ptr<Ui::IconButton>(this, st::dialogsCalendar))
, _cancelSearch(_searchControls, st::dialogsCancelSearch)
, _lockUnlock(
	_searchControls,
	object_ptr<Ui::IconButton>(this, st::dialogsLock))
, _scroll(this)
, _scrollToTop(_scroll, st::dialogsToUp)
, _stories((_layout != Layout::Child)
	? std::make_unique<Stories::List>(
		this,
		st::dialogsStoriesList,
		_storiesContents.events() | rpl::flatten_latest())
	: nullptr)
, _searchTimer([=] { search(); })
, _singleMessageSearch(&controller->session()) {
	const auto makeChildListShown = [](PeerId peerId, float64 shown) {
		return InnerWidget::ChildListShown{ peerId, shown };
	};
	using OverscrollType = Ui::ElasticScroll::OverscrollType;
	_scroll->setOverscrollTypes(
		_stories ? OverscrollType::Virtual : OverscrollType::Real,
		OverscrollType::Real);
	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(
		this,
		controller,
		rpl::combine(
			_childListPeerId.value(),
			_childListShown.value(),
			makeChildListShown)));
	_scrollToTop->raise();
	_lockUnlock->toggle(false, anim::type::instant);

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
			return (update.history == _openedForum->history());
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
		searchRequested();
	}, lifetime());
	_inner->completeHashtagRequests(
	) | rpl::start_with_next([=](const QString &tag) {
		completeHashtag(tag);
	}, lifetime());
	_inner->refreshHashtagsRequests(
	) | rpl::start_with_next([=] {
		searchCursorMoved();
	}, lifetime());
	_inner->changeSearchTabRequests(
	) | rpl::filter([=](ChatSearchTab tab) {
		return _searchState.tab != tab;
	}) | rpl::start_with_next([=](ChatSearchTab tab) {
		auto copy = _searchState;
		copy.tab = tab;
		applySearchState(std::move(copy));
	}, lifetime());
	_inner->cancelSearchRequests(
	) | rpl::start_with_next([=] {
		setInnerFocus(true);
		applySearchState({});
	}, lifetime());
	_inner->cancelSearchFromRequests(
	) | rpl::start_with_next([=] {
		auto copy = _searchState;
		copy.fromPeer = nullptr;
		if (copy.inChat.sublist()) {
			copy.inChat = session().data().history(session().user());
		}
		applySearchState(std::move(copy));
	}, lifetime());
	_inner->changeSearchFromRequests(
	) | rpl::start_with_next([=] {
		showSearchFrom();
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

	setAttribute(Qt::WA_InputMethodEnabled);
	controller->widget()->imeCompositionStarts(
	) | rpl::filter([=] {
		return redirectImeToSearch();
	}) | rpl::start_with_next([=] {
		_search->setFocusFast();
	}, lifetime());

	_search->changes(
	) | rpl::start_with_next([=] {
		crl::on_main(this, [=] { applySearchUpdate(); });
	}, _search->lifetime());

	_search->submits(
	) | rpl::start_with_next([=] { submit(); }, _search->lifetime());

	QObject::connect(
		_search->rawTextEdit().get(),
		&QTextEdit::cursorPositionChanged,
		this,
		[=] { searchCursorMoved(); },
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

	_cancelSearch->setClickedCallback([this] { cancelSearch(); });
	_jumpToDate->entity()->setClickedCallback([this] { showCalendar(); });
	_chooseFromUser->entity()->setClickedCallback([this] { showSearchFrom(); });
	rpl::single(rpl::empty) | rpl::then(
		session().domain().local().localPasscodeChanged()
	) | rpl::start_with_next([=] {
		updateLockUnlockVisibility();
	}, lifetime());
	const auto lockUnlock = _lockUnlock->entity();
	lockUnlock->setClickedCallback([=] {
		lockUnlock->setIconOverride(
			&st::dialogsUnlockIcon,
			&st::dialogsUnlockIconOver);
		Core::App().maybeLockByPasscode();
		lockUnlock->setIconOverride(nullptr);
	});

	setupMainMenuToggle();
	setupShortcuts();
	if (_stories) {
		setupStories();
	}

	_searchForNarrowLayout->setClickedCallback([=] {
		_search->setFocusFast();
		if (_childList) {
			controller->closeForum();
		}
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
			&& (!_searchFull
				|| (_searchInMigrated
					&& _searchFull
					&& !_searchFullMigrated))) {
			searchMore();
		} else if (_openedForum && state == WidgetState::Default) {
			_openedForum->requestTopics();
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

	_search->customUpDown(true);

	updateJumpToDateVisibility(true);
	updateSearchFromVisibility(true);
	setupSupportMode();
	setupScrollUpButton();
	setupTouchChatPreview();

	const auto overscrollBg = [=] {
		return anim::color(
			st::dialogsBg,
			st::dialogsBgOver,
			_childListShown.current());
	};
	_scroll->setOverscrollBg(overscrollBg());
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_scroll->setOverscrollBg(overscrollBg());
	}, lifetime());

	if (_layout != Layout::Child) {
		setupConnectingWidget();

		changeOpenedFolder(
			controller->openedFolder().current(),
			anim::type::instant);

		controller->openedFolder().changes(
		) | rpl::start_with_next([=](Data::Folder *folder) {
			changeOpenedFolder(folder, anim::type::normal);
		}, lifetime());

		controller->shownForum().changes(
		) | rpl::filter(!rpl::mappers::_1) | rpl::start_with_next([=] {
			if (_openedForum) {
				changeOpenedForum(nullptr, anim::type::normal);
			} else if (_childList) {
				closeChildList(anim::type::normal);
			}
		}, lifetime());

		_childListShown.changes(
		) | rpl::start_with_next([=] {
			_scroll->setOverscrollBg(overscrollBg());
			updateControlsGeometry();
		}, lifetime());

		_childListShown.changes(
		) | rpl::filter((rpl::mappers::_1 == 0.) || (rpl::mappers::_1 == 1.)
		) | rpl::start_with_next([=](float64 shown) {
			const auto color = (shown > 0.) ? &st::dialogsRippleBg : nullptr;
			_mainMenu.toggle->setRippleColorOverride(color);
			_searchForNarrowLayout->setRippleColorOverride(color);
		}, lifetime());

		setupMoreChatsBar();
		setupDownloadBar();
	}
}

void Widget::chosenRow(const ChosenRow &row) {
	storiesToggleExplicitExpand(false);

	if (!_searchState.query.isEmpty()) {
		if (const auto history = row.key.history()) {
			session().recentPeers().bump(history->peer);
		}
	}

	const auto history = row.key.history();
	const auto topicJump = history
		? history->peer->forumTopicFor(row.message.fullId.msg)
		: nullptr;
	if (topicJump) {
		if (controller()->shownForum().current() == topicJump->forum()) {
			controller()->closeForum();
		} else {
			if (!controller()->adaptive().isOneColumn()) {
				controller()->showForum(
					topicJump->forum(),
					Window::SectionShow().withChildColumn());
			}
			controller()->showThread(
				topicJump,
				ShowAtUnreadMsgId,
				Window::SectionShow::Way::ClearStack);
		}
		return;
	} else if (const auto topic = row.key.topic()) {
		session().data().saveViewAsMessages(topic->forum(), false);
		controller()->showThread(
			topic,
			row.message.fullId.msg,
			Window::SectionShow::Way::ClearStack);
	} else if (history
		&& row.userpicClick
		&& (row.message.fullId.msg == ShowAtUnreadMsgId)
		&& history->peer->hasActiveStories()
		&& !history->peer->isSelf()) {
		controller()->openPeerStories(history->peer->id);
		return;
	} else if (history
		&& history->isForum()
		&& !row.message.fullId
		&& (!controller()->adaptive().isOneColumn()
			|| !history->peer->forum()->channel()->viewForumAsMessages())) {
		const auto forum = history->peer->forum();
		if (controller()->shownForum().current() == forum) {
			controller()->closeForum();
			return;
		}
		controller()->showForum(
			forum,
			Window::SectionShow().withChildColumn());
		if (forum->channel()->viewForumAsMessages()) {
			controller()->showThread(
				history,
				ShowAtUnreadMsgId,
				Window::SectionShow::Way::ClearStack);
		}
		return;
	} else if (history) {
		const auto peer = history->peer;
		const auto showAtMsgId = controller()->uniqueChatsInSearchResults()
			? ShowAtUnreadMsgId
			: row.message.fullId.msg;
		if (row.newWindow) {
			controller()->showInNewWindow(peer, showAtMsgId);
		} else {
			controller()->showThread(
				history,
				showAtMsgId,
				Window::SectionShow::Way::ClearStack);
			hideChildList();
		}
	} else if (const auto folder = row.key.folder()) {
		if (row.userpicClick) {
			const auto list = Data::StorySourcesList::Hidden;
			const auto &sources = session().data().stories().sources(list);
			if (!sources.empty()) {
				controller()->openPeerStories(sources.front().id, list);
				return;
			}
		}
		controller()->openFolder(folder);
		hideChildList();
	}
	if (row.filteredRow && !session().supportMode()) {
		if (_subsectionTopBar) {
			_subsectionTopBar->toggleSearch(false, anim::type::instant);
		} else {
			escape();
		}
	}
	updateForceDisplayWide();
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

void Widget::scrollToDefaultChecked(bool verytop) {
	if (_scrollToAnimation.animating()) {
		return;
	}
	scrollToDefault(verytop);
}

void Widget::setupScrollUpButton() {
	_scrollToTop->setClickedCallback([=] { scrollToDefaultChecked(); });
	trackScroll(_scrollToTop);
	trackScroll(this);
	updateScrollUpVisibility();
}

void Widget::setupTouchChatPreview() {
	_scroll->setCustomTouchProcess([=](not_null<QTouchEvent*> e) {
		return _inner->processTouchEvent(e);
	});
	_inner->touchCancelRequests() | rpl::start_with_next([=] {
		QTouchEvent ev(QEvent::TouchCancel);
		ev.setTimestamp(crl::now());
		QGuiApplication::sendEvent(_scroll, &ev);
	}, _inner->lifetime());
}

void Widget::setupMoreChatsBar() {
	if (_layout == Layout::Child) {
		return;
	}
	controller()->activeChatsFilter(
	) | rpl::start_with_next([=](FilterId id) {
		storiesToggleExplicitExpand(false);
		const auto cancelled = cancelSearch(true);
		const auto guard = gsl::finally([&] {
			if (cancelled) {
				controller()->content()->dialogsCancelled();
			}
		});

		if (!id) {
			_moreChatsBar = nullptr;
			updateControlsGeometry();
			return;
		}
		const auto filters = &session().data().chatsFilters();
		_moreChatsBar = std::make_unique<Ui::MoreChatsBar>(
			this,
			filters->moreChatsContent(id));

		trackScroll(_moreChatsBar->wrap());

		_moreChatsBar->barClicks(
		) | rpl::start_with_next([=] {
			if (const auto missing = filters->moreChats(id)
				; !missing.empty()) {
				Api::ProcessFilterUpdate(controller(), id, missing);
			}
		}, _moreChatsBar->lifetime());

		_moreChatsBar->closeClicks(
		) | rpl::start_with_next([=] {
			Api::ProcessFilterUpdate(controller(), id, {});
		}, _moreChatsBar->lifetime());

		if (_showAnimation) {
			_moreChatsBar->hide();
		} else {
			_moreChatsBar->show();
			_moreChatsBar->finishAnimating();
		}

		_moreChatsBar->heightValue(
		) | rpl::start_with_next([=] {
			updateControlsGeometry();
		}, _moreChatsBar->lifetime());
	}, lifetime());
}

void Widget::setupDownloadBar() {
	if (_layout == Layout::Child) {
		return;
	}

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
	const auto smallColumn = (width() < st::columnMinimalWidthLeft)
		|| _childList;
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
	const auto shouldBeHidden
		= !_scrollToTopIsShown && !_scrollToTopShown.animating();
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
	_mainMenu.under->setClickedCallback([=] {
		_mainMenu.toggle->clicked({}, Qt::LeftButton);
	});
	_mainMenu.under->stackUnder(_mainMenu.toggle);
	_mainMenu.toggle->setClickedCallback([=] { showMainMenu(); });

	rpl::single(rpl::empty) | rpl::then(
		controller()->filtersMenuChanged()
	) | rpl::start_with_next([=] {
		const auto filtersHidden = !controller()->filtersWidth();
		_mainMenu.toggle->setVisible(filtersHidden);
		_mainMenu.under->setVisible(filtersHidden);
		_searchForNarrowLayout->setVisible(!filtersHidden);
		updateControlsGeometry();
	}, lifetime());

	Window::OtherAccountsUnreadState(
	) | rpl::start_with_next([=](const Window::OthersUnreadState &state) {
		const auto icon = !state.count
			? nullptr
			: !state.allMuted
			? &st::dialogsMenuToggleUnread
			: &st::dialogsMenuToggleUnreadMuted;
		_mainMenu.toggle->setIconOverride(icon, icon);
	}, _mainMenu.toggle->lifetime());
}

void Widget::setupStories() {
	_stories->verticalScrollEvents(
	) | rpl::start_with_next([=](not_null<QWheelEvent*> e) {
		_scroll->viewportEvent(e);
	}, _stories->lifetime());

	if (!Core::App().settings().storiesClickTooltipHidden()) {
		// Don't create tooltip
		// until storiesClickTooltipHidden can be returned to false.
		const auto hideTooltip = [=] {
			Core::App().settings().setStoriesClickTooltipHidden(true);
			Core::App().saveSettingsDelayed();
		};
		_stories->setShowTooltip(
			parentWidget(),
			rpl::combine(
				Core::App().settings().storiesClickTooltipHiddenValue(),
				shownValue(),
				!rpl::mappers::_1 && rpl::mappers::_2),
			hideTooltip);
	}

	_storiesContents.fire(Stories::ContentForSession(
		&controller()->session(),
		Data::StorySourcesList::NotHidden));

	const auto currentSource = [=] {
		using List = Data::StorySourcesList;
		return _openedFolder ? List::Hidden : List::NotHidden;
	};

	rpl::combine(
		_scroll->positionValue(),
		_scroll->movementValue(),
		_storiesExplicitExpandValue.value()
	) | rpl::start_with_next([=](
			Ui::ElasticScrollPosition position,
			Ui::ElasticScrollMovement movement,
			int explicitlyExpanded) {
		if (_stories->isHidden()) {
			return;
		}
		const auto overscrollTop = std::max(-position.overscroll, 0);
		if (overscrollTop > 0 && _storiesExplicitExpand) {
			_scroll->setOverscrollDefaults(
				-st::dialogsStoriesFull.height,
				0,
				true);
		}
		if (explicitlyExpanded > 0 && explicitlyExpanded < overscrollTop) {
			_storiesExplicitExpandAnimation.stop();
			_storiesExplicitExpand = false;
			_storiesExplicitExpandValue = 0;
			return;
		}
		const auto above = std::max(explicitlyExpanded, overscrollTop);
		if (_aboveScrollAdded != above) {
			_aboveScrollAdded = above;
			if (_updateScrollGeometryCached) {
				_updateScrollGeometryCached();
			}
		}
		using Phase = Ui::ElasticScrollMovement;
		_stories->setExpandedHeight(
			_aboveScrollAdded,
			((movement == Phase::Momentum || movement == Phase::Returning)
				&& (explicitlyExpanded < above)));
		if (position.overscroll > 0
			|| (position.value
				> (_storiesExplicitExpandScrollTop
					+ st::dialogsRowHeight))) {
			storiesToggleExplicitExpand(false);
		}
		updateLockUnlockPosition();
	}, lifetime());

	_stories->collapsedGeometryChanged(
	) | rpl::start_with_next([=] {
		updateLockUnlockPosition();
	}, lifetime());

	_stories->clicks(
	) | rpl::start_with_next([=](uint64 id) {
		controller()->openPeerStories(PeerId(int64(id)), currentSource());
	}, lifetime());

	_stories->showMenuRequests(
	) | rpl::start_with_next([=](const Stories::ShowMenuRequest &request) {
		FillSourceMenu(controller(), request);
	}, lifetime());

	_stories->loadMoreRequests(
	) | rpl::start_with_next([=] {
		session().data().stories().loadMore(currentSource());
	}, lifetime());

	_stories->toggleExpandedRequests(
	) | rpl::start_with_next([=](bool expanded) {
		const auto position = _scroll->position();
		if (!expanded) {
			_scroll->setOverscrollDefaults(0, 0);
		} else if (position.value > 0 || position.overscroll >= 0) {
			storiesToggleExplicitExpand(true);
			_scroll->setOverscrollDefaults(0, 0);
		} else {
			_scroll->setOverscrollDefaults(
				-st::dialogsStoriesFull.height,
				0);
		}
	}, lifetime());

	_stories->emptyValue() | rpl::skip(1) | rpl::start_with_next([=] {
		updateStoriesVisibility();
	}, lifetime());

	_stories->widthValue() | rpl::start_with_next([=] {
		updateLockUnlockPosition();
	}, lifetime());
}

void Widget::storiesToggleExplicitExpand(bool expand) {
	if (_storiesExplicitExpand == expand) {
		return;
	}
	_storiesExplicitExpand = expand;
	if (!expand) {
		_scroll->setOverscrollDefaults(0, 0, true);
	}
	const auto height = st::dialogsStoriesFull.height;
	const auto duration = kStoriesExpandDuration;
	_storiesExplicitExpandScrollTop = _scroll->position().value;
	_storiesExplicitExpandAnimation.start([=](float64 value) {
		_storiesExplicitExpandValue = int(base::SafeRound(value));
	}, expand ? 0 : height, expand ? height : 0, duration, anim::sineInOut);
}

void Widget::trackScroll(not_null<Ui::RpWidget*> widget) {
	widget->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::TouchBegin
			|| type == QEvent::TouchUpdate
			|| type == QEvent::TouchEnd
			|| type == QEvent::TouchCancel
			|| type == QEvent::Wheel) {
			_scroll->viewportEvent(e);
		}
	}, widget->lifetime());
}

void Widget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return isActiveWindow()
			&& Ui::InFocusChain(this)
			&& !_childList
			&& !controller()->isLayerShown()
			&& !controller()->window().locked();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		if (!controller()->activeChatCurrent()) {
			request->check(Command::Search) && request->handle([=] {
				if (const auto forum = _openedForum) {
					const auto history = forum->history();
					controller()->searchInChat(history);
					return true;
				} else if (!_openedFolder
					&& !_childList
					&& _search->isVisible()) {
					_search->setFocus();
					return true;
				}
				return false;
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
		search();
	}, lifetime());
}

void Widget::updateControlsVisibility(bool fast) {
	updateLoadMoreChatsVisibility();
	_scroll->setVisible(!_suggestions && _hidingSuggestions.empty());
	updateStoriesVisibility();
	if ((_openedFolder || _openedForum) && _searchHasFocus) {
		setInnerFocus();
	}
	if (_updateTelegram) {
		_updateTelegram->show();
	}
	_searchControls->setVisible(!_openedFolder && !_openedForum);
	if (_moreChatsBar) {
		_moreChatsBar->show();
	}
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
		updateLockUnlockVisibility();
		updateJumpToDateVisibility(fast);
		updateSearchFromVisibility(fast);
	}
	if (_connecting) {
		_connecting->setForceHidden(false);
	}
	if (_childList) {
		_childList->show();
		_childListShadow->show();
	}
	if (_hideChildListCanvas) {
		_hideChildListCanvas->show();
	}
	if (_childList && _searchHasFocus) {
		setInnerFocus();
	}
	updateLockUnlockPosition();
}

void Widget::updateLockUnlockPosition() {
	if (_lockUnlock->isHidden()) {
		return;
	}
	const auto stories = (_stories && !_stories->isHidden())
		? _stories->collapsedGeometryCurrent()
		: Stories::List::CollapsedGeometry();
	const auto simple = _search->x() + _search->width();
	const auto right = stories.geometry.isEmpty()
		? simple
		: anim::interpolate(stories.geometry.x(), simple, stories.expanded);
	_lockUnlock->move(
		right - _lockUnlock->width(),
		st::dialogsFilterPadding.y());
}

void Widget::updateHasFocus(not_null<QWidget*> focused) {
	const auto has = (focused == _search.data())
		|| (focused == _search->rawTextEdit());
	if (_searchHasFocus != has) {
		_searchHasFocus = has;
		if (_postponeProcessSearchFocusChange) {
			return;
		} else if (has) {
			processSearchFocusChange();
		} else {
			// Search field may loose focus from the destructor of some
			// widget, in that case we don't want to destroy _suggestions
			// synchronously, because it may lead to a crash.
			crl::on_main(this, [=] { processSearchFocusChange(); });
		}
	}
}

bool Widget::cancelSearchByMouseBack() {
	return _searchHasFocus
		&& !_searchSuggestionsLocked
		&& !_searchState.inChat
		&& cancelSearch();
}

void Widget::processSearchFocusChange() {
	_searchSuggestionsLocked = _suggestions && _suggestions->persist();
	updateCancelSearch();
	updateForceDisplayWide();
	updateSuggestions(anim::type::normal);
}

void Widget::updateSuggestions(anim::type animated) {
	const auto suggest = (_searchHasFocus || _searchSuggestionsLocked)
		&& !_searchState.inChat
		&& (_inner->state() == WidgetState::Default);
	if (anim::Disabled() || !session().data().chatsListLoaded()) {
		animated = anim::type::instant;
	}
	if (!suggest && _suggestions) {
		if (animated == anim::type::normal) {
			auto taken = base::take(_suggestions);
			taken->setVisible(false);
			storiesExplicitCollapse();
			updateStoriesVisibility();
			startWidthAnimation();
			taken->setVisible(true);
			_suggestions = base::take(taken);

			_suggestions->hide(animated, [=, raw = _suggestions.get()] {
				stopWidthAnimation();
				_hidingSuggestions.erase(
					ranges::remove(
						_hidingSuggestions,
						raw,
						&std::unique_ptr<Suggestions>::get),
					end(_hidingSuggestions));
				updateControlsVisibility();
			});
			_hidingSuggestions.push_back(std::move(_suggestions));
		} else {
			_suggestions = nullptr;
			_hidingSuggestions.clear();
			storiesExplicitCollapse();
			updateStoriesVisibility();
			_scroll->show();
		}
	} else if (suggest && !_suggestions) {
		if (animated == anim::type::normal) {
			startWidthAnimation();
		}
		// Hides stories and passcode lock.
		updateStoriesVisibility();
		_suggestions = std::make_unique<Suggestions>(
			this,
			controller(),
			TopPeersContent(&session()),
			RecentPeersContent(&session()));
		_searchSuggestionsLocked = false;

		rpl::merge(
			_suggestions->topPeerChosen(),
			_suggestions->recentPeerChosen(),
			_suggestions->myChannelChosen(),
			_suggestions->recommendationChosen()
		) | rpl::start_with_next([=](not_null<PeerData*> peer) {
			if (_searchSuggestionsLocked
				&& (!_suggestions || !_suggestions->persist())) {
				processSearchFocusChange();
			}
			chosenRow({
				.key = peer->owner().history(peer),
				.newWindow = base::IsCtrlPressed(),
			});
			if (!_searchSuggestionsLocked && _searchHasFocus) {
				setFocus();
				controller()->widget()->setInnerFocus();
			}
		}, _suggestions->lifetime());

		updateControlsGeometry();

		_suggestions->show(animated, [=] {
			stopWidthAnimation();
		});
		_scroll->hide();
	} else {
		updateStoriesVisibility();
	}
}

void Widget::changeOpenedSubsection(
		FnMut<void()> change,
		bool fromRight,
		anim::type animated) {
	if (isHidden()) {
		animated = anim::type::instant;
	}
	auto oldContentCache = QPixmap();
	const auto showDirection = fromRight
		? Window::SlideDirection::FromRight
		: Window::SlideDirection::FromLeft;
	if (animated == anim::type::normal) {
		if (_connecting) {
			_connecting->setForceHidden(true);
		}
		oldContentCache = grabForFolderSlideAnimation();
	}
	//_scroll->verticalScrollBar()->setMinimum(0);
	_showAnimation = nullptr;
	destroyChildListCanvas();
	change();
	refreshTopBars();
	updateControlsVisibility(true);
	_peerSearchRequest = 0;
	_api.request(base::take(_topicSearchRequest)).cancel();
	if (animated == anim::type::normal) {
		if (_connecting) {
			_connecting->setForceHidden(true);
		}
		auto newContentCache = grabForFolderSlideAnimation();
		if (_connecting) {
			_connecting->setForceHidden(false);
		}
		startSlideAnimation(
			std::move(oldContentCache),
			std::move(newContentCache),
			showDirection);
	}
}

void Widget::destroyChildListCanvas() {
	_childListShown = 0.;
	_hideChildListCanvas = nullptr;
}

void Widget::changeOpenedFolder(Data::Folder *folder, anim::type animated) {
	if (_openedFolder == folder) {
		return;
	}
	changeOpenedSubsection([&] {
		cancelSearch(true);
		closeChildList(anim::type::instant);
		controller()->closeForum();
		_openedFolder = folder;
		_inner->changeOpenedFolder(folder);
		if (_stories) {
			storiesExplicitCollapse();
		}
	}, (folder != nullptr), animated);
}

void Widget::storiesExplicitCollapse() {
	if (_storiesExplicitExpand) {
		storiesToggleExplicitExpand(false);
	} else if (_stories) {
		using Type = Ui::ElasticScroll::OverscrollType;
		_scroll->setOverscrollDefaults(0, 0);
		_scroll->setOverscrollTypes(Type::None, Type::Real);
		_scroll->setOverscrollTypes(
			_stories->isHidden() ? Type::Real : Type::Virtual,
			Type::Real);
	}
	_storiesExplicitExpandAnimation.stop();
	_storiesExplicitExpandValue = 0;

	using List = Data::StorySourcesList;
	collectStoriesUserpicsViews(_openedFolder
		? List::NotHidden
		: List::Hidden);
	_storiesContents.fire(Stories::ContentForSession(
		&session(),
		_openedFolder ? List::Hidden : List::NotHidden));
}

void Widget::collectStoriesUserpicsViews(Data::StorySourcesList list) {
	auto &map = (list == Data::StorySourcesList::Hidden)
		? _storiesUserpicsViewsHidden
		: _storiesUserpicsViewsShown;
	map.clear();
	auto &owner = session().data();
	for (const auto &source : owner.stories().sources(list)) {
		if (const auto peer = owner.peerLoaded(source.id)) {
			if (auto view = peer->activeUserpicView(); view.cloud) {
				map.emplace(source.id, std::move(view));
			}
		}
	}
}

void Widget::changeOpenedForum(Data::Forum *forum, anim::type animated) {
	if (_openedForum == forum) {
		return;
	}
	changeOpenedSubsection([&] {
		cancelSearch(true);
		closeChildList(anim::type::instant);
		_openedForum = forum;
		_searchState.tab = forum
			? ChatSearchTab::ThisPeer
			: ChatSearchTab::MyMessages;
		_api.request(base::take(_topicSearchRequest)).cancel();
		_inner->changeOpenedForum(forum);
		storiesToggleExplicitExpand(false);
		updateStoriesVisibility();
	}, (forum != nullptr), animated);
}

void Widget::hideChildList() {
	if (_childList) {
		controller()->closeForum();
	}
}

void Widget::refreshTopBars() {
	if (_openedFolder || _openedForum) {
		if (!_subsectionTopBar) {
			_subsectionTopBar.create(this, controller());
			if (_stories) {
				_stories->raise();
			}
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
				applySearchUpdate();
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
			? _openedForum->history().get()
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
		updateSearchFromVisibility(true);
	}
	_forumSearchRequested = false;
	if (_openedForum) {
		const auto channel = _openedForum->channel();
		channel->updateFull();

		_forumReportBar = std::make_unique<HistoryView::ContactStatus>(
			controller(),
			this,
			channel,
			true);
		_forumRequestsBar = std::make_unique<Ui::RequestsBar>(
			this,
			HistoryView::RequestsBarContentByPeer(
				channel,
				st::historyRequestsUserpics.size,
				true));
		_forumGroupCallBar = std::make_unique<Ui::GroupCallBar>(
			this,
			HistoryView::GroupCallBarContentByPeer(
				channel,
				st::historyGroupCallUserpics.size,
				true),
			Core::App().appDeactivatedValue());
		_forumTopShadow = std::make_unique<Ui::PlainShadow>(this);

		_forumRequestsBar->barClicks(
		) | rpl::start_with_next([=] {
			RequestsBoxController::Start(controller(), channel);
		}, _forumRequestsBar->lifetime());

		rpl::merge(
			_forumGroupCallBar->barClicks(),
			_forumGroupCallBar->joinClicks()
		) | rpl::start_with_next([=] {
			if (channel->groupCall()) {
				controller()->startOrJoinGroupCall(channel);
			}
		}, _forumGroupCallBar->lifetime());

		if (_showAnimation) {
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
	updateForceDisplayWide();
}

QPixmap Widget::grabForFolderSlideAnimation() {
	const auto hidden = _scrollToTop->isHidden();
	if (!hidden) {
		_scrollToTop->hide();
	}

	const auto rect = QRect(
		0,
		0,
		width(),
		_scroll->y() + _scroll->height());
	auto result = Ui::GrabWidget(this, rect);

	if (!hidden) {
		_scrollToTop->show();
	}
	return result;
}

void Widget::checkUpdateStatus() {
	Expects(!Core::UpdaterDisabled());

	if (_layout == Layout::Child) {
		return;
	}

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

void Widget::setInnerFocus(bool unfocusSearch) {
	if (_childList) {
		_childList->setInnerFocus();
	} else if (_subsectionTopBar && _subsectionTopBar->searchSetFocus()) {
		return;
	} else if (!unfocusSearch
		&& (!_search->getLastText().isEmpty()
			|| _searchState.inChat
			|| _searchHasFocus
			|| _searchSuggestionsLocked)) {
		_search->setFocus();
	} else {
		setFocus();
	}
}

bool Widget::searchHasFocus() const {
	return _searchHasFocus;
}

void Widget::jumpToTop(bool belowPinned) {
	if (session().supportMode()) {
		return;
	}
	if ((_searchState.query.trimmed().isEmpty() && !_searchState.inChat)) {
		auto to = 0;
		if (belowPinned) {
			const auto list = _openedForum
				? _openedForum->topicsList()
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

void Widget::raiseWithTooltip() {
	raise();
	if (_stories) {
		Ui::PostponeCall(this, [=] {
			_stories->raiseTooltip();
		});
	}
}

void Widget::scrollToDefault(bool verytop) {
	if (verytop) {
		//_scroll->verticalScrollBar()->setMinimum(0);
	}
	_scrollToAnimation.stop();
	auto scrollTop = _scroll->scrollTop();
	const auto scrollTo = 0;
	if (scrollTop == scrollTo) {
		return;
	}
	const auto maxAnimatedDelta = _scroll->height();
	if (scrollTo + maxAnimatedDelta < scrollTop) {
		scrollTop = scrollTo + maxAnimatedDelta;
		_scroll->scrollToY(scrollTop);
	}

	startScrollUpButtonAnimation(false);

	const auto scroll = [=] {
		const auto animated = qRound(_scrollToAnimation.value(scrollTo));
		const auto animatedDelta = animated - scrollTo;
		const auto realDelta = _scroll->scrollTop() - scrollTo;
		if (base::OppositeSigns(realDelta, animatedDelta)) {
			// We scrolled manually to the other side of target 'scrollTo'.
			_scrollToAnimation.stop();
		} else if (std::abs(realDelta) > std::abs(animatedDelta)) {
			// We scroll by animation only if it gets us closer to target.
			_scroll->scrollToY(animated);
		}
	};

	_scrollAnimationTo = scrollTo;
	_scrollToAnimation.start(
		scroll,
		scrollTop,
		scrollTo,
		st::slideDuration,
		anim::sineInOut);
}

[[nodiscard]] QPixmap Widget::grabNonNarrowScrollFrame() {
	auto scrollGeometry = _scroll->geometry();
	const auto top = _searchControls->y() + _searchControls->height();
	const auto skip = scrollGeometry.y() - top;
	auto wideGeometry = QRect(
		scrollGeometry.x(),
		scrollGeometry.y(),
		std::max(scrollGeometry.width(), st::columnMinimalWidthLeft),
		scrollGeometry.height());
	_scroll->setGeometry(wideGeometry);
	_inner->resize(wideGeometry.width(), _inner->height());
	_inner->setNarrowRatio(0.);
	Ui::SendPendingMoveResizeEvents(_scroll);
	const auto grabSize = QSize(
		wideGeometry.width(),
		skip + wideGeometry.height());
	auto image = QImage(
		grabSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		QPainter p(&image);
		Ui::RenderWidget(
			p,
			this,
			QPoint(),
			QRect(0, top, wideGeometry.width(), skip));
		Ui::RenderWidget(p, _scroll, QPoint(0, skip));
	}
	if (scrollGeometry != wideGeometry) {
		_scroll->setGeometry(scrollGeometry);
		updateControlsGeometry();
	}
	return Ui::PixmapFromImage(std::move(image));
}

void Widget::startWidthAnimation() {
	if (!_widthAnimationCache.isNull()) {
		return;
	}
	_widthAnimationCache = grabNonNarrowScrollFrame();
	_scroll->hide();
	updateStoriesVisibility();
}

void Widget::stopWidthAnimation() {
	_widthAnimationCache = QPixmap();
	if (!_showAnimation) {
		_scroll->setVisible(!_suggestions);
	}
	updateStoriesVisibility();
	update();
}

void Widget::updateStoriesVisibility() {
	updateLockUnlockVisibility();
	if (!_stories) {
		return;
	}
	const auto hidden = (_showAnimation != nullptr)
		|| _openedForum
		|| !_widthAnimationCache.isNull()
		|| _childList
		|| _searchHasFocus
		|| _searchSuggestionsLocked
		|| !_searchState.query.isEmpty()
		|| _searchState.inChat
		|| _stories->empty();
	if (_stories->isHidden() != hidden) {
		_stories->setVisible(!hidden);
		using Type = Ui::ElasticScroll::OverscrollType;
		if (hidden) {
			_scroll->setOverscrollDefaults(0, 0);
			_scroll->setOverscrollTypes(Type::Real, Type::Real);
			if (_scroll->position().overscroll < 0) {
				_scroll->scrollToY(0);
			}
			_scroll->update();
		} else {
			_scroll->setOverscrollDefaults(0, 0);
			_scroll->setOverscrollTypes(Type::Virtual, Type::Real);
			_storiesExplicitExpandValue.force_assign(
				_storiesExplicitExpandValue.current());
		}
		if (_aboveScrollAdded > 0 && _updateScrollGeometryCached) {
			_updateScrollGeometryCached();
		}
		updateLockUnlockPosition();
	}
}

void Widget::showFast() {
	if (isHidden()) {
		_inner->clearSelection();
	}
	show();
}

rpl::producer<float64> Widget::shownProgressValue() const {
	return _shownProgressValue.value();
}

void Widget::showAnimated(
		Window::SlideDirection direction,
		const Window::SectionSlideParams &params) {
	_showAnimation = nullptr;

	auto oldContentCache = params.oldContentCache;
	showFast();
	auto newContentCache = Ui::GrabWidget(this);

	if (_updateTelegram) {
		_updateTelegram->hide();
	}
	if (_connecting) {
		_connecting->setForceHidden(true);
	}
	if (_childList) {
		_childList->hide();
		_childListShadow->hide();
	}
	_shownProgressValue = 0.;
	startSlideAnimation(
		std::move(oldContentCache),
		std::move(newContentCache),
		direction);
}

void Widget::startSlideAnimation(
		QPixmap oldContentCache,
		QPixmap newContentCache,
		Window::SlideDirection direction) {
	_scroll->hide();
	if (_stories) {
		_stories->hide();
	}
	_searchControls->hide();
	if (_subsectionTopBar) {
		_subsectionTopBar->hide();
	}
	if (_moreChatsBar) {
		_moreChatsBar->hide();
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

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback([=] {
		if (_shownProgressValue.current() < 1.) {
			_shownProgressValue = _showAnimation->progress();
		}
		update();
	});
	_showAnimation->setFinishedCallback([=] { slideFinished(); });
	_showAnimation->setPixmaps(oldContentCache, newContentCache);
	_showAnimation->start();
}

bool Widget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect Widget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

void Widget::slideFinished() {
	_showAnimation = nullptr;
	_shownProgressValue = 1.;
	updateControlsVisibility(true);
	if ((!_subsectionTopBar || !_subsectionTopBar->searchHasFocus())
		&& !_searchHasFocus) {
		controller()->widget()->setInnerFocus();
	}
}

void Widget::escape() {
	if (!cancelSearch()) {
		if (controller()->shownForum().current()) {
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
	} else if (!_searchState.inChat
		&& controller()->activeChatEntryCurrent().key) {
		controller()->content()->dialogsCancelled();
	}
}

void Widget::submit() {
	if (_suggestions) {
		_suggestions->chooseRow();
		return;
	} else if (_inner->chooseRow()) {
		return;
	}
	const auto state = _inner->state();
	if (state == WidgetState::Default
		|| (state == WidgetState::Filtered
			&& _inner->hasFilteredResults())) {
		_inner->selectSkip(1);
		_inner->chooseRow();
	} else {
		search();
	}
}

void Widget::refreshLoadMoreButton(bool mayBlock, bool isBlocked) {
	if (_layout == Layout::Child) {
		return;
	}

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

bool Widget::search(bool inCache) {
	_processingSearch = true;
	const auto guard = gsl::finally([&] {
		_processingSearch = false;
		listScrollUpdated();
	});

	auto result = false;
	const auto query = _searchState.query.trimmed();
	const auto trimmed = (query.isEmpty() || query[0] != '#')
		? query
		: query.mid(1).trimmed();
	const auto inPeer = searchInPeer();
	const auto fromPeer = searchFromPeer();
	const auto &inTags = searchInTags();
	const auto tab = _searchState.tab;
	const auto fromStartType = inPeer
		? SearchRequestType::PeerFromStart
		: SearchRequestType::FromStart;
	if (trimmed.isEmpty() && !fromPeer && inTags.empty()) {
		cancelSearchRequest();
		searchApplyEmpty(fromStartType, 0);
		_api.request(base::take(_peerSearchRequest)).cancel();
		_peerSearchQuery = QString();
		peerSearchApplyEmpty(0);
		_api.request(base::take(_topicSearchRequest)).cancel();
		return true;
	} else if (inCache) {
		const auto success = _singleMessageSearch.lookup(query, [=] {
			searchRequested();
		});
		if (!success) {
			return false;
		}
		const auto i = _searchCache.find(query);
		if (i != _searchCache.end()) {
			_searchQuery = query;
			_searchQueryFrom = fromPeer;
			_searchQueryTags = inTags;
			_searchQueryTab = tab;
			_searchNextRate = 0;
			_searchFull = _searchFullMigrated = false;
			cancelSearchRequest();
			searchReceived(fromStartType, i->second, 0);
			result = true;
		}
	} else if (_searchQuery != query
		|| _searchQueryFrom != fromPeer
		|| _searchQueryTags != inTags
		|| _searchQueryTab != tab) {
		_searchQuery = query;
		_searchQueryFrom = fromPeer;
		_searchQueryTags = inTags;
		_searchQueryTab = tab;
		_searchNextRate = 0;
		_searchFull = _searchFullMigrated = false;
		cancelSearchRequest();
		if (inPeer) {
			const auto topic = searchInTopic();
			auto &histories = session().data().histories();
			const auto type = Data::Histories::RequestType::History;
			const auto history = session().data().history(inPeer);
			const auto sublist = _openedForum
				? nullptr
				: _searchState.inChat.sublist();
			const auto fromPeer = sublist ? nullptr : _searchQueryFrom;
			const auto savedPeer = sublist
				? sublist->peer().get()
				: nullptr;
			_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
				const auto type = SearchRequestType::PeerFromStart;
				using Flag = MTPmessages_Search::Flag;
				_searchRequest = session().api().request(MTPmessages_Search(
					MTP_flags((topic ? Flag::f_top_msg_id : Flag())
						| (fromPeer ? Flag::f_from_id : Flag())
						| (savedPeer ? Flag::f_saved_peer_id : Flag())
						| (_searchQueryTags.empty()
							? Flag()
							: Flag::f_saved_reaction)),
					inPeer->input,
					MTP_string(_searchQuery),
					(fromPeer ? fromPeer->input : MTP_inputPeerEmpty()),
					(savedPeer ? savedPeer->input : MTP_inputPeerEmpty()),
					MTP_vector_from_range(
						_searchQueryTags | ranges::views::transform(
							Data::ReactionToMTP
						)),
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
		} else if (_searchState.tab == ChatSearchTab::PublicPosts) {
			const auto type = SearchRequestType::FromStart;
			_searchRequest = session().api().request(MTPchannels_SearchPosts(
				MTP_string(_searchState.query.trimmed().mid(1)),
				MTP_int(0), // offset_rate
				MTP_inputPeerEmpty(), // offset_peer
				MTP_int(0), // offset_id
				MTP_int(kSearchPerPage)
			)).done([=](const MTPmessages_Messages &result) {
				searchReceived(type, result, _searchRequest);
			}).fail([=](const MTP::Error &error) {
				searchFailed(type, error, _searchRequest);
			}).send();
			_searchQueries.emplace(_searchRequest, _searchQuery);
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
				MTP_int(0), // offset_rate
				MTP_inputPeerEmpty(), // offset_peer
				MTP_int(0), // offset_id
				MTP_int(kSearchPerPage)
			)).done([=](const MTPmessages_Messages &result) {
				searchReceived(type, result, _searchRequest);
			}).fail([=](const MTP::Error &error) {
				searchFailed(type, error, _searchRequest);
			}).send();
			_searchQueries.emplace(_searchRequest, _searchQuery);
		}
	}
	const auto peerQuery = Api::ConvertPeerSearchQuery(query);
	if (searchForPeersRequired(peerQuery)) {
		if (inCache) {
			auto i = _peerSearchCache.find(peerQuery);
			if (i != _peerSearchCache.end()) {
				_peerSearchQuery = peerQuery;
				_peerSearchRequest = 0;
				peerSearchReceived(i->second, 0);
				result = true;
			}
		} else if (_peerSearchQuery != peerQuery) {
			_peerSearchQuery = peerQuery;
			_peerSearchFull = false;
			_peerSearchRequest = _api.request(MTPcontacts_Search(
				MTP_string(_peerSearchQuery),
				MTP_int(SearchPeopleLimit)
			)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
				peerSearchReceived(result, requestId);
			}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
				peerSearchFailed(error, requestId);
			}).send();
			_peerSearchQueries.emplace(_peerSearchRequest, _peerSearchQuery);
		}
	} else {
		_api.request(base::take(_peerSearchRequest)).cancel();
		_peerSearchQuery = peerQuery;
		peerSearchApplyEmpty(0);
	}
	if (searchForTopicsRequired(peerQuery)) {
		if (inCache) {
			if (_topicSearchQuery != peerQuery) {
				result = false;
			}
		} else if (_topicSearchQuery != peerQuery) {
			_topicSearchQuery = peerQuery;
			_topicSearchFull = false;
			searchTopics();
		}
	} else {
		_api.request(base::take(_topicSearchRequest)).cancel();
		_topicSearchQuery = peerQuery;
		_topicSearchFull = true;
	}
	return result;
}

bool Widget::searchForPeersRequired(const QString &query) const {
	return _searchState.filterChatsList()
		&& !_openedForum
		&& !query.isEmpty()
		&& !IsHashtagSearchQuery(query);
}

bool Widget::searchForTopicsRequired(const QString &query) const {
	return _searchState.filterChatsList()
		&& _openedForum
		&& !query.isEmpty()
		&& !IsHashtagSearchQuery(query)
		&& !_openedForum->topicsList()->loaded();
}

void Widget::searchRequested() {
	if (!search(true)) {
		_searchTimer.callOnce(AutoSearchTimeout);
	}
}

void Widget::showMainMenu() {
	controller()->widget()->showMainMenu();
}

void Widget::searchMessages(SearchState state) {
	applySearchState(std::move(state));
	session().local().saveRecentSearchHashtags(_searchState.query);
}

void Widget::searchTopics() {
	if (_topicSearchRequest || _topicSearchFull) {
		return;
	}
	_api.request(base::take(_topicSearchRequest)).cancel();
	_topicSearchRequest = _api.request(MTPchannels_GetForumTopics(
		MTP_flags(MTPchannels_GetForumTopics::Flag::f_q),
		_openedForum->channel()->inputChannel,
		MTP_string(_topicSearchQuery),
		MTP_int(_topicSearchOffsetDate),
		MTP_int(_topicSearchOffsetId),
		MTP_int(_topicSearchOffsetTopicId),
		MTP_int(kSearchPerPage)
	)).done([=](const MTPmessages_ForumTopics &result) {
		_topicSearchRequest = 0;
		const auto savedTopicId = _topicSearchOffsetTopicId;
		const auto byCreation = result.data().is_order_by_create_date();
		_openedForum->applyReceivedTopics(result, [&](
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
			const auto sublist = _openedForum
				? nullptr
				: _searchState.inChat.sublist();
			const auto fromPeer = sublist ? nullptr : _searchQueryFrom;
			const auto savedPeer = sublist
				? sublist->peer().get()
				: nullptr;
			_searchInHistoryRequest = histories.sendRequest(history, type, [=](Fn<void()> finish) {
				const auto type = _lastSearchId
					? SearchRequestType::PeerFromOffset
					: SearchRequestType::PeerFromStart;
				using Flag = MTPmessages_Search::Flag;
				_searchRequest = session().api().request(MTPmessages_Search(
					MTP_flags((topic ? Flag::f_top_msg_id : Flag())
						| (fromPeer ? Flag::f_from_id : Flag())
						| (savedPeer ? Flag::f_saved_peer_id : Flag())
						| (_searchQueryTags.empty()
							? Flag()
							: Flag::f_saved_reaction)),
					peer->input,
					MTP_string(_searchQuery),
					(fromPeer ? fromPeer->input : MTP_inputPeerEmpty()),
					(savedPeer ? savedPeer->input : MTP_inputPeerEmpty()),
					MTP_vector_from_range(
						_searchQueryTags | ranges::views::transform(
							Data::ReactionToMTP
						)),
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
				MTPInputPeer(), // saved_peer_id
				MTPVector<MTPReaction>(), // saved_reaction
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
				channel->processTopics(data.vtopics());
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

void Widget::searchApplyEmpty(SearchRequestType type, mtpRequestId id) {
	_searchFull = _searchFullMigrated = true;
	searchReceived(
		type,
		MTP_messages_messages(
			MTP_vector<MTPMessage>(),
			MTP_vector<MTPChat>(),
			MTP_vector<MTPUser>()),
		id);
}

void Widget::peerSearchApplyEmpty(mtpRequestId id) {
	_peerSearchFull = true;
	peerSearchReceived(
		MTP_contacts_found(
			MTP_vector<MTPPeer>(0),
			MTP_vector<MTPPeer>(0),
			MTP_vector<MTPChat>(0),
			MTP_vector<MTPUser>(0)),
		id);
}

void Widget::searchFailed(
		SearchRequestType type,
		const MTP::Error &error,
		mtpRequestId requestId) {
	if (error.type() == u"SEARCH_QUERY_EMPTY"_q) {
		searchApplyEmpty(type, requestId);
	} else if (_searchRequest == requestId) {
		_searchRequest = 0;
		if (type == SearchRequestType::MigratedFromStart || type == SearchRequestType::MigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
	}
}

void Widget::peerSearchFailed(const MTP::Error &error, mtpRequestId id) {
	if (_peerSearchRequest == id) {
		_peerSearchRequest = 0;
		_peerSearchFull = true;
	}
}

void Widget::dragEnterEvent(QDragEnterEvent *e) {
	using namespace Storage;

	const auto data = e->mimeData();
	_dragInScroll = false;
	_dragForward = !controller()->adaptive().isOneColumn()
		&& data->hasFormat(u"application/x-td-forward"_q);
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
		const auto global = mapToGlobal(e->pos());
		const auto thread = _suggestions
			? _suggestions->updateFromParentDrag(global)
			: _inner->updateFromParentDrag(global);
		e->setDropAction(thread ? Qt::CopyAction : Qt::IgnoreAction);
	} else {
		if (_dragForward) {
			updateDragInScroll(false);
		}
		if (_suggestions) {
			_suggestions->dragLeft();
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
	if (_suggestions) {
		_suggestions->dragLeft();
	}
	_inner->dragLeft();
	e->accept();
}

void Widget::updateDragInScroll(bool inScroll) {
	if (_dragInScroll != inScroll) {
		_dragInScroll = inScroll;
		if (_dragInScroll) {
			controller()->content()->showDragForwardInfo();
		} else {
			controller()->content()->dialogsCancelled();
		}
	}
}

void Widget::dropEvent(QDropEvent *e) {
	_chooseByDragTimer.cancel();
	if (_scroll->geometry().contains(e->pos())) {
		const auto globalPosition = mapToGlobal(e->pos());
		const auto thread = _suggestions
			? _suggestions->updateFromParentDrag(globalPosition)
			: _inner->updateFromParentDrag(globalPosition);
		if (thread) {
			e->setDropAction(Qt::CopyAction);
			e->accept();
			controller()->content()->filesOrForwardDrop(
				thread,
				e->mimeData());
			if (!thread->owningHistory()->isForum()) {
				hideChildList();
			}
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

void Widget::updateCancelSearch() {
	const auto shown = !_searchState.query.isEmpty()
		|| (!_searchState.inChat
			&& (_searchHasFocus || _searchSuggestionsLocked));
	_cancelSearch->toggle(shown, anim::type::normal);
}

QString Widget::validateSearchQuery() {
	const auto query = currentSearchQuery();
	if (_searchState.tab == ChatSearchTab::PublicPosts) {
		_searchingHashtag = true;
		const auto fixed = FixHashtagSearchQuery(
			query,
			currentSearchQueryCursorPosition());
		if (fixed.text != query) {
			setSearchQuery(fixed.text, fixed.cursorPosition);
		}
		return fixed.text;
	} else {
		_searchingHashtag = IsHashtagSearchQuery(query);
	}
	return query;
}

void Widget::applySearchUpdate() {
	auto copy = _searchState;
	copy.query = validateSearchQuery();
	applySearchState(std::move(copy));

	if (_chooseFromUser->toggled()
		|| _searchState.fromPeer
		|| !_searchState.tags.empty()) {
		auto switchToChooseFrom = HistoryView::SwitchToChooseFromQuery();
		if (_lastSearchText != switchToChooseFrom
			&& switchToChooseFrom.startsWith(_lastSearchText)
			&& _searchState.query == switchToChooseFrom) {
			showSearchFrom();
		}
	}
	_lastSearchText = _searchState.query;
}

void Widget::updateForceDisplayWide() {
	if (_childList) {
		_childList->updateForceDisplayWide();
		return;
	}
	controller()->setChatsForceDisplayWide(_searchHasFocus
		|| (_subsectionTopBar && _subsectionTopBar->searchHasFocus())
		|| _searchSuggestionsLocked
		|| !_searchState.query.isEmpty()
		|| _searchState.inChat);
}

void Widget::showForum(
		not_null<Data::Forum*> forum,
		const Window::SectionShow &params) {
	const auto nochat = !controller()->mainSectionShown();
	if (!params.childColumn
		|| (Core::App().settings().dialogsWidthRatio(nochat) == 0.)
		|| (_layout != Layout::Main)
		|| OptionForumHideChatsList.value()) {
		changeOpenedForum(forum, params.animated);
		return;
	}
	cancelSearch(true);
	openChildList(forum, params);
}

void Widget::openChildList(
		not_null<Data::Forum*> forum,
		const Window::SectionShow &params) {
	auto slide = Window::SectionSlideParams();
	const auto animated = !_childList
		&& (params.animated == anim::type::normal);
	if (animated) {
		destroyChildListCanvas();
		slide.oldContentCache = Ui::GrabWidget(
			this,
			QRect(_narrowWidth, 0, width() - _narrowWidth, height()));
	}
	auto copy = params;
	copy.childColumn = false;
	copy.animated = anim::type::instant;
	{
		if (_childList && InFocusChain(_childList.get())) {
			setFocus();
		}
		_childList = std::make_unique<Widget>(
			this,
			controller(),
			Layout::Child);
		_childList->showForum(forum, copy);
		_childListPeerId = forum->channel()->id;
	}

	_childListShadow = std::make_unique<Ui::RpWidget>(this);
	const auto shadow = _childListShadow.get();
	const auto opacity = shadow->lifetime().make_state<float64>(0.);
	shadow->setAttribute(Qt::WA_TransparentForMouseEvents);
	shadow->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(shadow);
		p.setOpacity(*opacity);
		p.fillRect(clip, st::shadowFg);
	}, shadow->lifetime());
	_childListShown.value() | rpl::start_with_next([=](float64 value) {
		*opacity = value;
		update();
		_inner->update();
		_search->setVisible(value < 1.);
		if (!value && _childListShadow.get() != shadow) {
			delete shadow;
		}
	}, shadow->lifetime());

	updateControlsGeometry();
	updateControlsVisibility(true);

	if (animated) {
		_childList->showAnimated(Window::SlideDirection::FromRight, slide);
		_childListShown = _childList->shownProgressValue();
	} else {
		_childListShown = 1.;
	}
	if (hasFocus()) {
		setInnerFocus();
	}
	updateForceDisplayWide();
}

void Widget::closeChildList(anim::type animated) {
	if (!_childList) {
		return;
	}
	const auto geometry = _childList->geometry();
	const auto shown = _childListShown.current();
	auto oldContentCache = QPixmap();
	auto animation = (Window::SlideAnimation*)nullptr;
	if (animated == anim::type::normal) {
		oldContentCache = Ui::GrabWidget(_childList.get());
		_hideChildListCanvas = std::make_unique<Ui::RpWidget>(this);
		_hideChildListCanvas->setAttribute(Qt::WA_TransparentForMouseEvents);
		_hideChildListCanvas->setGeometry(geometry);
		animation = _hideChildListCanvas->lifetime().make_state<
			Window::SlideAnimation
		>();
		_hideChildListCanvas->paintRequest(
		) | rpl::start_with_next([=] {
			QPainter p(_hideChildListCanvas.get());
			animation->paintContents(p);
		}, _hideChildListCanvas->lifetime());
	}
	if (InFocusChain(_childList.get())) {
		setFocus();
	}
	_childList = nullptr;
	_childListShown = 0.;
	if (hasFocus()) {
		setInnerFocus();
		_search->finishAnimating();
	}
	if (animated == anim::type::normal) {
		_hideChildListCanvas->hide();
		auto newContentCache = Ui::GrabWidget(this, geometry);
		_hideChildListCanvas->show();

		_childListShown = shown;
		_childListShadow.release();

		animation->setDirection(Window::SlideDirection::FromLeft);
		animation->setRepaintCallback([=] {
			_childListShown = (1. - animation->progress()) * shown;
			_hideChildListCanvas->update();
		});
		animation->setFinishedCallback([=] {
			destroyChildListCanvas();
		});
		animation->setPixmaps(oldContentCache, newContentCache);
		animation->start();
	} else {
		_childListShadow = nullptr;
	}
	updateStoriesVisibility();
	updateForceDisplayWide();
}

bool Widget::applySearchState(SearchState state) {
	if (_searchState == state) {
		return true;
	} else if (_childList) {
		if (_childList->applySearchState(state)) {
			return true;
		}
		hideChildList();
	}

	// Adjust state to be consistent.
	if (const auto peer = state.inChat.peer()) {
		if (const auto to = peer->migrateTo()) {
			state.inChat = peer->owner().history(to);
		}
	}
	const auto peer = state.inChat.peer();
	const auto topic = state.inChat.topic();
	const auto forum = peer ? peer->forum() : nullptr;
	if (state.inChat.folder() || (forum && !topic)) {
		state.inChat = {};
	}
	if (!state.inChat && !forum && !_openedForum) {
		state.fromPeer = nullptr;
	}
	if (state.tab == ChatSearchTab::PublicPosts
		&& !IsHashtagSearchQuery(state.query)) {
		state.tab = (_openedForum && !state.inChat)
			? ChatSearchTab::ThisPeer
			: ChatSearchTab::MyMessages;
	} else if (!state.inChat && !_searchingHashtag) {
		state.tab = (forum || _openedForum)
			? ChatSearchTab::ThisPeer
			: ChatSearchTab::MyMessages;
	}
	if (!state.tags.empty()) {
		state.inChat = session().data().history(session().user());
	}

	const auto clearQuery = state.fromPeer
		&& (_lastSearchText == HistoryView::SwitchToChooseFromQuery());
	if (clearQuery) {
		state.query = _lastSearchText = QString();
	}

	const auto inChatChanged = (_searchState.inChat != state.inChat);
	const auto fromPeerChanged = (_searchState.fromPeer != state.fromPeer);
	const auto tagsChanged = (_searchState.tags != state.tags);
	const auto queryChanged = (_searchState.query != state.query);
	const auto tabChanged = (_searchState.tab != state.tab);

	if (forum) {
		if (_openedForum == forum) {
			showSearchInTopBar(anim::type::normal);
		} else if (_layout == Layout::Main) {
			_forumSearchRequested = true;
			controller()->showForum(forum);
		} else {
			return false;
		}
	} else if (peer && (_layout != Layout::Main)) {
		return false;
	}

	if ((state.tab == ChatSearchTab::ThisTopic
		&& !state.inChat.topic())
		|| (state.tab == ChatSearchTab::ThisPeer
			&& !state.inChat
			&& !_openedForum)
		|| (state.tab == ChatSearchTab::PublicPosts
			&& !_searchingHashtag)) {
		state.tab = state.inChat.topic()
			? ChatSearchTab::ThisTopic
			: (state.inChat.owningHistory() || state.inChat.sublist())
			? ChatSearchTab::ThisPeer
			: ChatSearchTab::MyMessages;
	}

	const auto migrateFrom = (peer && !topic)
		? peer->migrateFrom()
		: nullptr;
	_searchInMigrated = migrateFrom
		? peer->owner().history(migrateFrom).get()
		: nullptr;
	_searchState = state;
	if (queryChanged) {
		updateLockUnlockVisibility(anim::type::normal);
		updateLoadMoreChatsVisibility();
	}
	if (inChatChanged) {
		controller()->setSearchInChat(_searchState.inChat);
	}
	if (queryChanged || inChatChanged) {
		updateCancelSearch();
		updateStoriesVisibility();
	}
	updateJumpToDateVisibility();
	updateSearchFromVisibility();
	updateLockUnlockPosition();

	if ((state.query.isEmpty() && !state.fromPeer && state.tags.empty())
		|| inChatChanged
		|| fromPeerChanged
		|| tagsChanged
		|| tabChanged) {
		clearSearchCache();
	}
	if (state.query.isEmpty()) {
		_peerSearchCache.clear();
		for (const auto &[requestId, query] : base::take(_peerSearchQueries)) {
			_api.request(requestId).cancel();
		}
		_peerSearchQuery = QString();
	}

	if (_searchState.inChat && _layout == Layout::Main) {
		controller()->closeFolder();
	}

	if (_searchState.query != currentSearchQuery()) {
		setSearchQuery(_searchState.query);
	}
	_inner->applySearchState(_searchState);

	if (!_postponeProcessSearchFocusChange) {
		// Suggestions depend on _inner->state(), not on _searchState.
		updateSuggestions(anim::type::instant);
	}

	_searchTagsLifetime = _inner->searchTagsChanges(
	) | rpl::start_with_next([=](std::vector<Data::ReactionId> &&list) {
		auto copy = _searchState;
		copy.tags = std::move(list);
		applySearchState(std::move(copy));
	});
	if (_subsectionTopBar) {
		_subsectionTopBar->searchEnableJumpToDate(
			_openedForum && _searchState.inChat);
	}
	if (!_searchState.inChat && _searchState.query.isEmpty()) {
		setInnerFocus();
	} else if (!_subsectionTopBar) {
		_search->setFocus();
	} else if (_openedForum && !_subsectionTopBar->searchSetFocus()) {
		_subsectionTopBar->toggleSearch(true, anim::type::normal);
	}
	updateForceDisplayWide();
	applySearchUpdate();
	return true;
}

void Widget::clearSearchCache() {
	_searchCache.clear();
	_singleMessageSearch.clear();
	for (const auto &[requestId, query] : base::take(_searchQueries)) {
		session().api().request(requestId).cancel();
	}
	_searchQuery = QString();
	_searchQueryFrom = nullptr;
	_searchQueryTags.clear();
	_topicSearchQuery = QString();
	_topicSearchOffsetDate = 0;
	_topicSearchOffsetId = _topicSearchOffsetTopicId = 0;
	_api.request(base::take(_peerSearchRequest)).cancel();
	_api.request(base::take(_topicSearchRequest)).cancel();
	cancelSearchRequest();
}

void Widget::showCalendar() {
	if (_searchState.inChat) {
		controller()->showCalendar(_searchState.inChat, QDate());
	}
}

void Widget::showSearchFrom() {
	if (const auto peer = searchInPeer()) {
		const auto weak = base::make_weak(_searchState.inChat.topic());
		const auto chat = (!_searchState.inChat && _openedForum)
			? Key(_openedForum->history())
			: _searchState.inChat;
		auto box = SearchFromBox(
			peer,
			crl::guard(this, [=](not_null<PeerData*> from) {
				controller()->hideLayer();
				auto copy = _searchState;
				if (!chat.topic()) {
					copy.inChat = chat;
					copy.fromPeer = from;
					applySearchState(std::move(copy));
				} else if (const auto strong = weak.get()) {
					copy.inChat = strong;
					copy.fromPeer = from;
					applySearchState(std::move(copy));
				}
			}),
			crl::guard(this, [=] { _search->setFocus(); }));
		if (box) {
			controller()->show(std::move(box));
		}
	}
}

void Widget::searchCursorMoved() {
	const auto to = _search->textCursor().position();
	const auto text = _search->getLastText();
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
	const auto t = _search->getLastText();
	auto cur = _search->textCursor().position();
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
				setSearchQuery(hashtag, start + 1 + tag.size() + 1);
				applySearchUpdate();
				return;
			}
			break;
		} else if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') {
			break;
		}
	}
	setSearchQuery(
		t.mid(0, cur) + '#' + tag + ' ' + t.mid(cur),
		cur + 1 + tag.size() + 1);
	applySearchUpdate();
}

void Widget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Widget::updateLockUnlockVisibility(anim::type animated) {
	if (_showAnimation) {
		return;
	}
	const auto hidden = !session().domain().local().hasLocalPasscode()
		|| _showAnimation
		|| _openedForum
		|| !_widthAnimationCache.isNull()
		|| _childList
		|| _searchHasFocus
		|| _searchSuggestionsLocked
		|| _searchState.inChat
		|| !_searchState.query.isEmpty();
	if (_lockUnlock->toggled() == hidden) {
		const auto stories = _stories && !_stories->empty();
		_lockUnlock->toggle(
			!hidden,
			stories ? anim::type::instant : animated);
		if (!hidden) {
			updateLockUnlockPosition();
		}
		updateControlsGeometry();
	}
}

void Widget::updateLoadMoreChatsVisibility() {
	if (_showAnimation || !_loadMoreChats) {
		return;
	}
	const auto hidden = (_openedFolder != nullptr)
		|| (_openedForum != nullptr)
		|| !_searchState.query.isEmpty();
	if (_loadMoreChats->isHidden() != hidden) {
		_loadMoreChats->setVisible(!hidden);
		updateControlsGeometry();
	}
}

void Widget::updateJumpToDateVisibility(bool fast) {
	if (_showAnimation) {
		return;
	}

	_jumpToDate->toggle(
		(searchInPeer() && _searchState.query.isEmpty()),
		fast ? anim::type::instant : anim::type::normal);
}

void Widget::updateSearchFromVisibility(bool fast) {
	auto visible = [&] {
		if (const auto peer = searchInPeer()) {
			if (peer->isChat() || peer->isMegagroup()) {
				return !_searchState.fromPeer;
			}
		}
		return false;
	}();
	const auto changed = (visible == !_chooseFromUser->toggled());
	_chooseFromUser->toggle(
		visible,
		fast ? anim::type::instant : anim::type::normal);
	if (_subsectionTopBar) {
		_subsectionTopBar->searchEnableChooseFromUser(true, visible);
	} else if (changed) {
		auto additional = QMargins();
		if (visible) {
			additional.setRight(_chooseFromUser->width());
		}
		_search->setAdditionalMargins(additional);
	}
}

void Widget::updateControlsGeometry() {
	if (width() < _narrowWidth) {
		return;
	}
	auto filterAreaTop = 0;

	const auto ratiow = anim::interpolate(
		width(),
		_narrowWidth,
		_childListShown.current());
	const auto smallw = st::columnMinimalWidthLeft - _narrowWidth;
	const auto narrowRatio = (ratiow < smallw)
		? ((smallw - ratiow) / float64(smallw - _narrowWidth))
		: 0.;

	auto filterLeft = (controller()->filtersWidth()
		? st::dialogsFilterSkip
		: (st::dialogsFilterPadding.x() + _mainMenu.toggle->width()))
		+ st::dialogsFilterPadding.x();
	const auto filterRight = st::dialogsFilterSkip + st::dialogsFilterPadding.x();
	const auto filterWidth = qMax(ratiow, smallw) - filterLeft - filterRight;
	const auto filterAreaHeight = st::topBarHeight;
	_searchControls->setGeometry(0, filterAreaTop, ratiow, filterAreaHeight);
	if (_subsectionTopBar) {
		_subsectionTopBar->setGeometryWithNarrowRatio(
			_searchControls->geometry(),
			_narrowWidth,
			narrowRatio);
	}

	auto filterTop = (filterAreaHeight - _search->height()) / 2;
	filterLeft = anim::interpolate(filterLeft, _narrowWidth, narrowRatio);
	_search->setGeometryToLeft(filterLeft, filterTop, filterWidth, _search->height());

	auto mainMenuLeft = anim::interpolate(
		st::dialogsFilterPadding.x(),
		(_narrowWidth - _mainMenu.toggle->width()) / 2,
		narrowRatio);
	_mainMenu.toggle->moveToLeft(mainMenuLeft, st::dialogsFilterPadding.y());
	_mainMenu.under->setGeometry(
		0,
		0,
		filterLeft,
		_mainMenu.toggle->y()
			+ _mainMenu.toggle->height()
			+ st::dialogsFilterPadding.y());
	const auto searchLeft = anim::interpolate(
		-_searchForNarrowLayout->width(),
		(_narrowWidth - _searchForNarrowLayout->width()) / 2,
		narrowRatio);
	_searchForNarrowLayout->moveToLeft(searchLeft, st::dialogsFilterPadding.y());

	auto right = filterLeft + filterWidth;
	_cancelSearch->moveToLeft(right - _cancelSearch->width(), _search->y());
	right -= _jumpToDate->width(); _jumpToDate->moveToLeft(right, _search->y());
	right -= _chooseFromUser->width(); _chooseFromUser->moveToLeft(right, _search->y());

	const auto barw = width();
	const auto expandedStoriesTop = filterAreaTop + filterAreaHeight;
	const auto storiesHeight = 2 * st::dialogsStories.photoTop
		+ st::dialogsStories.photo;
	const auto added = (st::dialogsFilter.heightMin - storiesHeight) / 2;
	if (_stories) {
		_stories->setLayoutConstraints(
			{ filterLeft + filterWidth, filterTop + added },
			style::al_right,
			{ 0, expandedStoriesTop, barw, st::dialogsStoriesFull.height });
	}
	if (_forumTopShadow) {
		_forumTopShadow->setGeometry(
			0,
			expandedStoriesTop,
			barw,
			st::lineWidth);
	}

	updateLockUnlockPosition();

	auto bottomSkip = 0;
	const auto putBottomButton = [&](auto &button) {
		if (button && !button->isHidden()) {
			const auto buttonHeight = button->height();
			bottomSkip += buttonHeight;
			button->setGeometry(
				0,
				height() - bottomSkip,
				barw,
				buttonHeight);
		}
	};
	putBottomButton(_updateTelegram);
	putBottomButton(_downloadBar);
	putBottomButton(_loadMoreChats);
	if (_connecting) {
		_connecting->setBottomSkip(bottomSkip);
	}
	if (_layout != Layout::Child) {
		controller()->setConnectingBottomSkip(bottomSkip);
	}

	const auto wasScrollTop = _scroll->scrollTop();
	const auto newScrollTop = (_topDelta < 0 && wasScrollTop <= 0)
		? wasScrollTop
		: (wasScrollTop + _topDelta);

	const auto scrollWidth = _childList ? _narrowWidth : barw;
	if (_moreChatsBar) {
		_moreChatsBar->resizeToWidth(barw);
	}
	if (_forumGroupCallBar) {
		_forumGroupCallBar->resizeToWidth(barw);
	}
	if (_forumRequestsBar) {
		_forumRequestsBar->resizeToWidth(barw);
	}
	_updateScrollGeometryCached = [=] {
		const auto moreChatsBarTop = expandedStoriesTop
			+ ((!_stories || _stories->isHidden()) ? 0 : _aboveScrollAdded);
		if (_moreChatsBar) {
			_moreChatsBar->move(0, moreChatsBarTop);
		}
		const auto forumGroupCallTop = moreChatsBarTop
			+ (_moreChatsBar ? _moreChatsBar->height() : 0);
		if (_forumGroupCallBar) {
			_forumGroupCallBar->move(0, forumGroupCallTop);
		}
		const auto forumRequestsTop = forumGroupCallTop
			+ (_forumGroupCallBar ? _forumGroupCallBar->height() : 0);
		if (_forumRequestsBar) {
			_forumRequestsBar->move(0, forumRequestsTop);
		}
		const auto forumReportTop = forumRequestsTop
			+ (_forumRequestsBar ? _forumRequestsBar->height() : 0);
		if (_forumReportBar) {
			_forumReportBar->bar().move(0, forumReportTop);
		}
		const auto scrollTop = forumReportTop
			+ (_forumReportBar ? _forumReportBar->bar().height() : 0);
		const auto scrollHeight = height() - scrollTop - bottomSkip;
		const auto wasScrollHeight = _scroll->height();
		_scroll->setGeometry(0, scrollTop, scrollWidth, scrollHeight);
		if (scrollHeight != wasScrollHeight) {
			controller()->floatPlayerAreaUpdated();
		}
	};
	_updateScrollGeometryCached();

	if (_suggestions) {
		_suggestions->setGeometry(
			0,
			expandedStoriesTop,
			scrollWidth,
			height() - expandedStoriesTop - bottomSkip);
	}

	_inner->resize(scrollWidth, _inner->height());
	_inner->setNarrowRatio(narrowRatio);
	if (newScrollTop != wasScrollTop) {
		_scroll->scrollToY(newScrollTop);
	} else {
		listScrollUpdated();
	}
	if (_scrollToTopIsShown) {
		updateScrollUpPosition();
	}

	if (_childList) {
		const auto childw = std::max(_narrowWidth, width() - scrollWidth);
		const auto childh = _scroll->y() + _scroll->height();
		const auto childx = width() - childw;
		_childList->setGeometryWithTopMoved(
			{ childx, 0, childw, childh },
			_topDelta);
		const auto line = st::lineWidth;
		_childListShadow->setGeometry(childx - line, 0, line, childh);
	}
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
	} else if ((e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Tab)
		&& _searchHasFocus
		&& !_searchState.inChat
		&& _searchState.query.isEmpty()) {
		escape();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		submit();
	} else if (_suggestions
		&& (e->key() == Qt::Key_Down
			|| e->key() == Qt::Key_Up
			|| e->key() == Qt::Key_Left
			|| e->key() == Qt::Key_Right)) {
		_suggestions->selectJump(Qt::Key(e->key()));
	} else if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		if (_suggestions) {
			_suggestions->selectJump(Qt::Key_Down, _scroll->height());
		} else {
			_inner->selectSkipPage(_scroll->height(), 1);
		}
	} else if (e->key() == Qt::Key_PageUp) {
		if (_suggestions) {
			_suggestions->selectJump(Qt::Key_Up, _scroll->height());
		} else {
			_inner->selectSkipPage(_scroll->height(), -1);
		}
	} else if (redirectKeyToSearch(e)) {
		// This delay in search focus processing allows us not to create
		// _suggestions in case the event inserts some non-whitespace search
		// query while still show _suggestions animated, if it is a space.
		_postponeProcessSearchFocusChange = true;
		_search->setFocusFast();
		if (e->key() != Qt::Key_Space) {
			QCoreApplication::sendEvent(_search->rawTextEdit(), e);
		}
		_postponeProcessSearchFocusChange = false;
		processSearchFocusChange();
	} else {
		e->ignore();
	}
}

void Widget::inputMethodEvent(QInputMethodEvent *e) {
	const auto cursor = _search->rawTextEdit()->textCursor();
	bool isGettingInput = !e->commitString().isEmpty()
		|| e->preeditString() != cursor.block().layout()->preeditAreaText()
		|| e->replacementLength() > 0;

	if (!isGettingInput || _postponeProcessSearchFocusChange) {
		Window::AbstractSectionWidget::inputMethodEvent(e);
		return;
	}

	// This delay in search focus processing allows us not to create
	// _suggestions in case the event inserts some non-whitespace search
	// query while still show _suggestions animated, if it is a space.
	_postponeProcessSearchFocusChange = true;
	_search->setFocusFast();
	QCoreApplication::sendEvent(_search->rawTextEdit(), e);
	_postponeProcessSearchFocusChange = false;
	processSearchFocusChange();
}

QVariant Widget::inputMethodQuery(Qt::InputMethodQuery query) const {
	return _search->rawTextEdit()->inputMethodQuery(query);
}

bool Widget::redirectToSearchPossible() const {
	return !_openedFolder
		&& !_openedForum
		&& !_childList
		&& _search->isVisible()
		&& !_search->hasFocus()
		&& hasFocus();
}

bool Widget::redirectKeyToSearch(QKeyEvent *e) const {
	if (!redirectToSearchPossible()) {
		return false;
	}
	const auto character = !(e->modifiers() & ~Qt::ShiftModifier)
		&& (e->key() != Qt::Key_Shift)
		&& RedirectTextToSearch(e->text());
	if (character) {
		return true;
	} else if (e != QKeySequence::Paste) {
		return false;
	}
	const auto useSelectionMode = (e->key() == Qt::Key_Insert)
		&& (e->modifiers() == (Qt::CTRL | Qt::SHIFT))
		&& QGuiApplication::clipboard()->supportsSelection();
	const auto pasteMode = useSelectionMode
		? QClipboard::Selection
		: QClipboard::Clipboard;
	const auto data = QGuiApplication::clipboard()->mimeData(pasteMode);
	return data && data->hasText();
}

bool Widget::redirectImeToSearch() const {
	return redirectToSearchPossible();
}

void Widget::paintEvent(QPaintEvent *e) {
	if (controller()->contentOverlapped(this, e)) {
		return;
	}

	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	if (_showAnimation) {
		_showAnimation->paintContents(p);
		return;
	}
	const auto bg = anim::brush(
		st::dialogsBg,
		st::dialogsBgOver,
		_childListShown.current());
	auto above = QRect(0, 0, width(), _scroll->y());
	if (above.intersects(r)) {
		p.fillRect(above.intersected(r), bg);
	}

	auto belowTop = _scroll->y() + _scroll->height();
	if (!_widthAnimationCache.isNull()) {
		const auto suggestionsShown = _suggestions
			? _suggestions->shownOpacity()
			: !_hidingSuggestions.empty()
			? _hidingSuggestions.back()->shownOpacity()
			: 0.;
		const auto suggestionsSkip = suggestionsShown
			* (st::topPeers.height + st::searchedBarHeight);
		const auto top = _searchControls->y()
			+ _searchControls->height()
			+ suggestionsSkip;
		p.drawPixmapLeft(0, top, width(), _widthAnimationCache);
		belowTop = top
			+ (_widthAnimationCache.height() / style::DevicePixelRatio());
	}

	auto below = QRect(0, belowTop, width(), height() - belowTop);
	if (below.intersects(r)) {
		p.fillRect(below.intersected(r), bg);
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
	return (_searchState.tab == ChatSearchTab::MyMessages
		|| _searchState.tab == ChatSearchTab::PublicPosts)
		? nullptr
		: _openedForum
		? _openedForum->channel().get()
		: _searchState.inChat.sublist()
		? session().user().get()
		: _searchState.inChat.peer();
}

Data::ForumTopic *Widget::searchInTopic() const {
	return (_searchState.tab != ChatSearchTab::ThisTopic)
		? nullptr
		: _searchState.inChat.topic();
}

PeerData *Widget::searchFromPeer() const {
	if (const auto peer = searchInPeer()) {
		if (peer->isChat() || peer->isMegagroup()) {
			return _searchState.fromPeer;
		}
	}
	return nullptr;
}

const std::vector<Data::ReactionId> &Widget::searchInTags() const {
	if (const auto peer = searchInPeer()) {
		if (peer->isSelf() && _searchState.tab == ChatSearchTab::ThisPeer) {
			return _searchState.tags;
		}
	}
	static const auto kEmpty = std::vector<Data::ReactionId>();
	return kEmpty;
}

QString Widget::currentSearchQuery() const {
	return _subsectionTopBar
		? _subsectionTopBar->searchQueryCurrent()
		: _search->getLastText();
}

int Widget::currentSearchQueryCursorPosition() const {
	return _subsectionTopBar
		? _subsectionTopBar->searchQueryCursorPosition()
		: _search->textCursor().position();
}

void Widget::clearSearchField() {
	if (_subsectionTopBar) {
		_subsectionTopBar->searchClear();
	} else {
		_search->clear();
	}
}

void Widget::setSearchQuery(const QString &query, int cursorPosition) {
	if (query.isEmpty()) {
		clearSearchField();
		return;
	}
	if (cursorPosition < 0) {
		cursorPosition = query.size();
	}
	if (_subsectionTopBar) {
		_subsectionTopBar->searchSetText(query, cursorPosition);
	} else {
		_search->setText(query);
		_search->setCursorPosition(cursorPosition);
	}
}

bool Widget::cancelSearch(bool forceFullCancel) {
	cancelSearchRequest();
	auto updatedState = _searchState;
	const auto clearingQuery = !updatedState.query.isEmpty();
	auto clearingInChat = (forceFullCancel || !clearingQuery)
		&& (updatedState.inChat
			|| updatedState.fromPeer
			|| !updatedState.tags.empty());
	if (clearingQuery) {
		updatedState.query = QString();
	}
	if (clearingInChat) {
		if (updatedState.inChat && controller()->adaptive().isOneColumn()) {
			if (const auto thread = updatedState.inChat.thread()) {
				controller()->showThread(thread);
			} else {
				Unexpected("Empty key in cancelSearch().");
			}
		}
		updatedState.inChat = {};
		updatedState.fromPeer = nullptr;
		updatedState.tags = {};
	}
	if (!clearingQuery
		&& _subsectionTopBar
		&& _subsectionTopBar->toggleSearch(false, anim::type::normal)) {
		setInnerFocus(true);
		clearingInChat = true;
	}
	const auto clearSearchFocus = (forceFullCancel || !updatedState.inChat)
		&& (_searchHasFocus || _searchSuggestionsLocked);
	if (!updatedState.inChat && _suggestions) {
		_suggestions->clearPersistance();
		_searchSuggestionsLocked = false;
	}
	if (!_suggestions && clearSearchFocus) {
		// Don't create suggestions in unfocus case.
		setInnerFocus(true);
	}
	_lastSearchPeer = nullptr;
	_lastSearchId = _lastSearchMigratedId = 0;
	_inner->clearFilter();
	applySearchState(std::move(updatedState));
	if (_suggestions && clearSearchFocus) {
		setInnerFocus(true);
	}
	updateForceDisplayWide();
	return clearingQuery || clearingInChat || clearSearchFocus;
}

Widget::~Widget() {
	cancelSearchRequest();

	// Destructor may hide the bar and attempt to double-destroy it.
	base::take(_downloadBar);
}

} // namespace Dialogs
