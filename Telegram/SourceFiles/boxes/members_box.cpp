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
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "boxes/contacts_box.h"
#include "boxes/confirm_box.h"
#include "boxes/edit_participant_box.h"
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

namespace {

constexpr auto kReloadChannelAdminsTimeout = 1000; // 1 second wait before reload admins in channel after adding

} // namespace

MembersBox::MembersBox(QWidget*, ChannelData *channel, MembersFilter filter)
: _channel(channel)
, _filter(filter) {
}

void MembersBox::prepare() {
	setTitle(langFactory((_filter == MembersFilter::Recent) ? lng_channel_members : lng_channel_admins));

	_inner = setInnerWidget(object_ptr<Inner>(this, _channel, _filter), st::boxLayerScroll);

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);
	refreshButtons();
	if (_filter == MembersFilter::Admins) {
		subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::ChannelRightsChanged, [this](const Notify::PeerUpdate &update) {
			if (update.peer == _channel) {
				refreshButtons();
			}
		}));
	}

	connect(_inner, SIGNAL(mustScrollTo(int, int)), this, SLOT(onScrollToY(int, int)));

	_loadTimer.create(this);
	connect(_loadTimer, SIGNAL(timeout()), _inner, SLOT(load()));
}

void MembersBox::refreshButtons() {
	clearButtons();
	addButton(langFactory(lng_close), [this] { closeBox(); });
	if (_filter == MembersFilter::Admins) {
		if (_channel->canAddAdmins()) {
			addLeftButton(langFactory(lng_channel_add_admin), [this] { onAdd(); });
		}
	} else if (_channel->amCreator() && (_channel->membersCount() < (_channel->isMegagroup() ? Global::MegagroupSizeMax() : Global::ChatSizeMax()) || (!_channel->isMegagroup() && !_channel->isPublic()))) {
		addLeftButton(langFactory(lng_channel_add_members), [this] { onAdd(); });
	}
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
	_loadTimer->start(kReloadChannelAdminsTimeout);
}

struct MembersBox::Inner::RowData {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	int rippleRowTop = 0;
	Text name;
	QString online;
	bool onlineColor;
	bool canKick;
};

MembersBox::Inner::Inner(QWidget *parent, gsl::not_null<ChannelData*> channel, MembersFilter filter) : TWidget(parent)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _channel(channel)
, _filter(filter)
, _kickText(lang((filter == MembersFilter::Admins) ? lng_profile_edit_admin : lng_profile_kick))
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
	if (_rows.empty()) {
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
			paintDialog(p, ms, _rows[from], selected, kickSelected);
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
	if (_selected >= 0 && _selected < _rows.size() && _kickSelected < 0) {
		ensureData(_rows[_selected]);
		addRipple(_rows[_selected].data.get());
	}
}

void MembersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	auto kickPressed = _kickPressed;
	setPressed(-1);
	if (e->button() == Qt::LeftButton) {
		if (pressed == _selected && kickPressed == _kickSelected) {
			if (kickPressed >= 0) {
				actionPressed(_rows[_kickSelected]);
			} else if (pressed >= 0) {
				chooseParticipant();
			}
		}
	}
}

void MembersBox::Inner::actionPressed(Member &row) {
	auto user = row.user;
	if (_kickBox) _kickBox->closeBox();
	if (_filter == MembersFilter::Recent) {
		auto text = (_channel->isMegagroup() ? lng_profile_sure_kick : lng_profile_sure_kick_channel)(lt_user, user->firstName);
		_kickBox = Ui::show(Box<ConfirmBox>(text, base::lambda_guarded(this, [this, user] {
			MTP::send(MTPchannels_EditBanned(_channel->inputChannel, user->inputUser, ChannelData::KickedRestrictedRights()), ::rpcDone(base::lambda_guarded(this, [this, user](const MTPUpdates &result) {
				App::main()->sentUpdatesReceived(result);
				removeKicked(user);
				if (_kickBox) _kickBox->closeBox();
			})), rpcFail(&Inner::kickFail));
		})), KeepOtherLayers);
	} else {
		auto currentRights = _rows[_kickSelected].adminRights;
		_kickBox = Ui::show(Box<EditAdminBox>(_channel, user, currentRights, base::lambda_guarded(this, [this, user](const MTPChannelAdminRights &rights) {
			if (_kickBox) _kickBox->closeBox();
			MTP::send(MTPchannels_EditAdmin(_channel->inputChannel, user->inputUser, rights), ::rpcDone(base::lambda_guarded(this, [this, user, rights](const MTPUpdates &result, mtpRequestId req) {
				if (App::main()) App::main()->sentUpdatesReceived(result);
				_channel->applyEditAdmin(user, rights);
				if (rights.c_channelAdminRights().vflags.v == 0) {
					removeKicked(user);
				} else {
					auto it = std::find_if(_rows.begin(), _rows.end(), [this, user](auto &&row) {
						return (row.user == user);
					});
					if (it != _rows.end()) {
						it->adminRights = rights;
					}
				}
				if (_kickBox) _kickBox->closeBox();
			})), rpcFail(&Inner::kickFail));
		})), KeepOtherLayers);
	}
}

void MembersBox::Inner::addRipple(gsl::not_null<RowData*> data) {
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

void MembersBox::Inner::stopLastRipple(gsl::not_null<RowData*> data) {
	if (data->ripple) {
		data->ripple->lastStop();
	}
}

void MembersBox::Inner::setPressed(int pressed) {
	if (_pressed >= 0 && _pressed < _rows.size()) {
		if (_rows[_pressed].data) {
			stopLastRipple(_rows[_pressed].data.get());
		}
	}
	_pressed = pressed;
}

void MembersBox::Inner::paintDialog(Painter &p, TimeMs ms, Member &row, bool selected, bool kickSelected) {
	ensureData(row);

	auto user = row.user;
	auto &data = row.data;

	p.fillRect(0, 0, width(), _rowHeight, selected ? st::contactsBgOver : st::contactsBg);
	if (data->ripple) {
		data->ripple->paint(p, 0, 0, width(), ms);
		if (data->ripple->empty()) {
			data->ripple.reset();
		}
	}
	user->paintUserpicLeft(p, st::contactsPadding.left(), st::contactsPadding.top(), width(), st::contactsPhotoSize);

	p.setPen(st::contactsNameFg);

	auto namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	auto namew = width() - namex - st::contactsPadding.right() - (data->canKick ? (_kickWidth + st::contactsCheckPosition.x() * 2) : 0);
	if (user->isVerified()) {
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
		_selected = _rows.empty() ? -1 : 0;
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
		if (!_rows.empty()) {
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

MembersBox::Inner::Member::Member(gsl::not_null<UserData*> user) : user(user) {
}

MembersBox::Inner::Member::Member(Member &&other) = default;

MembersBox::Inner::Member &MembersBox::Inner::Member::operator=(Member &&other) = default;

MembersBox::Inner::Member::~Member() = default;

void MembersBox::Inner::loadProfilePhotos() {
	if (_visibleTop >= _visibleBottom) return;

	auto yFrom = _visibleTop;
	auto yTo = yFrom + (_visibleBottom - _visibleTop) * 5;
	AuthSession::Current().downloader().clearPriorities();

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	if (!_rows.empty()) {
		int32 from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _rows.size()) {
			int32 to = (yTo / _rowHeight) + 1;
			if (to > _rows.size()) to = _rows.size();

			for (; from < to; ++from) {
				_rows[from].user->loadUserpic();
			}
		}
	}
}

void MembersBox::Inner::chooseParticipant() {
	if (_selected < 0 || _selected >= _rows.size()) return;
	if (auto peer = _rows[_selected].user) {
		Ui::hideLayer();
		Ui::showPeerProfile(peer);
	}
}

void MembersBox::Inner::refresh() {
	if (_rows.empty()) {
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
	for_const (auto &&row, _rows) {
		result.insert(row.user);
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

void MembersBox::Inner::ensureData(Member &row) {
	if (row.data) {
		return;
	}
	row.data = std::make_unique<RowData>();
	row.data->name.setText(st::contactsNameStyle, row.user->name, _textNameOptions);
	auto now = unixtime();
	row.data->online = App::onlineText(row.user, now);// lng_mediaview_date_time(lt_date, _dates[index].date().toString(qsl("dd.MM.yy")), lt_time, _dates[index].time().toString(cTimeFormat()));
	row.data->onlineColor = App::onlineColorUse(row.user, now);
	if (_filter == MembersFilter::Recent) {
		row.data->canKick = _channel->canBanMembers() ? (row.role == MemberRole::None) : false;
	} else if (_filter == MembersFilter::Admins) {
		row.data->canKick = _channel->amCreator() ? (row.role == MemberRole::Admin) : row.adminCanEdit;
	} else {
		row.data->canKick = false;
	}
}

void MembersBox::Inner::clear() {
	_rows.clear();
	if (_kickBox) _kickBox->closeBox();
	clearSel();
}

MembersBox::Inner::~Inner() {
	clear();
}

void MembersBox::Inner::updateSelection() {
	if (!_mouseSelection) return;

	auto p = mapFromGlobal(_lastMousePos);
	p.setY(p.y() - st::membersMarginTop);
	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	auto selected = (in && p.y() >= 0 && p.y() < _rows.size() * _rowHeight) ? (p.y() / _rowHeight) : -1;
	auto kickSelected = selected;
	if (selected >= 0) {
		ensureData(_rows[selected]);
	}
	if (selected >= 0 && (!_rows[selected].data->canKick || !QRect(width() - _kickWidth - st::contactsPadding.right() - st::contactsCheckPosition.x(), selected * _rowHeight + st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, _kickWidth, st::normalFont->height).contains(p))) {
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
	for (auto i= 0, l = int(_rows.size()); i != l; ++i) {
		auto &row = _rows[i];
		if (row.user == peer) {
			if (row.data) {
				row.data->name.setText(st::contactsNameStyle, peer->name, _textNameOptions);
				update(0, st::membersMarginTop + i * _rowHeight, width(), _rowHeight);
			}
			break;
		}
	}
}

void MembersBox::Inner::membersReceived(const MTPchannels_ChannelParticipants &result, mtpRequestId req) {
	Expects(result.type() == mtpc_channels_channelParticipants);

	clear();
	_loadingRequestId = 0;

	auto &d = result.c_channels_channelParticipants();
	auto &v = d.vparticipants.v;
	_rows.reserve(v.size());

	if (_filter == MembersFilter::Recent && _channel->membersCount() < d.vcount.v) {
		_channel->setMembersCount(d.vcount.v);
		if (App::main()) emit App::main()->peerUpdated(_channel);
	} else if (_filter == MembersFilter::Admins && _channel->adminsCount() < d.vcount.v) {
		_channel->setAdminsCount(d.vcount.v);
		if (App::main()) emit App::main()->peerUpdated(_channel);
	}
	App::feedUsers(d.vusers);

	auto emptyAdminRights = MTP_channelAdminRights(MTP_flags(0));
	auto emptyRestrictedRights = MTP_channelBannedRights(MTP_flags(0), MTP_int(0));
	for (auto i = v.cbegin(), e = v.cend(); i != e; ++i) {
		auto userId = UserId(0);
		auto addedTime = TimeId(0);
		auto role = MemberRole::None;
		auto adminCanEdit = false;
		auto adminRights = emptyAdminRights;
		auto restrictedRights = emptyRestrictedRights;
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
		case mtpc_channelParticipantAdmin:
			role = MemberRole::Admin;
			userId = i->c_channelParticipantAdmin().vuser_id.v;
			addedTime = i->c_channelParticipantAdmin().vdate.v;
			adminRights = i->c_channelParticipantAdmin().vadmin_rights;
			adminCanEdit = i->c_channelParticipantAdmin().is_can_edit();
			break;
		case mtpc_channelParticipantCreator:
			userId = i->c_channelParticipantCreator().vuser_id.v;
			addedTime = _channel->date;
			role = MemberRole::Creator;
			break;
		case mtpc_channelParticipantBanned:
			userId = i->c_channelParticipantBanned().vuser_id.v;
			addedTime = i->c_channelParticipantBanned().vdate.v;
			restrictedRights = i->c_channelParticipantBanned().vbanned_rights;
			role = MemberRole::Restricted;
		}
		if (auto user = App::userLoaded(userId)) {
			auto row = Member(user);
			row.adminCanEdit = adminCanEdit;
			row.adminRights = adminRights;
			row.restrictedRights = restrictedRights;
			row.date = date(addedTime);
			row.role = role;
			_rows.push_back(std::move(row));
			if (role == MemberRole::Creator && _channel->mgInfo) {
				_channel->mgInfo->creator = user;
			}
		}
	}

	// update admins if we got all of them
	if (_filter == MembersFilter::Admins && _channel->isMegagroup() && _rows.size() < Global::ChatSizeMax()) {
		_channel->mgInfo->lastAdmins.clear();
		for (auto &&row : _rows) {
			if (row.role == MemberRole::Admin) {
				_channel->mgInfo->lastAdmins.insert(row.user, MegagroupInfo::Admin { row.adminRights, row.adminCanEdit });
			}
		}

		Notify::peerUpdatedDelayed(_channel, Notify::PeerUpdate::Flag::AdminsChanged);
	}

	if (_rows.empty()) {
		auto row = Member(App::self());
		row.date = date(MTP_int(_channel->date));
		row.role = MemberRole::Self;
		row.adminRights = _channel->adminRightsBoxed();
		row.restrictedRights = _channel->restrictedRightsBoxed();
		_rows.push_back(std::move(row));
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

bool MembersBox::Inner::kickFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_kickBox) _kickBox->closeBox();
	load();
	return true;
}

void MembersBox::Inner::removeKicked(UserData *kicked) {
	auto it = std::find_if(_rows.begin(), _rows.end(), [this, kicked](auto &&row) {
		return (row.user == kicked);
	});
	if (it != _rows.end()) {
		_rows.erase(it);
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
}
