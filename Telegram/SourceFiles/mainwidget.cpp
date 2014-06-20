    /*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "boxes/addcontactbox.h"
#include "application.h"
#include "window.h"
#include "settingswidget.h"
#include "mainwidget.h"
#include "boxes/confirmbox.h"

TopBarWidget::TopBarWidget(MainWidget *w) : QWidget(w),
    a_over(0), _drawShadow(true), _selCount(0), _selStrWidth(0), _animating(false),
	_clearSelection(this, lang(lng_selected_clear), st::topBarButton),
	_forward(this, lang(lng_selected_forward), st::topBarActionButton),
	_delete(this, lang(lng_selected_delete), st::topBarActionButton),
	_edit(this, lang(lng_profile_edit_contact), st::topBarButton),
	_leaveGroup(this, lang(lng_profile_delete_and_exit), st::topBarButton),
	_addContact(this, lang(lng_profile_add_contact), st::topBarButton),
	_deleteContact(this, lang(lng_profile_delete_contact), st::topBarButton) {

	connect(&_forward, SIGNAL(clicked()), this, SLOT(onForwardSelection()));
	connect(&_delete, SIGNAL(clicked()), this, SLOT(onDeleteSelection()));
	connect(&_clearSelection, SIGNAL(clicked()), this, SLOT(onClearSelection()));
	connect(&_addContact, SIGNAL(clicked()), this, SLOT(onAddContact()));
	connect(&_deleteContact, SIGNAL(clicked()), this, SLOT(onDeleteContact()));
	connect(&_edit, SIGNAL(clicked()), this, SLOT(onEdit()));
	connect(&_leaveGroup, SIGNAL(clicked()), this, SLOT(onDeleteAndExit()));

	setCursor(style::cur_pointer);
	showAll();
}

void TopBarWidget::onForwardSelection() {
	if (App::main()) App::main()->forwardSelectedItems();
}

void TopBarWidget::onDeleteSelection() {
	if (App::main()) App::main()->deleteSelectedItems();
}

void TopBarWidget::onClearSelection() {
	if (App::main()) App::main()->clearSelectedItems();
}

void TopBarWidget::onEdit() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	if (p) App::wnd()->showLayer(new AddContactBox(p));
}

void TopBarWidget::onAddContact() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = (p && !p->chat) ? p->asUser() : 0;
	if (u) App::wnd()->showLayer(new AddContactBox(u->firstName, u->lastName, u->phone));
}

void TopBarWidget::onDeleteContact() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = (p && !p->chat) ? p->asUser() : 0;
	if (u) {
		ConfirmBox *box = new ConfirmBox(lang(lng_sure_delete_contact).replace(qsl("{contact}"), p->name));
		connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteContactSure()));
		App::wnd()->showLayer(box);
	}
}

void TopBarWidget::onDeleteContactSure() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = (p && !p->chat) ? p->asUser() : 0;
	if (u) {
		App::main()->showPeer(0, true);
		App::wnd()->hideLayer();
		MTP::send(MTPcontacts_DeleteContact(u->inputUser), App::main()->rpcDone(&MainWidget::deletedContact, u));
	}
}

void TopBarWidget::onDeleteAndExit() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	ChatData *c = (p && p->chat) ? p->asChat() : 0;
	if (c) {
		ConfirmBox *box = new ConfirmBox(lang(lng_sure_delete_and_exit).replace(qsl("{group}"), p->name));
		connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteAndExitSure()));
		App::wnd()->showLayer(box);
	}
}

void TopBarWidget::onDeleteAndExitSure() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	ChatData *c = (p && p->chat) ? p->asChat() : 0;
	if (c) {
		App::main()->showPeer(0, true);
		App::wnd()->hideLayer();
		MTP::send(MTPmessages_DeleteChatUser(MTP_int(p->id & 0xFFFFFFFF), App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistory, p), App::main()->rpcFail(&MainWidget::leaveChatFailed, p));
	}
}

void TopBarWidget::enterEvent(QEvent *e) {
	a_over.start(1);
	anim::start(this);
}

void TopBarWidget::leaveEvent(QEvent *e) {
	a_over.start(0);
	anim::start(this);
}

bool TopBarWidget::animStep(float64 ms) {
	float64 dt = ms / st::topBarDuration;
	bool res = true;
	if (dt >= 1) {
		a_over.finish();
		res = false;
	} else {
		a_over.update(dt, anim::linear);
	}
	update();
	return res;
}

void TopBarWidget::enableShadow(bool enable) {
	_drawShadow = enable;
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (e->rect().top() < st::topBarHeight) {
		p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBG->b);
		if (_clearSelection.isHidden()) {
			p.save();
			main()->paintTopBar(p, a_over.current(), 0);
			p.restore();
		} else {
			p.setFont(st::linkFont->f);
			p.setPen(st::btnDefLink.color->p);
			p.drawText(st::topBarSelectedPos.x(), st::topBarSelectedPos.y() + st::linkFont->ascent, _selStr);
		}
	} else {
		int a = 0; // optimize shadow-only drawing
	}
	if (_drawShadow) {
		p.setPen(st::titleShadowColor->p);
		for (int32 i = 0; i < st::titleShadow; ++i) {
			p.drawLine(st::titleShadow, st::topBarHeight + i, width(), st::topBarHeight + i);
		}
	}
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	if (e->button() == Qt::LeftButton && e->pos().y() < st::topBarHeight && (p || !_selCount)) {
		emit clicked();
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	int32 r = width();
	if (!_forward.isHidden()) {
		int32 availX = st::topBarSelectedPos.x() + _selStrWidth, availW = r - (_clearSelection.width() + st::topBarButton.width / 2) - availX;
		_forward.move(availX + (availW - _forward.width() - _delete.width() - st::topBarActionSkip) / 2, (st::topBarHeight - _forward.height()) / 2);
		_delete.move(availX + (availW + _forward.width() - _delete.width() + st::topBarActionSkip) / 2, (st::topBarHeight - _forward.height()) / 2);
	}
	if (!_clearSelection.isHidden()) _clearSelection.move(r -= _clearSelection.width(), 0);
	if (!_deleteContact.isHidden()) _deleteContact.move(r -= _deleteContact.width(), 0);
	if (!_leaveGroup.isHidden()) _leaveGroup.move(r -= _leaveGroup.width(), 0);
	if (!_edit.isHidden()) _edit.move(r -= _edit.width(), 0);
	if (!_addContact.isHidden()) _addContact.move(r -= _addContact.width(), 0);
}

void TopBarWidget::startAnim() {
	_edit.hide();
	_leaveGroup.hide();
	_addContact.hide();
	_deleteContact.hide();
    _clearSelection.hide();
    _delete.hide();
    _forward.hide();
    _animating = true;
}

void TopBarWidget::stopAnim() {
    _animating = false;
    showAll();
}

void TopBarWidget::showAll() {
    if (_animating) {
        resizeEvent(0);
        return;
    }
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	if (p && (p->chat || p->asUser()->contact >= 0)) {
		if (p->chat) {
			if (p->asChat()->forbidden) {
				_edit.hide();
			} else {
				_edit.show();
			}
			_leaveGroup.show();
			_addContact.hide();
			_deleteContact.hide();
		} else if (p->asUser()->contact > 0) {
			_edit.show();
			_leaveGroup.hide();
			_addContact.hide();
			_deleteContact.show();
		} else {
			_edit.hide();
			_leaveGroup.hide();
			_addContact.show();
			_deleteContact.hide();
		}
		_clearSelection.hide();
		_delete.hide();
		_forward.hide();
	} else {
		_edit.hide();
		_leaveGroup.hide();
		_addContact.hide();
		_deleteContact.hide();
		if (!p && _selCount) {
			_clearSelection.show();
			_delete.show();
			_forward.show();
		} else {
			_clearSelection.hide();
			_delete.hide();
			_forward.hide();
		}
	}
	resizeEvent(0);
}

void TopBarWidget::showSelected(uint32 selCount) {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	_selCount = selCount;
	_selStr = (_selCount > 0) ? lang((_selCount == 1) ? lng_selected_count_1 : lng_selected_count_5).arg(_selCount) : QString();
	_selStrWidth = st::btnDefLink.font->m.width(_selStr);
	setCursor((!p && _selCount) ? style::cur_default : style::cur_pointer);
	showAll();
}

MainWidget *TopBarWidget::main() {
	return static_cast<MainWidget*>(parentWidget());
}

MainWidget::MainWidget(Window *window) : QWidget(window), failedObjId(0), _dialogsWidth(st::dlgMinWidth),
	dialogs(this), history(this), profile(0), _topBar(this), hider(0),
    updPts(0), updDate(0), updQts(0), updSeq(0), updInited(false), onlineRequest(0) {
	setGeometry(QRect(0, st::titleHeight, App::wnd()->width(), App::wnd()->height() - st::titleHeight));

	connect(window, SIGNAL(resized(const QSize &)), this, SLOT(onParentResize(const QSize &)));
	connect(&dialogs, SIGNAL(peerChosen(const PeerId &)), this, SLOT(showPeer(const PeerId &)));
	connect(&dialogs, SIGNAL(cancelled()), this, SLOT(dialogsCancelled()));
	connect(&history, SIGNAL(cancelled()), &dialogs, SLOT(activate()));
	connect(this, SIGNAL(peerPhotoChanged(PeerData *)), this, SIGNAL(dialogsUpdated()));
	connect(&noUpdatesTimer, SIGNAL(timeout()), this, SLOT(getDifference()));
	connect(&onlineTimer, SIGNAL(timeout()), this, SLOT(setOnline()));
	connect(&onlineUpdater, SIGNAL(timeout()), this, SLOT(updateOnlineDisplay()));
	connect(this, SIGNAL(peerUpdated(PeerData*)), &history, SLOT(peerUpdated(PeerData*)));
	connect(&_topBar, SIGNAL(clicked()), this, SLOT(onTopBarClick()));
	connect(&history, SIGNAL(peerShown(PeerData*)), this, SLOT(onPeerShown(PeerData*)));
	connect(&updateNotifySettingTimer, SIGNAL(timeout()), this, SLOT(onUpdateNotifySettings()));

	noUpdatesTimer.setSingleShot(true);
	onlineTimer.setSingleShot(true);
	onlineUpdater.setSingleShot(true);
	updateNotifySettingTimer.setSingleShot(true);

	dialogs.show();
	history.show();
	_topBar.hide();

	_topBar.raise();
	dialogs.raise();

	MTP::setGlobalFailHandler(rpcFail(&MainWidget::updateFail));

	show();
	setFocus();
}

mtpRequestId MainWidget::onForward(const PeerId &peer, bool forwardSelected) {
	return history.onForward(peer, forwardSelected);
}

void MainWidget::onShareContact(const PeerId &peer, UserData *contact) {
	history.onShareContact(peer, contact);
}

void MainWidget::noHider(HistoryHider *destroyed) {
	if (hider == destroyed) {
		hider = 0;
	}
}

void MainWidget::forwardLayer(bool forwardSelected) {
	hider = new HistoryHider(this, forwardSelected);
	hider->show();
	resizeEvent(0);
	dialogs.activate();
}

void MainWidget::deleteLayer(int32 selectedCount) {
	QString str(lang((selectedCount < -1) ? lng_selected_cancel_sure_this : (selectedCount < 0 ? lng_selected_delete_sure_this : (selectedCount > 1 ? lng_selected_delete_sure_5 : lng_selected_delete_sure_1))));
	ConfirmBox *box = new ConfirmBox((selectedCount < 0) ? str : str.arg(selectedCount), lang(lng_selected_delete_confirm));
	if (selectedCount < 0) {
		connect(box, SIGNAL(confirmed()), &history, SLOT(onDeleteContextSure()));
	} else {
		connect(box, SIGNAL(confirmed()), &history, SLOT(onDeleteSelectedSure()));
	}
	App::wnd()->showLayer(box);
}

void MainWidget::shareContactLayer(UserData *contact) {
	hider = new HistoryHider(this, contact);
	hider->show();
	resizeEvent(0);
	dialogs.activate();
}

bool MainWidget::selectingPeer() {
	return !!hider;
}

void MainWidget::offerPeer(PeerId peer) {
	hider->offerPeer(peer);
}

void MainWidget::hidePeerSelect() {
	hider->startHide();
}

void MainWidget::focusPeerSelect() {
	hider->setFocus();
}

void MainWidget::dialogsActivate() {
	dialogs.activate();
}

bool MainWidget::leaveChatFailed(PeerData *peer, const RPCError &e) {
	if (e.type() == "CHAT_ID_INVALID") { // left this chat already
		if ((profile && profile->peer() == peer) || profileStack.indexOf(peer) >= 0 || history.peer() == peer) {
			showPeer(0);
		}
		dialogs.removePeer(peer);
		MTP::send(MTPmessages_DeleteHistory(peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, peer));
		return true;
	}
	return false;
}

void MainWidget::deleteHistory(PeerData *peer, const MTPmessages_StatedMessage &result) {
	sentFullDataReceived(0, result);
	if ((profile && profile->peer() == peer) || profileStack.indexOf(peer) >= 0 || history.peer() == peer) {
		showPeer(0);
	}
	dialogs.removePeer(peer);
	MTP::send(MTPmessages_DeleteHistory(peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, peer));
}

void MainWidget::deleteHistoryPart(PeerData *peer, const MTPmessages_AffectedHistory &result) {
	const MTPDmessages_affectedHistory &d(result.c_messages_affectedHistory());
	App::main()->updUpdated(d.vpts.v, 0, 0, d.vseq.v);

	int32 offset = d.voffset.v;
	if (!MTP::authedId() || offset <= 0) return;

	MTP::send(MTPmessages_DeleteHistory(peer->input, d.voffset), rpcDone(&MainWidget::deleteHistoryPart, peer));
}

void MainWidget::deletedContact(UserData *user, const MTPcontacts_Link &result) {
	const MTPDcontacts_link &d(result.c_contacts_link());
	App::feedUsers(MTP_vector<MTPUser>(QVector<MTPUser>(1, d.vuser)));
	App::feedUserLink(MTP_int(user->id & 0xFFFFFFFF), d.vmy_link, d.vforeign_link);
}

void MainWidget::deleteHistoryAndContact(UserData *user, const MTPcontacts_Link &result) {
	const MTPDcontacts_link &d(result.c_contacts_link());
	App::feedUsers(MTP_vector<MTPUser>(QVector<MTPUser>(1, d.vuser)));
	App::feedUserLink(MTP_int(user->id & 0xFFFFFFFF), d.vmy_link, d.vforeign_link);

	if ((profile && profile->peer() == user) || profileStack.indexOf(user) >= 0 || history.peer() == user) {
		showPeer(0);
	}
	dialogs.removePeer(user);
	MTP::send(MTPmessages_DeleteHistory(user->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, (PeerData*)user));
}

void MainWidget::clearHistory(PeerData *peer) {
	if (!peer->chat && peer->asUser()->contact <= 0) {
		dialogs.removePeer(peer->asUser());
	}
	dialogs.dialogsToUp();
	dialogs.update();
	App::history(peer->id)->clear();
	MTP::send(MTPmessages_DeleteHistory(peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, peer));
}

void MainWidget::removeContact(UserData *user) {
	dialogs.removeContact(user);
}

void MainWidget::addParticipants(ChatData *chat, const QVector<UserData*> &users) {
	for (QVector<UserData*>::const_iterator i = users.cbegin(), e = users.cend(); i != e; ++i) {
		MTP::send(MTPmessages_AddChatUser(MTP_int(chat->id & 0xFFFFFFFF), (*i)->inputUser, MTP_int(ForwardOnAdd)), rpcDone(&MainWidget::addParticipantDone, chat), rpcFail(&MainWidget::addParticipantFail, chat), 0, 5);
	}
	App::wnd()->hideLayer();
	showPeer(chat->id, false);
}

void MainWidget::addParticipantDone(ChatData *chat, const MTPmessages_StatedMessage &result) {
	sentFullDataReceived(0, result);
}

bool MainWidget::addParticipantFail(ChatData *chat, const RPCError &e) {
	if (e.type() == "USER_LEFT_CHAT") { // trying to return banned user to his group
	}
	return false;
}

void MainWidget::kickParticipant(ChatData *chat, UserData *user) {
	MTP::send(MTPmessages_DeleteChatUser(MTP_int(chat->id & 0xFFFFFFFF), user->inputUser), rpcDone(&MainWidget::kickParticipantDone, chat), rpcFail(&MainWidget::kickParticipantFail, chat));
	App::wnd()->hideLayer();
	showPeer(chat->id, false);
}

void MainWidget::kickParticipantDone(ChatData *chat, const MTPmessages_StatedMessage &result) {
	sentFullDataReceived(0, result);
}

bool MainWidget::kickParticipantFail(ChatData *chat, const RPCError &e) {
	e.type();
	return false;
}

void MainWidget::checkPeerHistory(PeerData *peer) {
	MTP::send(MTPmessages_GetHistory(peer->input, MTP_int(0), MTP_int(0), MTP_int(1)), rpcDone(&MainWidget::checkedHistory, peer));
}

void MainWidget::checkedHistory(PeerData *peer, const MTPmessages_Messages &result) {
	const QVector<MTPMessage> *v = 0;
	if (result.type() == mtpc_messages_messages) {
		const MTPDmessages_messages &d(result.c_messages_messages());
		App::feedChats(d.vchats);
		App::feedUsers(d.vusers);
		v = &d.vmessages.c_vector().v;
	} else if (result.type() == mtpc_messages_messagesSlice) {
		const MTPDmessages_messagesSlice &d(result.c_messages_messagesSlice());
		App::feedChats(d.vchats);
		App::feedUsers(d.vusers);
		v = &d.vmessages.c_vector().v;
	}
	if (!v) return;

	if (v->isEmpty()) {
		if ((profile && profile->peer() == peer) || profileStack.indexOf(peer) >= 0 || history.peer() == peer) {
			showPeer(0);
		}
		dialogs.removePeer(peer);
	} else {
		if (App::historyLoaded(peer->id)) {
			History *h = App::history(peer->id);
			h->addToBack((*v)[0], false);
		}
	}
}

void MainWidget::forwardSelectedItems() {
	history.onForwardSelected();
}

void MainWidget::deleteSelectedItems() {
	history.onDeleteSelected();
}

void MainWidget::clearSelectedItems() {
	history.onClearSelected();
}

DialogsIndexed &MainWidget::contactsList() {
	return dialogs.contactsList();
}

void MainWidget::sendMessage(History *hist, const QString &text) {
    readServerHistory(hist);
    QString msg = history.prepareMessage(text);
	if (!msg.isEmpty()) {
		MsgId newId = clientMsgId();
		uint64 randomId = MTP::nonce<uint64>();
        
		App::historyRegRandom(randomId, newId);
        
		MTPstring msgText(MTP_string(msg));
		hist->addToBack(MTP_message(MTP_int(newId), MTP_int(MTP::authedId()), App::peerToMTP(hist->peer->id), MTP_bool(true), MTP_bool(true), MTP_int(unixtime()), msgText, MTP_messageMediaEmpty()));
		historyToDown(hist);
		if (history.peer() == hist->peer) {
            history.peerMessagesUpdated();
        }
        
		MTP::send(MTPmessages_SendMessage(hist->peer->input, msgText, MTP_long(randomId)), App::main()->rpcDone(&MainWidget::sentDataReceived, randomId));
    }
}

void MainWidget::readServerHistory(History *hist, bool force) {
    if (!hist || (!force && (!hist->unreadCount || !hist->unreadLoaded))) return;
    
    ReadRequests::const_iterator i = _readRequests.constFind(hist->peer);
    if (i == _readRequests.cend()) {
        hist->inboxRead(true);
        _readRequests.insert(hist->peer, MTP::send(MTPmessages_ReadHistory(hist->peer->input, MTP_int(0), MTP_int(0)), rpcDone(&MainWidget::partWasRead, hist->peer)));
    }
}

void MainWidget::partWasRead(PeerData *peer, const MTPmessages_AffectedHistory &result) {
	const MTPDmessages_affectedHistory &d(result.c_messages_affectedHistory());
	App::main()->updUpdated(d.vpts.v, 0, 0, d.vseq.v);
    
	int32 offset = d.voffset.v;
	if (!MTP::authedId() || offset <= 0) {
        _readRequests.remove(peer);
    } else {
        _readRequests[peer] = MTP::send(MTPmessages_ReadHistory(peer->input, MTP_int(0), MTP_int(offset)), rpcDone(&MainWidget::partWasRead, peer));
    }
}

void MainWidget::videoLoadProgress(mtpFileLoader *loader) {
	VideoData *video = App::video(loader->objId());
	if (video->loader) {
		if (video->loader->done()) {
			video->finish();
			QString already = video->already();
			if (!already.isEmpty() && video->openOnSave) {
				psOpenFile(already, video->openOnSave < 0);
			}
		}
	}
	const VideoItems &items(App::videoItems());
	VideoItems::const_iterator i = items.constFind(video);
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			history.msgUpdated(j.key()->history()->peer->id, j.key());
		}
	}
}

void MainWidget::loadFailed(mtpFileLoader *loader, bool started, const char *retrySlot) {
	failedObjId = loader->objId();
	failedFileName = loader->fileName();
	ConfirmBox *box = new ConfirmBox(lang(started ? lng_download_finish_failed : lng_download_path_failed), started ? QString() : lang(lng_download_path_settings));
	if (started) {
		connect(box, SIGNAL(confirmed()), this, retrySlot);
	} else {
		connect(box, SIGNAL(confirmed()), App::wnd(), SLOT(showSettings()));
	}
	App::wnd()->showLayer(box);
}

void MainWidget::videoLoadFailed(mtpFileLoader *loader, bool started) {
	loadFailed(loader, started, SLOT(videoLoadRetry()));
	VideoData *video = App::video(loader->objId());
	if (video && video->loader) video->finish();
}

void MainWidget::videoLoadRetry() {
	App::wnd()->hideLayer();
	VideoData *video = App::video(failedObjId);
	if (video) video->save(failedFileName);
}

void MainWidget::audioLoadProgress(mtpFileLoader *loader) {
	AudioData *audio = App::audio(loader->objId());
	if (audio->loader) {
		if (audio->loader->done()) {
			audio->finish();
			QString already = audio->already();
			if (!already.isEmpty() && audio->openOnSave) {
				psOpenFile(already, audio->openOnSave < 0);
			}
		}
	}
	const AudioItems &items(App::audioItems());
	AudioItems::const_iterator i = items.constFind(audio);
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			history.msgUpdated(j.key()->history()->peer->id, j.key());
		}
	}
}

void MainWidget::audioLoadFailed(mtpFileLoader *loader, bool started) {
	loadFailed(loader, started, SLOT(audioLoadRetry()));
	AudioData *audio = App::audio(loader->objId());
	if (audio && audio->loader) audio->finish();
}

void MainWidget::audioLoadRetry() {
	App::wnd()->hideLayer();
	AudioData *audio = App::audio(failedObjId);
	if (audio) audio->save(failedFileName);
}

void MainWidget::documentLoadProgress(mtpFileLoader *loader) {
	DocumentData *document = App::document(loader->objId());
	if (document->loader) {
		if (document->loader->done()) {
			document->finish();
			QString already = document->already();
			if (!already.isEmpty() && document->openOnSave) {
				psOpenFile(already, document->openOnSave < 0);
			}
		}
	}
	const DocumentItems &items(App::documentItems());
	DocumentItems::const_iterator i = items.constFind(document);
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			history.msgUpdated(j.key()->history()->peer->id, j.key());
		}
	}
}

void MainWidget::documentLoadFailed(mtpFileLoader *loader, bool started) {
	loadFailed(loader, started, SLOT(documentLoadRetry()));
	DocumentData *document = App::document(loader->objId());
	if (document && document->loader) document->finish();
}

void MainWidget::documentLoadRetry() {
	App::wnd()->hideLayer();
	DocumentData *document = App::document(failedObjId);
	if (document) document->save(failedFileName);
}

void MainWidget::onParentResize(const QSize &newSize) {
	resize(newSize);
}

void MainWidget::updateOnlineDisplay() {
	history.updateOnlineDisplay(history.x(), width() - history.x() - st::sysBtnDelta * 2 - st::sysCls.img.pxWidth() - st::sysRes.img.pxWidth() - st::sysMin.img.pxWidth());
	if (profile) profile->updateOnlineDisplay();
	if (App::wnd()->settingsWidget()) App::wnd()->settingsWidget()->updateOnlineDisplay();
}

void MainWidget::confirmSendImage(const ReadyLocalMedia &img) {
	history.confirmSendImage(img);
}

void MainWidget::cancelSendImage() {
	history.cancelSendImage();
}

void MainWidget::dialogsCancelled() {
	if (hider) {
		hider->startHide();
	} else {
		history.activate();
	}
}

void MainWidget::setInnerFocus() {
	if (hider || !history.peer()) {
		if (hider && hider->wasOffered()) {
			hider->setFocus();
		} else {
			dialogs.setInnerFocus();
		}
	} else {
		history.activate();
	}
}

void MainWidget::createDialogAtTop(History *history, int32 unreadCount) {
	dialogs.createDialogAtTop(history, unreadCount);
}

bool MainWidget::getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const {
	if (history.getPhotoCoords(photo, x, y, w)) {
		x += history.x();
		y += history.y();
		return true;
	} else if (profile && profile->getPhotoCoords(photo, x, y, w)) {
		x += profile->x();
		y += profile->y();
		return true;
	}
	return false;
}

bool MainWidget::getVideoCoords(VideoData *video, int32 &x, int32 &y, int32 &w) const {
	if (history.getVideoCoords(video, x, y, w)) {
		x += history.x();
		y += history.y();
		return true;
	}
	return false;
}

void MainWidget::showPeer(const PeerId &peerId, bool back, bool force) {
	if (!back && profileStack.size() == 1 && profileStack[0]->id == peerId) {
		back = true;
	}
	App::wnd()->hideLayer();
	QPixmap animCache, animTopBarCache;
	if (force && hider) {
		hider->startHide();
		hider = 0;
	}
	if (force || !selectingPeer()) {
		if (history.isHidden() && profile) {
			dialogs.enableShadow(false);
			if (peerId) {
				_topBar.enableShadow(false);
				animCache = myGrab(this, history.geometry());
			} else {
				animCache = myGrab(this, QRect(_dialogsWidth, 0, width() - _dialogsWidth, height()));
			}
			animTopBarCache = myGrab(this, QRect(_topBar.x(), _topBar.y(), _topBar.width(), st::topBarHeight));
			dialogs.enableShadow();
			_topBar.enableShadow();
			history.show();
		}
	}
	history.showPeer(peerId, force);
	if (force || !selectingPeer()) {
		if (profile) {
			if (profile) profile->deleteLater();
			profile = 0;
			profileStack.clear();
			if (!history.peer() || !history.peer()->id) {
				_topBar.hide();
				resizeEvent(0);
			}
			if (!animCache.isNull()) {
				history.animShow(animCache, animTopBarCache, back);
			}
		}
	}
	dialogs.scrollToPeer(peerId);
	dialogs.update();
}

PeerData *MainWidget::peerBefore(const PeerData *peer) {
	if (selectingPeer()) return 0;
	return dialogs.peerBefore(peer);
}

PeerData *MainWidget::peerAfter(const PeerData *peer) {
	if (selectingPeer()) return 0;
	return dialogs.peerAfter(peer);
}

PeerData *MainWidget::peer() {
	return history.peer();
}

PeerData *MainWidget::activePeer() {
	return history.activePeer();
}

PeerData *MainWidget::profilePeer() {
	return profile ? profile->peer() : 0;
}

void MainWidget::showPeerProfile(const PeerData *peer, bool back) {
	dialogs.enableShadow(false);
	_topBar.enableShadow(false);
	QPixmap animCache = myGrab(this, history.geometry()), animTopBarCache = myGrab(this, QRect(_topBar.x(), _topBar.y(), _topBar.width(), st::topBarHeight));
	dialogs.enableShadow();
	_topBar.enableShadow();
	if (!back) {
		if (profile) {
			profileStack.push_back(profile->peer());
		} else {
			profileStack.push_back(history.peer());
		}
	}
	if (profile) profile->deleteLater();
	profile = new ProfileWidget(this, peer);
	_topBar.show();
	resizeEvent(0);
	profile->animShow(animCache, animTopBarCache, back);
	history.animStop();
	history.showPeer(0, false, true);
	history.hide();
	_topBar.raise();
	dialogs.raise();
	if (hider) hider->raise();
}

void MainWidget::showPeerBack() {
	if (profileStack.isEmpty() || selectingPeer()) return;
	PeerData *peer = profileStack.back();
	profileStack.pop_back();
	if (profileStack.isEmpty()) {
		showPeer(peer->id, true);
	} else {
		showPeerProfile(peer, true);
	}
}

QRect MainWidget::historyRect() const {
	QRect r(history.historyRect());
	r.moveLeft(r.left() + history.x());
	r.moveTop(r.top() + history.y());
	return r;
}

void MainWidget::dlgUpdated(DialogRow *row) {
	dialogs.dlgUpdated(row);
}

void MainWidget::dlgUpdated(History *row) {
	dialogs.dlgUpdated(row);
}

void MainWidget::windowShown() {
	history.windowShown();
}

void MainWidget::sentDataReceived(uint64 randomId, const MTPmessages_SentMessage &result) {
	switch (result.type()) {
	case mtpc_messages_sentMessage: {
		const MTPDmessages_sentMessage &d(result.c_messages_sentMessage());

		if (updInited && d.vseq.v) {
			if (d.vseq.v <= updSeq) return;
			if (d.vseq.v > updSeq + 1) return getDifference();
		}

		feedUpdate(MTP_updateMessageID(d.vid, MTP_long(randomId))); // ignore real date
		if (updInited) {
			updSetState(d.vpts.v, d.vdate.v, updQts, d.vseq.v);
		}
	} break;

	case mtpc_messages_sentMessageLink: {
		const MTPDmessages_sentMessageLink &d(result.c_messages_sentMessageLink());

		if (updInited && d.vseq.v) {
			if (d.vseq.v <= updSeq) return;
			if (d.vseq.v > updSeq + 1) return getDifference();
		}

		feedUpdate(MTP_updateMessageID(d.vid, MTP_long(randomId))); // ignore real date
		if (updInited) {
			updSetState(d.vpts.v, d.vdate.v, updQts, d.vseq.v);
		}

		App::feedUserLinks(d.vlinks);
	} break;
	};
}

void MainWidget::sentFullDataReceived(uint64 randomId, const MTPmessages_StatedMessage &result) {
	if (randomId) {
		const MTPMessage *msg = 0;
		switch (result.type()) {
		case mtpc_messages_statedMessage: msg = &result.c_messages_statedMessage().vmessage; break;
		case mtpc_messages_statedMessageLink: msg = &result.c_messages_statedMessageLink().vmessage; break;
		}
		if (msg) {
			MsgId msgId = 0;
			switch (msg->type()) {
			case mtpc_message: msgId = msg->c_message().vid.v; break;
			case mtpc_messageEmpty: msgId = msg->c_messageEmpty().vid.v; break;
			case mtpc_messageForwarded: msgId = msg->c_messageForwarded().vid.v; break;
			case mtpc_messageService: msgId = msg->c_messageService().vid.v; break;
			}
			if (msgId) {
				feedUpdate(MTP_updateMessageID(MTP_int(msgId), MTP_long(randomId))); // ignore real date
				App::feedMessageMedia(msgId, *msg);
			}
		}
	}

	switch (result.type()) {
	case mtpc_messages_statedMessage: {
		const MTPDmessages_statedMessage &d(result.c_messages_statedMessage());

		if (updInited && d.vseq.v) {
			if (d.vseq.v <= updSeq || d.vseq.v > updSeq + 1) return getDifference();
		}

		App::feedChats(d.vchats);
		App::feedUsers(d.vusers);
		if (!randomId) {
			feedUpdate(MTP_updateNewMessage(d.vmessage, d.vpts));
		}
		if (updInited) {
			updSetState(d.vpts.v, updDate, updQts, d.vseq.v);
		}
	} break;

	case mtpc_messages_statedMessageLink: {
		const MTPDmessages_statedMessageLink &d(result.c_messages_statedMessageLink());

		if (updInited && d.vseq.v) {
			if (d.vseq.v <= updSeq || d.vseq.v > updSeq + 1) return getDifference();
		}

		App::feedChats(d.vchats);
		App::feedUsers(d.vusers);
		if (!randomId) {
			feedUpdate(MTP_updateNewMessage(d.vmessage, d.vpts));
		}
		if (updInited) {
			updSetState(d.vpts.v, updDate, updQts, d.vseq.v);
		}

		App::feedUserLinks(d.vlinks);
	} break;
	};
}

void MainWidget::sentFullDatasReceived(const MTPmessages_StatedMessages &result) {
	switch (result.type()) {
	case mtpc_messages_statedMessages: {
		const MTPDmessages_statedMessages &d(result.c_messages_statedMessages());
		if (updInited && d.vseq.v) {
			if (d.vseq.v <= updSeq || d.vseq.v > updSeq + 1) return getDifference();
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, true);
		history.peerMessagesUpdated();

		if (updInited) {
			updSetState(d.vpts.v, updDate, updQts, d.vseq.v);
		}
	} break;

	case mtpc_messages_statedMessagesLinks: {
		const MTPDmessages_statedMessagesLinks &d(result.c_messages_statedMessagesLinks());

		if (updInited && d.vseq.v) {
			if (d.vseq.v <= updSeq || d.vseq.v > updSeq + 1) return getDifference();
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, true);
		history.peerMessagesUpdated();
		if (updInited) {
			updSetState(d.vpts.v, updDate, updQts, d.vseq.v);
		}

		App::feedUserLinks(d.vlinks);
	} break;
	};
}

void MainWidget::forwardDone(PeerId peer, const MTPmessages_StatedMessages &result) {
	sentFullDatasReceived(result);
	if (hider) hider->forwardDone();
	showPeer(peer, false, true);
	history.onClearSelected();
}

void MainWidget::msgUpdated(PeerId peer, HistoryItem *msg) {
	history.msgUpdated(peer, msg);
	if (!msg->history()->dialogs.isEmpty()) {
		dialogs.dlgUpdated(msg->history()->dialogs[0]);
	}
}

void MainWidget::historyToDown(History *hist) {
	history.historyToDown(hist);
}

void MainWidget::dialogsToUp() {
	dialogs.dialogsToUp();
}

void MainWidget::dialogsClear() {
	dialogs.onCancel();
}

void MainWidget::newUnreadMsg(History *hist, MsgId msgId) {
	history.newUnreadMsg(hist, msgId);
}

void MainWidget::historyWasRead() {
	history.historyWasRead(false);
}

void MainWidget::animShow(const QPixmap &bgAnimCache, bool back) {
	_bgAnimCache = bgAnimCache;

	anim::stop(this);
	showAll();
	_animCache = myGrab(this, rect());

	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);

	hideAll();
	anim::start(this);
	show();
}

bool MainWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		res = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();

		_animCache = _bgAnimCache = QPixmap();

		anim::stop(this);
		showAll();
		activate();
	} else {
		a_bgCoord.update(dt1, st::introHideFunc);
		a_bgAlpha.update(dt1, st::introAlphaHideFunc);
		a_coord.update(dt2, st::introShowFunc);
		a_alpha.update(dt2, st::introAlphaShowFunc);
	}
	update();
	return res;
}

void MainWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (animating()) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
	} else {
	}
}

void MainWidget::hideAll() {
	dialogs.hide();
	history.hide();
	if (profile) {
		profile->hide();
	}
	_topBar.hide();
}

void MainWidget::showAll() {
	dialogs.show();
	if (profile) {
		profile->show();
	} else {
		history.show();
	}
	if (profile || history.peer()) {
		_topBar.show();
	}
	App::wnd()->checkHistoryActivation();
}

void MainWidget::resizeEvent(QResizeEvent *e) {
	_dialogsWidth = snap<int>((width()  * 5) / 14, st::dlgMinWidth, st::dlgMaxWidth);
	int32 tbh = _topBar.isHidden() ? 0 : st::topBarHeight;
	dialogs.setGeometry(0, 0, _dialogsWidth + st::dlgShadow, height());
	_topBar.setGeometry(_dialogsWidth, 0, width() - _dialogsWidth, st::topBarHeight + st::titleShadow);
	history.setGeometry(_dialogsWidth, tbh, width() - _dialogsWidth, height() - tbh);
	if (profile) profile->setGeometry(history.geometry());
	if (hider) hider->setGeometry(QRect(_dialogsWidth, 0, width() - _dialogsWidth, height()));
}

void MainWidget::keyPressEvent(QKeyEvent *e) {
}

void MainWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (profile) {
		profile->paintTopBar(p, over, decreaseWidth);
	} else {
		history.paintTopBar(p, over, decreaseWidth);
	}
}

TopBarWidget *MainWidget::topBar() {
	return &_topBar;
}

void MainWidget::onTopBarClick() {
	if (profile) {
		profile->topBarClick();
	} else {
		history.topBarClick();
	}
}

void MainWidget::onPeerShown(PeerData *peer) {
	if (profile || (peer && peer->id)) {
		_topBar.show();
	} else {
		_topBar.hide();
	}
	resizeEvent(0);
}

void MainWidget::onUpdateNotifySettings() {
	while (!updateNotifySettingPeers.isEmpty()) {
		PeerData *peer = *updateNotifySettingPeers.begin();
		updateNotifySettingPeers.erase(updateNotifySettingPeers.begin());

		if (peer->notify == UnknownNotifySettings || peer->notify == EmptyNotifySettings) {
			peer->notify = new NotifySettings();
		}
		MTP::send(MTPaccount_UpdateNotifySettings(MTP_inputNotifyPeer(peer->input), MTP_inputPeerNotifySettings(MTP_int(peer->notify->mute), MTP_string(peer->notify->sound), MTP_bool(peer->notify->previews), MTP_int(peer->notify->events))), RPCResponseHandler(), 0, updateNotifySettingPeers.isEmpty() ? 0 : 10);
	}
}

void MainWidget::feedUpdates(const MTPVector<MTPUpdate> &updates, bool skipMessageIds) {
	const QVector<MTPUpdate> &v(updates.c_vector().v);
	for (QVector<MTPUpdate>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
		if (skipMessageIds && i->type() == mtpc_updateMessageID) continue;
		feedUpdate(*i);
	}
}

void MainWidget::feedMessageIds(const MTPVector<MTPUpdate> &updates) {
	const QVector<MTPUpdate> &v(updates.c_vector().v);
	for (QVector<MTPUpdate>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
		if (i->type() == mtpc_updateMessageID) {
			feedUpdate(*i);
		}
	}
}

bool MainWidget::updateFail(const RPCError &e) {
	if (MTP::authedId()) {
		App::logOut();
	}
	return true;
}

void MainWidget::updSetState(int32 pts, int32 date, int32 qts, int32 seq) {
	if (updPts < pts) updPts = pts;
	if (updDate < date) updDate = date;
	if (updQts < qts) updQts = qts;
	if (seq) updSeq = seq;
}

void MainWidget::gotState(const MTPupdates_State &state) {
	const MTPDupdates_state &d(state.c_updates_state());
	updSetState(d.vpts.v, d.vdate.v, d.vqts.v, d.vseq.v);

	MTP::setGlobalDoneHandler(rpcDone(&MainWidget::updateReceived));
	noUpdatesTimer.start(NoUpdatesTimeout);
	updInited = true;

	dialogs.loadDialogs();
	setOnline();
}

void MainWidget::gotDifference(const MTPupdates_Difference &diff) {
	switch (diff.type()) {
	case mtpc_updates_differenceEmpty: {
		const MTPDupdates_differenceEmpty &d(diff.c_updates_differenceEmpty());
		updSetState(updPts, d.vdate.v, updQts, d.vseq.v);

		MTP::setGlobalDoneHandler(rpcDone(&MainWidget::updateReceived));
		noUpdatesTimer.start(NoUpdatesTimeout);
		updInited = true;
	} break;
	case mtpc_updates_differenceSlice: {
		const MTPDupdates_differenceSlice &d(diff.c_updates_differenceSlice());
		feedDifference(d.vusers, d.vchats, d.vnew_messages, d.vother_updates);

		const MTPDupdates_state &s(d.vintermediate_state.c_updates_state());
		updSetState(s.vpts.v, s.vdate.v, s.vqts.v, s.vseq.v);

		updInited = true;

		getDifference();
	} break;
	case mtpc_updates_difference: {
		const MTPDupdates_difference &d(diff.c_updates_difference());
		feedDifference(d.vusers, d.vchats, d.vnew_messages, d.vother_updates);

		gotState(d.vstate);
	} break;
	};
}

void MainWidget::updUpdated(int32 pts, int32 date, int32 qts, int32 seq) {
	if (!updInited) return;
	if (seq && (seq < updSeq || seq > updSeq + 1)) return getDifference();
	updSetState(pts, date, qts, seq);
}

void MainWidget::feedDifference(const MTPVector<MTPUser> &users, const MTPVector<MTPChat> &chats, const MTPVector<MTPMessage> &msgs, const MTPVector<MTPUpdate> &other) {
	App::feedUsers(users);
	App::feedChats(chats);
	feedMessageIds(other);
	App::feedMsgs(msgs, true);
	feedUpdates(other, true);
	history.peerMessagesUpdated();
}

bool MainWidget::failDifference(const RPCError &e) {
	LOG(("RPC Error: %1 %2: %3").arg(e.code()).arg(e.type()).arg(e.description()));
	if (MTP::authedId()) {
		updInited = true;
		getDifference();
	}
	return true;
}

void MainWidget::getDifference() {
	if (!updInited) return;
	updInited = false;
	MTP::setGlobalDoneHandler(RPCDoneHandlerPtr(0));
	MTP::send(MTPupdates_GetDifference(MTP_int(updPts), MTP_int(updDate), MTP_int(updQts)), rpcDone(&MainWidget::gotDifference), rpcFail(&MainWidget::failDifference));
}

void MainWidget::start(const MTPUser &user) {
	MTP::authed(user.c_userSelf().vid.v);
	App::initMedia();
	App::feedUsers(MTP_vector<MTPUser>(QVector<MTPUser>(1, user)));
	App::app()->startUpdateCheck();
	MTP::send(MTPupdates_GetState(), rpcDone(&MainWidget::gotState));
	update();
}

void MainWidget::startFull(const MTPVector<MTPUser> &users) {
	const QVector<MTPUser> &v(users.c_vector().v);
	if (v.isEmpty() || v[0].type() != mtpc_userSelf) { // wtf?..
		return App::logOut();
	}
	start(v[0]);
}

void MainWidget::applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *history) {
	switch (settings.type()) {
	case mtpc_peerNotifySettingsEmpty:
		switch (peer.type()) {
		case mtpc_notifyAll: globalNotifyAllPtr = EmptyNotifySettings; break;
		case mtpc_notifyUsers: globalNotifyUsersPtr = EmptyNotifySettings; break;
		case mtpc_notifyChats: globalNotifyChatsPtr = EmptyNotifySettings; break;
		case mtpc_notifyPeer: {
			PeerData *data = App::peerLoaded(App::peerFromMTP(peer.c_notifyPeer().vpeer));
			if (data && data->notify != EmptyNotifySettings) {
				if (data->notify != UnknownNotifySettings) {
					delete data->notify;
				}
				data->notify = EmptyNotifySettings;
				App::history(data->id)->setMute(false);
			}
		} break;
		}
	break;
	case mtpc_peerNotifySettings: {
		const MTPDpeerNotifySettings &d(settings.c_peerNotifySettings());
		NotifySettingsPtr setTo = UnknownNotifySettings;
		PeerId peerId = 0;
		switch (peer.type()) {
		case mtpc_notifyAll: setTo = globalNotifyAllPtr = &globalNotifyAll; break;
		case mtpc_notifyUsers: setTo = globalNotifyUsersPtr = &globalNotifyUsers; break;
		case mtpc_notifyChats: setTo = globalNotifyChatsPtr = &globalNotifyChats; break;
		case mtpc_notifyPeer: {
			PeerData *data = App::peerLoaded(App::peerFromMTP(peer.c_notifyPeer().vpeer));
			if (!data) break;

			peerId = data->id;
			if (data->notify == UnknownNotifySettings || data->notify == EmptyNotifySettings) {
				data->notify = new NotifySettings();
			}
			setTo = data->notify;
		} break;
		}
		if (setTo == UnknownNotifySettings) break;

		setTo->mute = d.vmute_until.v;
		setTo->sound = d.vsound.c_string().v;
		setTo->previews = d.vshow_previews.v;
		setTo->events = d.vevents_mask.v;
		if (peerId) {
			if (!history) history = App::history(peerId);
			if (isNotifyMuted(setTo)) {
				App::wnd()->psClearNotify(history);
				history->setMute(true);
			} else {
				history->setMute(false);
			}
		}
	} break;
	}

	if (profile) {
		profile->updateNotifySettings();
	}
}

void MainWidget::gotNotifySetting(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings) {
	switch (peer.type()) {
	case mtpc_inputNotifyAll: applyNotifySetting(MTP_notifyAll(), settings); break;
	case mtpc_inputNotifyUsers: applyNotifySetting(MTP_notifyUsers(), settings); break;
	case mtpc_inputNotifyChats: applyNotifySetting(MTP_notifyChats(), settings); break;
	case mtpc_inputNotifyGeoChatPeer: break; // no MTP_peerGeoChat
	case mtpc_inputNotifyPeer:
		switch (peer.c_inputNotifyPeer().vpeer.type()) {
		case mtpc_inputPeerEmpty: applyNotifySetting(MTP_notifyPeer(MTP_peerUser(MTP_int(0))), settings); break;
		case mtpc_inputPeerSelf: applyNotifySetting(MTP_notifyPeer(MTP_peerUser(MTP_int(MTP::authedId()))), settings); break;
		case mtpc_inputPeerContact: applyNotifySetting(MTP_notifyPeer(MTP_peerUser(peer.c_inputNotifyPeer().vpeer.c_inputPeerContact().vuser_id)), settings); break;
		case mtpc_inputPeerForeign: applyNotifySetting(MTP_notifyPeer(MTP_peerUser(peer.c_inputNotifyPeer().vpeer.c_inputPeerForeign().vuser_id)), settings); break;
		case mtpc_inputPeerChat: applyNotifySetting(MTP_notifyPeer(MTP_peerChat(peer.c_inputNotifyPeer().vpeer.c_inputPeerChat().vchat_id)), settings); break;
		}
	break;
	}
	App::wnd()->psNotifySettingGot();
}

bool MainWidget::failNotifySetting(MTPInputNotifyPeer peer) {
	gotNotifySetting(peer, MTP_peerNotifySettingsEmpty());
	return true;
}

void MainWidget::updateNotifySetting(PeerData *peer, bool enabled) {
	updateNotifySettingPeers.insert(peer);
	if (peer->notify == EmptyNotifySettings) {
		if (!enabled) {
			peer->notify = new NotifySettings();
			peer->notify->sound = "";
			peer->notify->mute = unixtime() + 86400 * 365;
		}
	} else {
		if (peer->notify == UnknownNotifySettings) {
			peer->notify = new NotifySettings();
		}
		peer->notify->sound = enabled ? "default" : "";
		peer->notify->mute = enabled ? 0 : (unixtime() + 86400 * 365);
	}
	App::history(peer->id)->setMute(!enabled);
	updateNotifySettingTimer.start(NotifySettingSaveTimeout);
}

void MainWidget::activate() {
	if (!profile) {
		if (hider) {
			if (hider->wasOffered()) {
				hider->setFocus();
			} else {
				dialogs.activate();
			}
		} else if (history.peer()) {
			history.activate();
		} else {
			dialogs.activate();
		}
	}
	App::wnd()->fixOrder();
}

void MainWidget::destroyData() {
	history.destroyData();
	dialogs.destroyData();
}

void MainWidget::updateOnlineDisplayIn(int32 msecs) {
	onlineUpdater.start(msecs);
}

void MainWidget::addNewContact(int32 uid, bool show) {
	if (dialogs.addNewContact(uid, show)) {
		showPeer(App::peerFromUser(uid));
	}
}

bool MainWidget::isActive() const {
	return isVisible() && !animating();
}

bool MainWidget::historyIsActive() const {
	return isActive() && !profile;
}

int32 MainWidget::dlgsWidth() const {
	return dialogs.width();
}

MainWidget::~MainWidget() {
	delete hider;
	MTP::clearGlobalHandlers();
	App::deinitMedia(false);
	if (App::wnd()) App::wnd()->noMain(this);
}

void MainWidget::setOnline(int windowState) {
	if (onlineRequest) {
		MTP::cancel(onlineRequest);
		onlineRequest = 0;
	}
	onlineTimer.stop();
	bool isOnline = App::wnd()->psIsOnline(windowState);
	if (isOnline || windowState >= 0) {
		onlineRequest = MTP::send(MTPaccount_UpdateStatus(MTP_bool(!isOnline)));
        LOG(("App Info: Updating Online!"));
	}
	if (App::self()) App::self()->onlineTill = unixtime() + (isOnline ? 60 : -1);
	if (profile) {
		profile->updateOnlineDisplayTimer();
	} else {
		history.updateOnlineDisplayTimer();
	}
	onlineTimer.start(55000);
}

void MainWidget::mainStateChanged(Qt::WindowState state) {
	setOnline(state);
}

void MainWidget::updateReceived(const mtpPrime *from, const mtpPrime *end) {
	if (end <= from || !MTP::authedId()) return;

	if (mtpTypeId(*from) == mtpc_new_session_created) {
		MTPNewSession newSession(from, end);
		updSeq = 0;
		return getDifference();
	} else {
		try {
			MTPUpdates updates(from, end);

			noUpdatesTimer.start(NoUpdatesTimeout);

			switch (updates.type()) {
			case mtpc_updates: {
				const MTPDupdates &d(updates.c_updates());
				if (d.vseq.v) {
					if (d.vseq.v <= updSeq || d.vseq.v > updSeq + 1) return getDifference();
				}

				App::feedChats(d.vchats);
				App::feedUsers(d.vusers);
				feedUpdates(d.vupdates);
				
				updSetState(updPts, d.vdate.v, updQts, d.vseq.v);
			} break;

			case mtpc_updatesCombined: {
				const MTPDupdatesCombined &d(updates.c_updatesCombined());
				if (d.vseq.v) {
					if (d.vseq_start.v <= updSeq || d.vseq_start.v > updSeq + 1) return getDifference();
				}

				App::feedChats(d.vchats);
				App::feedUsers(d.vusers);
				feedUpdates(d.vupdates);

				updSetState(updPts, d.vdate.v, updQts, d.vseq.v);
			} break;

			case mtpc_updateShort: {
				const MTPDupdateShort &d(updates.c_updateShort());
				
				feedUpdate(d.vupdate);

				updSetState(updPts, d.vdate.v, updQts, updSeq);
			} break;

			case mtpc_updateShortMessage: {
				const MTPDupdateShortMessage &d(updates.c_updateShortMessage());
				if (d.vseq.v) {
					if (d.vseq.v <= updSeq || d.vseq.v > updSeq + 1) return getDifference();
				}

				if (!App::userLoaded(d.vfrom_id.v)) return getDifference();
				PeerId peer = App::histories().addToBack(MTP_message(d.vid, d.vfrom_id, MTP_peerUser(MTP_int(MTP::authedId())), MTP_bool(false), MTP_bool(true), d.vdate, d.vmessage, MTP_messageMediaEmpty()));
				history.peerMessagesUpdated(peer);

				updSetState(d.vpts.v, d.vdate.v, updQts, d.vseq.v);
			} break;

			case mtpc_updateShortChatMessage: {
				const MTPDupdateShortChatMessage &d(updates.c_updateShortChatMessage());
				if (d.vseq.v) {
					if (d.vseq.v <= updSeq || d.vseq.v > updSeq + 1) return getDifference();
				}

				if (!App::chatLoaded(d.vchat_id.v) || !App::userLoaded(d.vfrom_id.v)) return getDifference();
				PeerId peer = App::histories().addToBack(MTP_message(d.vid, d.vfrom_id, MTP_peerChat(d.vchat_id), MTP_bool(false), MTP_bool(true), d.vdate, d.vmessage, MTP_messageMediaEmpty()));
				history.peerMessagesUpdated(peer);

				updSetState(d.vpts.v, d.vdate.v, updQts, d.vseq.v);
			} break;

			case mtpc_updatesTooLong: {
				return getDifference();
			} break;
			}
		} catch(mtpErrorUnexpected &e) { // just some other type
		}
	}
	update();
/**/
}

void MainWidget::feedUpdate(const MTPUpdate &update) {
	if (!MTP::authedId()) return;

	switch (update.type()) {
	case mtpc_updateNewMessage: {
		const MTPDupdateNewMessage &d(update.c_updateNewMessage());
		PeerId peer = App::histories().addToBack(d.vmessage);
		history.peerMessagesUpdated(peer);
		if (updPts < d.vpts.v) updPts = d.vpts.v;
	} break;

	case mtpc_updateMessageID: {
		const MTPDupdateMessageID &d(update.c_updateMessageID());
		MsgId msg = App::histItemByRandom(d.vrandom_id.v);
		if (msg) {
			HistoryItem *msgRow = App::histItemById(msg);
			if (msgRow) {
				App::historyUnregItem(msgRow);
				if (msgRow->id > 0) --msgRow->history()->offset;
				msgRow->id = d.vid.v;
				if (msgRow->id > 0) ++msgRow->history()->offset;
				if (App::historyRegItem(msgRow)) {
					msgUpdated(msgRow->history()->peer->id, msgRow);
				} else {
					msgRow->destroy();
					history.peerMessagesUpdated();
				}
			}
			App::historyUnregRandom(d.vrandom_id.v);
		}
	} break;

	case mtpc_updateReadMessages: {
		const MTPDupdateReadMessages &d(update.c_updateReadMessages());
		App::feedWereRead(d.vmessages.c_vector().v);
		if (updPts < d.vpts.v) updPts = d.vpts.v;
	} break;

	case mtpc_updateDeleteMessages: {
		const MTPDupdateDeleteMessages &d(update.c_updateDeleteMessages());
		App::feedWereDeleted(d.vmessages.c_vector().v);
		history.peerMessagesUpdated();
		if (updPts < d.vpts.v) updPts = d.vpts.v;
	} break;

	case mtpc_updateRestoreMessages: {
		const MTPDupdateRestoreMessages &d(update.c_updateRestoreMessages());
		if (updPts < d.vpts.v) updPts = d.vpts.v;
	} break;

	case mtpc_updateUserTyping: {
		const MTPDupdateUserTyping &d(update.c_updateUserTyping());
		History *history = App::historyLoaded(App::peerFromUser(d.vuser_id));
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (history && user) {
			dialogs.regTyping(history, user);
		}
	} break;

	case mtpc_updateChatUserTyping: {
		const MTPDupdateChatUserTyping &d(update.c_updateChatUserTyping());
		History *history = App::historyLoaded(App::peerFromChat(d.vchat_id));
		UserData *user = (d.vuser_id.v == MTP::authedId()) ? 0 : App::userLoaded(d.vuser_id.v);
		if (history && user) {
			dialogs.regTyping(history, user);
		}
	} break;

	case mtpc_updateChatParticipants: {
		const MTPDupdateChatParticipants &d(update.c_updateChatParticipants());
		App::feedParticipants(d.vparticipants);
	} break;

	case mtpc_updateChatParticipantAdd: {
		const MTPDupdateChatParticipantAdd &d(update.c_updateChatParticipantAdd());
		App::feedParticipantAdd(d);
	} break;

	case mtpc_updateChatParticipantDelete: {
		const MTPDupdateChatParticipantDelete &d(update.c_updateChatParticipantDelete());
		App::feedParticipantDelete(d);
	} break;

	case mtpc_updateUserStatus: {
		const MTPDupdateUserStatus &d(update.c_updateUserStatus());
		if (d.vuser_id.v == MTP::authedId() && (d.vstatus.type() == mtpc_userStatusOffline || d.vstatus.type() == mtpc_userStatusEmpty)) {
			setOnline();
		} else {
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				switch (d.vstatus.type()) {
				case mtpc_userStatusEmpty: user->onlineTill = 0; break;
				case mtpc_userStatusOffline: user->onlineTill = d.vstatus.c_userStatusOffline().vwas_online.v; break;
				case mtpc_userStatusOnline: user->onlineTill = d.vstatus.c_userStatusOnline().vexpires.v; break;
				}
				if (App::main()) App::main()->peerUpdated(user);
			}
		}
	} break;

	case mtpc_updateUserName: {
		const MTPDupdateUserName &d(update.c_updateUserName());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user && user->contact <= 0) {
			user->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), user->nameOrPhone);
			if (App::main()) App::main()->peerUpdated(user);
		}
	} break;

	case mtpc_updateUserPhoto: {
		const MTPDupdateUserPhoto &d(update.c_updateUserPhoto());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user) {
			user->setPhoto(d.vphoto);
			user->photo->load();
			if (false && !d.vprevious.v && d.vuser_id.v != MTP::authedId() && d.vphoto.type() == mtpc_userProfilePhoto) {
				MTPPhoto photo(App::photoFromUserPhoto(MTP_int(user->id & 0xFFFFFFFF), d.vdate, d.vphoto));
				HistoryMedia *media = new HistoryPhoto(photo.c_photo(), 100);
				App::history(user->id)->addToBackService(clientMsgId(), date(d.vdate), lang(lng_action_user_photo).replace(qsl("{from}"), user->name), false, true, media);
			}
			if (App::main()) App::main()->peerUpdated(user);
		}
	} break;

	case mtpc_updateContactRegistered: {
		const MTPDupdateContactRegistered &d(update.c_updateContactRegistered());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user) {
			App::history(user->id)->addToBackService(clientMsgId(), date(d.vdate), lang(lng_action_user_registered).replace(qsl("{from}"), user->name), false, true);
		}
	} break;

	case mtpc_updateContactLink: {
		const MTPDupdateContactLink &d(update.c_updateContactLink());
		App::feedUserLink(d.vuser_id, d.vmy_link, d.vforeign_link);
	} break;

	case mtpc_updateActivation: {
		const MTPDupdateActivation &d(update.c_updateActivation());
	} break;

	case mtpc_updateNewAuthorization: {
		const MTPDupdateNewAuthorization &d(update.c_updateNewAuthorization());
	} break;

	case mtpc_updateNewEncryptedMessage: {
		const MTPDupdateNewEncryptedMessage &d(update.c_updateNewEncryptedMessage());
		if (updQts < d.vqts.v) updQts = d.vqts.v;
	} break;

	case mtpc_updateEncryptedChatTyping: {
		const MTPDupdateEncryptedChatTyping &d(update.c_updateEncryptedChatTyping());
	} break;

	case mtpc_updateEncryption: {
		const MTPDupdateEncryption &d(update.c_updateEncryption());
	} break;

	case mtpc_updateEncryptedMessagesRead: {
		const MTPDupdateEncryptedMessagesRead &d(update.c_updateEncryptedMessagesRead());
	} break;

	case mtpc_updateNewGeoChatMessage: {
		const MTPDupdateNewGeoChatMessage &d(update.c_updateNewGeoChatMessage());
//		PeerId peer = App::histories().addToBack(d.vmessage);
//		history.peerMessagesUpdated(peer);
	} break;

	case mtpc_updateUserBlocked: {
		const MTPDupdateUserBlocked &d(update.c_updateUserBlocked());
		//
	} break;

	case mtpc_updateNotifySettings: {
		const MTPDupdateNotifySettings &d(update.c_updateNotifySettings());
		applyNotifySetting(d.vpeer, d.vnotify_settings);
	} break;
	}
}
