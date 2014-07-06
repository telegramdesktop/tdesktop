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

#include "addparticipantbox.h"
#include "mainwidget.h"
#include "window.h"

AddParticipantInner::AddParticipantInner(ChatData *chat) : _chat(chat),
	_contacts(&App::main()->contactsList()), _sel(0), _filteredSel(-1), _mouseSel(false), _selCount(0) {
	
	_filter = qsl("a");
	updateFilter();

	for (DialogRow *r = _contacts->list.begin; r != _contacts->list.end; r = r->next) {
		r->attached = 0;
	}

	connect(App::main(), SIGNAL(dialogRowReplaced(DialogRow *, DialogRow *)), this, SLOT(onDialogRowReplaced(DialogRow *, DialogRow *)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
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

AddParticipantInner::ContactData *AddParticipantInner::contactData(DialogRow *row) {
	ContactData *data = (ContactData*)row->attached;
	if (!data) {
		UserData *user = row->history->peer->asUser();
		ContactsData::const_iterator i = _contactsData.constFind(user);
		if (i == _contactsData.cend()) {
			_contactsData.insert(user, data = new ContactData());
			data->inchat = _chat->participants.constFind(user) != _chat->participants.cend();
			data->check = false;
			data->name.setText(st::profileListNameFont, user->name, _textNameOptions);
			data->online = App::onlineText(user->onlineTill, _time);
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void AddParticipantInner::paintDialog(QPainter &p, DialogRow *row, bool sel) {
	int32 left = st::profileListPadding.width();

	UserData *user = row->history->peer->asUser();
	ContactData *data = contactData(row);

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

	p.setFont(st::profileSubFont->f);
	if (data->inchat || data->check) {
		p.setPen(st::white->p);
	} else {
		p.setPen((user->onlineTill >= _time ? st::profileOnlineColor : st::profileOfflineColor)->p);
	}
	p.drawText(left + st::profileListPhotoSize + st::profileListPadding.width(), st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, data->online);
}

void AddParticipantInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	QPainter p(this);

	_time = unixtime();
	p.fillRect(r, st::white->b);

	int32 yFrom = r.top();
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	if (_filter.isEmpty()) {
		if (_contacts->list.count) {
			_contacts->list.adjustCurrent(yFrom, rh);

			DialogRow *drawFrom = _contacts->list.current;
			p.translate(0, drawFrom->pos * rh);
			while (drawFrom != _contacts->list.end && drawFrom->pos * rh < r.bottom()) {
				paintDialog(p, drawFrom, (drawFrom == _sel));
				p.translate(0, rh);
				drawFrom = drawFrom->next;
			}
		} else {
			// ..
		}
	} else {
		if (_filtered.isEmpty()) {
			// ..
		} else {
			int32 from = yFrom / rh;
			if (from < 0) from = 0;
			if (from < _filtered.size()) {
				int32 to = (r.bottom() / rh) + 1;
				if (to > _filtered.size()) to = _filtered.size();

				p.translate(0, from * rh);
				for (; from < to; ++from) {
					paintDialog(p, _filtered[from], (_filteredSel == from));
					p.translate(0, rh);
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
	updateSel();
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
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, from;
	if (_filter.isEmpty()) {
		if (!_sel || contactData(_sel)->inchat) return;
		changeCheckState(_sel);
	} else {
		if (_filteredSel < 0 || _filteredSel >= _filtered.size() || contactData(_filtered[_filteredSel])->inchat) return;

		DialogRow *row = _filtered[_filteredSel];
		changeCheckState(row);

		PeerData *peer = row->history->peer;
		updateFilter();

		for (_sel = _contacts->list.begin; _sel != _contacts->list.end; _sel = _sel->next) {
			if (_sel->history->peer == peer) {
				break;
			}
		}
		if (_sel == _contacts->list.end) {
			_sel = 0;
		} else {
			emit mustScrollTo(_sel->pos * rh, (_sel->pos + 1) * rh);
		}
	}
	parentWidget()->update();
}

void AddParticipantInner::changeCheckState(DialogRow *row) {
	if (contactData(row)->check) {
		contactData(row)->check = false;
		--_selCount;
	} else if (_selCount + _chat->count < cMaxGroupCount()) {
		contactData(row)->check = true;
		++_selCount;
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
	return result;
}

void AddParticipantInner::updateSel() {
	if (!_mouseSel) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	if (_filter.isEmpty()) {
		DialogRow *newSel = rect().contains(p) ? _contacts->list.rowAtY(p.y(), rh) : 0;
		if (newSel != _sel) {
			_sel = newSel;
			parentWidget()->update();
		}		
	} else {
		int32 newFilteredSel = (p.y() >= 0 && rect().contains(p)) ? (p.y() / rh) : -1;
		if (newFilteredSel != _filteredSel) {
			_filteredSel = newFilteredSel;
			parentWidget()->update();
		}
	}
}

void AddParticipantInner::updateFilter(QString filter) {
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
		if (_filter.isEmpty()) {
			resize(width(), _contacts->list.count * rh);
			if (_contacts->list.count) {
				_sel = _contacts->list.begin;
				while (_sel->next->next &&& contactData(_sel)->inchat) {
					_sel = _sel->next;
				}
			}
		} else {
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
								if ((*ni).indexOf(*fi) == 0) {
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
			}
			_filteredSel = _filtered.isEmpty() ? -1 : 0;
			while (_filteredSel < _filtered.size() - 1 && contactData(_filtered[_filteredSel])->inchat) {
				++_filteredSel;
			}

			resize(width(), _filtered.size() * rh);
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
	for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
		delete *i;
	}
}

void AddParticipantInner::selectSkip(int32 dir) {
	_mouseSel = false;
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, origDir = dir;
	if (_filter.isEmpty()) {
		if (_sel) {
			if (dir > 0) {
				while (dir && _sel->next->next) {
					_sel = _sel->next;
					--dir;
				}
				while (contactData(_sel)->inchat && _sel->next->next) {
					_sel = _sel->next;
				}
				if (contactData(_sel)->inchat) {
					while (contactData(_sel)->inchat && _sel->prev) {
						_sel = _sel->prev;
					}
				}
			} else {
				while (dir && _sel->prev) {
					_sel = _sel->prev;
					++dir;
				}
				while (contactData(_sel)->inchat && _sel->prev) {
					_sel = _sel->prev;
				}
				if (contactData(_sel)->inchat) {
					while (contactData(_sel)->inchat && _sel->next->next) {
						_sel = _sel->next;
					}
				}
			}
		} else if (dir > 0 && _contacts->list.count) {
			_sel = _contacts->list.begin;
			while (contactData(_sel)->inchat && _sel->next->next) {
				_sel = _sel->next;
			}
		}
		if (_sel) {
			if (contactData(_sel)->inchat) {
				_sel = 0;
			} else {
				emit mustScrollTo(_sel->pos * rh, (_sel->pos + 1) * rh);
			}
		}
	} else {
		if (dir > 0) {
			if (_filteredSel < 0 && dir > 1) {
				_filteredSel = 0;
			}
			_filteredSel += dir;
			while (_filteredSel < _filtered.size() - 1 && contactData(_filtered[_filteredSel])->inchat) {
				++_filteredSel;
			}
			if (_filteredSel >= _filtered.size()) {
				_filteredSel = _filtered.size() - 1;
			}
			while (_filteredSel > 0 && contactData(_filtered[_filteredSel])->inchat) {
				--_filteredSel;
			}
		} else if (_filteredSel > 0) {
			_filteredSel += dir;
			if (_filteredSel < 0) {
				_filteredSel = 0;
			}
			if (_filteredSel < _filtered.size() - 1) {
				while (_filteredSel > 0 && contactData(_filtered[_filteredSel])->inchat) {
					--_filteredSel;
				}
			}
			while (_filteredSel < _filtered.size() - 1 && contactData(_filtered[_filteredSel])->inchat) {
				++_filteredSel;
			}
		}
		if (_filteredSel >= 0) {
			if (contactData(_filtered[_filteredSel])->inchat) {
				_filteredSel = -1;
			} else {
				emit mustScrollTo(_filteredSel * rh, (_filteredSel + 1) * rh);
			}
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
	_hiding(false), a_opacity(0, 1), af_opacity(anim::linear) {

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
	connect(&_filter, SIGNAL(cancelled()), this, SIGNAL(onClose()));
	connect(&_inner, SIGNAL(mustScrollTo(int,int)), &_scroll, SLOT(scrollToY(int,int)));

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
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
		a_opacity.update(dt, af_opacity);
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
