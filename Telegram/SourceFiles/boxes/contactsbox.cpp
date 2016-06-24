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
#include "boxes/contactsbox.h"

#include "dialogs/dialogs_indexed_list.h"
#include "lang.h"
#include "boxes/addcontactbox.h"
#include "boxes/contactsbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "ui/filedialog.h"
#include "boxes/photocropbox.h"
#include "boxes/confirmbox.h"
#include "observer_peer.h"
#include "apiwrap.h"

QString cantInviteError() {
	return lng_cant_invite_not_contact(lt_more_info, textcmdLink(qsl("https://telegram.me/spambot"), lang(lng_cant_more_info)));
}

ContactsInner::ContactsInner(CreatingGroupType creating) : TWidget()
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _newItemHeight(creating == CreatingGroupNone ? st::contactsNewItemHeight : 0)
, _creating(creating)
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::contactsAdminCheckbox)
, _contacts(App::main()->contactsList())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	init();
}

ContactsInner::ContactsInner(ChannelData *channel, MembersFilter membersFilter, const MembersAlreadyIn &already) : TWidget()
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _channel(channel)
, _membersFilter(membersFilter)
, _creating(CreatingGroupChannel)
, _already(already)
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::contactsAdminCheckbox)
, _contacts(App::main()->contactsList())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	init();
}

namespace {
	bool _sortByName(UserData *a, UserData *b) {
		return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	}
}

ContactsInner::ContactsInner(ChatData *chat, MembersFilter membersFilter) : TWidget()
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _chat(chat)
, _membersFilter(membersFilter)
, _allAdmins(this, lang(lng_chat_all_members_admins), !_chat->adminsEnabled(), st::contactsAdminCheckbox)
, _aboutWidth(st::boxWideWidth - st::contactsPadding.left() - st::contactsPadding.right() - st::contactsCheckPosition.x() * 2 - st::contactsCheckIcon.pxWidth())
, _aboutAllAdmins(st::boxTextFont, lang(lng_chat_about_all_admins), _defaultOptions, _aboutWidth)
, _aboutAdmins(st::boxTextFont, lang(lng_chat_about_admins), _defaultOptions, _aboutWidth)
, _customList((membersFilter == MembersFilterRecent) ? std_::unique_ptr<Dialogs::IndexedList>() : std_::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add))
, _contacts((membersFilter == MembersFilterRecent) ? App::main()->contactsList() : _customList.get())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	initList();
	if (membersFilter == MembersFilterAdmins) {
		_newItemHeight = st::contactsNewItemHeight + qMax(_aboutAllAdmins.countHeight(_aboutWidth), _aboutAdmins.countHeight(_aboutWidth)) + st::contactsAboutHeight;
		if (_contacts->isEmpty()) {
			App::api()->requestFullPeer(_chat);
		}
	}
	init();
}

ContactsInner::ContactsInner(UserData *bot) : TWidget()
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _bot(bot)
, _allAdmins(this, lang(lng_chat_all_members_admins), false, st::contactsAdminCheckbox)
, _customList(std_::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add))
, _contacts(_customList.get())
, _addContactLnk(this, lang(lng_add_contact_button)) {
	auto v = App::main()->dialogsList();
	for_const (auto row, *v) {
		auto peer = row->history()->peer;
		if (peer->isChat() && peer->asChat()->canEdit()) {
			_contacts->addToEnd(row->history());
		} else if (peer->isMegagroup() && (peer->asChannel()->amCreator() || peer->asChannel()->amEditor())) {
			_contacts->addToEnd(row->history());
		}
	}
	init();
}

void ContactsInner::init() {
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
	connect(&_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	connect(&_allAdmins, SIGNAL(changed()), this, SLOT(onAllAdminsChanged()));

	setAttribute(Qt::WA_OpaquePaintEvent);

	for_const (auto row, _contacts->all()) {
		row->attached = nullptr;
	}

	_filter = qsl("a");
	updateFilter();

	connect(App::main(), SIGNAL(dialogRowReplaced(Dialogs::Row*,Dialogs::Row*)), this, SLOT(onDialogRowReplaced(Dialogs::Row*,Dialogs::Row*)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
}

void ContactsInner::initList() {
	if (!_chat || _membersFilter != MembersFilterAdmins) return;

	QList<UserData*> admins, others;
	admins.reserve(_chat->admins.size() + 1);
	if (!_chat->participants.isEmpty()) {
		others.reserve(_chat->participants.size());
	}

	for (ChatData::Participants::const_iterator i = _chat->participants.cbegin(), e = _chat->participants.cend(); i != e; ++i) {
		if (i.key()->id == peerFromUser(_chat->creator)) continue;
		if (!_allAdmins.checked() && _chat->admins.contains(i.key())) {
			admins.push_back(i.key());
			_checkedContacts.insert(i.key(), true);
		} else {
			others.push_back(i.key());
		}
	}
	std::sort(admins.begin(), admins.end(), _sortByName);
	std::sort(others.begin(), others.end(), _sortByName);
	if (UserData *creator = App::userLoaded(_chat->creator)) {
		if (_chat->participants.contains(creator)) {
			admins.push_front(creator);
		}
	}
	for (int32 i = 0, l = admins.size(); i < l; ++i) {
		_contacts->addToEnd(App::history(admins.at(i)->id));
	}
	for (int32 i = 0, l = others.size(); i < l; ++i) {
		_contacts->addToEnd(App::history(others.at(i)->id));
	}
}

void ContactsInner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (bot()) {
		_contacts->peerNameChanged(peer, oldNames, oldChars);
	}
	peerUpdated(peer);
}

void ContactsInner::onAddBot() {
	if (_bot->botInfo && !_bot->botInfo->startGroupToken.isEmpty()) {
		MTP::send(MTPmessages_StartBot(_bot->inputUser, _addToPeer->input, MTP_long(rand_value<uint64>()), MTP_string(_bot->botInfo->startGroupToken)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, _bot));
	} else {
		App::main()->addParticipants(_addToPeer, QVector<UserData*>(1, _bot));
	}
	Ui::hideLayer();
	Ui::showPeerHistory(_addToPeer, ShowAtUnreadMsgId);
}

void ContactsInner::onAddAdmin() {
	if (_addAdminRequestId) return;
	_addAdminRequestId = MTP::send(MTPchannels_EditAdmin(_channel->inputChannel, _addAdmin->inputUser, MTP_channelRoleEditor()), rpcDone(&ContactsInner::addAdminDone), rpcFail(&ContactsInner::addAdminFail));
}

void ContactsInner::onNoAddAdminBox(QObject *obj) {
	if (obj == _addAdminBox) {
		_addAdminBox = 0;
	}
}

void ContactsInner::onAllAdminsChanged() {
	if (_saving) {
		if (_allAdmins.checked() != _allAdminsChecked) {
			_allAdmins.setChecked(_allAdminsChecked);
		}
	}
	update();
}

void ContactsInner::addAdminDone(const MTPUpdates &result, mtpRequestId req) {
	if (App::main()) App::main()->sentUpdatesReceived(result);
	if (req != _addAdminRequestId) return;

	_addAdminRequestId = 0;
	if (_addAdmin && _channel && _channel->isMegagroup()) {
		Notify::PeerUpdate update(_channel);
		if (_channel->mgInfo->lastParticipants.indexOf(_addAdmin) < 0) {
			_channel->mgInfo->lastParticipants.push_front(_addAdmin);
			update.flags |= Notify::PeerUpdate::Flag::MembersChanged;
		}
		_channel->mgInfo->lastAdmins.insert(_addAdmin);
		update.flags |= Notify::PeerUpdate::Flag::AdminsChanged;
		if (_addAdmin->botInfo) {
			_channel->mgInfo->bots.insert(_addAdmin);
			if (_channel->mgInfo->botStatus != 0 && _channel->mgInfo->botStatus < 2) {
				_channel->mgInfo->botStatus = 2;
			}
		}
		Notify::peerUpdatedDelayed(update);
	}
	if (_addAdminBox) _addAdminBox->onClose();
	emit adminAdded();
}

bool ContactsInner::addAdminFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (req != _addAdminRequestId) return true;

	_addAdminRequestId = 0;
	if (_addAdminBox) _addAdminBox->onClose();
	if (error.type() == "USERS_TOO_MUCH") {
		Ui::showLayer(new MaxInviteBox(_channel->inviteLink()), KeepOtherLayers);
	} else if (error.type() == "ADMINS_TOO_MUCH") {
		Ui::showLayer(new InformBox(lang(lng_channel_admins_too_much)), KeepOtherLayers);
	} else if (error.type() == qstr("USER_RESTRICTED")) {
		Ui::showLayer(new InformBox(lang(lng_cant_do_this)), KeepOtherLayers);
	} else  {
		emit adminAdded();
	}
	return true;
}

void ContactsInner::saving(bool flag) {
	_saving = flag;
	_allAdminsChecked = _allAdmins.checked();
	update();
}

void ContactsInner::peerUpdated(PeerData *peer) {
	if (_chat && (!peer || peer == _chat)) {
		bool inited = false;
		if (_membersFilter == MembersFilterAdmins && _contacts->isEmpty() && !_chat->participants.isEmpty()) {
			initList();
			inited = true;
		}
		if (!_chat->canEdit()) {
			Ui::hideLayer();
		} else if (!_chat->participants.isEmpty()) {
			for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
				delete i.value();
			}
			_contactsData.clear();
			for_const (auto row, _contacts->all()) {
				row->attached = nullptr;
			}
			if (!_filter.isEmpty()) {
				for (int32 j = 0, s = _filtered.size(); j < s; ++j) {
					_filtered[j]->attached = 0;
				}
			}
		}
		if (inited) {
			_filter += 'a';
			updateFilter(_lastQuery);
		}
		update();
	} else {
		ContactsData::iterator i = _contactsData.find(peer);
		if (i != _contactsData.cend()) {
			for_const (auto row, _contacts->all()) {
				if (row->attached == i.value()) {
					row->attached = nullptr;
					update(0, _newItemHeight + _rowHeight * row->pos(), width(), _rowHeight);
				}
			}
			if (!_filter.isEmpty()) {
				for (int32 j = 0, s = _filtered.size(); j < s; ++j) {
					if (_filtered[j]->attached == i.value()) {
						_filtered[j]->attached = 0;
						update(0, _rowHeight * j, width(), _rowHeight);
					}
				}
			}
			delete i.value();
			_contactsData.erase(i);
		}
	}
}

void ContactsInner::loadProfilePhotos(int32 yFrom) {
	int32 yTo = yFrom + (parentWidget() ? parentWidget()->height() : App::wnd()->height()) * 5;
	MTP::clearLoaderPriorities();

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	if (_filter.isEmpty()) {
		if (!_contacts->isEmpty()) {
			auto i = _contacts->cfind(yFrom - _newItemHeight, _rowHeight);
			for (auto end = _contacts->cend(); i != end; ++i) {
				if ((_newItemHeight + (*i)->pos() * _rowHeight) >= yTo) {
					break;
				}
				(*i)->history()->peer->loadUserpic();
			}
		}
	} else if (!_filtered.isEmpty()) {
		int32 from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			int32 to = (yTo / _rowHeight) + 1;
			if (to > _filtered.size()) to = _filtered.size();

			for (; from < to; ++from) {
				_filtered[from]->history()->peer->loadUserpic();
			}
		}
	}
}

ContactsInner::ContactData *ContactsInner::contactData(Dialogs::Row *row) {
	ContactData *data = (ContactData*)row->attached;
	if (!data) {
		PeerData *peer = row->history()->peer;
		ContactsData::const_iterator i = _contactsData.constFind(peer);
		if (i == _contactsData.cend()) {
			_contactsData.insert(peer, data = new ContactData());
			if (peer->isUser()) {
				if (_chat) {
					if (_membersFilter == MembersFilterRecent) {
						data->inchat = _chat->participants.contains(peer->asUser());
					} else {
						data->inchat = false;
					}
				} else if (_creating == CreatingGroupGroup) {
					data->inchat = (peerToUser(peer->id) == MTP::authedId());
				} else if (_channel) {
					data->inchat = (peerToUser(peer->id) == MTP::authedId()) || _already.contains(peer->asUser());
				} else {
					data->inchat = false;
				}
			} else {
				data->inchat = false;
			}
			data->onlineColor = false;
			data->check = _checkedContacts.contains(peer);
			data->name.setText(st::contactsNameFont, peer->name, _textNameOptions);
			if (peer->isUser()) {
				data->online = App::onlineText(peer->asUser(), _time);
				data->onlineColor = App::onlineColorUse(peer->asUser(), _time);
			} else if (peer->isChat()) {
				ChatData *chat = peer->asChat();
				if (!chat->amIn()) {
					data->online = lang(lng_chat_status_unaccessible);
				} else {
					data->online = lng_chat_status_members(lt_count, chat->count);
				}
			} else if (peer->isMegagroup()) {
				data->online = lang(lng_group_status);
			} else if (peer->isChannel()) {
				data->online = lang(lng_channel_status);
			}
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void ContactsInner::paintDialog(Painter &p, PeerData *peer, ContactData *data, bool sel) {
	UserData *user = peer->asUser();

	bool inverse = data->inchat || data->check;
	if (_chat && _membersFilter == MembersFilterAdmins) {
		inverse = false;
		if (_allAdmins.checked() || peer->id == peerFromUser(_chat->creator) || _saving) {
			sel = false;
		}
	} else {
		if (data->inchat || data->check || selectedCount() >= Global::MegagroupSizeMax()) {
			sel = false;
		}
	}
	p.fillRect(0, 0, width(), _rowHeight, inverse ? st::contactsBgActive : (sel ? st::contactsBgOver : st::white));
	p.setPen(inverse ? st::white : st::black);
	peer->paintUserpicLeft(p, st::contactsPhotoSize, st::contactsPadding.left(), st::contactsPadding.top(), width());

	int32 namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int32 iconw = (_chat || _creating != CreatingGroupNone) ? (st::contactsCheckPosition.x() * 2 + st::contactsCheckIcon.pxWidth()) : 0;
	int32 namew = width() - namex - st::contactsPadding.right() - iconw;
	if (peer->isVerified()) {
		namew -= st::verifiedCheck.pxWidth() + st::verifiedCheckPos.x();
		p.drawSpriteLeft(namex + qMin(data->name.maxWidth(), namew) + st::verifiedCheckPos.x(), st::contactsPadding.top() + st::contactsNameTop + st::verifiedCheckPos.y(), width(), st::verifiedCheck);
	}
	data->name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	if (_chat || (_creating != CreatingGroupNone && (!_channel || _membersFilter != MembersFilterAdmins))) {
		if (_chat && _membersFilter == MembersFilterAdmins) {
			if (sel || data->check || peer->id == peerFromUser(_chat->creator) || _allAdmins.checked()) {
				if (!data->check || peer->id == peerFromUser(_chat->creator) || _allAdmins.checked()) p.setOpacity(0.5);
				p.drawSpriteRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + st::contactsCheckPosition.y(), width(), st::contactsCheckIcon);
				if (!data->check || peer->id == peerFromUser(_chat->creator) || _allAdmins.checked()) p.setOpacity(1);
			}
		} else if (sel || data->check) {
			p.drawSpriteRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + st::contactsCheckPosition.y(), width(), (data->check ? st::contactsCheckActiveIcon : st::contactsCheckIcon));
		}
	}

	bool uname = (user || peer->isChannel()) && (data->online.at(0) == '@');
	p.setFont(st::contactsStatusFont->f);
	if (uname && !data->inchat && !data->check && !_lastQuery.isEmpty() && peer->userName().startsWith(_lastQuery, Qt::CaseInsensitive)) {
		int32 availw = width() - namex - st::contactsPadding.right() - iconw;
		QString first = '@' + peer->userName().mid(0, _lastQuery.size()), second = peer->userName().mid(_lastQuery.size());
		int32 w = st::contactsStatusFont->width(first);
		if (w >= availw || second.isEmpty()) {
			p.setPen(st::contactsStatusFgOnline);
			p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), st::contactsStatusFont->elided(first, availw));
		} else {
			second = st::contactsStatusFont->elided(second, availw - w);
			int32 secondw = st::contactsStatusFont->width(second);
			p.setPen(st::contactsStatusFgOnline);
			p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width() - secondw, first);
			p.setPen(sel ? st::contactsStatusFgOver : st::contactsStatusFg);
			p.drawTextLeft(namex + w, st::contactsPadding.top() + st::contactsStatusTop, width() + w, second);
		}
	} else {
		if (inverse) {
			p.setPen(st::black);
		} else if ((user && (uname || data->onlineColor)) || (peer->isChannel() && uname)) {
			p.setPen(st::contactsStatusFgOnline);
		} else {
			p.setPen(sel ? st::contactsStatusFgOver : st::contactsStatusFg);
		}
		p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), data->online);
	}
}

void ContactsInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	p.setClipRect(r);
	_time = unixtime();
	p.fillRect(r, st::white->b);

	int32 yFrom = r.y(), yTo = r.y() + r.height();
	if (_filter.isEmpty()) {
		if (!_contacts->isEmpty() || !_byUsername.isEmpty()) {
			if (_newItemHeight) {
				if (_chat) {
					p.fillRect(0, 0, width(), _newItemHeight - st::contactsPadding.bottom() - st::lineWidth, st::contactsAboutBg);
					p.fillRect(0, _newItemHeight - st::contactsPadding.bottom() - st::lineWidth, width(), st::lineWidth, st::shadowColor);
					p.setPen(st::black);
					p.drawTextLeft(st::contactsPadding.left(), st::contactsNewItemTop, width(), lang(lng_chat_all_members_admins));
					int32 iconw = st::contactsCheckPosition.x() * 2 + st::contactsCheckIcon.pxWidth();
					int32 aboutw = width() - st::contactsPadding.left() - st::contactsPadding.right() - iconw;
					(_allAdmins.checked() ? _aboutAllAdmins : _aboutAdmins).draw(p, st::contactsPadding.left(), st::contactsNewItemHeight + st::contactsAboutTop, aboutw);
				} else {
					p.fillRect(0, 0, width(), st::contactsNewItemHeight, (_newItemSel ? st::contactsBgOver : st::white)->b);
					p.setFont(st::contactsNameFont);
					p.drawSpriteLeft(st::contactsNewItemIconPosition.x(), st::contactsNewItemIconPosition.y(), width(), st::contactsNewItemIcon);
					p.setPen(st::contactsNewItemFg);
					p.drawTextLeft(st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left(), st::contactsNewItemTop, width(), lang(lng_add_contact_button));
				}
				yFrom -= _newItemHeight;
				yTo -= _newItemHeight;
				p.translate(0, _newItemHeight);
			}
			if (!_contacts->isEmpty()) {
				auto i = _contacts->cfind(yFrom, _rowHeight);
				p.translate(0, (*i)->pos() * _rowHeight);
				for (auto end = _contacts->cend(); i != end; ++i) {
					if ((*i)->pos() * _rowHeight >= yTo) {
						break;
					}
					paintDialog(p, (*i)->history()->peer, contactData(*i), (*i == _sel));
					p.translate(0, _rowHeight);
				}
				yFrom -= _contacts->size() * _rowHeight;
				yTo -= _contacts->size() * _rowHeight;
			}
			if (!_byUsername.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBG->b);
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, width(), st::searchedBarHeight), lang(lng_search_global_results), style::al_center);

				yFrom -= st::searchedBarHeight;
				yTo -= st::searchedBarHeight;
				p.translate(0, st::searchedBarHeight);

				int32 from = floorclamp(yFrom, _rowHeight, 0, _byUsername.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _byUsername.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					paintDialog(p, _byUsername[from], d_byUsername[from], (_byUsernameSel == from));
					p.translate(0, _rowHeight);
				}
			}
		} else {
			QString text;
			int32 skip = 0;
			if (bot()) {
				text = lang(cDialogsReceived() ? lng_bot_no_groups : lng_contacts_loading);
			} else if (_chat && _membersFilter == MembersFilterAdmins) {
				text = lang(lng_contacts_loading);
				p.fillRect(0, 0, width(), _newItemHeight - st::contactsPadding.bottom() - st::lineWidth, st::contactsAboutBg);
				p.fillRect(0, _newItemHeight - st::contactsPadding.bottom() - st::lineWidth, width(), st::lineWidth, st::shadowColor);
				p.setPen(st::black);
				p.drawTextLeft(st::contactsPadding.left(), st::contactsNewItemTop, width(), lang(lng_chat_all_members_admins));
				int32 iconw = st::contactsCheckPosition.x() * 2 + st::contactsCheckIcon.pxWidth();
				int32 aboutw = width() - st::contactsPadding.left() - st::contactsPadding.right() - iconw;
				(_allAdmins.checked() ? _aboutAllAdmins : _aboutAdmins).draw(p, st::contactsPadding.left(), st::contactsNewItemHeight + st::contactsAboutTop, aboutw);
				p.translate(0, _newItemHeight);
			} else if (cContactsReceived() && !_searching) {
				text = lang(lng_no_contacts);
				skip = st::noContactsFont->height;
			} else {
				text = lang(lng_contacts_loading);
			}
			p.setFont(st::noContactsFont->f);
			p.setPen(st::noContactsColor->p);
			p.drawText(QRect(0, 0, width(), st::noContactsHeight - skip), text, style::al_center);
		}
	} else {
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			p.setFont(st::noContactsFont->f);
			p.setPen(st::noContactsColor->p);
			QString text;
			if (bot()) {
				text = lang(cDialogsReceived() ? lng_bot_groups_not_found : lng_contacts_loading);
			} else if (_chat && _membersFilter == MembersFilterAdmins) {
				text = lang(_chat->participants.isEmpty() ? lng_contacts_loading : lng_contacts_not_found);
			} else {
				text = lang((cContactsReceived() && !_searching) ? lng_contacts_not_found : lng_contacts_loading);
			}
			p.drawText(QRect(0, 0, width(), st::noContactsHeight), text, style::al_center);
		} else {
			if (!_filtered.isEmpty()) {
				int32 from = floorclamp(yFrom, _rowHeight, 0, _filtered.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _filtered.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					paintDialog(p, _filtered[from]->history()->peer, contactData(_filtered[from]), (_filteredSel == from));
					p.translate(0, _rowHeight);
				}
			}
			if (!_byUsernameFiltered.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBG->b);
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, width(), st::searchedBarHeight), lang(lng_search_global_results), style::al_center);
				p.translate(0, st::searchedBarHeight);

				yFrom -= _filtered.size() * _rowHeight + st::searchedBarHeight;
				yTo -= _filtered.size() * _rowHeight + st::searchedBarHeight;
				int32 from = floorclamp(yFrom, _rowHeight, 0, _byUsernameFiltered.size());
				int32 to = ceilclamp(yTo, _rowHeight, 0, _byUsernameFiltered.size());
				p.translate(0, from * _rowHeight);
				for (; from < to; ++from) {
					paintDialog(p, _byUsernameFiltered[from], d_byUsernameFiltered[from], (_byUsernameSel == from));
					p.translate(0, _rowHeight);
				}
			}
		}
	}
}

void ContactsInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void ContactsInner::updateSelectedRow() {
	if (_filter.isEmpty()) {
		if (_newItemSel) {
			update(0, 0, width(), st::contactsNewItemHeight);
		}
		if (_sel) {
			update(0, _newItemHeight + _sel->pos() * _rowHeight, width(), _rowHeight);
		}
		if (_byUsernameSel >= 0) {
			update(0, _newItemHeight + _contacts->size() * _rowHeight + st::searchedBarHeight + _byUsernameSel * _rowHeight, width(), _rowHeight);
		}
	} else {
		if (_filteredSel >= 0) {
			update(0, _filteredSel * _rowHeight, width(), _rowHeight);
		}
		if (_byUsernameSel >= 0) {
			update(0, _filtered.size() * _rowHeight + st::searchedBarHeight + _byUsernameSel * _rowHeight, width(), _rowHeight);
		}
	}
}

void ContactsInner::leaveEvent(QEvent *e) {
	_mouseSel = false;
	setMouseTracking(false);
	if (_newItemSel || _sel || _filteredSel >= 0 || _byUsernameSel >= 0) {
		updateSelectedRow();
		_sel = 0;
		_newItemSel = false;
		_filteredSel = _byUsernameSel = -1;
	}
}

void ContactsInner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
}

void ContactsInner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (e->button() == Qt::LeftButton) {
		chooseParticipant();
	}
}

void ContactsInner::chooseParticipant() {
	if (_saving) return;
	bool addingAdmin = (_channel && _membersFilter == MembersFilterAdmins);
	if (!addingAdmin && (_chat || _creating != CreatingGroupNone)) {
		_time = unixtime();
		if (_filter.isEmpty()) {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsername.size()) {
				if (d_byUsername[_byUsernameSel]->inchat) return;
				changeCheckState(d_byUsername[_byUsernameSel], _byUsername[_byUsernameSel]);
			} else {
				if (!_sel || contactData(_sel)->inchat) return;
				changeCheckState(_sel);
			}
		} else {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsernameFiltered.size()) {
				if (d_byUsernameFiltered[_byUsernameSel]->inchat) return;
				changeCheckState(d_byUsernameFiltered[_byUsernameSel], _byUsernameFiltered[_byUsernameSel]);

				ContactData *moving = d_byUsernameFiltered[_byUsernameSel];
				int32 i = 0, l = d_byUsername.size();
				for (; i < l; ++i) {
					if (d_byUsername[i] == moving) {
						break;
					}
				}
				if (i == l) {
					d_byUsername.push_back(moving);
					_byUsername.push_back(_byUsernameFiltered[_byUsernameSel]);
					for (i = 0, l = _byUsernameDatas.size(); i < l;) {
						if (_byUsernameDatas[i] == moving) {
							_byUsernameDatas.removeAt(i);
							--l;
						} else {
							++i;
						}
					}
				}
			} else {
				if (_filteredSel < 0 || _filteredSel >= _filtered.size() || contactData(_filtered[_filteredSel])->inchat) return;
				changeCheckState(_filtered[_filteredSel]);
			}
			emit selectAllQuery();
		}
	} else {
		PeerData *peer = 0;
		if (_filter.isEmpty()) {
			if (_newItemSel) {
				emit addRequested();
				return;
			}
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsername.size()) {
				peer = _byUsername[_byUsernameSel];
			} else if (_sel) {
				peer = _sel->history()->peer;
			}
		} else {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsernameFiltered.size()) {
				peer = _byUsernameFiltered[_byUsernameSel];
			} else {
				if (_filteredSel < 0 || _filteredSel >= _filtered.size()) return;
				peer = _filtered[_filteredSel]->history()->peer;
			}
		}
		if (peer) {
			if (addingAdmin) {
				_addAdmin = peer->asUser();
				if (_addAdminRequestId) {
					MTP::cancel(_addAdminRequestId);
					_addAdminRequestId = 0;
				}
				if (_addAdminBox) _addAdminBox->deleteLater();
				_addAdminBox = new ConfirmBox(lng_channel_admin_sure(lt_user, _addAdmin->firstName));
				connect(_addAdminBox, SIGNAL(confirmed()), this, SLOT(onAddAdmin()));
				connect(_addAdminBox, SIGNAL(destroyed(QObject*)), this, SLOT(onNoAddAdminBox(QObject*)));
				Ui::showLayer(_addAdminBox, KeepOtherLayers);
			} else if (bot() && (peer->isChat() || peer->isMegagroup())) {
				_addToPeer = peer;
				ConfirmBox *box = new ConfirmBox(lng_bot_sure_invite(lt_group, peer->name));
				connect(box, SIGNAL(confirmed()), this, SLOT(onAddBot()));
				Ui::showLayer(box, KeepOtherLayers);
			} else {
				App::wnd()->hideSettings(true);
				App::main()->choosePeer(peer->id, ShowAtUnreadMsgId);
				Ui::hideLayer();
			}
		}
	}
	update();
}

void ContactsInner::changeCheckState(Dialogs::Row *row) {
	changeCheckState(contactData(row), row->history()->peer);
}

void ContactsInner::changeCheckState(ContactData *data, PeerData *peer) {
	int32 cnt = _selCount;
	if (data->check) {
		data->check = false;
		_checkedContacts.remove(peer);
		--_selCount;
	} else if (selectedCount() < ((_channel && _channel->isMegagroup()) ? Global::MegagroupSizeMax() : Global::ChatSizeMax())) {
		data->check = true;
		_checkedContacts.insert(peer, true);
		++_selCount;
	} else if (_channel && !_channel->isMegagroup()) {
		Ui::showLayer(new MaxInviteBox(_channel->inviteLink()), KeepOtherLayers);
	} else if (!_channel && selectedCount() >= Global::ChatSizeMax() && selectedCount() < Global::MegagroupSizeMax()) {
		Ui::showLayer(new InformBox(lng_profile_add_more_after_upgrade(lt_count, Global::MegagroupSizeMax())), KeepOtherLayers);
	}
	if (cnt != _selCount) emit chosenChanged();
}

int32 ContactsInner::selectedCount() const {
	int32 result = _selCount;
	if (_chat) {
		result += qMax(_chat->count, 1);
	} else if (_channel) {
		result += qMax(_channel->membersCount(), _already.size());
	} else if (_creating == CreatingGroupGroup) {
		result += 1;
	}
	return result;
}

void ContactsInner::updateSel() {
	if (!_mouseSel) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	if (_filter.isEmpty()) {
		bool newItemSel = false;
		if (_newItemHeight) {
			if (in && (p.y() >= 0) && (p.y() < _newItemHeight) && !(_chat && _membersFilter == MembersFilterAdmins)) {
				newItemSel = true;
			}
			p.setY(p.y() - _newItemHeight);
		}
		Dialogs::Row *newSel = (in && !newItemSel && (p.y() >= 0) && (p.y() < _contacts->size() * _rowHeight)) ? _contacts->rowAtY(p.y(), _rowHeight) : nullptr;
		int32 byUsernameSel = (in && !newItemSel && p.y() >= _contacts->size() * _rowHeight + st::searchedBarHeight) ? ((p.y() - _contacts->size() * _rowHeight - st::searchedBarHeight) / _rowHeight) : -1;
		if (byUsernameSel >= _byUsername.size()) byUsernameSel = -1;
		if (newSel != _sel || byUsernameSel != _byUsernameSel || newItemSel != _newItemSel) {
			updateSelectedRow();
			_newItemSel = newItemSel;
			_sel = newSel;
			_byUsernameSel = byUsernameSel;
			updateSelectedRow();
		}
	} else {
		int32 newFilteredSel = (in && p.y() >= 0 && p.y() < _filtered.size() * _rowHeight) ? (p.y() / _rowHeight) : -1;
		int32 byUsernameSel = (in && p.y() >= _filtered.size() * _rowHeight + st::searchedBarHeight) ? ((p.y() - _filtered.size() * _rowHeight - st::searchedBarHeight) / _rowHeight) : -1;
		if (byUsernameSel >= _byUsernameFiltered.size()) byUsernameSel = -1;
		if (newFilteredSel != _filteredSel || byUsernameSel != _byUsernameSel) {
			updateSelectedRow();
			_filteredSel = newFilteredSel;
			_byUsernameSel = byUsernameSel;
			updateSelectedRow();
		}
	}
}

void ContactsInner::updateFilter(QString filter) {
	_lastQuery = filter.toLower().trimmed();
	filter = textSearchKey(filter);

	_time = unixtime();
	QStringList f;
	if (!filter.isEmpty()) {
		QStringList filterList = filter.split(cWordSplit(), QString::SkipEmptyParts);
		int l = filterList.size();

		f.reserve(l);
		for (int i = 0; i < l; ++i) {
			QString filterName = filterList[i].trimmed();
			if (filterName.isEmpty()) continue;
			f.push_back(filterName);
		}
		filter = f.join(' ');
	}
	if (_filter != filter) {
		_filter = filter;

		_byUsernameFiltered.clear();
		d_byUsernameFiltered.clear();
		for (int i = 0, l = _byUsernameDatas.size(); i < l; ++i) {
			delete _byUsernameDatas[i];
		}
		_byUsernameDatas.clear();

		if (_filter.isEmpty()) {
			_sel = 0;
			refresh();
		} else {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			if (!_allAdmins.isHidden()) _allAdmins.hide();
			QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

			_filtered.clear();
			if (!f.isEmpty()) {
				const Dialogs::List *toFilter = nullptr;
				if (!_contacts->isEmpty()) {
					for (fi = fb; fi != fe; ++fi) {
						auto found = _contacts->filtered(fi->at(0));
						if (found->isEmpty()) {
							toFilter = nullptr;
							break;
						}
						if (!toFilter || toFilter->size() > found->size()) {
							toFilter = found;
						}
					}
				}
				if (toFilter) {
					_filtered.reserve(toFilter->size());
					for_const (auto row, *toFilter) {
						const PeerData::Names &names(row->history()->peer->names);
						PeerData::Names::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
						for (fi = fb; fi != fe; ++fi) {
							QString filterName(*fi);
							for (ni = nb; ni != ne; ++ni) {
								if (ni->startsWith(*fi)) {
									break;
								}
							}
							if (ni == ne) {
								break;
							}
						}
						if (fi == fe) {
							row->attached = nullptr;
							_filtered.push_back(row);
						}
					}
				}

				_byUsernameFiltered.reserve(_byUsername.size());
				d_byUsernameFiltered.reserve(d_byUsername.size());
				for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
					const PeerData::Names &names(_byUsername[i]->names);
					PeerData::Names::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
					for (fi = fb; fi != fe; ++fi) {
						QString filterName(*fi);
						for (ni = nb; ni != ne; ++ni) {
							if (ni->startsWith(*fi)) {
								break;
							}
						}
						if (ni == ne) {
							break;
						}
					}
					if (fi == fe) {
						_byUsernameFiltered.push_back(_byUsername[i]);
						d_byUsernameFiltered.push_back(d_byUsername[i]);
					}
				}
			}
			_filteredSel = -1;
			if (!_filtered.isEmpty()) {
				for (_filteredSel = 0; (_filteredSel < _filtered.size()) && contactData(_filtered[_filteredSel])->inchat;) {
					++_filteredSel;
				}
				if (_filteredSel == _filtered.size()) _filteredSel = -1;
			}
			_byUsernameSel = -1;
			if (_filteredSel < 0 && !_byUsernameFiltered.isEmpty()) {
				for (_byUsernameSel = 0; (_byUsernameSel < _byUsernameFiltered.size()) && d_byUsernameFiltered[_byUsernameSel]->inchat;) {
					++_byUsernameSel;
				}
				if (_byUsernameSel == _byUsernameFiltered.size()) _byUsernameSel = -1;
			}
			_mouseSel = false;
			refresh();

			if (!bot() && (!_chat || _membersFilter != MembersFilterAdmins)) {
				_searching = true;
				emit searchByUsername();
			}
		}
		update();
		loadProfilePhotos(0);
	}
}

void ContactsInner::onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow) {
	if (!_filter.isEmpty()) {
		for (FilteredDialogs::iterator i = _filtered.begin(), e = _filtered.end(); i != e;) {
			if (*i == oldRow) { // this row is shown in filtered and maybe is in contacts!
				if (newRow) {
					*i = newRow;
					++i;
				} else {
					i = _filtered.erase(i);
				}
			} else {
				++i;
			}
		}
		if (_filteredSel >= _filtered.size()) {
			_filteredSel = -1;
		}
	} else {
		if (_sel == oldRow) {
			_sel = newRow;
		}
	}
	_mouseSel = false;
	int cnt = (_filter.isEmpty() ? _contacts->size() : _filtered.size());
	int newh = cnt ? (cnt * _rowHeight) : st::noContactsHeight;
	resize(width(), newh);
}

void ContactsInner::peopleReceived(const QString &query, const QVector<MTPPeer> &people) {
	_lastQuery = query.toLower().trimmed();
	if (_lastQuery.at(0) == '@') _lastQuery = _lastQuery.mid(1);
	int32 already = _byUsernameFiltered.size();
	_byUsernameFiltered.reserve(already + people.size());
	d_byUsernameFiltered.reserve(already + people.size());
	for (QVector<MTPPeer>::const_iterator i = people.cbegin(), e = people.cend(); i != e; ++i) {
		PeerId peerId = peerFromMTP(*i);
		int32 j = 0;
		for (; j < already; ++j) {
			if (_byUsernameFiltered[j]->id == peerId) break;
		}
		if (j == already) {
			PeerData *p = App::peer(peerId);
			if (!p) continue;

			if (_channel || _chat || _creating != CreatingGroupNone) {
				if (p->isUser()) {
					if (p->asUser()->botInfo) {
						if (_chat || _creating == CreatingGroupGroup) { // skip bot's that can't be invited to groups
							if (p->asUser()->botInfo->cantJoinGroups) continue;
						}
						if (_channel) {
							if (!_channel->isMegagroup() && _membersFilter != MembersFilterAdmins) continue;
						}
					}
				} else {
					continue; // skip
				}
			}

			ContactData *d = new ContactData();
			_byUsernameDatas.push_back(d);
			d->inchat = _chat ? _chat->participants.contains(p->asUser()) : ((_creating == CreatingGroupGroup || _channel) ? (p == App::self()) : false);
			d->check = _checkedContacts.contains(p);
			d->name.setText(st::contactsNameFont, p->name, _textNameOptions);
			d->online = '@' + p->userName();

			_byUsernameFiltered.push_back(p);
			d_byUsernameFiltered.push_back(d);
		}
	}
	_searching = false;
	refresh();
}

void ContactsInner::refresh() {
	if (_filter.isEmpty()) {
		if (_chat && _membersFilter == MembersFilterAdmins) {
			if (_allAdmins.isHidden()) _allAdmins.show();
		} else {
			if (!_allAdmins.isHidden()) _allAdmins.hide();
		}
		if (!_contacts->isEmpty() || !_byUsername.isEmpty()) {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			resize(width(), _newItemHeight + (_contacts->size() * _rowHeight) + (_byUsername.isEmpty() ? 0 : (st::searchedBarHeight + _byUsername.size() * _rowHeight)));
		} else if (_chat && _membersFilter == MembersFilterAdmins) {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			resize(width(), _newItemHeight + st::noContactsHeight);
		} else {
			if (cContactsReceived() && !bot()) {
				if (_addContactLnk.isHidden()) _addContactLnk.show();
			} else {
				if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			}
			resize(width(), st::noContactsHeight);
		}
	} else {
		if (!_allAdmins.isHidden()) _allAdmins.hide();
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			resize(width(), st::noContactsHeight);
		} else {
			resize(width(), (_filtered.size() * _rowHeight) + (_byUsernameFiltered.isEmpty() ? 0 : (st::searchedBarHeight + _byUsernameFiltered.size() * _rowHeight)));
		}
	}
	update();
}

ChatData *ContactsInner::chat() const {
	return _chat;
}

ChannelData *ContactsInner::channel() const {
	return _channel;
}

MembersFilter ContactsInner::membersFilter() const {
	return _membersFilter;
}

UserData *ContactsInner::bot() const {
	return _bot;
}

CreatingGroupType ContactsInner::creating() const {
	return _creating;
}

ContactsInner::~ContactsInner() {
	for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
		delete *i;
	}
	if (_bot || (_chat && _membersFilter == MembersFilterAdmins)) {
		if (_bot && _bot->botInfo) _bot->botInfo->startGroupToken = QString();
	}
}

void ContactsInner::resizeEvent(QResizeEvent *e) {
	_addContactLnk.move((width() - _addContactLnk.width()) / 2, (st::noContactsHeight + st::noContactsFont->height) / 2);
	_allAdmins.moveToLeft(st::contactsPadding.left(), st::contactsNewItemTop);
}

void ContactsInner::selectSkip(int32 dir) {
	_time = unixtime();
	_mouseSel = false;
	if (_filter.isEmpty()) {
		int cur = 0;
		if (_sel) {
			for (auto i = _contacts->cbegin(); *i != _sel; ++i) {
				++cur;
			}
			if (_newItemHeight) ++cur;
		} else if (_byUsernameSel >= 0) {
			cur = (_contacts->size() + _byUsernameSel);
			if (_newItemHeight) ++cur;
		} else if (!_newItemSel) {
			cur = -1;
		}
		cur += dir;
		if (cur <= 0) {
			_newItemSel = (_chat && _membersFilter == MembersFilterAdmins) ? false : (_newItemHeight ? true : false);
			_sel = (!_newItemHeight && !_contacts->isEmpty()) ? *_contacts->cbegin() : nullptr;
			_byUsernameSel = (!_newItemHeight && _contacts->isEmpty() && !_byUsername.isEmpty()) ? 0 : -1;
		} else if (cur >= _contacts->size() + (_newItemHeight ? 1 : 0)) {
			_newItemSel = false;
			if (_byUsername.isEmpty()) {
				_sel = _contacts->isEmpty() ? nullptr : *(_contacts->cend() - 1);
				_byUsernameSel = -1;
			} else {
				_sel = nullptr;
				_byUsernameSel = cur - _contacts->size();
				if (_byUsernameSel >= _byUsername.size()) _byUsernameSel = _byUsername.size() - 1;
			}
		} else {
			_newItemSel = false;
			if (_newItemHeight) --cur;
			for (auto i = _contacts->cbegin(); ; ++i) {
				_sel = *i;
				if (!cur) {
					break;
				} else {
					--cur;
				}
			}
			_byUsernameSel = -1;
		}
		if (dir > 0) {
			for (auto i = _contacts->cfind(_sel), end = _contacts->cend(); i != end && contactData(*i)->inchat; ++i) {
				_sel = *i;
			}
			if (_sel && contactData(_sel)->inchat) {
				_sel = nullptr;
			}
			if (!_sel) {
				if (!_byUsername.isEmpty()) {
					if (_byUsernameSel < 0) _byUsernameSel = 0;
					for (; _byUsernameSel < _byUsername.size() && d_byUsername[_byUsernameSel]->inchat;) {
						++_byUsernameSel;
					}
					if (_byUsernameSel == _byUsername.size()) _byUsernameSel = -1;
				}
			}
		} else {
			while (_byUsernameSel >= 0 && d_byUsername[_byUsernameSel]->inchat) {
				--_byUsernameSel;
			}
			if (_byUsernameSel < 0) {
				if (!_contacts->isEmpty()) {
					if (!_newItemSel && !_sel) _sel = *(_contacts->cend() - 1);
					if (_sel) {
						for (auto i = _contacts->cfind(_sel), b = _contacts->cbegin(); i != b && contactData(*i)->inchat; --i) {
							_sel = *i;
						}
						if (contactData(_sel)->inchat) {
							_sel = nullptr;
						}
					}
				}
			}
		}
		if (_newItemSel) {
			emit mustScrollTo(0, _newItemHeight);
		} else if (_sel) {
			emit mustScrollTo(_newItemHeight + _sel->pos() * _rowHeight, _newItemHeight + (_sel->pos() + 1) * _rowHeight);
		} else if (_byUsernameSel >= 0) {
			emit mustScrollTo(_newItemHeight + (_contacts->size() + _byUsernameSel) * _rowHeight + st::searchedBarHeight, _newItemHeight + (_contacts->size() + _byUsernameSel + 1) * _rowHeight + st::searchedBarHeight);
		}
	} else {
		int cur = (_filteredSel >= 0) ? _filteredSel : ((_byUsernameSel >= 0) ? (_filtered.size() + _byUsernameSel) : -1);
		cur += dir;
		if (cur <= 0) {
			_filteredSel = _filtered.isEmpty() ? -1 : 0;
			_byUsernameSel = (_filtered.isEmpty() && !_byUsernameFiltered.isEmpty()) ? 0 : -1;
		} else if (cur >= _filtered.size()) {
			_filteredSel = -1;
			_byUsernameSel = cur - _filtered.size();
			if (_byUsernameSel >= _byUsernameFiltered.size()) _byUsernameSel = _byUsernameFiltered.size() - 1;
		} else {
			_filteredSel = cur;
			_byUsernameSel = -1;
		}
		if (dir > 0) {
			while (_filteredSel >= 0 && _filteredSel < _filtered.size() && contactData(_filtered[_filteredSel])->inchat) {
				++_filteredSel;
			}
			if (_filteredSel < 0 || _filteredSel >= _filtered.size()) {
				_filteredSel = -1;
				if (!_byUsernameFiltered.isEmpty()) {
					if (_byUsernameSel < 0) _byUsernameSel = 0;
					for (; _byUsernameSel < _byUsernameFiltered.size() && d_byUsernameFiltered[_byUsernameSel]->inchat;) {
						++_byUsernameSel;
					}
					if (_byUsernameSel == _byUsernameFiltered.size()) _byUsernameSel = -1;
				}
			}
		} else {
			while (_byUsernameSel >= 0 && d_byUsernameFiltered[_byUsernameSel]->inchat) {
				--_byUsernameSel;
			}
			if (_byUsernameSel < 0) {
				if (!_filtered.isEmpty()) {
					if (_filteredSel < 0) _filteredSel = _filtered.size() - 1;
					for (; _filteredSel >= 0 && contactData(_filtered[_filteredSel])->inchat;) {
						--_filteredSel;
					}
				}
			}
		}
		if (_filteredSel >= 0) {
			emit mustScrollTo(_filteredSel * _rowHeight, (_filteredSel + 1) * _rowHeight);
		} else if (_byUsernameSel >= 0) {
			int skip = _filtered.size() * _rowHeight + st::searchedBarHeight;
			emit mustScrollTo(skip + _byUsernameSel * _rowHeight, skip + (_byUsernameSel + 1) * _rowHeight);
		}
	}
	update();
}

void ContactsInner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

QVector<UserData*> ContactsInner::selected() {
	QVector<UserData*> result;
	for_const (auto row, *_contacts) {
		if (_checkedContacts.contains(row->history()->peer)) {
			contactData(row); // fill _contactsData
		}
	}
	result.reserve(_contactsData.size());
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check && i.key()->isUser()) {
			result.push_back(i.key()->asUser());
		}
	}
	for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->check && _byUsername[i]->isUser()) {
			result.push_back(_byUsername[i]->asUser());
		}
	}
	return result;
}

QVector<MTPInputUser> ContactsInner::selectedInputs() {
	QVector<MTPInputUser> result;
	for_const (auto row, *_contacts) {
		if (_checkedContacts.contains(row->history()->peer)) {
			contactData(row); // fill _contactsData
		}
	}
	result.reserve(_contactsData.size());
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check && i.key()->isUser()) {
			result.push_back(i.key()->asUser()->inputUser);
		}
	}
	for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->check && _byUsername[i]->isUser()) {
			result.push_back(_byUsername[i]->asUser()->inputUser);
		}
	}
	return result;
}

PeerData *ContactsInner::selectedUser() {
	for_const (auto row, *_contacts) {
		if (_checkedContacts.contains(row->history()->peer)) {
			contactData(row); // fill _contactsData
		}
	}
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check) {
			return i.key();
		}
	}
	for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->check) {
			return _byUsername[i];
		}
	}
	return 0;
}

ContactsBox::ContactsBox() : ItemListBox(st::contactsScroll)
, _inner(CreatingGroupNone)
, _filter(this, st::boxSearchField, lang(lng_participant_filter))
, _filterCancel(this, st::boxSearchCancel)
, _next(this, lang(lng_create_group_next), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(0)
, _saveRequestId(0) {
	init();
}

ContactsBox::ContactsBox(const QString &name, const QImage &photo) : ItemListBox(st::boxScroll)
, _inner(CreatingGroupGroup)
, _filter(this, st::boxSearchField, lang(lng_participant_filter))
, _filterCancel(this, st::boxSearchCancel)
, _next(this, lang(lng_create_group_create), st::defaultBoxButton)
, _cancel(this, lang(lng_create_group_back), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(0)
, _saveRequestId(0)
, _creationName(name)
, _creationPhoto(photo) {
	init();
}

ContactsBox::ContactsBox(ChannelData *channel) : ItemListBox(st::boxScroll)
, _inner(channel, MembersFilterRecent, MembersAlreadyIn())
, _filter(this, st::boxSearchField, lang(lng_participant_filter))
, _filterCancel(this, st::boxSearchCancel)
, _next(this, lang(lng_participant_invite), st::defaultBoxButton)
, _cancel(this, lang(lng_create_group_skip), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(0)
, _saveRequestId(0) {
	init();
}

ContactsBox::ContactsBox(ChannelData *channel, MembersFilter filter, const MembersAlreadyIn &already) : ItemListBox((filter == MembersFilterAdmins) ? st::contactsScroll : st::boxScroll)
, _inner(channel, filter, already)
, _filter(this, st::boxSearchField, lang(lng_participant_filter))
, _filterCancel(this, st::boxSearchCancel)
, _next(this, lang(lng_participant_invite), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(0)
, _saveRequestId(0) {
	init();
}

ContactsBox::ContactsBox(ChatData *chat, MembersFilter filter) : ItemListBox(st::boxScroll)
, _inner(chat, filter)
, _filter(this, st::boxSearchField, lang(lng_participant_filter))
, _filterCancel(this, st::boxSearchCancel)
, _next(this, lang((filter == MembersFilterAdmins) ? lng_settings_save : lng_participant_invite), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(0)
, _saveRequestId(0) {
	init();
}

ContactsBox::ContactsBox(UserData *bot) : ItemListBox(st::contactsScroll)
, _inner(bot)
, _filter(this, st::boxSearchField, lang(lng_participant_filter))
, _filterCancel(this, st::boxSearchCancel)
, _next(this, lang(lng_create_group_next), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(0)
, _saveRequestId(0) {
	init();
}

void ContactsBox::init() {
	bool inviting = (_inner.creating() == CreatingGroupGroup) || (_inner.channel() && _inner.membersFilter() == MembersFilterRecent) || _inner.chat();
	int32 topSkip = st::boxTitleHeight + _filter.height();
	int32 bottomSkip = inviting ? (st::boxButtonPadding.top() + _next.height() + st::boxButtonPadding.bottom()) : st::boxScrollSkip;
	ItemListBox::init(&_inner, bottomSkip, topSkip);

	connect(&_inner, SIGNAL(chosenChanged()), this, SLOT(onChosenChanged()));
	connect(&_inner, SIGNAL(addRequested()), App::wnd(), SLOT(onShowAddContact()));
	if (_inner.channel() && _inner.membersFilter() == MembersFilterAdmins) {
		_next.hide();
		_cancel.hide();
	} else if (_inner.chat() && _inner.membersFilter() == MembersFilterAdmins) {
		connect(&_next, SIGNAL(clicked()), this, SLOT(onSaveAdmins()));
		_bottomShadow = new ScrollableBoxShadow(this);
	} else if (_inner.chat() || _inner.channel()) {
		connect(&_next, SIGNAL(clicked()), this, SLOT(onInvite()));
		_bottomShadow = new ScrollableBoxShadow(this);
	} else if (_inner.creating() != CreatingGroupNone) {
		connect(&_next, SIGNAL(clicked()), this, SLOT(onCreate()));
		_bottomShadow = new ScrollableBoxShadow(this);
	} else {
		_next.hide();
		_cancel.hide();
	}
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_filter, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_filterCancel, SIGNAL(clicked()), this, SLOT(onFilterCancel()));
	connect(&_inner, SIGNAL(mustScrollTo(int, int)), &_scroll, SLOT(scrollToY(int, int)));
	connect(&_inner, SIGNAL(selectAllQuery()), &_filter, SLOT(selectAll()));
	connect(&_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));
	connect(&_inner, SIGNAL(adminAdded()), this, SIGNAL(adminAdded()));

	_filterCancel.setAttribute(Qt::WA_OpaquePaintEvent);

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	prepare();
}

bool ContactsBox::onSearchByUsername(bool searchCache) {
	QString q = _filter.getLastText().trimmed();
	if (q.isEmpty()) {
		if (_peopleRequest) {
			_peopleRequest = 0;
		}
		return true;
	}
	if (q.size() >= MinUsernameLength) {
		if (searchCache) {
			PeopleCache::const_iterator i = _peopleCache.constFind(q);
			if (i != _peopleCache.cend()) {
				_peopleQuery = q;
				_peopleRequest = 0;
				peopleReceived(i.value(), 0);
				return true;
			}
		} else if (_peopleQuery != q) {
			_peopleQuery = q;
			_peopleFull = false;
			_peopleRequest = MTP::send(MTPcontacts_Search(MTP_string(_peopleQuery), MTP_int(SearchPeopleLimit)), rpcDone(&ContactsBox::peopleReceived), rpcFail(&ContactsBox::peopleFailed));
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void ContactsBox::onNeedSearchByUsername() {
	if (!onSearchByUsername(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void ContactsBox::peopleReceived(const MTPcontacts_Found &result, mtpRequestId req) {
	QString q = _peopleQuery;

	PeopleQueries::iterator i = _peopleQueries.find(req);
	if (i != _peopleQueries.cend()) {
		q = i.value();
		_peopleCache[q] = result;
		_peopleQueries.erase(i);
	}

	if (_peopleRequest == req) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			App::feedUsers(result.c_contacts_found().vusers);
			App::feedChats(result.c_contacts_found().vchats);
			_inner.peopleReceived(q, result.c_contacts_found().vresults.c_vector().v);
		} break;
		}

		_peopleRequest = 0;
		_inner.updateSel();
		onScroll();
	}
}

bool ContactsBox::peopleFailed(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_peopleRequest == req) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
	return true;
}

void ContactsBox::hideAll() {
	_filter.hide();
	_filterCancel.hide();
	_next.hide();
	_cancel.hide();
	_topShadow.hide();
	if (_bottomShadow) _bottomShadow->hide();
	ItemListBox::hideAll();
}

void ContactsBox::showAll() {
	_filter.show();
	if (_filter.getLastText().isEmpty()) {
		_filterCancel.hide();
	} else {
		_filterCancel.show();
	}
	if (_inner.channel() && _inner.membersFilter() == MembersFilterAdmins) {
		_next.hide();
		_cancel.hide();
	} else if (_inner.chat() || _inner.channel()) {
		_next.show();
		_cancel.show();
	} else if (_inner.creating() != CreatingGroupNone) {
		_next.show();
		_cancel.show();
	} else {
		_next.hide();
		_cancel.hide();
	}
	_topShadow.show();
	if (_bottomShadow) _bottomShadow->show();
	ItemListBox::showAll();
}

void ContactsBox::showDone() {
	_filter.setFocus();
}

void ContactsBox::onSubmit() {
	_inner.chooseParticipant();
}

void ContactsBox::keyPressEvent(QKeyEvent *e) {
	if (_filter.hasFocus()) {
		if (e->key() == Qt::Key_Down) {
			_inner.selectSkip(1);
		} else if (e->key() == Qt::Key_Up) {
			_inner.selectSkip(-1);
		} else if (e->key() == Qt::Key_PageDown) {
			_inner.selectSkipPage(_scroll.height(), 1);
		} else if (e->key() == Qt::Key_PageUp) {
			_inner.selectSkipPage(_scroll.height(), -1);
		} else {
			ItemListBox::keyPressEvent(e);
		}
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void ContactsBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	bool addingAdmin = _inner.channel() && _inner.membersFilter() == MembersFilterAdmins;
	if (_inner.chat() && _inner.membersFilter() == MembersFilterAdmins) {
		paintTitle(p, lang(lng_channel_admins));
	} else if (_inner.chat() || _inner.creating() != CreatingGroupNone) {
		QString title(lang(addingAdmin ? lng_channel_add_admin : lng_profile_add_participant));
		QString additional((addingAdmin || (_inner.channel() && !_inner.channel()->isMegagroup())) ? QString() : QString("%1 / %2").arg(_inner.selectedCount()).arg(Global::MegagroupSizeMax()));
		paintTitle(p, title, additional);
	} else if (_inner.bot()) {
		paintTitle(p, lang(lng_bot_choose_group));
	} else {
		paintTitle(p, lang(lng_contacts_header));
	}
}

void ContactsBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	_filter.resize(width(), _filter.height());
	_filter.moveToLeft(0, st::boxTitleHeight);
	_filterCancel.moveToRight(0, st::boxTitleHeight);
	_inner.resize(width(), _inner.height());
	_next.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _next.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _next.width() + st::boxButtonPadding.left(), _next.y());
	_topShadow.setGeometry(0, st::boxTitleHeight + _filter.height(), width(), st::lineWidth);
	if (_bottomShadow) _bottomShadow->setGeometry(0, height() - st::boxButtonPadding.bottom() - _next.height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
}

void ContactsBox::closePressed() {
	if (_inner.channel() && !_inner.hasAlreadyMembersInChannel()) {
		Ui::showPeerHistory(_inner.channel(), ShowAtTheEndMsgId);
	}
}

void ContactsBox::onFilterCancel() {
	_filter.setText(QString());
}

void ContactsBox::onFilterUpdate() {
	_scroll.scrollToY(0);
	if (_filter.getLastText().isEmpty()) {
		_filterCancel.hide();
	} else {
		_filterCancel.show();
	}
	_inner.updateFilter(_filter.getLastText());
}

void ContactsBox::onChosenChanged() {
	update();
}

void ContactsBox::onInvite() {
	QVector<UserData*> users(_inner.selected());
	if (users.isEmpty()) {
		_filter.setFocus();
		_filter.showError();
		return;
	}

	App::main()->addParticipants(_inner.chat() ? (PeerData*)_inner.chat() : _inner.channel(), users);
	if (_inner.chat()) {
		Ui::hideLayer();
		Ui::showPeerHistory(_inner.chat(), ShowAtTheEndMsgId);
	} else {
		onClose();
	}
}

void ContactsBox::onCreate() {
	if (_saveRequestId) return;

	MTPVector<MTPInputUser> users(MTP_vector<MTPInputUser>(_inner.selectedInputs()));
	const auto &v(users.c_vector().v);
	if (v.isEmpty() || (v.size() == 1 && v.at(0).type() == mtpc_inputUserSelf)) {
		_filter.setFocus();
		_filter.showError();
		return;
	}
	_saveRequestId = MTP::send(MTPmessages_CreateChat(MTP_vector<MTPInputUser>(v), MTP_string(_creationName)), rpcDone(&ContactsBox::creationDone), rpcFail(&ContactsBox::creationFail));
}

void ContactsBox::onSaveAdmins() {
	if (_saveRequestId) return;

	_inner.saving(true);
	_saveRequestId = MTP::send(MTPmessages_ToggleChatAdmins(_inner.chat()->inputChat, MTP_bool(!_inner.allAdmins())), rpcDone(&ContactsBox::saveAdminsDone), rpcFail(&ContactsBox::saveAdminsFail));
}

void ContactsBox::saveAdminsDone(const MTPUpdates &result) {
	App::main()->sentUpdatesReceived(result);
	saveSelectedAdmins();
}

void ContactsBox::saveSelectedAdmins() {
	if (_inner.allAdmins() && !_inner.chat()->participants.isEmpty()) {
		onClose();
	} else {
		_saveRequestId = MTP::send(MTPmessages_GetFullChat(_inner.chat()->inputChat), rpcDone(&ContactsBox::getAdminsDone), rpcFail(&ContactsBox::saveAdminsFail));
	}
}

void ContactsBox::getAdminsDone(const MTPmessages_ChatFull &result) {
	App::api()->processFullPeer(_inner.chat(), result);
	if (_inner.allAdmins()) {
		onClose();
		return;
	}
	ChatData::Admins curadmins = _inner.chat()->admins;
	QVector<UserData*> newadmins = _inner.selected(), appoint;
	if (!newadmins.isEmpty()) {
		appoint.reserve(newadmins.size());
		for (int32 i = 0, l = newadmins.size(); i < l; ++i) {
			ChatData::Admins::iterator c = curadmins.find(newadmins.at(i));
			if (c == curadmins.cend()) {
				if (newadmins.at(i)->id != peerFromUser(_inner.chat()->creator)) {
					appoint.push_back(newadmins.at(i));
				}
			} else {
				curadmins.erase(c);
			}
		}
	}
	_saveRequestId = 0;

	for_const (UserData *user, curadmins) {
		MTP::send(MTPmessages_EditChatAdmin(_inner.chat()->inputChat, user->inputUser, MTP_boolFalse()), rpcDone(&ContactsBox::removeAdminDone, user), rpcFail(&ContactsBox::editAdminFail), 0, 10);
	}
	for_const (UserData *user, appoint) {
		MTP::send(MTPmessages_EditChatAdmin(_inner.chat()->inputChat, user->inputUser, MTP_boolTrue()), rpcDone(&ContactsBox::setAdminDone, user), rpcFail(&ContactsBox::editAdminFail), 0, 10);
	}
	MTP::sendAnything();

	_saveRequestId = curadmins.size() + appoint.size();
	if (!_saveRequestId) {
		onClose();
	}
}

void ContactsBox::setAdminDone(UserData *user, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		if (_inner.chat()->noParticipantInfo()) {
			App::api()->requestFullPeer(_inner.chat());
		} else {
			_inner.chat()->admins.insert(user);
		}
	}
	--_saveRequestId;
	if (!_saveRequestId) {
		emit App::main()->peerUpdated(_inner.chat());
		onClose();
	}
}

void ContactsBox::removeAdminDone(UserData *user, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		_inner.chat()->admins.remove(user);
	}
	--_saveRequestId;
	if (!_saveRequestId) {
		emit App::main()->peerUpdated(_inner.chat());
		onClose();
	}
}

bool ContactsBox::saveAdminsFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return true;
	_saveRequestId = 0;
	_inner.saving(false);
	if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
		saveSelectedAdmins();
	}
	return false;
}

bool ContactsBox::editAdminFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return true;
	--_saveRequestId;
	_inner.chat()->invalidateParticipants();
	if (!_saveRequestId) {
		if (error.type() == qstr("USER_RESTRICTED")) {
			Ui::showLayer(new InformBox(lang(lng_cant_do_this)));
			return true;
		}
		onClose();
	}
	return false;
}

void ContactsBox::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

void ContactsBox::creationDone(const MTPUpdates &updates) {
	Ui::hideLayer();

	App::main()->sentUpdatesReceived(updates);
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	default: LOG(("API Error: unexpected update cons %1 (ContactsBox::creationDone)").arg(updates.type())); break;
	}

	PeerData *peer = 0;
	if (v && !v->isEmpty() && v->front().type() == mtpc_chat) {
		peer = App::chat(v->front().c_chat().vid.v);
		if (peer) {
			if (!_creationPhoto.isNull()) {
				App::app()->uploadProfilePhoto(_creationPhoto, peer->id);
			}
			Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
		}
	} else {
		LOG(("API Error: chat not found in updates (ContactsBox::creationDone)"));
	}
}

bool ContactsBox::creationFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	if (error.type() == "NO_CHAT_TITLE") {
		emit closed();
		return true;
	} else if (error.type() == "USERS_TOO_FEW") {
		_filter.setFocus();
		_filter.showError();
		return true;
	} else if (error.type() == "PEER_FLOOD") {
		Ui::showLayer(new InformBox(cantInviteError()), KeepOtherLayers);
		return true;
	} else if (error.type() == qstr("USER_RESTRICTED")) {
		Ui::showLayer(new InformBox(lang(lng_cant_do_this)));
		return true;
	}
	return false;
}

MembersInner::MembersInner(ChannelData *channel, MembersFilter filter) : TWidget()
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _newItemHeight((channel->amCreator() && (channel->membersCount() < (channel->isMegagroup() ? Global::MegagroupSizeMax() : Global::ChatSizeMax()) || (!channel->isMegagroup() && !channel->isPublic()) || filter == MembersFilterAdmins)) ? st::contactsNewItemHeight : 0)
, _newItemSel(false)
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
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(peerUpdated(PeerData*)));

	refresh();

	load();
}

void MembersInner::load() {
	if (!_loadingRequestId) {
		_loadingRequestId = MTP::send(MTPchannels_GetParticipants(_channel->inputChannel, (_filter == MembersFilterRecent) ? MTP_channelParticipantsRecent() : MTP_channelParticipantsAdmins(), MTP_int(0), MTP_int(Global::ChatSizeMax())), rpcDone(&MembersInner::membersReceived), rpcFail(&MembersInner::membersFailed));
	}
}

void MembersInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	_time = unixtime();
	p.fillRect(r, st::white->b);

	int32 yFrom = r.y() - st::membersPadding.top(), yTo = r.y() + r.height() - st::membersPadding.top();
	p.translate(0, st::membersPadding.top());
	if (_rows.isEmpty()) {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	} else {
		if (_newItemHeight) {
			p.fillRect(0, 0, width(), _newItemHeight, (_newItemSel ? st::contactsBgOver : st::white)->b);
			p.drawSpriteLeft(st::contactsNewItemIconPosition.x(), st::contactsNewItemIconPosition.y(), width(), st::contactsNewItemIcon);
			p.setFont(st::contactsNameFont);
			p.setPen(st::contactsNewItemFg);
			p.drawTextLeft(st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left(), st::contactsNewItemTop, width(), lang(_filter == MembersFilterAdmins ? lng_channel_add_admins : lng_channel_add_members));

			yFrom -= _newItemHeight;
			yTo -= _newItemHeight;
			p.translate(0, _newItemHeight);
		}
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
		if (to == _rows.size() && _filter == MembersFilterRecent && (_rows.size() < _channel->membersCount() || _rows.size() >= Global::ChatSizeMax())) {
			p.setPen(st::stickersReorderFg);
			_about.draw(p, st::contactsPadding.left(), st::stickersReorderPadding.top(), _aboutWidth, style::al_center);
		}
	}
}

void MembersInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void MembersInner::leaveEvent(QEvent *e) {
	_mouseSel = false;
	setMouseTracking(false);
	if (_sel >= 0) {
		clearSel();
	}
}

void MembersInner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
}

void MembersInner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (e->button() == Qt::LeftButton && _kickSel < 0) {
		chooseParticipant();
	}
	_kickDown = _kickSel;
	update();
}

void MembersInner::mouseReleaseEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (_kickDown >= 0 && _kickDown == _kickSel && !_kickRequestId) {
		_kickConfirm = _rows.at(_kickSel);
		if (_kickBox) _kickBox->deleteLater();
		_kickBox = new ConfirmBox((_filter == MembersFilterRecent ? (_channel->isMegagroup() ? lng_profile_sure_kick : lng_profile_sure_kick_channel) : lng_profile_sure_kick_admin)(lt_user, _kickConfirm->firstName));
		connect(_kickBox, SIGNAL(confirmed()), this, SLOT(onKickConfirm()));
		connect(_kickBox, SIGNAL(destroyed(QObject*)), this, SLOT(onKickBoxDestroyed(QObject*)));
		Ui::showLayer(_kickBox, KeepOtherLayers);
	}
	_kickDown = -1;
}

void MembersInner::onKickBoxDestroyed(QObject *obj) {
	if (_kickBox == obj) {
		_kickBox = 0;
	}
}

void MembersInner::onKickConfirm() {
	if (_filter == MembersFilterRecent) {
		_kickRequestId = MTP::send(MTPchannels_KickFromChannel(_channel->inputChannel, _kickConfirm->inputUser, MTP_bool(true)), rpcDone(&MembersInner::kickDone), rpcFail(&MembersInner::kickFail));
	} else {
		_kickRequestId = MTP::send(MTPchannels_EditAdmin(_channel->inputChannel, _kickConfirm->inputUser, MTP_channelRoleEmpty()), rpcDone(&MembersInner::kickAdminDone), rpcFail(&MembersInner::kickFail));
	}
}

void MembersInner::paintDialog(Painter &p, PeerData *peer, MemberData *data, bool sel, bool kickSel, bool kickDown) {
	UserData *user = peer->asUser();

	p.fillRect(0, 0, width(), _rowHeight, (sel ? st::contactsBgOver : st::white)->b);
	peer->paintUserpicLeft(p, st::contactsPhotoSize, st::contactsPadding.left(), st::contactsPadding.top(), width());

	p.setPen(st::black);

	int32 namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int32 namew = width() - namex - st::contactsPadding.right() - (data->canKick ? (_kickWidth + st::contactsCheckPosition.x() * 2) : 0);
	if (peer->isVerified()) {
		namew -= st::verifiedCheck.pxWidth() + st::verifiedCheckPos.x();
		p.drawSpriteLeft(namex + qMin(data->name.maxWidth(), namew) + st::verifiedCheckPos.x(), st::contactsPadding.top() + st::contactsNameTop + st::verifiedCheckPos.y(), width(), st::verifiedCheck);
	}
	data->name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	if (data->canKick) {
		p.setFont((kickSel ? st::linkOverFont : st::linkFont)->f);
		if (kickDown) {
			p.setPen(st::btnDefLink.downColor->p);
		} else {
			p.setPen(st::btnDefLink.color->p);
		}
		p.drawTextRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, width(), _kickText, _kickWidth);
	}

	p.setFont(st::contactsStatusFont->f);
	p.setPen(data->onlineColor ? st::contactsStatusFgOnline : (sel ? st::contactsStatusFgOver : st::contactsStatusFg));
	p.drawTextLeft(namex, st::contactsPadding.top() + st::contactsStatusTop, width(), data->online);
}

void MembersInner::selectSkip(int32 dir) {
	_time = unixtime();
	_mouseSel = false;

	int cur = -1;
	if (_newItemHeight && _newItemSel) {
		cur = 0;
	} else if (_sel >= 0) {
		cur = _sel + (_newItemHeight ? 1 : 0);
	}
	cur += dir;
	if (cur <= 0) {
		_newItemSel = _newItemHeight ? true : false;
		_sel = (_newItemSel || _rows.isEmpty()) ? -1 : 0;
	} else if (cur >= _rows.size() + (_newItemHeight ? 1 : 0)) {
		_sel = -1;
	} else {
		_sel = cur - (_newItemHeight ? 1 : 0);
	}
	if (dir > 0) {
		if (_sel < 0 || _sel >= _rows.size()) {
			_sel = -1;
		}
	} else {
		if (!_rows.isEmpty()) {
			if (_sel < 0 && !_newItemSel) _sel = _rows.size() - 1;
		}
	}
	if (_newItemSel) {
		emit mustScrollTo(0, _newItemHeight);
	} else if (_sel >= 0) {
		emit mustScrollTo(_newItemHeight + _sel * _rowHeight, _newItemHeight + (_sel + 1) * _rowHeight);
	}

	update();
}

void MembersInner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

void MembersInner::loadProfilePhotos(int32 yFrom) {
	int32 yTo = yFrom + (parentWidget() ? parentWidget()->height() : App::wnd()->height()) * 5;
	MTP::clearLoaderPriorities();

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	if (!_rows.isEmpty()) {
		int32 from = (yFrom - _newItemHeight) / _rowHeight;
		if (from < 0) from = 0;
		if (from < _rows.size()) {
			int32 to = ((yTo - _newItemHeight) / _rowHeight) + 1;
			if (to > _rows.size()) to = _rows.size();

			for (; from < to; ++from) {
				_rows[from]->loadUserpic();
			}
		}
	}
}

void MembersInner::chooseParticipant() {
	if (_newItemSel) {
		emit addRequested();
		return;
	}
	if (_sel < 0 || _sel >= _rows.size()) return;
	if (PeerData *peer = _rows[_sel]) {
		Ui::hideLayer();
		Ui::showPeerProfile(peer);
	}
}

void MembersInner::refresh() {
	if (_rows.isEmpty()) {
		resize(width(), st::membersPadding.top() + st::noContactsHeight + st::membersPadding.bottom());
		_aboutHeight = 0;
	} else {
		_about.setText(st::boxTextFont, lng_channel_only_last_shown(lt_count, _rows.size()));
		_aboutHeight = st::stickersReorderPadding.top() + _about.countHeight(_aboutWidth) + st::stickersReorderPadding.bottom();
		if (_filter != MembersFilterRecent || (_rows.size() >= _channel->membersCount() && _rows.size() < Global::ChatSizeMax())) {
			_aboutHeight = 0;
		}
		resize(width(), st::membersPadding.top() + _newItemHeight + _rows.size() * _rowHeight + st::membersPadding.bottom() + _aboutHeight);
	}
	update();
}

ChannelData *MembersInner::channel() const {
	return _channel;
}

MembersFilter MembersInner::filter() const {
	return _filter;
}

MembersAlreadyIn MembersInner::already() const {
	MembersAlreadyIn result;
	for_const (auto peer, _rows) {
		if (peer->isUser()) {
			result.insert(peer->asUser());
		}
	}
	return result;
}

void MembersInner::clearSel() {
	updateSelectedRow();
	_newItemSel = false;
	_sel = _kickSel = _kickDown = -1;
	_lastMousePos = QCursor::pos();
	updateSel();
}

MembersInner::MemberData *MembersInner::data(int32 index) {
	if (MemberData *result = _datas.at(index)) {
		return result;
	}
	MemberData *result = _datas[index] = new MemberData();
	result->name.setText(st::contactsNameFont, _rows[index]->name, _textNameOptions);
	int32 t = unixtime();
	result->online = App::onlineText(_rows[index], t);// lng_mediaview_date_time(lt_date, _dates[index].date().toString(qsl("dd.MM.yy")), lt_time, _dates[index].time().toString(cTimeFormat()));
	result->onlineColor = App::onlineColorUse(_rows[index], t);
	if (_filter == MembersFilterRecent) {
		result->canKick = (_channel->amCreator() || _channel->amEditor() || _channel->amModerator()) ? (_roles[index] == MemberRoleNone) : false;
	} else if (_filter == MembersFilterAdmins) {
		result->canKick = _channel->amCreator() ? (_roles[index] == MemberRoleEditor || _roles[index] == MemberRoleModerator) : false;
	} else {
		result->canKick = false;
	}
	return result;
}

void MembersInner::clear() {
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

MembersInner::~MembersInner() {
	clear();
}

void MembersInner::updateSel() {
	if (!_mouseSel) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	p.setY(p.y() - st::membersPadding.top());
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	bool newItemSel = (in && p.y() >= 0 && p.y() < _newItemHeight);
	int32 newSel = (in && !newItemSel && p.y() >= _newItemHeight && p.y() < _newItemHeight + _rows.size() * _rowHeight) ? ((p.y() - _newItemHeight) / _rowHeight) : -1;
	int32 newKickSel = newSel;
	if (newSel >= 0 && (!data(newSel)->canKick || !QRect(width() - _kickWidth - st::contactsPadding.right() - st::contactsCheckPosition.x(), _newItemHeight + newSel * _rowHeight + st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, _kickWidth, st::normalFont->height).contains(p))) {
		newKickSel = -1;
	}
	if (newSel != _sel || newKickSel != _kickSel || newItemSel != _newItemSel) {
		updateSelectedRow();
		_newItemSel = newItemSel;
		_sel = newSel;
		_kickSel = newKickSel;
		updateSelectedRow();
		setCursor(_kickSel >= 0 ? style::cur_pointer : style::cur_default);
	}
}

void MembersInner::peerUpdated(PeerData *peer) {
	update();
}

void MembersInner::updateSelectedRow() {
	if (_newItemSel) {
		update(0, st::membersPadding.top(), width(), _newItemHeight);
	}
	if (_sel >= 0) {
		update(0, st::membersPadding.top() + _newItemHeight + _sel * _rowHeight, width(), _rowHeight);
	}
}

void MembersInner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
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

void MembersInner::membersReceived(const MTPchannels_ChannelParticipants &result, mtpRequestId req) {
	clear();
	_loadingRequestId = 0;

	if (result.type() == mtpc_channels_channelParticipants) {
		const auto &d(result.c_channels_channelParticipants());
		const auto &v(d.vparticipants.c_vector().v);
		_rows.reserve(v.size());
		_datas.reserve(v.size());
		_dates.reserve(v.size());
		_roles.reserve(v.size());

		if (_filter == MembersFilterRecent && _channel->membersCount() < d.vcount.v) {
			_channel->setMembersCount(d.vcount.v);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		} else if (_filter == MembersFilterAdmins && _channel->adminsCount() < d.vcount.v) {
			_channel->setAdminsCount(d.vcount.v);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		}
		App::feedUsers(d.vusers);

		for (QVector<MTPChannelParticipant>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			int32 userId = 0, addedTime = 0;
			MemberRole role = MemberRoleNone;
			switch (i->type()) {
			case mtpc_channelParticipant:
				userId = i->c_channelParticipant().vuser_id.v;
				addedTime = i->c_channelParticipant().vdate.v;
				break;
			case mtpc_channelParticipantSelf:
				role = MemberRoleSelf;
				userId = i->c_channelParticipantSelf().vuser_id.v;
				addedTime = i->c_channelParticipantSelf().vdate.v;
				break;
			case mtpc_channelParticipantModerator:
				role = MemberRoleModerator;
				userId = i->c_channelParticipantModerator().vuser_id.v;
				addedTime = i->c_channelParticipantModerator().vdate.v;
				break;
			case mtpc_channelParticipantEditor:
				role = MemberRoleEditor;
				userId = i->c_channelParticipantEditor().vuser_id.v;
				addedTime = i->c_channelParticipantEditor().vdate.v;
				break;
			case mtpc_channelParticipantKicked:
				userId = i->c_channelParticipantKicked().vuser_id.v;
				addedTime = i->c_channelParticipantKicked().vdate.v;
				role = MemberRoleKicked;
				break;
			case mtpc_channelParticipantCreator:
				userId = i->c_channelParticipantCreator().vuser_id.v;
				addedTime = _channel->date;
				role = MemberRoleCreator;
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
		if (_filter == MembersFilterAdmins && _channel->isMegagroup() && _rows.size() < Global::ChatSizeMax()) {
			_channel->mgInfo->lastAdmins.clear();
			for (int32 i = 0, l = _rows.size(); i != l; ++i) {
				if (_roles.at(i) == MemberRoleCreator || _roles.at(i) == MemberRoleEditor) {
					_channel->mgInfo->lastAdmins.insert(_rows.at(i));
				}
			}

			Notify::peerUpdatedDelayed(_channel, Notify::PeerUpdate::Flag::AdminsChanged);
		}
	}
	if (_rows.isEmpty()) {
		_rows.push_back(App::self());
		_dates.push_back(date(MTP_int(_channel->date)));
		_roles.push_back(MemberRoleSelf);
		_datas.push_back(0);
	}

	clearSel();
	_loading = false;
	refresh();

	emit loaded();
}

bool MembersInner::membersFailed(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	Ui::hideLayer();
	return true;
}

void MembersInner::kickDone(const MTPUpdates &result, mtpRequestId req) {
	App::main()->sentUpdatesReceived(result);

	if (_kickRequestId != req) return;
	removeKicked();
	if (_kickBox) _kickBox->onClose();
}

void MembersInner::kickAdminDone(const MTPUpdates &result, mtpRequestId req) {
	if (_kickRequestId != req) return;
	if (App::main()) App::main()->sentUpdatesReceived(result);
	removeKicked();
	if (_kickBox) _kickBox->onClose();
}

bool MembersInner::kickFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_kickBox) _kickBox->onClose();
	load();
	return true;
}

void MembersInner::removeKicked() {
	_kickRequestId = 0;
	int32 index = _rows.indexOf(_kickConfirm);
	if (index >= 0) {
		_rows.removeAt(index);
		delete _datas.at(index);
		_datas.removeAt(index);
		_dates.removeAt(index);
		_roles.removeAt(index);
		clearSel();
		if (_filter == MembersFilterRecent && _channel->membersCount() > 1) {
			_channel->setMembersCount(_channel->membersCount() - 1);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		} else if (_filter == MembersFilterAdmins && _channel->adminsCount() > 1) {
			_channel->setAdminsCount(_channel->adminsCount() - 1);
			if (App::main()) emit App::main()->peerUpdated(_channel);
		}
		refresh();
	}
	_kickConfirm = 0;
}

MembersBox::MembersBox(ChannelData *channel, MembersFilter filter) : ItemListBox(st::boxScroll)
, _inner(channel, filter)
, _addBox(0) {
	ItemListBox::init(&_inner);

	connect(&_inner, SIGNAL(addRequested()), this, SLOT(onAdd()));

	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(&_inner, SIGNAL(mustScrollTo(int, int)), &_scroll, SLOT(scrollToY(int, int)));
	connect(&_inner, SIGNAL(loaded()), this, SLOT(onLoaded()));

	connect(&_loadTimer, SIGNAL(timeout()), &_inner, SLOT(load()));

	prepare();
}

void MembersBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner.selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner.selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner.selectSkipPage(_scroll.height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner.selectSkipPage(_scroll.height(), -1);
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void MembersBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	QString title(lang(_inner.filter() == MembersFilterRecent ? lng_channel_members : lng_channel_admins));
	paintTitle(p, title);
}

void MembersBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	_inner.resize(width(), _inner.height());
}

void MembersBox::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

void MembersBox::onAdd() {
	if (_inner.filter() == MembersFilterRecent && _inner.channel()->membersCount() >= (_inner.channel()->isMegagroup() ? Global::MegagroupSizeMax() : Global::ChatSizeMax())) {
		Ui::showLayer(new MaxInviteBox(_inner.channel()->inviteLink()), KeepOtherLayers);
		return;
	}
	ContactsBox *box = new ContactsBox(_inner.channel(), _inner.filter(), _inner.already());
	if (_inner.filter() == MembersFilterRecent) {
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

void MembersBox::showDone() {
	_inner.clearSel();
	setFocus();
}
