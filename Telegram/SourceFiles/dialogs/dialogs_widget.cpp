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
#include "history/feed/history_feed_section.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/radial_animation.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/update_checker.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "messenger.h"
#include "boxes/peer_list_box.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_connecting_widget.h"
#include "profile/profile_channel_controllers.h"
#include "storage/storage_media_prepare.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

namespace {

QString SwitchToChooseFromQuery() {
	return qsl("from:");
}

} // namespace

class DialogsWidget::BottomButton : public Ui::RippleButton {
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
	void step_radial(TimeMs ms, bool timer);

	QString _text;
	const style::FlatButton &_st;
	const style::icon &_icon;
	const style::icon &_iconOver;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _loading;

};

DialogsWidget::BottomButton::BottomButton(
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

void DialogsWidget::BottomButton::setText(const QString &text) {
	_text = text.toUpper();
	update();
}

void DialogsWidget::BottomButton::step_radial(TimeMs ms, bool timer) {
	if (timer && !anim::Disabled() && width() < st::columnMinimalWidthLeft) {
		update();
	}
}

void DialogsWidget::BottomButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if ((was & StateFlag::Disabled) != (state() & StateFlag::Disabled)) {
		_loading = isDisabled()
			? std::make_unique<Ui::InfiniteRadialAnimation>(
				animation(this, &BottomButton::step_radial),
				st::dialogsLoadMoreLoading)
			: nullptr;
		if (_loading) {
			_loading->start();
		}
	}
	update();
}

void DialogsWidget::BottomButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto over = isOver() && !isDisabled();

	QRect r(0, height() - _st.height, width(), _st.height);
	p.fillRect(r, over ? _st.overBgColor : _st.bgColor);

	if (!isDisabled()) {
		paintRipple(p, 0, 0, getms());
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

DialogsWidget::DialogsWidget(QWidget *parent, not_null<Window::Controller*> controller) : Window::AbstractSectionWidget(parent, controller)
, _mainMenuToggle(this, st::dialogsMenuToggle)
, _filter(this, st::dialogsFilter, langFactory(lng_dlg_filter))
, _chooseFromUser(
	this,
	object_ptr<Ui::IconButton>(this, st::dialogsSearchFrom))
, _jumpToDate(
	this,
	object_ptr<Ui::IconButton>(this, st::dialogsCalendar))
, _cancelSearch(this, st::dialogsCancelSearch)
, _lockUnlock(this, st::dialogsLock)
, _scroll(this, st::dialogsScroll) {
	_inner = _scroll->setOwnedWidget(object_ptr<DialogsInner>(this, controller, parent));
	connect(_inner, SIGNAL(draggingScrollDelta(int)), this, SLOT(onDraggingScrollDelta(int)));
	connect(_inner, SIGNAL(mustScrollTo(int,int)), _scroll, SLOT(scrollToY(int,int)));
	connect(_inner, SIGNAL(dialogMoved(int,int)), this, SLOT(onDialogMoved(int,int)));
	connect(_inner, SIGNAL(searchMessages()), this, SLOT(onNeedSearchMessages()));
	connect(_inner, SIGNAL(clearSearchQuery()), this, SLOT(onCancel()));
	connect(_inner, SIGNAL(completeHashtag(QString)), this, SLOT(onCompleteHashtag(QString)));
	connect(_inner, SIGNAL(refreshHashtags()), this, SLOT(onFilterCursorMoved()));
	connect(_inner, SIGNAL(cancelSearchInChat()), this, SLOT(onCancelSearchInChat()));
	subscribe(_inner->searchFromUserChanged, [this](UserData *user) {
		setSearchInChat(_searchInChat, user);
		onFilterUpdate(true);
	});
	connect(_scroll, SIGNAL(geometryChanged()), _inner, SLOT(onParentGeometryChanged()));
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(_filter, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(_filter, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onFilterCursorMoved(int,int)));

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

	subscribe(Adaptive::Changed(), [this] { updateForwardBar(); });

	_cancelSearch->setClickedCallback([this] { onCancelSearch(); });
	_jumpToDate->entity()->setClickedCallback([this] { showJumpToDate(); });
	_chooseFromUser->entity()->setClickedCallback([this] { showSearchFrom(); });
	_lockUnlock->setVisible(Global::LocalPasscode());
	subscribe(Global::RefLocalPasscodeChanged(), [this] { updateLockUnlockVisibility(); });
	_lockUnlock->setClickedCallback([this] {
		_lockUnlock->setIconOverride(&st::dialogsUnlockIcon, &st::dialogsUnlockIconOver);
		Messenger::Instance().lockByPasscode();
		_lockUnlock->setIconOverride(nullptr);
	});
	_mainMenuToggle->setClickedCallback([this] { showMainMenu(); });

	_chooseByDragTimer.setSingleShot(true);
	connect(&_chooseByDragTimer, SIGNAL(timeout()), this, SLOT(onChooseByDrag()));

	setAcceptDrops(true);

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	_inner->setLoadMoreCallback([this] {
		using State = DialogsInner::State;
		const auto state = _inner->state();
		if (state == State::Filtered && (!_inner->waitingForSearch()
			|| (_searchInMigrated
				&& _searchFull
				&& !_searchFullMigrated))) {
			onSearchMore();
		} else {
			loadDialogs();
		}
	});

	_filter->setFocusPolicy(Qt::StrongFocus);
	_filter->customUpDown(true);

	updateJumpToDateVisibility(true);
	updateSearchFromVisibility(true);
	setupConnectingWidget();
	setupSupportLoadingLimit();
}

void DialogsWidget::setupConnectingWidget() {
	_connecting = Window::ConnectingWidget::CreateDefaultWidget(
		this,
		Window::AdaptiveIsOneColumn());
}

void DialogsWidget::setupSupportLoadingLimit() {
	if (!Auth().supportMode()) {
		return;
	}
	Auth().settings().supportChatsTimeSliceValue(
	) | rpl::start_with_next([=](int seconds) {
		_dialogsLoadTill = seconds ? std::max(unixtime() - seconds, 0) : 0;
		refreshLoadMoreButton();
	}, lifetime());
}

void DialogsWidget::checkUpdateStatus() {
	Expects(!Core::UpdaterDisabled());

	using Checker = Core::UpdateChecker;
	if (Checker().state() == Checker::State::Ready) {
		if (_updateTelegram) return;
		_updateTelegram.create(
			this,
			lang(lng_update_telegram),
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

void DialogsWidget::activate() {
	_filter->setFocus();
	_inner->activate();
}

void DialogsWidget::createDialog(Dialogs::Key key) {
	const auto creating = !key.entry()->inChatList(Dialogs::Mode::All);
	_inner->createDialog(key);
	const auto history = key.history();
	if (creating && history && history->peer->migrateFrom()) {
		if (const auto migrated = App::historyLoaded(
				history->peer->migrateFrom())) {
			if (migrated->inChatList(Dialogs::Mode::All)) {
				removeDialog(migrated);
			}
		}
	}
}

void DialogsWidget::repaintDialogRow(
		Dialogs::Mode list,
		not_null<Dialogs::Row*> row) {
	_inner->repaintDialogRow(list, row);
}

void DialogsWidget::repaintDialogRow(
		not_null<History*> history,
		MsgId messageId) {
	_inner->repaintDialogRow(history, messageId);
}

void DialogsWidget::dialogsToUp() {
	if (Auth().supportMode()) {
		return;
	}
	if (_filter->getLastText().trimmed().isEmpty() && !_searchInChat) {
		_scroll->scrollToY(0);
	}
}

void DialogsWidget::startWidthAnimation() {
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
	_scroll->render(&image, QPoint(0, 0), QRect(QPoint(0, 0), grabGeometry.size()), QWidget::DrawChildren | QWidget::IgnoreMask);
	_widthAnimationCache = App::pixmapFromImageInPlace(std::move(image));
	_scroll->setGeometry(scrollGeometry);
	_scroll->hide();
}

void DialogsWidget::stopWidthAnimation() {
	_widthAnimationCache = QPixmap();
	if (!_a_show.animating()) {
		_scroll->show();
	}
	update();
}

void DialogsWidget::showFast() {
	show();
	updateForwardBar();
}

void DialogsWidget::showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params) {
	_showDirection = direction;

	_a_show.finish();

	_cacheUnder = params.oldContentCache;
	show();
	updateForwardBar();
	_cacheOver = App::main()->grabForShowAnimation(params);

	_scroll->hide();
	_mainMenuToggle->hide();
	if (_forwardCancel) _forwardCancel->hide();
	_filter->hide();
	_cancelSearch->hide(anim::type::instant);
	_jumpToDate->hide(anim::type::instant);
	_chooseFromUser->hide(anim::type::instant);
	_lockUnlock->hide();
	_connecting->setForceHidden(true);

	int delta = st::slideShift;
	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());
}

bool DialogsWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect DialogsWidget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

void DialogsWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		_cacheUnder = _cacheOver = QPixmap();

		_scroll->show();
		_mainMenuToggle->show();
		if (_forwardCancel) _forwardCancel->show();
		_filter->show();
		_connecting->setForceHidden(false);
		updateLockUnlockVisibility();
		updateJumpToDateVisibility(true);
		updateSearchFromVisibility(true);

		onFilterUpdate();
		if (App::wnd()) App::wnd()->setInnerFocus();
	}
}

void DialogsWidget::onCancel() {
	if (!onCancelSearch() || (!_searchInChat && !App::main()->selectingPeer())) {
		emit cancelled();
	}
}

void DialogsWidget::notify_historyMuteUpdated(History *history) {
	_inner->notify_historyMuteUpdated(history);
}

void DialogsWidget::dialogsReceived(
		const MTPmessages_Dialogs &dialogs,
		mtpRequestId requestId) {
	if (_dialogsRequestId != requestId) return;

	const auto [dialogsList, messagesList] = [&] {
		const auto process = [&](const auto &data) {
			App::feedUsers(data.vusers);
			App::feedChats(data.vchats);
			return std::make_tuple(&data.vdialogs.v, &data.vmessages.v);
		};
		switch (dialogs.type()) {
		case mtpc_messages_dialogs:
			_dialogsFull = true;
			return process(dialogs.c_messages_dialogs());

		case mtpc_messages_dialogsSlice:
			return process(dialogs.c_messages_dialogsSlice());
		}
		Unexpected("Type in DialogsWidget::dialogsReceived");
	}();

	updateDialogsOffset(*dialogsList, *messagesList);

	applyReceivedDialogs(*dialogsList, *messagesList);

	_dialogsRequestId = 0;
	loadDialogs();

	if (!_dialogsRequestId) {
		refreshLoadMoreButton();
	}

	Auth().data().moreChatsLoaded().notify();
	if (_dialogsFull && _pinnedDialogsReceived) {
		Auth().data().allChatsLoaded().set(true);
	}
	Auth().api().requestContacts();
}

void DialogsWidget::updateDialogsOffset(
		const QVector<MTPDialog> &dialogs,
		const QVector<MTPMessage> &messages) {
	auto lastDate = TimeId(0);
	auto lastPeer = PeerId(0);
	auto lastMsgId = MsgId(0);
	const auto fillFromDialog = [&](const auto &dialog) {
		const auto peer = peerFromMTP(dialog.vpeer);
		const auto msgId = dialog.vtop_message.v;
		if (!peer || !msgId) {
			return;
		}
		if (!lastPeer) {
			lastPeer = peer;
		}
		if (!lastMsgId) {
			lastMsgId = msgId;
		}
		for (auto j = messages.size(); j != 0;) {
			const auto &message = messages[--j];
			if (IdFromMessage(message) == msgId
				&& PeerFromMessage(message) == peer) {
				if (const auto date = DateFromMessage(message)) {
					lastDate = date;
				}
				return;
			}
		}
	};
	for (auto i = dialogs.size(); i != 0;) {
		const auto &dialog = dialogs[--i];
		switch (dialog.type()) {
		case mtpc_dialog: fillFromDialog(dialog.c_dialog()); break;
//		case mtpc_dialogFeed: fillFromDialog(dialog.c_dialogFeed()); break; // #feed
		default: Unexpected("Type in DialogsWidget::updateDialogsOffset");
		}
		if (lastDate) {
			break;
		}
	}
	if (lastDate) {
		_dialogsOffsetDate = lastDate;
		_dialogsOffsetId = lastMsgId;
		_dialogsOffsetPeer = App::peer(lastPeer);
	} else {
		_dialogsFull = true;
	}
}

void DialogsWidget::refreshLoadMoreButton() {
	if (_dialogsFull || !_dialogsLoadTill) {
		_loadMoreChats.destroy();
		updateControlsGeometry();
		return;
	}
	if (!_loadMoreChats) {
		_loadMoreChats.create(
			this,
			"Load more",
			st::dialogsLoadMoreButton,
			st::dialogsLoadMore,
			st::dialogsLoadMore);
		_loadMoreChats->addClickHandler([=] {
			if (_loadMoreChats->isDisabled()) {
				return;
			}
			const auto max = Auth().settings().supportChatsTimeSlice();
			_dialogsLoadTill = _dialogsOffsetDate
				? (_dialogsOffsetDate - max)
				: (unixtime() - max);
			loadDialogs();
		});
		updateControlsGeometry();
	}
	const auto loading = !loadingBlockedByDate();
	_loadMoreChats->setDisabled(loading);
	_loadMoreChats->setText(loading ? "Loading..." : "Load more");
}

void DialogsWidget::pinnedDialogsReceived(
		const MTPmessages_PeerDialogs &result,
		mtpRequestId requestId) {
	Expects(result.type() == mtpc_messages_peerDialogs);

	if (_pinnedDialogsRequestId != requestId) return;

	auto &data = result.c_messages_peerDialogs();
	App::feedUsers(data.vusers);
	App::feedChats(data.vchats);

	Auth().data().applyPinnedDialogs(data.vdialogs.v);
	applyReceivedDialogs(data.vdialogs.v, data.vmessages.v);

	_pinnedDialogsRequestId = 0;
	_pinnedDialogsReceived = true;

	Auth().data().moreChatsLoaded().notify();
	if (_dialogsFull && _pinnedDialogsReceived) {
		Auth().data().allChatsLoaded().set(true);
	}
}

void DialogsWidget::applyReceivedDialogs(
		const QVector<MTPDialog> &dialogs,
		const QVector<MTPMessage> &messages) {
	App::feedMsgs(messages, NewMessageLast);
	_inner->dialogsReceived(dialogs);
	onListScroll();
}

bool DialogsWidget::dialogsFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_dialogsRequestId == requestId) {
		_dialogsRequestId = 0;
	} else if (_pinnedDialogsRequestId == requestId) {
		_pinnedDialogsRequestId = 0;
	}
	return true;
}

void DialogsWidget::onDraggingScrollDelta(int delta) {
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

void DialogsWidget::onDraggingScrollTimer() {
	auto delta = (_draggingScrollDelta > 0) ? qMin(_draggingScrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_draggingScrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + delta);
}

bool DialogsWidget::onSearchMessages(bool searchCache) {
	auto result = false;
	auto q = _filter->getLastText().trimmed();
	if (q.isEmpty() && !_searchFromUser) {
		MTP::cancel(base::take(_searchRequest));
		MTP::cancel(base::take(_peerSearchRequest));
		return true;
	}
	if (searchCache) {
		const auto i = _searchCache.constFind(q);
		if (i != _searchCache.cend()) {
			_searchQuery = q;
			_searchQueryFrom = _searchFromUser;
			_searchFull = _searchFullMigrated = false;
			MTP::cancel(base::take(_searchRequest));
			searchReceived(
				_searchInChat
					? DialogsSearchPeerFromStart
					: DialogsSearchFromStart,
				i.value(),
				0);
			result = true;
		}
	} else if (_searchQuery != q || _searchQueryFrom != _searchFromUser) {
		_searchQuery = q;
		_searchQueryFrom = _searchFromUser;
		_searchFull = _searchFullMigrated = false;
		MTP::cancel(base::take(_searchRequest));
		if (const auto peer = _searchInChat.peer()) {
			const auto flags = _searchQueryFrom
				? MTP_flags(MTPmessages_Search::Flag::f_from_id)
				: MTP_flags(0);
			_searchRequest = MTP::send(
				MTPmessages_Search(
					flags,
					peer->input,
					MTP_string(_searchQuery),
					_searchQueryFrom
						? _searchQueryFrom->inputUser
						: MTP_inputUserEmpty(),
					MTP_inputMessagesFilterEmpty(),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0),
					MTP_int(SearchPerPage),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0)),
				rpcDone(&DialogsWidget::searchReceived, DialogsSearchPeerFromStart),
				rpcFail(&DialogsWidget::searchFailed, DialogsSearchPeerFromStart));
		} else if (const auto feed = _searchInChat.feed()) {
			//_searchRequest = MTP::send( // #feed
			//	MTPchannels_SearchFeed(
			//		MTP_int(feed->id()),
			//		MTP_string(_searchQuery),
			//		MTP_int(0),
			//		MTP_inputPeerEmpty(),
			//		MTP_int(0),
			//		MTP_int(SearchPerPage)),
			//	rpcDone(&DialogsWidget::searchReceived, DialogsSearchFromStart),
			//	rpcFail(&DialogsWidget::searchFailed, DialogsSearchFromStart));
		} else {
			_searchRequest = MTP::send(
				MTPmessages_SearchGlobal(
					MTP_string(_searchQuery),
					MTP_int(0),
					MTP_inputPeerEmpty(),
					MTP_int(0),
					MTP_int(SearchPerPage)),
				rpcDone(&DialogsWidget::searchReceived, DialogsSearchFromStart),
				rpcFail(&DialogsWidget::searchFailed, DialogsSearchFromStart));
		}
		_searchQueries.insert(_searchRequest, _searchQuery);
	}
	if (searchForPeersRequired(q)) {
		if (searchCache) {
			auto i = _peerSearchCache.constFind(q);
			if (i != _peerSearchCache.cend()) {
				_peerSearchQuery = q;
				_peerSearchRequest = 0;
				peerSearchReceived(i.value(), 0);
				result = true;
			}
		} else if (_peerSearchQuery != q) {
			_peerSearchQuery = q;
			_peerSearchFull = false;
			_peerSearchRequest = MTP::send(
				MTPcontacts_Search(
					MTP_string(_peerSearchQuery),
					MTP_int(SearchPeopleLimit)),
				rpcDone(&DialogsWidget::peerSearchReceived),
				rpcFail(&DialogsWidget::peopleFailed));
			_peerSearchQueries.insert(_peerSearchRequest, _peerSearchQuery);
		}
	} else {
		_peerSearchQuery = q;
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

bool DialogsWidget::searchForPeersRequired(const QString &query) const {
	if (_searchInChat || query.isEmpty()) {
		return false;
	}
	return (query[0] != '#');
}

void DialogsWidget::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void DialogsWidget::onChooseByDrag() {
	_inner->chooseRow();
}

void DialogsWidget::showMainMenu() {
	App::wnd()->showMainMenu();
}

void DialogsWidget::searchMessages(
		const QString &query,
		Dialogs::Key inChat) {
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
		onFilterUpdate(true);
		_searchTimer.stop();
		onSearchMessages();

		Local::saveRecentSearchHashtags(query);
	}
}

void DialogsWidget::onSearchMore() {
	if (!_searchRequest) {
		if (!_searchFull) {
			auto offsetDate = _inner->lastSearchDate();
			auto offsetPeer = _inner->lastSearchPeer();
			auto offsetId = _inner->lastSearchId();
			if (const auto peer = _searchInChat.peer()) {
				auto flags = _searchQueryFrom
					? MTP_flags(MTPmessages_Search::Flag::f_from_id)
					: MTP_flags(0);
				_searchRequest = MTP::send(
					MTPmessages_Search(
						flags,
						peer->input,
						MTP_string(_searchQuery),
						_searchQueryFrom
							? _searchQueryFrom->inputUser
							: MTP_inputUserEmpty(),
						MTP_inputMessagesFilterEmpty(),
						MTP_int(0),
						MTP_int(0),
						MTP_int(offsetId),
						MTP_int(0),
						MTP_int(SearchPerPage),
						MTP_int(0),
						MTP_int(0),
						MTP_int(0)),
					rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart),
					rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart));
			} else if (const auto feed = _searchInChat.feed()) {
				//_searchRequest = MTP::send( // #feed
				//	MTPchannels_SearchFeed(
				//		MTP_int(feed->id()),
				//		MTP_string(_searchQuery),
				//		MTP_int(offsetDate),
				//		offsetPeer
				//			? offsetPeer->input
				//			: MTP_inputPeerEmpty(),
				//		MTP_int(offsetId),
				//		MTP_int(SearchPerPage)),
				//	rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart),
				//	rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart));
			} else {
				_searchRequest = MTP::send(
					MTPmessages_SearchGlobal(
						MTP_string(_searchQuery),
						MTP_int(offsetDate),
						offsetPeer
							? offsetPeer->input
							: MTP_inputPeerEmpty(),
						MTP_int(offsetId),
						MTP_int(SearchPerPage)),
					rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart),
					rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart));
			}
			if (!offsetId) {
				_searchQueries.insert(_searchRequest, _searchQuery);
			}
		} else if (_searchInMigrated && !_searchFullMigrated) {
			auto offsetMigratedId = _inner->lastSearchMigratedId();
			auto flags = _searchQueryFrom
				? MTP_flags(MTPmessages_Search::Flag::f_from_id)
				: MTP_flags(0);
			_searchRequest = MTP::send(
				MTPmessages_Search(
					flags,
					_searchInMigrated->peer->input,
					MTP_string(_searchQuery),
					_searchQueryFrom
						? _searchQueryFrom->inputUser
						: MTP_inputUserEmpty(),
					MTP_inputMessagesFilterEmpty(),
					MTP_int(0),
					MTP_int(0),
					MTP_int(offsetMigratedId),
					MTP_int(0),
					MTP_int(SearchPerPage),
					MTP_int(0),
					MTP_int(0),
					MTP_int(0)),
				rpcDone(&DialogsWidget::searchReceived, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart),
				rpcFail(&DialogsWidget::searchFailed, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart));
		}
	}
}

bool DialogsWidget::loadingBlockedByDate() const {
	return !_dialogsFull
		&& !_dialogsRequestId
		&& (_dialogsLoadTill > 0)
		&& (_dialogsOffsetDate > 0)
		&& (_dialogsOffsetDate <= _dialogsLoadTill);
}

void DialogsWidget::loadDialogs() {
	if (_dialogsRequestId) return;
	if (_dialogsFull) {
		_inner->addAllSavedPeers();
		return;
	} else if (loadingBlockedByDate()) {
		return;
	}

	const auto firstLoad = !_dialogsOffsetDate;
	const auto loadCount = firstLoad ? DialogsFirstLoad : DialogsPerPage;
	const auto flags = MTPmessages_GetDialogs::Flag::f_exclude_pinned;
	const auto feedId = 0;
	const auto hash = 0;
	_dialogsRequestId = MTP::send(
		MTPmessages_GetDialogs(
			MTP_flags(flags),
			//MTP_int(feedId), // #feed
			MTP_int(_dialogsOffsetDate),
			MTP_int(_dialogsOffsetId),
			_dialogsOffsetPeer
				? _dialogsOffsetPeer->input
				: MTP_inputPeerEmpty(),
			MTP_int(loadCount),
			MTP_int(hash)),
		rpcDone(&DialogsWidget::dialogsReceived),
		rpcFail(&DialogsWidget::dialogsFailed));
	if (!_pinnedDialogsReceived) {
		loadPinnedDialogs();
	}
	refreshLoadMoreButton();
}

void DialogsWidget::loadPinnedDialogs() {
	if (_pinnedDialogsRequestId) return;

	_pinnedDialogsReceived = false;
	_pinnedDialogsRequestId = MTP::send(MTPmessages_GetPinnedDialogs(), rpcDone(&DialogsWidget::pinnedDialogsReceived), rpcFail(&DialogsWidget::dialogsFailed));
}

void DialogsWidget::searchReceived(
		DialogsSearchRequestType type,
		const MTPmessages_Messages &result,
		mtpRequestId requestId) {
	using State = DialogsInner::State;
	const auto state = _inner->state();
	if (state == State::Filtered) {
		if (type == DialogsSearchFromStart || type == DialogsSearchPeerFromStart) {
			auto i = _searchQueries.find(requestId);
			if (i != _searchQueries.cend()) {
				_searchCache[i.value()] = result;
				_searchQueries.erase(i);
			}
		}
	}

	if (_searchRequest == requestId) {
		switch (result.type()) {
		case mtpc_messages_messages: {
			auto &d = result.c_messages_messages();
			if (_searchRequest != 0) {
				// Don't apply cached data!
				App::feedUsers(d.vusers);
				App::feedChats(d.vchats);
			}
			auto &msgs = d.vmessages.v;
			if (!_inner->searchReceived(msgs, type, msgs.size())) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;

		case mtpc_messages_messagesSlice: {
			auto &d = result.c_messages_messagesSlice();
			if (_searchRequest != 0) {
				// Don't apply cached data!
				App::feedUsers(d.vusers);
				App::feedChats(d.vchats);
			}
			auto &msgs = d.vmessages.v;
			if (!_inner->searchReceived(msgs, type, d.vcount.v)) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
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
					channel->ptsReceived(d.vpts.v);
				} else {
					LOG(("API Error: "
						"received messages.channelMessages when no channel "
						"was passed! (DialogsWidget::searchReceived)"));
				}
			} else {
				LOG(("API Error: "
					"received messages.channelMessages when no channel "
					"was passed! (DialogsWidget::searchReceived)"));
			}
			if (_searchRequest != 0) {
				// Don't apply cached data!
				App::feedUsers(d.vusers);
				App::feedChats(d.vchats);
			}
			auto &msgs = d.vmessages.v;
			if (!_inner->searchReceived(msgs, type, d.vcount.v)) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;

		case mtpc_messages_messagesNotModified: {
			LOG(("API Error: received messages.messagesNotModified! (DialogsWidget::searchReceived)"));
			if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
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
}

void DialogsWidget::peerSearchReceived(
		const MTPcontacts_Found &result,
		mtpRequestId requestId) {
	using State = DialogsInner::State;
	const auto state = _inner->state();
	auto q = _peerSearchQuery;
	if (state == State::Filtered) {
		auto i = _peerSearchQueries.find(requestId);
		if (i != _peerSearchQueries.cend()) {
			q = i.value();
			_peerSearchCache[q] = result;
			_peerSearchQueries.erase(i);
		}
	}
	if (_peerSearchRequest == requestId) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			auto &d = result.c_contacts_found();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			_inner->peerSearchReceived(q, d.vmy_results.v, d.vresults.v);
		} break;
		}

		_peerSearchRequest = 0;
		onListScroll();
	}
}

bool DialogsWidget::searchFailed(
		DialogsSearchRequestType type,
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_searchRequest == requestId) {
		_searchRequest = 0;
		if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
	}
	return true;
}

bool DialogsWidget::peopleFailed(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_peerSearchRequest == req) {
		_peerSearchRequest = 0;
		_peerSearchFull = true;
	}
	return true;
}

void DialogsWidget::dragEnterEvent(QDragEnterEvent *e) {
	using namespace Storage;

	if (App::main()->selectingPeer()) return;

	const auto data = e->mimeData();
	_dragInScroll = false;
	_dragForward = Adaptive::OneColumn()
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

void DialogsWidget::dragMoveEvent(QDragMoveEvent *e) {
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

void DialogsWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dragForward) {
		updateDragInScroll(false);
	} else {
		_chooseByDragTimer.stop();
	}
	_inner->dragLeft();
	e->accept();
}

void DialogsWidget::updateDragInScroll(bool inScroll) {
	if (_dragInScroll != inScroll) {
		_dragInScroll = inScroll;
		if (_dragInScroll) {
			App::main()->showForwardLayer({});
		} else {
			App::main()->dialogsCancelled();
		}
	}
}

void DialogsWidget::dropEvent(QDropEvent *e) {
	_chooseByDragTimer.stop();
	if (_scroll->geometry().contains(e->pos())) {
		if (auto peer = _inner->updateFromParentDrag(mapToGlobal(e->pos()))) {
			e->acceptProposedAction();
			App::main()->onFilesOrForwardDrop(peer->id, e->mimeData());
			controller()->window()->activateWindow();
		}
	}
}

void DialogsWidget::onListScroll() {
	auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void DialogsWidget::onFilterUpdate(bool force) {
	if (_a_show.animating() && !force) return;

	auto filterText = _filter->getLastText();
	_inner->onFilterUpdate(filterText, force);
	if (filterText.isEmpty() && !_searchFromUser) {
		clearSearchCache();
	}
	_cancelSearch->toggle(!filterText.isEmpty(), anim::type::normal);
	updateJumpToDateVisibility();

	if (filterText.isEmpty()) {
		_peerSearchCache.clear();
		_peerSearchQueries.clear();
		_peerSearchQuery = QString();
	}

	if (_chooseFromUser->toggled() || _searchFromUser) {
		auto switchToChooseFrom = SwitchToChooseFromQuery();
		if (_lastFilterText != switchToChooseFrom
			&& switchToChooseFrom.startsWith(_lastFilterText)
			&& filterText == switchToChooseFrom) {
			showSearchFrom();
		}
	}
	_lastFilterText = filterText;
}

void DialogsWidget::searchInChat(Dialogs::Key chat) {
	onCancelSearch();
	setSearchInChat(chat);
	onFilterUpdate(true);
}

void DialogsWidget::setSearchInChat(Dialogs::Key chat, UserData *from) {
	_searchInMigrated = nullptr;
	if (const auto peer = chat.peer()) {
		if (const auto migrateTo = peer->migrateTo()) {
			return setSearchInChat(App::history(migrateTo), from);
		} else if (const auto migrateFrom = peer->migrateFrom()) {
			_searchInMigrated = App::history(migrateFrom);
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
	if (_searchFromUser != from || searchInPeerUpdated) {
		_searchFromUser = from;
		updateSearchFromVisibility();
		clearSearchCache();
	}
	_inner->searchInChat(_searchInChat, _searchFromUser);
	if (_searchFromUser && _lastFilterText == SwitchToChooseFromQuery()) {
		onCancelSearch();
	}
	_filter->setFocus();
}

void DialogsWidget::clearSearchCache() {
	_searchCache.clear();
	_searchQueries.clear();
	_searchQuery = QString();
	_searchQueryFrom = nullptr;
	MTP::cancel(base::take(_searchRequest));
}

void DialogsWidget::showJumpToDate() {
	if (_searchInChat) {
		this->controller()->showJumpToDate(_searchInChat, QDate());
	}
}

void DialogsWidget::showSearchFrom() {
	if (const auto peer = _searchInChat.peer()) {
		const auto chat = _searchInChat;
		Dialogs::ShowSearchFromBox(
			controller(),
			peer,
			crl::guard(this, [=](not_null<UserData*> user) {
				Ui::hideLayer();
				setSearchInChat(chat, user);
				onFilterUpdate(true);
			}),
			crl::guard(this, [=] { _filter->setFocus(); }));
	}
}

void DialogsWidget::onFilterCursorMoved(int from, int to) {
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

void DialogsWidget::onCompleteHashtag(QString tag) {
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
				onFilterUpdate(true);
				return;
			}
			break;
		}
		if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') break;
	}
	_filter->setText(t.mid(0, cur) + '#' + tag + ' ' + t.mid(cur));
	_filter->setCursorPosition(cur + 1 + tag.size() + 1);
	onFilterUpdate(true);
}

void DialogsWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void DialogsWidget::updateLockUnlockVisibility() {
	if (!_a_show.animating()) {
		_lockUnlock->setVisible(Global::LocalPasscode());
	}
	updateControlsGeometry();
}

void DialogsWidget::updateJumpToDateVisibility(bool fast) {
	if (_a_show.animating()) return;

	_jumpToDate->toggle(
		(_searchInChat && _filter->getLastText().isEmpty()),
		fast ? anim::type::instant : anim::type::normal);
}

void DialogsWidget::updateSearchFromVisibility(bool fast) {
	auto visible = [&] {
		if (const auto peer = _searchInChat.peer()) {
			if (peer->isChat() || peer->isMegagroup()) {
				return !_searchFromUser;
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

void DialogsWidget::updateControlsGeometry() {
	auto filterAreaTop = 0;
	if (_forwardCancel) {
		_forwardCancel->moveToLeft(0, filterAreaTop);
		filterAreaTop += st::dialogsForwardHeight;
	}
	auto smallLayoutWidth = (st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x());
	auto smallLayoutRatio = (width() < st::columnMinimalWidthLeft) ? (st::columnMinimalWidthLeft - width()) / float64(st::columnMinimalWidthLeft - smallLayoutWidth) : 0.;
	auto filterLeft = st::dialogsFilterPadding.x() + _mainMenuToggle->width() + st::dialogsFilterPadding.x();
	auto filterRight = (Global::LocalPasscode() ? (st::dialogsFilterPadding.x() + _lockUnlock->width()) : st::dialogsFilterSkip) + st::dialogsFilterPadding.x();
	auto filterWidth = qMax(width(), st::columnMinimalWidthLeft) - filterLeft - filterRight;
	auto filterAreaHeight = st::dialogsFilterPadding.y() + _mainMenuToggle->height() + st::dialogsFilterPadding.y();
	auto filterTop = filterAreaTop + (filterAreaHeight - _filter->height()) / 2;
	filterLeft = anim::interpolate(filterLeft, smallLayoutWidth, smallLayoutRatio);
	_filter->setGeometryToLeft(filterLeft, filterTop, filterWidth, _filter->height());
	auto mainMenuLeft = anim::interpolate(st::dialogsFilterPadding.x(), (smallLayoutWidth - _mainMenuToggle->width()) / 2, smallLayoutRatio);
	_mainMenuToggle->moveToLeft(mainMenuLeft, filterAreaTop + st::dialogsFilterPadding.y());
	auto right = filterLeft + filterWidth;
	_lockUnlock->moveToLeft(right + st::dialogsFilterPadding.x(), filterAreaTop + st::dialogsFilterPadding.y());
	_cancelSearch->moveToLeft(right - _cancelSearch->width(), _filter->y());
	right -= _jumpToDate->width(); _jumpToDate->moveToLeft(right, _filter->y());
	right -= _chooseFromUser->width(); _chooseFromUser->moveToLeft(right, _filter->y());

	auto scrollTop = filterAreaTop + filterAreaHeight;
	auto addToScroll = App::main() ? App::main()->contentScrollAddToY() : 0;
	auto newScrollTop = _scroll->scrollTop() + addToScroll;
	auto scrollHeight = height() - scrollTop;
	const auto putBottomButton = [&](object_ptr<BottomButton> &button) {
		if (button) {
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
	if (scrollHeight != wasScrollHeight) {
		controller()->floatPlayerAreaUpdated().notify(true);
	}
	if (addToScroll) {
		_scroll->scrollToY(newScrollTop);
	} else {
		onListScroll();
	}
}

void DialogsWidget::updateForwardBar() {
	auto selecting = App::main()->selectingPeer();
	auto oneColumnSelecting = (Adaptive::OneColumn() && selecting);
	if (!oneColumnSelecting == !_forwardCancel) {
		return;
	}
	if (oneColumnSelecting) {
		_forwardCancel.create(this, st::dialogsForwardCancel);
		_forwardCancel->setClickedCallback([] { Global::RefPeerChooseCancel().notify(true); });
		if (!_a_show.animating()) _forwardCancel->show();
	} else {
		_forwardCancel.destroyDelayed();
	}
	updateControlsGeometry();
	update();
}

void DialogsWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!_inner->chooseRow()) {
			using State = DialogsInner::State;
			const auto state = _inner->state();
			if (state == State::Default
				|| (state == State::Filtered
					&& (!_inner->waitingForSearch() ||  _inner->hasFilteredResults()))) {
				_inner->selectSkip(1);
				_inner->chooseRow();
			} else {
				onSearchMessages();
			}
		}
	} else if (e->key() == Qt::Key_Down) {
		_inner->setMouseSelection(false);
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->setMouseSelection(false);
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->setMouseSelection(false);
		_inner->selectSkipPage(_scroll->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->setMouseSelection(false);
		_inner->selectSkipPage(_scroll->height(), -1);
	} else {
		e->ignore();
	}
}

void DialogsWidget::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	auto progress = _a_show.current(getms(), 1.);
	if (_a_show.animating()) {
		auto retina = cIntRetinaFactor();
		auto fromLeft = (_showDirection == Window::SlideDirection::FromLeft);
		auto coordUnder = fromLeft ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = fromLeft ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		auto shadow = fromLeft ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, 0, coordOver, _cacheUnder.height() / retina), _cacheUnder, QRect(-coordUnder * retina, 0, coordOver * retina, _cacheUnder.height()));
			p.setOpacity(shadow);
			p.fillRect(0, 0, coordOver, _cacheUnder.height() / retina, st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(QRect(coordOver, 0, _cacheOver.width() / retina, _cacheOver.height() / retina), _cacheOver, QRect(0, 0, _cacheOver.width(), _cacheOver.height()));
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), _cacheOver.height() / retina));
		return;
	}
	auto aboveTop = 0;
	if (_forwardCancel) {
		p.fillRect(0, aboveTop, width(), st::dialogsForwardHeight, st::dialogsForwardBg);
		p.setPen(st::dialogsForwardFg);
		p.setFont(st::dialogsForwardFont);
		p.drawTextLeft(st::dialogsForwardTextLeft, st::dialogsForwardTextTop, width(), lang(lng_forward_choose));
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

void DialogsWidget::destroyData() {
	_inner->destroyData();
}

void DialogsWidget::scrollToEntry(const Dialogs::RowDescriptor &entry) {
	_inner->scrollToEntry(entry);
}

void DialogsWidget::removeDialog(Dialogs::Key key) {
	_inner->removeDialog(key);
	onFilterUpdate();
}

Dialogs::IndexedList *DialogsWidget::contactsList() {
	return _inner->contactsList();
}

Dialogs::IndexedList *DialogsWidget::dialogsList() {
	return _inner->dialogsList();
}

Dialogs::IndexedList *DialogsWidget::contactsNoDialogsList() {
	return _inner->contactsNoDialogsList();
}

bool DialogsWidget::onCancelSearch() {
	bool clearing = !_filter->getLastText().isEmpty();
	if (_searchRequest) {
		MTP::cancel(_searchRequest);
		_searchRequest = 0;
	}
	if (_searchInChat && !clearing) {
		if (Adaptive::OneColumn()) {
			if (const auto peer = _searchInChat.peer()) {
				Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
			} else if (const auto feed = _searchInChat.feed()) {
				controller()->showSection(HistoryFeed::Memento(feed));
			} else {
				Unexpected("Empty key in onCancelSearch().");
			}
		}
		setSearchInChat(Dialogs::Key());
		clearing = true;
	}
	_inner->clearFilter();
	_filter->clear();
	_filter->updatePlaceholder();
	onFilterUpdate();
	return clearing;
}

void DialogsWidget::onCancelSearchInChat() {
	if (_searchRequest) {
		MTP::cancel(_searchRequest);
		_searchRequest = 0;
	}
	if (_searchInChat) {
		if (Adaptive::OneColumn() && !App::main()->selectingPeer()) {
			if (const auto peer = _searchInChat.peer()) {
				Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
			} else if (const auto feed = _searchInChat.feed()) {
				controller()->showSection(HistoryFeed::Memento(feed));
			} else {
				Unexpected("Empty key in onCancelSearchInPeer().");
			}
		}
		setSearchInChat(Dialogs::Key());
	}
	_inner->clearFilter();
	_filter->clear();
	_filter->updatePlaceholder();
	onFilterUpdate();
	if (!Adaptive::OneColumn() && !App::main()->selectingPeer()) {
		emit cancelled();
	}
}

void DialogsWidget::onDialogMoved(int movedFrom, int movedTo) {
	int32 st = _scroll->scrollTop();
	if (st > movedTo && st < movedFrom) {
		_scroll->scrollToY(st + st::dialogsRowHeight);
	}
}
