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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "application.h"
#include "window.h"
#include "dialogswidget.h"
#include "mainwidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/contactsbox.h"
#include "boxes/confirmbox.h"

#include "localstorage.h"

DialogsInner::DialogsInner(QWidget *parent, MainWidget *main) : SplittedWidget(parent)
, dialogs(DialogsSortByDate)
, contactsNoDialogs(DialogsSortByName)
, contacts(DialogsSortByName)
, sel(0)
, contactSel(false)
, selByMouse(false)
, _hashtagSel(-1)
, _filteredSel(-1)
, _searchedCount(0)
, _searchedMigratedCount(0)
, _searchedSel(-1)
, _peopleSel(-1)
, _lastSearchDate(0)
, _lastSearchPeer(0)
, _lastSearchId(0)
, _lastSearchMigratedId(0)
, _state(DefaultState)
, _addContactLnk(this, lang(lng_add_contact_button))
, _cancelSearchInPeer(this, st::btnCancelSearch)
, _overDelete(false)
, _searchInPeer(0)
, _searchInMigrated(0)
, _menuPeer(0)
, _menuActionPeer(0)
, _menu(0) {
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
	connect(main, SIGNAL(peerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)), this, SLOT(onPeerNameChanged(PeerData*, const PeerData::Names&, const PeerData::NameFirstChars&)));
	connect(main, SIGNAL(peerPhotoChanged(PeerData*)), this, SLOT(onPeerPhotoChanged(PeerData*)));
	connect(main, SIGNAL(dialogRowReplaced(DialogRow*,DialogRow*)), this, SLOT(onDialogRowReplaced(DialogRow*,DialogRow*)));
	connect(&_addContactLnk, SIGNAL(clicked()), App::wnd(), SLOT(onShowAddContact()));
	connect(&_cancelSearchInPeer, SIGNAL(clicked()), this, SIGNAL(cancelSearchInPeer()));
	_cancelSearchInPeer.hide();
	refresh();
}

int32 DialogsInner::filteredOffset() const {
	return _hashtagResults.size() * st::mentionHeight;
}

int32 DialogsInner::peopleOffset() const {
	return filteredOffset() + (_filterResults.size() * st::dlgHeight) + st::searchedBarHeight;
}

int32 DialogsInner::searchedOffset() const {
	int32 result = peopleOffset() + (_peopleResults.isEmpty() ? 0 : ((_peopleResults.size() * st::dlgHeight) + st::searchedBarHeight));
	if (_searchInPeer) result += st::dlgHeight;
	return result;
}

void DialogsInner::paintRegion(Painter &p, const QRegion &region, bool paintingOther) {
	QRegion original(rtl() ? region.translated(-otherWidth(), 0) : region);
	if (App::wnd() && App::wnd()->contentOverlapped(this, original)) return;

	if (!App::main()) return;

	QRect r(region.boundingRect());
	if (!paintingOther) {
		p.setClipRect(r);
	}

	if (_state == DefaultState) {
		int32 otherStart = dialogs.list.count * st::dlgHeight;
		PeerData *active = App::main()->activePeer(), *selected = _menuPeer ? _menuPeer : (sel ? sel->history->peer : 0);
		if (otherStart) {
			dialogs.list.paint(p, fullWidth(), r.top(), r.top() + r.height(), active, selected, paintingOther);
		}
		if (contactsNoDialogs.list.count && false) {
			contactsNoDialogs.list.paint(p, fullWidth(), r.top() - otherStart, r.top() + r.height() - otherStart, active, selected, paintingOther);
		} else if (!otherStart) {
			p.fillRect(r, st::white->b);
			if (!paintingOther) {
				p.setFont(st::noContactsFont->f);
				p.setPen(st::noContactsColor->p);
				p.drawText(QRect(0, 0, fullWidth(), st::noContactsHeight - (cContactsReceived() ? st::noContactsFont->height : 0)), lang(cContactsReceived() ? lng_no_chats : lng_contacts_loading), style::al_center);
			}
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (!_hashtagResults.isEmpty()) {
			int32 from = floorclamp(r.y(), st::mentionHeight, 0, _hashtagResults.size());
			int32 to = ceilclamp(r.y() + r.height(), st::mentionHeight, 0, _hashtagResults.size());
			p.translate(0, from * st::mentionHeight);
			if (from < _hashtagResults.size()) {
				int32 w = fullWidth(), htagwidth = w - st::dlgPaddingHor * 2;

				p.setFont(st::mentionFont->f);
				p.setPen(st::black->p);
				for (; from < to; ++from) {
					bool selected = (from == _hashtagSel);
					p.fillRect(0, 0, w, st::mentionHeight, (selected ? st::mentionBgOver : st::white)->b);
					if (!paintingOther) {
						if (selected) {
							int skip = (st::mentionHeight - st::notifyClose.icon.pxHeight()) / 2;
							p.drawPixmap(QPoint(w - st::notifyClose.icon.pxWidth() - skip, skip), App::sprite(), st::notifyClose.icon);

						}
						QString first = (_hashtagFilter.size() < 2) ? QString() : ('#' + _hashtagResults.at(from).mid(0, _hashtagFilter.size() - 1)), second = (_hashtagFilter.size() < 2) ? ('#' + _hashtagResults.at(from)) : _hashtagResults.at(from).mid(_hashtagFilter.size() - 1);
						int32 firstwidth = st::mentionFont->width(first), secondwidth = st::mentionFont->width(second);
						if (htagwidth < firstwidth + secondwidth) {
							if (htagwidth < firstwidth + st::mentionFont->elidew) {
								first = st::mentionFont->elided(first + second, htagwidth);
								second = QString();
							} else {
								second = st::mentionFont->elided(second, htagwidth - firstwidth);
							}
						}

						p.setFont(st::mentionFont->f);
						if (!first.isEmpty()) {
							p.setPen((selected ? st::mentionFgOverActive : st::mentionFgActive)->p);
							p.drawText(st::dlgPaddingHor, st::mentionTop + st::mentionFont->ascent, first);
						}
						if (!second.isEmpty()) {
							p.setPen((selected ? st::mentionFgOver : st::mentionFg)->p);
							p.drawText(st::dlgPaddingHor + firstwidth, st::mentionTop + st::mentionFont->ascent, second);
						}
					}
					p.translate(0, st::mentionHeight);
				}
			}
		}
		if (!_filterResults.isEmpty()) {
			int32 skip = filteredOffset();
			int32 from = floorclamp(r.y() - skip, st::dlgHeight, 0, _filterResults.size());
			int32 to = ceilclamp(r.y() + r.height() - skip, st::dlgHeight, 0, _filterResults.size());
			p.translate(0, from * st::dlgHeight);
			if (from < _filterResults.size()) {
				int32 w = fullWidth();
				PeerData *act = App::main()->activePeer();
				MsgId actId = App::main()->activeMsgId();
				for (; from < to; ++from) {
					bool active = ((_filterResults[from]->history->peer == act) || (_filterResults[from]->history->peer->migrateTo() && _filterResults[from]->history->peer->migrateTo() == act)) && !actId;
					bool selected = (from == _filteredSel) || (_filterResults[from]->history->peer == _menuPeer);
					_filterResults[from]->paint(p, w, active, selected, paintingOther);
					p.translate(0, st::dlgHeight);
				}
			}
		}

		if (!_peopleResults.isEmpty()) {
			p.fillRect(0, 0, fullWidth(), st::searchedBarHeight, st::searchedBarBG->b);
			if (!paintingOther) {
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, fullWidth(), st::searchedBarHeight), lang(lng_search_global_results), style::al_center);
			}
			p.translate(0, st::searchedBarHeight);

			int32 skip = peopleOffset();
			int32 from = floorclamp(r.y() - skip, st::dlgHeight, 0, _peopleResults.size());
			int32 to = ceilclamp(r.y() + r.height() - skip, st::dlgHeight, 0, _peopleResults.size());
			p.translate(0, from * st::dlgHeight);
			if (from < _peopleResults.size()) {
				int32 w = fullWidth();
				PeerData *act = App::main()->activePeer();
				MsgId actId = App::main()->activeMsgId();
				for (; from < to; ++from) {
					bool active = ((_peopleResults[from] == act) || (_peopleResults[from]->migrateTo() && _peopleResults[from]->migrateTo() == act)) && !actId;
					bool selected = (from == _peopleSel);
					peopleResultPaint(_peopleResults[from], p, w, active, selected, paintingOther);
					p.translate(0, st::dlgHeight);
				}
			}
		}

		if (_searchInPeer) {
			searchInPeerPaint(p, fullWidth(), paintingOther);
			p.translate(0, st::dlgHeight);
			if (_state == FilteredState && _searchResults.isEmpty()) {
				p.fillRect(0, 0, fullWidth(), st::searchedBarHeight, st::searchedBarBG->b);
				if (!paintingOther) {
					p.setFont(st::searchedBarFont->f);
					p.setPen(st::searchedBarColor->p);
					p.drawText(QRect(0, 0, fullWidth(), st::searchedBarHeight), lang(lng_dlg_search_for_messages), style::al_center);
				}
				p.translate(0, st::searchedBarHeight);
			}
		}

		if (_state == SearchedState || !_searchResults.isEmpty()) {
			QString text = lng_search_found_results(lt_count, _searchResults.isEmpty() ? 0 : (_searchedMigratedCount + _searchedCount));
			p.fillRect(0, 0, fullWidth(), st::searchedBarHeight, st::searchedBarBG->b);
			if (!paintingOther) {
				p.setFont(st::searchedBarFont->f);
				p.setPen(st::searchedBarColor->p);
				p.drawText(QRect(0, 0, fullWidth(), st::searchedBarHeight), text, style::al_center);
			}
			p.translate(0, st::searchedBarHeight);

			int32 skip = searchedOffset();
			int32 from = floorclamp(r.y() - skip, st::dlgHeight, 0, _searchResults.size());
			int32 to = ceilclamp(r.y() + r.height() - skip, st::dlgHeight, 0, _searchResults.size());
			p.translate(0, from * st::dlgHeight);
			if (from < _searchResults.size()) {
				int32 w = fullWidth();
				PeerData *act = App::main()->activePeer();
				MsgId actId = App::main()->activeMsgId();
				for (; from < to; ++from) {
					bool active = (_searchResults[from]->_item->history()->peer == act && _searchResults[from]->_item->id == actId) || (_searchResults[from]->_item->history()->peer->migrateTo() && _searchResults[from]->_item->history()->peer->migrateTo() == act && _searchResults[from]->_item->id == -actId);
					bool selected = (from == _searchedSel);
					_searchResults[from]->paint(p, w, active, selected, paintingOther);
					p.translate(0, st::dlgHeight);
				}
			}
		}
	}
}

void DialogsInner::peopleResultPaint(PeerData *peer, Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (act ? st::dlgActiveBG : (sel ? st::dlgHoverBG : st::dlgBG))->b);
	if (onlyBackground) return;

	History *history = App::history(peer->id);

	if (peer->migrateTo()) {
		p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, peer->migrateTo()->photo->pix(st::dlgPhotoSize));
	} else {
		p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, peer->photo->pix(st::dlgPhotoSize));
	}

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (peer->isChat()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgPos.x(), rectForName.top() + st::dlgChatImgPos.y()), App::sprite(), (act ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	} else if (peer->isChannel()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChannelImgPos.x(), rectForName.top() + st::dlgChannelImgPos.y()), App::sprite(), (act ? st::dlgActiveChannelImg : st::dlgChannelImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	}
	if (peer->isVerified()) {
		rectForName.setWidth(rectForName.width() - st::verifiedCheck.pxWidth() - st::verifiedCheckPos.x());
		p.drawSprite(rectForName.topLeft() + QPoint(qMin(peer->dialogName().maxWidth(), rectForName.width()), 0) + st::verifiedCheckPos, (act ? st::verifiedCheckInv : st::verifiedCheck));
	}

	QRect tr(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, namewidth, st::dlgFont->height);
	p.setFont(st::dlgHistFont->f);
	QString username = peer->userName();
	if (!act && username.toLower().startsWith(_peopleQuery)) {
		QString first = '@' + username.mid(0, _peopleQuery.size()), second = username.mid(_peopleQuery.size());
		int32 w = st::dlgHistFont->width(first);
		if (w >= tr.width()) {
			p.setPen(st::dlgSystemColor->p);
			p.drawText(tr.left(), tr.top() + st::dlgHistFont->ascent, st::dlgHistFont->elided(first, tr.width()));
		} else {
			p.setPen(st::dlgSystemColor->p);
			p.drawText(tr.left(), tr.top() + st::dlgHistFont->ascent, first);
			p.setPen(st::dlgTextColor->p);
			p.drawText(tr.left() + w, tr.top() + st::dlgHistFont->ascent, st::dlgHistFont->elided(second, tr.width() - w));
		}
	} else {
		p.setPen((act ? st::dlgActiveColor : st::dlgSystemColor)->p);
		p.drawText(tr.left(), tr.top() + st::dlgHistFont->ascent, st::dlgHistFont->elided('@' + username, tr.width()));
	}

	p.setPen((act ? st::dlgActiveColor : st::dlgNameColor)->p);
	peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void DialogsInner::searchInPeerPaint(Painter &p, int32 w, bool onlyBackground) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, st::dlgBG->b);
	if (onlyBackground) return;

	p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, _searchInPeer->photo->pix(st::dlgPhotoSize));

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor * 2 - st::btnCancelSearch.width;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (_searchInPeer->isChat() || _searchInPeer->isMegagroup()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgPos.x(), rectForName.top() + st::dlgChatImgPos.y()), App::sprite(), st::dlgChatImg);
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	} else if (_searchInPeer->isChannel()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChannelImgPos.x(), rectForName.top() + st::dlgChannelImgPos.y()), App::sprite(), st::dlgChannelImg);
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	}

	QRect tr(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, namewidth, st::dlgFont->height);
	p.setFont(st::dlgHistFont->f);
	p.setPen(st::dlgTextColor->p);
	p.drawText(tr.left(), tr.top() + st::dlgHistFont->ascent, st::dlgHistFont->elided(lang((_searchInPeer->isChannel() && !_searchInPeer->isMegagroup()) ? lng_dlg_search_channel : lng_dlg_search_chat), tr.width()));

	p.setPen(st::dlgNameColor->p);
	_searchInPeer->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void DialogsInner::activate() {
}

void DialogsInner::mouseMoveEvent(QMouseEvent *e) {
	lastMousePos = mapToGlobal(e->pos());
	selByMouse = true;
	onUpdateSelected(true);
}

void DialogsInner::onUpdateSelected(bool force) {
	QPoint mouse(mapFromGlobal(lastMousePos));
	if ((!force && !rect().contains(mouse)) || !selByMouse) return;

	int w = width(), mouseY = mouse.y();
	_overDelete = false;
	if (_state == DefaultState) {
		DialogRow *newSel = dialogs.list.rowAtY(mouseY, st::dlgHeight);
		int32 otherStart = dialogs.list.count * st::dlgHeight;
		if (newSel) {
			contactSel = false;
		} else {
			newSel = 0;// contactsNoDialogs.list.rowAtY(mouseY - otherStart, st::dlgHeight);
			contactSel = true;
		}
		if (newSel != sel) {
			updateSelectedRow();
			sel = newSel;
			updateSelectedRow();
			setCursor(sel ? style::cur_pointer : style::cur_default);
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (!_hashtagResults.isEmpty()) {
			int32 skip = 0, newHashtagSel = (mouseY >= skip) ? ((mouseY - skip) / int32(st::mentionHeight)) : -1;
			if (newHashtagSel < 0 || newHashtagSel >= _hashtagResults.size()) {
				newHashtagSel = -1;
			}
			if (newHashtagSel != _hashtagSel) {
				updateSelectedRow();
				_hashtagSel = newHashtagSel;
				updateSelectedRow();
				setCursor((_hashtagSel >= 0) ? style::cur_pointer : style::cur_default);
			}
			if (_hashtagSel >= 0) {
				_overDelete = (mouse.x() >= w - st::mentionHeight);
			}
		}
		if (!_filterResults.isEmpty()) {
			int32 skip = filteredOffset(), newFilteredSel = (mouseY >= skip) ? ((mouseY - skip) / int32(st::dlgHeight)) : -1;
			if (newFilteredSel < 0 || newFilteredSel >= _filterResults.size()) {
				newFilteredSel = -1;
			}
			if (newFilteredSel != _filteredSel) {
				updateSelectedRow();
				_filteredSel = newFilteredSel;
				updateSelectedRow();
				setCursor((_filteredSel >= 0) ? style::cur_pointer : style::cur_default);
			}
		}
		if (!_peopleResults.isEmpty()) {
			int32 skip = peopleOffset(), newPeopleSel = (mouseY >= skip) ? ((mouseY - skip) / int32(st::dlgHeight)) : -1;
			if (newPeopleSel < 0 || newPeopleSel >= _peopleResults.size()) {
				newPeopleSel = -1;
			}
			if (newPeopleSel != _peopleSel) {
				updateSelectedRow();
				_peopleSel = newPeopleSel;
				updateSelectedRow();
				setCursor((_peopleSel >= 0) ? style::cur_pointer : style::cur_default);
			}
		}
		if (_state == SearchedState && !_searchResults.isEmpty()) {
			int32 skip = searchedOffset(), newSearchedSel = (mouseY >= skip) ? ((mouseY - skip) / int32(st::dlgHeight)) : -1;
			if (newSearchedSel < 0 || newSearchedSel >= _searchResults.size()) {
				newSearchedSel = -1;
			}
			if (newSearchedSel != _searchedSel) {
				updateSelectedRow();
				_searchedSel = newSearchedSel;
				updateSelectedRow();
				setCursor((_searchedSel >= 0) ? style::cur_pointer : style::cur_default);
			}
		}
	}
}

void DialogsInner::mousePressEvent(QMouseEvent *e) {
	lastMousePos = mapToGlobal(e->pos());
	selByMouse = true;
	onUpdateSelected(true);
	if (e->button() == Qt::LeftButton) {
		choosePeer();
	}
}

void DialogsInner::resizeEvent(QResizeEvent *e) {
	_addContactLnk.move((width() - _addContactLnk.width()) / 2, (st::noContactsHeight + st::noContactsFont->height) / 2);
	_cancelSearchInPeer.move(width() - st::dlgPaddingHor - st::btnCancelSearch.width, (st::dlgHeight - st::btnCancelSearch.height) / 2);
}

void DialogsInner::onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow) {
	if (_state == FilteredState || _state == SearchedState) {
		for (FilteredDialogs::iterator i = _filterResults.begin(); i != _filterResults.end();) {
			if (*i == oldRow) { // this row is shown in filtered and maybe is in contacts!
				if (newRow) {
					*i = newRow;
					++i;
				} else {
					i = _filterResults.erase(i);
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

void DialogsInner::createDialog(History *history) {
	bool creating = history->dialogs.isEmpty();
	if (creating) {
		history->dialogs = dialogs.addToEnd(history);
		contactsNoDialogs.del(history->peer, history->dialogs[0]);
	}

	History::DialogLinks links = history->dialogs;
	int32 movedFrom = links[0]->pos * st::dlgHeight;
	dialogs.adjustByPos(links);
	int32 movedTo = links[0]->pos * st::dlgHeight;

	emit dialogMoved(movedFrom, movedTo);

	if (creating) {
		refresh();
	} else if (_state == DefaultState && movedFrom != movedTo) {
		update(0, qMin(movedFrom, movedTo), fullWidth(), qAbs(movedFrom - movedTo) + st::dlgHeight);
	}
}

void DialogsInner::removeDialog(History *history) {
	if (!history) return;
	if (history->peer == _menuPeer && _menu) {
		_menu->deleteLater();
	}
	if (sel && sel->history == history) {
		sel = 0;
	}
	dialogs.del(history->peer);
	history->dialogs = History::DialogLinks();
	history->clearNotifications();
	if (App::wnd()) App::wnd()->notifyClear(history);
	if (contacts.list.rowByPeer.constFind(history->peer->id) != contacts.list.rowByPeer.cend()) {
		if (contactsNoDialogs.list.rowByPeer.constFind(history->peer->id) == contactsNoDialogs.list.rowByPeer.cend()) {
			contactsNoDialogs.addByName(history);
		}
	}

	Local::removeSavedPeer(history->peer);

	emit App::main()->dialogsUpdated();

	refresh();
}

void DialogsInner::dlgUpdated(DialogRow *row) {
	if (_state == DefaultState) {
		update(0, row->pos * st::dlgHeight, fullWidth(), st::dlgHeight);
	} else if (_state == FilteredState || _state == SearchedState) {
		for (int32 i = 0, l = _filterResults.size(); i < l; ++i) {
			if (_filterResults.at(i)->history == row->history) {
				update(0, i * st::dlgHeight, fullWidth(), st::dlgHeight);
				break;
			}
		}
	}
}

void DialogsInner::dlgUpdated(History *history, MsgId msgId) {
	if (_state == DefaultState) {
		DialogRow *row = 0;
		DialogsList::RowByPeer::iterator i = dialogs.list.rowByPeer.find(history->peer->id);
		if (i != dialogs.list.rowByPeer.cend()) {
			update(0, i.value()->pos * st::dlgHeight, fullWidth(), st::dlgHeight);
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		int32 cnt = 0, add = filteredOffset();
		for (FilteredDialogs::const_iterator i = _filterResults.cbegin(), e = _filterResults.cend(); i != e; ++i) {
			if ((*i)->history == history) {
				update(0, add + cnt * st::dlgHeight, fullWidth(), st::dlgHeight);
				break;
			}
			++cnt;
		}
		if (!_peopleResults.isEmpty()) {
			int32 cnt = 0, add = peopleOffset();
			for (PeopleResults::const_iterator i = _peopleResults.cbegin(), e = _peopleResults.cend(); i != e; ++i) {
				if ((*i) == history->peer) {
					update(0, add + cnt * st::dlgHeight, fullWidth(), st::dlgHeight);
					break;
				}
				++cnt;
			}
		}
		if (!_searchResults.isEmpty()) {
			int32 cnt = 0, add = searchedOffset();
			for (SearchResults::const_iterator i = _searchResults.cbegin(), e = _searchResults.cend(); i != e; ++i) {
				if ((*i)->_item->history() == history && (*i)->_item->id == msgId) {
					update(0, add + cnt * st::dlgHeight, fullWidth(), st::dlgHeight);
					break;
				}
				++cnt;
			}
		}
	}
}

void DialogsInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
	lastMousePos = QCursor::pos();
	onUpdateSelected(true);
}

void DialogsInner::updateSelectedRow(PeerData *peer) {
	if (_state == DefaultState) {
		if (peer) {
			if (History *h = App::historyLoaded(peer->id)) {
				if (h->dialogs.contains(0)) {
					update(0, h->dialogs.value(0)->pos * st::dlgHeight, fullWidth(), st::dlgHeight);
				}
			}
		} else if (sel) {
			update(0, sel->pos * st::dlgHeight, fullWidth(), st::dlgHeight);
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (peer) {
			for (int32 i = 0, l = _filterResults.size(); i != l; ++i) {
				if (_filterResults.at(i)->history->peer == peer) {
					update(0, filteredOffset() + i * st::dlgHeight, fullWidth(), st::dlgHeight);
					break;
				}
			}
		} else if (_hashtagSel >= 0) {
			update(0, _hashtagSel * st::mentionHeight, fullWidth(), st::mentionHeight);
		} else if (_filteredSel >= 0) {
			update(0, filteredOffset() + _filteredSel * st::dlgHeight, fullWidth(), st::dlgHeight);
		} else if (_peopleSel >= 0) {
			update(0, peopleOffset() + _peopleSel * st::dlgHeight, fullWidth(), st::dlgHeight);
		} else if (_searchedSel >= 0) {
			update(0, searchedOffset() + _searchedSel * st::dlgHeight, fullWidth(), st::dlgHeight);
		}
	}

}

void DialogsInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	selByMouse = false;
	if (sel || _filteredSel >= 0 || _hashtagSel >= 0 || _searchedSel >= 0 || _peopleSel >= 0) {
		updateSelectedRow();
		sel = 0;
		_filteredSel = _searchedSel = _peopleSel = _hashtagSel = -1;
		setCursor(style::cur_default);
	}
}

void DialogsInner::contextMenuEvent(QContextMenuEvent *e) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
	}
	if (_menuPeer) {
		updateSelectedRow(_menuPeer);
		_menuPeer = 0;
		disconnect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
	}

	if (e->reason() == QContextMenuEvent::Mouse) {
		lastMousePos = e->globalPos();
		selByMouse = true;
		onUpdateSelected(true);
	}

	History *history = 0;
	if (_state == DefaultState) {
		if (sel) history = sel->history;
	} else if (_state == FilteredState || _state == SearchedState) {
		if (_filteredSel >= 0 && _filteredSel < _filterResults.size()) {
			history = _filterResults[_filteredSel]->history;
		}
	}
	if (!history) return;
	_menuPeer = history->peer;

	_menu = new PopupMenu();
	_menu->addAction(lang((_menuPeer->isChat() || _menuPeer->isMegagroup()) ? lng_context_view_group : (_menuPeer->isUser() ? lng_context_view_profile : lng_context_view_channel)), this, SLOT(onContextProfile()))->setEnabled(true);
	_menu->addAction(lang(menuPeerMuted() ? lng_enable_notifications_from_tray : lng_disable_notifications_from_tray), this, SLOT(onContextToggleNotifications()))->setEnabled(true);
	_menu->addAction(lang(lng_profile_search_messages), this, SLOT(onContextSearch()))->setEnabled(true);
	if (_menuPeer->isUser()) {
		_menu->addAction(lang(lng_profile_clear_history), this, SLOT(onContextClearHistory()))->setEnabled(true);
		_menu->addAction(lang(lng_profile_delete_conversation), this, SLOT(onContextDeleteAndLeave()))->setEnabled(true);
		if (_menuPeer->asUser()->access != UserNoAccess) {
			_menu->addAction(lang((_menuPeer->asUser()->blocked == UserIsBlocked) ? (_menuPeer->asUser()->botInfo ? lng_profile_unblock_bot : lng_profile_unblock_user) : (_menuPeer->asUser()->botInfo ? lng_profile_block_bot : lng_profile_block_user)), this, SLOT(onContextToggleBlock()))->setEnabled(true);
			connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
		}
	} else if (_menuPeer->isChat()) {
		_menu->addAction(lang(lng_profile_clear_history), this, SLOT(onContextClearHistory()))->setEnabled(true);
		_menu->addAction(lang(lng_profile_clear_and_exit), this, SLOT(onContextDeleteAndLeave()))->setEnabled(true);
	} else if (_menuPeer->isChannel() && _menuPeer->asChannel()->amIn() && !_menuPeer->asChannel()->amCreator()) {
		_menu->addAction(lang(_menuPeer->isMegagroup() ? lng_profile_leave_group : lng_profile_leave_channel), this, SLOT(onContextDeleteAndLeave()))->setEnabled(true);
	}

	connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroyed(QObject*)));
	_menu->popup(e->globalPos());
	e->accept();
}

bool DialogsInner::menuPeerMuted() {
	return _menuPeer && _menuPeer->notify != EmptyNotifySettings && _menuPeer->notify != UnknownNotifySettings && _menuPeer->notify->mute >= unixtime();
}

void DialogsInner::onContextProfile() {
	if (!_menuPeer) return;
	App::main()->showPeerProfile(_menuPeer);
}

void DialogsInner::onContextToggleNotifications() {
	if (!_menuPeer) return;
	App::main()->updateNotifySetting(_menuPeer, menuPeerMuted());
}

void DialogsInner::onContextSearch() {
	if (!_menuPeer) return;
	App::main()->searchInPeer(_menuPeer);
}

void DialogsInner::onContextClearHistory() {
	if (!_menuPeer || _menuPeer->isChannel()) return;

	_menuActionPeer = _menuPeer;
	ConfirmBox *box = new ConfirmBox(_menuPeer->isUser() ? lng_sure_delete_history(lt_contact, _menuPeer->name) : lng_sure_delete_group_history(lt_group, _menuPeer->name), lang(lng_box_delete), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onContextClearHistorySure()));
	Ui::showLayer(box);
}

void DialogsInner::onContextClearHistorySure() {
	if (!_menuActionPeer || _menuActionPeer->isChannel()) return;
	Ui::hideLayer();
	App::main()->clearHistory(_menuActionPeer);
}

void DialogsInner::onContextDeleteAndLeave() {
	if (!_menuPeer) return;

	_menuActionPeer = _menuPeer;
	ConfirmBox *box = new ConfirmBox(_menuPeer->isUser() ? lng_sure_delete_history(lt_contact, _menuPeer->name) : (_menuPeer->isChat() ? lng_sure_delete_and_exit(lt_group, _menuPeer->name) : lang(_menuPeer->isMegagroup() ? lng_sure_leave_group : lng_sure_leave_channel)), lang(_menuPeer->isUser() ? lng_box_delete : lng_box_leave), _menuPeer->isChannel() ? st::defaultBoxButton : st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onContextDeleteAndLeaveSure()));
	Ui::showLayer(box);
}

void DialogsInner::onContextDeleteAndLeaveSure() {
	if (!_menuActionPeer) return;

	Ui::hideLayer();
	Ui::showChatsList();
	if (_menuActionPeer->isUser()) {
		App::main()->deleteConversation(_menuActionPeer);
	} else if (_menuActionPeer->isChat()) {
		MTP::send(MTPmessages_DeleteChatUser(_menuActionPeer->asChat()->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, _menuActionPeer), App::main()->rpcFail(&MainWidget::leaveChatFailed, _menuActionPeer));
	} else if (_menuActionPeer->isChannel()) {
		if (_menuActionPeer->migrateFrom()) {
			App::main()->deleteConversation(_menuActionPeer->migrateFrom());
		}
		MTP::send(MTPchannels_LeaveChannel(_menuActionPeer->asChannel()->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
	}
}

void DialogsInner::onContextToggleBlock() {
	if (!_menuPeer || !_menuPeer->isUser()) return;
	if (_menuPeer->asUser()->blocked == UserIsBlocked) {
		MTP::send(MTPcontacts_Unblock(_menuPeer->asUser()->inputUser), rpcDone(&DialogsInner::contextBlockDone, qMakePair(_menuPeer->asUser(), false)));
	} else {
		MTP::send(MTPcontacts_Block(_menuPeer->asUser()->inputUser), rpcDone(&DialogsInner::contextBlockDone, qMakePair(_menuPeer->asUser(), true)));
	}
}

void DialogsInner::contextBlockDone(QPair<UserData*, bool> data, const MTPBool &result) {
	data.first->blocked = data.second ? UserIsBlocked : UserIsNotBlocked;
	emit App::main()->peerUpdated(data.first);
}

void DialogsInner::onMenuDestroyed(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
		if (_menuPeer) {
			updateSelectedRow(_menuPeer);
			_menuPeer = 0;
			disconnect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
		}
		lastMousePos = QCursor::pos();
		if (rect().contains(mapFromGlobal(lastMousePos))) {
			selByMouse = true;
			setMouseTracking(true);
			onUpdateSelected(true);
		}
	}
}

void DialogsInner::onParentGeometryChanged() {
	lastMousePos = QCursor::pos();
	if (rect().contains(mapFromGlobal(lastMousePos))) {
		setMouseTracking(true);
		onUpdateSelected(true);
	}
}

void DialogsInner::onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	dialogs.peerNameChanged(peer, oldNames, oldChars);
	contactsNoDialogs.peerNameChanged(peer, oldNames, oldChars);
	contacts.peerNameChanged(peer, oldNames, oldChars);
	update();
}

void DialogsInner::onPeerPhotoChanged(PeerData *peer) {
	update();
}

void DialogsInner::onFilterUpdate(QString newFilter, bool force) {
	newFilter = textSearchKey(newFilter);
	if (newFilter != _filter || force) {
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
		if (newFilter != _filter || force) {
			_filter = newFilter;
			if (!_searchInPeer && _filter.isEmpty()) {
				_state = DefaultState;
				_hashtagResults.clear();
				_filterResults.clear();
				_peopleResults.clear();
				_searchResults.clear();
				_lastSearchDate = 0;
				_lastSearchPeer = 0;
				_lastSearchId = _lastSearchMigratedId = 0;
			} else {
				QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

				_state = FilteredState;
				_filterResults.clear();
				if (!_searchInPeer && !f.isEmpty()) {
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
					_filterResults.reserve((dialogsToFilter ? dialogsToFilter->count : 0) + (contactsNoDialogsToFilter ? contactsNoDialogsToFilter->count : 0));
					if (dialogsToFilter && dialogsToFilter->count) {
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
								_filterResults.push_back(i);
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
									if (ni->startsWith(*fi)) {
										break;
									}
								}
								if (ni == ne) {
									break;
								}
							}
							if (fi == fe) {
								_filterResults.push_back(i);
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

void DialogsInner::onHashtagFilterUpdate(QStringRef newFilter) {
	if (newFilter.isEmpty() || newFilter.at(0) != '#' || _searchInPeer) {
		_hashtagFilter = QString();
		if (!_hashtagResults.isEmpty()) {
			_hashtagResults.clear();
			refresh(true);
			setMouseSel(false, true);
		}
		return;
	}
	_hashtagFilter = newFilter.toString();
	if (cRecentSearchHashtags().isEmpty() && cRecentWriteHashtags().isEmpty()) {
		Local::readRecentHashtagsAndBots();
	}
	const RecentHashtagPack &recent(cRecentSearchHashtags());
	_hashtagResults.clear();
	if (!recent.isEmpty()) {
		_hashtagResults.reserve(qMin(recent.size(), 5));
		for (RecentHashtagPack::const_iterator i = recent.cbegin(), e = recent.cend(); i != e; ++i) {
			if (i->first.startsWith(_hashtagFilter.midRef(1), Qt::CaseInsensitive) && i->first.size() + 1 != newFilter.size()) {
				_hashtagResults.push_back(i->first);
				if (_hashtagResults.size() == 5) break;
			}
		}
	}
	refresh(true);
	setMouseSel(false, true);
}

DialogsInner::~DialogsInner() {
	clearSearchResults();
}

void DialogsInner::clearSearchResults(bool clearPeople) {
	if (clearPeople) _peopleResults.clear();
	if (!_searchResults.isEmpty()) {
		for (SearchResults::const_iterator i = _searchResults.cbegin(), e = _searchResults.cend(); i != e; ++i) {
			delete *i;
		}
		_searchResults.clear();
	}
	_searchedCount = _searchedMigratedCount = 0;
	_lastSearchDate = 0;
	_lastSearchPeer = 0;
	_lastSearchId = _lastSearchMigratedId = 0;
}

void DialogsInner::updateNotifySettings(PeerData *peer) {
	if (_menu && _menuPeer == peer && _menu->actions().size() > 1) {
		_menu->actions().at(1)->setText(lang(menuPeerMuted() ? lng_enable_notifications_from_tray : lng_disable_notifications_from_tray));
	}
}

void DialogsInner::peerUpdated(PeerData *peer) {
	if (_menu && _menuPeer == peer && _menuPeer->isUser() && _menu->actions().size() > 5) {
		_menu->actions().at(5)->setText(lang((_menuPeer->asUser()->blocked == UserIsBlocked) ? (_menuPeer->asUser()->botInfo ? lng_profile_unblock_bot : lng_profile_unblock_user) : (_menuPeer->asUser()->botInfo ? lng_profile_block_bot : lng_profile_block_user)));
	}
}

PeerData *DialogsInner::updateFromParentDrag(QPoint globalPos) {
	lastMousePos = globalPos;
	selByMouse = true;
	onUpdateSelected(true);
	if (_state == DefaultState) {
		if (sel) return sel->history->peer;
	} else if (_state == FilteredState || _state == SearchedState) {
		if (_filteredSel >= 0 && _filteredSel < _filterResults.size()) {
			return _filterResults[_filteredSel]->history->peer;
		} else if (_peopleSel >= 0 && _peopleSel < _peopleResults.size()) {
			return _peopleResults[_peopleSel];
		} else if (_searchedSel >= 0 && _searchedSel < _searchResults.size()) {
			return _searchResults[_searchedSel]->_item->history()->peer;
		}
	}
	return 0;
}

void DialogsInner::itemRemoved(HistoryItem *item) {
	int wasCount = _searchResults.size();
	for (int i = 0; i < _searchResults.size();) {
		if (_searchResults[i]->_item == item) {
			_searchResults.remove(i);
			if (item->history()->peer == _searchInMigrated) {
				if (_searchedMigratedCount > 0) --_searchedMigratedCount;
			} else {
				if (_searchedCount > 0) --_searchedCount;
			}
		} else {
			++i;
		}
	}
	if (wasCount != _searchResults.size()) {
		refresh();
	}
}

void DialogsInner::dialogsReceived(const QVector<MTPDialog> &added) {
	for (QVector<MTPDialog>::const_iterator i = added.cbegin(), e = added.cend(); i != e; ++i) {
		History *history = 0;
		switch (i->type()) {
		case mtpc_dialog: {
			const MTPDdialog &d(i->c_dialog());
			history = App::historyFromDialog(peerFromMTP(d.vpeer), d.vunread_count.v, d.vread_inbox_max_id.v);
			App::main()->applyNotifySetting(MTP_notifyPeer(d.vpeer), d.vnotify_settings, history);
		} break;

		case mtpc_dialogChannel: {
			const MTPDdialogChannel &d(i->c_dialogChannel());
			PeerData *peer = App::peerLoaded(peerFromMTP(d.vpeer));
			int32 unreadCount = (peer && peer->isMegagroup()) ? d.vunread_count.v : d.vunread_important_count.v;
			History *history = App::historyFromDialog(peerFromMTP(d.vpeer), unreadCount, d.vread_inbox_max_id.v);
			if (history->peer->isChannel()) {
				history->asChannelHistory()->unreadCountAll = d.vunread_count.v;
				history->peer->asChannel()->ptsReceived(d.vpts.v);
				if (!history->peer->asChannel()->amCreator()) {
					MsgId topMsg = history->isMegagroup() ? d.vtop_message.v : d.vtop_important_message.v;
					if (HistoryItem *top = App::histItemById(history->channelId(), topMsg)) {
						if (top->date <= date(history->peer->asChannel()->date) && App::api()) {
							App::api()->requestSelfParticipant(history->peer->asChannel());
						}
					}
				}
			}
			if (!history->isMegagroup() && d.vtop_message.v > d.vtop_important_message.v) {
				history->setNotLoadedAtBottom();
			}
			App::main()->applyNotifySetting(MTP_notifyPeer(d.vpeer), d.vnotify_settings, history);
		} break;
		}

		if (history) {
			if (!history->lastMsgDate.isNull()) {
				addSavedPeersAfter(history->lastMsgDate);
			}
			contactsNoDialogs.del(history->peer);
			if (history->peer->migrateFrom()) {
				removeDialog(App::historyLoaded(history->peer->migrateFrom()->id));
			} else if (history->peer->migrateTo() && history->peer->migrateTo()->amIn()) {
				removeDialog(history);
			}
		}
	}

	if (App::wnd()) App::wnd()->updateCounter();
	if (!sel && dialogs.list.count) {
		sel = dialogs.list.begin;
		contactSel = false;
	}
	refresh();
}

void DialogsInner::addSavedPeersAfter(const QDateTime &date) {
	SavedPeersByTime &saved(cRefSavedPeersByTime());
	while (!saved.isEmpty() && (date.isNull() || date < saved.lastKey())) {
		History *history = App::history(saved.last()->id);
		history->setPosInDialogsDate(saved.lastKey());
		contactsNoDialogs.del(history->peer);
		saved.remove(saved.lastKey(), saved.last());
	}
}

void DialogsInner::addAllSavedPeers() {
	addSavedPeersAfter(QDateTime());
}

bool DialogsInner::searchReceived(const QVector<MTPMessage> &messages, DialogsSearchRequestType type, int32 fullCount) {
	if (type == DialogsSearchFromStart || type == DialogsSearchPeerFromStart) {
		clearSearchResults(false);
	}
	int32 lastDateFound = 0;
	for (QVector<MTPMessage>::const_iterator i = messages.cbegin(), e = messages.cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addNewMessage(*i, NewMessageExisting);
		int32 lastDate = dateFromMessage(*i);
		if (lastDate) {
			_searchResults.push_back(new FakeDialogRow(item));
			lastDateFound = lastDate;
			if (type == DialogsSearchFromStart || type == DialogsSearchFromOffset) {
				_lastSearchDate = lastDateFound;
			}
		}
		if (type == DialogsSearchFromStart || type == DialogsSearchFromOffset) {
			_lastSearchPeer = item->history()->peer;
		}
		if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
			_lastSearchMigratedId = item->id;
		} else {
			_lastSearchId = item->id;
		}
	}
	if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
		_searchedMigratedCount = fullCount;
	} else {
		_searchedCount = fullCount;
	}
	if (_state == FilteredState && (!_searchResults.isEmpty() || !_searchInMigrated || type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset)) {
		_state = SearchedState;
	}
	refresh();
	return lastDateFound != 0;
}

void DialogsInner::peopleReceived(const QString &query, const QVector<MTPPeer> &people) {
	_peopleQuery = query.toLower().trimmed();
	_peopleResults.clear();
	_peopleResults.reserve(people.size());
	for (QVector<MTPPeer>::const_iterator i = people.cbegin(), e = people.cend(); i != e; ++i) {
		PeerId peerId = peerFromMTP(*i);
		History *h = App::historyLoaded(peerId);
		if (h && !h->dialogs.isEmpty()) continue; // skip dialogs

		_peopleResults.push_back(App::peer(peerId));
	}
	refresh();
}

void DialogsInner::contactsReceived(const QVector<MTPContact> &contacts) {
	for (QVector<MTPContact>::const_iterator i = contacts.cbegin(), e = contacts.cend(); i != e; ++i) {
		int32 uid = i->c_contact().vuser_id.v;
		if (uid == MTP::authedId() && App::self()) {
			if (App::self()->contact < 1) {
				App::self()->contact = 1;
				Notify::userIsContactChanged(App::self());
			}
		}
	}
	if (!sel && contactsNoDialogs.list.count && false) {
		sel = contactsNoDialogs.list.begin;
		contactSel = true;
	}
	refresh();
}

void DialogsInner::notify_userIsContactChanged(UserData *user, bool fromThisApp) {
	if (user->contact > 0) {
		History *history = App::history(user->id);
		contacts.addByName(history);
		DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(user->id);
		if (i == dialogs.list.rowByPeer.cend()) {
			contactsNoDialogs.addByName(history);
		} else if (fromThisApp) {
			sel = i.value();
			contactSel = false;
		}
	} else {
		if (sel && sel->history->peer == user) {
			sel = 0;
		}
		contactsNoDialogs.del(user);
		contacts.del(user);
	}
	refresh();
}

void DialogsInner::refresh(bool toTop) {
	int32 h = 0;
	if (_state == DefaultState) {
		h = (dialogs.list.count/* + contactsNoDialogs.list.count*/) * st::dlgHeight;
		if (h) {
			if (!_addContactLnk.isHidden()) _addContactLnk.hide();
		} else {
			h = st::noContactsHeight;
			if (cContactsReceived()) {
				if (_addContactLnk.isHidden()) _addContactLnk.show();
			} else {
				if (!_addContactLnk.isHidden()) _addContactLnk.hide();
			}
		}
	} else {
		if (!_addContactLnk.isHidden()) _addContactLnk.hide();
		if (_state == FilteredState) {
			h = searchedOffset() + (_searchResults.count() * st::dlgHeight) + ((_searchResults.isEmpty() && !_searchInPeer) ? -st::searchedBarHeight : 0);
		} else if (_state == SearchedState) {
			h = searchedOffset() + (_searchResults.count() * st::dlgHeight);
		}
	}
	setHeight(h);
	if (toTop) {
		emit mustScrollTo(0, 0);
		loadPeerPhotos(0);
	}
	update();
}

void DialogsInner::setMouseSel(bool msel, bool toTop) {
	selByMouse = msel;
	if (!selByMouse && toTop) {
		if (_state == DefaultState) {
			sel = (dialogs.list.count ? dialogs.list.begin : (contactsNoDialogs.list.count ? contactsNoDialogs.list.begin : 0));
			contactSel = !dialogs.list.count && contactsNoDialogs.list.count;
		} else if (_state == FilteredState || _state == SearchedState) { // don't select first elem in search
			_filteredSel = _peopleSel = _searchedSel = _hashtagSel = -1;
			setCursor(style::cur_default);
		}
	}
}

void DialogsInner::setState(State newState) {
	_state = newState;
	if (_state == DefaultState) {
		clearSearchResults();
		_searchedSel = _peopleSel = _filteredSel = _hashtagSel = -1;
	} else if (_state == DefaultState || _state == SearchedState) {
		_hashtagResults.clear();
		_hashtagSel = -1;
		_filterResults.clear();
		_filteredSel = -1;
	}
	onFilterUpdate(_filter, true);
}

DialogsInner::State DialogsInner::state() const {
	return _state;
}

bool DialogsInner::hasFilteredResults() const {
	return !_filterResults.isEmpty() && _hashtagResults.isEmpty();
}

void DialogsInner::searchInPeer(PeerData *peer) {
	_searchInPeer = peer ? (peer->migrateTo() ? peer->migrateTo() : peer) : 0;
	_searchInMigrated = _searchInPeer ? _searchInPeer->migrateFrom() : 0;
	if (_searchInPeer) {
		onHashtagFilterUpdate(QStringRef());
		_cancelSearchInPeer.show();
	} else {
		_cancelSearchInPeer.hide();
	}
}

void DialogsInner::clearFilter() {
	if (_state == FilteredState || _state == SearchedState) {
		if (_searchInPeer) {
			_state = FilteredState;
		} else {
			_state = DefaultState;
		}
		_hashtagResults.clear();
		_filterResults.clear();
		_peopleResults.clear();
		_searchResults.clear();
		_lastSearchDate = 0;
		_lastSearchPeer = 0;
		_lastSearchId = _lastSearchMigratedId = 0;
		_filter = QString();
		refresh(true);
	}
}

void DialogsInner::selectSkip(int32 direction) {
	if (_state == DefaultState) {
		if (!sel) {
			if (dialogs.list.count && direction > 0) {
				sel = dialogs.list.begin;
			} else if (false && contactsNoDialogs.list.count && direction > 0) {
				sel = contactsNoDialogs.list.begin;
			} else {
				return;
			}
		} else if (direction > 0) {
			if (sel->next->next) {
				sel = sel->next;
			} else if (false && sel->next == dialogs.list.end && contactsNoDialogs.list.count) {
				sel = contactsNoDialogs.list.begin;
				contactSel = true;
			}
		} else {
			if (sel->prev) {
				sel = sel->prev;
			} else if (false && sel == contactsNoDialogs.list.begin && dialogs.list.count) {
				sel = dialogs.list.end->prev;
				contactSel = false;
			}
		}
		int32 fromY = (sel->pos + (contactSel ? dialogs.list.count : 0)) * st::dlgHeight;
		emit mustScrollTo(fromY, fromY + st::dlgHeight);
	} else if (_state == FilteredState || _state == SearchedState) {
		if (_hashtagResults.isEmpty() && _filterResults.isEmpty() && _peopleResults.isEmpty() && _searchResults.isEmpty()) return;
		if ((_hashtagSel < 0 || _hashtagSel >= _hashtagResults.size()) &&
			(_filteredSel < 0 || _filteredSel >= _filterResults.size()) &&
			(_peopleSel < 0 || _peopleSel >= _peopleResults.size()) &&
			(_searchedSel < 0 || _searchedSel >= _searchResults.size())) {
			if (_hashtagResults.isEmpty() && _filterResults.isEmpty() && _peopleResults.isEmpty()) {
				_searchedSel = 0;
			} else if (_hashtagResults.isEmpty() && _filterResults.isEmpty()) {
				_peopleSel = 0;
			} else if (_hashtagResults.isEmpty()) {
				_filteredSel = 0;
			} else {
				_hashtagSel = 0;
			}
		} else {
			int32 cur = (_hashtagSel >= 0 && _hashtagSel < _hashtagResults.size()) ? _hashtagSel : ((_filteredSel >= 0 && _filteredSel < _filterResults.size()) ? (_hashtagResults.size() + _filteredSel) : ((_peopleSel >= 0 && _peopleSel < _peopleResults.size()) ? (_peopleSel + _filterResults.size() + _hashtagResults.size()) : (_searchedSel + _peopleResults.size() + _filterResults.size() + _hashtagResults.size())));
			cur = snap(cur + direction, 0, _hashtagResults.size() + _filterResults.size() + _peopleResults.size() + _searchResults.size() - 1);
			if (cur < _hashtagResults.size()) {
				_hashtagSel = cur;
				_filteredSel = _peopleSel = _searchedSel = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size()) {
				_filteredSel = cur - _hashtagResults.size();
				_hashtagSel = _peopleSel = _searchedSel = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size() + _peopleResults.size()) {
				_peopleSel = cur - _hashtagResults.size() - _filterResults.size();
				_hashtagSel = _filteredSel = _searchedSel = -1;
			} else {
				_hashtagSel = _filteredSel = _peopleSel = -1;
				_searchedSel = cur - _hashtagResults.size() - _filterResults.size() - _peopleResults.size();
			}
		}
		if (_hashtagSel >= 0 && _hashtagSel < _hashtagResults.size()) {
			emit mustScrollTo(_hashtagSel * st::mentionHeight, (_hashtagSel + 1) * st::mentionHeight);
		} else if (_filteredSel >= 0 && _filteredSel < _filterResults.size()) {
			emit mustScrollTo(filteredOffset() + _filteredSel * st::dlgHeight, filteredOffset() + (_filteredSel + 1) * st::dlgHeight);
		} else if (_peopleSel >= 0 && _peopleSel < _peopleResults.size()) {
			emit mustScrollTo(peopleOffset() + _peopleSel * st::dlgHeight + (_peopleSel ? 0 : -st::searchedBarHeight), peopleOffset() + (_peopleSel + 1) * st::dlgHeight);
		} else {
			emit mustScrollTo(searchedOffset() + _searchedSel * st::dlgHeight + (_searchedSel ? 0 : -st::searchedBarHeight), searchedOffset() + (_searchedSel + 1) * st::dlgHeight);
		}
	}
	update();
}

void DialogsInner::scrollToPeer(const PeerId &peer, MsgId msgId) {
	int32 fromY = -1;
	if (_state == DefaultState) {
		DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(peer);
		if (i != dialogs.list.rowByPeer.cend()) {
			fromY = i.value()->pos * st::dlgHeight;
		} else if (false) {
			i = contactsNoDialogs.list.rowByPeer.constFind(peer);
			if (i != contactsNoDialogs.list.rowByPeer.cend()) {
				fromY = (i.value()->pos + dialogs.list.count) * st::dlgHeight;
			}
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (msgId) {
			for (int32 i = 0, c = _searchResults.size(); i < c; ++i) {
				if (_searchResults[i]->_item->history()->peer->id == peer && _searchResults[i]->_item->id == msgId) {
					fromY = searchedOffset() + i * st::dlgHeight;
					break;
				}
			}
		}
		if (fromY < 0) {
			for (int32 i = 0, c = _filterResults.size(); i < c; ++i) {
				if (_filterResults[i]->history->peer->id == peer) {
					fromY = filteredOffset() + (i * st::dlgHeight);
					break;
				}
			}
		}
	}
	if (fromY >= 0) {
		emit mustScrollTo(fromY, fromY + st::dlgHeight);
	}
}

void DialogsInner::selectSkipPage(int32 pixels, int32 direction) {
	int32 toSkip = pixels / int32(st::dlgHeight);
	if (_state == DefaultState) {
		if (!sel) {
			if (direction > 0 && dialogs.list.count) {
				sel = dialogs.list.begin;
			} else if (false && direction > 0 && contactsNoDialogs.list.count) {
				sel = contactsNoDialogs.list.begin;
			} else {
				return;
			}
		}
		if (direction > 0) {
			while (toSkip-- && sel->next->next) {
				sel = sel->next;
			}
			if (false && toSkip >= 0 && sel->next == dialogs.list.end && contactsNoDialogs.list.count) {
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
	update();
}

void DialogsInner::loadPeerPhotos(int32 yFrom) {
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
		int32 from = (yFrom - filteredOffset()) / st::dlgHeight;
		if (from < 0) from = 0;
		if (from < _filterResults.size()) {
			int32 to = (yTo / int32(st::dlgHeight)) + 1, w = width();
			if (to > _filterResults.size()) to = _filterResults.size();

			for (; from < to; ++from) {
				_filterResults[from]->history->peer->photo->load();
			}
		}

		from = (yFrom > filteredOffset() + st::searchedBarHeight ? ((yFrom - filteredOffset() - st::searchedBarHeight) / int32(st::dlgHeight)) : 0) - _filterResults.size();
		if (from < 0) from = 0;
		if (from < _peopleResults.size()) {
			int32 to = (yTo > filteredOffset() + st::searchedBarHeight ? ((yTo - filteredOffset() - st::searchedBarHeight) / int32(st::dlgHeight)) : 0) - _filterResults.size() + 1, w = width();
			if (to > _peopleResults.size()) to = _peopleResults.size();

			for (; from < to; ++from) {
				_peopleResults[from]->photo->load();
			}
		}
		from = (yFrom > filteredOffset() + ((_peopleResults.isEmpty() ? 0 : st::searchedBarHeight) + st::searchedBarHeight) ? ((yFrom - filteredOffset() - (_peopleResults.isEmpty() ? 0 : st::searchedBarHeight) - st::searchedBarHeight) / int32(st::dlgHeight)) : 0) - _filterResults.size() - _peopleResults.size();
		if (from < 0) from = 0;
		if (from < _searchResults.size()) {
			int32 to = (yTo > filteredOffset() + (_peopleResults.isEmpty() ? 0 : st::searchedBarHeight) + st::searchedBarHeight ? ((yTo - filteredOffset() - (_peopleResults.isEmpty() ? 0 : st::searchedBarHeight) - st::searchedBarHeight) / int32(st::dlgHeight)) : 0) - _filterResults.size() - _peopleResults.size() + 1, w = width();
			if (to > _searchResults.size()) to = _searchResults.size();

			for (; from < to; ++from) {
				_searchResults[from]->_item->history()->peer->photo->load();
			}
		}
	}
}

bool DialogsInner::choosePeer() {
	History *history = 0;
	MsgId msgId = ShowAtUnreadMsgId;
	if (_state == DefaultState) {
		if (sel) history = sel->history;
	} else if (_state == FilteredState || _state == SearchedState) {
		if (_hashtagSel >= 0 && _hashtagSel < _hashtagResults.size()) {
			QString hashtag = _hashtagResults.at(_hashtagSel);
			if (_overDelete) {
				lastMousePos = QCursor::pos();

				RecentHashtagPack recent(cRecentSearchHashtags());
				for (RecentHashtagPack::iterator i = recent.begin(); i != recent.cend();) {
					if (i->first == hashtag) {
						i = recent.erase(i);
					} else {
						++i;
					}
				}
				cSetRecentSearchHashtags(recent);
				Local::writeRecentHashtagsAndBots();
				emit refreshHashtags();

				selByMouse = true;
				onUpdateSelected(true);
			} else {
				saveRecentHashtags('#' + hashtag);
				emit completeHashtag(hashtag);
			}
			return true;
		}
		if (_filteredSel >= 0 && _filteredSel < _filterResults.size()) {
			history = _filterResults[_filteredSel]->history;
		} else if (_peopleSel >= 0 && _peopleSel < _peopleResults.size()) {
			history = App::history(_peopleResults[_peopleSel]->id);
		} else if (_searchedSel >= 0 && _searchedSel < _searchResults.size()) {
			history = _searchResults[_searchedSel]->_item->history();
			msgId = _searchResults[_searchedSel]->_item->id;
		}
	}
	if (history) {
		if (msgId > 0) {
			saveRecentHashtags(_filter);
		}
		bool chosen = (!App::main()->selectingPeer(true) && (_state == FilteredState || _state == SearchedState) && _filteredSel >= 0 && _filteredSel < _filterResults.size());
		App::main()->choosePeer(history->peer->id, msgId);
		if (chosen) {
			emit searchResultChosen();
		}
		updateSelectedRow();
		sel = 0;
		_filteredSel = _peopleSel = _searchedSel = _hashtagSel = -1;
		return true;
	}
	return false;
}

void DialogsInner::saveRecentHashtags(const QString &text) {
	bool found = false;
	QRegularExpressionMatch m;
	RecentHashtagPack recent(cRecentSearchHashtags());
	for (int32 i = 0, next = 0; (m = reHashtag().match(text, i)).hasMatch(); i = next) {
		i = m.capturedStart();
		next = m.capturedEnd();
		if (m.hasMatch()) {
			if (!m.capturedRef(1).isEmpty()) {
				++i;
			}
			if (!m.capturedRef(2).isEmpty()) {
				--next;
			}
		}
		if (!found && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) {
			Local::readRecentHashtagsAndBots();
			recent = cRecentSearchHashtags();
		}
		found = true;
		incrementRecentHashtag(recent, text.mid(i + 1, next - i - 1));
	}
	if (found) {
		cSetRecentSearchHashtags(recent);
		Local::writeRecentHashtagsAndBots();
	}
}

void DialogsInner::destroyData() {
	sel = 0;
	contactSel = false;
	_hashtagSel = -1;
	_hashtagResults.clear();
	_filteredSel = -1;
	_filterResults.clear();
	_filter.clear();
	_searchedSel = _peopleSel = -1;
	clearSearchResults();
	contacts.clear();
	contactsNoDialogs.clear();
	dialogs.clear();
}

void DialogsInner::peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	if (_state == DefaultState) {
		DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(inPeer->id);
		if (i == dialogs.list.rowByPeer.constEnd()) {
			i = contactsNoDialogs.list.rowByPeer.constFind(inPeer->id);
			if (i == contactsNoDialogs.list.rowByPeer.cend()) {
				outPeer = 0;
				outMsg = 0;
				return;
			}
			if (i.value()->prev) {
				outPeer = i.value()->prev->history->peer;
				outMsg = ShowAtUnreadMsgId;
				return;
			} else if (dialogs.list.count) {
				outPeer = dialogs.list.end->prev->history->peer;
				outMsg = ShowAtUnreadMsgId;
				return;
			}
			outPeer = 0;
			outMsg = 0;
			return;
		}
		if (i.value()->prev) {
			outPeer = i.value()->prev->history->peer;
			outMsg = ShowAtUnreadMsgId;
			return;
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (inMsg && !_searchResults.isEmpty()) {
			for (SearchResults::const_iterator b = _searchResults.cbegin(), i = b + 1, e = _searchResults.cend(); i != e; ++i) {
				if ((*i)->_item->history()->peer == inPeer && (*i)->_item->id == inMsg) {
					SearchResults::const_iterator j = i - 1;
					outPeer = (*j)->_item->history()->peer;
					outMsg = (*j)->_item->id;
					return;
				}
			}
			if (_searchResults.at(0)->_item->history()->peer == inPeer && _searchResults.at(0)->_item->id == inMsg) {
				outMsg = ShowAtUnreadMsgId;
				if (_peopleResults.isEmpty()) {
					if (_filterResults.isEmpty()) {
						outPeer = 0;
					} else {
						outPeer = _filterResults.back()->history->peer;
					}
				} else {
					outPeer = _peopleResults.back();
				}
				return;
			}
		}
		if (!_peopleResults.isEmpty() && _peopleResults.at(0) == inPeer) {
			outPeer = _filterResults.isEmpty() ? 0 : _filterResults.back()->history->peer;
			outMsg = ShowAtUnreadMsgId;
			return;
		}
		if (!_peopleResults.isEmpty()) {
			for (PeopleResults::const_iterator b = _peopleResults.cbegin(), i = b + 1, e = _peopleResults.cend(); i != e; ++i) {
				if ((*i) == inPeer) {
					outPeer = (*(i - 1));
					outMsg = ShowAtUnreadMsgId;
					return;
				}
			}
		}
		if (_filterResults.isEmpty() || _filterResults.at(0)->history->peer == inPeer) {
			outPeer = 0;
			outMsg = 0;
			return;
		}

		for (FilteredDialogs::const_iterator b = _filterResults.cbegin(), i = b + 1, e = _filterResults.cend(); i != e; ++i) {
			if ((*i)->history->peer == inPeer) {
				outPeer = (*(i - 1))->history->peer;
				outMsg = ShowAtUnreadMsgId;
				return;
			}
		}
	}
	outPeer = 0;
	outMsg = 0;
}

void DialogsInner::peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	if (_state == DefaultState) {
		DialogsList::RowByPeer::const_iterator i = dialogs.list.rowByPeer.constFind(inPeer->id);
		if (i == dialogs.list.rowByPeer.constEnd()) {
			//i = contactsNoDialogs.list.rowByPeer.constFind(inPeer->id);
			//if (i == contactsNoDialogs.list.rowByPeer.cend()) {
			//	outPeer = 0;
			//	outMsg = 0;
			//	return;
			//}
			//if (i.value()->next != contactsNoDialogs.list.end) {
			//	outPeer = i.value()->next->history->peer;
			//	outMsg = ShowAtUnreadMsgId;
			//	return;
			//}
			outPeer = 0;
			outMsg = 0;
			return;
		}

		if (i.value()->next != dialogs.list.end) {
			outPeer = i.value()->next->history->peer;
			outMsg = ShowAtUnreadMsgId;
			return;
		} else if (false && contactsNoDialogs.list.count) {
			outPeer = contactsNoDialogs.list.begin->history->peer;
			outMsg = ShowAtUnreadMsgId;
			return;
		}
	} else if (_state == FilteredState || _state == SearchedState) {
		if (inMsg) {
			for (SearchResults::const_iterator i = _searchResults.cbegin(), e = _searchResults.cend(); i != e; ++i) {
				if ((*i)->_item->history()->peer == inPeer && (*i)->_item->id == inMsg) {
					++i;
					outPeer = (i == e) ? 0 : (*i)->_item->history()->peer;
					outMsg = (i == e) ? 0 : (*i)->_item->id;
					return;
				}
			}
		}
		for (PeopleResults::const_iterator i = _peopleResults.cbegin(), e = _peopleResults.cend(); i != e; ++i) {
			if ((*i) == inPeer) {
				++i;
				if (i == e && !_searchResults.isEmpty()) {
					outPeer = _searchResults.front()->_item->history()->peer;
					outMsg = _searchResults.front()->_item->id;
				} else {
					outPeer = (i == e) ? 0 : (*i);
					outMsg = ShowAtUnreadMsgId;
				}
				return;
			}
		}
		for (FilteredDialogs::const_iterator i = _filterResults.cbegin(), e = _filterResults.cend(); i != e; ++i) {
			if ((*i)->history->peer == inPeer) {
				++i;
				if (i == e && !_peopleResults.isEmpty()) {
					outPeer = _peopleResults.front();
					outMsg = ShowAtUnreadMsgId;
				} else if (i == e && !_searchResults.isEmpty()) {
					outPeer = _searchResults.front()->_item->history()->peer;
					outMsg = _searchResults.front()->_item->id;
				} else {
					outPeer = (i == e) ? 0 : (*i)->history->peer;
					outMsg = ShowAtUnreadMsgId;
				}
				return;
			}
		}
	}
	outPeer = 0;
	outMsg = 0;
}

DialogsIndexed &DialogsInner::contactsList() {
	return contacts;
}

DialogsIndexed &DialogsInner::dialogsList() {
	return dialogs;
}

DialogsInner::FilteredDialogs &DialogsInner::filteredList() {
	return _filterResults;
}

DialogsInner::PeopleResults &DialogsInner::peopleList() {
	return _peopleResults;
}

DialogsInner::SearchResults &DialogsInner::searchList() {
	return _searchResults;
}

int32 DialogsInner::lastSearchDate() const {
	return _lastSearchDate;
}

PeerData *DialogsInner::lastSearchPeer() const {
	return _lastSearchPeer;
}

MsgId DialogsInner::lastSearchId() const {
	return _lastSearchId;
}

MsgId DialogsInner::lastSearchMigratedId() const {
	return _lastSearchMigratedId;
}

DialogsWidget::DialogsWidget(MainWidget *parent) : TWidget(parent)
, _dragInScroll(false)
, _dragForward(false)
, _dialogsFull(false)
, _dialogsOffsetDate(0)
, _dialogsOffsetId(0)
, _dialogsOffsetPeer(0)
, _dialogsRequest(0)
, _contactsRequest(0)
, _filter(this, st::dlgFilter, lang(lng_dlg_filter))
, _newGroup(this, st::btnNewGroup)
, _addContact(this, st::btnAddContact)
, _cancelSearch(this, st::btnCancelSearch)
, _scroll(this, st::dlgScroll)
, _inner(&_scroll, parent)
, _a_show(animation(this, &DialogsWidget::step_show))
, _searchInPeer(0)
, _searchInMigrated(0)
, _searchFull(false)
, _searchFullMigrated(false)
, _peopleFull(false)
{
	_scroll.setWidget(&_inner);
	_scroll.setFocusPolicy(Qt::NoFocus);
	connect(&_inner, SIGNAL(mustScrollTo(int,int)), &_scroll, SLOT(scrollToY(int,int)));
	connect(&_inner, SIGNAL(dialogMoved(int,int)), this, SLOT(onDialogMoved(int,int)));
	connect(&_inner, SIGNAL(searchMessages()), this, SLOT(onNeedSearchMessages()));
	connect(&_inner, SIGNAL(searchResultChosen()), this, SLOT(onCancel()));
	connect(&_inner, SIGNAL(completeHashtag(QString)), this, SLOT(onCompleteHashtag(QString)));
	connect(&_inner, SIGNAL(refreshHashtags()), this, SLOT(onFilterCursorMoved()));
	connect(&_inner, SIGNAL(cancelSearchInPeer()), this, SLOT(onCancelSearchInPeer()));
	connect(&_scroll, SIGNAL(geometryChanged()), &_inner, SLOT(onParentGeometryChanged()));
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(onUpdateSelected()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(&_filter, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_filter, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onFilterCursorMoved(int,int)));
	connect(parent, SIGNAL(dialogsUpdated()), this, SLOT(onListScroll()));
	connect(&_addContact, SIGNAL(clicked()), this, SLOT(onAddContact()));
	connect(&_newGroup, SIGNAL(clicked()), this, SLOT(onNewGroup()));
	connect(&_cancelSearch, SIGNAL(clicked()), this, SLOT(onCancelSearch()));

	_chooseByDragTimer.setSingleShot(true);
	connect(&_chooseByDragTimer, SIGNAL(timeout()), this, SLOT(onChooseByDrag()));

	setAcceptDrops(true);

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchMessages()));

	_scroll.show();
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
	_inner.activate();
}

void DialogsWidget::createDialog(History *history) {
	bool creating = history->dialogs.isEmpty();
	_inner.createDialog(history);
	if (creating && history->peer->migrateFrom()) {
		if (History *h = App::historyLoaded(history->peer->migrateFrom()->id)) {
			if (!h->dialogs.isEmpty()) {
				removeDialog(h);
			}
		}
	}
}

void DialogsWidget::dlgUpdated(DialogRow *row) {
	_inner.dlgUpdated(row);
}

void DialogsWidget::dlgUpdated(History *row, MsgId msgId) {
	_inner.dlgUpdated(row, msgId);
}

void DialogsWidget::dialogsToUp() {
	if (_filter.getLastText().trimmed().isEmpty()) {
		_scroll.scrollToY(0);
	}
}

void DialogsWidget::animShow(const QPixmap &bgAnimCache) {
	if (App::app()) App::app()->mtpPause();

	bool back = true;
	(back ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.stop();

	(back ? _cacheUnder : _cacheOver) = myGrab(this);

	_scroll.hide();
	_filter.hide();
	_cancelSearch.hide();
	_newGroup.hide();

	a_coordUnder = back ? anim::ivalue(-qFloor(st::slideShift * width()), 0) : anim::ivalue(0, -qFloor(st::slideShift * width()));
	a_coordOver = back ? anim::ivalue(0, width()) : anim::ivalue(width(), 0);
	a_shadow = back ? anim::fvalue(1, 0) : anim::fvalue(0, 1);
	_a_show.start();

	show();
}

void DialogsWidget::step_show(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		_a_show.stop();

		a_coordUnder.finish();
		a_coordOver.finish();
		a_shadow.finish();

		_cacheUnder = _cacheOver = QPixmap();

		_scroll.show();
		_filter.show();
		_a_show.stop();

		onFilterUpdate();
		activate();

		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordUnder.update(dt, st::slideFunction);
		a_coordOver.update(dt, st::slideFunction);
		a_shadow.update(dt, st::slideFunction);
	}
	if (timer) update();
}

void DialogsWidget::onCancel() {
	if (!onCancelSearch() || (!_searchInPeer && !App::main()->selectingPeer())) {
		emit cancelled();
	}
}

void DialogsWidget::itemRemoved(HistoryItem *item) {
	_inner.itemRemoved(item);
}

void DialogsWidget::updateNotifySettings(PeerData *peer) {
	_inner.updateNotifySettings(peer);
}

void DialogsWidget::notify_userIsContactChanged(UserData *user, bool fromThisApp) {
	if (fromThisApp) {
		_filter.setText(QString());
		_filter.updatePlaceholder();
		onFilterUpdate();
	}
	_inner.notify_userIsContactChanged(user, fromThisApp);
}

void DialogsWidget::unreadCountsReceived(const QVector<MTPDialog> &dialogs) {
	for (QVector<MTPDialog>::const_iterator i = dialogs.cbegin(), e = dialogs.cend(); i != e; ++i) {
		switch (i->type()) {
		case mtpc_dialog: {
			const MTPDdialog &d(i->c_dialog());
			if (History *h = App::historyLoaded(peerFromMTP(d.vpeer))) {
				App::main()->applyNotifySetting(MTP_notifyPeer(d.vpeer), d.vnotify_settings, h);
				if (d.vunread_count.v >= h->unreadCount) {
					h->setUnreadCount(d.vunread_count.v, false);
					h->inboxReadBefore = d.vread_inbox_max_id.v + 1;
				}
			}
		} break;
		case mtpc_dialogChannel: {
			const MTPDdialogChannel &d(i->c_dialogChannel());
			if (History *h = App::historyLoaded(peerFromMTP(d.vpeer))) {
				if (h->peer->isChannel()) {
					h->peer->asChannel()->ptsReceived(d.vpts.v);
					if (d.vunread_count.v >= h->asChannelHistory()->unreadCountAll) {
						h->asChannelHistory()->unreadCountAll = d.vunread_count.v;
						h->inboxReadBefore = d.vread_inbox_max_id.v + 1;
					}
				}
				App::main()->applyNotifySetting(MTP_notifyPeer(d.vpeer), d.vnotify_settings, h);
				int32 unreadCount = h->isMegagroup() ? d.vunread_count.v : d.vunread_important_count.v;
				if (unreadCount >= h->unreadCount) {
					h->setUnreadCount(unreadCount, false);
					h->inboxReadBefore = d.vread_inbox_max_id.v + 1;
				}
			}
		} break;
		}
	}
	if (App::wnd()) App::wnd()->updateCounter();
}

void DialogsWidget::dialogsReceived(const MTPmessages_Dialogs &dialogs, mtpRequestId req) {
	if (_dialogsRequest != req) return;

	const QVector<MTPDialog> *v = 0;
	const QVector<MTPMessage> *m = 0;
	switch (dialogs.type()) {
	case mtpc_messages_dialogs: {
		const MTPDmessages_dialogs &data(dialogs.c_messages_dialogs());
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		m = &data.vmessages.c_vector().v;
		v = &data.vdialogs.c_vector().v;
		_dialogsFull = true;
	} break;
	case mtpc_messages_dialogsSlice: {
		const MTPDmessages_dialogsSlice &data(dialogs.c_messages_dialogsSlice());
		App::feedUsers(data.vusers);
		App::feedChats(data.vchats);
		m = &data.vmessages.c_vector().v;
		v = &data.vdialogs.c_vector().v;
	} break;
	}

	if (!_contactsRequest) {
		_contactsRequest = MTP::send(MTPcontacts_GetContacts(MTP_string("")), rpcDone(&DialogsWidget::contactsReceived), rpcFail(&DialogsWidget::contactsFailed));
	}

	if (m) {
		App::feedMsgs(*m, NewMessageLast);
	}
	if (v) {
		unreadCountsReceived(*v);
		_inner.dialogsReceived(*v);
		onListScroll();

		int32 lastDate = 0;
		PeerId lastPeer = 0;
		MsgId lastMsgId = 0;
		for (int32 i = v->size(); i > 0;) {
			PeerId peer = 0;
			MsgId msgId = 0;
			const MTPDialog &d(v->at(--i));
			switch (d.type()) {
			case mtpc_dialog:
				msgId = d.c_dialog().vtop_message.v;
				peer = peerFromMTP(d.c_dialog().vpeer);
			break;
			case mtpc_dialogChannel:
				//msgId = d.c_dialogChannel().vtop_important_message.v;
				//if (!msgId) msgId = d.c_dialogChannel().vtop_message.v;
				msgId = d.c_dialogChannel().vtop_message.v;
				peer = peerFromMTP(d.c_dialogChannel().vpeer);
			break;
			}
			if (peer) {
				if (!lastPeer) lastPeer = peer;
				if (msgId) {
					if (!lastMsgId) lastMsgId = msgId;
					for (int32 j = m->size(); j > 0;) {
						const MTPMessage &d(m->at(--j));
						if (idFromMessage(d) == msgId && peerFromMessage(d) == peer) {
							int32 date = dateFromMessage(d);
							if (date) lastDate = date;
							break;
						}
					}
					if (lastDate) break;
				}
			}
		}
		if (lastDate) {
			_dialogsOffsetDate = lastDate;
			_dialogsOffsetId = lastMsgId;
			_dialogsOffsetPeer = App::peer(lastPeer);
		} else {
			_dialogsFull = true;
		}
	} else {
		_dialogsFull = true;
	}

	_dialogsRequest = 0;
	loadDialogs();
}

bool DialogsWidget::dialogsFailed(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_dialogsRequest == req) {
		_dialogsRequest = 0;
	}
	return true;
}

bool DialogsWidget::onSearchMessages(bool searchCache) {
	QString q = _filter.getLastText().trimmed();
	if (q.isEmpty()) {
		if (_searchRequest) {
			MTP::cancel(_searchRequest);
			_searchRequest = 0;
		}
		if (_peopleRequest) {
			MTP::cancel(_peopleRequest);
			_peopleRequest = 0;
		}
		return true;
	}
	if (searchCache) {
		SearchCache::const_iterator i = _searchCache.constFind(q);
		if (i != _searchCache.cend()) {
			_searchQuery = q;
			_searchFull = _searchFullMigrated = false;
			if (_searchRequest) {
				MTP::cancel(_searchRequest);
				_searchRequest = 0;
			}
			searchReceived(_searchInPeer ? DialogsSearchPeerFromStart : DialogsSearchFromStart, i.value(), 0);
			return true;
		}
	} else if (_searchQuery != q) {
		_searchQuery = q;
		_searchFull = _searchFullMigrated = false;
		if (_searchRequest) {
			MTP::cancel(_searchRequest);
		}
		if (_searchInPeer) {
			int32 flags = (_searchInPeer->isChannel() && !_searchInPeer->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
			_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _searchInPeer->input, MTP_string(_searchQuery), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, DialogsSearchPeerFromStart), rpcFail(&DialogsWidget::searchFailed, DialogsSearchPeerFromStart));
		} else {
			_searchRequest = MTP::send(MTPmessages_SearchGlobal(MTP_string(_searchQuery), MTP_int(0), MTP_inputPeerEmpty(), MTP_int(0), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, DialogsSearchFromStart), rpcFail(&DialogsWidget::searchFailed, DialogsSearchFromStart));
		}
		_searchQueries.insert(_searchRequest, _searchQuery);
	}
	if (!_searchInPeer && q.size() >= MinUsernameLength) {
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
			_peopleRequest = MTP::send(MTPcontacts_Search(MTP_string(_peopleQuery), MTP_int(SearchPeopleLimit)), rpcDone(&DialogsWidget::peopleReceived), rpcFail(&DialogsWidget::peopleFailed));
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void DialogsWidget::onNeedSearchMessages() {
	if (!onSearchMessages(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void DialogsWidget::onChooseByDrag() {
	_inner.choosePeer();
}

void DialogsWidget::searchMessages(const QString &query, PeerData *inPeer) {
	if ((_filter.getLastText() != query) || (inPeer && inPeer != _searchInPeer && inPeer->migrateTo() != _searchInPeer)) {
		if (inPeer) {
			_searchInPeer = inPeer->migrateTo() ? inPeer->migrateTo() : inPeer;
			_searchInMigrated = _searchInPeer ? _searchInPeer->migrateFrom() : 0;
			_inner.searchInPeer(_searchInPeer);
		}
		_filter.setText(query);
		_filter.updatePlaceholder();
		onFilterUpdate(true);
		_searchTimer.stop();
		onSearchMessages();

		_inner.saveRecentHashtags(query);
	}
}

void DialogsWidget::onSearchMore() {
	if (!_searchRequest) {
		if (!_searchFull) {
			int32 offsetDate = _inner.lastSearchDate();
			PeerData *offsetPeer = _inner.lastSearchPeer();
			MsgId offsetId = _inner.lastSearchId();
			if (_searchInPeer) {
				int32 flags = (_searchInPeer->isChannel() && !_searchInPeer->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
				_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _searchInPeer->input, MTP_string(_searchQuery), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(offsetId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart), rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchPeerFromOffset : DialogsSearchPeerFromStart));
			} else {
				_searchRequest = MTP::send(MTPmessages_SearchGlobal(MTP_string(_searchQuery), MTP_int(offsetDate), offsetPeer ? offsetPeer->input : MTP_inputPeerEmpty(), MTP_int(offsetId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart), rpcFail(&DialogsWidget::searchFailed, offsetId ? DialogsSearchFromOffset : DialogsSearchFromStart));
			}
			if (!offsetId) {
				_searchQueries.insert(_searchRequest, _searchQuery);
			}
		} else if (_searchInMigrated && !_searchFullMigrated) {
			MsgId offsetMigratedId = _inner.lastSearchMigratedId();
			int32 flags = (_searchInMigrated->isChannel() && !_searchInMigrated->isMegagroup()) ? MTPmessages_Search::flag_important_only : 0;
			_searchRequest = MTP::send(MTPmessages_Search(MTP_int(flags), _searchInMigrated->input, MTP_string(_searchQuery), MTP_inputMessagesFilterEmpty(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(offsetMigratedId), MTP_int(SearchPerPage)), rpcDone(&DialogsWidget::searchReceived, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart), rpcFail(&DialogsWidget::searchFailed, offsetMigratedId ? DialogsSearchMigratedFromOffset : DialogsSearchMigratedFromStart));
		}
	}
}

void DialogsWidget::loadDialogs() {
	if (_dialogsRequest) return;
	if (_dialogsFull) {
		_inner.addAllSavedPeers();
		cSetDialogsReceived(true);
		return;
	}

	int32 loadCount = _dialogsOffsetDate ? DialogsPerPage : DialogsFirstLoad;
	_dialogsRequest = MTP::send(MTPmessages_GetDialogs(MTP_int(_dialogsOffsetDate), MTP_int(_dialogsOffsetId), _dialogsOffsetPeer ? _dialogsOffsetPeer->input : MTP_inputPeerEmpty(), MTP_int(loadCount)), rpcDone(&DialogsWidget::dialogsReceived), rpcFail(&DialogsWidget::dialogsFailed));
}

void DialogsWidget::contactsReceived(const MTPcontacts_Contacts &contacts) {
	cSetContactsReceived(true);
	if (contacts.type() == mtpc_contacts_contacts) {
		const MTPDcontacts_contacts &d(contacts.c_contacts_contacts());
		App::feedUsers(d.vusers);
		_inner.contactsReceived(d.vcontacts.c_vector().v);
	}
	if (App::main()) App::main()->contactsReceived();
}

bool DialogsWidget::contactsFailed(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	return true;
}

void DialogsWidget::searchReceived(DialogsSearchRequestType type, const MTPmessages_Messages &result, mtpRequestId req) {
	if (_inner.state() == DialogsInner::FilteredState || _inner.state() == DialogsInner::SearchedState) {
		if (type == DialogsSearchFromStart || type == DialogsSearchPeerFromStart) {
			SearchQueries::iterator i = _searchQueries.find(req);
			if (i != _searchQueries.cend()) {
				_searchCache[i.value()] = result;
				_searchQueries.erase(i);
			}
		}
	}

	if (_searchRequest == req) {
		switch (result.type()) {
		case mtpc_messages_messages: {
			const MTPDmessages_messages &d(result.c_messages_messages());
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			const QVector<MTPMessage> &msgs(d.vmessages.c_vector().v);
			if (!_inner.searchReceived(msgs, type, msgs.size())) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;

		case mtpc_messages_messagesSlice: {
			const MTPDmessages_messagesSlice &d(result.c_messages_messagesSlice());
			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			const QVector<MTPMessage> &msgs(d.vmessages.c_vector().v);
			if (!_inner.searchReceived(msgs, type, d.vcount.v)) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;

		case mtpc_messages_channelMessages: {
			const MTPDmessages_channelMessages &d(result.c_messages_channelMessages());
			if (_searchInPeer && _searchInPeer->isChannel()) {
				_searchInPeer->asChannel()->ptsReceived(d.vpts.v);
			} else {
				LOG(("API Error: received messages.channelMessages when no channel was passed! (DialogsWidget::searchReceived)"));
			}
			if (d.has_collapsed()) { // should not be returned
				LOG(("API Error: channels.getMessages and messages.getMessages should not return collapsed groups! (DialogsWidget::searchReceived)"));
			}

			App::feedUsers(d.vusers);
			App::feedChats(d.vchats);
			const QVector<MTPMessage> &msgs(d.vmessages.c_vector().v);
			if (!_inner.searchReceived(msgs, type, d.vcount.v)) {
				if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
					_searchFullMigrated = true;
				} else {
					_searchFull = true;
				}
			}
		} break;
		}

		_searchRequest = 0;
		onListScroll();
	}
}

void DialogsWidget::peopleReceived(const MTPcontacts_Found &result, mtpRequestId req) {
	QString q = _peopleQuery;
	if (_inner.state() == DialogsInner::FilteredState || _inner.state() == DialogsInner::SearchedState) {
		PeopleQueries::iterator i = _peopleQueries.find(req);
		if (i != _peopleQueries.cend()) {
			q = i.value();
			_peopleCache[q] = result;
			_peopleQueries.erase(i);
		}
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
		onListScroll();
	}
}

bool DialogsWidget::searchFailed(DialogsSearchRequestType type, const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (_searchRequest == req) {
		_searchRequest = 0;
		if (type == DialogsSearchMigratedFromStart || type == DialogsSearchMigratedFromOffset) {
			_searchFullMigrated = true;
		} else {
			_searchFull = true;
		}
	}
	return true;
}

bool DialogsWidget::peopleFailed(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (_peopleRequest == req) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
	return true;
}

void DialogsWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (App::main()->selectingPeer()) return;

	_dragInScroll = false;
	_dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-selected"));
	if (!_dragForward) _dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-pressed-link"));
	if (!_dragForward) _dragForward = e->mimeData()->hasFormat(qsl("application/x-td-forward-pressed"));
	if (_dragForward && !cWideMode()) _dragForward = false;
	if (_dragForward) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
		updateDragInScroll(_scroll.geometry().contains(e->pos()));
	} else if (App::main() && App::main()->getDragState(e->mimeData()) != DragStateNone) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
	}
	_chooseByDragTimer.stop();
}

void DialogsWidget::dragMoveEvent(QDragMoveEvent *e) {
	if (_scroll.geometry().contains(e->pos())) {
		if (_dragForward) {
			updateDragInScroll(true);
		} else {
			_chooseByDragTimer.start(ChoosePeerByDragTimeout);
		}
		PeerData *p = _inner.updateFromParentDrag(mapToGlobal(e->pos()));
		if (p) {
			e->setDropAction(Qt::CopyAction);
		} else {
			e->setDropAction(Qt::IgnoreAction);
		}
	} else {
		if (_dragForward) updateDragInScroll(false);
		_inner.leaveEvent(0);
		e->setDropAction(Qt::IgnoreAction);
	}
	e->accept();
}

void DialogsWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dragForward) {
		updateDragInScroll(false);
	} else {
		_chooseByDragTimer.stop();
	}
	_inner.leaveEvent(0);
	e->accept();
}

void DialogsWidget::updateDragInScroll(bool inScroll) {
	if (_dragInScroll != inScroll) {
		_dragInScroll = inScroll;
		if (_dragInScroll) {
			App::main()->forwardLayer(1);
		} else {
			App::main()->dialogsCancelled();
		}
	}
}

void DialogsWidget::dropEvent(QDropEvent *e) {
	_chooseByDragTimer.stop();
	if (_scroll.geometry().contains(e->pos())) {
		PeerData *p = _inner.updateFromParentDrag(mapToGlobal(e->pos()));
		if (p) {
			e->acceptProposedAction();
			App::main()->onFilesOrForwardDrop(p->id, e->mimeData());
		}
	}
}

void DialogsWidget::onListScroll() {
//	if (!App::self()) return;

	_inner.loadPeerPhotos(_scroll.scrollTop());
	if (_inner.state() == DialogsInner::SearchedState || (_inner.state() == DialogsInner::FilteredState && _searchInMigrated && _searchFull && !_searchFullMigrated)) {
		if (_scroll.scrollTop() > (_inner.searchList().size() + _inner.filteredList().size() + _inner.peopleList().size()) * st::dlgHeight - PreloadHeightsCount * _scroll.height()) {
			onSearchMore();
		}
	} else if (_scroll.scrollTop() > _inner.dialogsList().list.count * st::dlgHeight - PreloadHeightsCount * _scroll.height()) {
		loadDialogs();
	}
}

void DialogsWidget::onFilterUpdate(bool force) {
	if (_a_show.animating() && !force) return;

	QString filterText = _filter.getLastText();
	_inner.onFilterUpdate(filterText, force);
	if (filterText.isEmpty()) {
		_searchCache.clear();
		_searchQueries.clear();
		_searchQuery = QString();
		_cancelSearch.hide();
		_newGroup.show();
	} else if (_cancelSearch.isHidden()) {
		_cancelSearch.show();
		_newGroup.hide();
	}
	if (filterText.size() < MinUsernameLength) {
		_peopleCache.clear();
		_peopleQueries.clear();
		_peopleQuery = QString();
	}
}

void DialogsWidget::searchInPeer(PeerData *peer) {
	onCancelSearch();
	_searchInPeer = peer ? (peer->migrateTo() ? peer->migrateTo() : peer) : 0;
	_searchInMigrated = _searchInPeer ? _searchInPeer->migrateFrom() : 0;
	_inner.searchInPeer(_searchInPeer);
	onFilterUpdate(true);
}

void DialogsWidget::onFilterCursorMoved(int from, int to) {
	if (to < 0) to = _filter.cursorPosition();
	QString t = _filter.getLastText();
	QStringRef r;
	for (int start = to; start > 0;) {
		--start;
		if (t.size() <= start) break;
		if (t.at(start) == '#') {
			r = t.midRef(start, to - start);
			break;
		}
		if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') break;
	}
	_inner.onHashtagFilterUpdate(r);
}

void DialogsWidget::onCompleteHashtag(QString tag) {
	QString t = _filter.getLastText(), r;
	int cur = _filter.cursorPosition();
	for (int start = cur; start > 0;) {
		--start;
		if (t.size() <= start) break;
		if (t.at(start) == '#') {
			if (cur == start + 1 || t.midRef(start + 1, cur - start - 1) == tag.midRef(0, cur - start - 1)) {
				for (; cur < t.size() && cur - start - 1 < tag.size(); ++cur) {
					if (t.at(cur) != tag.at(cur - start - 1)) break;
				}
				if (cur - start - 1 == tag.size() && cur < t.size() && t.at(cur) == ' ') ++cur;
				r = t.mid(0, start + 1) + tag + ' ' + t.mid(cur);
				_filter.setText(r);
				_filter.setCursorPosition(start + 1 + tag.size() + 1);
				onFilterUpdate(true);
				return;
			}
			break;
		}
		if (!t.at(start).isLetterOrNumber() && t.at(start) != '_') break;
	}
	_filter.setText(t.mid(0, cur) + '#' + tag + ' ' + t.mid(cur));
	_filter.setCursorPosition(cur + 1 + tag.size() + 1);
	onFilterUpdate(true);
}

void DialogsWidget::resizeEvent(QResizeEvent *e) {
	int32 w = width();
	_filter.setGeometry(st::dlgPaddingHor, st::dlgFilterPadding, w - 2 * st::dlgPaddingHor, _filter.height());
	_newGroup.move(w - _newGroup.width() - st::dlgPaddingHor, _filter.y());
	_addContact.move(w - _addContact.width() - st::dlgPaddingHor, _filter.y());
	_cancelSearch.move(w - _cancelSearch.width() - st::dlgPaddingHor, _filter.y());
	_scroll.move(0, _filter.height() + 2 * st::dlgFilterPadding);

	int32 addToY = App::main() ? App::main()->contentScrollAddToY() : 0;
	int32 newScrollY = _scroll.scrollTop() + addToY;
	_scroll.resize(w, height() - _filter.y() - _filter.height() - st::dlgFilterPadding - st::dlgPaddingVer);
	if (addToY) {
		_scroll.scrollToY(newScrollY);
	} else {
		onListScroll();
	}
}

void DialogsWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!_inner.choosePeer()) {
			if (_inner.state() == DialogsInner::DefaultState || _inner.state() == DialogsInner::SearchedState || (_inner.state() == DialogsInner::FilteredState && _inner.hasFilteredResults())) {
				_inner.selectSkip(1);
				_inner.choosePeer();
			} else {
				onSearchMessages();
			}
		}
	} else if (e->key() == Qt::Key_Down) {
		_inner.setMouseSel(false);
		_inner.selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner.setMouseSel(false);
		_inner.selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner.setMouseSel(false);
		_inner.selectSkipPage(_scroll.height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner.setMouseSel(false);
		_inner.selectSkipPage(_scroll.height(), -1);
	} else {
		e->ignore();
	}
}

void DialogsWidget::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	QPainter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	if (_a_show.animating()) {
		if (a_coordOver.current() > 0) {
			p.drawPixmap(QRect(0, 0, a_coordOver.current(), height()), _cacheUnder, QRect(-a_coordUnder.current() * cRetinaFactor(), 0, a_coordOver.current() * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(a_shadow.current() * st::slideFadeOut);
			p.fillRect(0, 0, a_coordOver.current(), height(), st::black->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow);
		return;
	}
	QRect above(0, 0, width(), _scroll.y());
	if (above.intersects(r)) {
		p.fillRect(above.intersected(r), st::white->b);
	}
	QRect below(0, _scroll.y() + qMin(_scroll.height(), _inner.height()), width(), height());
	if (below.intersects(r)) {
		p.fillRect(below.intersected(r), st::white->b);
	}
}

void DialogsWidget::destroyData() {
	_inner.destroyData();
}

void DialogsWidget::peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	return _inner.peerBefore(inPeer, inMsg, outPeer, outMsg);
}

void DialogsWidget::peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const {
	return _inner.peerAfter(inPeer, inMsg, outPeer, outMsg);
}

void DialogsWidget::scrollToPeer(const PeerId &peer, MsgId msgId) {
	_inner.scrollToPeer(peer, msgId);
}

void DialogsWidget::removeDialog(History *history) {
	_inner.removeDialog(history);
	onFilterUpdate();
}

DialogsIndexed &DialogsWidget::contactsList() {
	return _inner.contactsList();
}

DialogsIndexed &DialogsWidget::dialogsList() {
	return _inner.dialogsList();
}

void DialogsWidget::onAddContact() {
	Ui::showLayer(new AddContactBox(), KeepOtherLayers);
}

void DialogsWidget::onNewGroup() {
	Ui::showLayer(new NewGroupBox());
}

bool DialogsWidget::onCancelSearch() {
	bool clearing = !_filter.getLastText().isEmpty();
	if (_searchRequest) {
		MTP::cancel(_searchRequest);
		_searchRequest = 0;
	}
	if (_searchInPeer && !clearing) {
		if (!cWideMode()) {
			Ui::showPeerHistory(_searchInPeer, ShowAtUnreadMsgId);
		}
		_searchInPeer = _searchInMigrated = 0;
		_inner.searchInPeer(0);
		clearing = true;
	}
	_inner.clearFilter();
	_filter.clear();
	_filter.updatePlaceholder();
	onFilterUpdate();
	return clearing;
}

void DialogsWidget::onCancelSearchInPeer() {
	if (_searchRequest) {
		MTP::cancel(_searchRequest);
		_searchRequest = 0;
	}
	if (_searchInPeer) {
		if (!cWideMode() && !App::main()->selectingPeer()) {
			Ui::showPeerHistory(_searchInPeer, ShowAtUnreadMsgId);
		}
		_searchInPeer = _searchInMigrated = 0;
		_inner.searchInPeer(0);
	}
	_inner.clearFilter();
	_filter.clear();
	_filter.updatePlaceholder();
	onFilterUpdate();
	if (cWideMode() && !App::main()->selectingPeer()) {
		emit cancelled();
	}
}

void DialogsWidget::onDialogMoved(int movedFrom, int movedTo) {
	int32 st = _scroll.scrollTop();
	if (st > movedTo && st < movedFrom) {
		_scroll.scrollToY(st + st::dlgHeight);
	}
}
