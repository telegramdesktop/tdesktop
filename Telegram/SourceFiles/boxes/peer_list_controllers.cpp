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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/peer_list_controllers.h"

#include "styles/style_boxes.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "dialogs/dialogs_indexed_list.h"

void PeerListRowWithLink::setActionLink(const QString &action) {
	_action = action;
	refreshActionLink();
}

void PeerListRowWithLink::refreshActionLink() {
	if (!isInitialized()) return;
	_actionWidth = _action.isEmpty() ? 0 : st::normalFont->width(_action);
}

void PeerListRowWithLink::lazyInitialize() {
	PeerListRow::lazyInitialize();
	refreshActionLink();
}

QSize PeerListRowWithLink::actionSize() const {
	return QSize(_actionWidth, st::normalFont->height);
}

QMargins PeerListRowWithLink::actionMargins() const {
	return QMargins(st::contactsCheckPosition.x(), (st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom() - st::normalFont->height) / 2, st::contactsCheckPosition.x(), 0);
}

void PeerListRowWithLink::paintAction(Painter &p, TimeMs ms, int x, int y, int outerWidth, bool actionSelected) {
	p.setFont(actionSelected ? st::linkOverFont : st::linkFont);
	p.setPen(actionSelected ? st::defaultLinkButton.overColor : st::defaultLinkButton.color);
	p.drawTextLeft(x, y, outerWidth, _action, _actionWidth);
}

PeerListGlobalSearchController::PeerListGlobalSearchController() {
	_timer.setCallback([this] { searchOnServer(); });
}

void PeerListGlobalSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_requestId = 0;
		if (_query.size() >= MinUsernameLength && !searchInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

bool PeerListGlobalSearchController::searchInCache() {
	auto it = _cache.find(_query);
	if (it != _cache.cend()) {
		_requestId = 0;
		searchDone(it->second, _requestId);
		return true;
	}
	return false;
}

void PeerListGlobalSearchController::searchOnServer() {
	_requestId = request(MTPcontacts_Search(MTP_string(_query), MTP_int(SearchPeopleLimit))).done([this](const MTPcontacts_Found &result, mtpRequestId requestId) {
		searchDone(result, requestId);
	}).fail([this](const RPCError &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();
	_queries.emplace(_requestId, _query);
}

void PeerListGlobalSearchController::searchDone(const MTPcontacts_Found &result, mtpRequestId requestId) {
	Expects(result.type() == mtpc_contacts_found);

	auto &contacts = result.c_contacts_found();
	auto query = _query;
	if (requestId) {
		App::feedUsers(contacts.vusers);
		App::feedChats(contacts.vchats);
		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			query = it->second;
			_cache[query] = result;
			_queries.erase(it);
		}
	}
	if (_requestId == requestId) {
		_requestId = 0;
		for_const (auto &mtpPeer, contacts.vresults.v) {
			if (auto peer = App::peerLoaded(peerFromMTP(mtpPeer))) {
				delegate()->peerListSearchAddRow(peer);
			}
		}
		delegate()->peerListSearchRefreshRows();
	}
}

bool PeerListGlobalSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

ChatsListBoxController::ChatsListBoxController(std::unique_ptr<PeerListSearchController> searchController) : PeerListController(std::move(searchController)) {
}

void ChatsListBoxController::prepare() {
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);

	prepareViewHook();

	rebuildRows();

	auto &sessionData = Auth().data();
	subscribe(sessionData.contactsLoaded(), [this](bool loaded) {
		rebuildRows();
	});
	subscribe(sessionData.moreChatsLoaded(), [this] {
		rebuildRows();
	});
	subscribe(sessionData.allChatsLoaded(), [this](bool loaded) {
		checkForEmptyRows();
	});
}

void ChatsListBoxController::rebuildRows() {
	auto wasEmpty = !delegate()->peerListFullRowsCount();
	auto appendList = [this](auto chats) {
		auto count = 0;
		for_const (auto row, chats->all()) {
			auto history = row->history();
			if (history->peer->isUser()) {
				if (appendRow(history)) {
					++count;
				}
			}
		}
		return count;
	};
	auto added = appendList(App::main()->dialogsList());
	added += appendList(App::main()->contactsNoDialogsList());
	if (!wasEmpty && added > 0) {
		// Place dialogs list before contactsNoDialogs list.
		delegate()->peerListPartitionRows([](PeerListRow &a) {
			auto history = static_cast<Row&>(a).history();
			return history->inChatList(Dialogs::Mode::All);
		});
	}
	checkForEmptyRows();
	delegate()->peerListRefreshRows();
}

void ChatsListBoxController::checkForEmptyRows() {
	if (delegate()->peerListFullRowsCount()) {
		setDescriptionText(QString());
	} else {
		auto &sessionData = Auth().data();
		auto loaded = sessionData.contactsLoaded().value() && sessionData.allChatsLoaded().value();
		setDescriptionText(lang(loaded ? lng_contacts_not_found : lng_contacts_loading));
	}
}

std::unique_ptr<PeerListRow> ChatsListBoxController::createSearchRow(gsl::not_null<PeerData*> peer) {
	return createRow(App::history(peer));
}

bool ChatsListBoxController::appendRow(gsl::not_null<History*> history) {
	if (auto row = delegate()->peerListFindRow(history->peer->id)) {
		updateRowHook(static_cast<Row*>(row));
		return false;
	}
	if (auto row = createRow(history)) {
		delegate()->peerListAppendRow(std::move(row));
		return true;
	}
	return false;
}

ContactsBoxController::ContactsBoxController(std::unique_ptr<PeerListSearchController> searchController) : PeerListController(std::move(searchController)) {
}

void ContactsBoxController::prepare() {
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_contacts_header));

	prepareViewHook();

	rebuildRows();

	auto &sessionData = Auth().data();
	subscribe(sessionData.contactsLoaded(), [this](bool loaded) {
		rebuildRows();
	});
}

void ContactsBoxController::rebuildRows() {
	auto appendList = [this](auto chats) {
		auto count = 0;
		for_const (auto row, chats->all()) {
			auto history = row->history();
			if (auto user = history->peer->asUser()) {
				if (appendRow(user)) {
					++count;
				}
			}
		}
		return count;
	};
	appendList(App::main()->contactsList());
	checkForEmptyRows();
	delegate()->peerListRefreshRows();
}

void ContactsBoxController::checkForEmptyRows() {
	if (delegate()->peerListFullRowsCount()) {
		setDescriptionText(QString());
	} else {
		auto &sessionData = Auth().data();
		auto loaded = sessionData.contactsLoaded().value();
		setDescriptionText(lang(loaded ? lng_contacts_not_found : lng_contacts_loading));
	}
}

std::unique_ptr<PeerListRow> ContactsBoxController::createSearchRow(gsl::not_null<PeerData*> peer) {
	return createRow(peer->asUser());
}

void ContactsBoxController::rowClicked(gsl::not_null<PeerListRow*> row) {
	Ui::showPeerHistory(row->peer(), ShowAtUnreadMsgId);
}

bool ContactsBoxController::appendRow(gsl::not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		updateRowHook(row);
		return false;
	}
	if (auto row = createRow(user)) {
		delegate()->peerListAppendRow(std::move(row));
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> ContactsBoxController::createRow(gsl::not_null<UserData*> user) {
	return std::make_unique<PeerListRow>(user);
}
