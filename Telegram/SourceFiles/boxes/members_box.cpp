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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "boxes/members_box.h"

#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "lang.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "boxes/contactsbox.h"
#include "boxes/confirmbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "observer_peer.h"

MembersBox::MembersBox(ChannelData *channel, MembersFilter filter) : ItemListBox(st::boxScroll)
, _inner(this, channel, filter) {
	ItemListBox::init(_inner);

	if (channel->amCreator() && (channel->membersCount() < (channel->isMegagroup() ? Global::MegagroupSizeMax() : Global::ChatSizeMax()) || (!channel->isMegagroup() && !channel->isPublic()) || filter == MembersFilter::Admins)) {
		_add.create(this, st::contactsAdd);
		_add->setClickedCallback([this] { onAdd(); });
	}

	connect(scrollArea(), SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_inner, SIGNAL(mustScrollTo(int, int)), scrollArea(), SLOT(scrollToY(int, int)));

	connect(&_loadTimer, SIGNAL(timeout()), _inner, SLOT(load()));

	prepare();
}

void MembersBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(scrollArea()->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(scrollArea()->height(), -1);
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void MembersBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	QString title(lang(_inner->filter() == MembersFilter::Recent ? lng_channel_members : lng_channel_admins));
	paintTitle(p, title);
}

void MembersBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	_inner->resize(width(), _inner->height());

	if (_add) {
		_add->moveToRight(st::contactsAddPosition.x(), height() - st::contactsAddPosition.y() - _add->height());
	}
}

void MembersBox::onScroll() {
	_inner->loadProfilePhotos(scrollArea()->scrollTop());
}

void MembersBox::onAdd() {
	if (_inner->filter() == MembersFilter::Recent && _inner->channel()->membersCount() >= (_inner->channel()->isMegagroup() ? Global::MegagroupSizeMax() : Global::ChatSizeMax())) {
		Ui::showLayer(new MaxInviteBox(_inner->channel()->inviteLink()), KeepOtherLayers);
		return;
	}
	ContactsBox *box = new ContactsBox(_inner->channel(), _inner->filter(), _inner->already());
	if (_inner->filter() == MembersFilter::Recent) {
		Ui::showLayer(box);
	} else {
		_addBox = box;
		connect(_addBox, SIGNAL(adminAdded()), this, SLOT(onAdminAdded()));
		Ui::showLayer(_addBox, KeepOtherLayers);
	}
}

void MembersBox::onAdminAdded() {
	if (!_addBox) return;
	_addBox->onClose();
	_addBox = 0;
	_loadTimer.start(ReloadChannelMembersTimeout);
}

MembersBox::Inner::Inner(QWidget *parent, ChannelData *channel, MembersFilter filter) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _channel(channel)
, _filter(filter)
, _kickText(lang(lng_profile_kick))
, _time(0)
, _kickWidth(st::normalFont->width(_kickText))
, _sel(-1)
, _kickSel(-1)
, _kickDown(-1)
, _mouseSel(false)
, _kickConfirm(0)
, _kickRequestId(0)
, _kickBox(0)
, _loading(true)
, _loadingRequestId(0)
, _aboutWidth(st::boxWideWidth - st::contactsPadding.left() - st::contactsPadding.right())
, _about(_aboutWidth)
, _aboutHeight(0) {
	subscribe(FileDownload::ImageLoaded(), [this] { update(); });

	connect(App::main(), SIGNAL(peerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(peerUpdated(PeerData*)));

	refresh();

	load();
}

void MembersBox::Inner::load() {
	if (!_loadingRequestId) {
		_loadingRequestId = MTP::send(MTPchannels_GetParticipants(_channel->inputChannel, (_filter == MembersFilter::Recent) ? MTP_channelParticipantsRecent() : MTP_channelParticipantsAdmins(), MTP_int(0), MTP_int(Global::ChatSizeMax())), rpcDone(&Inner::membersReceived), rpcFail(&Inner::membersFailed));
	}
}

void MembersBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	_time = unixtime();
	p.fillRect(r, st::contactsBg);

	int32 yFrom = r.y() - st::membersPadding.top(), yTo = r.y() + r.height() - st::membersPadding.top();
	p.translate(0, st::membersPadding.top());
	if (_rows.isEmpty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	} else {
		int32 from = floorclamp(yFrom, _rowHeight, 0, _rows.size());
		int32 to = ceilclamp(yTo, _rowHeight, 0, _rows.size());
		p.translate(0, from * _rowHeight);
		for (; from < to; ++from) {
			bool sel = (from == _sel);
			bool kickSel = (from == _kickSel && (_kickDown < 0 || from == _kickDown));
			bool kickDown = kickSel && (from == _kickDown);
			paintDialog(p, _rows[from], data(from), sel, kickSel, kickDown);
			p.translate(0, _rowHeight);
		}
		if (to == _rows.size() && _filter == MembersFilter::Recent && (_rows.size() < _channel->membersCount() || _rows.size() >= Global::ChatSizeMax())) {
			p.setPen(st::membersAboutLimitFg);
			_about.draw(p, st::contactsPadding.left(), st::membersAboutLimitPadding.top(), _aboutWidth, style::al_center);
		}
	}
}

void MembersBox::Inner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void MembersBox::Inner::leaveEvent(QEvent *e) {
	_mouseSel = false;
	setMouseTracking(false);
	if (_sel >= 0) {
		clearSel();
	}
}

void MembersBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
}

void MembersBox::Inner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (e->button() == Qt::LeftButton && _kickSel < 0) {
		chooseParticipant();
	}
	_kickDown = _kickSel;
	update();
}

void MembersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (_kickDown >= 0 && _kickDown == _kickSel && !_kickRequestId) {
		_kickConfirm = _rows.at(_kickSel);
		if (_kickBox) _kickBox->deleteLater();
		_kickBox = new ConfirmBox((_filter == MembersFilter::Recent ? (_channel->isMegagroup() ? lng_profile_sure_kick : lng_profile_sure_kick_channel) : lng_profile_sure_kick_admin)(lt_user, _kickConfirm->firstName));
		connect(_kickBox, SIGNAL(confirmed()), this, SLOT(onKickConfirm()));
		connect(_kickBox, SIGNAL(destroyed(QObject*)), this, SLOT(onKickBoxDestroyed(QObject*)));
		Ui::showLayer(_kickBox, KeepOtherLayers);
	}
	_kickDown = -1;
}

void MembersBox::Inner::onKickBoxDestroyed(QObject *obj) {
	if (_kickBox == obj) {
		_kickBox = 0;
	}
}

void MembersBox::Inner::onKickConfirm() {
	if (_filter == MembersFilter::Recent) {
		_kickRequestId = MTP::send(MTPchannels_KickFromChannel(_channel->inputChannel, _kickConfirm->inputUser, MTP_bool(true)), rpcDone(&Inner::kickDone), rpcFail(&Inner::kickFail));
	} else {
		_kickRequestId = MTP::send(MTPchannels_EditAdmin(_channel->inputChannel, _kickConfirm->inputUser, MTP_channelRoleEmpty()), rpcDone(&Inner::kickAdminDone), rpcFail(&Inner::kickFail));
	}
}

void MembersBox::Inner::paintDialog(Painter &p, PeerData *peer, MemberData *data, bool sel, bool kickSel, bool kickDown) {
	UserData *user = peer->asUser();

	p.fillRect(0, 0, width(), _rowHeight, sel ? st::contactsBgOver : st::contactsBg);
	peer->paintUserpicLeft(p, st::contactsPhotoSize, st::contactsPadding.left(), st::contactsPadding.top(), width());

	p.setPen(st::contactsNameFg);

	int32 namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int32 namew = width() - namex - st::contactsPadding.right() - (data->canKick ? (_kickWidth + st::contactsCheckPosition.x() * 2) : 0);
	if (peer->isVerified()) {
		auto icon = &st::dialogsVerifiedIcon;
		namew -= icon->width();
		icon->paint(p, namex + qMin(data->name.maxWidth(), namew), st::contactsPadding.top() + st::contactsNameTop, width());
	}
	data->name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	if (data->canKick) {
		p.setFont((kickSel ? st::linkOverFont : st::linkFont)->f);
		p.setPen(kickDown ? st::defaultLinkButton.downColor : st::defaultLinkButton.color);
		p.drawTextRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, width(), _kickText, _kickWidth);
	}

	p.setFont(st::contactsStatusFont->f);
	p.setPen(data->onlineColor ? st::contactsStatusFgOnline : (sel ? st::contactsStatusFgOver : st::contactsStatusFg));
	p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), data->online);
}

void MembersBox::Inner::selectSkip(int32 dir) {
	_time = unixtime();
	_mouseSel = false;

	int cur = -1;
	if (_sel >= 0) {
		cur = _sel;
	}
	cur += dir;
	if (cur <= 0) {
		_sel = _rows.isEmpty() ? -1 : 0;
	} else if (cur >= _rows.size()) {
		_sel = -1;
	} else {
		_sel = cur;
	}
	if (dir > 0) {
		if (_sel < 0 || _sel >= _rows.size()) {
			_sel = -1;
		}
	} else {
		if (!_rows.isEmpty()) {
			if (_sel < 0) _sel = _rows.size() - 1;
		}
	}
	if (_sel >= 0) {
		emit mustScrollTo(_sel * _rowHeight, (_sel + 1) * _rowHeight);
	}

	update();
}

void MembersBox::Inner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

void MembersBox::Inner::loadProfilePhotos(int32 yFrom) {
	if (!parentWidget()) return;
	int32 yTo = yFrom + parentWidget()->height() * 5;
	MTP::clearLoaderPriorities();

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	if (!_rows.isEmpty()) {
		int32 from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _rows.size()) {
			int32 to = (yTo / _rowHeight) + 1;
			if (to > _rows.size()) to = _rows.size();

			for (; from < to; ++from) {
				_rows[from]->loadUserpic();
			}
		}
	}
}

void MembersBox::Inner::chooseParticipant() {
	if (_sel < 0 || _sel >= _rows.size()) return;
	if (PeerData *peer = _rows[_sel]) {
		Ui::hideLayer();
		Ui::showPeerProfile(peer);
	}
}

void MembersBox::Inner::refresh() {
	if (_rows.isEmpty()) {
		resize(width(), st::membersPadding.top() + st::noContactsHeight + st::membersPadding.bottom());
		_aboutHeight = 0;
	} else {
		_about.setText(st::boxTextFont, lng_channel_only_last_shown(lt_count, _rows.size()));
		_aboutHeight = st::membersAboutLimitPadding.top() + _about.countHeight(_aboutWidth) + st::membersAboutLimitPadding.bottom();
		if (_filter != MembersFilter::Recent || (_rows.size() >= _channel->membersCount() && _rows.size() < Global::ChatSizeMax())) {
			_aboutHeight = 0;
		}
		resize(width(), st::membersPadding.top() + _rows.size() * _rowHeight + st::membersPadding.bottom() + _aboutHeight);
	}
	update();
}

ChannelData *MembersBox::Inner::channel() const {
	return _channel;
}

MembersFilter MembersBox::Inner::filter() const {
	return _filter;
}

MembersAlreadyIn MembersBox::Inner::already() const {
	MembersAlreadyIn result;
	for_const (auto peer, _rows) {
		if (peer->isUser()) {
			result.insert(peer->asUser());
		}
	}
	return result;
}

void MembersBox::Inner::clearSel() {
	updateSelectedRow();
	_sel = _kickSel = _kickDown = -1;
	_lastMousePos = QCursor::pos();
	updateSel();
}

MembersBox::Inner::MemberData *MembersBox::Inner::data(int32 index) {
	if (MemberData *result = _datas.at(index)) {
		return result;
	}
	MemberData *result = _datas[index] = new MemberData();
	result->name.setText(st::contactsNameFont, _rows[index]->name, _textNameOptions);
	int32 t = unixtime();
	result->online = App::onlineText(_rows[index], t);// lng_mediaview_date_time(lt_date, _dates[index].date().toString(qsl("dd.MM.yy")), lt_time, _dates[index].time().toString(cTimeFormat()));
	result->onlineColor = App::onlineColorUse(_rows[index], t);
	if (_filter == MembersFilter::Recent) {
		result->canKick = (_channel->amCreator() || _channel->amEditor() || _channel->amModerator()) ? (_roles[index] == MemberRole::None) : false;
	} else if (_filter == MembersFilter::Admins) {
		result->canKick = _channel->amCreator() ? (_roles[index] == MemberRole::Editor || _roles[index] == MemberRole::Moderator) : false;
	} else {
		result->canKick = false;
	}
	return result;
}

void MembersBox::Inner::clear() {
	for (int32 i = 0, l = _datas.size(); i < l; ++i) {
		delete _datas.at(i);
	}
	_datas.clear();
	_rows.clear();
	_dates.clear();
	_roles.clear();
	if (_kickBox) _kickBox->deleteLater();
	clearSel();
}

MembersBox::Inner::~Inner() {
	clear();
}

void MembersBox::Inner::updateSel() {
	if (!_mouseSel) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	p.setY(p.y() - st::membersPadding.top());
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	int32 newSel = (in && p.y() >= 0 && p.y() < _rows.size() * _rowHeight) ? (p.y() / _rowHeight) : -1;
	int32 newKickSel = newSel;
	if (newSel >= 0 && (!data(newSel)->canKick || !QRect(width() - _kickWidth - st::contactsPadding.right() - st::contactsCheckPosition.x(), newSel * _rowHeight + st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, _kickWidth, st::normalFont->height).contains(p))) {
		newKickSel = -1;
	}
	if (newSel != _sel || newKickSel != _kickSel) {
		updateSelectedRow();
		_sel = newSel;
		_kickSel = newKickSel;
		updateSelectedRow();
		setCursor(_kickSel >= 0 ? style::cur_pointer : style::cur_default);
	}
}

void MembersBox::Inner::peerUpdated(PeerData *peer) {
	update();
}

void MembersBox::Inner::updateSelectedRow() {
	if (_sel >= 0) {
		update(0, st::membersPadding.top() + _sel * _rowHeight, width(), _rowHeight);
	}
}

void MembersBox::Inner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	for (int32 i = 0, l = _rows.size(); i < l; ++i) {
		if (_rows.at(i) == peer) {
			if (_datas.at(i)) {
				_datas.at(i)->name.setText(st::contactsNameFont, peer->name, _textNameOptions);
				update(0, st::membersPadding.top() + i * _rowHeight, width(), _rowHeight);
			} else {
				break;
			}
		}
	}
}

void MembersBox::Inner::membersReceived(const MTPchannels_ChannelParticipants &result, mtpRequestId req) {
	clear();
	_loadingRequestId = 0;

	if (result.type() == mtpc_channels_channelParticipants) {
		const auto &d(result.c_channels_channelParticipants());
		const auto &v(d.vparticipants.c_vector().v);
		_rows.reserve(v.size());
		_datas.reserve(v.size());
		_dates.reserve(v.size());
		_roles.reserve(v.size());

		if (_filter == MembersFilter::Recent && _channel->membersCount() < d.vcount.v) {
			_channel->setMembersCount(d.vcount.v);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		} else if (_filter == MembersFilter::Admins && _channel->adminsCount() < d.vcount.v) {
			_channel->setAdminsCount(d.vcount.v);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		}
		App::feedUsers(d.vusers);

		for (QVector<MTPChannelParticipant>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			int32 userId = 0, addedTime = 0;
			MemberRole role = MemberRole::None;
			switch (i->type()) {
			case mtpc_channelParticipant:
				userId = i->c_channelParticipant().vuser_id.v;
				addedTime = i->c_channelParticipant().vdate.v;
				break;
			case mtpc_channelParticipantSelf:
				role = MemberRole::Self;
				userId = i->c_channelParticipantSelf().vuser_id.v;
				addedTime = i->c_channelParticipantSelf().vdate.v;
				break;
			case mtpc_channelParticipantModerator:
				role = MemberRole::Moderator;
				userId = i->c_channelParticipantModerator().vuser_id.v;
				addedTime = i->c_channelParticipantModerator().vdate.v;
				break;
			case mtpc_channelParticipantEditor:
				role = MemberRole::Editor;
				userId = i->c_channelParticipantEditor().vuser_id.v;
				addedTime = i->c_channelParticipantEditor().vdate.v;
				break;
			case mtpc_channelParticipantKicked:
				userId = i->c_channelParticipantKicked().vuser_id.v;
				addedTime = i->c_channelParticipantKicked().vdate.v;
				role = MemberRole::Kicked;
				break;
			case mtpc_channelParticipantCreator:
				userId = i->c_channelParticipantCreator().vuser_id.v;
				addedTime = _channel->date;
				role = MemberRole::Creator;
				break;
			}
			if (UserData *user = App::userLoaded(userId)) {
				_rows.push_back(user);
				_dates.push_back(date(addedTime));
				_roles.push_back(role);
				_datas.push_back(0);
			}
		}

		// update admins if we got all of them
		if (_filter == MembersFilter::Admins && _channel->isMegagroup() && _rows.size() < Global::ChatSizeMax()) {
			_channel->mgInfo->lastAdmins.clear();
			for (int32 i = 0, l = _rows.size(); i != l; ++i) {
				if (_roles.at(i) == MemberRole::Creator || _roles.at(i) == MemberRole::Editor) {
					_channel->mgInfo->lastAdmins.insert(_rows.at(i));
				}
			}

			Notify::peerUpdatedDelayed(_channel, Notify::PeerUpdate::Flag::AdminsChanged);
		}
	}
	if (_rows.isEmpty()) {
		_rows.push_back(App::self());
		_dates.push_back(date(MTP_int(_channel->date)));
		_roles.push_back(MemberRole::Self);
		_datas.push_back(0);
	}

	clearSel();
	_loading = false;
	refresh();

	emit loaded();
}

bool MembersBox::Inner::membersFailed(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	Ui::hideLayer();
	return true;
}

void MembersBox::Inner::kickDone(const MTPUpdates &result, mtpRequestId req) {
	App::main()->sentUpdatesReceived(result);

	if (_kickRequestId != req) return;
	removeKicked();
	if (_kickBox) _kickBox->onClose();
}

void MembersBox::Inner::kickAdminDone(const MTPUpdates &result, mtpRequestId req) {
	if (_kickRequestId != req) return;
	if (App::main()) App::main()->sentUpdatesReceived(result);
	removeKicked();
	if (_kickBox) _kickBox->onClose();
}

bool MembersBox::Inner::kickFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_kickBox) _kickBox->onClose();
	load();
	return true;
}

void MembersBox::Inner::removeKicked() {
	_kickRequestId = 0;
	int32 index = _rows.indexOf(_kickConfirm);
	if (index >= 0) {
		_rows.removeAt(index);
		delete _datas.at(index);
		_datas.removeAt(index);
		_dates.removeAt(index);
		_roles.removeAt(index);
		clearSel();
		if (_filter == MembersFilter::Recent && _channel->membersCount() > 1) {
			_channel->setMembersCount(_channel->membersCount() - 1);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		} else if (_filter == MembersFilter::Admins && _channel->adminsCount() > 1) {
			_channel->setAdminsCount(_channel->adminsCount() - 1);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		}
		refresh();
	}
	_kickConfirm = 0;
}
