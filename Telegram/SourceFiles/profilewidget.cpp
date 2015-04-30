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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "lang.h"
#include "window.h"
#include "mainwidget.h"
#include "profilewidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "boxes/photocropbox.h"
#include "application.h"
#include "boxes/contactsbox.h"
#include "gui/filedialog.h"

ProfileInner::ProfileInner(ProfileWidget *profile, ScrollArea *scroll, const PeerData *peer) : TWidget(0),
	_profile(profile), _scroll(scroll), _peer(App::peer(peer->id)),
	_peerUser(_peer->chat ? 0 : _peer->asUser()), _peerChat(_peer->chat ? _peer->asChat() : 0), _hist(App::history(peer->id)),
	_chatAdmin(_peerChat ? (_peerChat->admin == MTP::authedId()) : false),

	// profile
	_nameCache(peer->name),
	_uploadPhoto(this, lang(lng_profile_set_group_photo), st::btnShareContact),
	_addParticipant(this, lang(lng_profile_add_participant), st::btnShareContact),
	_sendMessage(this, lang(lng_profile_send_message), st::btnShareContact),
	_shareContact(this, lang(lng_profile_share_contact), st::btnShareContact),
	_cancelPhoto(this, lang(lng_cancel)),
	_createInvitationLink(this, lang(lng_group_invite_create)),
	_invitationLink(this, qsl("telegram.me/joinchat/")),

	a_photo(0),
	_photoOver(false),

	// settings
	_enableNotifications(this, lang(lng_profile_enable_notifications)),
	_clearHistory(this, lang(lng_profile_clear_history)),

	// shared media
	_allMediaTypes(false),
	_mediaShowAll(this, lang(lng_profile_show_all_types)),
	_mediaPhotos(this, QString()),
	_mediaVideos(this, QString()),
	_mediaDocuments(this, QString()),
	_mediaAudios(this, QString()),

	// participants
	_pHeight(st::profileListPhotoSize + st::profileListPadding.height() * 2),
	_kickWidth(st::linkFont->m.width(lang(lng_profile_kick))),
	_selectedRow(-1), _lastPreload(0), _contactId(0),
	_kickOver(0), _kickDown(0), _kickConfirm(0),
	
	_menu(0) {

	connect(App::api(), SIGNAL(fullPeerLoaded(PeerData*)), this, SLOT(onFullPeerLoaded(PeerData*)));

	if (_peerUser) {
		_phoneText = _peerUser->phone.isEmpty() ? QString() : App::formatPhone(_peerUser->phone);
		App::api()->requestFullPeer(_peer);
	} else if (_peerChat->photoId) {
		PhotoData *ph = App::photo(_peerChat->photoId);
		if (ph->date) {
			_photoLink = TextLinkPtr(new PhotoLink(ph, _peer));
		}
	} else {
		App::api()->requestFullPeer(_peer);
	}

	// profile
	_nameText.setText(st::profileNameFont, _nameCache, _textNameOptions);

	connect(&_uploadPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhoto()));
	connect(&_addParticipant, SIGNAL(clicked()), this, SLOT(onAddParticipant()));
	connect(&_sendMessage, SIGNAL(clicked()), this, SLOT(onSendMessage()));
	connect(&_shareContact, SIGNAL(clicked()), this, SLOT(onShareContact()));
	connect(&_cancelPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhotoCancel()));
	connect(&_createInvitationLink, SIGNAL(clicked()), this, SLOT(onCreateInvitationLink()));
	connect(&_invitationLink, SIGNAL(clicked()), this, SLOT(onInvitationLink()));
	_invitationLink.setAcceptBoth(true);
	updateInvitationLink();

	connect(App::app(), SIGNAL(peerPhotoDone(PeerId)), this, SLOT(onPhotoUpdateDone(PeerId)));
	connect(App::app(), SIGNAL(peerPhotoFail(PeerId)), this, SLOT(onPhotoUpdateFail(PeerId)));

	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(peerUpdated(PeerData *)));

	// settings
	connect(&_enableNotifications, SIGNAL(clicked()), this, SLOT(onEnableNotifications()));
	connect(&_clearHistory, SIGNAL(clicked()), this, SLOT(onClearHistory()));

	// shared media
	connect(&_mediaShowAll, SIGNAL(clicked()), this, SLOT(onMediaShowAll()));
	connect(&_mediaPhotos, SIGNAL(clicked()), this, SLOT(onMediaPhotos()));
	connect(&_mediaVideos, SIGNAL(clicked()), this, SLOT(onMediaVideos()));
	connect(&_mediaDocuments, SIGNAL(clicked()), this, SLOT(onMediaDocuments()));
	connect(&_mediaAudios, SIGNAL(clicked()), this, SLOT(onMediaAudios()));
	_mediaLinks[OverviewPhotos] = &_mediaPhotos;
	_mediaLinks[OverviewVideos] = &_mediaVideos;
	_mediaLinks[OverviewDocuments] = &_mediaDocuments;
	_mediaLinks[OverviewAudios] = &_mediaAudios;
	App::main()->preloadOverviews(_peer);

	App::contextItem(0);

	resizeEvent(0);
	showAll();
}

void ProfileInner::onShareContact() {
	App::main()->shareContactLayer(_peerUser);
}

void ProfileInner::onSendMessage() {
	App::main()->showPeer(_peer->id);
}

void ProfileInner::onEnableNotifications() {
	App::main()->updateNotifySetting(_peer, _enableNotifications.checked());
}

void ProfileInner::saveError(const QString &str) {
	_errorText = str;
	resizeEvent(0);
	showAll();
	update();
}

void ProfileInner::loadProfilePhotos(int32 yFrom) {
	_lastPreload = yFrom;

	int32 yTo = yFrom + (parentWidget() ? parentWidget()->height() : App::wnd()->height()) * 5;
	MTP::clearLoaderPriorities();

	int32 partfrom = _mediaAudios.y() + _mediaAudios.height() + st::profileHeaderSkip;
	yFrom -= partfrom;
	yTo -= partfrom;

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;
	yFrom /= _pHeight;
	yTo = yTo / _pHeight + 1;
	if (yFrom >= _participants.size()) return;
	if (yTo > _participants.size()) yTo = _participants.size();
	for (int32 i = yFrom; i < yTo; ++i) {
		_participants[i]->photo->load();
	}
}

void ProfileInner::onUpdatePhoto() {
	saveError();

	QStringList imgExtensions(cImgExtensions());	
	QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;All files (*.*)"));

	QImage img;
	QString file;
	QByteArray remoteContent;
	if (filedialogGetOpenFile(file, remoteContent, lang(lng_choose_images), filter)) {
		if (!remoteContent.isEmpty()) {
			img = App::readImage(remoteContent);
		} else {
			if (!file.isEmpty()) {
				img = App::readImage(file);
			}
		}
	} else {
		return;
	}

	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		saveError(lang(lng_bad_photo));
		return;
	}
	PhotoCropBox *box = new PhotoCropBox(img, _peer->id);
	connect(box, SIGNAL(closed()), this, SLOT(onPhotoUpdateStart()));
	App::wnd()->showLayer(box);
}

void ProfileInner::onClearHistory() {
	ConfirmBox *box = new ConfirmBox(lng_sure_delete_history(lt_contact, _peer->name));
	connect(box, SIGNAL(confirmed()), this, SLOT(onClearHistorySure()));
	App::wnd()->showLayer(box);
}

void ProfileInner::onClearHistorySure() {
	App::main()->showPeer(0, 0, true);
	App::wnd()->hideLayer();
	App::main()->clearHistory(_peer);
}

void ProfileInner::onAddParticipant() {
	App::wnd()->showLayer(new ContactsBox(_peerChat));
}

void ProfileInner::onUpdatePhotoCancel() {
	App::app()->cancelPhotoUpdate(_peer->id);
	showAll();
	update();
}

void ProfileInner::onPhotoUpdateStart() {
	showAll();
	update();
}

void ProfileInner::onPhotoUpdateFail(PeerId peer) {
	if (_peer->id != peer) return;
	saveError(lang(lng_bad_photo));
	showAll();
	update();
}

void ProfileInner::onPhotoUpdateDone(PeerId peer) {
	if (_peer->id != peer) return;
	saveError();
	showAll();
	update();
}

void ProfileInner::onMediaShowAll() {
	_allMediaTypes = true;
	resizeEvent(0);
	showAll();
}

void ProfileInner::onMediaPhotos() {
	App::main()->showMediaOverview(_peer, OverviewPhotos);
}

void ProfileInner::onMediaVideos() {
	App::main()->showMediaOverview(_peer, OverviewVideos);
}

void ProfileInner::onMediaDocuments() {
	App::main()->showMediaOverview(_peer, OverviewDocuments);
}

void ProfileInner::onMediaAudios() {
	App::main()->showMediaOverview(_peer, OverviewAudios);
}

void ProfileInner::onInvitationLink() {
	QApplication::clipboard()->setText(_peerChat->invitationUrl);
	App::wnd()->showLayer(new ConfirmBox(lang(lng_group_invite_copied), true));
}

void ProfileInner::onCreateInvitationLink() {
	ConfirmBox *box = new ConfirmBox(lang(_peerChat->invitationUrl.isEmpty() ? lng_group_invite_about : lng_group_invite_about_new));
	connect(box, SIGNAL(confirmed()), this, SLOT(onCreateInvitationLinkSure()));
	App::wnd()->showLayer(box);
}

void ProfileInner::onCreateInvitationLinkSure() {
	MTP::send(MTPmessages_ExportChatInvite(App::peerToMTP(_peerChat->id).c_peerChat().vchat_id), rpcDone(&ProfileInner::chatInviteDone));
}

void ProfileInner::chatInviteDone(const MTPExportedChatInvite &result) {
	_peerChat->invitationUrl = (result.type() == mtpc_chatInviteExported) ? qs(result.c_chatInviteExported().vlink) : QString();
	updateInvitationLink();
	showAll();
	resizeEvent(0);
	App::wnd()->hideLayer();
}

void ProfileInner::onFullPeerLoaded(PeerData *peer) {
	if (peer != _peer) return;
	if (_peerUser) {
		PhotoData *userPhoto = _peerUser->photoId ? App::photo(_peerUser->photoId) : 0;
		if (userPhoto && userPhoto->date) {
			_photoLink = TextLinkPtr(new PhotoLink(userPhoto, _peer));
		} else {
			_photoLink = TextLinkPtr();
		}
	} else if (_peerChat) {
		updateInvitationLink();
		showAll();
		resizeEvent(0);
	}
}

void ProfileInner::peerUpdated(PeerData *data) {
	if (data == _peer) {
		PhotoData *photo = 0;
		if (_peerUser) {
			_phoneText = _peerUser->phone.isEmpty() ? QString() : App::formatPhone(_peerUser->phone);
			if (_peerUser->photoId) photo = App::photo(_peerUser->photoId);
		} else {
			if (_peerChat->photoId) photo = App::photo(_peerChat->photoId);
		}
		_photoLink = (photo && photo->date) ? TextLinkPtr(new PhotoLink(photo, _peer)) : TextLinkPtr();
		if (_peer->name != _nameCache) {
			_nameCache = _peer->name;
			_nameText.setText(st::profileNameFont, _nameCache, _textNameOptions);
		}
	}
	showAll();
	update();
}

void ProfileInner::updateOnlineDisplay() {
	reorderParticipants();
	update();
}

void ProfileInner::updateOnlineDisplayTimer() {
	int32 t = unixtime(), minIn = 86400;
	if (_peerChat) {
		if (_peerChat->participants.isEmpty()) return;

		for (ChatData::Participants::const_iterator i = _peerChat->participants.cbegin(), e = _peerChat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key()->onlineTill, t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else {
		minIn = App::onlineWillChangeIn(_peerUser->onlineTill, t);
	}
	App::main()->updateOnlineDisplayIn(minIn * 1000);
}

void ProfileInner::reorderParticipants() {
	int32 was = _participants.size(), t = unixtime(), onlineCount = 0;
	if (_peerChat && !_peerChat->forbidden) {
		if (_peerChat->count <= 0 || !_peerChat->participants.isEmpty()) {
			_participants.clear();
			for (ParticipantsData::iterator i = _participantsData.begin(), e = _participantsData.end(); i != e; ++i) {
				if (*i) {
					delete *i;
					*i = 0;
				}
			}
			_participants.reserve(_peerChat->participants.size());
			_participantsData.resize(_peerChat->participants.size());
		}
		UserData *self = App::self();
        bool onlyMe = true;
        for (ChatData::Participants::const_iterator i = _peerChat->participants.cbegin(), e = _peerChat->participants.cend(); i != e; ++i) {
			UserData *user = i.key();
			int32 until = App::onlineForSort(user->onlineTill, t);
			Participants::iterator before = _participants.begin();
			if (user != self) {
				if (before != _participants.end() && (*before) == self) {
					++before;
				}
				while (before != _participants.end() && App::onlineForSort((*before)->onlineTill, t) >= until) {
					++before;
				}
                if (until > t && onlyMe) onlyMe = false;
            }
			_participants.insert(before, user);
			if (until > t) {
				++onlineCount;
			}
		}
		if (_peerChat->count > 0 && _participants.isEmpty()) {
			App::api()->requestFullPeer(_peer);
			if (_onlineText.isEmpty()) _onlineText = lng_chat_status_members(lt_count, _peerChat->count);
        } else if (onlineCount && !onlyMe) {
			_onlineText = lng_chat_status_members_online(lt_count, _participants.size(), lt_count_online, onlineCount);
		} else {
			_onlineText = lng_chat_status_members(lt_count, _participants.size());
		}
		loadProfilePhotos(_lastPreload);
	} else {
		_participants.clear();
		if (_peerUser) {
			_onlineText = App::onlineText(_peerUser, t, true);
		} else {
			_onlineText = lang(lng_chat_status_unaccessible);
		}
	}
	if (was != _participants.size()) {
		resizeEvent(0);
	}
}

bool ProfileInner::event(QEvent *e) {
	if (e->type() == QEvent::MouseMove) {
		_lastPos = static_cast<QMouseEvent*>(e)->globalPos();
		updateSelected();
	}
	return QWidget::event(e);
}

void ProfileInner::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r(e->rect());
	p.setClipRect(r);

	int32 top = 0, l_time = unixtime();

	// profile
	top += st::profilePadding.top();
	if (_photoLink || !_peerChat || _peerChat->forbidden) {
		p.drawPixmap(_left, top, _peer->photo->pix(st::profilePhotoSize));
	} else {
		if (a_photo.current() < 1) {
			p.drawPixmap(QPoint(_left, top), App::sprite(), st::setPhotoImg);
		}
		if (a_photo.current() > 0) {
			p.setOpacity(a_photo.current());
			p.drawPixmap(QPoint(_left, top), App::sprite(), st::setOverPhotoImg);
			p.setOpacity(1);
		}
	}
	p.setPen(st::black->p);
	_nameText.drawElided(p, _left + st::profilePhotoSize + st::profileNameLeft, top + st::profileNameTop, _width - st::profilePhotoSize - st::profileNameLeft);

	p.setFont(st::profileStatusFont->f);
	int32 addbyname = 0;
	if (_peerUser && !_peerUser->username.isEmpty()) {
		addbyname = st::profileStatusTop + st::linkFont->ascent - (st::profileNameTop + st::profileNameFont->ascent);
		p.setPen(st::black->p);
		p.drawText(_left + st::profilePhotoSize + st::profileStatusLeft, top + st::profileStatusTop + st::linkFont->ascent, '@' + _peerUser->username);
	}
	p.setPen((_peerUser && App::onlineColorUse(_peerUser->onlineTill, l_time) ? st::profileOnlineColor : st::profileOfflineColor)->p);
	p.drawText(_left + st::profilePhotoSize + st::profileStatusLeft, top + addbyname + st::profileStatusTop + st::linkFont->ascent, _onlineText);
	if (_chatAdmin && !_peerChat->invitationUrl.isEmpty()) {
		p.setPen(st::black->p);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, _createInvitationLink.y() + st::linkFont->ascent, lang(lng_group_invite_link));
	}
	if (!_cancelPhoto.isHidden()) {
		p.setPen(st::profileOfflineColor->p);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, _cancelPhoto.y() + addbyname + st::linkFont->ascent, lang(lng_settings_uploading_photo));
	}

	if (!_errorText.isEmpty()) {
		p.setFont(st::setErrFont->f);
		p.setPen(st::setErrColor->p);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, top + addbyname + st::profilePhoneTop + st::profilePhoneFont->ascent, _errorText);
	}
	if (!_phoneText.isEmpty()) {
		p.setPen(st::black->p);
		p.setFont(st::linkFont->f);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, top + addbyname + st::profilePhoneTop + st::profilePhoneFont->ascent, _phoneText);
	}
	top += st::profilePhotoSize;
	top += st::profileButtonTop;

	if (_peerChat && _peerChat->forbidden) {
		int32 w = st::btnShareContact.font->m.width(lang(lng_profile_chat_unaccessible));
		p.setFont(st::btnShareContact.font->f);
		p.setPen(st::profileOfflineColor->p);
		p.drawText(_left + (_width - w) / 2, top + st::btnShareContact.textTop + st::btnShareContact.font->ascent, lang(lng_profile_chat_unaccessible));
	}
	top += _shareContact.height();

	// settings
	p.setFont(st::profileHeaderFont->f);
	p.setPen(st::profileHeaderColor->p);
	p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lang(lng_profile_settings_section));
	top += st::profileHeaderSkip;

	top += _enableNotifications.height();

	// shared media
	p.setFont(st::profileHeaderFont->f);
	p.setPen(st::profileHeaderColor->p);
	p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lang(lng_profile_shared_media));
	top += st::profileHeaderSkip;

	p.setFont(st::linkFont->f);
	p.setPen(st::black->p);
	int oneState = 0; // < 0 - loading, 0 - no media, > 0 - link shown
	for (int i = 0; i < OverviewCount; ++i) {
		int32 count = (_hist->_overviewCount[i] > 0) ? _hist->_overviewCount[i] : (_hist->_overviewCount[i] == 0 ? _hist->_overview[i].size() : -1);
		if (count < 0) {
			if (!oneState) oneState = count;
			if (!_allMediaTypes) {
				p.drawText(_left, top + st::linkFont->ascent, lang(lng_profile_loading));
				break;
			}
		} else if (count > 0) {
			oneState = count;
			if (!_allMediaTypes) {
				break;
			}
			top += _mediaLinks[i]->height() + st::setLittleSkip;
		}
	}
	if (_allMediaTypes) {
		if (oneState > 0) {
			top -= st::setLittleSkip;
		} else {
			p.drawText(_left, top + st::linkFont->ascent, lang(oneState < 0 ? lng_profile_loading : lng_profile_no_media));
			top += _mediaLinks[OverviewPhotos]->height();
		}
	} else {
		if (!oneState) {
			p.drawText(_left, top + st::linkFont->ascent, lang(lng_profile_no_media));
		}
		top += _mediaLinks[OverviewPhotos]->height();
	}

	// participants
	if (_peerChat && (_peerChat->count > 0 || !_participants.isEmpty())) {
		QString sectionHeader = lang(_participants.isEmpty() ? lng_profile_loading : lng_profile_participants_section);
		p.setFont(st::profileHeaderFont->f);
		p.setPen(st::profileHeaderColor->p);
		p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, sectionHeader);
		top += st::profileHeaderSkip;

		int32 partfrom = top;
		if (!_participants.isEmpty()) {
			int32 cnt = 0, fullCnt = _participants.size();
			for (Participants::const_iterator i = _participants.cbegin(), e = _participants.cend(); i != e; ++i, ++cnt) {
				int32 top = partfrom + cnt * _pHeight;
				if (top + _pHeight <= r.top()) continue;
				if (top > r.bottom()) break;

				if (_selectedRow == cnt) {
					p.fillRect(_left - st::profileListPadding.width(), top, _width + 2 * st::profileListPadding.width(), _pHeight, st::profileHoverBG->b);
				}

				UserData *user = *i;
				p.drawPixmap(_left, top + st::profileListPadding.height(), user->photo->pix(st::profileListPhotoSize));
				ParticipantData *data = _participantsData[cnt];
				if (!data) {
					data = _participantsData[cnt] = new ParticipantData();
					data->name.setText(st::profileListNameFont, user->name, _textNameOptions);
					data->online = App::onlineText(user, l_time);
					data->cankick = (user != App::self()) && (_chatAdmin || (_peerChat->cankick.constFind(user) != _peerChat->cankick.cend()));
				}
				p.setPen(st::profileListNameColor->p);
				p.setFont(st::linkFont->f);
				data->name.drawElided(p, _left + st::profileListPhotoSize + st::profileListPadding.width(), top + st::profileListNameTop, _width - _kickWidth - st::profileListPadding.width() - st::profileListPhotoSize - st::profileListPadding.width());
				p.setFont(st::profileSubFont->f);
				p.setPen((App::onlineColorUse(user->onlineTill, l_time) ? st::profileOnlineColor : st::profileOfflineColor)->p);
				p.drawText(_left + st::profileListPhotoSize + st::profileListPadding.width(), top + st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, data->online);

				if (data->cankick) {
					bool over = (user == _kickOver && (!_kickDown || _kickDown == _kickOver));
					p.setFont((over ? st::linkOverFont : st::linkFont)->f);
					if (user == _kickOver && _kickOver == _kickDown) {
						p.setPen(st::btnDefLink.downColor->p);
					} else {
						p.setPen(st::btnDefLink.color->p);
					}
					p.drawText(_left + _width - _kickWidth, top + (_pHeight - st::linkFont->height) / 2 + st::linkFont->ascent, lang(lng_profile_kick));
				}
			}
			top += fullCnt * _pHeight;
		}
	}

	top += st::profileHeaderTop + st::profileHeaderFont->ascent - st::linkFont->ascent;
	top += _clearHistory.height();
}

void ProfileInner::mouseMoveEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();

	bool photoOver = QRect(_left, st::profilePadding.top(), st::setPhotoSize, st::setPhotoSize).contains(e->pos());
	if (photoOver != _photoOver) {
		_photoOver = photoOver;
		if (!_photoLink && _peerChat && !_peerChat->forbidden) {
			a_photo.start(_photoOver ? 1 : 0);
			anim::start(this);
		}
	}
	if (!_photoLink && (!_peerChat || _peerChat->forbidden)) {
		setCursor((_kickOver || _kickDown) ? style::cur_pointer : style::cur_default);
	} else {
		setCursor((_kickOver || _kickDown || _photoOver) ? style::cur_pointer : style::cur_default);
	}
}

void ProfileInner::updateSelected() {
	if (!isVisible()) return;

	QPoint lp = mapFromGlobal(_lastPos);

	int32 partfrom = _mediaAudios.y() + _mediaAudios.height() + st::profileHeaderSkip;
	int32 newSelected = (lp.x() >= _left - st::profileListPadding.width() && lp.x() < _left + _width + st::profileListPadding.width() && lp.y() >= partfrom) ? (lp.y() - partfrom) / _pHeight : -1;

	UserData *newKickOver = 0;
	if (newSelected >= 0 && newSelected < _participants.size()) {
		ParticipantData *data = _participantsData[newSelected];
		if (data && data->cankick) {
			int32 top = partfrom + newSelected * _pHeight + (_pHeight - st::linkFont->height) / 2;
			if ((lp.x() >= _left + _width - _kickWidth) && (lp.x() < _left + _width) && (lp.y() >= top) && (lp.y() < top + st::linkFont->height)) {
				newKickOver = _participants[newSelected];
			}
		}
	}
	if (_kickOver != newKickOver) {
		_kickOver = newKickOver;
		update();
	}
	if (_kickDown) return;

	if (newSelected != _selectedRow) {
		_selectedRow = newSelected;
		update();
	}
}

void ProfileInner::mousePressEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();
	if (e->button() == Qt::LeftButton) {
		if (_kickOver) {
			_kickDown = _kickOver;
			update();
		} else if (_selectedRow >= 0 && _selectedRow < _participants.size()) {
			App::main()->showPeerProfile(_participants[_selectedRow]);
		} else if (QRect(_left, st::profilePadding.top(), st::setPhotoSize, st::setPhotoSize).contains(e->pos())) {
			if (_photoLink) {
				_photoLink->onClick(e->button());
			} else if (_peerChat && !_peerChat->forbidden) {
				onUpdatePhoto();
			}
		}
	}
}

void ProfileInner::mouseReleaseEvent(QMouseEvent *e) {
	if (_kickDown && _kickDown == _kickOver) {
		_kickConfirm = _kickOver;
		ConfirmBox *box = new ConfirmBox(lng_profile_sure_kick(lt_user, _kickOver->firstName));
		connect(box, SIGNAL(confirmed()), this, SLOT(onKickConfirm()));
		App::wnd()->showLayer(box);
	}
	_kickDown = 0;
	setCursor(_kickOver ? style::cur_pointer : style::cur_default);
	update();
}

void ProfileInner::onKickConfirm() {
	App::main()->kickParticipant(_peerChat, _kickConfirm);
}

void ProfileInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		App::main()->showBackFromStack();
	}
}

void ProfileInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::enterEvent(e);
}

void ProfileInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::leaveEvent(e);
}

void ProfileInner::leaveToChildEvent(QEvent *e) {
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::leaveToChildEvent(e);
}

void ProfileInner::resizeEvent(QResizeEvent *e) {
	_width = qMin(width() - st::profilePadding.left() - st::profilePadding.right(), int(st::profileMaxWidth));
	_left = (width() - _width) / 2;

	int32 top = 0, btnWidth = (_width - st::profileButtonSkip) / 2;
	
	// profile
	top += st::profilePadding.top();
	if (_chatAdmin) {
		if (_peerChat->invitationUrl.isEmpty()) {
			_createInvitationLink.move(_left + st::profilePhotoSize + st::profilePhoneLeft, top + st::profilePhoneTop);
		} else {
			_createInvitationLink.move(_left + _width - _createInvitationLink.width(), top + st::profilePhoneTop);
		}
		if (!_invitationText.isEmpty()) {
			_invitationLink.setText(st::linkFont->m.elidedText(_invitationText, Qt::ElideRight, _width - st::profilePhotoSize - st::profilePhoneLeft));
		}
		_invitationLink.move(_left + st::profilePhotoSize + st::profilePhoneLeft, top + st::profilePhoneTop + st::linkFont->height * 1.2);
		_cancelPhoto.move(_left + _width - _cancelPhoto.width(), top + st::profilePhotoSize - st::linkFont->height);
	} else {
		_cancelPhoto.move(_left + _width - _cancelPhoto.width(), top + st::profilePhoneTop);
	}
	top += st::profilePhotoSize;

	top += st::profileButtonTop;
	_uploadPhoto.setGeometry(_left, top, btnWidth, _uploadPhoto.height());
	_sendMessage.setGeometry(_left, top, btnWidth, _sendMessage.height());
	_addParticipant.setGeometry(_left + _width - btnWidth, top, btnWidth, _addParticipant.height());
	_shareContact.setGeometry(_left + _width - btnWidth, top, btnWidth, _shareContact.height());
	top += _shareContact.height();

	// settings
	top += st::profileHeaderSkip;
	_enableNotifications.move(_left, top); top += _enableNotifications.height();

	// shared media
	top += st::profileHeaderSkip;

	_mediaShowAll.move(_left + _width - _mediaShowAll.width(), top);
	int wasCount = 0; // < 0 - loading, 0 - no media, > 0 - link shown
	for (int i = 0; i < OverviewCount; ++i) {
		if (_allMediaTypes) {
			int32 count = (_hist->_overviewCount[i] > 0) ? _hist->_overviewCount[i] : (_hist->_overviewCount[i] == 0 ? _hist->_overview[i].size() : -1);
			if (count > 0) {
				if (wasCount) top += _mediaLinks[i]->height() + st::setLittleSkip;
				wasCount = count;
			}
		}
		_mediaLinks[i]->move(_left, top);
	}
	top += _mediaLinks[OverviewPhotos]->height();

	// participants
	if (_peerChat && (_peerChat->count > 0 || !_participants.isEmpty())) {
		top += st::profileHeaderSkip;
		if (!_participants.isEmpty()) {
			int32 fullCnt = _participants.size();
			top += fullCnt * _pHeight;
		}
	}
	top += st::profileHeaderTop + st::profileHeaderFont->ascent - st::linkFont->ascent;
	_clearHistory.move(_left, top);
}

void ProfileInner::contextMenuEvent(QContextMenuEvent *e) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
	}
	if (!_phoneText.isEmpty() || (_peerUser && !_peerUser->username.isEmpty())) {
		QRect info(_left + st::profilePhotoSize + st::profilePhoneLeft, st::profilePadding.top(), _width - st::profilePhotoSize - st::profilePhoneLeft, st::profilePhotoSize);
		if (info.contains(mapFromGlobal(e->globalPos()))) {
			_menu = new ContextMenu(this);
			if (!_phoneText.isEmpty()) {
				_menu->addAction(lang(lng_profile_copy_phone), this, SLOT(onCopyPhone()))->setEnabled(true);
			}
			if (_peerUser && !_peerUser->username.isEmpty()) {
				_menu->addAction(lang(lng_context_copy_mention), this, SLOT(onCopyUsername()))->setEnabled(true);
			}
			_menu->deleteOnHide();
			connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
			_menu->popup(e->globalPos());
			e->accept();
		}
	}
}

void ProfileInner::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
}

void ProfileInner::onCopyPhone() {
	QApplication::clipboard()->setText(_phoneText);
}

void ProfileInner::onCopyUsername() {
	QApplication::clipboard()->setText('@' + _peerUser->username);
}

bool ProfileInner::animStep(float64 ms) {
	float64 dt = ms / st::setPhotoDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_photo.finish();
	} else {
		a_photo.update(dt, anim::linear);
	}
	update(_left, st::profilePadding.top(), st::setPhotoSize, st::setPhotoSize);
	return res;
}

PeerData *ProfileInner::peer() const {
	return _peer;
}

bool ProfileInner::allMediaShown() const {
	return _allMediaTypes;
}

ProfileInner::~ProfileInner() {
	for (ParticipantsData::iterator i = _participantsData.begin(), e = _participantsData.end(); i != e; ++i) {
		delete *i;
	}
	_participantsData.clear();
}
	
void ProfileInner::openContextImage() {
}

void ProfileInner::deleteContextImage() {
}

void ProfileInner::updateNotifySettings() {
	_enableNotifications.setChecked(_peer->notify == EmptyNotifySettings || _peer->notify == UnknownNotifySettings || _peer->notify->mute < unixtime());
}

void ProfileInner::mediaOverviewUpdated(PeerData *peer) {
	if (peer == _peer) {
		resizeEvent(0);
		showAll();
		update();
	}
}

void ProfileInner::showAll() {
	if (_peerChat) {
		_sendMessage.hide();
		_shareContact.hide();
		if (_peerChat->forbidden) {
			_uploadPhoto.hide();
			_cancelPhoto.hide();
			_addParticipant.hide();
			_createInvitationLink.hide();
			_invitationLink.hide();
		} else {
			if (App::app()->isPhotoUpdating(_peer->id)) {
				_uploadPhoto.hide();
				_cancelPhoto.show();
			} else {
				_uploadPhoto.show();
				_cancelPhoto.hide();
			}
			if (_chatAdmin) {
				_createInvitationLink.show();
				if (_peerChat->invitationUrl.isEmpty()) {
					_invitationLink.hide();
				} else {
					_invitationLink.show();
				}
			} else {
				_createInvitationLink.hide();
				_invitationLink.hide();
			}
			if (_peerChat->count < cMaxGroupCount()) {
				_addParticipant.show();
			} else {
				_addParticipant.hide();
			}
		}
		_enableNotifications.show();
		_clearHistory.hide();
	} else {
		_uploadPhoto.hide();
		_cancelPhoto.hide();
		_addParticipant.hide();
		_createInvitationLink.hide();
		_invitationLink.hide();
		_sendMessage.show();
		if (_peerUser->phone.isEmpty()) {
			_shareContact.hide();
		} else {
			_shareContact.show();
		}
		_enableNotifications.show();
		_clearHistory.show();
	}
	updateNotifySettings();

	// shared media
	bool first = false, wasCount = false, manyCounts = false;
	for (int i = 0; i < OverviewCount; ++i) {
		int32 count = (_hist->_overviewCount[i] > 0) ? _hist->_overviewCount[i] : (_hist->_overviewCount[i] == 0 ? _hist->_overview[i].size() : -1);
		if (count > 0) {
			if (wasCount) {
				manyCounts = true;
			} else {
				wasCount = true;
			}
		}
		if (!first || _allMediaTypes) {
			if (count > 0 || count < 0) {
				first = true;
			} else if (!_allMediaTypes) {
				_mediaLinks[i]->hide();
				continue;
			}
			if (count > 0) {
				_mediaLinks[i]->setText(overviewLinkText(i, count));
				_mediaLinks[i]->show();
			} else {
				_mediaLinks[i]->hide();
			}
		} else {
			_mediaLinks[i]->hide();
		}
	}
	if (_allMediaTypes || !manyCounts) {
		_mediaShowAll.hide();
	} else {
		_mediaShowAll.show();
	}

	// participants
	reorderParticipants();
	int32 h;
	if (_peerUser) {
		h = _clearHistory.y() + _clearHistory.height() + st::profileHeaderSkip;
	} else {
		h = _mediaAudios.y() + _mediaAudios.height() + st::profileHeaderSkip;
		if (!_participants.isEmpty()) {
			h += st::profileHeaderSkip + _participants.size() * _pHeight;
		} else if (_peerChat->count > 0) {
			h += st::profileHeaderSkip;
		}
	}
	resize(width(), h);
}

void ProfileInner::updateInvitationLink() {
	if (!_peerChat) return;
	if (_peerChat->invitationUrl.isEmpty()) {
		_createInvitationLink.setText(lang(lng_group_invite_create));
	} else {
		_createInvitationLink.setText(lang(lng_group_invite_create_new));
		_invitationText = _peerChat->invitationUrl;
		if (_invitationText.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) {
			_invitationText = _invitationText.mid(7);
		} else if (_invitationText.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
			_invitationText = _invitationText.mid(8);
		}
	}
}

QString ProfileInner::overviewLinkText(int32 type, int32 count) {
	switch (type) {
	case OverviewPhotos: return lng_profile_photos(lt_count, count);
	case OverviewVideos: return lng_profile_videos(lt_count, count);
	case OverviewDocuments: return lng_profile_files(lt_count, count);
	case OverviewAudios: return lng_profile_audios(lt_count, count);
	}
	return QString();
}

ProfileWidget::ProfileWidget(QWidget *parent, const PeerData *peer) : QWidget(parent)
    , _scroll(this, st::setScroll)
    , _inner(this, &_scroll, peer)
    , _showing(false)
{
	_scroll.setWidget(&_inner);
	_scroll.move(0, 0);
	_inner.move(0, 0);
	_scroll.show();
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSelected()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
}

void ProfileWidget::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

void ProfileWidget::resizeEvent(QResizeEvent *e) {
	_scroll.resize(size());
	_inner.resize(width(), _inner.height());
}

void ProfileWidget::mousePressEvent(QMouseEvent *e) {
}

void ProfileWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (animating() && _showing) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
	} else {
		p.fillRect(e->rect(), st::white->b);
	}
}

void ProfileWidget::dragEnterEvent(QDragEnterEvent *e) {
}

void ProfileWidget::dropEvent(QDropEvent *e) {
}

void ProfileWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (animating() && _showing) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimTopBarCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animTopBarCache);
	} else {
		p.setOpacity(st::topBarBackAlpha + (1 - st::topBarBackAlpha) * over);
		p.drawPixmap(QPoint(st::topBarBackPadding.left(), (st::topBarHeight - st::topBarBackImg.pxHeight()) / 2), App::sprite(), st::topBarBackImg);
		p.setFont(st::topBarBackFont->f);
		p.setPen(st::topBarBackColor->p);
		p.drawText(st::topBarBackPadding.left() + st::topBarBackImg.pxWidth() + st::topBarBackPadding.right(), (st::topBarHeight - st::topBarBackFont->height) / 2 + st::topBarBackFont->ascent, lang(peer()->chat ? lng_profile_group_info : lng_profile_info));
	}
}

void ProfileWidget::topBarClick() {
	App::main()->showBackFromStack();
}

PeerData *ProfileWidget::peer() const {
	return _inner.peer();
}

int32 ProfileWidget::lastScrollTop() const {
	return _scroll.scrollTop();
}

bool ProfileWidget::allMediaShown() const {
	return _inner.allMediaShown();
}

void ProfileWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back, int32 lastScrollTop, bool allMediaShown) {
	stopGif();
	_bgAnimCache = bgAnimCache;
	_bgAnimTopBarCache = bgAnimTopBarCache;
	if (allMediaShown) _inner.onMediaShowAll();
	if (lastScrollTop >= 0) _scroll.scrollToY(lastScrollTop);
	_animCache = myGrab(this, rect());
	App::main()->topBar()->stopAnim();
	_animTopBarCache = myGrab(App::main()->topBar(), QRect(0, 0, width(), st::topBarHeight));
	App::main()->topBar()->startAnim();
	_scroll.hide();
	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);
	anim::start(this);
	_showing = true;
	show();
	_inner.setFocus();
	App::main()->topBar()->update();
}

bool ProfileWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		res = _showing = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();
		_bgAnimCache = _animCache = _animTopBarCache = _bgAnimTopBarCache = QPixmap();
		App::main()->topBar()->stopAnim();
		_scroll.show();
		activate();
	} else {
		a_bgCoord.update(dt1, st::introHideFunc);
		a_bgAlpha.update(dt1, st::introAlphaHideFunc);
		a_coord.update(dt2, st::introShowFunc);
		a_alpha.update(dt2, st::introAlphaShowFunc);
	}
	update();
	App::main()->topBar()->update();
	return res;
}

void ProfileWidget::updateOnlineDisplay() {
	_inner.updateOnlineDisplay();
	updateOnlineDisplayTimer();
}

void ProfileWidget::updateOnlineDisplayTimer() {
	_inner.updateOnlineDisplayTimer();
}

void ProfileWidget::updateNotifySettings() {
	_inner.updateNotifySettings();
}

void ProfileWidget::mediaOverviewUpdated(PeerData *peer) {
	_inner.mediaOverviewUpdated(peer);
}

ProfileWidget::~ProfileWidget() {
}

void ProfileWidget::activate() {
	_inner.setFocus();
}
