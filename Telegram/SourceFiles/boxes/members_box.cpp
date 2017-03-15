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
#include "ui/effects/ripple_animation.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "storage/file_download.h"

// Not used for now.
//
//MembersAddButton::MembersAddButton(QWidget *parent, const style::TwoIconButton &st) : RippleButton(parent, st.ripple)
//, _st(st) {
//	resize(_st.width, _st.height);
//	setCursor(style::cur_pointer);
//}
//
//void MembersAddButton::paintEvent(QPaintEvent *e) {
//	Painter p(this);
//
//	auto ms = getms();
//	auto over = isOver();
//	auto down = isDown();
//
//	((over || down) ? _st.iconBelowOver : _st.iconBelow).paint(p, _st.iconPosition, width());
//	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);
//	((over || down) ? _st.iconAboveOver : _st.iconAbove).paint(p, _st.iconPosition, width());
//}
//
//QImage MembersAddButton::prepareRippleMask() const {
//	return Ui::RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
//}
//
//QPoint MembersAddButton::prepareRippleStartPosition() const {
//	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
//}

MembersBox::MembersBox(QWidget*, ChannelData *channel, MembersFilter filter)
: _channel(channel)
, _filter(filter) {
}

void MembersBox::prepare() {
	setTitle(lang(_filter == MembersFilter::Recent ? lng_channel_members : lng_channel_admins));

	_inner = setInnerWidget(object_ptr<Inner>(this, _channel, _filter), st::boxLayerScroll);

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);
	addButton(lang(lng_close), [this] { closeBox(); });
	if (_channel->amCreator() && (_channel->membersCount() < (_channel->isMegagroup() ? Global::MegagroupSizeMax() : Global::ChatSizeMax()) || (!_channel->isMegagroup() && !_channel->isPublic()) || _filter == MembersFilter::Admins)) {
		addLeftButton(lang((_filter == MembersFilter::Admins) ? lng_channel_add_admin : lng_channel_add_members), [this] { onAdd(); });
	}

	connect(_inner, SIGNAL(mustScrollTo(int, int)), this, SLOT(onScrollToY(int, int)));

	_loadTimer.create(this);
	connect(_loadTimer, SIGNAL(timeout()), _inner, SLOT(load()));
}

void MembersBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(height(), -1);
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void MembersBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_inner->resize(width(), _inner->height());
}

void MembersBox::onAdd() {
	if (_inner->filter() == MembersFilter::Recent && _inner->channel()->membersCount() >= (_inner->channel()->isMegagroup() ? Global::MegagroupSizeMax() : Global::ChatSizeMax())) {
		Ui::show(Box<MaxInviteBox>(_inner->channel()->inviteLink()), KeepOtherLayers);
		return;
	}
	auto box = Box<ContactsBox>(_inner->channel(), _inner->filter(), _inner->already());
	if (_inner->filter() == MembersFilter::Recent) {
		Ui::show(std::move(box));
	} else {
		_addBox = Ui::show(std::move(box), KeepOtherLayers);
		if (_addBox) {
			connect(_addBox, SIGNAL(adminAdded()), this, SLOT(onAdminAdded()));
		}
	}
}

void MembersBox::onAdminAdded() {
	if (!_addBox) return;
	_addBox->closeBox();
	_addBox = nullptr;
	_loadTimer->start(ReloadChannelMembersTimeout);
}

MembersBox::Inner::Inner(QWidget *parent, ChannelData *channel, MembersFilter filter) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _channel(channel)
, _filter(filter)
, _kickText(lang(lng_profile_kick))
, _kickWidth(st::normalFont->width(_kickText))
, _aboutWidth(st::boxWideWidth - st::contactsPadding.left() - st::contactsPadding.right())
, _about(_aboutWidth) {
	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });

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

	auto ms = getms();
	auto yFrom = r.y() - st::membersMarginTop;
	auto yTo = r.y() + r.height() - st::membersMarginTop;
	p.translate(0, st::membersMarginTop);
	if (_rows.isEmpty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	} else {
		int32 from = floorclamp(yFrom, _rowHeight, 0, _rows.size());
		int32 to = ceilclamp(yTo, _rowHeight, 0, _rows.size());
		p.translate(0, from * _rowHeight);
		for (; from < to; ++from) {
			auto selected = (_pressed >= 0) ? (from == _pressed) : (from == _selected);
			auto kickSelected = (_pressed >= 0) ? (from == _kickPressed && from == _kickSelected) : (from == _kickSelected);
			paintDialog(p, ms, _rows[from], data(from), selected, kickSelected);
			p.translate(0, _rowHeight);
		}
		if (to == _rows.size() && _filter == MembersFilter::Recent && (_rows.size() < _channel->membersCount() || _rows.size() >= Global::ChatSizeMax())) {
			p.setPen(st::membersAboutLimitFg);
			_about.draw(p, st::contactsPadding.left(), st::membersAboutLimitPadding.top(), _aboutWidth, style::al_center);
		}
	}
}

void MembersBox::Inner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void MembersBox::Inner::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	if (_selected >= 0) {
		clearSel();
	}
}

void MembersBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSelection = true;
	_lastMousePos = e->globalPos();
	updateSelection();
}

void MembersBox::Inner::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	_lastMousePos = e->globalPos();
	updateSelection();
	setPressed(_selected);
	_kickPressed = _kickSelected;
	if (_selected >= 0 && _selected < _datas.size() && _kickSelected < 0) {
		addRipple(_datas[_selected]);
	}
}

void MembersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	auto kickPressed = _kickPressed;
	setPressed(-1);
	if (e->button() == Qt::LeftButton) {
		if (pressed == _selected && kickPressed == _kickSelected) {
			if (kickPressed >= 0) {
				if (!_kickRequestId) {
					_kickConfirm = _rows.at(_kickSelected);
					if (_kickBox) _kickBox->deleteLater();
					auto text = (_filter == MembersFilter::Recent ? (_channel->isMegagroup() ? lng_profile_sure_kick : lng_profile_sure_kick_channel) : lng_profile_sure_kick_admin)(lt_user, _kickConfirm->firstName);
					_kickBox = Ui::show(Box<ConfirmBox>(text, base::lambda_guarded(this, [this] {
						if (_filter == MembersFilter::Recent) {
							_kickRequestId = MTP::send(MTPchannels_KickFromChannel(_channel->inputChannel, _kickConfirm->inputUser, MTP_bool(true)), rpcDone(&Inner::kickDone), rpcFail(&Inner::kickFail));
						} else {
							_kickRequestId = MTP::send(MTPchannels_EditAdmin(_channel->inputChannel, _kickConfirm->inputUser, MTP_channelRoleEmpty()), rpcDone(&Inner::kickAdminDone), rpcFail(&Inner::kickFail));
						}
					})), KeepOtherLayers);
				}
			} else if (pressed >= 0) {
				chooseParticipant();
			}
		}
	}
}

void MembersBox::Inner::addRipple(MemberData *data) {
	auto rowTop = getSelectedRowTop();
	if (!data->ripple) {
		auto mask = Ui::RippleAnimation::rectMask(QSize(width(), _rowHeight));
		data->ripple = std::make_unique<Ui::RippleAnimation>(st::contactsRipple, std::move(mask), [this, data] {
			updateRowWithTop(data->rippleRowTop);
		});
	}
	data->rippleRowTop = rowTop;
	data->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(0, rowTop));
}

void MembersBox::Inner::stopLastRipple(MemberData *data) {
	if (data->ripple) {
		data->ripple->lastStop();
	}
}

void MembersBox::Inner::setPressed(int pressed) {
	if (_pressed >= 0 && _pressed < _datas.size()) {
		stopLastRipple(_datas[_pressed]);
	}
	_pressed = pressed;
}

void MembersBox::Inner::paintDialog(Painter &p, TimeMs ms, PeerData *peer, MemberData *data, bool selected, bool kickSelected) {
	UserData *user = peer->asUser();

	p.fillRect(0, 0, width(), _rowHeight, selected ? st::contactsBgOver : st::contactsBg);
	if (data->ripple) {
		data->ripple->paint(p, 0, 0, width(), ms);
		if (data->ripple->empty()) {
			data->ripple.reset();
		}
	}
	peer->paintUserpicLeft(p, st::contactsPadding.left(), st::contactsPadding.top(), width(), st::contactsPhotoSize);

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
		p.setFont(kickSelected ? st::linkOverFont : st::linkFont);
		p.setPen(kickSelected ? st::defaultLinkButton.overColor : st::defaultLinkButton.color);
		p.drawTextRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, width(), _kickText, _kickWidth);
	}

	p.setFont(st::contactsStatusFont->f);
	p.setPen(data->onlineColor ? st::contactsStatusFgOnline : (selected ? st::contactsStatusFgOver : st::contactsStatusFg));
	p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), data->online);
}

void MembersBox::Inner::selectSkip(int32 dir) {
	_time = unixtime();
	_mouseSelection = false;

	int cur = -1;
	if (_selected >= 0) {
		cur = _selected;
	}
	cur += dir;
	if (cur <= 0) {
		_selected = _rows.isEmpty() ? -1 : 0;
	} else if (cur >= _rows.size()) {
		_selected = -1;
	} else {
		_selected = cur;
	}
	if (dir > 0) {
		if (_selected < 0 || _selected >= _rows.size()) {
			_selected = -1;
		}
	} else {
		if (!_rows.isEmpty()) {
			if (_selected < 0) _selected = _rows.size() - 1;
		}
	}
	if (_selected >= 0) {
		emit mustScrollTo(st::membersMarginTop + _selected * _rowHeight, st::membersMarginTop + (_selected + 1) * _rowHeight);
	}

	update();
}

void MembersBox::Inner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

MembersBox::Inner::MemberData::MemberData() = default;

MembersBox::Inner::MemberData::~MemberData() = default;

void MembersBox::Inner::loadProfilePhotos() {
	if (_visibleTop >= _visibleBottom) return;

	auto yFrom = _visibleTop;
	auto yTo = yFrom + (_visibleBottom - _visibleTop) * 5;
	AuthSession::Current().downloader().clearPriorities();

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
	if (_selected < 0 || _selected >= _rows.size()) return;
	if (auto peer = _rows[_selected]) {
		Ui::hideLayer();
		Ui::showPeerProfile(peer);
	}
}

void MembersBox::Inner::refresh() {
	if (_rows.isEmpty()) {
		resize(width(), st::membersMarginTop + st::noContactsHeight + st::membersMarginBottom);
		_aboutHeight = 0;
	} else {
		_about.setText(st::boxLabelStyle, lng_channel_only_last_shown(lt_count, _rows.size()));
		_aboutHeight = st::membersAboutLimitPadding.top() + _about.countHeight(_aboutWidth) + st::membersAboutLimitPadding.bottom();
		if (_filter != MembersFilter::Recent || (_rows.size() >= _channel->membersCount() && _rows.size() < Global::ChatSizeMax())) {
			_aboutHeight = 0;
		}
		resize(width(), st::membersMarginTop + _aboutHeight + _rows.size() * _rowHeight + st::membersMarginBottom);
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

void MembersBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadProfilePhotos();
}

void MembersBox::Inner::clearSel() {
	updateSelectedRow();
	_selected = _kickSelected = -1;
	_lastMousePos = QCursor::pos();
	updateSelection();
}

MembersBox::Inner::MemberData *MembersBox::Inner::data(int32 index) {
	if (MemberData *result = _datas.at(index)) {
		return result;
	}
	MemberData *result = _datas[index] = new MemberData();
	result->name.setText(st::contactsNameStyle, _rows[index]->name, _textNameOptions);
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

void MembersBox::Inner::updateSelection() {
	if (!_mouseSelection) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	p.setY(p.y() - st::membersMarginTop);
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	auto selected = (in && p.y() >= 0 && p.y() < _rows.size() * _rowHeight) ? (p.y() / _rowHeight) : -1;
	auto kickSelected = selected;
	if (selected >= 0 && (!data(selected)->canKick || !QRect(width() - _kickWidth - st::contactsPadding.right() - st::contactsCheckPosition.x(), selected * _rowHeight + st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, _kickWidth, st::normalFont->height).contains(p))) {
		kickSelected = -1;
	}
	if (_selected != selected || _kickSelected != kickSelected) {
		updateSelectedRow();
		_selected = selected;
		_kickSelected = kickSelected;
		updateSelectedRow();
		setCursor(_kickSelected >= 0 ? style::cur_pointer : style::cur_default);
	}
}

void MembersBox::Inner::peerUpdated(PeerData *peer) {
	update();
}

int MembersBox::Inner::getSelectedRowTop() const {
	if (_selected >= 0) {
		return st::membersMarginTop + _selected * _rowHeight;
	}
	return -1;
}

void MembersBox::Inner::updateRowWithTop(int rowTop) {
	update(0, rowTop, width(), _rowHeight);
}

void MembersBox::Inner::updateSelectedRow() {
	auto rowTop = getSelectedRowTop();
	if (rowTop >= 0) {
		updateRowWithTop(rowTop);
	}
}

void MembersBox::Inner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	for (int32 i = 0, l = _rows.size(); i < l; ++i) {
		if (_rows.at(i) == peer) {
			if (_datas.at(i)) {
				_datas.at(i)->name.setText(st::contactsNameStyle, peer->name, _textNameOptions);
				update(0, st::membersMarginTop + i * _rowHeight, width(), _rowHeight);
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
		auto &d = result.c_channels_channelParticipants();
		auto &v = d.vparticipants.v;
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
	if (_kickBox) _kickBox->closeBox();
}

void MembersBox::Inner::kickAdminDone(const MTPUpdates &result, mtpRequestId req) {
	if (_kickRequestId != req) return;
	if (App::main()) App::main()->sentUpdatesReceived(result);
	removeKicked();
	if (_kickBox) _kickBox->closeBox();
}

bool MembersBox::Inner::kickFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_kickBox) _kickBox->closeBox();
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
