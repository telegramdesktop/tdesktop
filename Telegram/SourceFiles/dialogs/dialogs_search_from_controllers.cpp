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
#include "dialogs/dialogs_search_from_controllers.h"

#include "lang/lang_keys.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "apiwrap.h"

namespace Dialogs {

void ShowSearchFromBox(PeerData *peer, base::lambda<void(gsl::not_null<UserData*>)> callback) {
	auto createController = [peer, callback = std::move(callback)]()->std::unique_ptr<PeerListController> {
		if (peer) {
			if (auto chat = peer->asChat()) {
				return std::make_unique<Dialogs::ChatSearchFromController>(chat, std::move(callback));
			} else if (auto group = peer->asMegagroup()) {
				return std::make_unique<Dialogs::ChannelSearchFromController>(group, std::move(callback));
			}
		}
		return nullptr;
	};
	if (auto controller = createController()) {
		Ui::show(Box<PeerListBox>(std::move(controller), [](PeerListBox *box) {
			box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
		}), KeepOtherLayers);
	}
}

ChatSearchFromController::ChatSearchFromController(gsl::not_null<ChatData*> chat, base::lambda<void(gsl::not_null<UserData*>)> callback) : PeerListController()
, _chat(chat)
, _callback(std::move(callback)) {
}

void ChatSearchFromController::prepare() {
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_search_messages_from));

	rebuildRows();

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::MembersChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _chat) {
			rebuildRows();
		}
	}));
}

void ChatSearchFromController::rowClicked(gsl::not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());
	_callback(row->peer()->asUser());
}

void ChatSearchFromController::rebuildRows() {
	auto ms = getms();
	auto wasEmpty = !delegate()->peerListFullRowsCount();

	auto now = unixtime();
	QMultiMap<int32, UserData*> ordered;
	if (_chat->noParticipantInfo()) {
		AuthSession::Current().api().requestFullPeer(_chat);
	} else if (!_chat->participants.isEmpty()) {
		for (auto i = _chat->participants.cbegin(), e = _chat->participants.cend(); i != e; ++i) {
			auto user = i.key();
			ordered.insertMulti(App::onlineForSort(user, now), user);
		}
	}
	for_const (auto user, _chat->lastAuthors) {
		if (user->isInaccessible()) continue;
		appendRow(user);
		if (!ordered.isEmpty()) {
			ordered.remove(App::onlineForSort(user, now), user);
		}
	}
	if (!ordered.isEmpty()) {
		for (auto i = ordered.cend(), b = ordered.cbegin(); i != b;) {
			appendRow(*(--i));
		}
	}
	checkForEmptyRows();
	delegate()->peerListRefreshRows();
}

void ChatSearchFromController::checkForEmptyRows() {
	if (delegate()->peerListFullRowsCount()) {
		setDescriptionText(QString());
	} else {
		setDescriptionText(lang(lng_contacts_loading));
	}
}

void ChatSearchFromController::appendRow(gsl::not_null<UserData*> user) {
	if (!delegate()->peerListFindRow(user->id)) {
		delegate()->peerListAppendRow(std::make_unique<PeerListRow>(user));
	}
}

ChannelSearchFromController::ChannelSearchFromController(gsl::not_null<ChannelData*> channel, base::lambda<void(gsl::not_null<UserData*>)> callback) : ParticipantsBoxController(channel, ParticipantsBoxController::Role::Members)
, _callback(std::move(callback)) {
}

void ChannelSearchFromController::prepare() {
	ParticipantsBoxController::prepare();
	delegate()->peerListSetTitle(langFactory(lng_search_messages_from));
}

void ChannelSearchFromController::rowClicked(gsl::not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());
	_callback(row->peer()->asUser());
}

std::unique_ptr<PeerListRow> ChannelSearchFromController::createRow(gsl::not_null<UserData*> user) const {
	return std::make_unique<PeerListRow>(user);
}

} // namespace Dialogs
