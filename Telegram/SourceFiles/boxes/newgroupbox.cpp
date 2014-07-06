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

#include "newgroupbox.h"
#include "mainwidget.h"
#include "window.h"

NewGroupInner::NewGroupInner() : _contacts(&App::main()->contactsList()), _sel(0), _filteredSel(-1), _mouseSel(false), _selCount(0) {

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

void NewGroupInner::peerUpdated(PeerData *peer) {
	if (!peer->chat) {
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

void NewGroupInner::loadProfilePhotos(int32 yFrom) {
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

NewGroupInner::ContactData *NewGroupInner::contactData(DialogRow *row) {
	ContactData *data = (ContactData*)row->attached;
	if (!data) {
		UserData *user = row->history->peer->asUser();
		ContactsData::const_iterator i = _contactsData.constFind(user);
		if (i == _contactsData.cend()) {
			_contactsData.insert(user, data = new ContactData());
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

void NewGroupInner::paintDialog(QPainter &p, DialogRow *row, bool sel) {
	int32 left = st::profileListPadding.width();

	UserData *user = row->history->peer->asUser();
	ContactData *data = contactData(row);

	if (_selCount >= cMaxGroupCount() && !data->check) {
		sel = false;
	}

	if (sel || data->check) {
		p.fillRect(0, 0, width(), 2 * st::profileListPadding.height() + st::profileListPhotoSize, (data->check ? st::profileActiveBG : st::profileHoverBG)->b);
	}

	p.drawPixmap(left, st::profileListPadding.height(), user->photo->pix(st::profileListPhotoSize));

	if (data->check) {
		p.setPen(st::white->p);
	} else {
		p.setPen(st::profileListNameColor->p);
	}
	data->name.drawElided(p, left + st::profileListPhotoSize + st::participantDelta, st::profileListNameTop, width() - st::profileListPadding.width() - st::profileListPhotoSize - st::profileListPadding.width() - st::participantDelta - st::scrollDef.width - st::profileCheckRect.pxWidth());

	if (sel || data->check) {
		p.drawPixmap(QPoint(width() - st::profileCheckRect.pxWidth() - st::profileCheckDeltaX, st::profileListPadding.height() + (st::profileListPhotoSize - st::profileCheckRect.pxHeight()) / 2 - st::profileCheckDeltaY), App::sprite(), (data->check ? st::profileCheckActiveRect : st::profileCheckRect));
	}

	p.setFont(st::profileSubFont->f);
	if (data->check) {
		p.setPen(st::white->p);
	} else {
		p.setPen((user->onlineTill >= _time ? st::profileOnlineColor : st::profileOfflineColor)->p);
	}
	p.drawText(left + st::profileListPhotoSize + st::participantDelta, st::profileListPadding.height() + st::profileListPhotoSize - 6, data->online);
}

void NewGroupInner::paintEvent(QPaintEvent *e) {
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

void NewGroupInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void NewGroupInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	updateSel();
}

void NewGroupInner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
}

void NewGroupInner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (e->button() == Qt::LeftButton) {
		chooseParticipant();
	}
}

void NewGroupInner::changeCheckState(DialogRow *row) {
	if (contactData(row)->check) {
		contactData(row)->check = false;
		--_selCount;
	} else if (_selCount < cMaxGroupCount()) {
		contactData(row)->check = true;
		++_selCount;
	}
}

void NewGroupInner::chooseParticipant() {
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, from;
	if (_filter.isEmpty()) {
		if (!_sel) return;
		changeCheckState(_sel);
	} else {
		if (_filteredSel < 0 || _filteredSel >= _filtered.size()) return;

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

void NewGroupInner::updateSel() {
	if (!_mouseSel) return;

	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	QPoint p(mapFromGlobal(_lastMousePos));
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

void NewGroupInner::updateFilter(QString filter) {
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

			resize(width(), _filtered.size() * rh);
		}
		if (parentWidget()) parentWidget()->update();
		loadProfilePhotos(0);
	}
}

void NewGroupInner::onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow) {
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

NewGroupInner::~NewGroupInner() {
	for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
		delete *i;
	}
}

void NewGroupInner::selectSkip(int32 dir) {
	_mouseSel = false;
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, origDir = dir;
	if (_filter.isEmpty()) {
		if (_sel) {
			if (dir > 0) {
				while (dir && _sel->next->next) {
					_sel = _sel->next;
					--dir;
				}
			} else {
				while (dir && _sel->prev) {
					_sel = _sel->prev;
					++dir;
				}
			}
		} else if (dir > 0 && _contacts->list.count) {
			_sel = _contacts->list.begin;
		}
		if (_sel) {
			emit mustScrollTo(_sel->pos * rh, (_sel->pos + 1) * rh);
		}
	} else {
		if (dir > 0) {
			if (_filteredSel < 0 && dir > 1) {
				_filteredSel = 0;
			}
			_filteredSel += dir;
			if (_filteredSel >= _filtered.size()) {
				_filteredSel = _filtered.size() - 1;
			}
		} else if (_filteredSel > 0) {
			_filteredSel += dir;
			if (_filteredSel < 0) {
				_filteredSel = 0;
			}
		}
		if (_filteredSel >= 0) {
			emit mustScrollTo(_filteredSel * rh, (_filteredSel + 1) * rh);
		}
	}
	parentWidget()->update();
}

void NewGroupInner::selectSkipPage(int32 h, int32 dir) {
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2;
	int32 points = h / rh;
	if (!points) return;
	selectSkip(points * dir);
}

QVector<MTPInputUser> NewGroupInner::selectedInputs() {
	QVector<MTPInputUser> result;
	result.reserve(_contactsData.size());
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check) {
			result.push_back(i.key()->inputUser);
		}
	}
	return result;
}

PeerData *NewGroupInner::selectedUser() {
	for (ContactsData::const_iterator i = _contactsData.cbegin(), e = _contactsData.cend(); i != e; ++i) {
		if (i.value()->check) {
			return i.key();
		}
	}
	return 0;
}

NewGroupBox::NewGroupBox() : _scroll(this, st::newGroupScroll), _inner(),
	_filter(this, st::contactsFilter, lang(lng_participant_filter)),
	_next(this, lang(lng_create_group_next), st::btnSelectDone),
	_cancel(this, lang(lng_cancel), st::btnSelectCancel),
    _hiding(false),	a_opacity(0, 1) {

	_width = st::participantWidth;
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();
	if (_height > st::participantMaxHeight) _height = st::participantMaxHeight;

	resize(_width, _height);

	_scroll.setWidget(&_inner);
	_scroll.setFocusPolicy(Qt::NoFocus);

	connect(&_next, SIGNAL(clicked()), this, SLOT(onNext()));
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

void NewGroupBox::hideAll() {
	_filter.hide();
	_scroll.hide();
	_next.hide();
	_cancel.hide();
}

void NewGroupBox::showAll() {
	_filter.show();
	_scroll.show();
	_next.show();
	_cancel.show();
}

void NewGroupBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		_inner.chooseParticipant();
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
			e->ignore();
		}
	} else {
		e->ignore();
	}
}

void NewGroupBox::parentResized() {
	QSize s = parentWidget()->size();
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();
	if (_height > st::participantMaxHeight) _height = st::participantMaxHeight;
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void NewGroupBox::paintEvent(QPaintEvent *e) {
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
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_create_new_group));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void NewGroupBox::resizeEvent(QResizeEvent *e) {
	LayeredWidget::resizeEvent(e);
	_filter.move(st::newGroupNamePadding.left(), st::contactsAdd.height + st::newGroupNamePadding.top());
	_inner.resize(_width, _inner.height());
	_scroll.resize(_width, _height - st::contactsAdd.height - st::newGroupNamePadding.top() - _filter.height() - st::newGroupNamePadding.bottom() - _cancel.height());
	_scroll.move(0, _filter.y() + _filter.height() + st::newGroupNamePadding.bottom());
	_next.move(width() - _next.width(), _height - _next.height());
	_cancel.move(0, _height - _cancel.height());
}

void NewGroupBox::animStep(float64 dt) {
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

void NewGroupBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

void NewGroupBox::onFilterUpdate() {
	_scroll.scrollToY(0);
	_inner.updateFilter(_filter.text());
}

void NewGroupBox::onClose() {
	emit closed();
}

void NewGroupBox::onNext() {
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

void NewGroupBox::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

NewGroupBox::~NewGroupBox() {
}

CreateGroupBox::CreateGroupBox(const MTPVector<MTPInputUser> &users) : _users(users),
	_createRequestId(0),
	_name(this, st::newGroupName, lang(lng_dlg_new_group_name)),
	_create(this, lang(lng_dlg_create_group), st::btnSelectDone),
	_cancel(this, lang(lng_cancel), st::btnSelectCancel),
    _hiding(false),	a_opacity(0, 1) {
	_width = st::addContactWidth;

	_height = st::addContactTitleHeight + st::addContactPadding.top() + _name.height() + st::addContactPadding.bottom() + _create.height();

	_name.setGeometry(st::addContactPadding.left(), st::addContactTitleHeight + st::addContactPadding.top(), _width - st::addContactPadding.left() - st::addContactPadding.right(), _name.height());

	int32 buttonTop = _name.y() + _name.height() + st::addContactPadding.bottom();
	_cancel.move(0, buttonTop);
	_create.move(_width - _create.width(), buttonTop);

	connect(&_create, SIGNAL(clicked()), this, SLOT(onCreate()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onCancel()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
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
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void CreateGroupBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void CreateGroupBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);
			p.fillRect(0, _height - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.fillRect(st::btnSelectCancel.width, _height - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

			// draw box title / text
			p.setPen(st::black->p);
			p.setFont(st::addContactTitleFont->f);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_create_group_title));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void CreateGroupBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			_name.setFocus();
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
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

void CreateGroupBox::created(const MTPmessages_StatedMessage &result) {
	App::main()->sentFullDataReceived(0, result);
	const QVector<MTPChat> *d = 0;
	switch (result.type()) {
	case mtpc_messages_statedMessage: {
		d = &result.c_messages_statedMessage().vchats.c_vector().v;
	} break;

	case mtpc_messages_statedMessageLink: {
		d = &result.c_messages_statedMessageLink().vchats.c_vector().v;
	} break;
	}
	App::wnd()->hideLayer();
	PeerId peerId = 0;
	if (d && !d->isEmpty()) {
		switch (d->first().type()) {
		case mtpc_chat: peerId = App::peerFromChat(d->first().c_chat().vid); break;
		case mtpc_chatForbidden: peerId = App::peerFromChat(d->first().c_chatForbidden().vid); break;
		case mtpc_chatEmpty: peerId = App::peerFromChat(d->first().c_chatEmpty().vid); break;
		}
	}
	if (peerId) {
		App::main()->showPeer(peerId);
	}
}

bool CreateGroupBox::failed(const RPCError &e) {
	_createRequestId = 0;
	if (e.type() == "NO_CHAT_TITLE") {
		_name.setFocus();
		return true;
	} else if (e.type() == "USERS_TOO_FEW") {
		emit closed();
		return true;
	}
	return false;
}

void CreateGroupBox::onCancel() {
	emit closed();
}

void CreateGroupBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

CreateGroupBox::~CreateGroupBox() {
}
