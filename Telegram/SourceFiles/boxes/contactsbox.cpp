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

#include "addcontactbox.h"
#include "contactsbox.h"
#include "mainwidget.h"
#include "window.h"

#include "application.h"

#include "gui/filedialog.h"
#include "photocropbox.h"

#include "confirmbox.h"

ContactsInner::ContactsInner(CreatingGroupType creating) : _chat(0), _channel(0), _bot(0), _creating(creating), _addToChat(0),
_contacts(&App::main()->contactsList()),
_sel(0),
_filteredSel(-1),
_mouseSel(false),
_selCount(1),
_searching(false),
_byUsernameSel(-1),
_addContactLnk(this, lang(lng_add_contact_button)) {
	init();
}

ContactsInner::ContactsInner(ChannelData *channel) : _chat(0), _channel(channel), _bot(0), _creating(CreatingGroupChannel), _addToChat(0),
_contacts(&App::main()->contactsList()),
_sel(0),
_filteredSel(-1),
_mouseSel(false),
_selCount(1),
_searching(false),
_byUsernameSel(-1),
_addContactLnk(this, lang(lng_add_contact_button)) {
	init();
}

ContactsInner::ContactsInner(ChatData *chat) : _chat(chat), _channel(0), _bot(0), _creating(CreatingGroupNone), _addToChat(0),
_contacts(&App::main()->contactsList()),
_sel(0),
_filteredSel(-1),
_mouseSel(false),
_selCount(0),
_searching(false),
_byUsernameSel(-1),
_addContactLnk(this, lang(lng_add_contact_button)) {
	init();
}

ContactsInner::ContactsInner(UserData *bot) : _chat(0), _channel(0), _bot(bot), _creating(CreatingGroupNone), _addToChat(0),
_contacts(new DialogsIndexed(DialogsSortByAdd)),
_sel(0),
_filteredSel(-1),
_mouseSel(false),
_selCount(0),
_searching(false),
_byUsernameSel(-1),
_addContactLnk(this, lang(lng_add_contact_button)) {
	DialogsIndexed &v(App::main()->dialogsList());
	for (DialogRow *r = v.list.begin; r != v.list.end; r = r->next) {
		if (r->history->peer->isChat() && !r->history->peer->asChat()->forbidden && !r->history->peer->asChat()->left) {
			_contacts->addToEnd(r->history);
		}
	}
	init();
}

void ContactsInner::init() {
	connect(&_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));

	for (DialogRow *r = _contacts->list.begin; r != _contacts->list.end; r = r->next) {
		r->attached = 0;
	}

	_filter = qsl("a");
	updateFilter();

	connect(App::main(), SIGNAL(dialogRowReplaced(DialogRow *, DialogRow *)), this, SLOT(onDialogRowReplaced(DialogRow *, DialogRow *)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(onPeerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
}

void ContactsInner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (bot()) {
		_contacts->peerNameChanged(peer, oldNames, oldChars);
	}
	peerUpdated(peer);
}

void ContactsInner::onAddBot() {
	if (_bot->botInfo && !_bot->botInfo->startGroupToken.isEmpty()) {
		MTP::send(MTPmessages_StartBot(_bot->inputUser, _addToChat->inputChat, MTP_long(MTP::nonce<uint64>()), MTP_string(_bot->botInfo->startGroupToken)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, _bot));
	} else {
		App::main()->addParticipants(_addToChat, QVector<UserData*>(1, _bot));
	}
	App::wnd()->hideLayer();
	App::main()->showPeerHistory(_addToChat->id, ShowAtUnreadMsgId);
}

void ContactsInner::peerUpdated(PeerData *peer) {
	if (_chat && (!peer || peer == _chat)) {
		if (_chat->forbidden || _chat->left) {
			App::wnd()->hideLayer();
		} else if (!_chat->participants.isEmpty() || _chat->count <= 0) {
			for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
				delete i.value();
			}
			_contactsData.clear();
			for (DialogRow *row = _contacts->list.begin; row->next; row = row->next) {
				row->attached = 0;
			}
			if (!_filter.isEmpty()) {
				for (int32 j = 0, s = _filtered.size(); j < s; ++j) {
					_filtered[j]->attached = 0;
				}
			}
		}
	} else {
		ContactsData::iterator i = _contactsData.find(peer);
		if (i != _contactsData.cend()) {
			for (DialogRow *row = _contacts->list.begin; row->next; row = row->next) {
				if (row->attached == i.value()) row->attached = 0;
			}
			if (!_filter.isEmpty()) {
				for (int32 j = 0, s = _filtered.size(); j < s; ++j) {
					if (_filtered[j]->attached == i.value()) _filtered[j]->attached = 0;
				}
			}
			delete i.value();
			_contactsData.erase(i);
		}
	}

	parentWidget()->update();
}

void ContactsInner::loadProfilePhotos(int32 yFrom) {
	int32 yTo = yFrom + (parentWidget() ? parentWidget()->height() : App::wnd()->height()) * 5;
	MTP::clearLoaderPriorities();

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	if (_filter.isEmpty()) {
		if (_contacts->list.count) {
			_contacts->list.adjustCurrent(yFrom, rh);
			for (
				DialogRow *preloadFrom = _contacts->list.current;
				preloadFrom != _contacts->list.end && preloadFrom->pos * rh < yTo;
				preloadFrom = preloadFrom->next
			) {
				preloadFrom->history->peer->photo->load();
			}
		}
	} else if (!_filtered.isEmpty()) {
		int32 from = yFrom / rh;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			int32 to = (yTo / rh) + 1;
			if (to > _filtered.size()) to = _filtered.size();

			for (; from < to; ++from) {
				_filtered[from]->history->peer->photo->load();
			}
		}
	}
}

ContactsInner::ContactData *ContactsInner::contactData(DialogRow *row) {
	ContactData *data = (ContactData*)row->attached;
	if (!data) {
		PeerData *peer = row->history->peer;
		ContactsData::const_iterator i = _contactsData.constFind(peer);
		if (i == _contactsData.cend()) {
			_contactsData.insert(peer, data = new ContactData());
			data->inchat = (_chat && peer->isUser()) ? _chat->participants.contains(peer->asUser()) : ((_creating == CreatingGroupGroup || _channel) ? (peer == App::self()) : false);
			data->check = _checkedContacts.contains(peer);
			data->name.setText(st::profileListNameFont, peer->name, _textNameOptions);
			if (peer->isUser()) {
				data->online = App::onlineText(peer->asUser(), _time);
			} else if (peer->isChat()) {
				ChatData *chat = peer->asChat();
				if (chat->forbidden || chat->left) {
					data->online = lang(lng_chat_status_unaccessible);
				} else {
					data->online = lng_chat_status_members(lt_count, chat->count);
				}
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

void ContactsInner::paintDialog(QPainter &p, PeerData *peer, ContactData *data, bool sel) {
	int32 left = st::profileListPadding.width();

	UserData *user = peer->asUser();

	if (data->inchat || data->check || selectedCount() >= cMaxGroupCount()) {
		sel = false;
	}

	if (sel || data->inchat || data->check) {
		p.fillRect(0, 0, width(), 2 * st::profileListPadding.height() + st::profileListPhotoSize, ((data->inchat || data->check) ? st::profileActiveBG : st::profileHoverBG)->b);
	}

	p.drawPixmap(left, st::profileListPadding.height(), peer->photo->pix(st::profileListPhotoSize));

	if (data->inchat || data->check) {
		p.setPen(st::white->p);
	} else {
		p.setPen(st::profileListNameColor->p);
	}
	int32 iconw = (_chat || _creating != CreatingGroupNone) ? st::profileCheckRect.pxWidth() : st::contactsImg.pxWidth();
	data->name.drawElided(p, left + st::profileListPhotoSize + st::participantDelta, st::profileListNameTop, width() - left - st::profileListPhotoSize - st::profileListPadding.width() - st::participantDelta - st::scrollDef.width - iconw);

	if (_chat || _creating !=CreatingGroupNone) {
		if (sel || data->check) {
			p.drawPixmap(QPoint(width() - st::profileCheckRect.pxWidth() - st::profileCheckDeltaX, st::profileListPadding.height() + (st::profileListPhotoSize - st::profileCheckRect.pxHeight()) / 2 - st::profileCheckDeltaY), App::sprite(), (data->check ? st::profileCheckActiveRect : st::profileCheckRect));
		}
	} else if (sel) {
		p.drawPixmap(QPoint(width() - st::contactsImg.pxWidth() - st::profileCheckDeltaX, st::profileListPadding.height() + (st::profileListPhotoSize - st::contactsImg.pxHeight()) / 2 - st::profileCheckDeltaY), App::sprite(), st::contactsImg);
	}

	bool uname = (user || peer->isChannel()) && (data->online.at(0) == '@');
	p.setFont(st::profileSubFont->f);
	if (uname && !data->inchat && !data->check && !_lastQuery.isEmpty() && peer->userName().startsWith(_lastQuery, Qt::CaseInsensitive)) {
		int32 availw = width() - (left + st::profileListPhotoSize + st::profileListPadding.width() * 2);
		QString first = '@' + peer->userName().mid(0, _lastQuery.size()), second = peer->userName().mid(_lastQuery.size());
		int32 w = st::profileSubFont->m.width(first);
		if (w >= availw || second.isEmpty()) {
			p.setPen(st::profileOnlineColor->p);
			p.drawText(left + st::profileListPhotoSize + st::profileListPadding.width(), st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, st::profileSubFont->m.elidedText(first, Qt::ElideRight, availw));
		} else {
			p.setPen(st::profileOnlineColor->p);
			p.drawText(left + st::profileListPhotoSize + st::profileListPadding.width(), st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, first);
			p.setPen(st::profileOfflineColor->p);
			p.drawText(left + st::profileListPhotoSize + st::profileListPadding.width() + w, st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, st::profileSubFont->m.elidedText(second, Qt::ElideRight, availw - w));
		}
	} else {
		if (data->inchat || data->check) {
			p.setPen(st::white->p);
		} else if ((user && (uname || App::onlineColorUse(user, _time))) || (peer->isChannel() && uname)) {
			p.setPen(st::profileOnlineColor->p);
		} else {
			p.setPen(st::profileOfflineColor->p);
		}
		p.drawText(left + st::profileListPhotoSize + st::profileListPadding.width(), st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, data->online);
	}
}

void ContactsInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	QPainter p(this);

	_time = unixtime();
	p.fillRect(r, st::white->b);

	int32 yFrom = r.top(), yTo = r.bottom();
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	if (_filter.isEmpty()) {
		if (_contacts->list.count || !_byUsername.isEmpty()) {
			if (_contacts->list.count) {
				_contacts->list.adjustCurrent(yFrom, rh);

				DialogRow *drawFrom = _contacts->list.current;
				p.translate(0, drawFrom->pos * rh);
				while (drawFrom != _contacts->list.end && drawFrom->pos * rh < yTo) {
					paintDialog(p, drawFrom->history->peer, contactData(drawFrom), (drawFrom == _sel));
					p.translate(0, rh);
					drawFrom = drawFrom->next;
				}
			}
			if (!_byUsername.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBG->b);
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, width(), st::searchedBarHeight), lang(lng_search_global_results), style::al_center);
				p.translate(0, st::searchedBarHeight);

				yFrom -= _contacts->list.count * rh + st::searchedBarHeight;
				yTo -= _contacts->list.count * rh + st::searchedBarHeight;
				int32 from = (yFrom >= 0) ? (yFrom / rh) : 0;
				if (from < _byUsername.size()) {
					int32 to = (yTo / rh) + 1;
					if (to > _byUsername.size()) to = _byUsername.size();

					p.translate(0, from * rh);
					for (; from < to; ++from) {
						paintDialog(p, _byUsername[from], d_byUsername[from], (_byUsernameSel == from));
						p.translate(0, rh);
					}
				}
			}
		} else {
			p.setFont(st::noContactsFont->f);
			p.setPen(st::noContactsColor->p);
			QString text;
			int32 skip = 0;
			if (bot()) {
				text = lang(cDialogsReceived() ? lng_bot_no_groups : lng_contacts_loading);
			} else if (cContactsReceived() && !_searching) {
				text = lang(lng_no_contacts);
				skip = st::noContactsFont->height;
			} else {
				text = lang(lng_contacts_loading);
			}
			p.drawText(QRect(0, 0, width(), st::noContactsHeight - skip), text, style::al_center);
		}
	} else {
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			p.setFont(st::noContactsFont->f);
			p.setPen(st::noContactsColor->p);
			QString text;
			if (bot()) {
				text = lang(cDialogsReceived() ? lng_bot_groups_not_found : lng_contacts_loading);
			} else {
				text = lang((cContactsReceived() && !_searching) ? lng_contacts_not_found : lng_contacts_loading);
			}
			p.drawText(QRect(0, 0, width(), st::noContactsHeight), text, style::al_center);
		} else {
			if (!_filtered.isEmpty()) {
				int32 from = (yFrom >= 0) ? (yFrom / rh) : 0;
				if (from < _filtered.size()) {
					int32 to = (yTo / rh) + 1;
					if (to > _filtered.size()) to = _filtered.size();

					p.translate(0, from * rh);
					for (; from < to; ++from) {
						paintDialog(p, _filtered[from]->history->peer, contactData(_filtered[from]), (_filteredSel == from));
						p.translate(0, rh);
					}
				}
			}
			if (!_byUsernameFiltered.isEmpty()) {
				p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBG->b);
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, width(), st::searchedBarHeight), lang(lng_search_global_results), style::al_center);
				p.translate(0, st::searchedBarHeight);

				yFrom -= _filtered.size() * rh + st::searchedBarHeight;
				yTo -= _filtered.size() * rh + st::searchedBarHeight;
				int32 from = (yFrom >= 0) ? (yFrom / rh) : 0;
				if (from < _byUsernameFiltered.size()) {
					int32 to = (yTo / rh) + 1;
					if (to > _byUsernameFiltered.size()) to = _byUsernameFiltered.size();

					p.translate(0, from * rh);
					for (; from < to; ++from) {
						paintDialog(p, _byUsernameFiltered[from], d_byUsernameFiltered[from], (_byUsernameSel == from));
						p.translate(0, rh);
					}
				}
			}
		}
	}
}

void ContactsInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void ContactsInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	if (_sel || _filteredSel >= 0 || _byUsernameSel >= 0) {
		_sel = 0;
		_filteredSel = _byUsernameSel = -1;
		parentWidget()->update();
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
	if (_chat || _creating != CreatingGroupNone) {
		_time = unixtime();
		int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, from;
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
		int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, from;
		PeerData *peer = 0;
		if (_filter.isEmpty()) {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsername.size()) {
				peer = _byUsername[_byUsernameSel];
			} else if (_sel) {
				peer = _sel->history->peer;
			}
		} else {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsernameFiltered.size()) {
				peer = _byUsernameFiltered[_byUsernameSel];
			} else {
				if (_filteredSel < 0 || _filteredSel >= _filtered.size()) return;
				peer = _filtered[_filteredSel]->history->peer;
			}
		}
		if (peer) {
			if (bot() && peer->isChat()) {
				_addToChat = peer->asChat();
				ConfirmBox *box = new ConfirmBox(lng_bot_sure_invite(lt_group, peer->name));
				connect(box, SIGNAL(confirmed()), this, SLOT(onAddBot()));
				App::wnd()->replaceLayer(box);
			} else {
				App::wnd()->hideSettings(true);
				App::main()->choosePeer(peer->id, ShowAtUnreadMsgId);
				App::wnd()->hideLayer();
			}
		}
	}
	parentWidget()->update();
}

void ContactsInner::changeCheckState(DialogRow *row) {
	changeCheckState(contactData(row), row->history->peer);
}

void ContactsInner::changeCheckState(ContactData *data, PeerData *peer) {
	int32 cnt = _selCount;
	if (data->check) {
		data->check = false;
		_checkedContacts.remove(peer);
		--_selCount;
	} else if (selectedCount() < cMaxGroupCount()) {
		data->check = true;
		_checkedContacts.insert(peer, true);
		++_selCount;
	}
	if (cnt != _selCount) emit chosenChanged();
}

int32 ContactsInner::selectedCount() const {
	return _selCount + ((_chat && _chat->count >= 0) ? _chat->count : 0);
}

void ContactsInner::updateSel() {
	QPoint p(mapFromGlobal(_lastMousePos));
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	if (_filter.isEmpty()) {
		DialogRow *newSel = (in && (p.y() >= 0) && (p.y() < _contacts->list.count * rh)) ? _contacts->list.rowAtY(p.y(), rh) : 0;
		int32 byUsernameSel = (in && p.y() >= _contacts->list.count * rh + st::searchedBarHeight) ? ((p.y() - _contacts->list.count * rh - st::searchedBarHeight) / rh) : -1;
		if (byUsernameSel >= _byUsername.size()) byUsernameSel = -1;
		if (newSel != _sel || byUsernameSel != _byUsernameSel) {
			_sel = newSel;
			_byUsernameSel = byUsernameSel;
			parentWidget()->update();
		}
	} else {
		int32 newFilteredSel = (in && p.y() >= 0 && p.y() < _filtered.size() * rh) ? (p.y() / rh) : -1;
		int32 byUsernameSel = (in && p.y() >= _filtered.size() * rh + st::searchedBarHeight) ? ((p.y() - _filtered.size() * rh - st::searchedBarHeight) / rh) : -1;
		if (byUsernameSel >= _byUsernameFiltered.size()) byUsernameSel = -1;
		if (newFilteredSel != _filteredSel || byUsernameSel != _byUsernameSel) {
			_filteredSel = newFilteredSel;
			_byUsernameSel = byUsernameSel;
			parentWidget()->update();
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
		int32 rh = (st::profileListPhotoSize + st::profileListPadding.height() * 2);
		_filter = filter;

		_byUsernameFiltered.clear();
		d_byUsernameFiltered.clear();
		for (int i = 0, l = _byUsernameDatas.size(); i < l; ++i) {
			delete _byUsernameDatas[i];
		}
		_byUsernameDatas.clear();

		if (_filter.isEmpty()) {
			_sel = 0;
			if (_contacts->list.count) {
				_sel = _contacts->list.begin;
				while (_sel->next->next && contactData(_sel)->inchat) {
					_sel = _sel->next;
				}
			}
			if (!_sel && !_byUsername.isEmpty()) {
				_byUsernameSel = 0;
				while (_byUsernameSel < _byUsername.size() && d_byUsername[_byUsernameSel]->inchat) {
					++_byUsernameSel;
				}
				if (_byUsernameSel == _byUsername.size()) _byUsernameSel = -1;
			} else {
				_byUsernameSel = -1;
			}
			refresh();
		} else {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

			_filtered.clear();
			if (!f.isEmpty()) {
				DialogsList *dialogsToFilter = 0;
				if (_contacts->list.count) {
					for (fi = fb; fi != fe; ++fi) {
						DialogsIndexed::DialogsIndex::iterator i = _contacts->index.find(fi->at(0));
						if (i == _contacts->index.cend()) {
							dialogsToFilter = 0;
							break;
						}
						if (!dialogsToFilter || dialogsToFilter->count > i.value()->count) {
							dialogsToFilter = i.value();
						}
					}
				}
				if (dialogsToFilter && dialogsToFilter->count) {
					_filtered.reserve(dialogsToFilter->count);
					for (DialogRow *i = dialogsToFilter->begin, *e = dialogsToFilter->end; i != e; i = i->next) {
						const PeerData::Names &names(i->history->peer->names);
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
							i->attached = 0;
							_filtered.push_back(i);
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

			refresh();

			if (!bot()) {
				_searching = true;
				emit searchByUsername();
			}
		}
		if (parentWidget()) parentWidget()->update();
		loadProfilePhotos(0);
	}
}

void ContactsInner::onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow) {
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
	int32 rh = (st::profileListPhotoSize + st::profileListPadding.height() * 2);
	int32 cnt = (_filter.isEmpty() ? _contacts->list.count : _filtered.size());
	int32 newh = cnt ? (cnt * rh + st::contactsClose.height) : st::noContactsHeight;
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

			if ((!p->isUser() || p->asUser()->botInfo && p->asUser()->botInfo->cantJoinGroups) && (_chat || _creating != CreatingGroupNone)) continue; // skip bot's that can't be invited to groups
			if (p->isUser() && p->asUser()->botInfo && _creating == CreatingGroupChannel) continue; // skip bots in channels

			ContactData *d = new ContactData();
			_byUsernameDatas.push_back(d);
			d->inchat = _chat ? _chat->participants.contains(p->asUser()) : ((_creating == CreatingGroupGroup || _channel) ? (p == App::self()) : false);
			d->check = _checkedContacts.contains(p);
			d->name.setText(st::profileListNameFont, p->name, _textNameOptions);
			d->online = '@' + p->userName();

			_byUsernameFiltered.push_back(p);
			d_byUsernameFiltered.push_back(d);
		}
	}
	_searching = false;
	refresh();
}

void ContactsInner::refresh() {
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	if (_filter.isEmpty()) {
		if (_contacts->list.count || !_byUsername.isEmpty()) {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			resize(width(), (_contacts->list.count * rh) + (_byUsername.isEmpty() ? 0 : (st::searchedBarHeight + _byUsername.size() * rh)));
		} else {
			if (cContactsReceived() && !bot()) {
				if (_addContactLnk.isHidden()) _addContactLnk.show();
			} else {
				if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			}
			resize(width(), st::noContactsHeight);
		}
	} else {
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			resize(width(), st::noContactsHeight);
		} else {
			resize(width(), (_filtered.size() * rh) + (_byUsernameFiltered.isEmpty() ? 0 : (st::searchedBarHeight + _byUsernameFiltered.size() * rh)));
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
	if (_bot) {
		delete _contacts;
		if (_bot->botInfo) _bot->botInfo->startGroupToken = QString();
	}
}

void ContactsInner::resizeEvent(QResizeEvent *e) {
	_addContactLnk.move((width() - _addContactLnk.width()) / 2, (st::noContactsHeight + st::noContactsFont->height) / 2);
}

void ContactsInner::selectSkip(int32 dir) {
	_time = unixtime();
	_mouseSel = false;
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, origDir = dir;
	if (_filter.isEmpty()) {
		int cur = 0;
		if (_sel) {
			for (DialogRow *i = _contacts->list.begin; i != _sel; i = i->next) {
				++cur;
			}
		} else {
			cur = (_byUsernameSel >= 0) ? (_contacts->list.count + _byUsernameSel) : -1;
		}
		cur += dir;
		if (cur <= 0) {
			_sel = _contacts->list.count ? _contacts->list.begin : 0;
			_byUsernameSel = (!_contacts->list.count && !_byUsername.isEmpty()) ? 0 : -1;
		} else if (cur >= _contacts->list.count) {
			if (_byUsername.isEmpty()) {
				_sel = _contacts->list.count ? _contacts->list.end->prev : 0;
				_byUsernameSel = -1;
			} else {
				_sel = 0;
				_byUsernameSel = cur - _contacts->list.count;
				if (_byUsernameSel >= _byUsername.size()) _byUsernameSel = _byUsername.size() - 1;
			}
		} else {
			for (_sel = _contacts->list.begin; cur; _sel = _sel->next) {
				--cur;
			}
			_byUsernameSel = -1;
		}
		if (dir > 0) {
			while (_sel && _sel->next && contactData(_sel)->inchat) {
				_sel = _sel->next;
			}
			if (!_sel || !_sel->next) {
				_sel = 0;
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
				if (_contacts->list.count) {
					if (!_sel) _sel = _contacts->list.end->prev;
					for (; _sel && contactData(_sel)->inchat;) {
						_sel = _sel->prev;
					}
				}
			}
		}
		if (_sel) {
			emit mustScrollTo(_sel->pos * rh, (_sel->pos + 1) * rh);
		} else if (_byUsernameSel >= 0) {
			emit mustScrollTo((_contacts->list.count + _byUsernameSel) * rh + st::searchedBarHeight, (_contacts->list.count + _byUsernameSel + 1) * rh + st::searchedBarHeight);
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
			emit mustScrollTo(_filteredSel * rh, (_filteredSel + 1) * rh);
		} else if (_byUsernameSel >= 0) {
			int skip = _filtered.size() * rh + st::searchedBarHeight;
			emit mustScrollTo(skip + _byUsernameSel * rh, skip + (_byUsernameSel + 1) * rh);
		}
	}
	parentWidget()->update();
}

void ContactsInner::selectSkipPage(int32 h, int32 dir) {
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	int32 points = h / rh;
	if (!points) return;
	selectSkip(points * dir);
}

QVector<UserData*> ContactsInner::selected() {
	QVector<UserData*> result;
	for (DialogRow *row = _contacts->list.begin; row->next; row = row->next) {
		if (_checkedContacts.contains(row->history->peer)) {
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
	for (DialogRow *row = _contacts->list.begin; row->next; row = row->next) {
		if (_checkedContacts.contains(row->history->peer)) {
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
	for (DialogRow *row = _contacts->list.begin; row->next; row = row->next) {
		if (_checkedContacts.contains(row->history->peer)) {
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

ContactsBox::ContactsBox() : ItemListBox(st::boxNoTopScroll), _inner(CreatingGroupNone),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_create_group_next), st::btnSelectDone),
_cancel(this, lang(lng_contacts_done), st::contactsClose),
_creationRequestId(0) {
	init();
}

ContactsBox::ContactsBox(const QString &name, const QImage &photo) : ItemListBox(st::boxNoTopScroll), _inner(CreatingGroupGroup),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_create_group_create), st::btnSelectDone),
_cancel(this, lang(lng_create_group_back), st::btnSelectCancel),
_creationRequestId(0), _creationName(name), _creationPhoto(photo) {
	init();
}

ContactsBox::ContactsBox(ChannelData *channel) : ItemListBox(st::boxNoTopScroll), _inner(channel),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_participant_invite), st::btnSelectDone),
_cancel(this, lang(lng_create_group_skip), st::btnSelectCancel),
_creationRequestId(0) {
	init();
}

ContactsBox::ContactsBox(ChatData *chat) : ItemListBox(st::boxNoTopScroll), _inner(chat),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_participant_invite), st::btnSelectDone),
_cancel(this, lang(lng_cancel), st::btnSelectCancel),
_creationRequestId(0) {
	init();
}

ContactsBox::ContactsBox(UserData *bot) : ItemListBox(st::boxNoTopScroll), _inner(bot),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_create_group_next), st::btnSelectDone),
_cancel(this, lang(lng_cancel), st::contactsClose),
_creationRequestId(0) {
	init();
}

void ContactsBox::init() {
	ItemListBox::init(&_inner, _cancel.height(), st::contactsAdd.height + st::newGroupNamePadding.top() + _filter.height() + st::newGroupNamePadding.bottom());

	connect(&_inner, SIGNAL(chosenChanged()), this, SLOT(update()));
	if (_inner.chat()) {
		_addContact.hide();
	} else if (_inner.creating() != CreatingGroupNone) {
		_addContact.hide();
	} else {
		connect(&_addContact, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	}
	if (_inner.chat() || _inner.channel()) {
		connect(&_next, SIGNAL(clicked()), this, SLOT(onInvite()));
	} else if (_inner.creating() != CreatingGroupNone) {
		connect(&_next, SIGNAL(clicked()), this, SLOT(onCreate()));
	} else {
		_next.hide();
	}
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSel()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_inner, SIGNAL(mustScrollTo(int, int)), &_scroll, SLOT(scrollToY(int, int)));
	connect(&_inner, SIGNAL(selectAllQuery()), &_filter, SLOT(selectAll()));
	connect(&_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	prepare();
}

bool ContactsBox::onSearchByUsername(bool searchCache) {
	QString q = _filter.text().trimmed();
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
			_inner.peopleReceived(q, result.c_contacts_found().vresults.c_vector().v);
		} break;
		}

		_peopleRequest = 0;
		_inner.updateSel();
		onScroll();
	}
}

bool ContactsBox::peopleFailed(const RPCError &error, mtpRequestId req) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	if (_peopleRequest == req) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
	return true;
}

void ContactsBox::hideAll() {
	ItemListBox::hideAll();
	_addContact.hide();
	_filter.hide();
	_next.hide();
	_cancel.hide();
}

void ContactsBox::showAll() {
	ItemListBox::showAll();
	_filter.show();
	if (_inner.chat()) {
		_next.show();
		_addContact.hide();
	} else if (_inner.creating() != CreatingGroupNone) {
		_next.show();
		_addContact.hide();
	} else {
		_next.hide();
		if (_inner.bot()) {
			_addContact.hide();
		} else {
			_addContact.show();
		}
	}
	_cancel.show();
}

void ContactsBox::showDone() {
	_filter.setFocus();
}

void ContactsBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (_filter.hasFocus()) {
			_inner.chooseParticipant();
		} else {
			ItemListBox::keyPressEvent(e);
		}
	} else if (_filter.hasFocus()) {
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

	if (_inner.chat() || _inner.creating() != CreatingGroupNone) {
		QString title(lang(lng_profile_add_participant));
		paintTitle(p, title, true);

		p.setPen(st::newGroupLimitFg);
		p.drawTextLeft(st::boxTitlePos.x() + st::boxTitleFont->m.width(title) + st::addContactDelta, st::boxTitlePos.y(), width(), QString("%1 / %2").arg(_inner.selectedCount()).arg(cMaxGroupCount()));

		// paint button sep
		p.fillRect(st::btnSelectCancel.width, size().height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
	} else if (_inner.bot()) {
		paintTitle(p, lang(lng_bot_choose_group), true);
	} else {
		paintTitle(p, lang(lng_contacts_header), true);
	}
}

void ContactsBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	_addContact.move(width() - _addContact.width(), 0);
	_filter.move(st::newGroupNamePadding.left(), _addContact.height() + st::newGroupNamePadding.top());
	_inner.resize(width(), _inner.height());
	_next.move(width() - _next.width(), height() - _next.height());
	_cancel.move(0, height() - _cancel.height());
}

void ContactsBox::closePressed() {
	if (_inner.channel()) {
		App::main()->showPeerHistory(_inner.channel()->id, ShowAtTheEndMsgId);
	}
}

void ContactsBox::onFilterUpdate() {
	_scroll.scrollToY(0);
	_inner.updateFilter(_filter.text());
}

void ContactsBox::onAdd() {
	App::wnd()->replaceLayer(new AddContactBox());
}

void ContactsBox::onInvite() {
	QVector<UserData*> users(_inner.selected());
	if (users.isEmpty()) {
		_filter.setFocus();
		_filter.notaBene();
		return;
	}

	App::main()->addParticipants(_inner.chat() ? (PeerData*)_inner.chat() : _inner.channel(), users);
	if (_inner.chat()) {
		App::wnd()->hideLayer();
		App::main()->showPeerHistory(_inner.chat()->id, ShowAtTheEndMsgId);
	} else {
		onClose();
	}
}

void ContactsBox::onCreate() {
	if (_creationRequestId) return;

	MTPVector<MTPInputUser> users(MTP_vector<MTPInputUser>(_inner.selectedInputs()));
	const QVector<MTPInputUser> &v(users.c_vector().v);
	if (v.isEmpty() || (v.size() == 1 && v.at(0).type() == mtpc_inputUserSelf)) {
		_filter.setFocus();
		_filter.notaBene();
		return;
	}
	_creationRequestId = MTP::send(MTPmessages_CreateChat(MTP_vector<MTPInputUser>(v), MTP_string(_creationName)), rpcDone(&ContactsBox::creationDone), rpcFail(&ContactsBox::creationFail));
}

void ContactsBox::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

PeerData *chatOrChannelCreated(const MTPUpdates &updates, const QImage &photo) {
	App::main()->sentUpdatesReceived(updates);

	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	case mtpc_updateShort: {
	} break;
	case mtpc_updateShortMessage: {
	} break;
	case mtpc_updateShortChatMessage: {
	} break;
	case mtpc_updateShortSentMessage: {
	} break;
	case mtpc_updatesTooLong: {
	} break;
	}
	if (v && !v->isEmpty() && v->front().type() == mtpc_chat) {
		ChatData *chat = App::chat(v->front().c_chat().vid.v);
		if (chat) {
			if (!photo.isNull()) {
				App::app()->uploadProfilePhoto(photo, chat->id);
			}
			return chat;
		}
	} else if (v && !v->isEmpty() && v->front().type() == mtpc_channel) {
		ChannelData *channel = App::channel(v->front().c_channel().vid.v);
		if (channel) {
			if (!photo.isNull()) {
				App::app()->uploadProfilePhoto(photo, channel->id);
			}
			return channel;
		}
	}

	return 0;
}

void ContactsBox::creationDone(const MTPUpdates &updates) {
	App::wnd()->hideLayer();

	PeerData *peer = chatOrChannelCreated(updates, _creationPhoto);
	if (peer) {
		App::main()->showPeerHistory(peer->id, ShowAtUnreadMsgId);
	}
}

bool ContactsBox::creationFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_creationRequestId = 0;
	if (error.type() == "NO_CHAT_TITLE") {
		emit closed();
		return true;
	} else if (error.type() == "USERS_TOO_FEW") {
		_filter.setFocus();
		_filter.notaBene();
		return true;
	} else if (error.type() == "PEER_FLOOD") {
		App::wnd()->replaceLayer(new ConfirmBox(lng_cant_invite_not_contact(lt_more_info, textcmdLink(qsl("https://telegram.org/faq?_hash=can-39t-send-messages-to-non-contacts"), lang(lng_cant_more_info)))));
		return true;
	}
	return false;
}

NewGroupBox::NewGroupBox() : AbstractBox(),
_group(this, qsl("group_type"), 0, lang(lng_create_group_title), true),
_channel(this, qsl("group_type"), 1, lang(lng_create_channel_title)),
_aboutGroupWidth(width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::rbDefFlat.textLeft),
_aboutGroup(st::normalFont, lang(lng_create_group_about), _defaultOptions, _aboutGroupWidth),
_aboutChannel(st::normalFont, lang(lng_create_channel_about), _defaultOptions, _aboutGroupWidth),
_next(this, lang(lng_create_group_next), st::btnSelectDone),
_cancel(this, lang(lng_cancel), st::btnSelectCancel) {
	_aboutGroupHeight = _aboutGroup.countHeight(_aboutGroupWidth);
	setMaxHeight(st::newGroupPadding.top() + _group.height() + _aboutGroupHeight + st::newGroupSkip + _channel.height() + _aboutChannel.countHeight(_aboutGroupWidth) + st::newGroupPadding.bottom() + _next.height());

	connect(&_next, SIGNAL(clicked()), this, SLOT(onNext()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void NewGroupBox::hideAll() {
	_group.hide();
	_channel.hide();
	_cancel.hide();
	_next.hide();
}

void NewGroupBox::showAll() {
	_group.show();
	_channel.show();
	_cancel.show();
	_next.show();
}

void NewGroupBox::showDone() {
	setFocus();
}

void NewGroupBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onNext();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void NewGroupBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	p.setPen(st::newGroupAboutFg->p);

	QRect aboutGroup(st::newGroupPadding.left() + st::rbDefFlat.textLeft, _group.y() + _group.height(), width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::rbDefFlat.textLeft, _aboutGroupHeight);
	if (rtl()) aboutGroup.setX(width() - aboutGroup.x() - aboutGroup.width());
	_aboutGroup.draw(p, aboutGroup.x(), aboutGroup.y(), aboutGroup.width());

	QRect aboutChannel(st::newGroupPadding.left() + st::rbDefFlat.textLeft, _channel.y() + _channel.height(), width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::rbDefFlat.textLeft, _aboutGroupHeight);
	if (rtl()) aboutChannel.setX(width() - aboutChannel.x() - aboutChannel.width());
	_aboutChannel.draw(p, aboutChannel.x(), aboutChannel.y(), aboutChannel.width());

	// paint shadow
	p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

void NewGroupBox::resizeEvent(QResizeEvent *e) {
	_group.moveToLeft(st::newGroupPadding.left(), st::newGroupPadding.top(), width());
	_channel.moveToLeft(st::newGroupPadding.left(), _group.y() + _group.height() + _aboutGroupHeight + st::newGroupSkip, width());

	int32 buttonTop = height() - st::btnSelectCancel.height;
	_cancel.moveToLeft(0, buttonTop, width());
	_next.moveToRight(0, buttonTop, width());
}

void NewGroupBox::onNext() {
	App::wnd()->replaceLayer(new GroupInfoBox(_group.checked() ? CreatingGroupGroup : CreatingGroupChannel));
}

GroupInfoBox::GroupInfoBox(CreatingGroupType creating) : AbstractBox(),
_creating(creating),
a_photoOver(0, 0),
a_photo(animFunc(this, &GroupInfoBox::photoAnimStep)),
_photoOver(false),
_descriptionOver(false),
a_descriptionBg(st::newGroupName.bgColor->c, st::newGroupName.bgColor->c),
a_descriptionBorder(st::newGroupName.borderColor->c, st::newGroupName.borderColor->c),
a_description(animFunc(this, &GroupInfoBox::descriptionAnimStep)),
_name(this, st::newGroupName, lang(_creating == CreatingGroupChannel ? lng_dlg_new_channel_name : lng_dlg_new_group_name)),
_photo(this, lang(lng_create_group_photo), st::newGroupPhoto),
_description(this, st::newGroupDescription, lang(lng_create_group_description)),
_next(this, lang(_creating == CreatingGroupChannel ? lng_create_group_create : lng_create_group_next), st::btnSelectDone),
_cancel(this, lang(lng_create_group_back), st::btnSelectCancel),
_creationRequestId(0), _createdChannel(0) {

	setMouseTracking(true);

	_description.resize(width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::newGroupDescriptionPadding.left() - st::newGroupDescriptionPadding.right(), _name.height() - st::newGroupDescriptionPadding.top() - st::newGroupDescriptionPadding.bottom());
	_description.setMinHeight(_description.height());
	_description.setMaxHeight(3 * _description.height() + 2 * st::newGroupDescriptionPadding.top() + 2 * st::newGroupDescriptionPadding.bottom());

	updateMaxHeight();
	_description.setMaxLength(MaxChannelDescription);
	connect(&_description, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
	connect(&_description, SIGNAL(submitted(bool)), this, SLOT(onNext()));
	connect(&_description, SIGNAL(cancelled()), this, SLOT(onClose()));

	connect(&_photo, SIGNAL(clicked()), this, SLOT(onPhoto()));

	connect(&_next, SIGNAL(clicked()), this, SLOT(onNext()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void GroupInfoBox::hideAll() {
	_name.hide();
	_photo.hide();
	_description.hide();
	_cancel.hide();
	_next.hide();
}

void GroupInfoBox::showAll() {
	_name.show();
	_photo.show();
	if (_creating == CreatingGroupChannel) {
		_description.show();
	} else {
		_description.hide();
	}
	_cancel.show();
	_next.show();
}

void GroupInfoBox::showDone() {
	_name.setFocus();
}

void GroupInfoBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_name.hasFocus()) {
			if (_name.text().trimmed().isEmpty()) {
				_name.setFocus();
				_name.notaBene();
			} else if (_description.isHidden()) {
				onNext();
			} else {
				_description.setFocus();
			}
		}
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void GroupInfoBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	QRect phRect(photoRect());
	if (phRect.intersects(e->rect())) {
		if (_photoSmall.isNull()) {
			int32 s = st::newGroupPhotoSize * cIntRetinaFactor();
			QRect ph(st::setPhotoImg), overph(st::setOverPhotoImg);
			if (a_photoOver.current() < 1) {
				p.drawPixmapLeft(phRect.topLeft(), width(), App::sprite(), QRect(ph.x() + (ph.width() - s) / 2, ph.y() + (ph.height() - s) / 2, s, s));
			}
			if (a_photoOver.current() > 0) {
				p.setOpacity(a_photoOver.current());
				p.drawPixmapLeft(phRect.topLeft(), width(), App::sprite(), QRect(overph.x() + (overph.width() - s) / 2, overph.y() + (overph.height() - s) / 2, s, s));
				p.setOpacity(1);
			}
		} else {
			p.drawPixmap(st::newGroupPadding.left(), st::newGroupPadding.left(), _photoSmall);
		}
		if (phRect.contains(e->rect())) {
			return;
		}
	}
	QRect descRect(descriptionRect());
	if (_creating == CreatingGroupChannel && descRect.intersects(e->rect())) {
		p.fillRect(descRect, a_descriptionBg.current());
		if (st::newGroupName.borderWidth) {
			QBrush b(a_descriptionBorder.current());
			p.fillRect(descRect.x(), descRect.y(), descRect.width() - st::newGroupName.borderWidth, st::newGroupName.borderWidth, b);
			p.fillRect(descRect.x() + descRect.width() - st::newGroupName.borderWidth, descRect.y(), st::newGroupName.borderWidth, descRect.height() - st::newGroupName.borderWidth, b);
			p.fillRect(descRect.x() + st::newGroupName.borderWidth, descRect.y() + descRect.height() - st::newGroupName.borderWidth, descRect.width() - st::newGroupName.borderWidth, st::newGroupName.borderWidth, b);
			p.fillRect(descRect.x(), descRect.y() + st::newGroupName.borderWidth, st::newGroupName.borderWidth, descRect.height() - st::newGroupName.borderWidth, b);
		}
		if (descRect.contains(e->rect())) {
			return;
		}
	}

	// paint shadow
	p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

void GroupInfoBox::resizeEvent(QResizeEvent *e) {
	int32 nameLeft = st::newGroupPhotoSize + st::newGroupPhotoSkip;
	_name.resize(width() - st::newGroupPadding.left() - st::newGroupPadding.right() - nameLeft, _name.height());
	_name.moveToLeft(st::newGroupPadding.left() + nameLeft, st::newGroupPadding.top(), width());
	_photo.moveToLeft(_name.x(), _name.y() + st::newGroupPhotoSize - _photo.height(), width());

	_description.moveToLeft(st::newGroupPadding.left() + st::newGroupDescriptionPadding.left(), _photo.y() + _photo.height() + st::newGroupDescriptionSkip + st::newGroupDescriptionPadding.top(), width());
	_description.installEventFilter(this);

	int32 buttonTop = (_creating == CreatingGroupChannel) ? (_description.y() + _description.height() + st::newGroupDescriptionPadding.bottom()) : (_photo.y() + _photo.height());
	buttonTop += st::newGroupPadding.bottom();
	_cancel.move(0, buttonTop);
	_next.move(width() - _next.width(), buttonTop);
}

void GroupInfoBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void GroupInfoBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool photoOver = photoRect().contains(p);
	if (photoOver != _photoOver) {
		_photoOver = photoOver;
		if (_photoSmall.isNull()) {
			a_photoOver.start(_photoOver ? 1 : 0);
			a_photo.start();
		}
	}

	bool descriptionOver = _photoOver ? false : descriptionRect().contains(p);
	if (descriptionOver != _descriptionOver) {
		_descriptionOver = descriptionOver;
	}

	setCursor(_photoOver ? style::cur_pointer : (_descriptionOver ? style::cur_text : style::cur_default));
}

void GroupInfoBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_photoOver) {
		onPhoto();
	} else if (_descriptionOver) {
		_description.setFocus();
	}
}

void GroupInfoBox::leaveEvent(QEvent *e) {
	updateSelected(QCursor::pos());
}

bool GroupInfoBox::descriptionAnimStep(float64 ms) {
	float dt = ms / st::newGroupName.phDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_descriptionBg.finish();
		a_descriptionBorder.finish();
	} else {
		a_descriptionBg.update(dt, st::newGroupName.phColorFunc);
		a_descriptionBorder.update(dt, st::newGroupName.phColorFunc);
	}
	update(descriptionRect());
	return res;
}

bool GroupInfoBox::photoAnimStep(float64 ms) {
	float64 dt = ms / st::setPhotoDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_photoOver.finish();
	} else {
		a_photoOver.update(dt, anim::linear);
	}
	update(photoRect());
	return res;
}

bool GroupInfoBox::eventFilter(QObject *obj, QEvent *e) {
	if (obj == &_description) {
		if (e->type() == QEvent::FocusIn) {
			a_descriptionBorder.start(st::newGroupName.borderActive->c);
			a_descriptionBg.start(st::newGroupName.bgActive->c);
			a_description.start();
		} else if (e->type() == QEvent::FocusOut) {
			a_descriptionBorder.start(st::newGroupName.borderColor->c);
			a_descriptionBg.start(st::newGroupName.bgColor->c);
			a_description.start();
		}
	}
	return AbstractBox::eventFilter(obj, e);
}

void GroupInfoBox::onNext() {
	if (_creationRequestId) return;

	QString name = _name.text().trimmed();
	if (name.isEmpty()) {
		_name.setFocus();
		_name.notaBene();
		return;
	}
	if (_creating == CreatingGroupGroup) {
		App::wnd()->replaceLayer(new ContactsBox(name, _photoBig));
	} else {
		_creationRequestId = MTP::send(MTPchannels_CreateChannel(MTP_int(0), MTP_string(name), MTP_string(_description.getLastText().trimmed()), MTP_vector<MTPInputUser>(0)), rpcDone(&GroupInfoBox::creationDone), rpcFail(&GroupInfoBox::creationFail));
	}
}

void GroupInfoBox::creationDone(const MTPUpdates &updates) {
	PeerData *result = chatOrChannelCreated(updates, _photoBig);
	if (!result || !result->isChannel()) {
		onClose();
	} else {
		_createdChannel = result->asChannel();
		_creationRequestId = MTP::send(MTPchannels_ExportInvite(_createdChannel->inputChannel), rpcDone(&GroupInfoBox::exportDone));
	}
}

bool GroupInfoBox::creationFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_creationRequestId = 0;
	if (error.type() == "NO_CHAT_TITLE") {
		_name.setFocus();
		_name.notaBene();
		return true;
	} else if (error.type() == "PEER_FLOOD") {
		App::wnd()->replaceLayer(new ConfirmBox(lng_cant_invite_not_contact(lt_more_info, textcmdLink(qsl("https://telegram.org/faq?_hash=can-39t-send-messages-to-non-contacts"), lang(lng_cant_more_info)))));
		return true;
	}
	return false;
}

void GroupInfoBox::exportDone(const MTPExportedChatInvite &result) {
	_creationRequestId = 0;
	if (result.type() == mtpc_chatInviteExported) {
		_createdChannel->invitationUrl = qs(result.c_chatInviteExported().vlink);
	}
	App::wnd()->hideLayer(true);
	App::wnd()->showLayer(new SetupChannelBox(_createdChannel), true);
}

void GroupInfoBox::onDescriptionResized() {
	updateMaxHeight();
	update();
}

QRect GroupInfoBox::descriptionRect() const {
	return rtlrect(_description.x() - st::newGroupDescriptionPadding.left(), _description.y() - st::newGroupDescriptionPadding.top(), _description.width() + st::newGroupDescriptionPadding.left() + st::newGroupDescriptionPadding.right(), _description.height() + st::newGroupDescriptionPadding.top() + st::newGroupDescriptionPadding.bottom(), width());
}

QRect GroupInfoBox::photoRect() const {
	return rtlrect(st::newGroupPadding.left(), st::newGroupPadding.top(), st::newGroupPhotoSize, st::newGroupPhotoSize, width());
}

void GroupInfoBox::updateMaxHeight() {
	int32 h = st::newGroupPadding.top() + st::newGroupPhotoSize + st::newGroupPadding.bottom() + _next.height();
	if (_creating == CreatingGroupChannel) {
		h += st::newGroupDescriptionSkip + st::newGroupDescriptionPadding.top() + _description.height() + st::newGroupDescriptionPadding.bottom();
	}
	setMaxHeight(h);
}

void GroupInfoBox::onPhoto() {
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
		return;
	}
	PhotoCropBox *box = new PhotoCropBox(img, (_creating == CreatingGroupChannel) ? peerFromChannel(0) : peerFromChat(0), false);
	connect(box, SIGNAL(ready(const QImage&)), this, SLOT(onPhotoReady(const QImage&)));
	App::wnd()->replaceLayer(box);
}

void GroupInfoBox::onPhotoReady(const QImage &img) {
	_photoBig = img;
	_photoSmall = QPixmap::fromImage(img.scaled(st::newGroupPhotoSize * cIntRetinaFactor(), st::newGroupPhotoSize * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
	_photoSmall.setDevicePixelRatio(cRetinaFactor());
}

SetupChannelBox::SetupChannelBox(ChannelData *channel) : AbstractBox(),
_channel(channel),
_public(this, qsl("channel_privacy"), 0, lang(lng_create_public_channel_title), true),
_private(this, qsl("channel_privacy"), 1, lang(lng_create_private_channel_title)),
_comments(this, lang(lng_create_channel_comments), true),
_aboutPublicWidth(width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::rbDefFlat.textLeft),
_aboutPublic(st::normalFont, lang(lng_create_public_channel_about), _defaultOptions, _aboutPublicWidth),
_aboutPrivate(st::normalFont, lang(lng_create_private_channel_about), _defaultOptions, _aboutPublicWidth),
_aboutComments(st::normalFont, lang(lng_create_channel_comments_about), _defaultOptions, _aboutPublicWidth),
_linkPlaceholder(qsl("telegram.me/")),
_link(this, st::newGroupLink, QString()),
_linkOver(false),
_save(this, lang(lng_create_group_save), st::btnSelectDone),
_skip(this, lang(lng_create_group_skip), st::btnSelectCancel),
_saveRequestId(0), _checkRequestId(0),
a_goodOpacity(0, 0), a_good(animFunc(this, &SetupChannelBox::goodAnimStep)) {
	setMouseTracking(true);

	_link.setTextMargin(style::margins(st::newGroupLink.textMrg.left() + st::newGroupLink.font->m.width(_linkPlaceholder), st::newGroupLink.textMrg.top(), st::newGroupLink.textMrg.right(), st::newGroupLink.textMrg.bottom()));

	_aboutPublicHeight = _aboutPublic.countHeight(_aboutPublicWidth);
	setMaxHeight(st::newGroupPadding.top() + _public.height() + _aboutPublicHeight + st::newGroupSkip + _private.height() + _aboutPrivate.countHeight(_aboutPublicWidth) + st::newGroupSkip + _comments.height() + _aboutComments.countHeight(_aboutPublicWidth) + st::newGroupPadding.bottom() + st::newGroupLinkPadding.top() + _link.height() + st::newGroupLinkPadding.bottom() + _save.height());

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_skip, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_link, SIGNAL(changed()), this, SLOT(onChange()));

	_checkTimer.setSingleShot(true);
	connect(&_checkTimer, SIGNAL(timeout()), this, SLOT(onCheck()));

	connect(&_public, SIGNAL(changed()), this, SLOT(onPrivacyChange()));
	connect(&_private, SIGNAL(changed()), this, SLOT(onPrivacyChange()));

	prepare();
}

void SetupChannelBox::hideAll() {
	_link.hide();
	_save.hide();
	_skip.hide();
}

void SetupChannelBox::showAll() {
	if (_public.checked()) {
		_link.show();
	} else {
		_link.hide();
	}
	_save.show();
	_skip.show();
}

void SetupChannelBox::showDone() {
	_link.setFocus();
}

void SetupChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_link.hasFocus()) {
			if (_link.text().trimmed().isEmpty()) {
				_link.setFocus();
				_link.notaBene();
			} else {
				onSave();
			}
		}
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void SetupChannelBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	p.setPen(st::newGroupAboutFg);

	QRect aboutPublic = rtlrect(st::newGroupPadding.left() + st::rbDefFlat.textLeft, _public.y() + _public.height(), width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::rbDefFlat.textLeft, _aboutPublicHeight, width());
	_aboutPublic.draw(p, aboutPublic.x(), aboutPublic.y(), aboutPublic.width());

	QRect aboutPrivate = rtlrect(st::newGroupPadding.left() + st::rbDefFlat.textLeft, _private.y() + _private.height(), width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::rbDefFlat.textLeft, _aboutPublicHeight, width());
	_aboutPrivate.draw(p, aboutPrivate.x(), aboutPrivate.y(), aboutPrivate.width());

	QRect aboutComments = rtlrect(st::newGroupPadding.left() + st::rbDefFlat.textLeft, _comments.y() + _comments.height(), width() - st::newGroupPadding.left() - st::newGroupPadding.right() - st::rbDefFlat.textLeft, _aboutPublicHeight, width());
	_aboutComments.draw(p, aboutComments.x(), aboutComments.y(), aboutComments.width());

	p.setPen(st::black);
	p.setFont(st::newGroupLinkFont);
	p.drawTextLeft(st::newGroupPadding.left(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop, width(), lang(_link.isHidden() ? lng_create_group_invite_link : lng_create_group_link));

	if (_link.isHidden()) {
		QTextOption option(style::al_left);
		option.setWrapMode(QTextOption::WrapAnywhere);
		p.setFont(_linkOver ? st::newGroupLink.font->underline() : st::newGroupLink.font);
		p.setPen(st::btnDefLink.color);
		p.drawText(_invitationLink, _channel->invitationUrl, option);
		if (!_goodTextLink.isEmpty() && a_goodOpacity.current() > 0) {
			p.setOpacity(a_goodOpacity.current());
			p.setPen(st::setGoodColor->p);
			p.setFont(st::setErrFont->f);
			p.drawTextRight(st::newGroupPadding.right(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::setErrFont->ascent, width(), _goodTextLink);
			p.setOpacity(1);
		}
	} else {
		p.setFont(st::newGroupLink.font);
		p.setPen(st::newGroupLink.phColor);
		p.drawText(QRect(_link.x() + st::newGroupLink.textMrg.left(), _link.y() + st::newGroupLink.textMrg.top(), _link.width(), _link.height() - st::newGroupLink.textMrg.top() - st::newGroupLink.textMrg.bottom()), _linkPlaceholder, style::al_left);

		if (!_errorText.isEmpty()) {
			p.setPen(st::setErrColor->p);
			p.setFont(st::setErrFont->f);
			p.drawTextRight(st::newGroupPadding.right(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::setErrFont->ascent, width(), _errorText);
		} else if (!_goodText.isEmpty()) {
			p.setPen(st::setGoodColor->p);
			p.setFont(st::setErrFont->f);
			p.drawTextRight(st::newGroupPadding.right(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::setErrFont->ascent, width(), _goodText);
		}
	}

	// paint shadow
	p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

void SetupChannelBox::resizeEvent(QResizeEvent *e) {
	_public.moveToLeft(st::newGroupPadding.left(), st::newGroupPadding.top(), width());
	_private.moveToLeft(st::newGroupPadding.left(), _public.y() + _public.height() + _aboutPublicHeight + st::newGroupSkip, width());
	_comments.moveToLeft(st::newGroupPadding.left(), _private.y() + _private.height() + _aboutPrivate.countHeight(_aboutPublicWidth) + st::newGroupSkip, width());

	_link.setGeometry(st::newGroupLinkPadding.left(), _comments.y() + _comments.height() + _aboutComments.countHeight(_aboutPublicWidth) + st::newGroupPadding.bottom() + st::newGroupLinkPadding.top(), width() - st::newGroupPadding.left() - st::newGroupPadding.right(), _link.height());
	_invitationLink = QRect(_link.x(), _link.y() + (_link.height() / 2) - st::newGroupLinkFont->height, _link.width(), 2 * st::newGroupLinkFont->height);

	int32 buttonTop = _link.y() + _link.height() + st::newGroupLinkPadding.bottom();
	_skip.moveToLeft(0, buttonTop, width());
	_save.moveToRight(0, buttonTop, width());
}

void SetupChannelBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void SetupChannelBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_linkOver) {
		App::app()->clipboard()->setText(_channel->invitationUrl);
		_goodTextLink = lang(lng_create_channel_link_copied);
		a_goodOpacity = anim::fvalue(1, 0);
		a_good.start();
	}
}

void SetupChannelBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool linkOver = _invitationLink.contains(p);
	if (linkOver != _linkOver) {
		_linkOver = linkOver;
		update();
		setCursor(_linkOver ? style::cur_pointer : style::cur_default);
	}
}

bool SetupChannelBox::goodAnimStep(float64 ms) {
	float dt = ms / st::newGroupLinkFadeDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_goodOpacity.finish();
	} else {
		a_goodOpacity.update(dt, anim::linear);
	}
	update();
	return res;
}

void SetupChannelBox::closePressed() {
	App::wnd()->showLayer(new ContactsBox(_channel), true);
}

void SetupChannelBox::onSave() {
	if (!_public.checked()) {
		if (!_comments.checked()) {
			MTP::send(MTPchannels_ToggleComments(_channel->inputChannel, MTP_bool(false)));
		}
		onClose();
	}

	if (_saveRequestId) return;

	QString link = _link.text().trimmed();
	if (link.isEmpty()) {
		_link.setFocus();
		_link.notaBene();
		return;
	}

	if (_comments.checked()) {
		MTP::send(MTPchannels_ToggleComments(_channel->inputChannel, MTP_bool(true)), RPCResponseHandler(), 0, 5);
	}
	_sentUsername = link;
	_saveRequestId = MTP::send(MTPchannels_UpdateUsername(_channel->inputChannel, MTP_string(_sentUsername)), rpcDone(&SetupChannelBox::onUpdateDone), rpcFail(&SetupChannelBox::onUpdateFail));
}

void SetupChannelBox::onChange() {
	QString name = _link.text().trimmed();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer.stop();
	} else {
		int32 i, len = name.size();
		for (int32 i = 0; i < len; ++i) {
			QChar ch = name.at(i);
			if ((ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_' && (ch != '@' || i > 0)) {
				if (_errorText != lang(lng_create_channel_link_bad_symbols)) {
					_errorText = lang(lng_create_channel_link_bad_symbols);
					update();
				}
				_checkTimer.stop();
				return;
			}
		}
		if (name.size() < MinUsernameLength) {
			if (_errorText != lang(lng_create_channel_link_too_short)) {
				_errorText = lang(lng_create_channel_link_too_short);
				update();
			}
			_checkTimer.stop();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer.start(UsernameCheckTimeout);
		}
	}
}

void SetupChannelBox::onCheck() {
	if (_checkRequestId) {
		MTP::cancel(_checkRequestId);
	}
	QString link = _link.text().trimmed();
	if (link.size() >= MinUsernameLength) {
		_checkUsername = link;
		_checkRequestId = MTP::send(MTPchannels_CheckUsername(_channel->inputChannel, MTP_string(link)), rpcDone(&SetupChannelBox::onCheckDone), rpcFail(&SetupChannelBox::onCheckFail));
	}
}

void SetupChannelBox::onPrivacyChange() {
	if (_public.checked()) {
		_link.show();
		_link.setFocus();
	} else {
		_link.hide();
		setFocus();
	}
	update();
}

void SetupChannelBox::onUpdateDone(const MTPBool &result) {
	_channel->setName(textOneLine(_channel->name), _sentUsername);
	onClose();
}

bool SetupChannelBox::onUpdateFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_saveRequestId = 0;
	QString err(error.type());
	if (err == "USERNAME_NOT_MODIFIED" || _sentUsername == _channel->username) {
		_channel->setName(textOneLine(_channel->name), textOneLine(_sentUsername));
		onClose();
		return true;
	} else if (err == "USERNAME_INVALID") {
		_link.setFocus();
		_link.notaBene();
		_errorText = lang(lng_create_channel_link_invalid);
		update();
		return true;
	} else if (err == "USERNAME_OCCUPIED" || err == "USERNAMES_UNAVAILABLE") {
		_link.setFocus();
		_link.notaBene();
		_errorText = lang(lng_create_channel_link_occupied);
		update();
		return true;
	}
	_link.setFocus();
	return true;
}

void SetupChannelBox::onCheckDone(const MTPBool &result) {
	_checkRequestId = 0;
	QString newError = (result.v || _checkUsername == _channel->username) ? QString() : lang(lng_create_channel_link_occupied);
	QString newGood = newError.isEmpty() ? lang(lng_create_channel_link_available) : QString();
	if (_errorText != newError || _goodText != newGood) {
		_errorText = newError;
		_goodText = newGood;
		update();
	}
}

bool SetupChannelBox::onCheckFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_checkRequestId = 0;
	QString err(error.type());
	if (err == "USERNAME_INVALID") {
		_errorText = lang(lng_create_channel_link_invalid);
		update();
		return true;
	} else if (err == "USERNAME_OCCUPIED" && _checkUsername != _channel->username) {
		_errorText = lang(lng_create_channel_link_occupied);
		update();
		return true;
	}
	_goodText = QString();
	_link.setFocus();
	return true;
}
