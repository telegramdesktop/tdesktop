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

#include "addparticipantbox.h"
#include "mainwidget.h"
#include "window.h"

AddParticipantInner::AddParticipantInner(ChatData *chat) : _chat(chat),
_contacts(&App::main()->contactsList()),
_sel(0),
_filteredSel(-1),
_mouseSel(false),
_selCount(0),
_searching(false),
_byUsernameSel(-1),
_addContactLnk(this, lang(lng_add_contact_button)) {

	connect(&_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));

	for (DialogRow *r = _contacts->list.begin; r != _contacts->list.end; r = r->next) {
		r->attached = 0;
	}

	_filter = qsl("a");
	updateFilter();

	connect(App::main(), SIGNAL(dialogRowReplaced(DialogRow*,DialogRow*)), this, SLOT(onDialogRowReplaced(DialogRow*,DialogRow*)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)), this, SLOT(peerUpdated(PeerData*)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
}

void AddParticipantInner::peerUpdated(PeerData *peer) {
	if (!peer || peer == _chat) {
		if (_chat->forbidden) {
			App::wnd()->hideLayer();
		} else if (!_chat->participants.isEmpty() || _chat->count <= 0) {
			for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i ) {
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
	} else if (!peer->chat) {
		ContactsData::iterator i = _contactsData.find(peer->asUser());
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

void AddParticipantInner::loadProfilePhotos(int32 yFrom) {
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
		yFrom -= _contacts->list.count * rh + st::searchedBarHeight;
		yTo -= _contacts->list.count * rh + st::searchedBarHeight;
		int32 from = (yFrom >= 0) ? (yFrom / rh) : 0;
		if (from < _byUsername.size()) {
			int32 to = (yTo / rh) + 1;
			if (to > _byUsername.size()) to = _byUsername.size();
			for (; from < to; ++from) {
				_byUsername[from]->photo->load();
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
		yFrom -= _filtered.size() * rh + st::searchedBarHeight;
		yTo -= _filtered.size() * rh + st::searchedBarHeight;
		from = (yFrom >= 0) ? (yFrom / rh) : 0;
		if (from < _byUsernameFiltered.size()) {
			int32 to = (yTo / rh) + 1;
			if (to > _byUsernameFiltered.size()) to = _byUsernameFiltered.size();
			for (; from < to; ++from) {
				_byUsernameFiltered[from]->photo->load();
			}
		}
	}
}

AddParticipantInner::ContactData *AddParticipantInner::contactData(DialogRow *row) {
	ContactData *data = (ContactData*)row->attached;
	if (!data) {
		UserData *user = row->history->peer->asUser();
		ContactsData::const_iterator i = _contactsData.constFind(user);
		if (i == _contactsData.cend()) {
			_contactsData.insert(user, data = new ContactData());
			data->inchat = _chat->participants.contains(user);
			data->check = false;
			data->name.setText(st::profileListNameFont, user->name, _textNameOptions);
			data->online = App::onlineText(user, _time);
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void AddParticipantInner::paintDialog(QPainter &p, UserData *user, ContactData *data, bool sel) {
	int32 left = st::profileListPadding.width();

	if (data->inchat || data->check || _selCount + _chat->count >= cMaxGroupCount()) {
		sel = false;
	}

	if (sel || data->inchat || data->check) {
		p.fillRect(0, 0, width(), 2 * st::profileListPadding.height() + st::profileListPhotoSize, ((data->inchat || data->check) ? st::profileActiveBG : st::profileHoverBG)->b);
	}

	p.drawPixmap(left, st::profileListPadding.height(), user->photo->pix(st::profileListPhotoSize));

	if (data->inchat || data->check) {
		p.setPen(st::white->p);
	} else {
		p.setPen(st::profileListNameColor->p);
	}
	data->name.drawElided(p, left + st::profileListPhotoSize + st::profileListPadding.width(), st::profileListNameTop, width() - st::participantDelta - st::profileListPadding.width() * 2 - st::profileListPhotoSize - st::profileListPadding.width() * 2);

	if (sel || data->check) {
		p.drawPixmap(QPoint(width() - st::profileCheckRect.pxWidth() - st::profileCheckDeltaX, st::profileListPadding.height() + (st::profileListPhotoSize - st::profileCheckRect.pxHeight()) / 2 - st::profileCheckDeltaY), App::sprite(), (data->check ? st::profileCheckActiveRect : st::profileCheckRect));
	}

	bool uname = (data->online.at(0) == '@');
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
		} else {
			p.setPen(((uname || App::onlineColorUse(user->onlineTill, _time)) ? st::profileOnlineColor : st::profileOfflineColor)->p);
		}
		p.drawText(left + st::profileListPhotoSize + st::profileListPadding.width(), st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, data->online);
	}
}

void AddParticipantInner::paintEvent(QPaintEvent *e) {
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
					paintDialog(p, drawFrom->history->peer->asUser(), contactData(drawFrom), (drawFrom == _sel));
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
			p.drawText(QRect(0, 0, width(), st::noContactsHeight - ((cContactsReceived() && !_searching) ? st::noContactsFont->height : 0)), lang((cContactsReceived() && !_searching) ? lng_no_contacts : lng_contacts_loading), style::al_center);
		}
	} else {
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			p.setFont(st::noContactsFont->f);
			p.setPen(st::noContactsColor->p);
			p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang((cContactsReceived() && !_searching) ? lng_no_contacts : lng_contacts_loading), style::al_center);
		} else {
			if (!_filtered.isEmpty()) {
				int32 from = (yFrom >= 0) ? (yFrom / rh) : 0;
				if (from < _filtered.size()) {
					int32 to = (yTo / rh) + 1;
					if (to > _filtered.size()) to = _filtered.size();

					p.translate(0, from * rh);
					for (; from < to; ++from) {
						paintDialog(p, _filtered[from]->history->peer->asUser(), contactData(_filtered[from]), (_filteredSel == from));
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

void AddParticipantInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void AddParticipantInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	if (_sel || _filteredSel >= 0 || _byUsernameSel >= 0) {
		_sel = 0;
		_filteredSel = _byUsernameSel = -1;
		parentWidget()->update();
	}
}

void AddParticipantInner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
}

void AddParticipantInner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (e->button() == Qt::LeftButton) {
		chooseParticipant();
	}
}

void AddParticipantInner::chooseParticipant() {
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
	parentWidget()->update();
}

void AddParticipantInner::changeCheckState(DialogRow *row) {
	changeCheckState(contactData(row));
}

void AddParticipantInner::changeCheckState(ContactData *data) {
	if (data->check) {
		data->check = false;
		--_selCount;
	} else if (_selCount + _chat->count < cMaxGroupCount()) {
		data->check = true;
		++_selCount;
	}
}

void AddParticipantInner::peopleReceived(const QString &query, const QVector<MTPContactFound> &people) {
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
			ContactData *d = new ContactData();
			_byUsernameDatas.push_back(d);
			d->inchat = _chat->participants.contains(u);
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

void AddParticipantInner::refresh() {
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	if (_filter.isEmpty()) {
		if (_contacts->list.count || !_byUsername.isEmpty()) {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			resize(width(), (_contacts->list.count * rh) + (_byUsername.isEmpty() ? 0 : (st::searchedBarHeight + _byUsername.size() * rh)));
		} else {
			if (cContactsReceived()) {
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
}

ChatData *AddParticipantInner::chat() {
	return _chat;
}

QVector<UserData*> AddParticipantInner::selected() {
	QVector<UserData*> result;
	result.reserve(_contactsData.size());
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check) {
			result.push_back(i.key());
		}
	}
	for (int32 i = 0, l = _byUsername.size(); i < l; ++i) {
		if (d_byUsername[i]->check) {
			result.push_back(_byUsername[i]);
		}
	}
	return result;
}

void AddParticipantInner::updateSel() {
	if (!_mouseSel) return;

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

void AddParticipantInner::updateFilter(QString filter) {
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
				while (_sel->next->next &&& contactData(_sel)->inchat) {
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

			_searching = true;
			emit searchByUsername();
		}
		if (parentWidget()) parentWidget()->update();
		loadProfilePhotos(0);
	}
}

void AddParticipantInner::onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow) {
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
	int32 newh = (_filter.isEmpty() ? _contacts->list.count : _filtered.size()) * rh;
	resize(width(), newh);
}

AddParticipantInner::~AddParticipantInner() {
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		delete *i;
	}
	for (ByUsernameDatas::const_iterator i = d_byUsername.cbegin(), e = d_byUsername.cend(); i != e; ++i) {
		delete *i;
	}
	for (ByUsernameDatas::const_iterator i = _byUsernameDatas.cbegin(), e = _byUsernameDatas.cend(); i != e; ++i) {
		delete *i;
	}
}

void AddParticipantInner::resizeEvent(QResizeEvent *e) {
	_addContactLnk.move((width() - _addContactLnk.width()) / 2, (st::noContactsHeight + st::noContactsFont->height) / 2);
}

void AddParticipantInner::selectSkip(int32 dir) {
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
			_sel = 0;
			_byUsernameSel = cur - _contacts->list.count;
			if (_byUsernameSel >= _byUsername.size()) _byUsernameSel = _byUsername.size() - 1;
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

void AddParticipantInner::selectSkipPage(int32 h, int32 dir) {
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	int32 points = h / rh;
	if (!points) return;
	selectSkip(points * dir);
}

AddParticipantBox::AddParticipantBox(ChatData *chat) :
	_scroll(this, st::newGroupScroll), _inner(chat),
	_filter(this, st::contactsFilter, lang(lng_participant_filter)),
	_invite(this, lang(lng_participant_invite), st::btnSelectDone),
	_cancel(this, lang(lng_cancel), st::btnSelectCancel),
	_hiding(false), a_opacity(0, 1) {

	_width = st::participantWidth;
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();
	if (_height > st::participantMaxHeight) _height = st::participantMaxHeight;

	resize(_width, _height);

	_scroll.setWidget(&_inner);
	_scroll.setFocusPolicy(Qt::NoFocus);

	connect(&_invite, SIGNAL(clicked()), this, SLOT(onInvite()));
	connect(&_cancel, SIGNAL(clicked()), this, SIGNAL(closed()));
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSel()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_filter, SIGNAL(cancelled()), this, SLOT(onClose()));
	connect(&_inner, SIGNAL(mustScrollTo(int,int)), &_scroll, SLOT(scrollToY(int,int)));
	connect(&_inner, SIGNAL(selectAllQuery()), &_filter, SLOT(selectAll()));
	connect(&_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

bool AddParticipantBox::onSearchByUsername(bool searchCache) {
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
			_peopleRequest = MTP::send(MTPcontacts_Search(MTP_string(_peopleQuery), MTP_int(SearchPeopleLimit)), rpcDone(&AddParticipantBox::peopleReceived), rpcFail(&AddParticipantBox::peopleFailed));
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void AddParticipantBox::onNeedSearchByUsername() {
	if (!onSearchByUsername(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void AddParticipantBox::peopleReceived(const MTPcontacts_Found &result, mtpRequestId req) {
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

bool AddParticipantBox::peopleFailed(const RPCError &error, mtpRequestId req) {
	if (_peopleRequest == req) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
	return true;
}

void AddParticipantBox::hideAll() {
	_filter.hide();
	_scroll.hide();
	_cancel.hide();
	_invite.hide();
}

void AddParticipantBox::showAll() {
	_filter.show();
	_scroll.show();
	_cancel.show();
	_invite.show();
}

void AddParticipantBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		_inner.chooseParticipant();
	} else if (e->key() == Qt::Key_Down) {
		_inner.selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner.selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner.selectSkipPage(_scroll.height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner.selectSkipPage(_scroll.height(), -1);
	} else {
		e->ignore();
	}
}

void AddParticipantBox::parentResized() {
	QSize s = parentWidget()->size();
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();
	if (_height > st::participantMaxHeight) _height = st::participantMaxHeight;
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void AddParticipantBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::participantFilter.height, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);
			p.fillRect(0, size().height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.fillRect(st::btnSelectCancel.width, size().height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

			// draw box title / text
			p.setPen(st::black->p);
			p.setFont(st::addContactTitleFont->f);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_profile_add_participant));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void AddParticipantBox::resizeEvent(QResizeEvent *e) {
	LayeredWidget::resizeEvent(e);
	_filter.move(st::newGroupNamePadding.left(), st::contactsAdd.height + st::newGroupNamePadding.top());
	_inner.resize(_width, _inner.height());
	_scroll.resize(_width, _height - st::contactsAdd.height - st::newGroupNamePadding.top() - _filter.height() - st::newGroupNamePadding.bottom() - _cancel.height());
	_scroll.move(0, _filter.y() + _filter.height() + st::newGroupNamePadding.bottom());
	_invite.move(width() - _invite.width(), _height - _invite.height());
	_cancel.move(0, _height - _cancel.height());
}

void AddParticipantBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			_filter.setFocus();
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
}

void AddParticipantBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

void AddParticipantBox::onFilterUpdate() {
	_scroll.scrollToY(0);
	_inner.updateFilter(_filter.text());
}

void AddParticipantBox::onClose() {
	emit closed();
}

void AddParticipantBox::onInvite() {
	QVector<UserData*> users(_inner.selected());
	if (users.isEmpty()) {
		_filter.setFocus();
		return;
	}

	App::main()->addParticipants(_inner.chat(), users);
}

void AddParticipantBox::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

AddParticipantBox::~AddParticipantBox() {

}
