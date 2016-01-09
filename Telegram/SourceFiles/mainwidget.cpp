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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "boxes/addcontactbox.h"
#include "fileuploader.h"
#include "application.h"
#include "window.h"
#include "settingswidget.h"
#include "mainwidget.h"
#include "boxes/confirmbox.h"
#include "boxes/stickersetbox.h"
#include "boxes/contactsbox.h"
#include "boxes/downloadpathbox.h"

#include "localstorage.h"

#include "audio.h"

TopBarWidget::TopBarWidget(MainWidget *w) : TWidget(w)
, a_over(0)
, _a_appearance(animation(this, &TopBarWidget::step_appearance))
, _selPeer(0)
, _selCount(0)
, _canDelete(false)
, _selStrLeft(-st::topBarButton.width / 2)
, _selStrWidth(0)
, _animating(false)
, _clearSelection(this, lang(lng_selected_clear), st::topBarButton)
, _forward(this, lang(lng_selected_forward), st::topBarActionButton)
, _delete(this, lang(lng_selected_delete), st::topBarActionButton)
, _selectionButtonsWidth(_clearSelection.width() + _forward.width() + _delete.width()), _forwardDeleteWidth(qMax(_forward.textWidth(), _delete.textWidth()))
, _info(this, lang(lng_topbar_info), st::topBarButton)
, _edit(this, lang(lng_profile_edit_contact), st::topBarButton)
, _leaveGroup(this, lang(lng_profile_delete_and_exit), st::topBarButton)
, _addContact(this, lang(lng_profile_add_contact), st::topBarButton)
, _deleteContact(this, lang(lng_profile_delete_contact), st::topBarButton)
, _mediaType(this, lang(lng_media_type), st::topBarButton)
, _sideShadow(this, st::shadowColor) {

	connect(&_forward, SIGNAL(clicked()), this, SLOT(onForwardSelection()));
	connect(&_delete, SIGNAL(clicked()), this, SLOT(onDeleteSelection()));
	connect(&_clearSelection, SIGNAL(clicked()), this, SLOT(onClearSelection()));
	connect(&_info, SIGNAL(clicked()), this, SLOT(onInfoClicked()));
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

void TopBarWidget::onInfoClicked() {
	PeerData *p = App::main() ? App::main()->historyPeer() : 0;
	if (p) App::main()->showPeerProfile(p);
}

void TopBarWidget::onAddContact() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = p ? p->asUser() : 0;
	if (u) Ui::showLayer(new AddContactBox(u->firstName, u->lastName, u->phone.isEmpty() ? App::phoneFromSharedContact(peerToUser(u->id)) : u->phone));
}

void TopBarWidget::onEdit() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	if (p) {
		if (p->isChannel()) {
			Ui::showLayer(new EditChannelBox(p->asChannel()));
		} else if (p->isChat()) {
			Ui::showLayer(new EditNameTitleBox(p));
		} else if (p->isUser()) {
			Ui::showLayer(new AddContactBox(p->asUser()));
		}
	}
}

void TopBarWidget::onDeleteContact() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = p ? p->asUser() : 0;
	if (u) {
		ConfirmBox *box = new ConfirmBox(lng_sure_delete_contact(lt_contact, p->name), lang(lng_box_delete));
		connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteContactSure()));
		Ui::showLayer(box);
	}
}

void TopBarWidget::onDeleteContactSure() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = p ? p->asUser() : 0;
	if (u) {
		Ui::showChatsList();
		Ui::hideLayer();
		MTP::send(MTPcontacts_DeleteContact(u->inputUser), App::main()->rpcDone(&MainWidget::deletedContact, u));
	}
}

void TopBarWidget::onDeleteAndExit() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	ChatData *c = p ? p->asChat() : 0;
	if (c) {
		ConfirmBox *box = new ConfirmBox(lng_sure_delete_and_exit(lt_group, p->name), lang(lng_box_leave), st::attentionBoxButton);
		connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteAndExitSure()));
		Ui::showLayer(box);
	}
}

void TopBarWidget::onDeleteAndExitSure() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	ChatData *c = p ? p->asChat() : 0;
	if (c) {
		Ui::showChatsList();
		Ui::hideLayer();
		MTP::send(MTPmessages_DeleteChatUser(c->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, p), App::main()->rpcFail(&MainWidget::leaveChatFailed, p));
	}
}

void TopBarWidget::enterEvent(QEvent *e) {
	a_over.start(1);
	_a_appearance.start();
}

void TopBarWidget::enterFromChildEvent(QEvent *e) {
	a_over.start(1);
	_a_appearance.start();
}

void TopBarWidget::leaveEvent(QEvent *e) {
	a_over.start(0);
	_a_appearance.start();
}

void TopBarWidget::leaveToChildEvent(QEvent *e) {
	a_over.start(0);
	_a_appearance.start();
}

void TopBarWidget::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / st::topBarDuration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_over.finish();
	} else {
		a_over.update(dt, anim::linear);
	}
	if (timer) update();
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (e->rect().top() < st::topBarHeight) { // optimize shadow-only drawing
		p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBG->b);
		if (_clearSelection.isHidden()) {
			p.save();
			main()->paintTopBar(p, a_over.current(), _info.isHidden() ? 0 : _info.width());
			p.restore();
		} else {
			p.setFont(st::linkFont->f);
			p.setPen(st::btnDefLink.color->p);
			p.drawText(_selStrLeft, st::topBarButton.textTop + st::linkFont->ascent, _selStr);
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
	if (!_forward.isHidden() || !_delete.isHidden()) {
		int32 fullW = r - (_selectionButtonsWidth + (_selStrWidth - st::topBarButton.width) + st::topBarActionSkip);
		int32 selectedClearWidth = st::topBarButton.width, forwardDeleteWidth = st::topBarActionButton.width - _forwardDeleteWidth, skip = st::topBarActionSkip;
		while (fullW < 0) {
			int fit = 0;
			if (selectedClearWidth < -2 * (st::topBarMinPadding + 1)) {
				fullW += 4;
				selectedClearWidth += 2;
			} else if (selectedClearWidth < -2 * st::topBarMinPadding) {
				fullW += (-2 * st::topBarMinPadding - selectedClearWidth) * 2;
				selectedClearWidth = -2 * st::topBarMinPadding;
			} else {
				++fit;
			}
			if (fullW >= 0) break;

			if (forwardDeleteWidth > 2 * (st::topBarMinPadding + 1)) {
				fullW += 4;
				forwardDeleteWidth -= 2;
			} else if (forwardDeleteWidth > 2 * st::topBarMinPadding) {
				fullW += (forwardDeleteWidth - 2 * st::topBarMinPadding) * 2;
				forwardDeleteWidth = 2 * st::topBarMinPadding;
			} else {
				++fit;
			}
			if (fullW >= 0) break;

			if (skip > st::topBarMinPadding) {
				--skip;
				++fullW;
			} else {
				++fit;
			}
			if (fullW >= 0 || fit >= 3) break;
		}
		_clearSelection.setWidth(selectedClearWidth);
		_forward.setWidth(_forwardDeleteWidth + forwardDeleteWidth);
		_delete.setWidth(_forwardDeleteWidth + forwardDeleteWidth);
		_selStrLeft = -selectedClearWidth / 2;

		int32 availX = _selStrLeft + _selStrWidth, availW = r - (_clearSelection.width() + selectedClearWidth / 2) - availX;
		if (_forward.isHidden()) {
			_delete.move(availX + (availW - _delete.width()) / 2, (st::topBarHeight - _forward.height()) / 2);
		} else if (_delete.isHidden()) {
			_forward.move(availX + (availW - _forward.width()) / 2, (st::topBarHeight - _forward.height()) / 2);
		} else {
			_forward.move(availX + (availW - _forward.width() - _delete.width() - skip) / 2, (st::topBarHeight - _forward.height()) / 2);
			_delete.move(availX + (availW + _forward.width() - _delete.width() + skip) / 2, (st::topBarHeight - _forward.height()) / 2);
		}
		_clearSelection.move(r -= _clearSelection.width(), 0);
	}
	if (!_info.isHidden()) _info.move(r -= _info.width(), 0);
	if (!_deleteContact.isHidden()) _deleteContact.move(r -= _deleteContact.width(), 0);
	if (!_leaveGroup.isHidden()) _leaveGroup.move(r -= _leaveGroup.width(), 0);
	if (!_edit.isHidden()) _edit.move(r -= _edit.width(), 0);
	if (!_addContact.isHidden()) _addContact.move(r -= _addContact.width(), 0);
	if (!_mediaType.isHidden()) _mediaType.move(r -= _mediaType.width(), 0);

	_sideShadow.resize(st::lineWidth, height());
	_sideShadow.moveToLeft(0, 0);
}

void TopBarWidget::startAnim() {
	_info.hide();
	_edit.hide();
	_leaveGroup.hide();
	_addContact.hide();
	_deleteContact.hide();
    _clearSelection.hide();
    _delete.hide();
    _forward.hide();
	_mediaType.hide();

	_animating = true;
}

void TopBarWidget::stopAnim() {
    _animating = false;
	_sideShadow.setVisible(cWideMode());
	showAll();
}

void TopBarWidget::showAll() {
    if (_animating) {
        resizeEvent(0);
        return;
    }
	PeerData *p = App::main() ? App::main()->profilePeer() : 0, *h = App::main() ? App::main()->historyPeer() : 0, *o = App::main() ? App::main()->overviewPeer() : 0;
	if (p && (p->isChat() || (p->isUser() && (p->asUser()->contact >= 0 || !App::phoneFromSharedContact(peerToUser(p->id)).isEmpty())))) {
		if (p->isChat()) {
			if (p->asChat()->canEdit()) {
				_edit.show();
			} else {
				_edit.hide();
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
		_info.hide();
		_delete.hide();
		_forward.hide();
		_mediaType.hide();
	} else {
		if (p && p->isChannel() && (p->asChannel()->amCreator() || (p->isMegagroup() && p->asChannel()->amEditor()))) {
			_edit.show();
		} else {
			_edit.hide();
		}
		_leaveGroup.hide();
		_addContact.hide();
		_deleteContact.hide();
		if (!p && _selCount) {
			_clearSelection.show();
			if (_canDelete) {
				_delete.show();
			} else {
				_delete.hide();
			}
			_forward.show();
			_mediaType.hide();
		} else {
			_clearSelection.hide();
			_delete.hide();
			_forward.hide();
			if (App::main() && App::main()->mediaTypeSwitch()) {
				_mediaType.show();
			} else {
				_mediaType.hide();
			}
		}
        if (App::main() && App::main()->historyPeer() && !o && !p && _clearSelection.isHidden() && !cWideMode()) {
			_info.show();
		} else {
			_info.hide();
		}
	}
	_sideShadow.setVisible(cWideMode());
	resizeEvent(0);
}

void TopBarWidget::showSelected(uint32 selCount, bool canDelete) {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	_selPeer = App::main()->overviewPeer() ? App::main()->overviewPeer() : App::main()->peer();
	_selCount = selCount;
	_canDelete = canDelete;
	_selStr = (_selCount > 0) ? lng_selected_count(lt_count, _selCount) : QString();
	_selStrWidth = st::btnDefLink.font->width(_selStr);
	setCursor((!p && _selCount) ? style::cur_default : style::cur_pointer);
	showAll();
}

void TopBarWidget::updateWideMode() {
	showAll();
}

FlatButton *TopBarWidget::mediaTypeButton() {
	return &_mediaType;
}

MainWidget *TopBarWidget::main() {
	return static_cast<MainWidget*>(parentWidget());
}

MainWidget::MainWidget(Window *window) : TWidget(window)
, _started(0)
, failedObjId(0)
, _toForwardNameVersion(0)
, _a_show(animation(this, &MainWidget::step_show))
, _dialogsWidth(st::dlgMinWidth)
, dialogs(this)
, history(this)
, profile(0)
, overview(0)
, _player(this)
, _topBar(this)
, _forwardConfirm(0)
, _hider(0)
, _peerInStack(0)
, _msgIdInStack(0)
, _stickerPreview(0)
, _playerHeight(0)
, _contentScrollAddToY(0)
, _mediaType(this)
, _mediaTypeMask(0)
, updDate(0)
, updQts(-1)
, updSeq(0)
, _getDifferenceTimeByPts(0)
, _getDifferenceTimeAfterFail(0)
, _onlineRequest(0)
, _lastWasOnline(false)
, _lastSetOnline(0)
, _isIdle(false)
, _failDifferenceTimeout(1)
, _lastUpdateTime(0)
, _handlingChannelDifference(false)
, _cachedX(0)
, _cachedY(0)
, _background(0)
, _api(new ApiWrap(this)) {
	setGeometry(QRect(0, st::titleHeight, App::wnd()->width(), App::wnd()->height() - st::titleHeight));

	MTP::setGlobalDoneHandler(rpcDone(&MainWidget::updateReceived));
	_ptsWaiter.setRequesting(true);
	updateScrollColors();

	connect(App::wnd(), SIGNAL(resized(const QSize&)), this, SLOT(onParentResize(const QSize&)));
	connect(&dialogs, SIGNAL(cancelled()), this, SLOT(dialogsCancelled()));
	connect(&history, SIGNAL(cancelled()), &dialogs, SLOT(activate()));
	connect(this, SIGNAL(peerPhotoChanged(PeerData*)), this, SIGNAL(dialogsUpdated()));
	connect(&noUpdatesTimer, SIGNAL(timeout()), this, SLOT(mtpPing()));
	connect(&_onlineTimer, SIGNAL(timeout()), this, SLOT(updateOnline()));
	connect(&_onlineUpdater, SIGNAL(timeout()), this, SLOT(updateOnlineDisplay()));
	connect(&_idleFinishTimer, SIGNAL(timeout()), this, SLOT(checkIdleFinish()));
	connect(&_bySeqTimer, SIGNAL(timeout()), this, SLOT(getDifference()));
	connect(&_byPtsTimer, SIGNAL(timeout()), this, SLOT(onGetDifferenceTimeByPts()));
	connect(&_failDifferenceTimer, SIGNAL(timeout()), this, SLOT(onGetDifferenceTimeAfterFail()));
	connect(_api, SIGNAL(fullPeerUpdated(PeerData*)), this, SLOT(onFullPeerUpdated(PeerData*)));
	connect(this, SIGNAL(peerUpdated(PeerData*)), &history, SLOT(peerUpdated(PeerData*)));
	connect(&_topBar, SIGNAL(clicked()), this, SLOT(onTopBarClick()));
	connect(&history, SIGNAL(historyShown(History*,MsgId)), this, SLOT(onHistoryShown(History*,MsgId)));
	connect(&updateNotifySettingTimer, SIGNAL(timeout()), this, SLOT(onUpdateNotifySettings()));
	if (audioPlayer()) {
		connect(audioPlayer(), SIGNAL(updated(const AudioMsgId&)), this, SLOT(audioPlayProgress(const AudioMsgId&)));
		connect(audioPlayer(), SIGNAL(stopped(const AudioMsgId&)), this, SLOT(audioPlayProgress(const AudioMsgId&)));
		connect(audioPlayer(), SIGNAL(updated(const SongMsgId&)), this, SLOT(documentPlayProgress(const SongMsgId&)));
		connect(audioPlayer(), SIGNAL(stopped(const SongMsgId&)), this, SLOT(documentPlayProgress(const SongMsgId&)));
	}
	connect(&_updateMutedTimer, SIGNAL(timeout()), this, SLOT(onUpdateMuted()));
	connect(&_viewsIncrementTimer, SIGNAL(timeout()), this, SLOT(onViewsIncrement()));

	_webPageUpdater.setSingleShot(true);
	connect(&_webPageUpdater, SIGNAL(timeout()), this, SLOT(webPagesUpdate()));

	connect(&_cacheBackgroundTimer, SIGNAL(timeout()), this, SLOT(onCacheBackground()));

	dialogs.show();
	if (cWideMode()) {
		history.show();
	} else {
		history.hide();
	}
	App::wnd()->getTitle()->updateBackButton();
	_topBar.hide();

	_player.hide();

	orderWidgets();

	MTP::setGlobalFailHandler(rpcFail(&MainWidget::updateFail));

	_mediaType.hide();
	_topBar.mediaTypeButton()->installEventFilter(&_mediaType);

	show();
	setFocus();

	_api->init();
}

bool MainWidget::onForward(const PeerId &peer, ForwardWhatMessages what) {
	PeerData *p = App::peer(peer);
	if (!peer || (p->isChannel() && !p->asChannel()->canPublish() && p->asChannel()->isBroadcast()) || (p->isChat() && !p->asChat()->canWrite()) || (p->isUser() && p->asUser()->access == UserNoAccess)) {
		Ui::showLayer(new InformBox(lang(lng_forward_cant)));
		return false;
	}
	history.cancelReply();
	_toForward.clear();
	if (what == ForwardSelectedMessages) {
		if (overview) {
			overview->fillSelectedItems(_toForward, false);
		} else {
			history.fillSelectedItems(_toForward, false);
		}
	} else {
		HistoryItem *item = 0;
		if (what == ForwardContextMessage) {
			item = App::contextItem();
		} else if (what == ForwardPressedMessage) {
			item = App::pressedItem();
		} else if (what == ForwardPressedLinkMessage) {
			item = App::pressedLinkItem();
		}
		if (item && item->toHistoryMessage() && item->id > 0) {
			_toForward.insert(item->id, item);
		}
	}
	updateForwardingTexts();
	Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	history.onClearSelected();
	history.updateForwarding();
	return true;
}

bool MainWidget::onShareUrl(const PeerId &peer, const QString &url, const QString &text) {
	PeerData *p = App::peer(peer);
	if (!peer || (p->isChannel() && !p->asChannel()->canPublish() && p->asChannel()->isBroadcast()) || (p->isChat() && !p->asChat()->canWrite()) || (p->isUser() && p->asUser()->access == UserNoAccess)) {
		Ui::showLayer(new InformBox(lang(lng_share_cant)));
		return false;
	}
	History *h = App::history(peer);
	h->draft = url + '\n' + text;
	h->draftCursor.anchor = url.size() + 1;
	h->draftCursor.position = h->draftCursor.anchor + text.size();
	h->draftPreviewCancelled = false;
	bool opened = history.peer() && (history.peer()->id == peer);
	if (opened) {
		history.applyDraft();
	} else {
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	}
	return true;
}

bool MainWidget::hasForwardingItems() {
	return !_toForward.isEmpty();
}

void MainWidget::fillForwardingInfo(Text *&from, Text *&text, bool &serviceColor, ImagePtr &preview) {
	if (_toForward.isEmpty()) return;
	int32 version = 0;
	for (SelectedItemSet::const_iterator i = _toForward.cbegin(), e = _toForward.cend(); i != e; ++i) {
		if (HistoryForwarded *fwd = i.value()->toHistoryForwarded()) {
			version += fwd->fromForwarded()->nameVersion;
		} else {
			version += i.value()->from()->nameVersion;
		}
	}
	if (version != _toForwardNameVersion) {
		updateForwardingTexts();
	}
	from = &_toForwardFrom;
	text = &_toForwardText;
	serviceColor = (_toForward.size() > 1) || _toForward.cbegin().value()->getMedia() || _toForward.cbegin().value()->serviceMsg();
	if (_toForward.size() < 2 && _toForward.cbegin().value()->getMedia() && _toForward.cbegin().value()->getMedia()->hasReplyPreview()) {
		preview = _toForward.cbegin().value()->getMedia()->replyPreview();
	}
}

void MainWidget::updateForwardingTexts() {
	int32 version = 0;
	QString from, text;
	if (!_toForward.isEmpty()) {
		QMap<PeerData*, bool> fromUsersMap;
		QVector<PeerData*> fromUsers;
		fromUsers.reserve(_toForward.size());
		for (SelectedItemSet::const_iterator i = _toForward.cbegin(), e = _toForward.cend(); i != e; ++i) {
			PeerData *from = i.value()->from();
			if (HistoryForwarded *fwd = i.value()->toHistoryForwarded()) {
				from = fwd->fromForwarded();
			}
			if (!fromUsersMap.contains(from)) {
				fromUsersMap.insert(from, true);
				fromUsers.push_back(from);
			}
			version += i.value()->from()->nameVersion;
		}
		if (fromUsers.size() > 2) {
			from = lng_forwarding_from(lt_user, fromUsers.at(0)->shortName(), lt_count, fromUsers.size() - 1);
		} else if (fromUsers.size() < 2) {
			from = fromUsers.at(0)->name;
		} else {
			from = lng_forwarding_from_two(lt_user, fromUsers.at(0)->shortName(), lt_second_user, fromUsers.at(1)->shortName());
		}

		if (_toForward.size() < 2) {
			text = _toForward.cbegin().value()->inReplyText();
		} else {
			text = lng_forward_messages(lt_count, _toForward.size());
		}
	}
	_toForwardFrom.setText(st::msgServiceNameFont, from, _textNameOptions);
	_toForwardText.setText(st::msgFont, text, _textDlgOptions);
	_toForwardNameVersion = version;
}

void MainWidget::cancelForwarding() {
	if (_toForward.isEmpty()) return;

	_toForward.clear();
	history.cancelForwarding();
}

void MainWidget::finishForwarding(History *hist, bool broadcast) {
	if (!hist) return;

	bool fromChannelName = hist->peer->isChannel() && !hist->peer->isMegagroup() && hist->peer->asChannel()->canPublish() && (hist->peer->asChannel()->isBroadcast() || broadcast);
	if (!_toForward.isEmpty()) {
		bool genClientSideMessage = (_toForward.size() < 2);
		PeerData *forwardFrom = 0;
		App::main()->readServerHistory(hist, false);

		int32 flags = fromChannelName ? MTPmessages_ForwardMessages::flag_broadcast : 0;

		QVector<MTPint> ids;
		QVector<MTPlong> randomIds;
		ids.reserve(_toForward.size());
		randomIds.reserve(_toForward.size());
		for (SelectedItemSet::const_iterator i = _toForward.cbegin(), e = _toForward.cend(); i != e; ++i) {
			uint64 randomId = MTP::nonce<uint64>();
			if (genClientSideMessage) {
				FullMsgId newId(peerToChannel(hist->peer->id), clientMsgId());
				HistoryMessage *msg = static_cast<HistoryMessage*>(_toForward.cbegin().value());
				hist->addNewForwarded(newId.msg, date(MTP_int(unixtime())), fromChannelName ? 0 : MTP::authedId(), msg);
				if (HistoryMedia *media = msg->getMedia()) {
					if (media->type() == MediaTypeSticker) {
						App::main()->incrementSticker(media->getDocument());
					}
				}
				App::historyRegRandom(randomId, newId);
			}
			if (forwardFrom != i.value()->history()->peer) {
				if (forwardFrom) {
					hist->sendRequestId = MTP::send(MTPmessages_ForwardMessages(MTP_int(flags), forwardFrom->input, MTP_vector<MTPint>(ids), MTP_vector<MTPlong>(randomIds), hist->peer->input), rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, hist->sendRequestId);
					ids.resize(0);
					randomIds.resize(0);
				}
				forwardFrom = i.value()->history()->peer;
			}
			ids.push_back(MTP_int(i.value()->id));
			randomIds.push_back(MTP_long(randomId));
		}
		hist->sendRequestId = MTP::send(MTPmessages_ForwardMessages(MTP_int(flags), forwardFrom->input, MTP_vector<MTPint>(ids), MTP_vector<MTPlong>(randomIds), hist->peer->input), rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, hist->sendRequestId);

		if (history.peer() == hist->peer) {
			history.peerMessagesUpdated();
		}

		cancelForwarding();
	}

	historyToDown(hist);
	dialogsToUp();
	history.peerMessagesUpdated(hist->peer->id);
}

void MainWidget::webPageUpdated(WebPageData *data) {
	_webPagesUpdated.insert(data->id, true);
	_webPageUpdater.start(0);
}

void MainWidget::webPagesUpdate() {
	if (_webPagesUpdated.isEmpty()) return;

	_webPageUpdater.stop();
	const WebPageItems &items(App::webPageItems());
	for (QMap<WebPageId, bool>::const_iterator i = _webPagesUpdated.cbegin(), e = _webPagesUpdated.cend(); i != e; ++i) {
		WebPageItems::const_iterator j = items.constFind(App::webPage(i.key()));
		if (j != items.cend()) {
			for (HistoryItemsMap::const_iterator k = j.value().cbegin(), e = j.value().cend(); k != e; ++k) {
				k.key()->initDimensions();
				Notify::historyItemResized(k.key());
			}
		}
	}
	_webPagesUpdated.clear();
}

void MainWidget::updateMutedIn(int32 seconds) {
	if (seconds > 86400) seconds = 86400;
	int32 ms = seconds * 1000;
	if (_updateMutedTimer.isActive() && _updateMutedTimer.remainingTime() <= ms) return;
	_updateMutedTimer.start(ms);
}

void MainWidget::updateStickers() {
	history.updateStickers();
}

void MainWidget::onUpdateMuted() {
	App::updateMuted();
}

void MainWidget::onShareContact(const PeerId &peer, UserData *contact) {
	history.onShareContact(peer, contact);
}

void MainWidget::onSendPaths(const PeerId &peer) {
	history.onSendPaths(peer);
}

void MainWidget::onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data) {
	if (data->hasFormat(qsl("application/x-td-forward-selected"))) {
		onForward(peer, ForwardSelectedMessages);
	} else if (data->hasFormat(qsl("application/x-td-forward-pressed-link"))) {
		onForward(peer, ForwardPressedLinkMessage);
	} else if (data->hasFormat(qsl("application/x-td-forward-pressed"))) {
		onForward(peer, ForwardPressedMessage);
	} else {
		Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
		history.onFilesDrop(data);
	}
}

QPixmap MainWidget::grabInner() {
	if (overview && !overview->isHidden()) {
		return myGrab(overview);
	} else if (profile && !profile->isHidden()) {
		return myGrab(profile);
	} else if (!cWideMode() && history.isHidden()) {
		return myGrab(&dialogs, QRect(0, st::topBarHeight, dialogs.width(), dialogs.height() - st::topBarHeight));
	} else if (history.peer()) {
		return myGrab(&history);
	} else {
		return myGrab(&history, QRect(0, st::topBarHeight, history.width(), history.height() - st::topBarHeight));
	}
}

bool MainWidget::isItemVisible(HistoryItem *item) {
	if (isHidden() || _a_show.animating()) {
		return false;
	}
	return history.isItemVisible(item);
}

QPixmap MainWidget::grabTopBar() {
	if (!_topBar.isHidden()) {
		return myGrab(&_topBar);
	} else if (!cWideMode() && history.isHidden()) {
		return myGrab(&dialogs, QRect(0, 0, dialogs.width(), st::topBarHeight));
	} else {
		return myGrab(&history, QRect(0, 0, history.width(), st::topBarHeight));
	}
}

void MainWidget::ui_showStickerPreview(DocumentData *sticker) {
	if (!sticker || ((!sticker->isAnimation() || !sticker->loaded()) && !sticker->sticker())) return;
	if (!_stickerPreview) {
		_stickerPreview = new StickerPreviewWidget(this);
		resizeEvent(0);
	}
	_stickerPreview->showPreview(sticker);
}

void MainWidget::ui_hideStickerPreview() {
	if (!_stickerPreview) return;
	_stickerPreview->hidePreview();
}

void MainWidget::notify_botCommandsChanged(UserData *bot) {
	history.notify_botCommandsChanged(bot);
}

void MainWidget::notify_inlineBotRequesting(bool requesting) {
	history.notify_inlineBotRequesting(requesting);
}

void MainWidget::notify_userIsBotChanged(UserData *bot) {
	history.notify_userIsBotChanged(bot);
}

void MainWidget::notify_userIsContactChanged(UserData *user, bool fromThisApp) {
	if (!user) return;

	dialogs.notify_userIsContactChanged(user, fromThisApp);

	const SharedContactItems &items(App::sharedContactItems());
	SharedContactItems::const_iterator i = items.constFind(peerToUser(user->id));
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			j.key()->initDimensions();
			Ui::repaintHistoryItem(j.key());
		}
	}

	if (user->contact > 0 && fromThisApp) {
		Ui::showPeerHistory(user->id, ShowAtTheEndMsgId);
	}
}

void MainWidget::notify_migrateUpdated(PeerData *peer) {
	history.notify_migrateUpdated(peer);
}

void MainWidget::notify_clipStopperHidden(ClipStopperType type) {
	history.notify_clipStopperHidden(type);
}

void MainWidget::ui_repaintHistoryItem(const HistoryItem *item) {
	history.ui_repaintHistoryItem(item);
	if (!item->history()->dialogs.isEmpty() && item->history()->lastMsg == item) {
		dialogs.dlgUpdated(item->history()->dialogs[0]);
	}
	if (overview) overview->ui_repaintHistoryItem(item);
}

void MainWidget::ui_repaintInlineItem(const LayoutInlineItem *layout) {
	history.ui_repaintInlineItem(layout);
}

bool MainWidget::ui_isInlineItemVisible(const LayoutInlineItem *layout) {
	return history.ui_isInlineItemVisible(layout);
}

bool MainWidget::ui_isInlineItemBeingChosen() {
	return history.ui_isInlineItemBeingChosen();
}

void MainWidget::notify_historyItemLayoutChanged(const HistoryItem *item) {
	history.notify_historyItemLayoutChanged(item);
	if (overview) overview->notify_historyItemLayoutChanged(item);
}

void MainWidget::notify_automaticLoadSettingsChangedGif() {
	history.notify_automaticLoadSettingsChangedGif();
}

void MainWidget::notify_historyItemResized(const HistoryItem *item, bool scrollToIt) {
	if (!item || ((history.peer() == item->history()->peer || (history.peer() && history.peer() == item->history()->peer->migrateTo())) && !item->detached())) {
		history.notify_historyItemResized(item, scrollToIt);
	} else if (item) {
		item->history()->width = 0;
		if (history.peer() == item->history()->peer || (history.peer() && history.peer() == item->history()->peer->migrateTo())) {
			history.resizeEvent(0);
		}
	}
	if (item) Ui::repaintHistoryItem(item);
}

void MainWidget::noHider(HistoryHider *destroyed) {
	if (_hider == destroyed) {
		_hider = 0;
		if (cWideMode()) {
			if (_forwardConfirm) {
				_forwardConfirm->deleteLater();
				_forwardConfirm = 0;
			}
		} else {
			if (_forwardConfirm) {
				_forwardConfirm->startHide();
				_forwardConfirm = 0;
			}
			onHistoryShown(history.history(), history.msgId());
			if (profile || overview || (history.peer() && history.peer()->id)) {
				QPixmap animCache = grabInner(), animTopBarCache = grabTopBar();
				dialogs.hide();
				if (overview) {
					overview->show();
					overview->animShow(animCache, animTopBarCache);
				} else if (profile) {
					profile->show();
					profile->animShow(animCache, animTopBarCache);
				} else {
					history.show();
					history.animShow(animCache, animTopBarCache);
				}
			}
			App::wnd()->getTitle()->updateBackButton();
		}
	}
}

void MainWidget::hiderLayer(HistoryHider *h) {
	if (App::passcoded()) {
		delete h;
		return;
	}

	_hider = h;
	connect(_hider, SIGNAL(forwarded()), &dialogs, SLOT(onCancelSearch()));
	if (cWideMode()) {
		_hider->show();
		resizeEvent(0);
		dialogs.activate();
	} else {
		dialogsToUp();

		_hider->hide();
		QPixmap animCache = myGrab(this, QRect(0, _playerHeight, _dialogsWidth, height() - _playerHeight));

		onHistoryShown(0, 0);
		if (overview) {
			overview->hide();
		} else if (profile) {
			profile->hide();
		} else {
			history.hide();
		}
		dialogs.show();
		resizeEvent(0);
		dialogs.animShow(animCache);
		App::wnd()->getTitle()->updateBackButton();
	}
}

void MainWidget::forwardLayer(int32 forwardSelected) {
	hiderLayer((forwardSelected < 0) ? (new HistoryHider(this)) : (new HistoryHider(this, forwardSelected > 0)));
}

void MainWidget::deleteLayer(int32 selectedCount) {
	QString str((selectedCount < 0) ? lang(selectedCount < -1 ? lng_selected_cancel_sure_this : lng_selected_delete_sure_this) : lng_selected_delete_sure(lt_count, selectedCount));
	QString btn(lang((selectedCount < -1) ? lng_selected_upload_stop : lng_box_delete)), cancel(lang((selectedCount < -1) ? lng_continue : lng_cancel));
	ConfirmBox *box = new ConfirmBox(str, btn, st::defaultBoxButton, cancel);
	if (selectedCount < 0) {
		if (selectedCount < -1) {
			if (HistoryItem *item = App::contextItem()) {
				App::uploader()->pause(item->fullId());
				connect(box, SIGNAL(destroyed(QObject*)), App::uploader(), SLOT(unpause()));
			}
		}
		connect(box, SIGNAL(confirmed()), overview ? overview : static_cast<QWidget*>(&history), SLOT(onDeleteContextSure()));
	} else {
		connect(box, SIGNAL(confirmed()), overview ? overview : static_cast<QWidget*>(&history), SLOT(onDeleteSelectedSure()));
	}
	Ui::showLayer(box);
}

void MainWidget::shareContactLayer(UserData *contact) {
	hiderLayer(new HistoryHider(this, contact));
}

void MainWidget::shareUrlLayer(const QString &url, const QString &text) {
	hiderLayer(new HistoryHider(this, url, text));
}

bool MainWidget::selectingPeer(bool withConfirm) {
	return _hider ? (withConfirm ? _hider->withConfirm() : true) : false;
}

void MainWidget::offerPeer(PeerId peer) {
	Ui::hideLayer();
	if (_hider->offerPeer(peer) && !cWideMode()) {
		_forwardConfirm = new ConfirmBox(_hider->offeredText(), lang(lng_forward_send));
		connect(_forwardConfirm, SIGNAL(confirmed()), _hider, SLOT(forward()));
		connect(_forwardConfirm, SIGNAL(cancelled()), this, SLOT(onForwardCancel()));
		connect(_forwardConfirm, SIGNAL(destroyed(QObject*)), this, SLOT(onForwardCancel(QObject*)));
		Ui::showLayer(_forwardConfirm);
	}
}

void MainWidget::onForwardCancel(QObject *obj) {
	if (!obj || obj == _forwardConfirm) {
		if (_forwardConfirm) {
			if (!obj) _forwardConfirm->startHide();
			_forwardConfirm = 0;
		}
		if (_hider) _hider->offerPeer(0);
	}
}

void MainWidget::dialogsActivate() {
	dialogs.activate();
}

DragState MainWidget::getDragState(const QMimeData *mime) {
	return history.getDragState(mime);
}

bool MainWidget::leaveChatFailed(PeerData *peer, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	if (error.type() == qstr("USER_NOT_PARTICIPANT") || error.type() == qstr("CHAT_ID_INVALID") || error.type() == qstr("PEER_ID_INVALID")) { // left this chat already
		deleteConversation(peer);
		return true;
	}
	return false;
}

void MainWidget::deleteHistoryAfterLeave(PeerData *peer, const MTPUpdates &updates) {
	sentUpdatesReceived(updates);
	deleteConversation(peer);
}

void MainWidget::deleteHistoryPart(PeerData *peer, const MTPmessages_AffectedHistory &result) {
	const MTPDmessages_affectedHistory &d(result.c_messages_affectedHistory());
	if (ptsUpdated(d.vpts.v, d.vpts_count.v)) {
		ptsApplySkippedUpdates();
		App::emitPeerUpdated();
	}

	int32 offset = d.voffset.v;
	if (!MTP::authedId()) return;
	if (offset <= 0) {
		cRefReportSpamStatuses().remove(peer->id);
		Local::writeReportSpamStatuses();
		return;
	}

	MTP::send(MTPmessages_DeleteHistory(peer->input, d.voffset), rpcDone(&MainWidget::deleteHistoryPart, peer));
}

void MainWidget::deleteMessages(PeerData *peer, const QVector<MTPint> &ids) {
	if (peer->isChannel()) {
		MTP::send(MTPchannels_DeleteMessages(peer->asChannel()->inputChannel, MTP_vector<MTPint>(ids)), rpcDone(&MainWidget::messagesAffected, peer));
	} else {
		MTP::send(MTPmessages_DeleteMessages(MTP_vector<MTPint>(ids)), rpcDone(&MainWidget::messagesAffected, peer));
	}
}

void MainWidget::deletedContact(UserData *user, const MTPcontacts_Link &result) {
	const MTPDcontacts_link &d(result.c_contacts_link());
	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser), false);
	App::feedUserLink(MTP_int(peerToUser(user->id)), d.vmy_link, d.vforeign_link, false);
	App::emitPeerUpdated();
}

void MainWidget::removeDialog(History *history) {
	dialogs.removeDialog(history);
}

void MainWidget::deleteConversation(PeerData *peer, bool deleteHistory) {
	if (activePeer() == peer) {
		Ui::showChatsList();
	}
	if (History *h = App::historyLoaded(peer->id)) {
		removeDialog(h);
		if (peer->isMegagroup() && peer->asChannel()->mgInfo->migrateFromPtr) {
			if (History *migrated = App::historyLoaded(peer->asChannel()->mgInfo->migrateFromPtr->id)) {
				if (migrated->lastMsg) { // return initial dialog
					migrated->setLastMessage(migrated->lastMsg);
				} else {
					checkPeerHistory(migrated->peer);
				}
			}
		}
		h->clear();
		h->newLoaded = true;
		h->oldLoaded = deleteHistory;
		if (h->isChannel()) {
			h->asChannelHistory()->clearOther();
		}
	}
	if (peer->isChannel()) {
		peer->asChannel()->ptsWaitingForShortPoll(-1);
	}
	if (deleteHistory) {
		MTP::send(MTPmessages_DeleteHistory(peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, peer));
	}
}

void MainWidget::clearHistory(PeerData *peer) {
	if (History *h = App::historyLoaded(peer->id)) {
		if (h->lastMsg) {
			Local::addSavedPeer(h->peer, h->lastMsg->date);
		}
		h->clear();
		h->newLoaded = h->oldLoaded = true;
	}
	Ui::showPeerHistory(peer->id, ShowAtUnreadMsgId);
	MTP::send(MTPmessages_DeleteHistory(peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, peer));
}

void MainWidget::addParticipants(PeerData *chatOrChannel, const QVector<UserData*> &users) {
	if (chatOrChannel->isChat()) {
		for (QVector<UserData*>::const_iterator i = users.cbegin(), e = users.cend(); i != e; ++i) {
			MTP::send(MTPmessages_AddChatUser(chatOrChannel->asChat()->inputChat, (*i)->inputUser, MTP_int(ForwardOnAdd)), rpcDone(&MainWidget::sentUpdatesReceived), rpcFail(&MainWidget::addParticipantFail, *i), 0, 5);
		}
	} else if (chatOrChannel->isChannel()) {
		QVector<MTPInputUser> inputUsers;
		inputUsers.reserve(qMin(users.size(), int(MaxUsersPerInvite)));
		for (QVector<UserData*>::const_iterator i = users.cbegin(), e = users.cend(); i != e; ++i) {
			inputUsers.push_back((*i)->inputUser);
			if (inputUsers.size() == MaxUsersPerInvite) {
				MTP::send(MTPchannels_InviteToChannel(chatOrChannel->asChannel()->inputChannel, MTP_vector<MTPInputUser>(inputUsers)), rpcDone(&MainWidget::inviteToChannelDone, chatOrChannel->asChannel()), rpcFail(&MainWidget::addParticipantsFail, chatOrChannel->asChannel()), 0, 5);
				inputUsers.clear();
			}
		}
		if (!inputUsers.isEmpty()) {
			MTP::send(MTPchannels_InviteToChannel(chatOrChannel->asChannel()->inputChannel, MTP_vector<MTPInputUser>(inputUsers)), rpcDone(&MainWidget::inviteToChannelDone, chatOrChannel->asChannel()), rpcFail(&MainWidget::addParticipantsFail, chatOrChannel->asChannel()), 0, 5);
		}
	}
}

bool MainWidget::addParticipantFail(UserData *user, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	QString text = lang(lng_failed_add_participant);
	if (error.type() == "USER_LEFT_CHAT") { // trying to return banned user to his group
	} else if (error.type() == "USER_NOT_MUTUAL_CONTACT") { // trying to return user who does not have me in contacts
		text = lang(lng_failed_add_not_mutual);
	} else if (error.type() == "USER_ALREADY_PARTICIPANT" && user->botInfo) {
		text = lang(lng_bot_already_in_group);
	} else if (error.type() == "PEER_FLOOD") {
		text = lng_cant_invite_not_contact(lt_more_info, textcmdLink(qsl("https://telegram.org/faq?_hash=can-39t-send-messages-to-non-contacts"), lang(lng_cant_more_info)));
	}
	Ui::showLayer(new InformBox(text));
	return false;
}

bool MainWidget::addParticipantsFail(ChannelData *channel, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	QString text = lang(lng_failed_add_participant);
	if (error.type() == "USER_LEFT_CHAT") { // trying to return banned user to his group
	} else if (error.type() == "USER_NOT_MUTUAL_CONTACT") { // trying to return user who does not have me in contacts
		text = lang(channel->isMegagroup() ? lng_failed_add_not_mutual : lng_failed_add_not_mutual_channel);
	} else if (error.type() == "PEER_FLOOD") {
		text = lng_cant_invite_not_contact(lt_more_info, textcmdLink(qsl("https://telegram.org/faq?_hash=can-39t-send-messages-to-non-contacts"), lang(lng_cant_more_info)));
	}
	Ui::showLayer(new InformBox(text));
	return false;
}

void MainWidget::kickParticipant(ChatData *chat, UserData *user) {
	MTP::send(MTPmessages_DeleteChatUser(chat->inputChat, user->inputUser), rpcDone(&MainWidget::sentUpdatesReceived), rpcFail(&MainWidget::kickParticipantFail, chat));
	Ui::hideLayer();
	Ui::showPeerHistory(chat->id, ShowAtTheEndMsgId);
}

bool MainWidget::kickParticipantFail(ChatData *chat, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	error.type();
	return false;
}

void MainWidget::checkPeerHistory(PeerData *peer) {
	if (peer->isChannel() && !peer->isMegagroup()) {
		MTP::send(MTPchannels_GetImportantHistory(peer->asChannel()->inputChannel, MTP_int(0), MTP_int(0), MTP_int(1), MTP_int(0), MTP_int(0)), rpcDone(&MainWidget::checkedHistory, peer));
	} else {
		MTP::send(MTPmessages_GetHistory(peer->input, MTP_int(0), MTP_int(0), MTP_int(1), MTP_int(0), MTP_int(0)), rpcDone(&MainWidget::checkedHistory, peer));
	}
}

void MainWidget::checkedHistory(PeerData *peer, const MTPmessages_Messages &result) {
	const QVector<MTPMessage> *v = 0;
	const QVector<MTPMessageGroup> *collapsed = 0;
	switch (result.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &d(result.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &d(result.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_channelMessages: {
		const MTPDmessages_channelMessages &d(result.c_messages_channelMessages());
		if (peer && peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (MainWidget::checkedHistory)"));
		}

		collapsed = &d.vcollapsed.c_vector().v;
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;
	}
	if (!v) return;

	if (v->isEmpty()) {
		if (peer->isChat() && peer->asChat()->haveLeft()) {
			deleteConversation(peer, false);
		} else if (peer->isChannel()) {
			if (peer->asChannel()->inviter > 0 && peer->asChannel()->amIn()) {
				if (UserData *from = App::userLoaded(peer->asChannel()->inviter)) {
					History *h = App::history(peer->id);
					h->clear(true);
					h->addNewerSlice(QVector<MTPMessage>(), 0);
					h->asChannelHistory()->insertJoinedMessage(true);
					history.peerMessagesUpdated(h->peer->id);
				}
			}
		} else {
			History *h = App::historyLoaded(peer->id);
			if (h) Local::addSavedPeer(peer, h->lastMsgDate);
		}
	} else {
		History *h = App::history(peer->id);
		if (!h->lastMsg) {
			HistoryItem *item = h->addNewMessage((*v)[0], NewMessageLast);
			if (collapsed && !collapsed->isEmpty() && collapsed->at(0).type() == mtpc_messageGroup && h->isChannel()) {
				if (collapsed->at(0).c_messageGroup().vmax_id.v > item->id) {
					if (h->asChannelHistory()->onlyImportant()) {
						h->asChannelHistory()->clearOther();
					} else {
						h->setNotLoadedAtBottom();
					}
				}
			}
		}
		if (!h->lastMsgDate.isNull() && h->loadedAtBottom()) {
			if (peer->isChannel() && peer->asChannel()->inviter > 0 && h->lastMsgDate <= peer->asChannel()->inviteDate && peer->asChannel()->amIn()) {
				if (UserData *from = App::userLoaded(peer->asChannel()->inviter)) {
					h->asChannelHistory()->insertJoinedMessage(true);
					history.peerMessagesUpdated(h->peer->id);
				}
			}
		}
	}
}

bool MainWidget::sendMessageFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	if (error.type() == qsl("PEER_FLOOD")) {
		Ui::showLayer(new InformBox(lng_cant_send_to_not_contact(lt_more_info, textcmdLink(qsl("https://telegram.org/faq?_hash=can-39t-send-messages-to-non-contacts"), lang(lng_cant_more_info)))));
		return true;
	}
	return false;
}

void MainWidget::onCacheBackground() {
	const QPixmap &bg(*cChatBackground());
	if (cTileBackground()) {
		QImage result(_willCacheFor.width() * cIntRetinaFactor(), _willCacheFor.height() * cIntRetinaFactor(), QImage::Format_RGB32);
        result.setDevicePixelRatio(cRetinaFactor());
		{
			QPainter p(&result);
			int left = 0, top = 0, right = _willCacheFor.width(), bottom = _willCacheFor.height();
			float64 w = bg.width() / cRetinaFactor(), h = bg.height() / cRetinaFactor();
			int sx = 0, sy = 0, cx = qCeil(_willCacheFor.width() / w), cy = qCeil(_willCacheFor.height() / h);
			for (int i = sx; i < cx; ++i) {
				for (int j = sy; j < cy; ++j) {
					p.drawPixmap(QPointF(i * w, j * h), bg);
				}
			}
		}
		_cachedX = 0;
		_cachedY = 0;
		_cachedBackground = QPixmap::fromImage(result);
	} else {
		QRect to, from;
		backgroundParams(_willCacheFor, to, from);
		_cachedX = to.x();
		_cachedY = to.y();
		_cachedBackground = QPixmap::fromImage(bg.toImage().copy(from).scaled(to.width() * cIntRetinaFactor(), to.height() * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		_cachedBackground.setDevicePixelRatio(cRetinaFactor());
	}
	_cachedFor = _willCacheFor;
}

void MainWidget::forwardSelectedItems() {
	if (overview) {
		overview->onForwardSelected();
	} else {
		history.onForwardSelected();
	}
}

void MainWidget::deleteSelectedItems() {
	if (overview) {
		overview->onDeleteSelected();
	} else {
		history.onDeleteSelected();
	}
}

void MainWidget::clearSelectedItems() {
	if (overview) {
		overview->onClearSelected();
	} else {
		history.onClearSelected();
	}
}

DialogsIndexed &MainWidget::contactsList() {
	return dialogs.contactsList();
}

DialogsIndexed &MainWidget::dialogsList() {
	return dialogs.dialogsList();
}

void MainWidget::sendMessage(History *hist, const QString &text, MsgId replyTo, bool broadcast, WebPageId webPageId) {
	readServerHistory(hist, false);
	history.fastShowAtEnd(hist);

	if (!hist || !history.canSendMessages(hist->peer)) {
		return;
	}

	saveRecentHashtags(text);

	EntitiesInText sendingEntities, leftEntities;
	QString sendingText, leftText = prepareTextWithEntities(text, leftEntities, itemTextOptions(hist, App::self()).flags);

	if (replyTo < 0) replyTo = history.replyToId();
	while (textSplit(sendingText, sendingEntities, leftText, leftEntities, MaxMessageSize)) {
		FullMsgId newId(peerToChannel(hist->peer->id), clientMsgId());
		uint64 randomId = MTP::nonce<uint64>();

		trimTextWithEntities(sendingText, sendingEntities);

		App::historyRegRandom(randomId, newId);
		App::historyRegSentData(randomId, hist->peer->id, sendingText);

		MTPstring msgText(MTP_string(sendingText));
		int32 flags = newMessageFlags(hist->peer) | MTPDmessage::flag_entities; // unread, out
		int32 sendFlags = 0;
		if (replyTo) {
			flags |= MTPDmessage::flag_reply_to_msg_id;
			sendFlags |= MTPmessages_SendMessage::flag_reply_to_msg_id;
		}
		MTPMessageMedia media = MTP_messageMediaEmpty();
		if (webPageId == 0xFFFFFFFFFFFFFFFFULL) {
			sendFlags |= MTPmessages_SendMessage::flag_no_webpage;
		} else if (webPageId) {
			WebPageData *page = App::webPage(webPageId);
			media = MTP_messageMediaWebPage(MTP_webPagePending(MTP_long(page->id), MTP_int(page->pendingTill)));
			flags |= MTPDmessage::flag_media;
		}
		bool fromChannelName = hist->peer->isChannel() && !hist->peer->isMegagroup() && hist->peer->asChannel()->canPublish() && (hist->peer->asChannel()->isBroadcast() || broadcast);
		if (fromChannelName) {
			sendFlags |= MTPmessages_SendMessage::flag_broadcast;
			flags |= MTPDmessage::flag_views;
		} else {
			flags |= MTPDmessage::flag_from_id;
		}
		MTPVector<MTPMessageEntity> localEntities = linksToMTP(sendingEntities), sentEntities = linksToMTP(sendingEntities, true);
		if (!sentEntities.c_vector().v.isEmpty()) {
			sendFlags |= MTPmessages_SendMessage::flag_entities;
		}
		hist->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(fromChannelName ? 0 : MTP::authedId()), peerToMTP(hist->peer->id), MTPPeer(), MTPint(), MTPint(), MTP_int(replyTo), MTP_int(unixtime()), msgText, media, MTPnullMarkup, localEntities, MTP_int(1)), NewMessageUnread);
		hist->sendRequestId = MTP::send(MTPmessages_SendMessage(MTP_int(sendFlags), hist->peer->input, MTP_int(replyTo), msgText, MTP_long(randomId), MTPnullMarkup, sentEntities), rpcDone(&MainWidget::sentUpdatesReceived, randomId), rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
	}

	finishForwarding(hist, broadcast);
}

void MainWidget::saveRecentHashtags(const QString &text) {
	bool found = false;
	QRegularExpressionMatch m;
	RecentHashtagPack recent(cRecentWriteHashtags());
	for (int32 i = 0, next = 0; (m = reHashtag().match(text, i)).hasMatch(); i = next) {
		i = m.capturedStart();
		next = m.capturedEnd();
		if (m.hasMatch()) {
			if (!m.capturedRef(1).isEmpty()) {
				++i;
			}
			if (!m.capturedRef(2).isEmpty()) {
				--next;
			}
		}
		if (!found && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) {
			Local::readRecentHashtagsAndBots();
			recent = cRecentWriteHashtags();
		}
		found = true;
		incrementRecentHashtag(recent, text.mid(i + 1, next - i - 1));
	}
	if (found) {
		cSetRecentWriteHashtags(recent);
		Local::writeRecentHashtagsAndBots();
	}
}

void MainWidget::readServerHistory(History *hist, bool force) {
	if (!hist || (!force && !hist->unreadCount)) return;

	MsgId upTo = hist->inboxRead(0);
	if (hist->isChannel() && !hist->peer->asChannel()->amIn()) {
		return; // no read request for channels that I didn't koin
	}

	ReadRequests::const_iterator i = _readRequests.constFind(hist->peer);
    if (i == _readRequests.cend()) {
		sendReadRequest(hist->peer, upTo);
	} else {
		ReadRequestsPending::iterator i = _readRequestsPending.find(hist->peer);
		if (i == _readRequestsPending.cend()) {
			_readRequestsPending.insert(hist->peer, upTo);
		} else if (i.value() < upTo) {
			i.value() = upTo;
		}
	}
}

uint64 MainWidget::animActiveTimeStart(const HistoryItem *msg) const {
	return history.animActiveTimeStart(msg);
}

void MainWidget::stopAnimActive() {
	history.stopAnimActive();
}

void MainWidget::sendBotCommand(const QString &cmd, MsgId replyTo) {
	history.sendBotCommand(cmd, replyTo);
}

bool MainWidget::insertBotCommand(const QString &cmd, bool specialGif) {
	return history.insertBotCommand(cmd, specialGif);
}

void MainWidget::searchMessages(const QString &query, PeerData *inPeer) {
	App::wnd()->hideMediaview();
	dialogs.searchMessages(query, inPeer);
	if (!cWideMode()) {
		Ui::showChatsList();
	} else {
		dialogs.activate();
	}
}

bool MainWidget::preloadOverview(PeerData *peer, MediaOverviewType type) {
	MTPMessagesFilter filter = typeToMediaFilter(type);
	if (type == OverviewCount) return false;

	History *h = App::history(peer->id);
	if (h->overviewCountLoaded(type) || _overviewPreload[type].constFind(peer) != _overviewPreload[type].cend()) {
		return false;
	}

	int32 flags = (peer->isChannel() && !peer->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
	_overviewPreload[type].insert(peer, MTP::send(MTPmessages_Search(MTP_int(flags), peer->input, MTP_string(""), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0)), rpcDone(&MainWidget::overviewPreloaded, peer), rpcFail(&MainWidget::overviewFailed, peer), 0, 10));
	return true;
}

void MainWidget::preloadOverviews(PeerData *peer) {
	History *h = App::history(peer->id);
	bool sending = false;
	for (int32 i = 0; i < OverviewCount; ++i) {
		if (preloadOverview(peer, MediaOverviewType(i))) {
			sending = true;
		}
	}
	if (sending) {
		MTP::sendAnything();
	}
}

void MainWidget::overviewPreloaded(PeerData *peer, const MTPmessages_Messages &result, mtpRequestId req) {
	MediaOverviewType type = OverviewCount;
	for (int32 i = 0; i < OverviewCount; ++i) {
		OverviewsPreload::iterator j = _overviewPreload[i].find(peer);
		if (j != _overviewPreload[i].end() && j.value() == req) {
			type = MediaOverviewType(i);
			_overviewPreload[i].erase(j);
			break;
		}
	}

	if (type == OverviewCount) return;

	App::history(peer->id)->overviewSliceDone(type, result, true);

	mediaOverviewUpdated(peer, type);
}

void MainWidget::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	if (profile) profile->mediaOverviewUpdated(peer, type);
	if (!_player.isHidden()) _player.mediaOverviewUpdated(peer, type);
	if (overview && (overview->peer() == peer || overview->peer()->migrateFrom() == peer)) {
		overview->mediaOverviewUpdated(peer, type);

		int32 mask = 0;
		History *h = peer ? App::historyLoaded((peer->migrateTo() ? peer->migrateTo() : peer)->id) : 0;
		History *m = (peer && peer->migrateFrom()) ? App::historyLoaded(peer->migrateFrom()->id) : 0;
		if (h) {
			for (int32 i = 0; i < OverviewCount; ++i) {
				if (!h->overview[i].isEmpty() || h->overviewCount(i) > 0 || i == overview->type()) {
					mask |= (1 << i);
				} else if (m && (!m->overview[i].isEmpty() || m->overviewCount(i) > 0)) {
					mask |= (1 << i);
				}
			}
		}
		if (mask != _mediaTypeMask) {
			_mediaType.resetButtons();
			for (int32 i = 0; i < OverviewCount; ++i) {
				if (mask & (1 << i)) {
					switch (i) {
					case OverviewPhotos: connect(_mediaType.addButton(new IconedButton(this, st::dropdownMediaPhotos, lang(lng_media_type_photos))), SIGNAL(clicked()), this, SLOT(onPhotosSelect())); break;
					case OverviewVideos: connect(_mediaType.addButton(new IconedButton(this, st::dropdownMediaVideos, lang(lng_media_type_videos))), SIGNAL(clicked()), this, SLOT(onVideosSelect())); break;
					case OverviewAudioDocuments: connect(_mediaType.addButton(new IconedButton(this, st::dropdownMediaSongs, lang(lng_media_type_songs))), SIGNAL(clicked()), this, SLOT(onSongsSelect())); break;
					case OverviewDocuments: connect(_mediaType.addButton(new IconedButton(this, st::dropdownMediaDocuments, lang(lng_media_type_files))), SIGNAL(clicked()), this, SLOT(onDocumentsSelect())); break;
					case OverviewAudios: connect(_mediaType.addButton(new IconedButton(this, st::dropdownMediaAudios, lang(lng_media_type_audios))), SIGNAL(clicked()), this, SLOT(onAudiosSelect())); break;
					case OverviewLinks: connect(_mediaType.addButton(new IconedButton(this, st::dropdownMediaLinks, lang(lng_media_type_links))), SIGNAL(clicked()), this, SLOT(onLinksSelect())); break;
					}
				}
			}
			_mediaTypeMask = mask;
			_mediaType.move(width() - _mediaType.width(), st::topBarHeight);
			overview->updateTopBarSelection();
		}
	}
}

void MainWidget::changingMsgId(HistoryItem *row, MsgId newId) {
	if (overview) overview->changingMsgId(row, newId);
}

void MainWidget::itemRemoved(HistoryItem *item) {
	api()->itemRemoved(item);
	dialogs.itemRemoved(item);
	if (history.peer() == item->history()->peer || (history.peer() && history.peer() == item->history()->peer->migrateTo())) {
		history.itemRemoved(item);
	}
	if (overview && (overview->peer() == item->history()->peer || (overview->peer() && overview->peer() == item->history()->peer->migrateTo()))) {
		overview->itemRemoved(item);
	}
	if (!_toForward.isEmpty()) {
		SelectedItemSet::iterator i = _toForward.find(item->id);
		if (i != _toForward.cend() && i.value() == item) {
			_toForward.erase(i);
			updateForwardingTexts();
		} else {
			i = _toForward.find(item->id - ServerMaxMsgId);
			if (i != _toForward.cend() && i.value() == item) {
				_toForward.erase(i);
				updateForwardingTexts();
			}
		}
	}
}

bool MainWidget::overviewFailed(PeerData *peer, const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	MediaOverviewType type = OverviewCount;
	for (int32 i = 0; i < OverviewCount; ++i) {
		OverviewsPreload::iterator j = _overviewPreload[i].find(peer);
		if (j != _overviewPreload[i].end() && j.value() == req) {
			_overviewPreload[i].erase(j);
			break;
		}
	}
	return true;
}

void MainWidget::loadMediaBack(PeerData *peer, MediaOverviewType type, bool many) {
	if (_overviewLoad[type].constFind(peer) != _overviewLoad[type].cend()) return;

	History *history = App::history(peer->id);
	if (history->overviewLoaded(type)) return;

	MsgId minId = history->overviewMinId(type);
	int32 limit = (many || history->overview[type].size() > MediaOverviewStartPerPage) ? SearchPerPage : MediaOverviewStartPerPage;
	MTPMessagesFilter filter = typeToMediaFilter(type);
	if (type == OverviewCount) return;

	int32 flags = (peer->isChannel() && !peer->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
	_overviewLoad[type].insert(peer, MTP::send(MTPmessages_Search(MTP_int(flags), peer->input, MTPstring(), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(minId), MTP_int(limit)), rpcDone(&MainWidget::overviewLoaded, history)));
}

void MainWidget::peerUsernameChanged(PeerData *peer) {
	if (profile && profile->peer() == peer) {
		profile->peerUsernameChanged();
	}
	if (App::settings() && peer == App::self()) {
		App::settings()->usernameChanged();
	}
}

void MainWidget::checkLastUpdate(bool afterSleep) {
	uint64 n = getms(true);
	if (_lastUpdateTime && n > _lastUpdateTime + (afterSleep ? NoUpdatesAfterSleepTimeout : NoUpdatesTimeout)) {
		_lastUpdateTime = n;
		MTP::ping();
	}
}

void MainWidget::showAddContact() {
	dialogs.onAddContact();
}

void MainWidget::showNewGroup() {
	dialogs.onNewGroup();
}

void MainWidget::overviewLoaded(History *history, const MTPmessages_Messages &result, mtpRequestId req) {
	OverviewsPreload::iterator it;
	MediaOverviewType type = OverviewCount;
	for (int32 i = 0; i < OverviewCount; ++i) {
		it = _overviewLoad[i].find(history->peer);
		if (it != _overviewLoad[i].cend()) {
			type = MediaOverviewType(i);
			_overviewLoad[i].erase(it);
			break;
		}
	}
	if (type == OverviewCount) return;

	history->overviewSliceDone(type, result);

	if (App::wnd()) App::wnd()->mediaOverviewUpdated(history->peer, type);
}

void MainWidget::sendReadRequest(PeerData *peer, MsgId upTo) {
	if (!MTP::authedId()) return;
	if (peer->isChannel()) {
		_readRequests.insert(peer, qMakePair(MTP::send(MTPchannels_ReadHistory(peer->asChannel()->inputChannel, MTP_int(upTo)), rpcDone(&MainWidget::channelWasRead, peer), rpcFail(&MainWidget::readRequestFail, peer)), upTo));
	} else {
		_readRequests.insert(peer, qMakePair(MTP::send(MTPmessages_ReadHistory(peer->input, MTP_int(upTo)), rpcDone(&MainWidget::historyWasRead, peer), rpcFail(&MainWidget::readRequestFail, peer)), upTo));
	}
}

void MainWidget::channelWasRead(PeerData *peer, const MTPBool &result) {
	readRequestDone(peer);
}

void MainWidget::historyWasRead(PeerData *peer, const MTPmessages_AffectedMessages &result) {
	messagesAffected(peer, result);
	readRequestDone(peer);
}

bool MainWidget::readRequestFail(PeerData *peer, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	readRequestDone(peer);
	return false;
}

void MainWidget::readRequestDone(PeerData *peer) {
	_readRequests.remove(peer);
	ReadRequestsPending::iterator i = _readRequestsPending.find(peer);
	if (i != _readRequestsPending.cend()) {
		sendReadRequest(peer, i.value());
		_readRequestsPending.erase(i);
	}
}

void MainWidget::messagesAffected(PeerData *peer, const MTPmessages_AffectedMessages &result) {
	const MTPDmessages_affectedMessages &d(result.c_messages_affectedMessages());
	if (peer && peer->isChannel()) {
		if (peer->asChannel()->ptsUpdated(d.vpts.v, d.vpts_count.v)) {
			peer->asChannel()->ptsApplySkippedUpdates();
			App::emitPeerUpdated();
		}
	} else {
		if (ptsUpdated(d.vpts.v, d.vpts_count.v)) {
			ptsApplySkippedUpdates();
			App::emitPeerUpdated();
		}
	}
	if (History *h = App::historyLoaded(peer ? peer->id : 0)) {
		if (!h->lastMsg) {
			checkPeerHistory(peer);
		}
	}
}

void MainWidget::videoLoadProgress(FileLoader *loader) {
	mtpFileLoader *l = loader ? loader->mtpLoader() : 0;
	if (!l) return;

	VideoData *video = App::video(l->objId());
	if (video->loaded()) {
		video->performActionOnLoad();
	}

	const VideoItems &items(App::videoItems());
	VideoItems::const_iterator i = items.constFind(video);
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			Ui::repaintHistoryItem(j.key());
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
		connect(box, SIGNAL(confirmed()), this, SLOT(onDownloadPathSettings()));
	}
	Ui::showLayer(box);
}

void MainWidget::onDownloadPathSettings() {
	cSetDownloadPath(QString());
	cSetDownloadPathBookmark(QByteArray());
	DownloadPathBox *box = new DownloadPathBox();
	if (App::wnd() && App::wnd()->settingsWidget()) {
		connect(box, SIGNAL(closed()), App::wnd()->settingsWidget(), SLOT(onDownloadPathEdited()));
	}
	Ui::showLayer(box);
}

void MainWidget::ui_showPeerHistoryAsync(quint64 peerId, qint32 showAtMsgId) {
	Ui::showPeerHistory(peerId, showAtMsgId);
}

void MainWidget::videoLoadFailed(FileLoader *loader, bool started) {
	mtpFileLoader *l = loader ? loader->mtpLoader() : 0;
	if (!l) return;

	loadFailed(l, started, SLOT(videoLoadRetry()));
	VideoData *video = App::video(l->objId());
	if (video) {
		if (video->loading()) video->cancel();
		video->status = FileDownloadFailed;
	}
}

void MainWidget::videoLoadRetry() {
	Ui::hideLayer();
	VideoData *video = App::video(failedObjId);
	if (video) video->save(failedFileName);
}

void MainWidget::audioLoadProgress(FileLoader *loader) {
	mtpFileLoader *l = loader ? loader->mtpLoader() : 0;
	if (!l) return;

	AudioData *audio = App::audio(l->objId());
	if (audio->loaded()) {
		audio->performActionOnLoad();
	}

	const AudioItems &items(App::audioItems());
	AudioItems::const_iterator i = items.constFind(audio);
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			Ui::repaintHistoryItem(j.key());
		}
	}
}

void MainWidget::audioPlayProgress(const AudioMsgId &audioId) {
	AudioMsgId playing;
	AudioPlayerState state = AudioPlayerStopped;
	audioPlayer()->currentState(&playing, &state);
	if (playing == audioId && state == AudioPlayerStoppedAtStart) {
		audioPlayer()->clearStoppedAtStart(audioId);

		AudioData *audio = audioId.audio;
		QString already = audio->already(true);
		if (already.isEmpty() && !audio->data().isEmpty()) {
			bool mp3 = (audio->mime == qstr("audio/mp3"));
			QString filename = saveFileName(lang(lng_save_audio), mp3 ? qsl("MP3 Audio (*.mp3);;All files (*.*)") : qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), mp3 ? qsl(".mp3") : qsl(".ogg"), false);
			if (!filename.isEmpty()) {
				QFile f(filename);
				if (f.open(QIODevice::WriteOnly)) {
					if (f.write(audio->data()) == audio->data().size()) {
						f.close();
						already = filename;
						audio->setLocation(FileLocation(StorageFilePartial, filename));
						Local::writeFileLocation(mediaKey(mtpToLocationType(mtpc_inputAudioFileLocation), audio->dc, audio->id), FileLocation(mtpToStorageType(mtpc_storage_filePartial), filename));
					}
				}
			}
		}
		if (!already.isEmpty()) {
			psOpenFile(already);
		}
	}

	if (HistoryItem *item = App::histItemById(audioId.msgId)) {
		Ui::repaintHistoryItem(item);
	}
}

void MainWidget::documentPlayProgress(const SongMsgId &songId) {
	SongMsgId playing;
	AudioPlayerState playingState = AudioPlayerStopped;
	int64 playingPosition = 0, playingDuration = 0;
	int32 playingFrequency = 0;
	audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
	if (playing == songId && playingState == AudioPlayerStoppedAtStart) {
		playingState = AudioPlayerStopped;
		audioPlayer()->clearStoppedAtStart(songId);

		DocumentData *document = songId.song;
		QString already = document->already(true);
		if (already.isEmpty() && !document->data().isEmpty()) {
			QString name = document->name, filter;
			MimeType mimeType = mimeTypeForName(document->mime);
			QStringList p = mimeType.globPatterns();
			QString pattern = p.isEmpty() ? QString() : p.front();
			if (name.isEmpty()) {
				name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
			}
			if (pattern.isEmpty()) {
				filter = QString();
			} else {
				filter = mimeType.filterString() + qsl(";;All files (*.*)");
			}
			QString filename = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, false);
			if (!filename.isEmpty()) {
				QFile f(filename);
				if (f.open(QIODevice::WriteOnly)) {
					if (f.write(document->data()) == document->data().size()) {
						f.close();
						already = filename;
						document->setLocation(FileLocation(StorageFilePartial, filename));
						Local::writeFileLocation(mediaKey(mtpToLocationType(mtpc_inputDocumentFileLocation), document->dc, document->id), FileLocation(mtpToStorageType(mtpc_storage_filePartial), filename));
					}
				}
			}
		}
		if (!already.isEmpty()) {
			psOpenFile(already);
		}
	}

	if (playing == songId) {
		_player.updateState(playing, playingState, playingPosition, playingDuration, playingFrequency);

		if (!(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing && !_a_show.animating()) {
			if (_player.isHidden()) {
				_player.clearSelection();
				_player.show();
				_playerHeight = _contentScrollAddToY = _player.height();
				resizeEvent(0);
			}
		}
	}

	if (HistoryItem *item = App::histItemById(songId.msgId)) {
		Ui::repaintHistoryItem(item);
	}
}

void MainWidget::hidePlayer() {
	if (!_player.isHidden()) {
		_player.hide();
		_contentScrollAddToY = -_player.height();
		_playerHeight = 0;
		resizeEvent(0);
	}
}

void MainWidget::audioLoadFailed(FileLoader *loader, bool started) {
	mtpFileLoader *l = loader ? loader->mtpLoader() : 0;
	if (!l) return;

	loadFailed(l, started, SLOT(audioLoadRetry()));
	AudioData *audio = App::audio(l->objId());
	if (audio) {
		if (audio->loading()) audio->cancel();
		audio->status = FileDownloadFailed;
	}
}

void MainWidget::audioLoadRetry() {
	Ui::hideLayer();
	AudioData *audio = App::audio(failedObjId);
	if (audio) audio->save(failedFileName);
}

void MainWidget::documentLoadProgress(FileLoader *loader) {
	mtpFileLoader *l = loader ? loader->mtpLoader() : 0;
	if (!l) return;

	DocumentData *document = App::document(l->objId());
	if (document->loaded()) {
		document->performActionOnLoad();
	}

	const DocumentItems &items(App::documentItems());
	DocumentItems::const_iterator i = items.constFind(document);
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			Ui::repaintHistoryItem(j.key());
		}
	}
	App::wnd()->documentUpdated(document);

	if (!document->loaded() && document->loading() && document->song() && audioPlayer()) {
		SongMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		int64 playingPosition = 0, playingDuration = 0;
		int32 playingFrequency = 0;
		audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
		if (playing.song == document && !_player.isHidden()) {
			_player.updateState(playing, playingState, playingPosition, playingDuration, playingFrequency);
		}
	}
}

void MainWidget::documentLoadFailed(FileLoader *loader, bool started) {
	mtpFileLoader *l = loader ? loader->mtpLoader() : 0;
	if (!l) return;

	loadFailed(l, started, SLOT(documentLoadRetry()));
	DocumentData *document = App::document(l->objId());
	if (document) {
		if (document->loading()) document->cancel();
		document->status = FileDownloadFailed;
	}
}

void MainWidget::documentLoadRetry() {
	Ui::hideLayer();
	DocumentData *document = App::document(failedObjId);
	if (document) document->save(failedFileName);
}

void MainWidget::inlineResultLoadProgress(FileLoader *loader) {
	//InlineResult *result = App::inlineResultFromLoader(loader);
	//if (!result) return;

	//result->loaded();

	//Ui::repaintInlineItem();
}

void MainWidget::inlineResultLoadFailed(FileLoader *loader, bool started) {
	//InlineResult *result = App::inlineResultFromLoader(loader);
	//if (!result) return;

	//result->loaded();

	//Ui::repaintInlineItem();
}

void MainWidget::audioMarkRead(AudioData *data) {
	const AudioItems &items(App::audioItems());
	AudioItems::const_iterator i = items.constFind(data);
	if (i != items.cend()) {
		mediaMarkRead(i.value());
	}
}

void MainWidget::videoMarkRead(VideoData *data) {
	const VideoItems &items(App::videoItems());
	VideoItems::const_iterator i = items.constFind(data);
	if (i != items.cend()) {
		mediaMarkRead(i.value());
	}
}

void MainWidget::mediaMarkRead(const HistoryItemsMap &items) {
	QVector<MTPint> markedIds;
	markedIds.reserve(items.size());
	for (HistoryItemsMap::const_iterator j = items.cbegin(), e = items.cend(); j != e; ++j) {
		if (!j.key()->out() && j.key()->isMediaUnread()) {
			j.key()->markMediaRead();
			if (j.key()->id > 0) {
				markedIds.push_back(MTP_int(j.key()->id));
			}
		}
	}
	if (!markedIds.isEmpty()) {
		MTP::send(MTPmessages_ReadMessageContents(MTP_vector<MTPint>(markedIds)), rpcDone(&MainWidget::messagesAffected, (PeerData*)0));
	}
}

void MainWidget::onParentResize(const QSize &newSize) {
	resize(newSize);
}

void MainWidget::updateOnlineDisplay() {
	if (this != App::main()) return;
	history.updateOnlineDisplay(history.x(), width() - history.x() - st::sysBtnDelta * 2 - st::sysCls.img.pxWidth() - st::sysRes.img.pxWidth() - st::sysMin.img.pxWidth());
	if (profile) profile->updateOnlineDisplay();
	if (App::wnd()->settingsWidget()) App::wnd()->settingsWidget()->updateOnlineDisplay();
}

void MainWidget::onSendFileConfirm(const FileLoadResultPtr &file, bool ctrlShiftEnter) {
	bool lastKeyboardUsed = history.lastForceReplyReplied(FullMsgId(peerToChannel(file->to.peer), file->to.replyTo));
	history.confirmSendFile(file, ctrlShiftEnter);
	history.cancelReply(lastKeyboardUsed);
}

void MainWidget::onSendFileCancel(const FileLoadResultPtr &file) {
	history.cancelSendFile(file);
}

void MainWidget::onShareContactConfirm(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool ctrlShiftEnter) {
	history.confirmShareContact(phone, fname, lname, replyTo, ctrlShiftEnter);
}

void MainWidget::onShareContactCancel() {
	history.cancelShareContact();
}

void MainWidget::dialogsCancelled() {
	if (_hider) {
		_hider->startHide();
		noHider(_hider);
	}
	history.activate();
}

void MainWidget::serviceNotification(const QString &msg, const MTPMessageMedia &media) {
	int32 flags = MTPDmessage::flag_unread | MTPDmessage::flag_entities | MTPDmessage::flag_from_id;
	QString sendingText, leftText = msg;
	EntitiesInText sendingEntities, leftEntities = textParseEntities(leftText, _historyTextNoMonoOptions.flags);
	HistoryItem *item = 0;
	while (textSplit(sendingText, sendingEntities, leftText, leftEntities, MaxMessageSize)) {
		MTPVector<MTPMessageEntity> localEntities = linksToMTP(sendingEntities);
		item = App::histories().addNewMessage(MTP_message(MTP_int(flags), MTP_int(clientMsgId()), MTP_int(ServiceUserId), MTP_peerUser(MTP_int(MTP::authedId())), MTPPeer(), MTPint(), MTPint(), MTPint(), MTP_int(unixtime()), MTP_string(sendingText), media, MTPnullMarkup, localEntities, MTPint()), NewMessageUnread);
	}
	if (item) {
		history.peerMessagesUpdated(item->history()->peer->id);
	}
}

void MainWidget::serviceHistoryDone(const MTPmessages_Messages &msgs) {
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &d(msgs.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageLast);
	} break;

	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &d(msgs.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageLast);
	} break;

	case mtpc_messages_channelMessages: {
		const MTPDmessages_channelMessages &d(msgs.c_messages_channelMessages());
		LOG(("API Error: received messages.channelMessages! (MainWidget::serviceHistoryDone)"));
		if (d.has_collapsed()) { // should not be returned
			LOG(("API Error: channels.getMessages and messages.getMessages should not return collapsed groups! (MainWidget::serviceHistoryDone)"));
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageLast);
	} break;
	}

	App::wnd()->showDelayedServiceMsgs();
}

bool MainWidget::serviceHistoryFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	App::wnd()->showDelayedServiceMsgs();
	return false;
}

bool MainWidget::isIdle() const {
	return _isIdle;
}

void MainWidget::clearCachedBackground() {
	_cachedBackground = QPixmap();
	_cacheBackgroundTimer.stop();
}

QPixmap MainWidget::cachedBackground(const QRect &forRect, int &x, int &y) {
	if (!_cachedBackground.isNull() && forRect == _cachedFor) {
		x = _cachedX;
		y = _cachedY;
		return _cachedBackground;
	}
	if (_willCacheFor != forRect || !_cacheBackgroundTimer.isActive()) {
		_willCacheFor = forRect;
		_cacheBackgroundTimer.start(CacheBackgroundTimeout);
	}
	return QPixmap();
}

void MainWidget::backgroundParams(const QRect &forRect, QRect &to, QRect &from) const {
	const QSize &bg(cChatBackground()->size());
	if (uint64(bg.width()) * forRect.height() > uint64(bg.height()) * forRect.width()) {
		float64 pxsize = forRect.height() / float64(bg.height());
		int takewidth = qCeil(forRect.width() / pxsize);
		if (takewidth > bg.width()) {
			takewidth = bg.width();
		} else if ((bg.width() % 2) != (takewidth % 2)) {
			++takewidth;
		}
		to = QRect(int((forRect.width() - takewidth * pxsize) / 2.), 0, qCeil(takewidth * pxsize), forRect.height());
		from = QRect((bg.width() - takewidth) / 2, 0, takewidth, bg.height());
	} else {
		float64 pxsize = forRect.width() / float64(bg.width());
		int takeheight = qCeil(forRect.height() / pxsize);
		if (takeheight > bg.height()) {
			takeheight = bg.height();
		} else if ((bg.height() % 2) != (takeheight % 2)) {
			++takeheight;
		}
		to = QRect(0, int((forRect.height() - takeheight * pxsize) / 2.), forRect.width(), qCeil(takeheight * pxsize));
		from = QRect(0, (bg.height() - takeheight) / 2, bg.width(), takeheight);
	}
}

void MainWidget::updateScrollColors() {
	history.updateScrollColors();
	if (overview) overview->updateScrollColors();
}

void MainWidget::setChatBackground(const App::WallPaper &wp) {
	_background = new App::WallPaper(wp);
	_background->full->loadEvenCancelled();
	checkChatBackground();
}

bool MainWidget::chatBackgroundLoading() {
	return !!_background;
}

void MainWidget::checkChatBackground() {
	if (_background) {
		if (_background->full->loaded()) {
			if (_background->full->isNull()) {
				App::initBackground();
			} else if (_background->id == 0 || _background->id == DefaultChatBackground) {
				App::initBackground(_background->id);
			} else {
				App::initBackground(_background->id, _background->full->pix().toImage());
			}
			delete _background;
			_background = 0;
			QTimer::singleShot(0, this, SLOT(update()));
		}
	}
}

ImagePtr MainWidget::newBackgroundThumb() {
	return _background ? _background->thumb : ImagePtr();
}

ApiWrap *MainWidget::api() {
	return _api;
}

void MainWidget::updateReplyTo() {
	history.updateReplyTo(true);
}

void MainWidget::updateBotKeyboard(History *h) {
	history.updateBotKeyboard(h);
}

void MainWidget::pushReplyReturn(HistoryItem *item) {
	history.pushReplyReturn(item);
}

void MainWidget::setInnerFocus() {
	if (_hider || !history.peer()) {
		if (_hider && _hider->wasOffered()) {
			_hider->setFocus();
		} else if (overview) {
			overview->activate();
		} else if (profile) {
			profile->activate();
		} else {
			dialogsActivate();
		}
	} else if (overview) {
		overview->activate();
	} else if (profile) {
		profile->activate();
	} else {
		history.setInnerFocus();
	}
}

void MainWidget::scheduleViewIncrement(HistoryItem *item) {
	PeerData *peer = item->history()->peer;
	ViewsIncrement::iterator i = _viewsIncremented.find(peer);
	if (i != _viewsIncremented.cend()) {
		if (i.value().contains(item->id)) return;
	} else {
		i = _viewsIncremented.insert(peer, ViewsIncrementMap());
	}
	i.value().insert(item->id, true);
	ViewsIncrement::iterator j = _viewsToIncrement.find(peer);
	if (j == _viewsToIncrement.cend()) {
		j = _viewsToIncrement.insert(peer, ViewsIncrementMap());
		_viewsIncrementTimer.start(SendViewsTimeout);
	}
	j.value().insert(item->id, true);
}

void MainWidget::onViewsIncrement() {
	for (ViewsIncrement::iterator i = _viewsToIncrement.begin(); i != _viewsToIncrement.cend();) {
		if (_viewsIncrementRequests.contains(i.key())) {
			++i;
			continue;
		}

		QVector<MTPint> ids;
		ids.reserve(i.value().size());
		for (ViewsIncrementMap::const_iterator j = i.value().cbegin(), end = i.value().cend(); j != end; ++j) {
			ids.push_back(MTP_int(j.key()));
		}
		mtpRequestId req = MTP::send(MTPmessages_GetMessagesViews(i.key()->input, MTP_vector<MTPint>(ids), MTP_bool(true)), rpcDone(&MainWidget::viewsIncrementDone, ids), rpcFail(&MainWidget::viewsIncrementFail), 0, 5);
		_viewsIncrementRequests.insert(i.key(), req);
		i = _viewsToIncrement.erase(i);
	}
}

void MainWidget::viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req) {
	const QVector<MTPint> &v(result.c_vector().v);
	if (ids.size() == v.size()) {
		for (ViewsIncrementRequests::iterator i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
			if (i.value() == req) {
				PeerData *peer = i.key();
				ChannelId channel = peerToChannel(peer->id);
				for (int32 j = 0, l = ids.size(); j < l; ++j) {
					if (HistoryItem *item = App::histItemById(channel, ids.at(j).v)) {
						item->setViewsCount(v.at(j).v);
					}
				}
				_viewsIncrementRequests.erase(i);
				break;
			}
		}
	}
	if (!_viewsToIncrement.isEmpty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.start(SendViewsTimeout);
	}
}

bool MainWidget::viewsIncrementFail(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	for (ViewsIncrementRequests::iterator i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
		if (i.value() == req) {
			_viewsIncrementRequests.erase(i);
			break;
		}
	}
	if (!_viewsToIncrement.isEmpty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.start(SendViewsTimeout);
	}
	return false;
}

HistoryItem *MainWidget::atTopImportantMsg(int32 &bottomUnderScrollTop) const {
	return history.atTopImportantMsg(bottomUnderScrollTop);
}

void MainWidget::createDialog(History *history) {
	dialogs.createDialog(history);
}

void MainWidget::choosePeer(PeerId peerId, MsgId showAtMsgId) {
	if (selectingPeer()) {
		offerPeer(peerId);
	} else {
		Ui::showPeerHistory(peerId, showAtMsgId);
	}
}

void MainWidget::clearBotStartToken(PeerData *peer) {
	if (peer && peer->isUser() && peer->asUser()->botInfo) {
		peer->asUser()->botInfo->startToken = QString();
	}
}

void MainWidget::contactsReceived() {
	history.contactsReceived();
}

void MainWidget::updateAfterDrag() {
	if (overview) {
		overview->updateAfterDrag();
	} else {
		history.updateAfterDrag();
	}
}

void MainWidget::ctrlEnterSubmitUpdated() {
	history.ctrlEnterSubmitUpdated();
}

void MainWidget::ui_showPeerHistory(quint64 peerId, qint32 showAtMsgId, bool back) {
	if (PeerData *peer = App::peerLoaded(peerId)) {
		if (peer->migrateTo()) {
			peerId = peer->migrateTo()->id;
			if (showAtMsgId > 0) showAtMsgId = -showAtMsgId;
		}
	}
	if (!back && (!peerId || (_stack.size() == 1 && _stack[0]->type() == HistoryStackItem && _stack[0]->peer->id == peerId))) {
		back = true;
	}

	PeerData *wasActivePeer = activePeer();

	Ui::hideLayer();
	if (_hider) {
		_hider->startHide();
		_hider = 0;
	}

	QPixmap animCache, animTopBarCache;
	if (!_a_show.animating() && ((history.isHidden() && (profile || overview)) || (!cWideMode() && (history.isHidden() || !peerId)))) {
		if (peerId) {
			animCache = grabInner();
		} else if (cWideMode()) {
			animCache = myGrab(this, QRect(_dialogsWidth, _playerHeight, width() - _dialogsWidth, height() - _playerHeight));
		} else {
			animCache = myGrab(this, QRect(0, _playerHeight, _dialogsWidth, height() - _playerHeight));
		}
		if (peerId || cWideMode()) {
			animTopBarCache = grabTopBar();
		}
		history.show();
	}
	if (history.peer() && history.peer()->id != peerId) clearBotStartToken(history.peer());
	history.showHistory(peerId, showAtMsgId);

	bool noPeer = (!history.peer() || !history.peer()->id), onlyDialogs = noPeer && !cWideMode();
	if (profile || overview) {
		if (profile) {
			profile->hide();
			profile->clear();
			profile->deleteLater();
			profile->rpcInvalidate();
			profile = 0;
		}
		if (overview) {
			overview->hide();
			overview->clear();
			overview->deleteLater();
			overview->rpcInvalidate();
			overview = 0;
		}
		clearBotStartToken(_peerInStack);
		dlgUpdated();
		_peerInStack = 0;
		_msgIdInStack = 0;
		_stack.clear();
	}
	if (onlyDialogs) {
		_topBar.hide();
		history.hide();
		if (!_a_show.animating()) {
			dialogs.show();
			if (!animCache.isNull()) {
				dialogs.animShow(animCache);
			}
		}
	} else {
		if (noPeer) {
			_topBar.hide();
			resizeEvent(0);
		} else if (wasActivePeer != activePeer()) {
			if (activePeer()->isChannel()) {
				activePeer()->asChannel()->ptsWaitingForShortPoll(WaitForChannelGetDifference);
			}
			_viewsIncremented.remove(activePeer());
		}
		if (!cWideMode() && !dialogs.isHidden()) dialogs.hide();
		if (!_a_show.animating()) {
			if (history.isHidden()) history.show();
			if (!animCache.isNull()) {
				history.animShow(animCache, animTopBarCache, back);
			} else if (App::wnd()) {
				QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));
			}
		}
	}
	//if (wasActivePeer && wasActivePeer->isChannel() && activePeer() != wasActivePeer) {
	//	wasActivePeer->asChannel()->ptsWaitingForShortPoll(false);
	//}

	if (!dialogs.isHidden()) {
		if (!back) {
			dialogs.scrollToPeer(peerId, showAtMsgId);
		}
		dialogs.update();
	}
	App::wnd()->getTitle()->updateBackButton();
}

void MainWidget::peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) {
	if (selectingPeer()) {
		outPeer = 0;
		outMsg = 0;
		return;
	}
	dialogs.peerBefore(inPeer, inMsg, outPeer, outMsg);
}

void MainWidget::peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) {
	if (selectingPeer()) {
		outPeer = 0;
		outMsg = 0;
		return;
	}
	dialogs.peerAfter(inPeer, inMsg, outPeer, outMsg);
}

PeerData *MainWidget::historyPeer() {
	return history.peer();
}

PeerData *MainWidget::peer() {
	return overview ? overview->peer() : history.peer();
}

PeerData *MainWidget::activePeer() {
	return history.peer() ? history.peer() : _peerInStack;
}

MsgId MainWidget::activeMsgId() {
	return history.peer() ? history.msgId() : _msgIdInStack;
}

PeerData *MainWidget::profilePeer() {
	return profile ? profile->peer() : 0;
}

PeerData *MainWidget::overviewPeer() {
	return overview ? overview->peer() : 0;
}

bool MainWidget::mediaTypeSwitch() {
	if (!overview) return false;

	for (int32 i = 0; i < OverviewCount; ++i) {
		if (!(_mediaTypeMask & ~(1 << i))) {
			return false;
		}
	}
	return true;
}

void MainWidget::showMediaOverview(PeerData *peer, MediaOverviewType type, bool back, int32 lastScrollTop) {
	if (peer->migrateTo()) {
		peer = peer->migrateTo();
	}

	App::wnd()->hideSettings();
	if (overview && overview->peer() == peer) {
		if (overview->type() != type) {
			overview->switchType(type);
		} else if (type == OverviewAudioDocuments) { // hack for player
			showBackFromStack();
		}
		return;
	}

	QRect topBarRect = QRect(_topBar.x(), _topBar.y(), _topBar.width(), st::topBarHeight);
	QRect historyRect = QRect(history.x(), topBarRect.y() + topBarRect.height(), history.width(), history.y() + history.height() - topBarRect.y() - topBarRect.height());
	QPixmap animCache, animTopBarCache;
	if (!_a_show.animating() && (!cWideMode() || profile || overview || history.peer())) {
		animCache = grabInner();
		animTopBarCache = grabTopBar();
	}
	if (!back) {
		if (overview) {
			_stack.push_back(new StackItemOverview(overview->peer(), overview->type(), overview->lastWidth(), overview->lastScrollTop()));
		} else if (profile) {
			_stack.push_back(new StackItemProfile(profile->peer(), profile->lastScrollTop()));
		} else if (history.peer()) {
			dlgUpdated();
			_peerInStack = history.peer();
			_msgIdInStack = history.msgId();
			dlgUpdated();
			_stack.push_back(new StackItemHistory(_peerInStack, _msgIdInStack, history.replyReturns()));
		}
	}
	if (overview) {
		overview->hide();
		overview->clear();
		overview->deleteLater();
		overview->rpcInvalidate();
	}
	if (profile) {
		profile->hide();
		profile->clear();
		profile->deleteLater();
		profile->rpcInvalidate();
		profile = 0;
	}
	overview = new OverviewWidget(this, peer, type);
	_mediaTypeMask = 0;
	_topBar.show();
	resizeEvent(0);
	mediaOverviewUpdated(peer, type);
	if (!animCache.isNull()) {
		overview->animShow(animCache, animTopBarCache, back, lastScrollTop);
	} else {
		overview->fastShow();
	}
	history.animStop();
	if (back) clearBotStartToken(history.peer());
	history.showHistory(0, 0);
	history.hide();
	if (!cWideMode()) dialogs.hide();

	orderWidgets();

	App::wnd()->getTitle()->updateBackButton();
}

void MainWidget::showPeerProfile(PeerData *peer, bool back, int32 lastScrollTop) {
	if (peer->migrateTo()) {
		peer = peer->migrateTo();
	}

	App::wnd()->hideSettings();
	if (profile && profile->peer() == peer) return;

	QPixmap animCache = grabInner(), animTopBarCache = grabTopBar();
	if (!back) {
		if (overview) {
			_stack.push_back(new StackItemOverview(overview->peer(), overview->type(), overview->lastWidth(), overview->lastScrollTop()));
		} else if (profile) {
			_stack.push_back(new StackItemProfile(profile->peer(), profile->lastScrollTop()));
		} else if (history.peer()) {
			dlgUpdated();
			_peerInStack = history.peer();
			_msgIdInStack = history.msgId();
			dlgUpdated();
			_stack.push_back(new StackItemHistory(_peerInStack, _msgIdInStack, history.replyReturns()));
		}
	}
	if (overview) {
		overview->hide();
		overview->clear();
		overview->deleteLater();
		overview->rpcInvalidate();
		overview = 0;
	}
	if (profile) {
		profile->hide();
		profile->clear();
		profile->deleteLater();
		profile->rpcInvalidate();
	}
	profile = new ProfileWidget(this, peer);
	_topBar.show();
	resizeEvent(0);
	profile->animShow(animCache, animTopBarCache, back, lastScrollTop);
	history.animStop();
	if (back) clearBotStartToken(history.peer());
	history.showHistory(0, 0);
	history.hide();
	if (!cWideMode()) dialogs.hide();

	orderWidgets();

	App::wnd()->getTitle()->updateBackButton();
}

void MainWidget::showBackFromStack() {
	if (selectingPeer()) return;
	if (_stack.isEmpty()) {
		Ui::showChatsList();
		if (App::wnd()) QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));
		return;
	}
	StackItem *item = _stack.back();
	_stack.pop_back();
	if (item->type() == HistoryStackItem) {
		dlgUpdated();
		_peerInStack = 0;
		_msgIdInStack = 0;
		for (int32 i = _stack.size(); i > 0;) {
			if (_stack.at(--i)->type() == HistoryStackItem) {
				_peerInStack = static_cast<StackItemHistory*>(_stack.at(i))->peer;
				_msgIdInStack = static_cast<StackItemHistory*>(_stack.at(i))->msgId;
				dlgUpdated();
				break;
			}
		}
		StackItemHistory *histItem = static_cast<StackItemHistory*>(item);
		Ui::showPeerHistory(histItem->peer->id, App::main()->activeMsgId(), true);
		history.setReplyReturns(histItem->peer->id, histItem->replyReturns);
	} else if (item->type() == ProfileStackItem) {
		StackItemProfile *profItem = static_cast<StackItemProfile*>(item);
		showPeerProfile(profItem->peer, true, profItem->lastScrollTop);
	} else if (item->type() == OverviewStackItem) {
		StackItemOverview *overItem = static_cast<StackItemOverview*>(item);
		showMediaOverview(overItem->peer, overItem->mediaType, true, overItem->lastScrollTop);
	}
	delete item;
}

void MainWidget::orderWidgets() {
	_topBar.raise();
	_player.raise();
	dialogs.raise();
	_mediaType.raise();
	if (_hider) _hider->raise();
	if (_stickerPreview) _stickerPreview->raise();
}

QRect MainWidget::historyRect() const {
	QRect r(history.historyRect());
	r.moveLeft(r.left() + history.x());
	r.moveTop(r.top() + history.y());
	return r;
}

void MainWidget::dlgUpdated(DialogRow *row) {
	if (row) {
		dialogs.dlgUpdated(row);
	} else if (_peerInStack) {
		dialogs.dlgUpdated(App::history(_peerInStack->id), _msgIdInStack);
	}
}

void MainWidget::dlgUpdated(History *row, MsgId msgId) {
	if (!row) return;
	if (msgId < 0 && -msgId < ServerMaxMsgId && row->peer->migrateFrom()) {
		dialogs.dlgUpdated(App::history(row->peer->migrateFrom()->id), -msgId);
	} else {
		dialogs.dlgUpdated(row, msgId);
	}
}

void MainWidget::windowShown() {
	history.windowShown();
}

void MainWidget::sentUpdatesReceived(uint64 randomId, const MTPUpdates &result) {
	feedUpdates(result, randomId);
	App::emitPeerUpdated();
}

void MainWidget::inviteToChannelDone(ChannelData *channel, const MTPUpdates &updates) {
	sentUpdatesReceived(updates);
	QTimer::singleShot(ReloadChannelMembersTimeout, this, SLOT(onActiveChannelUpdateFull()));
}

void MainWidget::onActiveChannelUpdateFull() {
	if (activePeer() && activePeer()->isChannel()) {
		activePeer()->asChannel()->updateFull(true);
	}
}

void MainWidget::historyToDown(History *hist) {
	history.historyToDown(hist);
}

void MainWidget::dialogsToUp() {
	dialogs.dialogsToUp();
}

void MainWidget::newUnreadMsg(History *hist, HistoryItem *item) {
	history.newUnreadMsg(hist, item);
}

void MainWidget::historyWasRead() {
	history.historyWasRead(false);
}

void MainWidget::historyCleared(History *hist) {
	history.historyCleared(hist);
}

void MainWidget::animShow(const QPixmap &bgAnimCache, bool back) {
	if (App::app()) App::app()->mtpPause();

	(back ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.stop();

	showAll();
	(back ? _cacheUnder : _cacheOver) = myGrab(this);
	hideAll();

	a_coordUnder = back ? anim::ivalue(-qFloor(st::slideShift * width()), 0) : anim::ivalue(0, -qFloor(st::slideShift * width()));
	a_coordOver = back ? anim::ivalue(0, width()) : anim::ivalue(width(), 0);
	a_shadow = back ? anim::fvalue(1, 0) : anim::fvalue(0, 1);
	_a_show.start();

	show();
}

void MainWidget::step_show(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		_a_show.stop();

		a_coordUnder.finish();
		a_coordOver.finish();
		a_shadow.finish();

		_cacheUnder = _cacheOver = QPixmap();

		showAll();
		activate();

		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordUnder.update(dt, st::slideFunction);
		a_coordOver.update(dt, st::slideFunction);
		a_shadow.update(dt, st::slideFunction);
	}
	if (timer) update();
}

void MainWidget::animStop_show() {
	_a_show.stop();
}

void MainWidget::paintEvent(QPaintEvent *e) {
	if (_background) checkChatBackground();

	Painter p(this);
	if (_a_show.animating()) {
		if (a_coordOver.current() > 0) {
			p.drawPixmap(QRect(0, 0, a_coordOver.current(), height()), _cacheUnder, QRect(-a_coordUnder.current() * cRetinaFactor(), 0, a_coordOver.current() * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(a_shadow.current() * st::slideFadeOut);
			p.fillRect(0, 0, a_coordOver.current(), height(), st::black->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow);
	}
}

void MainWidget::hideAll() {
	dialogs.hide();
	history.hide();
	if (profile) {
		profile->hide();
	}
	if (overview) {
		overview->hide();
	}
	_topBar.hide();
	_mediaType.hide();
	_player.hide();
	_playerHeight = 0;
}

void MainWidget::showAll() {
	if (cPasswordRecovered()) {
		cSetPasswordRecovered(false);
		Ui::showLayer(new InformBox(lang(lng_signin_password_removed)));
	}
	if (cWideMode()) {
		if (_hider) {
			_hider->show();
			if (_forwardConfirm) {
				Ui::hideLayer(true);
				_forwardConfirm = 0;
			}
		}
		dialogs.show();
		if (overview) {
			overview->show();
		} else if (profile) {
			profile->show();
		} else {
			history.show();
			history.resizeEvent(0);
		}
		if (profile || overview || history.peer()) {
			_topBar.show();
		}
	} else {
		if (_hider) {
			_hider->hide();
			if (!_forwardConfirm && _hider->wasOffered()) {
				_forwardConfirm = new ConfirmBox(_hider->offeredText(), lang(lng_forward_send));
				connect(_forwardConfirm, SIGNAL(confirmed()), _hider, SLOT(forward()));
				connect(_forwardConfirm, SIGNAL(cancelled()), this, SLOT(onForwardCancel()));
				Ui::showLayer(_forwardConfirm, ForceFastShowLayer);
			}
		}
		if (selectingPeer()) {
			dialogs.show();
			history.hide();
			if (overview) overview->hide();
			if (profile) profile->hide();
			_topBar.hide();
		} else if (overview) {
			overview->show();
		} else if (profile) {
			profile->show();
		} else if (history.peer()) {
			history.show();
			history.resizeEvent(0);
		} else {
			dialogs.show();
			history.hide();
		}
		if (!selectingPeer() && (profile || overview || history.peer())) {
			_topBar.show();
			dialogs.hide();
		}
	}
	if (audioPlayer()) {
		SongMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		int64 playingPosition = 0, playingDuration = 0;
		int32 playingFrequency = 0;
		audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
		if (playing) {
			_player.updateState(playing, playingState, playingPosition, playingDuration, playingFrequency);
			if (!(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				_player.clearSelection();
				_player.show();
				_playerHeight = _player.height();
			}
		}
	}
	resizeEvent(0);

	App::wnd()->checkHistoryActivation();
}

void MainWidget::resizeEvent(QResizeEvent *e) {
	int32 tbh = _topBar.isHidden() ? 0 : st::topBarHeight;
	if (cWideMode()) {
		_dialogsWidth = snap<int>((width() * 5) / 14, st::dlgMinWidth, st::dlgMaxWidth);
		dialogs.resize(_dialogsWidth, height());
		dialogs.moveToLeft(0, 0);
		_player.resize(width() - _dialogsWidth, _player.height());
		_player.moveToLeft(_dialogsWidth, 0);
		_topBar.resize(width() - _dialogsWidth, st::topBarHeight);
		_topBar.moveToLeft(_dialogsWidth, _playerHeight);
		history.resize(width() - _dialogsWidth, height() - _playerHeight - tbh);
		history.moveToLeft(_dialogsWidth, _playerHeight + tbh);
		if (_hider) {
			_hider->resize(width() - _dialogsWidth, height());
			_hider->moveToLeft(_dialogsWidth, 0);
		}
	} else {
		_dialogsWidth = width();
		_player.setGeometry(0, 0, _dialogsWidth, _player.height());
		dialogs.setGeometry(0, _playerHeight, _dialogsWidth, height() - _playerHeight);
		_topBar.setGeometry(0, _playerHeight, _dialogsWidth, st::topBarHeight);
		history.setGeometry(0, _playerHeight + tbh, _dialogsWidth, height() - _playerHeight - tbh);
		if (_hider) _hider->setGeometry(0, 0, _dialogsWidth, height());
	}
	_mediaType.moveToLeft(width() - _mediaType.width(), _playerHeight + st::topBarHeight);
	if (profile) profile->setGeometry(history.geometry());
	if (overview) overview->setGeometry(history.geometry());
	if (_stickerPreview) _stickerPreview->setGeometry(rect());
	_contentScrollAddToY = 0;
}

int32 MainWidget::contentScrollAddToY() const {
	return _contentScrollAddToY;
}

void MainWidget::keyPressEvent(QKeyEvent *e) {
}

void MainWidget::updateWideMode() {
	showAll();
	_topBar.updateWideMode();
	history.updateWideMode();
	if (overview) overview->updateWideMode();
	if (profile) profile->updateWideMode();
	_player.updateWideMode();
}

bool MainWidget::needBackButton() {
	return overview || profile || (history.peer() && history.peer()->id);
}

void MainWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (profile) {
		profile->paintTopBar(p, over, decreaseWidth);
	} else if (overview) {
		overview->paintTopBar(p, over, decreaseWidth);
	} else {
		history.paintTopBar(p, over, decreaseWidth);
	}
}

void MainWidget::onPhotosSelect() {
	if (overview) overview->switchType(OverviewPhotos);
	_mediaType.hideStart();
}

void MainWidget::onVideosSelect() {
	if (overview) overview->switchType(OverviewVideos);
	_mediaType.hideStart();
}

void MainWidget::onSongsSelect() {
	if (overview) overview->switchType(OverviewAudioDocuments);
	_mediaType.hideStart();
}

void MainWidget::onDocumentsSelect() {
	if (overview) overview->switchType(OverviewDocuments);
	_mediaType.hideStart();
}

void MainWidget::onAudiosSelect() {
	if (overview) overview->switchType(OverviewAudios);
	_mediaType.hideStart();
}

void MainWidget::onLinksSelect() {
	if (overview) overview->switchType(OverviewLinks);
	_mediaType.hideStart();
}

TopBarWidget *MainWidget::topBar() {
	return &_topBar;
}

PlayerWidget *MainWidget::player() {
	return &_player;
}

void MainWidget::onTopBarClick() {
	if (profile) {
		profile->topBarClick();
	} else if (overview) {
		overview->topBarClick();
	} else {
		history.topBarClick();
	}
}

void MainWidget::onHistoryShown(History *history, MsgId atMsgId) {
	if ((cWideMode() || !selectingPeer()) && (profile || overview || history)) {
		_topBar.show();
	} else {
		_topBar.hide();
	}
	resizeEvent(0);
	if (_a_show.animating()) {
		_topBar.hide();
	}

	dlgUpdated(history, atMsgId);
}

void MainWidget::searchInPeer(PeerData *peer) {
	dialogs.searchInPeer(peer);
	if (cWideMode()) {
		dialogs.activate();
	} else {
		dialogsToUp();
		Ui::showChatsList();
	}
}

void MainWidget::onUpdateNotifySettings() {
	if (this != App::main()) return;
	while (!updateNotifySettingPeers.isEmpty()) {
		PeerData *peer = *updateNotifySettingPeers.begin();
		updateNotifySettingPeers.erase(updateNotifySettingPeers.begin());

		if (peer->notify == UnknownNotifySettings || peer->notify == EmptyNotifySettings) {
			peer->notify = new NotifySettings();
		}
		MTP::send(MTPaccount_UpdateNotifySettings(MTP_inputNotifyPeer(peer->input), MTP_inputPeerNotifySettings(MTP_int(peer->notify->mute), MTP_string(peer->notify->sound), MTP_bool(peer->notify->previews), MTP_int(peer->notify->events))), RPCResponseHandler(), 0, updateNotifySettingPeers.isEmpty() ? 0 : 10);
	}
}

void MainWidget::feedUpdateVector(const MTPVector<MTPUpdate> &updates, bool skipMessageIds) {
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
	if (pts) _ptsWaiter.init(pts);
	if (updDate < date) updDate = date;
	if (qts && updQts < qts) {
		updQts = qts;
	}
	if (seq && seq != updSeq) {
		updSeq = seq;
		if (_bySeqTimer.isActive()) _bySeqTimer.stop();
		for (QMap<int32, MTPUpdates>::iterator i = _bySeqUpdates.begin(); i != _bySeqUpdates.end();) {
			int32 s = i.key();
			if (s <= seq + 1) {
				MTPUpdates v = i.value();
				i = _bySeqUpdates.erase(i);
				if (s == seq + 1) {
					return feedUpdates(v);
				}
			} else {
				if (!_bySeqTimer.isActive()) _bySeqTimer.start(WaitForSkippedTimeout);
				break;
			}
		}
	}
}

void MainWidget::gotChannelDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff) {
	_channelFailDifferenceTimeout.remove(channel);

	int32 timeout = 0;
	bool isFinal = true;
	switch (diff.type()) {
	case mtpc_updates_channelDifferenceEmpty: {
		const MTPDupdates_channelDifferenceEmpty &d(diff.c_updates_channelDifferenceEmpty());
		if (d.has_timeout()) timeout = d.vtimeout.v;
		isFinal = d.is_final();
		channel->ptsInit(d.vpts.v);
	} break;

	case mtpc_updates_channelDifferenceTooLong: {
		const MTPDupdates_channelDifferenceTooLong &d(diff.c_updates_channelDifferenceTooLong());

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		History *h = App::historyLoaded(channel->id);
		if (h) {
			h->setNotLoadedAtBottom();
			h->asChannelHistory()->clearOther();
		}
		App::feedMsgs(d.vmessages, NewMessageLast);
		if (h) {
			MsgId topMsg = h->isMegagroup() ? d.vtop_message.v : d.vtop_important_message.v;
			if (HistoryItem *item = App::histItemById(peerToChannel(channel->id), topMsg)) {
				h->setLastMessage(item);
			}
			int32 unreadCount = h->isMegagroup() ? d.vunread_count.v : d.vunread_important_count.v;
			if (unreadCount >= h->unreadCount) {
				h->setUnreadCount(unreadCount, false);
				h->inboxReadBefore = d.vread_inbox_max_id.v + 1;
			}
			if (d.vunread_count.v >= h->asChannelHistory()->unreadCountAll) {
				h->asChannelHistory()->unreadCountAll = d.vunread_count.v;
				h->inboxReadBefore = d.vread_inbox_max_id.v + 1;
			}
			if (history.peer() == channel) {
				history.updateToEndVisibility();
				history.onListScroll();
			}
			h->asChannelHistory()->getRangeDifference();
		}

		if (d.has_timeout()) timeout = d.vtimeout.v;
		isFinal = d.is_final();
		channel->ptsInit(d.vpts.v);
	} break;

	case mtpc_updates_channelDifference: {
		const MTPDupdates_channelDifference &d(diff.c_updates_channelDifference());

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats, false);

		_handlingChannelDifference = true;
		feedMessageIds(d.vother_updates);

		// feed messages and groups, copy from App::feedMsgs
		History *h = App::history(channel->id);
		const QVector<MTPMessage> &vmsgs(d.vnew_messages.c_vector().v);
		QMap<uint64, int32> msgsIds;
		for (int32 i = 0, l = vmsgs.size(); i < l; ++i) {
			const MTPMessage &msg(vmsgs.at(i));
			switch (msg.type()) {
			case mtpc_message: {
				const MTPDmessage &d(msg.c_message());
				if (App::checkEntitiesAndViewsUpdate(d)) { // new message, index my forwarded messages to links overview, already in blocks
					LOG(("Skipping message, because it is already in blocks!"));
				} else {
					msgsIds.insert((uint64(uint32(d.vid.v)) << 32) | uint64(i), i + 1);
				}
			} break;
			case mtpc_messageEmpty: msgsIds.insert((uint64(uint32(msg.c_messageEmpty().vid.v)) << 32) | uint64(i), i + 1); break;
			case mtpc_messageService: msgsIds.insert((uint64(uint32(msg.c_messageService().vid.v)) << 32) | uint64(i), i + 1); break;
			}
		}
		const QVector<MTPUpdate> &vother(d.vother_updates.c_vector().v);
		for (int32 i = 0, l = vother.size(); i < l; ++i) {
			if (vother.at(i).type() == mtpc_updateChannelGroup) {
				const MTPDupdateChannelGroup &updateGroup(vother.at(i).c_updateChannelGroup());
				if (updateGroup.vgroup.type() == mtpc_messageGroup) {
					const MTPDmessageGroup &group(updateGroup.vgroup.c_messageGroup());
					if (updateGroup.vchannel_id.v != peerToChannel(channel->id)) {
						LOG(("API Error: updateChannelGroup with invalid channel_id returned in channelDifference, channelId: %1, channel_id: %2").arg(peerToChannel(channel->id)).arg(updateGroup.vchannel_id.v));
						continue;
					}
					msgsIds.insert((uint64((uint32(group.vmin_id.v) + uint32(group.vmax_id.v)) / 2) << 32), -i - 1);
				}
			}
		}
		for (QMap<uint64, int32>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			if (i.value() > 0) { // add message
				const MTPMessage &msg(vmsgs.at(i.value() - 1));
				if (channel->id != peerFromMessage(msg)) {
					LOG(("API Error: message with invalid peer returned in channelDifference, channelId: %1, peer: %2").arg(peerToChannel(channel->id)).arg(peerFromMessage(msg)));
					continue; // wtf
				}
				h->addNewMessage(msg, NewMessageUnread);
			} else { // add group
				const MTPDupdateChannelGroup &updateGroup(vother.at(-i.value() - 1).c_updateChannelGroup());
				h->asChannelHistory()->addNewGroup(updateGroup.vgroup);
			}
		}

		feedUpdateVector(d.vother_updates, true);
		_handlingChannelDifference = false;

		if (d.has_timeout()) timeout = d.vtimeout.v;
		isFinal = d.is_final();
		channel->ptsInit(d.vpts.v);
	} break;
	}

	channel->ptsSetRequesting(false);

	if (!isFinal) {
		MTP_LOG(0, ("getChannelDifference { good - after not final channelDifference was received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getChannelDifference(channel);
	} else if (activePeer() == channel) {
		channel->ptsWaitingForShortPoll(timeout ? (timeout * 1000) : WaitForChannelGetDifference);
	}

	App::emitPeerUpdated();
}

void MainWidget::gotRangeDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff) {
	int32 nextRequestPts = 0;
	bool isFinal = true;
	switch (diff.type()) {
	case mtpc_updates_channelDifferenceEmpty: {
		const MTPDupdates_channelDifferenceEmpty &d(diff.c_updates_channelDifferenceEmpty());
		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifferenceTooLong: {
		const MTPDupdates_channelDifferenceTooLong &d(diff.c_updates_channelDifferenceTooLong());

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);

		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifference: {
		const MTPDupdates_channelDifference &d(diff.c_updates_channelDifference());

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats, false);

		_handlingChannelDifference = true;
		feedMessageIds(d.vother_updates);
		App::feedMsgs(d.vnew_messages, NewMessageUnread);
		feedUpdateVector(d.vother_updates, true);
		_handlingChannelDifference = false;

		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;
	}

	if (!isFinal) {
		if (History *h = App::historyLoaded(channel->id)) {
			MTP_LOG(0, ("getChannelDifference { good - after not final channelDifference was received, validating history part }%1").arg(cTestMode() ? " TESTMODE" : ""));
			h->asChannelHistory()->getRangeDifferenceNext(nextRequestPts);
		}
	}

	App::emitPeerUpdated();
}

bool MainWidget::failChannelDifference(ChannelData *channel, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	LOG(("RPC Error in getChannelDifference: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	failDifferenceStartTimerFor(channel);
	return true;
}

void MainWidget::gotState(const MTPupdates_State &state) {
	const MTPDupdates_state &d(state.c_updates_state());
	updSetState(d.vpts.v, d.vdate.v, d.vqts.v, d.vseq.v);

	_lastUpdateTime = getms(true);
	noUpdatesTimer.start(NoUpdatesTimeout);
	_ptsWaiter.setRequesting(false);

	dialogs.loadDialogs();
	updateOnline();

	App::emitPeerUpdated();
}

void MainWidget::gotDifference(const MTPupdates_Difference &diff) {
	_failDifferenceTimeout = 1;

	switch (diff.type()) {
	case mtpc_updates_differenceEmpty: {
		const MTPDupdates_differenceEmpty &d(diff.c_updates_differenceEmpty());
		updSetState(_ptsWaiter.current(), d.vdate.v, updQts, d.vseq.v);

		_lastUpdateTime = getms(true);
		noUpdatesTimer.start(NoUpdatesTimeout);

		_ptsWaiter.setRequesting(false);

		App::emitPeerUpdated();
	} break;
	case mtpc_updates_differenceSlice: {
		const MTPDupdates_differenceSlice &d(diff.c_updates_differenceSlice());
		feedDifference(d.vusers, d.vchats, d.vnew_messages, d.vother_updates);

		const MTPDupdates_state &s(d.vintermediate_state.c_updates_state());
		updSetState(s.vpts.v, s.vdate.v, s.vqts.v, s.vseq.v);

		_ptsWaiter.setRequesting(false);

		MTP_LOG(0, ("getDifference { good - after a slice of difference was received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getDifference();

		App::emitPeerUpdated();
	} break;
	case mtpc_updates_difference: {
		const MTPDupdates_difference &d(diff.c_updates_difference());
		feedDifference(d.vusers, d.vchats, d.vnew_messages, d.vother_updates);

		gotState(d.vstate);
	} break;
	};
}

bool MainWidget::getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, uint64 &curTime) {
	if (channel) {
		if (ms <= 0) {
			ChannelGetDifferenceTime::iterator i = channelCurTime.find(channel);
			if (i != channelCurTime.cend()) {
				channelCurTime.erase(i);
			} else {
				return false;
			}
		} else {
			uint64 when = getms(true) + ms;
			ChannelGetDifferenceTime::iterator i = channelCurTime.find(channel);
			if (i != channelCurTime.cend()) {
				if (i.value() > when) {
					i.value() = when;
				} else {
					return false;
				}
			} else {
				channelCurTime.insert(channel, when);
			}
		}
	} else {
		if (ms <= 0) {
			if (curTime) {
				curTime = 0;
			} else {
				return false;
			}
		} else {
			uint64 when = getms(true) + ms;
			if (!curTime || curTime > when) {
				curTime = when;
			} else {
				return false;
			}
		}
	}
	return true;
}

void MainWidget::ptsWaiterStartTimerFor(ChannelData *channel, int32 ms) {
	if (getDifferenceTimeChanged(channel, ms, _channelGetDifferenceTimeByPts, _getDifferenceTimeByPts)) {
		onGetDifferenceTimeByPts();
	}
}

void MainWidget::failDifferenceStartTimerFor(ChannelData *channel) {
	int32 ms = 0;
	ChannelFailDifferenceTimeout::iterator i;
	if (channel) {
		i = _channelFailDifferenceTimeout.find(channel);
		if (i == _channelFailDifferenceTimeout.cend()) {
			i = _channelFailDifferenceTimeout.insert(channel, 1);
		}
		ms = i.value() * 1000;
	} else {
		ms = _failDifferenceTimeout * 1000;
	}
	if (getDifferenceTimeChanged(channel, ms, _channelGetDifferenceTimeAfterFail, _getDifferenceTimeAfterFail)) {
		onGetDifferenceTimeAfterFail();
	}
	if (channel) {
		if (i.value() < 64) i.value() *= 2;
	} else {
		if (_failDifferenceTimeout < 64) _failDifferenceTimeout *= 2;
	}
}

bool MainWidget::ptsUpdated(int32 pts, int32 ptsCount) { // return false if need to save that update and apply later
	return _ptsWaiter.updated(0, pts, ptsCount);
}

bool MainWidget::ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdates &updates) {
	return _ptsWaiter.updated(0, pts, ptsCount, updates);
}

bool MainWidget::ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdate &update) {
	return _ptsWaiter.updated(0, pts, ptsCount, update);
}

void MainWidget::ptsApplySkippedUpdates() {
	return _ptsWaiter.applySkippedUpdates(0);
}

void MainWidget::feedDifference(const MTPVector<MTPUser> &users, const MTPVector<MTPChat> &chats, const MTPVector<MTPMessage> &msgs, const MTPVector<MTPUpdate> &other) {
	App::wnd()->checkAutoLock();
	App::feedUsers(users, false);
	App::feedChats(chats, false);
	feedMessageIds(other);
	App::feedMsgs(msgs, NewMessageUnread);
	feedUpdateVector(other, true);
	history.peerMessagesUpdated();
}

bool MainWidget::failDifference(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	LOG(("RPC Error in getDifference: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	failDifferenceStartTimerFor(0);
	return true;
}

void MainWidget::onGetDifferenceTimeByPts() {
	if (!MTP::authedId()) return;

	uint64 now = getms(true), wait = 0;
	if (_getDifferenceTimeByPts) {
		if (_getDifferenceTimeByPts > now) {
			wait = _getDifferenceTimeByPts - now;
		} else {
			getDifference();
		}
	}
	for (ChannelGetDifferenceTime::iterator i = _channelGetDifferenceTimeByPts.begin(); i != _channelGetDifferenceTimeByPts.cend();) {
		if (i.value() > now) {
			wait = wait ? qMin(wait, i.value() - now) : (i.value() - now);
			++i;
		} else {
			getChannelDifference(i.key(), GetChannelDifferenceFromPtsGap);
			i = _channelGetDifferenceTimeByPts.erase(i);
		}
	}
	if (wait) {
		_byPtsTimer.start(wait);
	} else {
		_byPtsTimer.stop();
	}
}

void MainWidget::onGetDifferenceTimeAfterFail() {
	if (!MTP::authedId()) return;

	uint64 now = getms(true), wait = 0;
	if (_getDifferenceTimeAfterFail) {
		if (_getDifferenceTimeAfterFail > now) {
			wait = _getDifferenceTimeAfterFail - now;
		} else {
			_ptsWaiter.setRequesting(false);
			MTP_LOG(0, ("getDifference { force - after get difference failed }%1").arg(cTestMode() ? " TESTMODE" : ""));
			getDifference();
		}
	}
	for (ChannelGetDifferenceTime::iterator i = _channelGetDifferenceTimeAfterFail.begin(); i != _channelGetDifferenceTimeAfterFail.cend();) {
		if (i.value() > now) {
			wait = wait ? qMin(wait, i.value() - now) : (i.value() - now);
			++i;
		} else {
			getChannelDifference(i.key(), GetChannelDifferenceFromFail);
			i = _channelGetDifferenceTimeAfterFail.erase(i);
		}
	}
	if (wait) {
		_failDifferenceTimer.start(wait);
	} else {
		_failDifferenceTimer.stop();
	}
}

void MainWidget::getDifference() {
	if (this != App::main()) return;

	_getDifferenceTimeByPts = 0;

	LOG(("Getting difference! no updates timer: %1, remains: %2").arg(noUpdatesTimer.isActive() ? 1 : 0).arg(noUpdatesTimer.remainingTime()));
	if (_ptsWaiter.requesting()) return;

	_bySeqUpdates.clear();
	_bySeqTimer.stop();

	noUpdatesTimer.stop();
	_getDifferenceTimeAfterFail = 0;

	LOG(("Getting difference for %1, %2").arg(_ptsWaiter.current()).arg(updDate));
	_ptsWaiter.setRequesting(true);
	MTP::send(MTPupdates_GetDifference(MTP_int(_ptsWaiter.current()), MTP_int(updDate), MTP_int(updQts)), rpcDone(&MainWidget::gotDifference), rpcFail(&MainWidget::failDifference));
}

void MainWidget::getChannelDifference(ChannelData *channel, GetChannelDifferenceFrom from) {
	if (this != App::main() || !channel) return;

	if (from != GetChannelDifferenceFromPtsGap) {
		_channelGetDifferenceTimeByPts.remove(channel);
	}

	LOG(("Getting channel difference!"));
	if (!channel->ptsInited() || channel->ptsRequesting()) return;

	if (from != GetChannelDifferenceFromFail) {
		_channelGetDifferenceTimeAfterFail.remove(channel);
	}

	LOG(("Getting channel difference for %1").arg(channel->pts()));
	channel->ptsSetRequesting(true);

	MTPChannelMessagesFilter filter;
	if (activePeer() == channel) {
		filter = MTP_channelMessagesFilterEmpty();
	} else {
		filter = MTP_channelMessagesFilterEmpty(); //MTP_channelMessagesFilterCollapsed(); - not supported
		if (History *history = App::historyLoaded(channel->id)) {
			if (!history->isMegagroup() && !history->asChannelHistory()->onlyImportant()) {
				MsgId fixInScrollMsgId = 0;
				int32 fixInScrollMsgTop = 0;
				history->asChannelHistory()->getSwitchReadyFor(SwitchAtTopMsgId, fixInScrollMsgId, fixInScrollMsgTop);
				history->getReadyFor(ShowAtTheEndMsgId, fixInScrollMsgId, fixInScrollMsgTop);
				history->lastWidth = 0;
				history->lastScrollTop = INT_MAX;
			}
		}
	}
	MTP::send(MTPupdates_GetChannelDifference(channel->inputChannel, filter, MTP_int(channel->pts()), MTP_int(MTPChannelGetDifferenceLimit)), rpcDone(&MainWidget::gotChannelDifference, channel), rpcFail(&MainWidget::failChannelDifference, channel));
}

void MainWidget::mtpPing() {
	MTP::ping();
}

void MainWidget::start(const MTPUser &user) {
	int32 uid = user.c_user().vid.v;
	if (MTP::authedId() != uid) {
		MTP::authed(uid);
		Local::writeMtpData();
	}

	Local::readSavedPeers();

	cSetOtherOnline(0);
	App::feedUsers(MTP_vector<MTPUser>(1, user));
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	App::app()->startUpdateCheck();
	#endif
	MTP::send(MTPupdates_GetState(), rpcDone(&MainWidget::gotState));
	update();
	if (!cStartUrl().isEmpty()) {
		openLocalUrl(cStartUrl());
		cSetStartUrl(QString());
	}
	_started = true;
	App::wnd()->sendServiceHistoryRequest();
	Local::readStickers();
	Local::readSavedGifs();
	history.start();
}

bool MainWidget::started() {
	return _started;
}

void MainWidget::openLocalUrl(const QString &url) {
	QString u(url.trimmed());
	if (u.startsWith(qstr("tg://resolve"), Qt::CaseInsensitive)) {
		QRegularExpressionMatch m = QRegularExpression(qsl("^tg://resolve/?\\?domain=([a-zA-Z0-9\\.\\_]+)(&(start|startgroup)=([a-zA-Z0-9\\.\\_\\-]+))?(&|$)"), QRegularExpression::CaseInsensitiveOption).match(u);
		if (m.hasMatch()) {
			QString start = m.captured(3), startToken = m.captured(4);
			openPeerByName(m.captured(1), (start == qsl("startgroup")), startToken);
		}
	} else if (u.startsWith(qstr("tg://join"), Qt::CaseInsensitive)) {
		QRegularExpressionMatch m = QRegularExpression(qsl("^tg://join/?\\?invite=([a-zA-Z0-9\\.\\_\\-]+)(&|$)"), QRegularExpression::CaseInsensitiveOption).match(u);
		if (m.hasMatch()) {
			joinGroupByHash(m.captured(1));
		}
	} else if (u.startsWith(qstr("tg://addstickers"), Qt::CaseInsensitive)) {
		QRegularExpressionMatch m = QRegularExpression(qsl("^tg://addstickers/?\\?set=([a-zA-Z0-9\\.\\_]+)(&|$)"), QRegularExpression::CaseInsensitiveOption).match(u);
		if (m.hasMatch()) {
			stickersBox(MTP_inputStickerSetShortName(MTP_string(m.captured(1))));
		}
	} else if (u.startsWith(qstr("tg://msg_url"), Qt::CaseInsensitive)) {
		QRegularExpressionMatch m = QRegularExpression(qsl("^tg://msg_url/?\\?(.+)(#|$)"), QRegularExpression::CaseInsensitiveOption).match(u);
		if (m.hasMatch()) {
			QStringList params = m.captured(1).split('&');
			QString url, text;
			for (int32 i = 0, l = params.size(); i < l; ++i) {
				if (params.at(i).startsWith(qstr("url="), Qt::CaseInsensitive)) {
					url = myUrlDecode(params.at(i).mid(4));
				} else if (params.at(i).startsWith(qstr("text="), Qt::CaseInsensitive)) {
					text = myUrlDecode(params.at(i).mid(5));
				}
			}
			if (!url.isEmpty()) {
				shareUrlLayer(url, text);
			}
		}
	}
}

void MainWidget::openPeerByName(const QString &username, bool toProfile, const QString &startToken) {
	App::wnd()->hideMediaview();

	PeerData *peer = App::peerByName(username);
	if (peer) {
		if (toProfile && !peer->isChannel()) {
			if (peer->isUser() && peer->asUser()->botInfo && !peer->asUser()->botInfo->cantJoinGroups && !startToken.isEmpty()) {
				peer->asUser()->botInfo->startGroupToken = startToken;
				Ui::showLayer(new ContactsBox(peer->asUser()));
			} else {
				showPeerProfile(peer);
			}
		} else {
			if (peer->isUser() && peer->asUser()->botInfo) {
				peer->asUser()->botInfo->startToken = startToken;
				if (peer == history.peer()) {
					history.updateControlsVisibility();
					history.resizeEvent(0);
				}
			}
			Ui::showPeerHistoryAsync(peer->id, ShowAtUnreadMsgId);
		}
	} else {
		MTP::send(MTPcontacts_ResolveUsername(MTP_string(username)), rpcDone(&MainWidget::usernameResolveDone, qMakePair(toProfile, startToken)), rpcFail(&MainWidget::usernameResolveFail, username));
	}
}

void MainWidget::joinGroupByHash(const QString &hash) {
	App::wnd()->hideMediaview();
	MTP::send(MTPmessages_CheckChatInvite(MTP_string(hash)), rpcDone(&MainWidget::inviteCheckDone, hash), rpcFail(&MainWidget::inviteCheckFail));
}

void MainWidget::stickersBox(const MTPInputStickerSet &set) {
	App::wnd()->hideMediaview();
	StickerSetBox *box = new StickerSetBox(set);
	connect(box, SIGNAL(installed(uint64)), this, SLOT(onStickersInstalled(uint64)));
	Ui::showLayer(box);
}

void MainWidget::onStickersInstalled(uint64 setId) {
	emit stickersUpdated();
	history.stickersInstalled(setId);
}

void MainWidget::onFullPeerUpdated(PeerData *peer) {
	emit peerUpdated(peer);
}

void MainWidget::onSelfParticipantUpdated(ChannelData *channel) {
	History *h = App::historyLoaded(channel->id);
	if (_updatedChannels.contains(channel)) {
		_updatedChannels.remove(channel);
		if ((h ? h : App::history(channel->id))->isEmpty()) {
			checkPeerHistory(channel);
		} else {
			h->asChannelHistory()->checkJoinedMessage(true);
			history.peerMessagesUpdated(channel->id);
		}
	} else if (h) {
		h->asChannelHistory()->checkJoinedMessage();
		history.peerMessagesUpdated(channel->id);
	}
}

bool MainWidget::contentOverlapped(const QRect &globalRect) {
	return (history.contentOverlapped(globalRect) ||
			_mediaType.overlaps(globalRect));
}

void MainWidget::usernameResolveDone(QPair<bool, QString> toProfileStartToken, const MTPcontacts_ResolvedPeer &result) {
	Ui::hideLayer();
	if (result.type() != mtpc_contacts_resolvedPeer) return;

	const MTPDcontacts_resolvedPeer &d(result.c_contacts_resolvedPeer());
	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);
	PeerId peerId = peerFromMTP(d.vpeer);
	if (!peerId) return;

	PeerData *peer = App::peer(peerId);
	if (toProfileStartToken.first) {
		if (peer->isUser() && peer->asUser()->botInfo && !peer->asUser()->botInfo->cantJoinGroups && !toProfileStartToken.second.isEmpty()) {
			peer->asUser()->botInfo->startGroupToken = toProfileStartToken.second;
			Ui::showLayer(new ContactsBox(peer->asUser()));
		} else if (peer->isChannel()) {
			Ui::showPeerHistory(peer->id, ShowAtUnreadMsgId);
		} else {
			showPeerProfile(peer);
		}
	} else {
		if (peer->isUser() && peer->asUser()->botInfo) {
			peer->asUser()->botInfo->startToken = toProfileStartToken.second;
			if (peer == history.peer()) {
				history.updateControlsVisibility();
				history.resizeEvent(0);
			}
		}
		Ui::showPeerHistory(peer->id, ShowAtUnreadMsgId);
	}
}

bool MainWidget::usernameResolveFail(QString name, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	if (error.code() == 400) {
		Ui::showLayer(new InformBox(lng_username_not_found(lt_user, name)));
	}
	return true;
}

void MainWidget::inviteCheckDone(QString hash, const MTPChatInvite &invite) {
	switch (invite.type()) {
	case mtpc_chatInvite: {
		const MTPDchatInvite &d(invite.c_chatInvite());
		ConfirmBox *box = new ConfirmBox(((d.is_channel() && !d.is_megagroup()) ? lng_group_invite_want_join_channel : lng_group_invite_want_join)(lt_title, qs(d.vtitle)), lang(lng_group_invite_join));
		_inviteHash = hash;
		connect(box, SIGNAL(confirmed()), this, SLOT(onInviteImport()));
		Ui::showLayer(box);
	} break;

	case mtpc_chatInviteAlready: {
		const MTPDchatInviteAlready &d(invite.c_chatInviteAlready());
		PeerData *chat = App::feedChats(MTP_vector<MTPChat>(1, d.vchat));
		if (chat) {
			Ui::showPeerHistory(chat->id, ShowAtUnreadMsgId);
		}
	} break;
	}
}

bool MainWidget::inviteCheckFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	if (error.code() == 400) {
		Ui::showLayer(new InformBox(lang(lng_group_invite_bad_link)));
	}
	return true;
}

void MainWidget::onInviteImport() {
	if (_inviteHash.isEmpty()) return;
	MTP::send(MTPmessages_ImportChatInvite(MTP_string(_inviteHash)), rpcDone(&MainWidget::inviteImportDone), rpcFail(&MainWidget::inviteImportFail));
}

void MainWidget::inviteImportDone(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);

	Ui::hideLayer();
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	default: LOG(("API Error: unexpected update cons %1 (MainWidget::inviteImportDone)").arg(updates.type())); break;
	}
	if (v && !v->isEmpty()) {
		if (v->front().type() == mtpc_chat) {
			Ui::showPeerHistory(peerFromChat(v->front().c_chat().vid.v), ShowAtTheEndMsgId);
		} else if (v->front().type() == mtpc_channel) {
			Ui::showPeerHistory(peerFromChannel(v->front().c_channel().vid.v), ShowAtTheEndMsgId);
		}
	}
}

bool MainWidget::inviteImportFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	if (error.code() == 400) {
		Ui::showLayer(new InformBox(lang(error.type() == qsl("USERS_TOO_MUCH") ? lng_group_invite_no_room : lng_group_invite_bad_link)));
	}
	return true;
}

void MainWidget::startFull(const MTPVector<MTPUser> &users) {
	const QVector<MTPUser> &v(users.c_vector().v);
	if (v.isEmpty() || v[0].type() != mtpc_user || !v[0].c_user().is_self()) { // wtf?..
		return App::logOut();
	}
	start(v[0]);
}

void MainWidget::applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *h) {
	PeerData *updatePeer = 0;
	switch (settings.type()) {
	case mtpc_peerNotifySettingsEmpty:
		switch (peer.type()) {
		case mtpc_notifyAll: globalNotifyAllPtr = EmptyNotifySettings; break;
		case mtpc_notifyUsers: globalNotifyUsersPtr = EmptyNotifySettings; break;
		case mtpc_notifyChats: globalNotifyChatsPtr = EmptyNotifySettings; break;
		case mtpc_notifyPeer: {
			updatePeer = App::peerLoaded(peerFromMTP(peer.c_notifyPeer().vpeer));
			if (updatePeer && updatePeer->notify != EmptyNotifySettings) {
				if (updatePeer->notify != UnknownNotifySettings) {
					delete updatePeer->notify;
				}
				updatePeer->notify = EmptyNotifySettings;
				App::unregMuted(updatePeer);
				if (!h) h = App::history(updatePeer->id);
				h->setMute(false);
			}
		} break;
		}
	break;
	case mtpc_peerNotifySettings: {
		const MTPDpeerNotifySettings &d(settings.c_peerNotifySettings());
		NotifySettingsPtr setTo = UnknownNotifySettings;
		switch (peer.type()) {
		case mtpc_notifyAll: setTo = globalNotifyAllPtr = &globalNotifyAll; break;
		case mtpc_notifyUsers: setTo = globalNotifyUsersPtr = &globalNotifyUsers; break;
		case mtpc_notifyChats: setTo = globalNotifyChatsPtr = &globalNotifyChats; break;
		case mtpc_notifyPeer: {
			updatePeer = App::peerLoaded(peerFromMTP(peer.c_notifyPeer().vpeer));
			if (!updatePeer) break;

			if (updatePeer->notify == UnknownNotifySettings || updatePeer->notify == EmptyNotifySettings) {
				updatePeer->notify = new NotifySettings();
			}
			setTo = updatePeer->notify;
		} break;
		}
		if (setTo == UnknownNotifySettings) break;

		setTo->mute = d.vmute_until.v;
		setTo->sound = d.vsound.c_string().v;
		setTo->previews = mtpIsTrue(d.vshow_previews);
		setTo->events = d.vevents_mask.v;
		if (updatePeer) {
			if (!h) h = App::history(updatePeer->id);
			int32 changeIn = 0;
			if (isNotifyMuted(setTo, &changeIn)) {
				App::wnd()->notifyClear(h);
				h->setMute(true);
				App::regMuted(updatePeer, changeIn);
			} else {
				h->setMute(false);
			}
		}
	} break;
	}

	if (updatePeer) {
		if (history.peer() == updatePeer) {
			history.updateNotifySettings();
		}
		dialogs.updateNotifySettings(updatePeer);
		if (profile && profile->peer() == updatePeer) {
			profile->updateNotifySettings();
		}
	}
}

void MainWidget::gotNotifySetting(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings) {
	switch (peer.type()) {
	case mtpc_inputNotifyAll: applyNotifySetting(MTP_notifyAll(), settings); break;
	case mtpc_inputNotifyUsers: applyNotifySetting(MTP_notifyUsers(), settings); break;
	case mtpc_inputNotifyChats: applyNotifySetting(MTP_notifyChats(), settings); break;
	case mtpc_inputNotifyPeer:
		switch (peer.c_inputNotifyPeer().vpeer.type()) {
		case mtpc_inputPeerEmpty: applyNotifySetting(MTP_notifyPeer(MTP_peerUser(MTP_int(0))), settings); break;
		case mtpc_inputPeerSelf: applyNotifySetting(MTP_notifyPeer(MTP_peerUser(MTP_int(MTP::authedId()))), settings); break;
		case mtpc_inputPeerUser: applyNotifySetting(MTP_notifyPeer(MTP_peerUser(peer.c_inputNotifyPeer().vpeer.c_inputPeerUser().vuser_id)), settings); break;
		case mtpc_inputPeerChat: applyNotifySetting(MTP_notifyPeer(MTP_peerChat(peer.c_inputNotifyPeer().vpeer.c_inputPeerChat().vchat_id)), settings); break;
		case mtpc_inputPeerChannel: applyNotifySetting(MTP_notifyPeer(MTP_peerChannel(peer.c_inputNotifyPeer().vpeer.c_inputPeerChannel().vchannel_id)), settings); break;
		}
	break;
	}
	App::wnd()->notifySettingGot();
}

bool MainWidget::failNotifySetting(MTPInputNotifyPeer peer, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	gotNotifySetting(peer, MTP_peerNotifySettingsEmpty());
	return true;
}

void MainWidget::updateNotifySetting(PeerData *peer, bool enabled) {
	updateNotifySettingPeers.insert(peer);
	int32 muteFor = 86400 * 365;
	if (peer->notify == EmptyNotifySettings) {
		if (!enabled) {
			peer->notify = new NotifySettings();
			peer->notify->sound = "";
			peer->notify->mute = unixtime() + muteFor;
		}
	} else {
		if (peer->notify == UnknownNotifySettings) {
			peer->notify = new NotifySettings();
		}
		peer->notify->sound = enabled ? "default" : "";
		peer->notify->mute = enabled ? 0 : (unixtime() + muteFor);
	}
	if (!enabled) {
		App::regMuted(peer, muteFor + 1);
	} else {
		App::unregMuted(peer);
	}
	App::history(peer->id)->setMute(!enabled);
	if (history.peer() == peer) history.updateNotifySettings();
	updateNotifySettingTimer.start(NotifySettingSaveTimeout);
}

void MainWidget::incrementSticker(DocumentData *sticker) {
	if (!sticker || !sticker->sticker()) return;

	RecentStickerPack &recent(cGetRecentStickers());
	RecentStickerPack::iterator i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == sticker) {
			++i->second;
			if (i->second > 0x8000) {
				for (RecentStickerPack::iterator j = recent.begin(); j != e; ++j) {
					if (j->second > 1) {
						j->second /= 2;
					} else {
						j->second = 1;
					}
				}
			}
			for (; i != recent.begin(); --i) {
				if ((i - 1)->second > i->second) {
					break;
				}
				qSwap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= StickerPanPerRow * StickerPanRowsPerPage) recent.pop_back();
		recent.push_back(qMakePair(sticker, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			qSwap(*i, *(i - 1));
		}
	}

	Local::writeUserSettings();

	bool found = false;
	uint64 setId = 0;
	QString setName;
	switch (sticker->sticker()->set.type()) {
	case mtpc_inputStickerSetID: setId = sticker->sticker()->set.c_inputStickerSetID().vid.v; break;
	case mtpc_inputStickerSetShortName: setName = qs(sticker->sticker()->set.c_inputStickerSetShortName().vshort_name).toLower().trimmed(); break;
	}
	StickerSets &sets(cRefStickerSets());
	for (StickerSets::const_iterator i = sets.cbegin(); i != sets.cend(); ++i) {
		if (i->id == CustomStickerSetId || i->id == DefaultStickerSetId || (setId && i->id == setId) || (!setName.isEmpty() && i->shortName.toLower().trimmed() == setName)) {
			for (int32 j = 0, l = i->stickers.size(); j < l; ++j) {
				if (i->stickers.at(j) == sticker) {
					found = true;
					break;
				}
			}
			if (found) break;
		}
	}
	if (!found) {
		StickerSets::iterator it = sets.find(CustomStickerSetId);
		if (it == sets.cend()) {
			it = sets.insert(CustomStickerSetId, StickerSet(CustomStickerSetId, 0, lang(lng_custom_stickers), QString(), 0, 0, 0));
		}
		it->stickers.push_back(sticker);
		++it->count;
		Local::writeStickers();
	}
	history.updateRecentStickers();
}

void MainWidget::activate() {
	if (_a_show.animating()) return;
	if (!profile && !overview) {
		if (_hider) {
			if (_hider->wasOffered()) {
				_hider->setFocus();
			} else {
				dialogs.activate();
			}
        } else if (App::wnd() && !Ui::isLayerShown()) {
			if (!cSendPaths().isEmpty()) {
				forwardLayer(-1);
			} else if (history.peer()) {
				history.activate();
			} else {
				dialogs.activate();
			}
		}
	}
	App::wnd()->fixOrder();
}

void MainWidget::destroyData() {
	history.destroyData();
	dialogs.destroyData();
}

void MainWidget::updateOnlineDisplayIn(int32 msecs) {
	_onlineUpdater.start(msecs);
}

bool MainWidget::isActive() const {
	return !_isIdle && isVisible() && !_a_show.animating();
}

bool MainWidget::historyIsActive() const {
	return isActive() && !profile && !overview && history.isActive();
}

bool MainWidget::lastWasOnline() const {
	return _lastWasOnline;
}

uint64 MainWidget::lastSetOnline() const {
	return _lastSetOnline;
}

int32 MainWidget::dlgsWidth() const {
	return dialogs.width();
}

MainWidget::~MainWidget() {
	if (App::main() == this) history.showHistory(0, 0);

	delete _background;

	delete _hider;
	MTP::clearGlobalHandlers();
	delete _api;
	if (App::wnd()) App::wnd()->noMain(this);
}

void MainWidget::updateOnline(bool gotOtherOffline) {
	if (this != App::main()) return;
	App::wnd()->checkAutoLock();

	bool isOnline = App::wnd()->isActive();
	int updateIn = cOnlineUpdatePeriod();
	if (isOnline) {
		uint64 idle = psIdleTime();
		if (idle >= uint64(cOfflineIdleTimeout())) {
			isOnline = false;
			if (!_isIdle) {
				_isIdle = true;
				_idleFinishTimer.start(900);
			}
		} else {
			updateIn = qMin(updateIn, int(cOfflineIdleTimeout() - idle));
		}
	}
	uint64 ms = getms(true);
	if (isOnline != _lastWasOnline || (isOnline && _lastSetOnline + cOnlineUpdatePeriod() <= ms) || (isOnline && gotOtherOffline)) {
		if (_onlineRequest) {
			MTP::cancel(_onlineRequest);
			_onlineRequest = 0;
		}

		_lastWasOnline = isOnline;
		_lastSetOnline = ms;
		_onlineRequest = MTP::send(MTPaccount_UpdateStatus(MTP_bool(!isOnline)));

		if (App::self()) App::self()->onlineTill = unixtime() + (isOnline ? (cOnlineUpdatePeriod() / 1000) : -1);

		_lastSetOnline = getms(true);

		updateOnlineDisplay();
	} else if (isOnline) {
		updateIn = qMin(updateIn, int(_lastSetOnline + cOnlineUpdatePeriod() - ms));
	}
	_onlineTimer.start(updateIn);
}

void MainWidget::checkIdleFinish() {
	if (this != App::main()) return;
	if (psIdleTime() < uint64(cOfflineIdleTimeout())) {
		_idleFinishTimer.stop();
		_isIdle = false;
		updateOnline();
		if (App::wnd()) App::wnd()->checkHistoryActivation();
	} else {
		_idleFinishTimer.start(900);
	}
}

void MainWidget::updateReceived(const mtpPrime *from, const mtpPrime *end) {
	if (end <= from || !MTP::authedId()) return;

	App::wnd()->checkAutoLock();

	if (mtpTypeId(*from) == mtpc_new_session_created) {
		MTPNewSession newSession(from, end);
		updSeq = 0;
		MTP_LOG(0, ("getDifference { after new_session_created }%1").arg(cTestMode() ? " TESTMODE" : ""));
		return getDifference();
	} else {
		try {
			MTPUpdates updates(from, end);

			_lastUpdateTime = getms(true);
			noUpdatesTimer.start(NoUpdatesTimeout);
			if (!_ptsWaiter.requesting()) {
				feedUpdates(updates);
			}
			App::emitPeerUpdated();
		} catch (mtpErrorUnexpected &e) { // just some other type
		}
	}
	update();
}

void MainWidget::feedUpdates(const MTPUpdates &updates, uint64 randomId) {
	switch (updates.type()) {
	case mtpc_updates: {
		const MTPDupdates &d(updates.c_updates());
		if (d.vseq.v) {
			if (d.vseq.v <= updSeq) return;
			if (d.vseq.v > updSeq + 1) {
				_bySeqUpdates.insert(d.vseq.v, updates);
				return _bySeqTimer.start(WaitForSkippedTimeout);
			}
		}

		App::feedUsers(d.vusers, false);
		App::feedChats(d.vchats, false);
		feedUpdateVector(d.vupdates);

		updSetState(0, d.vdate.v, updQts, d.vseq.v);
	} break;

	case mtpc_updatesCombined: {
		const MTPDupdatesCombined &d(updates.c_updatesCombined());
		if (d.vseq_start.v) {
			if (d.vseq_start.v <= updSeq) return;
			if (d.vseq_start.v > updSeq + 1) {
				_bySeqUpdates.insert(d.vseq_start.v, updates);
				return _bySeqTimer.start(WaitForSkippedTimeout);
			}
		}

		App::feedUsers(d.vusers, false);
		App::feedChats(d.vchats, false);
		feedUpdateVector(d.vupdates);

		updSetState(0, d.vdate.v, updQts, d.vseq.v);
	} break;

	case mtpc_updateShort: {
		const MTPDupdateShort &d(updates.c_updateShort());

		feedUpdate(d.vupdate);

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updateShortMessage: {
		const MTPDupdateShortMessage &d(updates.c_updateShortMessage());
		if (!App::userLoaded(d.vuser_id.v) || (d.has_fwd_from_id() && !App::peerLoaded(peerFromMTP(d.vfwd_from_id))) || (d.has_via_bot_id() && !App::peerLoaded(peerFromUser(d.vvia_bot_id)))) {
			MTP_LOG(0, ("getDifference { good - getting user for updateShortMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));
			return getDifference();
		}

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, updates)) {
			return;
		}

		// update before applying skipped
		int32 flags = d.vflags.v | MTPDmessage::flag_from_id;
		HistoryItem *item = App::histories().addNewMessage(MTP_message(MTP_int(flags), d.vid, d.is_out() ? MTP_int(MTP::authedId()) : d.vuser_id, MTP_peerUser(d.is_out() ? d.vuser_id : MTP_int(MTP::authedId())), d.vfwd_from_id, d.vfwd_date, d.vvia_bot_id, d.vreply_to_msg_id, d.vdate, d.vmessage, MTP_messageMediaEmpty(), MTPnullMarkup, d.has_entities() ? d.ventities : MTPnullEntities, MTPint()), NewMessageUnread);
		if (item) {
			history.peerMessagesUpdated(item->history()->peer->id);
		}

		ptsApplySkippedUpdates();

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updateShortChatMessage: {
		const MTPDupdateShortChatMessage &d(updates.c_updateShortChatMessage());
		bool noFrom = !App::userLoaded(d.vfrom_id.v);
		if (!App::chatLoaded(d.vchat_id.v) || noFrom || (d.has_fwd_from_id() && !App::peerLoaded(peerFromMTP(d.vfwd_from_id))) || (d.has_via_bot_id() && !App::peerLoaded(peerFromUser(d.vvia_bot_id)))) {
			MTP_LOG(0, ("getDifference { good - getting user for updateShortChatMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));
			if (noFrom && App::api()) App::api()->requestFullPeer(App::chatLoaded(d.vchat_id.v));
			return getDifference();
		}

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, updates)) {
			return;
		}

		// update before applying skipped
		int32 flags = d.vflags.v | MTPDmessage::flag_from_id;
		HistoryItem *item = App::histories().addNewMessage(MTP_message(MTP_int(flags), d.vid, d.vfrom_id, MTP_peerChat(d.vchat_id), d.vfwd_from_id, d.vfwd_date, d.vvia_bot_id, d.vreply_to_msg_id, d.vdate, d.vmessage, MTP_messageMediaEmpty(), MTPnullMarkup, d.has_entities() ? d.ventities : MTPnullEntities, MTPint()), NewMessageUnread);
		if (item) {
			history.peerMessagesUpdated(item->history()->peer->id);
		}

		ptsApplySkippedUpdates();

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updateShortSentMessage: {
		const MTPDupdateShortSentMessage &d(updates.c_updateShortSentMessage());
		if (randomId) {
			PeerId peerId = 0;
			QString text;
			App::histSentDataByItem(randomId, peerId, text);

			feedUpdate(MTP_updateMessageID(d.vid, MTP_long(randomId))); // ignore real date
			if (peerId) {
				if (HistoryItem *item = App::histItemById(peerToChannel(peerId), d.vid.v)) {
					item->setText(text, d.has_entities() ? entitiesFromMTP(d.ventities.c_vector().v) : EntitiesInText());
					item->updateMedia(d.has_media() ? (&d.vmedia) : 0);
					item->initDimensions();
					Notify::historyItemResized(item);

					item->addToOverview(AddToOverviewNew);
				}
			}
		}

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, updates)) {
			return;
		}
		// update before applying skipped
		ptsApplySkippedUpdates();

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updatesTooLong: {
		MTP_LOG(0, ("getDifference { good - updatesTooLong received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		return getDifference();
	} break;
	}
}

void MainWidget::feedUpdate(const MTPUpdate &update) {
	if (!MTP::authedId()) return;

	switch (update.type()) {
	case mtpc_updateNewMessage: {
		const MTPDupdateNewMessage &d(update.c_updateNewMessage());

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		bool needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			HistoryItem *item = App::histories().addNewMessage(d.vmessage, NewMessageUnread);
			if (item) {
				history.peerMessagesUpdated(item->history()->peer->id);
			}
		}
		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateMessageID: {
		const MTPDupdateMessageID &d(update.c_updateMessageID());
		FullMsgId msg = App::histItemByRandom(d.vrandom_id.v);
		if (msg.msg) {
			HistoryItem *msgRow = App::histItemById(msg);
			if (msgRow) {
				if (App::histItemById(msg.channel, d.vid.v)) {
					History *h = msgRow->history();
					bool wasLast = (h->lastMsg == msgRow);
					msgRow->destroy();
					if (wasLast && !h->lastMsg) {
						checkPeerHistory(h->peer);
					}
					history.peerMessagesUpdated();
				} else {
					App::historyUnregItem(msgRow);
					if (App::wnd()) App::wnd()->changingMsgId(msgRow, d.vid.v);
					msgRow->setId(d.vid.v);
					if (msgRow->history()->peer->isSelf()) {
						msgRow->history()->unregTyping(App::self());
					}
					App::historyRegItem(msgRow);
					Ui::repaintHistoryItem(msgRow);
				}
			}
			App::historyUnregRandom(d.vrandom_id.v);
		}
		App::historyUnregSentData(d.vrandom_id.v);
	} break;

	case mtpc_updateReadMessagesContents: {
		const MTPDupdateReadMessagesContents &d(update.c_updateReadMessagesContents());

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		const QVector<MTPint> &v(d.vmessages.c_vector().v);
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			if (HistoryItem *item = App::histItemById(NoChannel, v.at(i).v)) {
				if (item->isMediaUnread()) {
					item->markMediaRead();
					Ui::repaintHistoryItem(item);
					if (item->out() && item->history()->peer->isUser()) {
						item->history()->peer->asUser()->madeAction();
					}
				}
			}
		}

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateReadHistoryInbox: {
		const MTPDupdateReadHistoryInbox &d(update.c_updateReadHistoryInbox());

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		App::feedInboxRead(peerFromMTP(d.vpeer), d.vmax_id.v);

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateReadHistoryOutbox: {
		const MTPDupdateReadHistoryOutbox &d(update.c_updateReadHistoryOutbox());

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		PeerId id = peerFromMTP(d.vpeer);
		App::feedOutboxRead(id, d.vmax_id.v);
		if (history.peer() && history.peer()->id == id) {
			history.update();
		}
		if (History *h = App::historyLoaded(id)) {
			if (h->lastMsg && h->lastMsg->out() && h->lastMsg->id <= d.vmax_id.v) {
				dlgUpdated(h, h->lastMsg->id);
			}
			if (!h->dialogs.isEmpty()) {
				dlgUpdated(h->dialogs[0]);
			}
		}

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateWebPage: {
		const MTPDupdateWebPage &d(update.c_updateWebPage());

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		App::feedWebPage(d.vwebpage);
		history.updatePreview();
		webPagesUpdate();

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateDeleteMessages: {
		const MTPDupdateDeleteMessages &d(update.c_updateDeleteMessages());

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		App::feedWereDeleted(NoChannel, d.vmessages.c_vector().v);
		history.peerMessagesUpdated();

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateUserTyping: {
		const MTPDupdateUserTyping &d(update.c_updateUserTyping());
		History *history = App::historyLoaded(peerFromUser(d.vuser_id));
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (history && user) {
			App::histories().regSendAction(history, user, d.vaction);
		}
	} break;

	case mtpc_updateChatUserTyping: {
		const MTPDupdateChatUserTyping &d(update.c_updateChatUserTyping());
		History *history = 0;
		if (PeerData *chat = App::peerLoaded(peerFromChat(d.vchat_id.v))) {
			history = App::historyLoaded(chat->id);
		} else if (PeerData *channel = App::peerLoaded(peerFromChannel(d.vchat_id.v))) {
			history = App::historyLoaded(channel->id);
		}
		UserData *user = (d.vuser_id.v == MTP::authedId()) ? 0 : App::userLoaded(d.vuser_id.v);
		if (history && user) {
			App::histories().regSendAction(history, user, d.vaction);
		}
	} break;

	case mtpc_updateChatParticipants: {
		App::feedParticipants(update.c_updateChatParticipants().vparticipants, true, false);
	} break;

	case mtpc_updateChatParticipantAdd: {
		App::feedParticipantAdd(update.c_updateChatParticipantAdd(), false);
	} break;

	case mtpc_updateChatParticipantDelete: {
		App::feedParticipantDelete(update.c_updateChatParticipantDelete(), false);
	} break;

	case mtpc_updateChatAdmins: {
		App::feedChatAdmins(update.c_updateChatAdmins(), false);
	} break;

	case mtpc_updateChatParticipantAdmin: {
		App::feedParticipantAdmin(update.c_updateChatParticipantAdmin(), false);
	} break;

	case mtpc_updateUserStatus: {
		const MTPDupdateUserStatus &d(update.c_updateUserStatus());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user) {
			switch (d.vstatus.type()) {
			case mtpc_userStatusEmpty: user->onlineTill = 0; break;
			case mtpc_userStatusRecently:
				if (user->onlineTill > -10) { // don't modify pseudo-online
					user->onlineTill = -2;
				}
			break;
			case mtpc_userStatusLastWeek: user->onlineTill = -3; break;
			case mtpc_userStatusLastMonth: user->onlineTill = -4; break;
			case mtpc_userStatusOffline: user->onlineTill = d.vstatus.c_userStatusOffline().vwas_online.v; break;
			case mtpc_userStatusOnline: user->onlineTill = d.vstatus.c_userStatusOnline().vexpires.v; break;
			}
			App::markPeerUpdated(user);
		}
		if (d.vuser_id.v == MTP::authedId()) {
			if (d.vstatus.type() == mtpc_userStatusOffline || d.vstatus.type() == mtpc_userStatusEmpty) {
				updateOnline(true);
				if (d.vstatus.type() == mtpc_userStatusOffline) {
					cSetOtherOnline(d.vstatus.c_userStatusOffline().vwas_online.v);
				}
			} else if (d.vstatus.type() == mtpc_userStatusOnline) {
				cSetOtherOnline(d.vstatus.c_userStatusOnline().vexpires.v);
			}
		}
	} break;

	case mtpc_updateUserName: {
		const MTPDupdateUserName &d(update.c_updateUserName());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user) {
			if (user->contact <= 0) {
				user->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), user->nameOrPhone, textOneLine(qs(d.vusername)));
			} else {
				user->setName(textOneLine(user->firstName), textOneLine(user->lastName), user->nameOrPhone, textOneLine(qs(d.vusername)));
			}
			App::markPeerUpdated(user);
		}
	} break;

	case mtpc_updateUserPhoto: {
		const MTPDupdateUserPhoto &d(update.c_updateUserPhoto());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user) {
			user->setPhoto(d.vphoto);
			user->photo->load();
			if (mtpIsTrue(d.vprevious)) {
				user->photosCount = -1;
				user->photos.clear();
			} else {
				if (user->photoId && user->photoId != UnknownPeerPhotoId) {
					if (user->photosCount > 0) ++user->photosCount;
					user->photos.push_front(App::photo(user->photoId));
				} else {
					user->photosCount = -1;
					user->photos.clear();
				}
			}
			App::markPeerUpdated(user);
			if (App::wnd()) App::wnd()->mediaOverviewUpdated(user, OverviewCount);
		}
	} break;

	case mtpc_updateContactRegistered: {
		const MTPDupdateContactRegistered &d(update.c_updateContactRegistered());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user) {
			if (App::history(user->id)->loadedAtBottom()) {
				App::history(user->id)->addNewService(clientMsgId(), date(d.vdate), lng_action_user_registered(lt_from, user->name), MTPDmessage::flag_unread);
			}
		}
	} break;

	case mtpc_updateContactLink: {
		const MTPDupdateContactLink &d(update.c_updateContactLink());
		App::feedUserLink(d.vuser_id, d.vmy_link, d.vforeign_link, false);
	} break;

	case mtpc_updateNotifySettings: {
		const MTPDupdateNotifySettings &d(update.c_updateNotifySettings());
		applyNotifySetting(d.vpeer, d.vnotify_settings);
	} break;

	case mtpc_updateDcOptions: {
		const MTPDupdateDcOptions &d(update.c_updateDcOptions());
		MTP::updateDcOptions(d.vdc_options.c_vector().v);
	} break;

	case mtpc_updateUserPhone: {
		const MTPDupdateUserPhone &d(update.c_updateUserPhone());
		UserData *user = App::userLoaded(d.vuser_id.v);
		if (user) {
			user->setPhone(qs(d.vphone));
			user->setName(user->firstName, user->lastName, (user->contact || isServiceUser(user->id) || user->isSelf() || user->phone.isEmpty()) ? QString() : App::formatPhone(user->phone), user->username);
			App::markPeerUpdated(user);
		}
	} break;

	case mtpc_updateNewEncryptedMessage: {
		const MTPDupdateNewEncryptedMessage &d(update.c_updateNewEncryptedMessage());
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

	case mtpc_updateUserBlocked: {
		const MTPDupdateUserBlocked &d(update.c_updateUserBlocked());
		if (UserData *user = App::userLoaded(d.vuser_id.v)) {
			user->blocked = mtpIsTrue(d.vblocked) ? UserIsBlocked : UserIsNotBlocked;
			App::markPeerUpdated(user);
		}
	} break;

	case mtpc_updateNewAuthorization: {
		const MTPDupdateNewAuthorization &d(update.c_updateNewAuthorization());
		QDateTime datetime = date(d.vdate);

		QString name = App::self()->firstName;
		QString day = langDayOfWeekFull(datetime.date()), date = langDayOfMonthFull(datetime.date()), time = datetime.time().toString(cTimeFormat());
		QString device = qs(d.vdevice), location = qs(d.vlocation);
		LangString text = lng_new_authorization(lt_name, App::self()->firstName, lt_day, day, lt_date, date, lt_time, time, lt_device, device, lt_location, location);
		App::wnd()->serviceNotification(text);

		emit App::wnd()->newAuthorization();
	} break;

	case mtpc_updateServiceNotification: {
		const MTPDupdateServiceNotification &d(update.c_updateServiceNotification());
		if (mtpIsTrue(d.vpopup)) {
			Ui::showLayer(new InformBox(qs(d.vmessage)));
		} else {
			App::wnd()->serviceNotification(qs(d.vmessage), d.vmedia);
		}
	} break;

	case mtpc_updatePrivacy: {
		const MTPDupdatePrivacy &d(update.c_updatePrivacy());
	} break;

	case mtpc_updateChannel: {
		const MTPDupdateChannel &d(update.c_updateChannel());
		if (ChannelData *channel = App::channelLoaded(d.vchannel_id.v)) {
			App::markPeerUpdated(channel);
			channel->inviter = 0;
			if (!channel->amIn()) {
				deleteConversation(channel, false);
			} else if (!channel->amCreator() && App::history(channel->id)) { // create history
				_updatedChannels.insert(channel, true);
				if (App::api()) App::api()->requestSelfParticipant(channel);
			}
		}
	} break;

	case mtpc_updateNewChannelMessage: {
		const MTPDupdateNewChannelMessage &d(update.c_updateNewChannelMessage());
		ChannelData *channel = App::channelLoaded(peerToChannel(peerFromMessage(d.vmessage)));

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else if (!channel->ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
				return;
			}
		}

		// update before applying skipped
		bool needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			HistoryItem *item = App::histories().addNewMessage(d.vmessage, NewMessageUnread);
			if (item) {
				history.peerMessagesUpdated(item->history()->peer->id);
			}
		}
		if (channel && !_handlingChannelDifference) {
			channel->ptsApplySkippedUpdates();
		}
	} break;

	case mtpc_updateReadChannelInbox: {
		const MTPDupdateReadChannelInbox &d(update.c_updateReadChannelInbox());
		ChannelData *channel = App::channelLoaded(d.vchannel_id.v);
		App::feedInboxRead(peerFromChannel(d.vchannel_id.v), d.vmax_id.v);
	} break;

	case mtpc_updateDeleteChannelMessages: {
		const MTPDupdateDeleteChannelMessages &d(update.c_updateDeleteChannelMessages());
		ChannelData *channel = App::channelLoaded(d.vchannel_id.v);

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else if (!channel->ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
				return;
			}
		}

		// update before applying skipped
		App::feedWereDeleted(d.vchannel_id.v, d.vmessages.c_vector().v);
		history.peerMessagesUpdated();

		if (channel && !_handlingChannelDifference) {
			channel->ptsApplySkippedUpdates();
		}
	} break;

	case mtpc_updateChannelGroup: {
		if (!_handlingChannelDifference) {
			LOG(("API Error: got updateChannelGroup not in channelDifference!"));
		}
	} break;

	case mtpc_updateChannelTooLong: {
		const MTPDupdateChannelTooLong &d(update.c_updateChannelTooLong());
		if (ChannelData *channel = App::channelLoaded(d.vchannel_id.v)) {
			getChannelDifference(channel);
		}
	} break;

	case mtpc_updateChannelMessageViews: {
		const MTPDupdateChannelMessageViews &d(update.c_updateChannelMessageViews());
		if (HistoryItem *item = App::histItemById(d.vchannel_id.v, d.vid.v)) {
			item->setViewsCount(d.vviews.v);
		}
	} break;

	case mtpc_updateNewStickerSet: {
		const MTPDupdateNewStickerSet &d(update.c_updateNewStickerSet());
		if (d.vstickerset.type() == mtpc_messages_stickerSet) {
			const MTPDmessages_stickerSet &set(d.vstickerset.c_messages_stickerSet());
			if (set.vset.type() == mtpc_stickerSet) {
				const MTPDstickerSet &s(set.vset.c_stickerSet());

				StickerSets &sets(cRefStickerSets());
				StickerSets::iterator it = sets.find(s.vid.v);
				if (it == sets.cend()) {
					it = sets.insert(s.vid.v, StickerSet(s.vid.v, s.vaccess_hash.v, stickerSetTitle(s), qs(s.vshort_name), s.vcount.v, s.vhash.v, s.vflags.v));
				}

				const QVector<MTPDocument> &v(set.vdocuments.c_vector().v);
				it->stickers.clear();
				it->stickers.reserve(v.size());
				for (int32 i = 0, l = v.size(); i < l; ++i) {
					DocumentData *doc = App::feedDocument(v.at(i));
					if (!doc || !doc->sticker()) continue;

					it->stickers.push_back(doc);
				}
				it->emoji.clear();
				const QVector<MTPStickerPack> &packs(set.vpacks.c_vector().v);
				for (int32 i = 0, l = packs.size(); i < l; ++i) {
					if (packs.at(i).type() != mtpc_stickerPack) continue;
					const MTPDstickerPack &pack(packs.at(i).c_stickerPack());
					if (EmojiPtr e = emojiGetNoColor(emojiFromText(qs(pack.vemoticon)))) {
						const QVector<MTPlong> &stickers(pack.vdocuments.c_vector().v);
						StickerPack p;
						p.reserve(stickers.size());
						for (int32 j = 0, c = stickers.size(); j < c; ++j) {
							DocumentData *doc = App::document(stickers.at(j).v);
							if (!doc || !doc->sticker()) continue;

							p.push_back(doc);
						}
						it->emoji.insert(e, p);
					}
				}

				StickerSetsOrder &order(cRefStickerSetsOrder());
				int32 insertAtIndex = 0, currentIndex = order.indexOf(s.vid.v);
				if (currentIndex != insertAtIndex) {
					if (currentIndex > 0) {
						order.removeAt(currentIndex);
					}
					order.insert(insertAtIndex, s.vid.v);
				}

				StickerSets::iterator custom = sets.find(CustomStickerSetId);
				if (custom != sets.cend()) {
					for (int32 i = 0, l = it->stickers.size(); i < l; ++i) {
						int32 removeIndex = custom->stickers.indexOf(it->stickers.at(i));
						if (removeIndex >= 0) custom->stickers.removeAt(removeIndex);
					}
					if (custom->stickers.isEmpty()) {
						sets.erase(custom);
					}
				}
				Local::writeStickers();
				emit stickersUpdated();
			}
		}
	} break;

	case mtpc_updateStickerSetsOrder: {
		const MTPDupdateStickerSetsOrder &d(update.c_updateStickerSetsOrder());
		const QVector<MTPlong> &order(d.vorder.c_vector().v);
		const StickerSets &sets(cStickerSets());
		StickerSetsOrder result;
		for (int32 i = 0, l = order.size(); i < l; ++i) {
			if (sets.constFind(order.at(i).v) == sets.cend()) {
				break;
			}
			result.push_back(order.at(i).v);
		}
		if (result.size() != cStickerSetsOrder().size() || result.size() != order.size()) {
			cSetLastStickersUpdate(0);
			App::main()->updateStickers();
		} else {
			cSetStickerSetsOrder(result);
			Local::writeStickers();
			emit stickersUpdated();
		}
	} break;

	case mtpc_updateStickerSets: {
		cSetLastStickersUpdate(0);
		App::main()->updateStickers();
	} break;

	case mtpc_updateSavedGifs: {
		cSetLastSavedGifsUpdate(0);
		App::main()->updateStickers();
	} break;

	}
}
