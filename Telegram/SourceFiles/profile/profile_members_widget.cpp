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
#include "profile/profile_members_widget.h"

#include "styles/style_profile.h"
#include "mtproto/file_download.h"
#include "ui/buttons/left_outline_button.h"
#include "ui/flatlabel.h"
#include "boxes/contactsbox.h"
#include "boxes/confirmbox.h"
#include "core/click_handler_types.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "lang.h"

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

MembersWidget::MembersWidget(QWidget *parent, PeerData *peer, TitleVisibility titleVisibility)
: BlockWidget(parent, peer, (titleVisibility == TitleVisibility::Visible) ? lang(lng_profile_participants_section) : QString()) {
	setMouseTracking(true);

	_removeWidth = st::normalFont->width(lang(lng_profile_kick));

	_updateOnlineTimer.setSingleShot(true);
	connect(&_updateOnlineTimer, SIGNAL(timeout()), this, SLOT(onUpdateOnlineDisplay()));

	auto observeEvents = UpdateFlag::AdminsChanged
		| UpdateFlag::MembersChanged
		| UpdateFlag::UserOnlineChanged;
	Notify::registerPeerObserver(observeEvents, this, &MembersWidget::notifyPeerUpdated);
	FileDownload::registerImageLoadedObserver(this, &MembersWidget::repaintCallback);

	refreshMembers();
}

void MembersWidget::repaintCallback() {
	update();
}

void MembersWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		if (update.flags & UpdateFlag::UserOnlineChanged) {
			if (auto user = update.peer->asUser()) {
				refreshUserOnline(user);
			}
		}
		return;
	}

	if (update.flags & UpdateFlag::MembersChanged) {
		refreshMembers();
		contentSizeUpdated();
	} else if (update.flags & UpdateFlag::AdminsChanged) {
		if (auto chat = peer()->asChat()) {
			for_const (auto member, _list) {
				setMemberFlags(member, chat);
			}
		} else if (auto megagroup = peer()->asMegagroup()) {
			for_const (auto member, _list) {
				setMemberFlags(member, megagroup);
			}
		}
	}
	repaintCallback();
}

void MembersWidget::refreshUserOnline(UserData *user) {
	auto it = _membersByUser.find(user);
	if (it == _membersByUser.cend()) return;

	_now = unixtime();

	auto member = it.value();
	member->online = !user->botInfo && App::onlineColorUse(user->onlineTill, _now);
	member->onlineTill = user->onlineTill;
	member->onlineForSort = user->isSelf() ? INT_MAX : App::onlineForSort(user, _now);
	member->onlineText = QString();

	sortMembers();
	update();
}

void MembersWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	if (auto megagroup = peer()->asMegagroup()) {
		auto megagroupInfo = megagroup->mgInfo;
		if (!megagroupInfo->lastParticipants.isEmpty() && megagroupInfo->lastParticipants.size() < megagroup->membersCount()) {
			if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
				App::api()->requestLastParticipants(megagroup, false);
			}
		}
	}

	preloadUserPhotos();
}

int MembersWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop();

	if (_limitReachedInfo) {
		int limitReachedInfoWidth = newWidth - getListLeft();
		accumulate_min(limitReachedInfoWidth, st::profileBlockWideWidthMax);

		_limitReachedInfo->resizeToWidth(limitReachedInfoWidth);
		_limitReachedInfo->moveToLeft(getListLeft(), contentTop());
		newHeight = getListTop();
	}

	newHeight += _list.size() * st::profileMemberHeight;

	return newHeight;
}

void MembersWidget::paintContents(Painter &p) {
	int left = getListLeft();
	int top = getListTop();
	int memberRowWidth = width() - left;
	accumulate_min(memberRowWidth, st::profileBlockWideWidthMax);
	if (_limitReachedInfo) {
		int infoTop = contentTop();
		int infoHeight = top - infoTop - st::profileLimitReachedSkip;
		paintOutlinedRect(p, left, infoTop, memberRowWidth, infoHeight);
	}

	_now = unixtime();
	int from = floorclamp(_visibleTop - top, st::profileMemberHeight, 0, _list.size());
	int to = ceilclamp(_visibleBottom - top, st::profileMemberHeight, 0, _list.size());
	for (int i = from; i < to; ++i) {
		int y = top + i * st::profileMemberHeight;
		bool selected = (i == _selected);
		bool selectedKick = selected && _selectedKick;
		if (_pressed >= 0) {
			if (_pressed != _selected) {
				selected = selectedKick = false;
			} else if (!_pressedKick) {
				_selectedKick = false;
			}
		}
		paintMember(p, left, y, _list.at(i), selected, selectedKick);
	}
}

void MembersWidget::paintOutlinedRect(Painter &p, int x, int y, int w, int h) const {
	int outlineWidth = st::defaultLeftOutlineButton.outlineWidth;
	p.fillRect(rtlrect(x, y, outlineWidth, h, width()), st::defaultLeftOutlineButton.outlineFgOver);
	p.fillRect(rtlrect(x + outlineWidth, y, w - outlineWidth, h, width()), st::defaultLeftOutlineButton.textBgOver);
}

void MembersWidget::mouseMoveEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();
}

void MembersWidget::mousePressEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();

	_pressed = _selected;
	_pressedKick = _selectedKick;
}

void MembersWidget::mouseReleaseEvent(QMouseEvent *e) {
	_mousePosition = e->globalPos();
	updateSelection();

	auto pressed = _pressed;
	auto pressedKick = _pressedKick;
	_pressed = -1;
	_pressedKick = false;
	if (pressed >= 0 && pressed < _list.size() && pressed == _selected && pressedKick == _selectedKick) {
		auto member = _list.at(pressed);
		if (pressedKick) {
			Ui::showLayer(new KickMemberBox(peer(), member->user));
		} else {
			Ui::showPeerProfile(member->user);
		}
	}
	setCursor(_selectedKick ? style::cur_pointer : style::cur_default);
	repaintSelectedRow();
}

void MembersWidget::enterEvent(QEvent *e) {
	_mousePosition = QCursor::pos();
	updateSelection();
}

void MembersWidget::leaveEvent(QEvent *e) {
	_mousePosition = QPoint(-1, -1);
	updateSelection();
}

void MembersWidget::updateSelection() {
	int selected = -1;
	bool selectedKick = false;

	auto mouse = mapFromGlobal(_mousePosition);
	if (rtl()) mouse.setX(width() - mouse.x());
	int left = getListLeft();
	int top = getListTop();
	int memberRowWidth = width() - left;
	accumulate_min(memberRowWidth, st::profileBlockWideWidthMax);
	if (mouse.x() >= left && mouse.x() < left + memberRowWidth && mouse.y() >= top) {
		selected = (mouse.y() - top) / st::profileMemberHeight;
		if (selected >= _list.size()) {
			selected = -1;
		} else if (_list.at(selected)->canBeKicked) {
			int skip = st::profileMemberPhotoPosition.x();
			int nameLeft = left + st::profileMemberNamePosition.x();
			int nameTop = top + _selected * st::profileMemberHeight + st::profileMemberNamePosition.y();
			int nameWidth = memberRowWidth - st::profileMemberNamePosition.x() - skip;
			if (mouse.x() >= nameLeft + nameWidth - _removeWidth && mouse.x() < nameLeft + nameWidth) {
				if (mouse.y() >= nameTop && mouse.y() < nameTop + st::normalFont->height) {
					selectedKick = true;
				}
			}
		}
	}

	setSelected(selected, selectedKick);
}

void MembersWidget::setSelected(int selected, bool selectedKick) {
	if (_selected == selected && _selectedKick == selectedKick) {
		return;
	}

	repaintSelectedRow();
	if (_selectedKick != selectedKick) {
		_selectedKick = selectedKick;
		if (_pressed < 0) {
			setCursor(_selectedKick ? style::cur_pointer : style::cur_default);
		}
	}
	if (_selected != selected) {
		_selected = selected;
		repaintSelectedRow();
	}
}

void MembersWidget::repaintSelectedRow() {
	if (_selected >= 0) {
		int left = getListLeft();
		rtlupdate(left, getListTop() + _selected * st::profileMemberHeight, width() - left, st::profileMemberHeight);
	}
}

int MembersWidget::getListLeft() const {
	return st::profileBlockTitlePosition.x() - st::profileMemberPaddingLeft;
}

int MembersWidget::getListTop() const {
	int result = contentTop();
	if (_limitReachedInfo) {
		result += _limitReachedInfo->height();
		result += st::profileLimitReachedSkip;
	}
	return result;
}

void MembersWidget::refreshMembers() {
	_now = unixtime();
	if (auto chat = peer()->asChat()) {
		checkSelfAdmin(chat);
		if (chat->noParticipantInfo()) {
			App::api()->requestFullPeer(chat);
		}
		fillChatMembers(chat);
		refreshLimitReached();
	} else if (auto megagroup = peer()->asMegagroup()) {
		checkSelfAdmin(megagroup);
		auto megagroupInfo = megagroup->mgInfo;
		if (megagroupInfo->lastParticipants.isEmpty() || megagroup->lastParticipantsCountOutdated()) {
			App::api()->requestLastParticipants(megagroup);
		}
		fillMegagroupMembers(megagroup);
	}
	sortMembers();

	refreshVisibility();
}

void MembersWidget::refreshLimitReached() {
	auto chat = peer()->asChat();
	if (!chat) return;

	bool limitReachedShown = (_list.size() >= Global::ChatSizeMax()) && chat->amCreator() && !emptyTitle();
	if (limitReachedShown && !_limitReachedInfo) {
		_limitReachedInfo = new FlatLabel(this, st::profileLimitReachedLabel, st::profileLimitReachedStyle);
		QString title = textRichPrepare(lng_profile_migrate_reached(lt_count, Global::ChatSizeMax()));
		QString body = textRichPrepare(lang(lng_profile_migrate_body));
		QString link = textRichPrepare(lang(lng_profile_migrate_learn_more));
		QString text = qsl("%1%2%3\n%4 [a href=\"https://telegram.org/blog/supergroups5k\"]%5[/a]").arg(textcmdStartSemibold()).arg(title).arg(textcmdStopSemibold()).arg(body).arg(link);
		_limitReachedInfo->setRichText(text);
		_limitReachedInfo->setClickHandlerHook(func(this, &MembersWidget::limitReachedHook));
	} else if (!limitReachedShown && _limitReachedInfo) {
		_limitReachedInfo.destroy();
	}
}

bool MembersWidget::limitReachedHook(const ClickHandlerPtr &handler, Qt::MouseButton button) {
	Ui::showLayer(new ConvertToSupergroupBox(peer()->asChat()));
	return false;
}

void MembersWidget::checkSelfAdmin(ChatData *chat) {
	if (chat->participants.isEmpty()) return;

	auto self = App::self();
	if (chat->amAdmin() && !chat->admins.contains(self)) {
		chat->admins.insert(self);
	} else if (!chat->amAdmin() && chat->admins.contains(self)) {
		chat->admins.remove(self);
	}
}

void MembersWidget::checkSelfAdmin(ChannelData *megagroup) {
	if (megagroup->mgInfo->lastParticipants.isEmpty()) return;

	bool amAdmin = (megagroup->amCreator() || megagroup->amEditor());
	auto self = App::self();
	if (amAdmin && !megagroup->mgInfo->lastAdmins.contains(self)) {
		megagroup->mgInfo->lastAdmins.insert(self);
	} else if (!amAdmin && megagroup->mgInfo->lastAdmins.contains(self)) {
		megagroup->mgInfo->lastAdmins.remove(self);
	}
}

void MembersWidget::preloadUserPhotos() {
	int top = getListTop();
	int preloadFor = (_visibleBottom - _visibleTop) * PreloadHeightsCount;
	int from = floorclamp(_visibleTop - top, st::profileMemberHeight, 0, _list.size());
	int to = ceilclamp(_visibleBottom + preloadFor - top, st::profileMemberHeight, 0, _list.size());
	for (int i = from; i < to; ++i) {
		_list.at(i)->user->loadUserpic();
	}
}

void MembersWidget::refreshVisibility() {
	setVisible(!_list.isEmpty());
}

void MembersWidget::sortMembers() {
	if (!_sortByOnline || _list.isEmpty()) return;

	qSort(_list.begin(), _list.end(), [](Member *a, Member *b) -> bool {
		return a->onlineForSort > b->onlineForSort;
	});

	updateOnlineCount();
}

void MembersWidget::updateOnlineCount() {
	bool onlyMe = true;
	int newOnlineCount = 0;
	for_const (auto member, _list) {
		bool isOnline = !member->user->botInfo && App::onlineColorUse(member->onlineTill, _now);
		if (member->online != isOnline) {
			member->online = isOnline;
			member->onlineText = QString();
		}
		if (member->online) {
			++newOnlineCount;
			if (!member->user->isSelf()) {
				onlyMe = false;
			}
		}
	}
	if (newOnlineCount == 1 && onlyMe) {
		newOnlineCount = 0;
	}
	if (_onlineCount != newOnlineCount) {
		_onlineCount = newOnlineCount;
		emit onlineCountUpdated(_onlineCount);
	}
}

MembersWidget::Member *MembersWidget::addUser(ChatData *chat, UserData *user) {
	auto member = getMember(user);
	setMemberFlags(member, chat);
	_list.push_back(member);
	return member;
}

void MembersWidget::fillChatMembers(ChatData *chat) {
	if (chat->participants.isEmpty()) return;

	_list.clear();
	if (!chat->amIn()) return;

	_sortByOnline = true;

	_list.reserve(chat->participants.size());
	addUser(chat, App::self())->onlineForSort = INT_MAX; // Put me on the first place.
	for (auto i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
		auto user = i.key();
		if (!user->isSelf()) {
			addUser(chat, user);
		}
	}
}

void MembersWidget::setMemberFlags(Member *member, ChatData *chat) {
	auto isCreator = (chat->creator == peerToUser(member->user->id));
	auto isAdmin = chat->admins.contains(member->user);
	member->isAdmin = isCreator || isAdmin;
	if (member->user->id == peerFromUser(MTP::authedId())) {
		member->canBeKicked = false;
	} else if (chat->amCreator() || (chat->amAdmin() && !member->isAdmin)) {
		member->canBeKicked = true;
	} else {
		member->canBeKicked = chat->invitedByMe.contains(member->user);
	}
}

MembersWidget::Member *MembersWidget::addUser(ChannelData *megagroup, UserData *user) {
	auto member = getMember(user);
	setMemberFlags(member, megagroup);
	_list.push_back(member);
	return member;
}

void MembersWidget::fillMegagroupMembers(ChannelData *megagroup) {
	t_assert(megagroup->mgInfo != nullptr);
	if (megagroup->mgInfo->lastParticipants.isEmpty()) return;

	if (!megagroup->amIn()) {
		_list.clear();
		return;
	}

	_sortByOnline = (megagroup->membersCount() > 0 && megagroup->membersCount() <= Global::ChatSizeMax());

	auto &membersList = megagroup->mgInfo->lastParticipants;
	if (_sortByOnline) {
		_list.clear();
		_list.reserve(membersList.size());
		addUser(megagroup, App::self())->onlineForSort = INT_MAX;
	} else if (membersList.size() >= _list.size()) {
		if (addUsersToEnd(megagroup)) {
			return;
		}
	}
	if (!_sortByOnline) {
		_list.clear();
		_list.reserve(membersList.size());
	}
	for_const (auto user, membersList) {
		if (!_sortByOnline || !user->isSelf()) {
			addUser(megagroup, user);
		}
	}
}

bool MembersWidget::addUsersToEnd(ChannelData *megagroup) {
	auto &membersList = megagroup->mgInfo->lastParticipants;
	for (int i = 0, count = _list.size(); i < count; ++i) {
		if (_list.at(i)->user != membersList.at(i)) {
			return false;
		}
	}
	_list.reserve(membersList.size());
	for (int i = _list.size(), count = membersList.size(); i < count; ++i) {
		addUser(megagroup, membersList.at(i));
	}
	return true;
}

void MembersWidget::setMemberFlags(Member *member, ChannelData *megagroup) {
	auto amCreatorOrAdmin = (peerToUser(member->user->id) == MTP::authedId()) && (megagroup->amCreator() || megagroup->amEditor());
	auto isAdmin = megagroup->mgInfo->lastAdmins.contains(member->user);
	member->isAdmin = amCreatorOrAdmin || isAdmin;
	if (member->user->isSelf()) {
		member->canBeKicked = false;
	} else if (megagroup->amCreator() || (megagroup->amEditor() && !member->isAdmin)) {
		member->canBeKicked = true;
	} else {
		member->canBeKicked = false;
	}
}

MembersWidget::Member *MembersWidget::getMember(UserData *user) {
	auto it = _membersByUser.constFind(user);
	if (it == _membersByUser.cend()) {
		auto member = new Member(user);
		it = _membersByUser.insert(user, member);
		member->online = !user->botInfo && App::onlineColorUse(user->onlineTill, _now);
		member->onlineTill = user->onlineTill;
		member->onlineForSort = App::onlineForSort(user, _now);
	}
	return it.value();
}

void MembersWidget::paintMember(Painter &p, int x, int y, Member *member, bool selected, bool selectedKick) {
	int memberRowWidth = width() - x;
	if (selected) {
		accumulate_min(memberRowWidth, st::profileBlockWideWidthMax);
		paintOutlinedRect(p, x, y, memberRowWidth, st::profileMemberHeight);
	}
	int skip = st::profileMemberPhotoPosition.x();

	member->user->paintUserpicLeft(p, st::profileMemberPhotoSize, x + st::profileMemberPhotoPosition.x(), y + st::profileMemberPhotoPosition.y(), width());

	if (member->name.isEmpty()) {
		member->name.setText(st::semiboldFont, App::peerName(member->user), _textNameOptions);
	}
	int nameLeft = x + st::profileMemberNamePosition.x();
	int nameTop = y + st::profileMemberNamePosition.y();
	int nameWidth = memberRowWidth - st::profileMemberNamePosition.x() - skip;
	if (member->canBeKicked && selected) {
		p.setFont(selectedKick ? st::normalFont->underline() : st::normalFont);
		p.setPen(st::windowActiveTextFg);
		p.drawTextLeft(nameLeft + nameWidth - _removeWidth, nameTop, width(), lang(lng_profile_kick), _removeWidth);
		nameWidth -= _removeWidth + skip;
	}
	if (member->isAdmin) {
		nameWidth -= st::profileMemberAdminIcon.width();
		int iconLeft = nameLeft + qMin(nameWidth, member->name.maxWidth());
		st::profileMemberAdminIcon.paint(p, QPoint(iconLeft, nameTop), width());
	}
	p.setPen(st::profileMemberNameFg);
	member->name.drawLeftElided(p, nameLeft, nameTop, nameWidth, width());

	if (member->onlineText.isEmpty() || (member->onlineTextTill <= _now)) {
		if (member->user->botInfo) {
			bool seesAllMessages = (member->user->botInfo->readsAllHistory || member->isAdmin);
			member->onlineText = lang(seesAllMessages ? lng_status_bot_reads_all : lng_status_bot_not_reads_all);
			member->onlineTextTill = _now + 86400;
		} else {
			member->online = App::onlineColorUse(member->onlineTill, _now);
			member->onlineText = App::onlineText(member->onlineTill, _now);
			member->onlineTextTill = _now + App::onlineWillChangeIn(member->onlineTill, _now);
		}
	}
	if (_updateOnlineAt <= _now || _updateOnlineAt > member->onlineTextTill) {
		_updateOnlineAt = member->onlineTextTill;
		_updateOnlineTimer.start((_updateOnlineAt - _now + 1) * 1000);
	}

	if (member->online) {
		p.setPen(st::profileMemberStatusFgActive);
	} else {
		p.setPen(selected ? st::profileMemberStatusFgOver : st::profileMemberStatusFg);
	}
	p.setFont(st::normalFont);
	p.drawTextLeft(x + st::profileMemberStatusPosition.x(), y + st::profileMemberStatusPosition.y(), width(), member->onlineText);
}

void MembersWidget::onUpdateOnlineDisplay() {
	if (_sortByOnline) {
		_now = unixtime();

		bool changed = false;
		for_const (auto member, _list) {
			if (!member->online) {
				if (!member->user->isSelf()) {
					continue;
				} else {
					break;
				}
			}
			bool isOnline = !member->user->botInfo && App::onlineColorUse(member->onlineTill, _now);
			if (!isOnline) {
				changed = true;
			}
		}
		if (changed) {
			updateOnlineCount();
		}
	}
	update();
}

MembersWidget::~MembersWidget() {
	auto members = createAndSwap(_membersByUser);
	for_const (auto member, members) {
		delete member;
	}
}

ChannelMembersWidget::ChannelMembersWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_participants_section)) {
	auto observeEvents = UpdateFlag::ChannelCanViewAdmins
		| UpdateFlag::ChannelCanViewMembers
		| UpdateFlag::AdminsChanged
		| UpdateFlag::MembersChanged;
	Notify::registerPeerObserver(observeEvents, this, &ChannelMembersWidget::notifyPeerUpdated);

	refreshButtons();
}

void ChannelMembersWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		return;
	}

	if (update.flags & (UpdateFlag::ChannelCanViewAdmins | UpdateFlag::AdminsChanged)) {
		refreshAdmins();
	}
	if (update.flags & (UpdateFlag::ChannelCanViewMembers | UpdateFlag::MembersChanged)) {
		refreshMembers();
	}
	refreshVisibility();

	contentSizeUpdated();
}

void ChannelMembersWidget::addButton(const QString &text, ChildWidget<Ui::LeftOutlineButton> *button, const char *slot) {
	if (text.isEmpty()) {
		button->destroy();
	} else if (*button) {
		(*button)->setText(text);
	} else {
		(*button) = new Ui::LeftOutlineButton(this, text, st::defaultLeftOutlineButton);
		(*button)->show();
		connect(*button, SIGNAL(clicked()), this, slot);
	}
}

void ChannelMembersWidget::refreshButtons() {
	refreshAdmins();
	refreshMembers();

	refreshVisibility();
}

void ChannelMembersWidget::refreshAdmins() {
	auto getAdminsText = [this]() -> QString {
		if (auto channel = peer()->asChannel()) {
			if (!channel->isMegagroup() && channel->canViewAdmins()) {
				int adminsCount = qMax(channel->adminsCount(), 1);
				return lng_channel_admins_link(lt_count, adminsCount);
			}
		}
		return QString();
	};
	addButton(getAdminsText(), &_admins, SLOT(onAdmins()));
}

void ChannelMembersWidget::refreshMembers() {
	auto getMembersText = [this]() -> QString {
		if (auto channel = peer()->asChannel()) {
			if (!channel->isMegagroup() && channel->canViewMembers()) {
				int membersCount = qMax(channel->membersCount(), 1);
				return lng_channel_members_link(lt_count, membersCount);
			}
		}
		return QString();
	};
	addButton(getMembersText(), &_members, SLOT(onMembers()));
}

void ChannelMembersWidget::refreshVisibility() {
	setVisible(_admins || _members);
}

int ChannelMembersWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop();

	auto resizeButton = [this, &newHeight, newWidth](ChildWidget<Ui::LeftOutlineButton> &button) {
		if (!button) {
			return;
		}

		int left = defaultOutlineButtonLeft();
		int availableWidth = newWidth - left - st::profileBlockMarginRight;
		accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);
		button->resizeToWidth(availableWidth);
		button->moveToLeft(left, newHeight);
		newHeight += button->height();
	};

	resizeButton(_admins);
	resizeButton(_members);

	return newHeight;
}

void ChannelMembersWidget::onAdmins() {
	if (auto channel = peer()->asChannel()) {
		Ui::showLayer(new MembersBox(channel, MembersFilterAdmins));
	}
}

void ChannelMembersWidget::onMembers() {
	if (auto channel = peer()->asChannel()) {
		Ui::showLayer(new MembersBox(channel, MembersFilterRecent));
	}
}

} // namespace Profile
