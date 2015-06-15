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

#include "confirmbox.h"

ContactsInner::ContactsInner(bool creatingChat) : _chat(0), _bot(0), _creatingChat(creatingChat), _addToChat(0),
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

ContactsInner::ContactsInner(ChatData *chat) : _chat(chat), _bot(0), _creatingChat(false), _addToChat(0),
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

ContactsInner::ContactsInner(UserData *bot) : _chat(0), _bot(bot), _creatingChat(false), _addToChat(0),
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
		if (r->history->peer->chat && !r->history->peer->asChat()->forbidden) {
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
	App::main()->addParticipants(_addToChat, QVector<UserData*>(1, _bot));
}

void ContactsInner::peerUpdated(PeerData *peer) {
	if (_chat && (!peer || peer == _chat)) {
		if (_chat->forbidden) {
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
			data->inchat = (_chat && !peer->chat) ? _chat->participants.contains(peer->asUser()) : false;
			data->check = false;
			data->name.setText(st::profileListNameFont, peer->name, _textNameOptions);
			if (peer->chat) {
				ChatData *chat = peer->asChat();
				if (chat->forbidden) {
					data->online = lang(lng_chat_status_unaccessible);
				} else {
					data->online = lng_chat_status_members(lt_count, chat->count);
				}
			} else {
				data->online = App::onlineText(peer->asUser(), _time);
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

	UserData *user = peer->chat ? 0 : peer->asUser();

	if (data->inchat || data->check || _selCount + (_chat ? _chat->count : 0) >= cMaxGroupCount()) {
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
	int32 iconw = (_chat || _creatingChat) ? st::profileCheckRect.pxWidth() : st::contactsImg.pxWidth();
	data->name.drawElided(p, left + st::profileListPhotoSize + st::participantDelta, st::profileListNameTop, width() - left - st::profileListPhotoSize - st::profileListPadding.width() - st::participantDelta - st::scrollDef.width - iconw);

	if (_chat || _creatingChat) {
		if (sel || data->check) {
			p.drawPixmap(QPoint(width() - st::profileCheckRect.pxWidth() - st::profileCheckDeltaX, st::profileListPadding.height() + (st::profileListPhotoSize - st::profileCheckRect.pxHeight()) / 2 - st::profileCheckDeltaY), App::sprite(), (data->check ? st::profileCheckActiveRect : st::profileCheckRect));
		}
	} else if (sel) {
		p.drawPixmap(QPoint(width() - st::contactsImg.pxWidth() - st::profileCheckDeltaX, st::profileListPadding.height() + (st::profileListPhotoSize - st::contactsImg.pxHeight()) / 2 - st::profileCheckDeltaY), App::sprite(), st::contactsImg);
	}

	bool uname = user && (data->online.at(0) == '@');
	p.setFont(st::profileSubFont->f);
	if (uname && !data->inchat && !data->check && !_lastQuery.isEmpty() && user->username.startsWith(_lastQuery, Qt::CaseInsensitive)) {
		int32 availw = width() - (left + st::profileListPhotoSize + st::profileListPadding.width() * 2);
		QString first = '@' + user->username.mid(0, _lastQuery.size()), second = user->username.mid(_lastQuery.size());
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
		} else if (user && (uname || App::onlineColorUse(user->onlineTill, _time))) {
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
	if (_chat || _creatingChat) {
		_time = unixtime();
		int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, from;
		if (_filter.isEmpty()) {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsername.size()) {
				if (d_byUsername[_byUsernameSel]->inchat) return;
				changeCheckState(d_byUsername[_byUsernameSel]);
			} else {
				if (!_sel || contactData(_sel)->inchat) return;
				changeCheckState(_sel);
			}
		} else {
			if (_byUsernameSel >= 0 && _byUsernameSel < _byUsernameFiltered.size()) {
				if (d_byUsernameFiltered[_byUsernameSel]->inchat) return;
				changeCheckState(d_byUsernameFiltered[_byUsernameSel]);

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
			if (bot() && peer->chat) {
				_addToChat = peer->asChat();
				ConfirmBox *box = new ConfirmBox(lng_bot_sure_invite(lt_group, peer->name));
				connect(box, SIGNAL(confirmed()), this, SLOT(onAddBot()));
				App::wnd()->replaceLayer(box);
			} else {
				App::wnd()->hideSettings(true);
				App::main()->showPeer(peer->id, 0, false, true);
				App::wnd()->hideLayer();
			}
		}
	}
	parentWidget()->update();
}

void ContactsInner::changeCheckState(DialogRow *row) {
	changeCheckState(contactData(row));
}

void ContactsInner::changeCheckState(ContactData *data) {
	if (data->check) {
		data->check = false;
		--_selCount;
	} else if (_selCount + (_chat ? _chat->count : 0) < cMaxGroupCount()) {
		data->check = true;
		++_selCount;
	}
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

void ContactsInner::peopleReceived(const QString &query, const QVector<MTPContactFound> &people) {
	_lastQuery = query.toLower().trimmed();
	if (_lastQuery.at(0) == '@') _lastQuery = _lastQuery.mid(1);
	int32 already = _byUsernameFiltered.size();
	_byUsernameFiltered.reserve(already + people.size());
	d_byUsernameFiltered.reserve(already + people.size());
	for (QVector<MTPContactFound>::const_iterator i = people.cbegin(), e = people.cend(); i != e; ++i) {
		int32 uid = i->c_contactFound().vuser_id.v, j = 0;
		for (; j < already; ++j) {
			if (_byUsernameFiltered[j]->id == App::peerFromUser(uid)) break;
		}
		if (j == already) {
			UserData *u = App::user(uid);
			if (u->botInfo && u->botInfo->cantJoinGroups && (_chat || _creatingChat)) continue; // skip bot's that can't be invited to groups

			ContactData *d = new ContactData();
			_byUsernameDatas.push_back(d);
			d->inchat = _chat ? _chat->participants.contains(u) : false;
			d->check = false;
			d->name.setText(st::profileListNameFont, u->name, _textNameOptions);
			d->online = '@' + u->username;

			_byUsernameFiltered.push_back(u);
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

UserData *ContactsInner::bot() const {
	return _bot;
}

bool ContactsInner::creatingChat() const {
	return _creatingChat;
}

ContactsInner::~ContactsInner() {
	for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
		delete *i;
	}
	if (_bot) delete _contacts;
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
	result.reserve(_contactsData.size());
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check && !i.key()->chat) {
			result.push_back(i.key()->asUser());
		}
	}
	for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->check) {
			result.push_back(_byUsername[i]);
		}
	}
	return result;
}

QVector<MTPInputUser> ContactsInner::selectedInputs() {
	QVector<MTPInputUser> result;
	result.reserve(_contactsData.size());
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check && !i.key()->chat) {
			result.push_back(i.key()->inputUser);
		}
	}
	for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->check) {
			result.push_back(_byUsername[i]->inputUser);
		}
	}
	return result;
}

PeerData *ContactsInner::selectedUser() {
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

ContactsBox::ContactsBox(bool creatingChat) : ItemListBox(st::boxNoTopScroll), _inner(creatingChat),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_create_group_next), st::btnSelectDone),
_cancel(this, lang(lng_contacts_done), creatingChat ? st::btnSelectCancel : st::contactsClose) {
	init();
}

ContactsBox::ContactsBox(ChatData *chat) : ItemListBox(st::boxNoTopScroll), _inner(chat),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_participant_invite), st::btnSelectDone),
_cancel(this, lang(lng_cancel), st::btnSelectCancel) {
	init();
}

ContactsBox::ContactsBox(UserData *bot) : ItemListBox(st::boxNoTopScroll), _inner(bot),
_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
_filter(this, st::contactsFilter, lang(lng_participant_filter)),
_next(this, lang(lng_create_group_next), st::btnSelectDone),
_cancel(this, lang(lng_cancel), st::contactsClose) {
	init();
}

void ContactsBox::init() {
	ItemListBox::init(&_inner, _cancel.height(), st::contactsAdd.height + st::newGroupNamePadding.top() + _filter.height() + st::newGroupNamePadding.bottom());

	if (_inner.chat() || _inner.creatingChat()) {
		_addContact.hide();
	} else {
		connect(&_addContact, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	}
	if (_inner.chat()) {
		connect(&_next, SIGNAL(clicked()), this, SLOT(onInvite()));
	} else if (_inner.creatingChat()) {
		connect(&_next, SIGNAL(clicked()), this, SLOT(onNext()));
	} else {
		_next.hide();
	}
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSel()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_filter, SIGNAL(cancelled()), this, SLOT(onClose()));
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
	if (_inner.chat() || _inner.creatingChat()) {
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

	if (_inner.chat() || _inner.creatingChat()) {
		paintTitle(p, lang(_inner.chat() ? lng_profile_add_participant : lng_create_new_group), true);

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
		return;
	}

	App::main()->addParticipants(_inner.chat(), users);
}

void ContactsBox::onNext() {
	MTPVector<MTPInputUser> users(MTP_vector<MTPInputUser>(_inner.selectedInputs()));
	const QVector<MTPInputUser> &v(users.c_vector().v);
	if (v.isEmpty()) {
		_filter.setFocus();
		_filter.notaBene();
	} else if (v.size() == 1) {
		App::main()->showPeer(_inner.selectedUser()->id);
	} else {
		App::wnd()->replaceLayer(new CreateGroupBox(users));
	}
}

void ContactsBox::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

CreateGroupBox::CreateGroupBox(const MTPVector<MTPInputUser> &users) : AbstractBox(), _users(users),
_createRequestId(0),
_name(this, st::newGroupName, lang(lng_dlg_new_group_name)),
_create(this, lang(lng_dlg_create_group), st::btnSelectDone),
_cancel(this, lang(lng_cancel), st::btnSelectCancel) {

	setMaxHeight(st::boxTitleHeight + st::addContactPadding.top() + _name.height() + st::addContactPadding.bottom() + _create.height());

	connect(&_create, SIGNAL(clicked()), this, SLOT(onCreate()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void CreateGroupBox::hideAll() {
	_name.hide();
	_cancel.hide();
	_create.hide();
}

void CreateGroupBox::showAll() {
	_name.show();
	_cancel.show();
	_create.show();
}

void CreateGroupBox::showDone() {
	_name.setFocus();
}

void CreateGroupBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_name.hasFocus()) {
			if (_name.text().trimmed().isEmpty()) {
				_name.setFocus();
				_name.notaBene();
			} else {
				onCreate();
			}
		}
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void CreateGroupBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_create_group_title), true);

	// paint shadow
	p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

void CreateGroupBox::resizeEvent(QResizeEvent *e) {
	_name.setGeometry(st::addContactPadding.left(), st::boxTitleHeight + st::addContactPadding.top(), width() - st::addContactPadding.left() - st::addContactPadding.right(), _name.height());

	int32 buttonTop = _name.y() + _name.height() + st::addContactPadding.bottom();
	_cancel.move(0, buttonTop);
	_create.move(width() - _create.width(), buttonTop);
}

void CreateGroupBox::onCreate() {
	if (_createRequestId) return;

	QString name = _name.text();
	if (name.isEmpty()) {
		_name.setFocus();
		_name.notaBene();
		return;
	}

	_create.setDisabled(true);
	_name.setDisabled(true);
	_createRequestId = MTP::send(MTPmessages_CreateChat(_users, MTP_string(_name.text())), rpcDone(&CreateGroupBox::created), rpcFail(&CreateGroupBox::failed));
}

void CreateGroupBox::created(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);

	App::wnd()->hideLayer();
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
	case mtpc_updatesTooLong: {
	} break;
	}
	if (v && !v->isEmpty() && v->front().type() == mtpc_chat) {
		App::main()->showPeer(App::peerFromChat(v->front().c_chat().vid.v));
	}
}

bool CreateGroupBox::failed(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_createRequestId = 0;
	if (error.type() == "NO_CHAT_TITLE") {
		_name.setFocus();
		return true;
	} else if (error.type() == "USERS_TOO_FEW") {
		emit closed();
		return true;
	}
	return false;
}
