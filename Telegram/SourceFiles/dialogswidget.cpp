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
#include "style.h"
#include "lang.h"

#include "window.h"
#include "dialogswidget.h"
#include "mainwidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/newgroupbox.h"

DialogsListWidget::DialogsListWidget(QWidget *parent, MainWidget *main) : QWidget(parent),
	dialogs(false), contactsNoDialogs(true), contacts(true), sel(0), contactSel(false), selByMouse(false), filteredSel(-1), searchedCount(0), searchedSel(-1), _state(DefaultState) {
	connect(main, SIGNAL(dialogToTop(const History::DialogLinks &)), this, SLOT(onDialogToTop(const History::DialogLinks &)));
	connect(main, SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(onPeerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)));
	connect(main, SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(onPeerPhotoChanged(PeerData *)));
	connect(main, SIGNAL(dialogRowReplaced(DialogRow *, DialogRow *)), this, SLOT(onDialogRowReplaced(DialogRow *, DialogRow *)));
	connect(main, SIGNAL(historyItemReplaced(HistoryItem *, HistoryItem *)), this, SLOT(onItemReplaced(HistoryItem *, HistoryItem *)));
	connect(main, SIGNAL(historyItemDeleted(HistoryItem *)), this, SLOT(onItemRemoved(HistoryItem *)));
}

void DialogsListWidget::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	bool trivial = (rect() == r);

	QPainter p(this); 
	if (!trivial) {
		p.setClipRect(r);
	}

	if (_state == DefaultState) {
		int32 otherStart = dialogs.list.count * st::dlgHeight;
		PeerData *active = App::main()->activePeer(), *selected = sel ? sel->history->peer : 0;
		if (otherStart) {
			dialogs.list.paint(p, width(), r.top(), r.bottom(), active, selected);
		}
		if (contactsNoDialogs.list.count) {
			contactsNoDialogs.list.paint(p, width(), r.top() - otherStart, r.bottom() - otherStart, active, selected);
		} else if (!otherStart) {
			// .. paint no dialogs found
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (filterResults.isEmpty()) {
			// .. paint no dialogs 
		} else {
			int32 from = r.top() / int32(st::dlgHeight);
			if (from < 0) {
				from = 0;
			} else if (from > filterResults.size()) {
				from = filterResults.size();
			}
			p.translate(0, from * st::dlgHeight);
			if (from < filterResults.size()) {
				int32 to = (r.bottom() / int32(st::dlgHeight)) + 1, w = width();
				if (to > filterResults.size()) to = filterResults.size();
				for (; from < to; ++from) {
					bool active = (filterResults[from]->history->peer == App::main()->activePeer() && !App::main()->activeMsgId());
					bool selected = (from == filteredSel);
					filterResults[from]->paint(p, w, active, selected);
					p.translate(0, st::dlgHeight);
				}
			}
		}

		if (_state == SearchedState || !searchResults.isEmpty()) {
			QString text = searchResults.isEmpty() ? lang(lng_search_no_results) : lang(searchedCount > 1 ? lng_search_n_results : lng_search_one_result).replace(qsl("{count}"), QString::number(searchedCount));
			p.fillRect(0, 0, width(), st::searchedBarHeight, st::searchedBarBG->b);
			p.setFont(st::searchedBarFont->f);
			p.setPen(st::searchedBarColor->p);
			p.drawText(QRect(0, 0, width(), st::searchedBarHeight), text, style::al_center);
			p.translate(0, st::searchedBarHeight);

			int32 skip = filterResults.size() * st::dlgHeight + st::searchedBarHeight;
			int32 from = (r.top() - skip) / int32(st::dlgHeight);
			if (from < 0) from = 0;
			if (from < searchResults.size()) {
				int32 to = ((r.bottom() - skip) / int32(st::dlgHeight)) + 1, w = width();
				if (to > searchResults.size()) to = searchResults.size();

				p.translate(0, from * st::dlgHeight);
				for (; from < to; ++from) {
					bool active = (searchResults[from]->_item->id == App::main()->activeMsgId());
					bool selected = (from == searchedSel);
					searchResults[from]->paint(p, w, active, selected);
					p.translate(0, st::dlgHeight);
				}
			}
		}
	}
}

void DialogsListWidget::activate() {
	if (_state == DefaultState && !sel) {
		selectSkip(1);
	}
}

void DialogsListWidget::mouseMoveEvent(QMouseEvent *e) {
	lastMousePos = mapToGlobal(e->pos());
	selByMouse = true;
	onUpdateSelected(true);
    repaint();
}

void DialogsListWidget::onUpdateSelected(bool force) {
	QPoint mouse(mapFromGlobal(lastMousePos));
	if ((!force && !rect().contains(mouse)) || !selByMouse) return;

	int w = width(), mouseY = mouse.y();
	if (_state == DefaultState) {
		DialogRow *newSel = dialogs.list.rowAtY(mouseY, st::dlgHeight);
		int32 otherStart = dialogs.list.count * st::dlgHeight;
		if (newSel) {
			contactSel = false;
		} else {
			newSel = contactsNoDialogs.list.rowAtY(mouseY - otherStart, st::dlgHeight);
			contactSel = true;
		}
		if (newSel != sel) {
			sel = newSel;
			setCursor(sel ? style::cur_pointer : style::cur_default);
			parentWidget()->update();
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (!filterResults.isEmpty()) {
			int32 newFilteredSel = mouseY / int32(st::dlgHeight);
			if (newFilteredSel < 0 || newFilteredSel >= filterResults.size()) {
				newFilteredSel = -1;
			}
			if (newFilteredSel != filteredSel) {
				filteredSel = newFilteredSel;
				setCursor((filteredSel >= 0) ? style::cur_pointer : style::cur_default);
				parentWidget()->update();
			}
		}
		if (_state == SearchedState && !searchResults.isEmpty()) {
			mouseY -= filterResults.size() * st::dlgHeight + st::searchedBarHeight;
			int32 newSearchedSel = (mouseY >= 0) ? mouseY / int32(st::dlgHeight) : -1;
			if (newSearchedSel < 0 || newSearchedSel >= searchResults.size()) {
				newSearchedSel = -1;
			}
			if (newSearchedSel != searchedSel) {
				searchedSel = newSearchedSel;
				setCursor((searchedSel >= 0) ? style::cur_pointer : style::cur_default);
				parentWidget()->update();
			}
		}
	}
}

void DialogsListWidget::mousePressEvent(QMouseEvent *e) {
	lastMousePos = mapToGlobal(e->pos());
	selByMouse = true;
	onUpdateSelected(true);
	if (e->button() == Qt::LeftButton) {
		choosePeer();
	}
}

void DialogsListWidget::onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow) {
	if (_state == FilteredState || _state == SearchedState) {
		for (FilteredDialogs::iterator i = filterResults.begin(); i != filterResults.end();) {
			if (*i == oldRow) { // this row is shown in filtered and maybe is in contacts!
				if (newRow) {
					*i = newRow;
					++i;
				} else {
					i = filterResults.erase(i);
				}
			} else {
				++i;
			}
		}
	}
	if (sel == oldRow) {
		sel = newRow;
	}
}

void DialogsListWidget::createDialogAtTop(History *history, int32 unreadCount) {
	history->updateNameText();

	History::DialogLinks links = dialogs.addToEnd(history);
	int32 movedFrom = links[0]->pos * st::dlgHeight;
	dialogs.bringToTop(links);
	history->dialogs = links;

	contactsNoDialogs.del(history->peer, links[0]);

	emit dialogToTopFrom(movedFrom);
	emit App::main()->dialogsUpdated();

	refresh();
}

void DialogsListWidget::removePeer(PeerData *peer) {
	if (sel && sel->history->peer == peer) {
		sel = 0;
	}
	History *history = App::history(peer->id);
	dialogs.del(peer);
	history->dialogs = History::DialogLinks();
	if (contacts.list.rowByPeer.constFind(peer->id) != contacts.list.rowByPeer.cend()) {
		if (contactsNoDialogs.list.rowByPeer.constFind(peer->id) == contactsNoDialogs.list.rowByPeer.cend()) {
			contactsNoDialogs.addByName(App::history(peer->id));
		}
	}
//	contactsNoDialogs.del(peer);
//	contacts.del(peer);
//	App::deleteHistory(peer->id);

	emit App::main()->dialogsUpdated();

	refresh();
}

void DialogsListWidget::removeContact(UserData *user) {
	if (sel && sel->history->peer == user) {
		sel = 0;
	}
	contactsNoDialogs.del(user);
	contacts.del(user);

	emit App::main()->dialogsUpdated();

	refresh();
}

void DialogsListWidget::dlgUpdated(DialogRow *row) {
	if (_state == DefaultState) {
		update(0, row->pos * st::dlgHeight, width(), st::dlgHeight);
	} else if (_state == FilteredState || _state == SearchedState) {
		int32 cnt = 0;
		for (FilteredDialogs::const_iterator i = filterResults.cbegin(), e = filterResults.cend(); i != e; ++i) {
			if ((*i)->history == row->history) {
				update(0, cnt * st::dlgHeight, width(), st::dlgHeight);
				break;
			}
			++cnt;
		}
	}
}

void DialogsListWidget::dlgUpdated(History *history) {
	if (_state == DefaultState) {
		DialogRow *row = 0;
		DialogsList::RowByPeer::iterator i = dialogs.list.rowByPeer.find(history->peer->id);
		if (i != dialogs.list.rowByPeer.cend()) {
			update(0, i.value()->pos * st::dlgHeight, width(), st::dlgHeight);
		} else {
			i = contactsNoDialogs.list.rowByPeer.find(history->peer->id);
			if (i != contactsNoDialogs.list.rowByPeer.cend()) {
				update(0, (dialogs.list.count + i.value()->pos) * st::dlgHeight, width(), st::dlgHeight);
			}
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		int32 cnt = 0;
		for (FilteredDialogs::const_iterator i = filterResults.cbegin(), e = filterResults.cend(); i != e; ++i) {
			if ((*i)->history == history) {
				update(0, cnt * st::dlgHeight, width(), st::dlgHeight);
				break;
			}
			++cnt;
		}
		if (!searchResults.isEmpty()) {
			int32 cnt = 0, add = filterResults.size() * st::dlgHeight + st::searchedBarHeight;
			for (SearchResults::const_iterator i = searchResults.cbegin(), e = searchResults.cend(); i != e; ++i) {
				if ((*i)->_item->history() == history) {
					update(0, add + cnt * st::dlgHeight, width(), st::dlgHeight);
					break;
				}
				++cnt;
			}
		}
	}
}

void DialogsListWidget::enterEvent(QEvent *e) {
	setMouseTracking(true);
	lastMousePos = QCursor::pos();
	onUpdateSelected(true);
}

void DialogsListWidget::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	if (sel || filteredSel >= 0) {
		sel = 0;
		filteredSel = -1;
		parentWidget()->update();
	}
}

void DialogsListWidget::onParentGeometryChanged() {
	lastMousePos = QCursor::pos();
	if (rect().contains(mapFromGlobal(lastMousePos))) {
		setMouseTracking(true);
		onUpdateSelected(true);
	}
}

void DialogsListWidget::onDialogToTop(const History::DialogLinks &links) {
	int32 movedFrom = links[0]->pos * st::dlgHeight;
	dialogs.bringToTop(links);
	emit dialogToTopFrom(movedFrom);
	emit App::main()->dialogsUpdated();
	parentWidget()->update();
}

void DialogsListWidget::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	dialogs.peerNameChanged(peer, oldNames, oldChars);
	contactsNoDialogs.peerNameChanged(peer, oldNames, oldChars);
	contacts.peerNameChanged(peer, oldNames, oldChars);
	parentWidget()->update();
}

void DialogsListWidget::onPeerPhotoChanged(PeerData *peer) {
	parentWidget()->update();
}

void DialogsListWidget::onFilterUpdate(QString newFilter, bool force) {
	newFilter = textAccentFold(newFilter.trimmed().toLower());
	if (newFilter != filter || force) {
		QStringList f;
		if (!newFilter.isEmpty()) {
			QStringList filterList = newFilter.split(cWordSplit(), QString::SkipEmptyParts);
			int l = filterList.size();

			f.reserve(l);
			for (int i = 0; i < l; ++i) {
				QString filterName = filterList[i].trimmed();
				if (filterName.isEmpty()) continue;
				f.push_back(filterName);
			}
			newFilter = f.join(' ');
		}
		if (newFilter != filter || force) {
			filter = newFilter;
			if (filter.isEmpty()) {
				_state = DefaultState;
				filterResults.clear();
				searchResults.clear();
			} else {
				QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

				_state = FilteredState;
				filterResults.clear();
				if (!f.isEmpty()) {
					DialogsList *dialogsToFilter = 0, *contactsNoDialogsToFilter = 0;
					if (dialogs.list.count) {
						for (fi = fb; fi != fe; ++fi) {
							DialogsIndexed::DialogsIndex::iterator i = dialogs.index.find(fi->at(0));
							if (i == dialogs.index.cend()) {
								dialogsToFilter = 0;
								break;
							}
							if (!dialogsToFilter || dialogsToFilter->count > i.value()->count) {
								dialogsToFilter = i.value();
							}
						}
					}
					if (contactsNoDialogs.list.count) {
						for (fi = fb; fi != fe; ++fi) {
							DialogsIndexed::DialogsIndex::iterator i = contactsNoDialogs.index.find(fi->at(0));
							if (i == contactsNoDialogs.index.cend()) {
								contactsNoDialogsToFilter = 0;
								break;
							}
							if (!contactsNoDialogsToFilter || contactsNoDialogsToFilter->count > i.value()->count) {
								contactsNoDialogsToFilter = i.value();
							}
						}
					}
					filterResults.reserve((dialogsToFilter ? dialogsToFilter->count : 0) + (contactsNoDialogsToFilter ? contactsNoDialogsToFilter->count : 0));
					if (dialogsToFilter && dialogsToFilter->count) {
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
								filterResults.push_back(i);
							}
						}
					}
					if (contactsNoDialogsToFilter && contactsNoDialogsToFilter->count) {
						for (DialogRow *i = contactsNoDialogsToFilter->begin, *e = contactsNoDialogsToFilter->end; i != e; i = i->next) {
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
								filterResults.push_back(i);
							}
						}
					}
				}
			}
		}
		refresh(true);
		setMouseSel(false, true);
	}
	if (_state != DefaultState) {
		emit searchMessages();
	}
}

DialogsListWidget::~DialogsListWidget() {
	clearSearchResults();
}

void DialogsListWidget::clearSearchResults() {	
	if (!searchResults.isEmpty()) {
		for (SearchResults::const_iterator i = searchResults.cbegin(), e = searchResults.cend(); i != e; ++i) {
			delete *i;
		}
		searchResults.clear();
	}
}

void DialogsListWidget::onItemReplaced(HistoryItem *oldItem, HistoryItem *newItem) {
	for (int i = 0; i < searchResults.size(); ++i) {
		if (searchResults[i]->_item == oldItem) {
			searchResults[i]->_item = newItem;
		}
	}
}

void DialogsListWidget::onItemRemoved(HistoryItem *item) {
	int wasCount = searchResults.size();
	for (int i = 0; i < searchResults.size(); ++i) {
		if (searchResults[i]->_item == item) {
			searchResults.remove(i);
		} else {
			++i;
		}
	}
	if (wasCount != searchResults.size()) {
		refresh();
	}
}

void DialogsListWidget::dialogsReceived(const QVector<MTPDialog> &added) {
	for (QVector<MTPDialog>::const_iterator i = added.cbegin(), e = added.cend(); i != e; ++i) {
		if (i->type() == mtpc_dialog) {
			addDialog(i->c_dialog());
		}
	}
	if (App::wnd()) App::wnd()->psUpdateCounter();
	if (!sel && dialogs.list.count) {
		sel = dialogs.list.begin;
		contactSel = false;
	}
	refresh();
}

void DialogsListWidget::searchReceived(const QVector<MTPMessage> &messages, bool fromStart, int32 fullCount) {
	if (fromStart) {
		clearSearchResults();
	}
	for (QVector<MTPMessage>::const_iterator i = messages.cbegin(), e = messages.cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addToBack(*i, -1);
		searchResults.push_back(new FakeDialogRow(item));
	}
	searchedCount = fullCount;
	if (_state == FilteredState) {
		_state = SearchedState;
	}
	refresh();
}

void DialogsListWidget::contactsReceived(const QVector<MTPContact> &contacts) {
	for (QVector<MTPContact>::const_iterator i = contacts.cbegin(), e = contacts.cend(); i != e; ++i) {
		addNewContact(i->c_contact().vuser_id.v);
	}
	if (!sel && contactsNoDialogs.list.count) {
		sel = contactsNoDialogs.list.begin;
		contactSel = true;
	}
	refresh();
}

int32 DialogsListWidget::addNewContact(int32 uid, bool select) {
	PeerId peer = App::peerFromUser(uid);
	if (!App::peerLoaded(peer)) return -1;

	History *history = App::history(peer);
	contacts.addByName(history);
	DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(peer);
	if (i == dialogs.list.rowByPeer.cend()) {
		DialogRow *added = contactsNoDialogs.addByName(history);
		if (!added) return -1;
		if (select) {
			sel = added;
			contactSel = true;
		}
		return added ? ((dialogs.list.count + added->pos) * st::dlgHeight) : -1;
	}
	if (select) {
		sel = i.value();
		contactSel = false;
	}
	return i.value()->pos * st::dlgHeight;
}

void DialogsListWidget::refresh(bool toTop) {
	int32 h = 0;
	if (_state == DefaultState) {
		h = (dialogs.list.count + contactsNoDialogs.list.count) * st::dlgHeight;
	} else if (_state == FilteredState) {
		h = (filterResults.count() + searchResults.count()) * st::dlgHeight + (searchResults.isEmpty() ? 0 : st::searchedBarHeight);
	} else if (_state == SearchedState) {
		h = (filterResults.count() + searchResults.count()) * st::dlgHeight + st::searchedBarHeight;
	}
	resize(width(), h);
	if (toTop) {
		emit mustScrollTo(0, 0);
		loadPeerPhotos(0);
	}
	parentWidget()->update();
}

void DialogsListWidget::setMouseSel(bool msel, bool toTop) {
	selByMouse = msel;
	if (!selByMouse && toTop) {
		if (_state == DefaultState) {
			sel = (dialogs.list.count ? dialogs.list.begin : (contactsNoDialogs.list.count ? contactsNoDialogs.list.begin : 0));
			contactSel = !dialogs.list.count && contactsNoDialogs.list.count;
		} else if (_state == FilteredState || _state == SearchedState) { // don't select first elem in search
			filteredSel = -1;
			searchedSel = -1;
		}
	}
}

void DialogsListWidget::setState(State newState) {
	_state = newState;
	if (_state == DefaultState) {
		clearSearchResults();
		searchedSel = filteredSel = -1;
	} else if (_state == DefaultState || _state == SearchedState) {
		filterResults.clear();
		filteredSel = -1;
	}
	onFilterUpdate(filter, true);
	refresh(true);
}

DialogsListWidget::State DialogsListWidget::state() const {
	return _state;
}

void DialogsListWidget::clearFilter() {
	if (_state == FilteredState || _state == SearchedState) {
		_state = DefaultState;
		filterResults.clear();
		searchResults.clear();
		filter = QString();
		refresh(true);
	}
}

void DialogsListWidget::addDialog(const MTPDdialog &dialog) {
	History *history = App::history(App::peerFromMTP(dialog.vpeer), dialog.vunread_count.v);
	History::DialogLinks links = dialogs.addToEnd(history);
	history->dialogs = links;
	contactsNoDialogs.del(history->peer);

	App::main()->applyNotifySetting(MTP_notifyPeer(dialog.vpeer), dialog.vnotify_settings, history);
}

void DialogsListWidget::selectSkip(int32 direction) {
	int32 skipMore = 0;
	if (_state == DefaultState) {
		if (!sel) {
			if (dialogs.list.count && direction > 0) {
				sel = dialogs.list.begin;
			} else if (contactsNoDialogs.list.count && direction > 0) {
				sel = contactsNoDialogs.list.begin;
			} else {
				return;
			}
		} else if (direction > 0) {
			if (sel->next->next) {
				sel = sel->next;
			} else if (sel->next == dialogs.list.end && contactsNoDialogs.list.count) {
				sel = contactsNoDialogs.list.begin;
				contactSel = true;
			}
		} else {
			if (sel->prev) {
				sel = sel->prev;
			} else if (sel == contactsNoDialogs.list.begin && dialogs.list.count) {
				sel = dialogs.list.end->prev;
				contactSel = false;
			}
		}
		int32 fromY = (sel->pos + (contactSel ? dialogs.list.count : 0)) * st::dlgHeight;
		emit mustScrollTo(fromY, fromY + st::dlgHeight);
	} else if (_state == FilteredState || _state == SearchedState) {
		if (filterResults.isEmpty() && searchResults.isEmpty()) return;
		if (filteredSel < 0 || filteredSel >= filterResults.size()) {
			if (searchedSel < 0 || searchedSel >= searchResults.size()) {
				if (filterResults.isEmpty()) {
					searchedSel = 0;
				} else {
					filteredSel = 0;
				}
			} else if (direction < 0 && !searchedSel && !filterResults.isEmpty()) {
				searchedSel = -1;
				filteredSel = filterResults.size() + direction;
				if (filteredSel < 0) filteredSel = 0;
			} else {
				if (direction < -1 && searchedSel + direction < 0) {
					skipMore = direction + searchedSel;
					if (skipMore == direction) {
						skipMore = 0;
					} else {
						direction -= skipMore;
					}
				}
				searchedSel = snap(searchedSel + direction, 0, searchResults.size() - 1);
			}
		} else if (direction > 0 && filteredSel == filterResults.size() - 1 && !searchResults.isEmpty()) {
			filteredSel = -1;
			searchedSel = direction - 1;
			if (searchedSel > searchResults.size() - 1) searchedSel = searchResults.size() - 1;
		} else {
			if (direction > 1 && filteredSel + direction > filterResults.size() - 1) {
				skipMore = direction - (filterResults.size() - 1 - filteredSel);
				if (skipMore == direction) {
					skipMore = 0;
				} else {
					direction -= skipMore;
				}
			}
			filteredSel = snap(filteredSel + direction, 0, filterResults.size() - 1);
		}
		if (filteredSel >= 0 && filteredSel < filterResults.size()) {
			emit mustScrollTo(filteredSel * st::dlgHeight, (filteredSel + 1) * st::dlgHeight);
		} else {
			emit mustScrollTo((searchedSel + filterResults.size()) * st::dlgHeight + (searchedSel ? st::searchedBarHeight : 0), (searchedSel + filterResults.size() + 1) * st::dlgHeight + st::searchedBarHeight);
		}
	}
	if (skipMore) {
		return selectSkip(skipMore);
	} else {
		parentWidget()->update();
	}
}

void DialogsListWidget::scrollToPeer(const PeerId &peer) {
	int32 fromY = -1;
	if (_state == DefaultState) {
		DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(peer);
		if (i != dialogs.list.rowByPeer.cend()) {
			fromY = i.value()->pos * st::dlgHeight;
		} else {
			i = contactsNoDialogs.list.rowByPeer.constFind(peer);
			if (i != contactsNoDialogs.list.rowByPeer.cend()) {
				fromY = (i.value()->pos + dialogs.list.count) * st::dlgHeight;
			}
		}
	} else if (_state == FilteredState) {
		for (int32 i = 0, c = filterResults.size(); i < c; ++i) {
			if (filterResults[i]->history->peer->id == peer) {
				fromY = i * st::dlgHeight;
				break;
			}
		}
	}
	if (fromY >= 0) {
		emit mustScrollTo(fromY, fromY + st::dlgHeight);
	}
}

void DialogsListWidget::selectSkipPage(int32 pixels, int32 direction) {
	int32 toSkip = pixels / int32(st::dlgHeight);
	if (_state == DefaultState) {
		if (!sel) {
			if (direction > 0 && dialogs.list.count) {
				sel = dialogs.list.begin;
			} else if (direction > 0 && contactsNoDialogs.list.count) {
				sel = contactsNoDialogs.list.begin;
			} else {
				return;
			}
		}
		if (direction > 0) {
			while (toSkip-- && sel->next->next) {
				sel = sel->next;
			}
			if (toSkip >= 0 && sel->next == dialogs.list.end && contactsNoDialogs.list.count) {
				sel = contactsNoDialogs.list.begin;
				while (toSkip-- && sel->next->next) {
					sel = sel->next;
				}
				contactSel = true;
			}
		} else {
			while (toSkip-- && sel->prev) {
				sel = sel->prev;
			}
			if (toSkip >= 0 && sel == contactsNoDialogs.list.begin && dialogs.list.count) {
				sel = dialogs.list.end->prev;
				while (toSkip-- && sel->prev) {
					sel = sel->prev;
				}
				contactSel = false;
			}
		}
		int32 fromY = (sel->pos + (contactSel ? dialogs.list.count : 0)) * st::dlgHeight;
		emit mustScrollTo(fromY, fromY + st::dlgHeight);
	} else {
		return selectSkip(direction * toSkip);
	}
	parentWidget()->update();
}

void DialogsListWidget::loadPeerPhotos(int32 yFrom) {
	int32 yTo = yFrom + parentWidget()->height() * 5;
	MTP::clearLoaderPriorities();
	if (_state == DefaultState) {
		int32 otherStart = dialogs.list.count * st::dlgHeight;
		if (yFrom < otherStart) {
			dialogs.list.adjustCurrent(yFrom, st::dlgHeight);
			for (DialogRow *row = dialogs.list.current; row != dialogs.list.end && (row->pos * st::dlgHeight) < yTo; row = row->next) {
				row->history->peer->photo->load();
			}
			yFrom = 0;
		} else {
			yFrom -= otherStart;
		}
		yTo -= otherStart;
		if (yTo > 0) {
			contactsNoDialogs.list.adjustCurrent(yFrom, st::dlgHeight);
			for (DialogRow *row = contactsNoDialogs.list.current; row != contactsNoDialogs.list.end && (row->pos * st::dlgHeight) < yTo; row = row->next) {
				row->history->peer->photo->load();
			}
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		int32 from = yFrom / st::dlgHeight;
		if (from < 0) from = 0;
		if (from < filterResults.size()) {
			int32 to = (yTo / int32(st::dlgHeight)) + 1, w = width();
			if (to > filterResults.size()) to = filterResults.size();
			
			for (; from < to; ++from) {
				filterResults[from]->history->peer->photo->load();
			}
		}

		from = (yFrom > st::searchedBarHeight ? ((yFrom - st::searchedBarHeight) / int32(st::dlgHeight)) : 0) - filterResults.size();
		if (from < 0) from = 0;
		if (from < searchResults.size()) {
			int32 to = (yTo > st::searchedBarHeight ? ((yTo - st::searchedBarHeight) / int32(st::dlgHeight)) : 0) - filterResults.size() + 1, w = width();
			if (to > searchResults.size()) to = searchResults.size();

			for (; from < to; ++from) {
				searchResults[from]->_item->history()->peer->photo->load();
			}
		}
	}
}

bool DialogsListWidget::choosePeer() {
	History *history = 0;
	MsgId msgId = 0;
	if (_state == DefaultState) {
		if (sel) history = sel->history;
	} else if (_state == FilteredState || _state == SearchedState) {
		if (filteredSel >= 0 && filteredSel < filterResults.size()) {
			history = filterResults[filteredSel]->history;
		} else if (searchedSel >= 0 && searchedSel < searchResults.size()) {
			history = searchResults[searchedSel]->_item->history();
			msgId = searchResults[searchedSel]->_item->id;
		}
	}
	if (history) {
		emit peerChosen(history->peer->id, msgId);
		sel = 0;
		filteredSel = -1;
		parentWidget()->update();
		return true;
	}
	return false;
}

void DialogsListWidget::destroyData() {
	sel = 0;
	contactSel = false;
	filteredSel = 0;
	filterResults.clear();
	filter.clear();
	searchedSel = 0;
	clearSearchResults();
	contacts.clear();
	contactsNoDialogs.clear();
	dialogs.clear();
}

PeerData *DialogsListWidget::peerBefore(const PeerData *peer) const {
	if (_state == DefaultState) {
		DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(peer->id);
		if (i == dialogs.list.rowByPeer.constEnd()) {
			i = contactsNoDialogs.list.rowByPeer.constFind(peer->id);
			if (i == contactsNoDialogs.list.rowByPeer.cend()) {
				return 0;
			}
			if (i.value()->prev) {
				return i.value()->prev->history->peer;
			} else if (dialogs.list.count) {
				return dialogs.list.end->prev->history->peer;
			}
			return 0;
		}
		if (i.value()->prev) {
			return i.value()->prev->history->peer;
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (filterResults.isEmpty() || filterResults.at(0)->history->peer == peer) return 0;

		for (FilteredDialogs::const_iterator b = filterResults.cbegin(), i = b + 1, e = filterResults.cend(); i != e; ++i) {
			if ((*i)->history->peer == peer) {
				FilteredDialogs::const_iterator j = i - 1;
				return (*j)->history->peer;
			}
		}
	}
	return 0;
}

PeerData *DialogsListWidget::peerAfter(const PeerData *peer) const {
	if (_state == DefaultState) {
		DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(peer->id);
		if (i == dialogs.list.rowByPeer.constEnd()) {
			i = contactsNoDialogs.list.rowByPeer.constFind(peer->id);
			if (i == contactsNoDialogs.list.rowByPeer.cend()) {
				return 0;
			}
			if (i.value()->next != contactsNoDialogs.list.end) {
				return i.value()->next->history->peer;
			}
			return 0;
		}

		if (i.value()->next != dialogs.list.end) {
			return i.value()->next->history->peer;
		} else if (contactsNoDialogs.list.count) {
			return contactsNoDialogs.list.begin->history->peer;
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		for (FilteredDialogs::const_iterator i = filterResults.cbegin(), e = filterResults.cend(); i != e; ++i) {
			if ((*i)->history->peer == peer) {
				++i;
				return (i == e) ? 0 : (*i)->history->peer;
			}
		}
	}
	return 0;
}

DialogsIndexed &DialogsListWidget::contactsList() {
	return contacts;
}

DialogsIndexed &DialogsListWidget::dialogsList() {
	return dialogs;
}

DialogsListWidget::SearchResults &DialogsListWidget::searchList() {
	return searchResults;
}

DialogsWidget::DialogsWidget(MainWidget *parent) : QWidget(parent)
, _configLoaded(false)
, _drawShadow(true)
, dlgOffset(0)
, dlgCount(-1)
, dlgPreloading(0)
, contactsRequest(0)
, _filter(this, st::dlgFilter, lang(lng_dlg_filter))
, _newGroup(this, st::btnNewGroup)
, _addContact(this, st::btnAddContact)
, _cancelSearch(this, st::btnCancelSearch)
, scroll(this, st::dlgScroll)
, list(&scroll, parent)
, _searchFull(false)
{
	scroll.setWidget(&list);
	scroll.setFocusPolicy(Qt::NoFocus);
	connect(&list, SIGNAL(mustScrollTo(int, int)), &scroll, SLOT(scrollToY(int, int)));
	connect(&list, SIGNAL(dialogToTopFrom(int)), this, SLOT(onDialogToTopFrom(int)));
	connect(&list, SIGNAL(peerChosen(const PeerId &, MsgId)), this, SIGNAL(peerChosen(const PeerId &, MsgId)));
	connect(&list, SIGNAL(searchMessages()), this, SLOT(onNeedSearchMessages()));
	connect(&scroll, SIGNAL(geometryChanged()), &list, SLOT(onParentGeometryChanged()));
	connect(&scroll, SIGNAL(scrolled()), &list, SLOT(onUpdateSelected()));
	connect(&scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(&_filter, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(parent, SIGNAL(dialogsUpdated()), this, SLOT(onListScroll()));
	connect(&_addContact, SIGNAL(clicked()), this, SLOT(onAddContact()));
	connect(&_newGroup, SIGNAL(clicked()), this, SLOT(onNewGroup()));
	connect(&_cancelSearch, SIGNAL(clicked()), this, SLOT(onCancelSearch()));

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	scroll.show();
	_filter.show();
	_filter.move(st::dlgPaddingHor, st::dlgFilterPadding);
	_filter.setFocusPolicy(Qt::StrongFocus);
	_filter.customUpDown(true);
	_addContact.hide();
	_newGroup.show();
	_cancelSearch.hide();
	_newGroup.move(width() - _newGroup.width() - st::dlgPaddingHor, 0);
	_addContact.move(width() - _addContact.width() - st::dlgPaddingHor, 0);
	_cancelSearch.move(width() - _cancelSearch.width() - st::dlgPaddingHor, 0);
}

void DialogsWidget::activate() {
	_filter.setFocus();
	list.activate();
}

void DialogsWidget::createDialogAtTop(History *history, int32 unreadCount) {
	list.createDialogAtTop(history, unreadCount);
}

void DialogsWidget::dlgUpdated(DialogRow *row) {
	list.dlgUpdated(row);
}

void DialogsWidget::dlgUpdated(History *row) {
	list.dlgUpdated(row);
}

void DialogsWidget::dialogsToUp() {
	if (_filter.text().trimmed().isEmpty()) {
		scroll.scrollToY(0);
	}
}

void DialogsWidget::setInnerFocus() {
	_filter.setFocus();
}

void DialogsWidget::regTyping(History *history, UserData *user) {
	uint64 ms = getms();
	history->typing[user] = ms + 6000;

	Histories::TypingHistories::const_iterator i = App::histories().typing.find(history);
	if (i == App::histories().typing.cend()) {
		App::histories().typing.insert(history, ms);
		history->typingFrame = 0;
	}

	history->updateTyping(ms, history->typingFrame, true);
	anim::start(this);
}

bool DialogsWidget::animStep(float64) {
	uint64 ms = getms();
	Histories::TypingHistories &typing(App::histories().typing);
	for (Histories::TypingHistories::iterator i = typing.begin(), e = typing.end(); i != e;) {
		uint32 typingFrame = (ms - i.value()) / 150;
		if (i.key()->updateTyping(ms, typingFrame)) {
			list.dlgUpdated(i.key());
			App::main()->topBar()->update();
		}
		if (i.key()->typing.isEmpty()) {
			i = typing.erase(i);
		} else {
			++i;
		}
	}
	return !typing.isEmpty();
}

void DialogsWidget::onCancel() {
	onCancelSearch();
	emit cancelled();
}

void DialogsWidget::clearFiltered() {
	onCancel();
}

void DialogsWidget::unreadCountsReceived(const QVector<MTPDialog> &dialogs) {
	for (QVector<MTPDialog>::const_iterator i = dialogs.cbegin(), e = dialogs.cend(); i != e; ++i) {
		const MTPDdialog &d(i->c_dialog());
		Histories::iterator j = App::histories().find(App::peerFromMTP(d.vpeer));
		if (j != App::histories().end()) {
			App::main()->applyNotifySetting(MTP_notifyPeer(d.vpeer), d.vnotify_settings, j.value());
			j.value()->setUnreadCount(d.vunread_count.v, false);
		}
	}
	if (App::wnd()) App::wnd()->psUpdateCounter();
}

void DialogsWidget::dialogsReceived(const MTPmessages_Dialogs &dialogs) {
	const QVector<MTPDialog> *dlgList = 0;
	switch (dialogs.type()) {
	case mtpc_messages_dialogs: {
		const MTPDmessages_dialogs &data(dialogs.c_messages_dialogs());
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		App::feedMsgs(data.vmessages);
		dlgList = &data.vdialogs.c_vector().v;
		dlgCount = dlgList->size();
	} break;
	case mtpc_messages_dialogsSlice: {
		const MTPDmessages_dialogsSlice &data(dialogs.c_messages_dialogsSlice());
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		App::feedMsgs(data.vmessages);
		dlgList = &data.vdialogs.c_vector().v;
		dlgCount = data.vcount.v;
	} break;
	}

	unreadCountsReceived(*dlgList);

	if (!contactsRequest) {
		contactsRequest = MTP::send(MTPcontacts_GetContacts(MTP_string("")), rpcDone(&DialogsWidget::contactsReceived), rpcFail(&DialogsWidget::contactsFailed));
	}

	if (dlgList) {
		list.dialogsReceived(*dlgList);
		onListScroll();

		if (dlgList->size()) {
			dlgOffset += dlgList->size();
		} else {
			dlgCount = dlgOffset;
		}
	} else {
		dlgCount = dlgOffset;
		loadConfig();
	}

	dlgPreloading = 0;
	if (dlgList) {
		loadDialogs();
	}
}

bool DialogsWidget::dialogsFailed(const RPCError &e) {
	LOG(("RPC Error: %1 %2: %3").arg(e.code()).arg(e.type()).arg(e.description()));
	dlgPreloading = 0;
	return true;
}

bool DialogsWidget::onSearchMessages(bool searchCache) {
	QString q = _filter.text().trimmed();
	if (q.isEmpty()) {
		if (_searchRequest) {
			_searchRequest = 0;
		}
		return true;
	}
	if (searchCache) {
		SearchCache::const_iterator i = _searchCache.constFind(q);
		if (i != _searchCache.cend()) {
			_searchQuery = q;
			_searchFull = false;
			_searchRequest = 0;
			searchReceived(true, i.value(), 0);
			return true;
		}
	} else if (_searchQuery != q) {
		_searchQuery = q;
		_searchFull = false;
		_searchRequest = MTP::send(MTPmessages_Search(MTP_inputPeerEmpty(), MTP_string(_searchQuery), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, true), rpcFail(&DialogsWidget::searchFailed));
		_searchQueries.insert(_searchRequest, _searchQuery);
	}
	return false;
}


void DialogsWidget::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void DialogsWidget::onSearchMore(MsgId minMsgId) {
	if (!_searchRequest && !_searchFull) {
		_searchRequest = MTP::send(MTPmessages_Search(MTP_inputPeerEmpty(), MTP_string(_searchQuery), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(minMsgId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, !minMsgId), rpcFail(&DialogsWidget::searchFailed));
		if (!minMsgId) {
			_searchQueries.insert(_searchRequest, _searchQuery);
		}
	}
}

void DialogsWidget::loadConfig() {
	if (!_configLoaded) {
		mtpConfigLoader()->load();
		_configLoaded = true;
	}
}

void DialogsWidget::loadDialogs() {
	if (dlgPreloading) return;
	if (dlgCount >= 0 && dlgOffset >= dlgCount) {
		loadConfig();
		return;
	}

	int32 loadCount = dlgOffset ? DialogsPerPage : DialogsFirstLoad;
	dlgPreloading = MTP::send(MTPmessages_GetDialogs(MTP_int(dlgOffset), MTP_int(0), MTP_int(loadCount)), rpcDone(&DialogsWidget::dialogsReceived), rpcFail(&DialogsWidget::dialogsFailed));
}

void DialogsWidget::contactsReceived(const MTPcontacts_Contacts &contacts) {
	if (contacts.type() == mtpc_contacts_contacts) {
		const MTPDcontacts_contacts &d(contacts.c_contacts_contacts());
		App::feedUsers(d.vusers);
		list.contactsReceived(d.vcontacts.c_vector().v);
	}
}

bool DialogsWidget::contactsFailed() {
	return true;
}

void DialogsWidget::searchReceived(bool fromStart, const MTPmessages_Messages &result, mtpRequestId req) {
	if (fromStart && (list.state() == DialogsListWidget::FilteredState || list.state() == DialogsListWidget::SearchedState)) {
		SearchQueries::iterator i = _searchQueries.find(req);
		if (i != _searchQueries.cend()) {
			_searchCache[i.value()] = result;
			_searchQueries.erase(i);
		}
	}

	if (_searchRequest == req) {
		switch (result.type()) {
		case mtpc_messages_messages: {
			App::feedUsers(result.c_messages_messages().vusers);
			App::feedChats(result.c_messages_messages().vchats);
			const QVector<MTPMessage> &msgs(result.c_messages_messages().vmessages.c_vector().v);
			list.searchReceived(msgs, fromStart, msgs.size());
			if (msgs.isEmpty()) {
				_searchFull = true;
			}
		} break;

		case mtpc_messages_messagesSlice: {
			App::feedUsers(result.c_messages_messagesSlice().vusers);
			App::feedChats(result.c_messages_messagesSlice().vchats);
			const QVector<MTPMessage> &msgs(result.c_messages_messagesSlice().vmessages.c_vector().v);
			list.searchReceived(msgs, fromStart, result.c_messages_messagesSlice().vcount.v);
			if (msgs.isEmpty()) {
				_searchFull = true;
			}
		} break;
		}

		_searchRequest = 0;
	}
}

bool DialogsWidget::searchFailed(const RPCError &error, mtpRequestId req) {
	if (_searchRequest == req) {
		_searchRequest = 0;
		_searchFull = true;
	}
	return true;
}

bool DialogsWidget::addNewContact(int32 uid, bool show) {
	_filter.setText(QString());
	onFilterUpdate();
	int32 to = list.addNewContact(uid, true);
	if (to < 0 || !show) return false;
	list.refresh();
	scroll.scrollToY(to);
	return true;
}

void DialogsWidget::onListScroll() {
	list.loadPeerPhotos(scroll.scrollTop());
	if (list.state() == DialogsListWidget::SearchedState) {
		DialogsListWidget::SearchResults &res(list.searchList());
		if (scroll.scrollTop() > res.size() * st::dlgHeight - 2 * scroll.height()) {
			onSearchMore(res.isEmpty() ? 0 : res.back()->_item->id);
		}
	} else if (scroll.scrollTop() > list.dialogsList().list.count * st::dlgHeight - scroll.height()) {
		loadDialogs();
	}
}

void DialogsWidget::onFilterUpdate() {
	QString filterText = _filter.text();
	list.onFilterUpdate(filterText);
	if (filterText.isEmpty()) {
		_searchCache.clear();
		_searchQueries.clear();
		_searchQuery = QString();
		if (!_cancelSearch.isHidden()) {
			_cancelSearch.hide();
			_newGroup.show();
		}
	} else if (_cancelSearch.isHidden()) {
		_cancelSearch.show();
		_newGroup.hide();
	}
}

void DialogsWidget::resizeEvent(QResizeEvent *e) {
	int32 w = width() - st::dlgShadow;
	_filter.setGeometry(st::dlgPaddingHor, st::dlgFilterPadding, w - 2 * st::dlgPaddingHor, _filter.height());
	_newGroup.move(w - _newGroup.width() - st::dlgPaddingHor, _filter.y());
	_addContact.move(w - _addContact.width() - st::dlgPaddingHor, _filter.y());
	_cancelSearch.move(w - _cancelSearch.width() - st::dlgPaddingHor, _filter.y());
	scroll.move(0, _filter.height() + 2 * st::dlgFilterPadding);
	scroll.resize(w, height() - _filter.y() - _filter.height() - st::dlgFilterPadding - st::dlgPaddingVer);
	list.resize(w, list.height());
	onListScroll();
}

void DialogsWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!list.choosePeer() && (list.state() == DialogsListWidget::SearchedState || list.state() == DialogsListWidget::FilteredState)) {
			onSearchMessages();
		}
	} else if (e->key() == Qt::Key_Down) {
		list.setMouseSel(false);
		list.selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		list.setMouseSel(false);
		list.selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		list.setMouseSel(false);
		list.selectSkipPage(scroll.height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		list.setMouseSel(false);
		list.selectSkipPage(scroll.height(), -1);
	} else {
		e->ignore();
	}
}

void DialogsWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_drawShadow) {
		p.fillRect(width() - st::dlgShadow, 0, st::dlgShadow, height(), st::dlgShadowColor->b);
	}
}

void DialogsWidget::destroyData() {
	list.destroyData();
}

PeerData *DialogsWidget::peerBefore(const PeerData *peer) const {
	return list.peerBefore(peer);
}

PeerData *DialogsWidget::peerAfter(const PeerData *peer) const {
	return list.peerAfter(peer);
}

void DialogsWidget::scrollToPeer(const PeerId &peer) {
	if (list.state() != DialogsListWidget::SearchedState) {
		list.scrollToPeer(peer);
	}
}

void DialogsWidget::removePeer(PeerData *peer) {
	_filter.setText(QString());
	onFilterUpdate();
	list.removePeer(peer);
}

void DialogsWidget::removeContact(UserData *user) {
	_filter.setText(QString());
	onFilterUpdate();
	list.removeContact(user);
}

DialogsIndexed &DialogsWidget::contactsList() {
	return list.contactsList();
}

void DialogsWidget::onAddContact() {
	App::wnd()->showLayer(new AddContactBox());
}

void DialogsWidget::onNewGroup() {
	App::wnd()->showLayer(new NewGroupBox());
}

void DialogsWidget::onCancelSearch() {
	list.clearFilter();
	_filter.clear();
	_filter.updatePlaceholder();
	onFilterUpdate();
}

void DialogsWidget::onDialogToTopFrom(int movedFrom) {
	if (scroll.scrollTop() > 0) {
		if (movedFrom > scroll.scrollTop()) {
			scroll.scrollToY(scroll.scrollTop() + st::dlgHeight);
		}
	}
}

void DialogsWidget::enableShadow(bool enable) {
	_drawShadow = enable;
}
