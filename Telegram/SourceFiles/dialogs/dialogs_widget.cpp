/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "dialogs/dialogs_widget.h"

#include "dialogs/dialogs_inner_widget.h"
#include "dialogs/dialogs_search_from_controllers.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/fade_wrap.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "autoupdater.h"
#include "auth_session.h"
#include "messenger.h"
#include "boxes/peer_list_box.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "profile/profile_channel_controllers.h"

namespace {

QString SwitchToChooseFromQuery() {
	return qsl("from:");
}

} // namespace

class DialogsWidget::UpdateButton : public Ui::RippleButton {
public:
	UpdateButton(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	QString _text;
	const style::FlatButton &_st;

};

DialogsWidget::UpdateButton::UpdateButton(QWidget *parent) : RippleButton(parent, st::dialogsUpdateButton.ripple)
, _text(lang(lng_update_telegram).toUpper())
, _st(st::dialogsUpdateButton) {
	resize(st::columnMinimalWidthLeft, _st.height);
}

void DialogsWidget::UpdateButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

void DialogsWidget::UpdateButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r(0, height() - _st.height, width(), _st.height);
	p.fillRect(r, isOver() ? _st.overBgColor : _st.bgColor);

	paintRipple(p, 0, 0, getms());

	p.setFont(isOver() ? _st.overFont : _st.font);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(isOver() ? _st.overColor : _st.color);

	if (width() >= st::columnMinimalWidthLeft) {
		r.setTop(_st.textTop);
		p.drawText(r, _text, style::al_top);
	} else {
		(isOver() ? st::dialogsInstallUpdateOver : st::dialogsInstallUpdate).paintInCenter(p, r);
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
	connect(_inner, SIGNAL(searchResultChosen()), this, SLOT(onCancel()));
	connect(_inner, SIGNAL(completeHashtag(QString)), this, SLOT(onCompleteHashtag(QString)));
	connect(_inner, SIGNAL(refreshHashtags()), this, SLOT(onFilterCursorMoved()));
	connect(_inner, SIGNAL(cancelSearchInPeer()), this, SLOT(onCancelSearchInPeer()));
	subscribe(_inner->searchFromUserChanged, [this](UserData *user) {
		setSearchInPeer(_searchInPeer, user);
		onFilterUpdate(true);
	});
	connect(_scroll, SIGNAL(geometryChanged()), _inner, SLOT(onParentGeometryChanged()));
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(_filter, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(_filter, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onFilterCursorMoved(int,int)));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Sandbox::connect(SIGNAL(updateLatest()), this, SLOT(onCheckUpdateStatus()));
	Sandbox::connect(SIGNAL(updateFailed()), this, SLOT(onCheckUpdateStatus()));
	Sandbox::connect(SIGNAL(updateReady()), this, SLOT(onCheckUpdateStatus()));
	onCheckUpdateStatus();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

	subscribe(Adaptive::Changed(), [this] { updateForwardBar(); });

	_cancelSearch->setClickedCallback([this] { onCancelSearch(); });
	_jumpToDate->entity()->setClickedCallback([this] { if (_searchInPeer) this->controller()->showJumpToDate(_searchInPeer, QDate()); });
	_chooseFromUser->entity()->setClickedCallback([this] { showSearchFrom(); });
	_lockUnlock->setVisible(Global::LocalPasscode());
	subscribe(Global::RefLocalPasscodeChanged(), [this] { updateLockUnlockVisibility(); });
	_lockUnlock->setClickedCallback([this] {
		_lockUnlock->setIconOverride(&st::dialogsUnlockIcon, &st::dialogsUnlockIconOver);
		Messenger::Instance().setupPasscode();
		_lockUnlock->setIconOverride(nullptr);
	});
	_mainMenuToggle->setClickedCallback([this] { showMainMenu(); });

	_chooseByDragTimer.setSingleShot(true);
	connect(&_chooseByDragTimer, SIGNAL(timeout()), this, SLOT(onChooseByDrag()));

	setAcceptDrops(true);

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	_inner->setLoadMoreCallback([this] {
		if (_inner->state() == DialogsInner::SearchedState || (_inner->state() == DialogsInner::FilteredState && _searchInMigrated && _searchFull && !_searchFullMigrated)) {
			onSearchMore();
		} else {
			loadDialogs();
		}
	});

	_filter->setFocusPolicy(Qt::StrongFocus);
	_filter->customUpDown(true);

	updateJumpToDateVisibility(true);
	updateSearchFromVisibility(true);
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void DialogsWidget::onCheckUpdateStatus() {
	if (Sandbox::updatingState() == Application::UpdatingReady) {
		if (_updateTelegram) return;
		_updateTelegram.create(this);
		_updateTelegram->show();
		_updateTelegram->setClickedCallback([] {
			checkReadyUpdate();
			App::restart();
		});
	} else {
		if (!_updateTelegram) return;
		_updateTelegram.destroy();
	}
	updateControlsGeometry();
}
#endif // TDESKTOP_DISABLE_AUTOUPDATE

void DialogsWidget::activate() {
	_filter->setFocus();
	_inner->activate();
}

void DialogsWidget::createDialog(History *history) {
	auto creating = !history->inChatList(Dialogs::Mode::All);
	_inner->createDialog(history);
	if (creating && history->peer->migrateFrom()) {
		if (auto migrated = App::historyLoaded(history->peer->migrateFrom()->id)) {
			if (migrated->inChatList(Dialogs::Mode::All)) {
				removeDialog(migrated);
			}
		}
	}
}

void DialogsWidget::dlgUpdated(Dialogs::Mode list, Dialogs::Row *row) {
	_inner->dlgUpdated(list, row);
}

void DialogsWidget::dlgUpdated(PeerData *peer, MsgId msgId) {
	_inner->dlgUpdated(peer, msgId);
}

void DialogsWidget::dialogsToUp() {
	if (_filter->getLastText().trimmed().isEmpty()) {
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
	myEnsureResized(_scroll);
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
		updateLockUnlockVisibility();
		updateJumpToDateVisibility(true);
		updateSearchFromVisibility(true);

		onFilterUpdate();
		if (App::wnd()) App::wnd()->setInnerFocus();
	}
}

void DialogsWidget::onCancel() {
	if (!onCancelSearch() || (!_searchInPeer && !App::main()->selectingPeer())) {
		emit cancelled();
	}
}

void DialogsWidget::notify_userIsContactChanged(UserData *user, bool fromThisApp) {
	if (fromThisApp) {
		_filter->setText(QString());
		_filter->updatePlaceholder();
		onFilterUpdate();
	}
	_inner->notify_userIsContactChanged(user, fromThisApp);
}

void DialogsWidget::notify_historyMuteUpdated(History *history) {
	_inner->notify_historyMuteUpdated(history);
}

void DialogsWidget::unreadCountsReceived(const QVector<MTPDialog> &dialogs) {
}

void DialogsWidget::dialogsReceived(const MTPmessages_Dialogs &dialogs, mtpRequestId requestId) {
	if (_dialogsRequestId != requestId) return;

	const QVector<MTPDialog> *dialogsList = 0;
	const QVector<MTPMessage> *messagesList = 0;
	switch (dialogs.type()) {
	case mtpc_messages_dialogs: {
		auto &data = dialogs.c_messages_dialogs();
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		messagesList = &data.vmessages.v;
		dialogsList = &data.vdialogs.v;
		_dialogsFull = true;
	} break;
	case mtpc_messages_dialogsSlice: {
		auto &data = dialogs.c_messages_dialogsSlice();
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		messagesList = &data.vmessages.v;
		dialogsList = &data.vdialogs.v;
	} break;
	}

	if (!Auth().data().contactsLoaded().value() && !_contactsRequestId) {
		_contactsRequestId = MTP::send(MTPcontacts_GetContacts(MTP_int(0)), rpcDone(&DialogsWidget::contactsReceived), rpcFail(&DialogsWidget::contactsFailed));
	}

	if (dialogsList) {
		TimeId lastDate = 0;
		PeerId lastPeer = 0;
		MsgId lastMsgId = 0;
		for (int i = dialogsList->size(); i > 0;) {
			auto &dialog = dialogsList->at(--i);
			if (dialog.type() != mtpc_dialog) {
				continue;
			}

			auto &dialogData = dialog.c_dialog();
			if (auto peer = peerFromMTP(dialogData.vpeer)) {
				auto history = App::history(peer);
				history->setPinnedDialog(dialogData.is_pinned());

				if (!lastDate) {
					if (!lastPeer) lastPeer = peer;
					if (auto msgId = dialogData.vtop_message.v) {
						if (!lastMsgId) lastMsgId = msgId;
						for (int j = messagesList->size(); j > 0;) {
							auto &message = messagesList->at(--j);
							if (idFromMessage(message) == msgId && peerFromMessage(message) == peer) {
								if (auto date = dateFromMessage(message)) {
									lastDate = date;
								}
								break;
							}
						}
					}
				}
			}
		}
		if (lastDate) {
			_dialogsOffsetDate = lastDate;
			_dialogsOffsetId = lastMsgId;
			_dialogsOffsetPeer = App::peer(lastPeer);
		} else {
			_dialogsFull = true;
		}

		Assert(messagesList != nullptr);
		App::feedMsgs(*messagesList, NewMessageLast);

		unreadCountsReceived(*dialogsList);
		_inner->dialogsReceived(*dialogsList);
		onListScroll();
	} else {
		_dialogsFull = true;
	}

	_dialogsRequestId = 0;
	loadDialogs();

	Auth().data().moreChatsLoaded().notify();
	if (_dialogsFull) {
		Auth().data().allChatsLoaded().set(true);
	}
}

void DialogsWidget::pinnedDialogsReceived(const MTPmessages_PeerDialogs &dialogs, mtpRequestId requestId) {
	if (_pinnedDialogsRequestId != requestId) return;

	if (dialogs.type() == mtpc_messages_peerDialogs) {
		App::histories().clearPinned();

		auto &dialogsData = dialogs.c_messages_peerDialogs();
		App::feedUsers(dialogsData.vusers);
		App::feedChats(dialogsData.vchats);
		auto &list = dialogsData.vdialogs.v;
		for (auto i = list.size(); i > 0;) {
			auto &dialog = list[--i];
			if (dialog.type() != mtpc_dialog) {
				continue;
			}

			auto &dialogData = dialog.c_dialog();
			if (auto peer = peerFromMTP(dialogData.vpeer)) {
				auto history = App::history(peer);
				history->setPinnedDialog(dialogData.is_pinned());
			}
		}
		App::feedMsgs(dialogsData.vmessages, NewMessageLast);
		unreadCountsReceived(list);
		_inner->dialogsReceived(list);
		onListScroll();
	}

	_pinnedDialogsRequestId = 0;
	_pinnedDialogsReceived = true;

	Auth().data().moreChatsLoaded().notify();
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
	auto q = _filter->getLastText().trimmed();
	if (q.isEmpty() && !_searchFromUser) {
		MTP::cancel(base::take(_searchRequest));
		MTP::cancel(base::take(_peerSearchRequest));
		return true;
	}
	if (searchCache) {
		SearchCache::const_iterator i = _searchCache.constFind(q);
		if (i != _searchCache.cend()) {
			_searchQuery = q;
			_searchQueryFrom = _searchFromUser;
			_searchFull = _searchFullMigrated = false;
			MTP::cancel(base::take(_searchRequest));
			searchReceived(_searchInPeer ? DialogsSearchPeerFromStart : DialogsSearchFromStart, i.value(), 0);
			return true;
		}
	} else if (_searchQuery != q || _searchQueryFrom != _searchFromUser) {
		_searchQuery = q;
		_searchQueryFrom = _searchFromUser;
		_searchFull = _searchFullMigrated = false;
		MTP::cancel(base::take(_searchRequest));
		if (_searchInPeer) {
			auto flags = _searchQueryFrom ? MTP_flags(MTPmessages_Search::Flag::f_from_id) : MTP_flags(0);
			_searchRequest = MTP::send(MTPmessages_Search(flags, _searchInPeer->input, MTP_string(_searchQuery), _searchQueryFrom ? _searchQueryFrom->inputUser : MTP_inputUserEmpty(), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(SearchPerPage), MTP_int(0), MTP_int(0)), rpcDone(&DialogsWidget::searchReceived, DialogsSearchPeerFromStart), rpcFail(&DialogsWidget::searchFailed, DialogsSearchPeerFromStart));
		} else {
			_searchRequest = MTP::send(MTPmessages_SearchGlobal(MTP_string(_searchQuery), MTP_int(0), MTP_inputPeerEmpty(), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, DialogsSearchFromStart), rpcFail(&DialogsWidget::searchFailed, DialogsSearchFromStart));
		}
		_searchQueries.insert(_searchRequest, _searchQuery);
	}
	if (!_searchInPeer && q.size() >= MinUsernameLength) {
		if (searchCache) {
			auto i = _peerSearchCache.constFind(q);
			if (i != _peerSearchCache.cend()) {
				_peerSearchQuery = q;
				_peerSearchRequest = 0;
				peerSearchReceived(i.value(), 0);
				return true;
			}
		} else if (_peerSearchQuery != q) {
			_peerSearchQuery = q;
			_peerSearchFull = false;
			_peerSearchRequest = MTP::send(MTPcontacts_Search(MTP_string(_peerSearchQuery), MTP_int(SearchPeopleLimit)), rpcDone(&DialogsWidget::peerSearchReceived), rpcFail(&DialogsWidget::peopleFailed));
			_peerSearchQueries.insert(_peerSearchRequest, _peerSearchQuery);
		}
	}
	return false;
}

void DialogsWidget::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void DialogsWidget::onChooseByDrag() {
	_inner->choosePeer();
}

void DialogsWidget::showMainMenu() {
	App::wnd()->showMainMenu();
}

void DialogsWidget::searchMessages(const QString &query, PeerData *inPeer) {
	if ((_filter->getLastText() != query) || (inPeer && inPeer != _searchInPeer && inPeer->migrateTo() != _searchInPeer)) {
		if (inPeer) {
			onCancelSearch();
			setSearchInPeer(inPeer);
		}
		_filter->setText(query);
		_filter->updatePlaceholder();
		onFilterUpdate(true);
		_searchTimer.stop();
		onSearchMessages();

		_inner->saveRecentHashtags(query);
	}
}

void DialogsWidget::onSearchMore() {
	if (!_searchRequest) {
		if (!_searchFull) {
			auto offsetDate = _inner->lastSearchDate();
			auto offsetPeer = _inner->lastSearchPeer();
			auto offsetId = _inner->lastSearchId();
			if (_searchInPeer) {
				auto flags = _searchQueryFrom ? MTP_flags(MTPmessages_Search::Flag::f_from_id) : MTP_flags(0);
				_searchRequest = MTP::send(MTPmessages_Search(flags, _searchInPeer->input, MTP_string(_searchQuery), _searchQueryFrom ? _searchQueryFrom->inputUser : MTP_inputUserEmpty(), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(offsetId), MTP_int(0), MTP_int(SearchPerPage), MTP_int(0), MTP_int(0)), rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart), rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart));
			} else {
				_searchRequest = MTP::send(MTPmessages_SearchGlobal(MTP_string(_searchQuery), MTP_int(offsetDate), offsetPeer ? offsetPeer->input : MTP_inputPeerEmpty(), MTP_int(offsetId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart), rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart));
			}
			if (!offsetId) {
				_searchQueries.insert(_searchRequest, _searchQuery);
			}
		} else if (_searchInMigrated && !_searchFullMigrated) {
			auto offsetMigratedId = _inner->lastSearchMigratedId();
			auto flags = _searchQueryFrom ? MTP_flags(MTPmessages_Search::Flag::f_from_id) : MTP_flags(0);
			_searchRequest = MTP::send(MTPmessages_Search(flags, _searchInMigrated->input, MTP_string(_searchQuery), _searchQueryFrom ? _searchQueryFrom->inputUser : MTP_inputUserEmpty(), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(offsetMigratedId), MTP_int(0), MTP_int(SearchPerPage), MTP_int(0), MTP_int(0)), rpcDone(&DialogsWidget::searchReceived, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart), rpcFail(&DialogsWidget::searchFailed, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart));
		}
	}
}

void DialogsWidget::loadDialogs() {
	if (_dialogsRequestId) return;
	if (_dialogsFull) {
		_inner->addAllSavedPeers();
		return;
	}

	auto firstLoad = !_dialogsOffsetDate;
	auto loadCount = firstLoad ? DialogsFirstLoad : DialogsPerPage;
	auto flags = MTPmessages_GetDialogs::Flag::f_exclude_pinned;
	_dialogsRequestId = MTP::send(MTPmessages_GetDialogs(MTP_flags(flags), MTP_int(_dialogsOffsetDate), MTP_int(_dialogsOffsetId), _dialogsOffsetPeer ? _dialogsOffsetPeer->input : MTP_inputPeerEmpty(), MTP_int(loadCount)), rpcDone(&DialogsWidget::dialogsReceived), rpcFail(&DialogsWidget::dialogsFailed));
	if (!_pinnedDialogsReceived) {
		loadPinnedDialogs();
	}
}

void DialogsWidget::loadPinnedDialogs() {
	if (_pinnedDialogsRequestId) return;

	_pinnedDialogsReceived = false;
	_pinnedDialogsRequestId = MTP::send(MTPmessages_GetPinnedDialogs(), rpcDone(&DialogsWidget::pinnedDialogsReceived), rpcFail(&DialogsWidget::dialogsFailed));
}

void DialogsWidget::contactsReceived(const MTPcontacts_Contacts &result) {
	_contactsRequestId = 0;
	if (result.type() == mtpc_contacts_contacts) {
		auto &d = result.c_contacts_contacts();
		App::feedUsers(d.vusers);
		_inner->contactsReceived(d.vcontacts.v);
	}
	Auth().data().contactsLoaded().set(true);
}

bool DialogsWidget::contactsFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	_contactsRequestId = 0;
	return true;
}

void DialogsWidget::searchReceived(DialogsSearchRequestType type, const MTPmessages_Messages &result, mtpRequestId req) {
	if (_inner->state() == DialogsInner::FilteredState || _inner->state() == DialogsInner::SearchedState) {
		if (type == DialogsSearchFromStart || type == DialogsSearchPeerFromStart) {
			auto i = _searchQueries.find(req);
			if (i != _searchQueries.cend()) {
				_searchCache[i.value()] = result;
				_searchQueries.erase(i);
			}
		}
	}

	if (_searchRequest == req) {
		switch (result.type()) {
		case mtpc_messages_messages: {
			auto &d = result.c_messages_messages();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
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
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
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
			if (_searchInPeer && _searchInPeer->isChannel()) {
				_searchInPeer->asChannel()->ptsReceived(d.vpts.v);
			} else {
				LOG(("API Error: received messages.channelMessages when no channel was passed! (DialogsWidget::searchReceived)"));
			}
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
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

void DialogsWidget::peerSearchReceived(const MTPcontacts_Found &result, mtpRequestId req) {
	auto q = _peerSearchQuery;
	if (_inner->state() == DialogsInner::FilteredState || _inner->state() == DialogsInner::SearchedState) {
		auto i = _peerSearchQueries.find(req);
		if (i != _peerSearchQueries.cend()) {
			q = i.value();
			_peerSearchCache[q] = result;
			_peerSearchQueries.erase(i);
		}
	}
	if (_peerSearchRequest == req) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			auto &d = result.c_contacts_found();
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			_inner->peerSearchReceived(q, d.vresults.v);
		} break;
		}

		_peerSearchRequest = 0;
		onListScroll();
	}
}

bool DialogsWidget::searchFailed(DialogsSearchRequestType type, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_searchRequest == req) {
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
	if (App::main()->selectingPeer()) return;

	_dragInScroll = false;
	_dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-selected"));
	if (!_dragForward) _dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-pressed-link"));
	if (!_dragForward) _dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-pressed"));
	if (_dragForward && Adaptive::OneColumn()) _dragForward = false;
	if (_dragForward) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
		updateDragInScroll(_scroll->geometry().contains(e->pos()));
	} else if (App::main() && App::main()->getDragState(e->mimeData()) != DragStateNone) {
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
		PeerData *p = _inner->updateFromParentDrag(mapToGlobal(e->pos()));
		if (p) {
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
			App::main()->showForwardLayer(SelectedItemSet());
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
	if (filterText.isEmpty()) {
		clearSearchCache();
	}
	_cancelSearch->toggle(!filterText.isEmpty(), anim::type::normal);
	updateJumpToDateVisibility();

	if (filterText.size() < MinUsernameLength) {
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

void DialogsWidget::searchInPeer(PeerData *peer) {
	onCancelSearch();
	setSearchInPeer(peer);
	onFilterUpdate(true);
}

void DialogsWidget::setSearchInPeer(PeerData *peer, UserData *from) {
	auto searchInPeerUpdated = false;
	auto newSearchInPeer = peer ? (peer->migrateTo() ? peer->migrateTo() : peer) : nullptr;
	_searchInMigrated = newSearchInPeer ? newSearchInPeer->migrateFrom() : nullptr;
	searchInPeerUpdated = (newSearchInPeer != _searchInPeer);
	if (searchInPeerUpdated) {
		_searchInPeer = newSearchInPeer;
		from = nullptr;
		controller()->searchInPeer = _searchInPeer;
		updateJumpToDateVisibility();
	} else if (!_searchInPeer) {
		from = nullptr;
	}
	if (_searchFromUser != from || searchInPeerUpdated) {
		_searchFromUser = from;
		updateSearchFromVisibility();
		clearSearchCache();
	}
	_inner->searchInPeer(_searchInPeer, _searchFromUser);
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

void DialogsWidget::showSearchFrom() {
	if (!_searchInPeer) {
		return;
	}
	auto peer = _searchInPeer;
	Dialogs::ShowSearchFromBox(
		controller(),
		peer,
		base::lambda_guarded(this, [this, peer](
				not_null<UserData*> user) {
			Ui::hideLayer();
			setSearchInPeer(peer, user);
			onFilterUpdate(true);
		}),
		base::lambda_guarded(this, [this] { _filter->setFocus(); }));
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
		(_searchInPeer && _filter->getLastText().isEmpty()),
		fast ? anim::type::instant : anim::type::normal);
}

void DialogsWidget::updateSearchFromVisibility(bool fast) {
	auto visible = _searchInPeer && (_searchInPeer->isChat() || _searchInPeer->isMegagroup()) && !_searchFromUser;
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
	if (_updateTelegram) {
		auto updateHeight = _updateTelegram->height();
		_updateTelegram->setGeometry(0, height() - updateHeight, width(), updateHeight);
		scrollHeight -= updateHeight;
	}
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
		if (!_inner->choosePeer()) {
			if (_inner->state() == DialogsInner::DefaultState || _inner->state() == DialogsInner::SearchedState || (_inner->state() == DialogsInner::FilteredState && _inner->hasFilteredResults())) {
				_inner->selectSkip(1);
				_inner->choosePeer();
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

void DialogsWidget::peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	return _inner->peerBefore(inPeer, inMsg, outPeer, outMsg);
}

void DialogsWidget::peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	return _inner->peerAfter(inPeer, inMsg, outPeer, outMsg);
}

void DialogsWidget::scrollToPeer(const PeerId &peer, MsgId msgId) {
	_inner->scrollToPeer(peer, msgId);
}

void DialogsWidget::removeDialog(History *history) {
	_inner->removeDialog(history);
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
	if (_searchInPeer && !clearing) {
		if (Adaptive::OneColumn()) {
			Ui::showPeerHistory(_searchInPeer, ShowAtUnreadMsgId);
		}
		setSearchInPeer(nullptr);
		clearing = true;
	}
	_inner->clearFilter();
	_filter->clear();
	_filter->updatePlaceholder();
	onFilterUpdate();
	return clearing;
}

void DialogsWidget::onCancelSearchInPeer() {
	if (_searchRequest) {
		MTP::cancel(_searchRequest);
		_searchRequest = 0;
	}
	if (_searchInPeer) {
		if (Adaptive::OneColumn() && !App::main()->selectingPeer()) {
			Ui::showPeerHistory(_searchInPeer, ShowAtUnreadMsgId);
		}
		setSearchInPeer(nullptr);
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
