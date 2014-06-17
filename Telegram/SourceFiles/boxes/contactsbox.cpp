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

#include "addcontactbox.h"
#include "contactsbox.h"
#include "mainwidget.h"
#include "window.h"

ContactsInner::ContactsInner() : _contacts(&App::main()->contactsList()), _sel(0), _filteredSel(-1), _mouseSel(false) {
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

void ContactsInner::peerUpdated(PeerData *peer) {
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
		UserData *user = row->history->peer->asUser();
		ContactsData::const_iterator i = _contactsData.constFind(user);
		if (i == _contactsData.cend()) {
			_contactsData.insert(user, data = new ContactData());
			data->name.setText(st::profileListNameFont, user->name, _textNameOptions);
			data->online = App::onlineText(user->onlineTill, _time);
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void ContactsInner::paintDialog(QPainter &p, DialogRow *row, bool sel) {
	int32 left = st::profileListPadding.width();

	UserData *user = row->history->peer->asUser();
	ContactData *data = contactData(row);

	if (sel) {
		p.fillRect(0, 0, width(), 2 * st::profileListPadding.height() + st::profileListPhotoSize, st::profileHoverBG->b);
	}

	p.drawPixmap(left, st::profileListPadding.height(), user->photo->pix(st::profileListPhotoSize));

	p.setPen(st::profileListNameColor->p);
	data->name.drawElided(p, left + st::profileListPhotoSize + st::participantDelta, st::profileListNameTop, width() - st::profileListPadding.width() - st::profileListPhotoSize - st::profileListPadding.width() - st::participantDelta - st::scrollDef.width - st::contactsImg.width());

	if (sel) {
		p.drawPixmap(QPoint(width() - st::contactsImg.width() - st::profileCheckDeltaX, st::profileListPadding.height() + (st::profileListPhotoSize - st::contactsImg.height()) / 2 - st::profileCheckDeltaY), App::sprite(), st::contactsImg);
	}

	p.setFont(st::profileSubFont->f);
	p.setPen((user->onlineTill >= _time ? st::profileOnlineColor : st::profileOfflineColor)->p);

	p.drawText(left + st::profileListPhotoSize + st::participantDelta, st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, data->online);
}

void ContactsInner::paintEvent(QPaintEvent *e) {
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

void ContactsInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void ContactsInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	updateSel();
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
	int32 rh = st::profileListPhotoSize + st::profileListPadding.height() * 2, from;
	DialogRow *r = 0;
	if (_filter.isEmpty()) {
		r = _sel;
	} else {
		if (_filteredSel < 0 || _filteredSel >= _filtered.size()) return;
		r = _filtered[_filteredSel];
	}
	if (r) {
		App::wnd()->hideSettings(true);
		App::main()->showPeer(r->history->peer->id, false, true);
		App::wnd()->hideLayer();
	}

	parentWidget()->update();
}

void ContactsInner::updateSel() {
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

void ContactsInner::updateFilter(QString filter) {
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
			resize(width(), _contacts->list.count * rh + st::contactsClose.height);
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

			resize(width(), _filtered.size() * rh + st::contactsClose.height);
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
	int32 newh = (_filter.isEmpty() ? _contacts->list.count : _filtered.size()) * rh;
	resize(width(), newh);
}

ContactsInner::~ContactsInner() {
	for (ContactsData::iterator i = _contactsData.begin(), e = _contactsData.end(); i != e; ++i) {
		delete *i;
	}
}

void ContactsInner::selectSkip(int32 dir) {
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
			emit mustScrollTo(_sel->pos * rh, (_sel->pos + 1) * rh + st::contactsClose.height);
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
			emit mustScrollTo(_filteredSel * rh, (_filteredSel + 1) * rh + st::contactsClose.height);
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

ContactsBox::ContactsBox() : _inner(), _hiding(false), _scroll(this, st::newGroupScroll),
	_addContact(this, lang(lng_add_contact_button), st::contactsAdd),
	_filter(this, st::contactsFilter, lang(lng_participant_filter)),
	_close(this, lang(lng_contacts_done), st::contactsClose),
	a_opacity(0, 1) {

	_width = st::participantWidth;
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();
	if (_height > st::participantMaxHeight) _height = st::participantMaxHeight;

	resize(_width, _height);

	_scroll.setWidget(&_inner);
	_scroll.setFocusPolicy(Qt::NoFocus);

	connect(&_addContact, SIGNAL(clicked()), this, SLOT(onAdd()));
	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSel()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_filter, SIGNAL(cancelled()), this, SIGNAL(onClose()));
	connect(&_inner, SIGNAL(mustScrollTo(int,int)), &_scroll, SLOT(scrollToY(int,int)));

	showAll();
	_cache = grab(rect());
	hideAll();
}

void ContactsBox::hideAll() {
	_addContact.hide();
	_filter.hide();
	_scroll.hide();
	_close.hide();
}

void ContactsBox::showAll() {
	_addContact.show();
	_filter.show();
	_scroll.show();
	_close.show();
}

void ContactsBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (_filter.hasFocus()) {
			_inner.chooseParticipant();
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
			e->ignore();
		}
	} else {
		e->ignore();
	}
}

void ContactsBox::parentResized() {
	QSize s = parentWidget()->size();
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();
	if (_height > st::participantMaxHeight) _height = st::participantMaxHeight;
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void ContactsBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

			// paint shadows
			p.fillRect(0, _addContact.height(), _width, st::scrollDef.topsh, st::scrollDef.shColor->b);

			// paint button sep
			p.setPen(st::btnSelectSep->p);
			p.drawLine(st::btnSelectCancel.width, size().height() - st::btnSelectCancel.height, st::btnSelectCancel.width, size().height() - 1);

			// draw box title / text
			p.setPen(st::black->p);
			p.setFont(st::addContactTitleFont->f);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_contacts_header));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void ContactsBox::resizeEvent(QResizeEvent *e) {
	LayeredWidget::resizeEvent(e);
	_addContact.move(_width - _addContact.width(), 0);
	_filter.move(st::newGroupNamePadding.left(), _addContact.height() + st::newGroupNamePadding.top());
	_inner.resize(_width, _inner.height());
	_scroll.resize(_width, _height - _addContact.height() - st::newGroupNamePadding.top() - _filter.height() - st::newGroupNamePadding.bottom());
	_scroll.move(0, _filter.y() + _filter.height() + st::newGroupNamePadding.bottom());
	_close.move(0, _height - _close.height());
}

void ContactsBox::animStep(float64 dt) {
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

void ContactsBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = grab(rect());
		hideAll();
	}
	a_opacity.start(0);
}

void ContactsBox::onFilterUpdate() {
	_scroll.scrollToY(0);
	_inner.updateFilter(_filter.text());
}

void ContactsBox::onAdd() {
	App::wnd()->replaceLayer(new AddContactBox());
}

void ContactsBox::onClose() {
	emit closed();
}

void ContactsBox::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
}

ContactsBox::~ContactsBox() {

}
