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

#include "lang.h"
#include "window.h"
#include "mainwidget.h"
#include "profilewidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "boxes/photocropbox.h"
#include "application.h"
#include "boxes/addparticipantbox.h"
#include "gui/filedialog.h"

ProfileInner::ProfileInner(ProfileWidget *profile, ScrollArea *scroll, const PeerData *peer) : TWidget(0),
	_profile(profile), _scroll(scroll), _peer(App::peer(peer->id)),
	_peerUser(_peer->chat ? 0 : _peer->asUser()), _peerChat(_peer->chat ? _peer->asChat() : 0),
	_chatAdmin(_peerChat ? (_peerChat->admin == MTP::authedId()) : false),

	// profile
	_nameCache(peer->name),
	_uploadPhoto(this, lang(lng_profile_set_group_photo), st::btnShareContact),
	_addParticipant(this, lang(lng_profile_add_participant), st::btnShareContact),
	_sendMessage(this, lang(lng_profile_send_message), st::btnShareContact),
	_shareContact(this, lang(lng_profile_share_contact), st::btnShareContact),
	_cancelPhoto(this, lang(lng_cancel)),

	a_photo(0),
	_photoOver(false),

	// settings
	_enableNotifications(this, lang(lng_profile_enable_notifications)),
	_clearHistory(this, lang(lng_profile_clear_history)),

	// participants
	_pHeight(st::profileListPhotoSize + st::profileListPadding.height() * 2),
	_kickWidth(st::linkFont->m.width(lang(lng_profile_kick))),
	_selectedRow(-1), _lastPreload(0), _contactId(0),
	_kickOver(0), _kickDown(0), _kickConfirm(0),
	
	_loadingId(0) {

	if (_peerUser) {
		_phoneText = _peerUser->phone.isEmpty() ? QString() : App::formatPhone(_peerUser->phone);
		_loadingId = MTP::send(MTPusers_GetFullUser(_peerUser->inputUser), rpcDone(&ProfileInner::gotFullUser));
	} else if (_peerChat->photoId) {
		PhotoData *ph = App::photo(_peerChat->photoId);
		if (ph->date) {
			_photoLink = TextLinkPtr(new PhotoLink(ph));
		}
	} else {
		_loadingId = MTP::send(MTPmessages_GetFullChat(App::peerToMTP(_peerChat->id).c_peerChat().vchat_id), rpcDone(&ProfileInner::gotFullChat));
	}

	// profile
	_nameText.setText(st::profileNameFont, _nameCache, _textNameOptions);

	connect(&_uploadPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhoto()));
	connect(&_addParticipant, SIGNAL(clicked()), this, SLOT(onAddParticipant()));
	connect(&_sendMessage, SIGNAL(clicked()), this, SLOT(onSendMessage()));
	connect(&_shareContact, SIGNAL(clicked()), this, SLOT(onShareContact()));
	connect(&_cancelPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhotoCancel()));

	connect(App::app(), SIGNAL(peerPhotoDone(PeerId)), this, SLOT(onPhotoUpdateDone(PeerId)));
	connect(App::app(), SIGNAL(peerPhotoFail(PeerId)), this, SLOT(onPhotoUpdateFail(PeerId)));

	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(peerUpdated(PeerData *)));

	// settings
	connect(&_enableNotifications, SIGNAL(clicked()), this, SLOT(onEnableNotifications()));
	connect(&_clearHistory, SIGNAL(clicked()), this, SLOT(onClearHistory()));

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

	int32 partfrom = (_enableNotifications.y() + _enableNotifications.height()) + st::profileHeaderSkip;
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
	ConfirmBox *box = new ConfirmBox(lang(lng_sure_delete_history).replace(qsl("{contact}"), _peer->name));
	connect(box, SIGNAL(confirmed()), this, SLOT(onClearHistorySure()));
	App::wnd()->showLayer(box);
}

void ProfileInner::onClearHistorySure() {
	App::main()->showPeer(0, true);
	App::wnd()->hideLayer();
	App::main()->clearHistory(_peer);
}

void ProfileInner::onAddParticipant() {
	AddParticipantBox *box = new AddParticipantBox(_peerChat);
	App::wnd()->showLayer(box);
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

void ProfileInner::gotFullUser(const MTPUserFull &user) {
	_loadingId = 0;
	const MTPDuserFull &d(user.c_userFull());
	App::feedPhoto(d.vprofile_photo);
	App::feedUsers(MTP_vector<MTPUser>(QVector<MTPUser>(1, d.vuser)));
	PhotoData *userPhoto = _peerUser->photoId ? App::photo(_peerUser->photoId) : 0;
	if (userPhoto && userPhoto->date) {
		_photoLink = TextLinkPtr(new PhotoLink(userPhoto));
	} else {
		_photoLink = TextLinkPtr();
	}
	App::main()->gotNotifySetting(MTP_inputNotifyPeer(_peer->input), d.vnotify_settings);
	App::feedUserLink(MTP_int(_peerUser->id), d.vlink.c_contacts_link().vmy_link, d.vlink.c_contacts_link().vforeign_link);
}

void ProfileInner::gotFullChat(const MTPmessages_ChatFull &res) {
	_loadingId = 0;
	const MTPDmessages_chatFull &d(res.c_messages_chatFull());
	PeerId peerId = App::peerFromChat(d.vfull_chat.c_chatFull().vid);
	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);
	App::feedParticipants(d.vfull_chat.c_chatFull().vparticipants);
	App::main()->gotNotifySetting(MTP_inputNotifyPeer(_peer->input), d.vfull_chat.c_chatFull().vnotify_settings);
	PhotoData *photo = App::feedPhoto(d.vfull_chat.c_chatFull().vchat_photo);
	if (photo) {
		ChatData *chat = App::peer(peerId)->asChat();
		if (chat) {
			chat->photoId = photo->id;
			photo->chat = chat;
		}
	}
	emit App::main()->peerUpdated(_peer);
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
		_photoLink = (photo && photo->date) ? TextLinkPtr(new PhotoLink(photo)) : TextLinkPtr();
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
		for (ChatData::Participants::const_iterator i = _peerChat->participants.cbegin(), e = _peerChat->participants.cend(); i != e; ++i) {
			UserData *user = i.key();
			int32 until = user->onlineTill;
			Participants::iterator before = _participants.begin();
			if (user != self) {
				if (before != _participants.end() && (*before) == self) {
					++before;
				}
				while (before != _participants.end() && (*before)->onlineTill >= until) {
					++before;
				}
			}
			_participants.insert(before, user);
			if (until > t) {
				++onlineCount;
			}
		}
		if (_peerChat->count > 0 && _participants.isEmpty() && !_loadingId) {
			_loadingId = MTP::send(MTPmessages_GetFullChat(App::peerToMTP(_peerChat->id).c_peerChat().vchat_id), rpcDone(&ProfileInner::gotFullChat));
			if (_onlineText.isEmpty()) _onlineText = lang(lng_chat_members).arg(_peerChat->count);
		} else if (onlineCount) {
			_onlineText = lang(lng_chat_members_online).arg(_participants.size()).arg(onlineCount);
		} else {
			_onlineText = lang(lng_chat_members).arg(_participants.size());
		}
		loadProfilePhotos(_lastPreload);
	} else {
		_participants.clear();
		if (_peerUser) {
			_onlineText = App::onlineText(_peerUser->onlineTill, t);
		} else {
			_onlineText = lang(lng_chat_no_members);
		}
	}
	if (was != _participants.size()) {
		resizeEvent(0);
	}
}

bool ProfileInner::event(QEvent *e) {
	if (e->type() == QEvent::MouseMove) {
		QMouseEvent *ev = dynamic_cast<QMouseEvent*>(e);
		if (ev) {
			_lastPos = ev->globalPos();
			updateSelected();
		}
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
	p.setPen((_peerUser && _peerUser->onlineTill >= l_time ? st::profileOnlineColor : st::profileOfflineColor)->p);
	p.drawText(_left + st::profilePhotoSize + st::profileStatusLeft, top + st::profileStatusTop + st::linkFont->ascent, _onlineText);
	if (!_cancelPhoto.isHidden()) {
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, _cancelPhoto.y() + st::linkFont->ascent, lang(lng_settings_uploading_photo));
	}

	if (!_errorText.isEmpty()) {
		p.setFont(st::setErrFont->f);
		p.setPen(st::setErrColor->p);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, top + st::profilePhoneTop + st::profilePhoneFont->ascent, _errorText);
	}
	if (!_phoneText.isEmpty()) {
		p.setPen(st::black->p);
		p.setFont(st::linkFont->f);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, top + st::profilePhoneTop + st::profilePhoneFont->ascent, _phoneText);
	}
	top += st::profilePhotoSize;
	top += st::profileButtonTop;

	if (_peerChat && _peerChat->forbidden) {
		int32 w = st::btnShareContact.font->m.width(lang(lng_chat_no_members));
		p.setFont(st::btnShareContact.font->f);
		p.setPen(st::profileOfflineColor->p);
		p.drawText(_left + (_width - w) / 2, top + st::btnShareContact.textTop + st::btnShareContact.font->ascent, lang(lng_chat_no_members));
	}
	top += _shareContact.height();

	// settings
	p.setFont(st::profileHeaderFont->f);
	p.setPen(st::profileHeaderColor->p);
	p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lang(lng_profile_settings_section));
	top += st::profileHeaderSkip;

	top += _enableNotifications.height();

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
					data->online = App::onlineText(user->onlineTill, l_time);
					data->cankick = (user != App::self()) && (_chatAdmin || (_peerChat->cankick.constFind(user) != _peerChat->cankick.cend()));
				}
				p.setPen(st::profileListNameColor->p);
				p.setFont(st::linkFont->f);
				data->name.drawElided(p, _left + st::profileListPhotoSize + st::profileListPadding.width(), top + st::profileListNameTop, _width - _kickWidth - st::profileListPadding.width() - st::profileListPhotoSize - st::profileListPadding.width());
				p.setFont(st::profileSubFont->f);
				p.setPen((user->onlineTill >= l_time ? st::profileOnlineColor : st::profileOfflineColor)->p);
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

	int32 partfrom = (_enableNotifications.y() + _enableNotifications.height()) + st::profileHeaderSkip;
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

void ProfileInner::mouseReleaseEvent(QMouseEvent *e) {
	if (_kickDown && _kickDown == _kickOver) {
		_kickConfirm = _kickOver;
		ConfirmBox *box = new ConfirmBox(lang(lng_profile_sure_kick).replace(qsl("{user}"), _kickOver->firstName));
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
	if (e->key() == Qt::Key_Escape) {
		App::main()->showPeer(0, true);
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
	_cancelPhoto.move(_left + _width - _cancelPhoto.width(), top + st::profilePhoneTop);
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

bool ProfileInner::getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const {
	if ((_peerUser && photo->id == _peerUser->photoId) || (_peerChat && photo->id == _peerChat->photoId)) {
		x = _left;
		y = st::profilePadding.top();
		w = st::setPhotoSize;
		return true;
	}
	return false;
}

PeerData *ProfileInner::peer() const {
	return _peer;
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

void ProfileInner::showAll() {
	if (_peerChat) {
		_sendMessage.hide();
		_shareContact.hide();
		if (_peerChat->forbidden) {
			_uploadPhoto.hide();
			_cancelPhoto.hide();
			_addParticipant.hide();
		} else {
			if (App::app()->isPhotoUpdating(_peer->id)) {
				_uploadPhoto.hide();
				_cancelPhoto.show();
			} else {
				_uploadPhoto.show();
				_cancelPhoto.hide();
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

	// participants
	reorderParticipants();
	int32 h;
	if (_peerUser) {
		h = _clearHistory.y() + _clearHistory.height() + st::profileHeaderSkip;
	} else {
		h = _enableNotifications.y() + _enableNotifications.height() + st::profileHeaderSkip;
		if (!_participants.isEmpty()) {
			h += st::profileHeaderSkip + _participants.size() * _pHeight;
		} else if (_peerChat->count > 0) {
			h += st::profileHeaderSkip;
		}
	}
	resize(width(), h);
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

bool ProfileWidget::getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const {
	if (_inner.getPhotoCoords(photo, x, y, w)) {
		x += _inner.x();
		y += _inner.y();
		return true;
	}
	return false;
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
		p.drawText(st::topBarBackPadding.left() + st::topBarBackImg.pxWidth() + st::topBarBackPadding.right(), (st::topBarHeight - st::titleFont->height) / 2 + st::titleFont->ascent, lang(peer()->chat ? lng_profile_group_info : lng_profile_info));
	}
}

void ProfileWidget::topBarClick() {
	App::main()->showPeerBack();
}

PeerData *ProfileWidget::peer() const {
	return _inner.peer();
}

void ProfileWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back) {
	_bgAnimCache = bgAnimCache;
	_bgAnimTopBarCache = bgAnimTopBarCache;
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

ProfileWidget::~ProfileWidget() {
}

void ProfileWidget::activate() {
	_inner.setFocus();
}
