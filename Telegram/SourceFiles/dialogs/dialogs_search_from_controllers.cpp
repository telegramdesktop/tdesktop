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
#include "data/data_peer_values.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "apiwrap.h"

namespace Dialogs {

void ShowSearchFromBox(
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer,
		base::lambda<void(not_null<UserData*>)> callback,
		base::lambda<void()> closedCallback) {
	auto createController = [
		navigation,
		peer,
		callback = std::move(callback)
	]() -> std::unique_ptr<PeerListController> {
		if (peer) {
			if (auto chat = peer->asChat()) {
				return std::make_unique<Dialogs::ChatSearchFromController>(
					navigation,
					chat,
					std::move(callback));
			} else if (auto group = peer->asMegagroup()) {
				return std::make_unique<Dialogs::ChannelSearchFromController>(
					navigation,
					group,
					std::move(callback));
			}
		}
		return nullptr;
	};
	if (auto controller = createController()) {
		auto subscription = std::make_shared<base::Subscription>();
		auto box = Ui::show(Box<PeerListBox>(std::move(controller), [subscription](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_cancel), [box, subscription] { box->closeBox(); });
		}), LayerOption::KeepOther);
		*subscription = box->boxClosing.add_subscription(std::move(closedCallback));
	}
}

ChatSearchFromController::ChatSearchFromController(
	not_null<Window::Navigation*> navigation,
	not_null<ChatData*> chat,
	base::lambda<void(not_null<UserData*>)> callback)
: PeerListController()
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

void ChatSearchFromController::rowClicked(not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());
	_callback(row->peer()->asUser());
}

void ChatSearchFromController::rebuildRows() {
	auto ms = getms();
	auto wasEmpty = !delegate()->peerListFullRowsCount();

	auto now = unixtime();
	const auto byOnline = [&](not_null<UserData*> user) {
		return Data::SortByOnlineValue(user, now);
	};
	auto ordered = QMultiMap<TimeId, not_null<UserData*>>();
	if (_chat->noParticipantInfo()) {
		Auth().api().requestFullPeer(_chat);
	} else if (!_chat->participants.empty()) {
		for (const auto [user, version] : _chat->participants) {
			ordered.insertMulti(byOnline(user), user);
		}
	}
	for_const (auto user, _chat->lastAuthors) {
		if (user->isInaccessible()) continue;
		appendRow(user);
		if (!ordered.isEmpty()) {
			ordered.remove(byOnline(user), user);
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

void ChatSearchFromController::appendRow(not_null<UserData*> user) {
	if (!delegate()->peerListFindRow(user->id)) {
		delegate()->peerListAppendRow(std::make_unique<PeerListRow>(user));
	}
}

ChannelSearchFromController::ChannelSearchFromController(
	not_null<Window::Navigation*> navigation,
	not_null<ChannelData*> channel,
	base::lambda<void(not_null<UserData*>)> callback)
: ParticipantsBoxController(
	navigation,
	channel,
	ParticipantsBoxController::Role::Members)
, _callback(std::move(callback)) {
}

void ChannelSearchFromController::prepare() {
	ParticipantsBoxController::prepare();
	delegate()->peerListSetTitle(langFactory(lng_search_messages_from));
}

void ChannelSearchFromController::rowClicked(not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());
	_callback(row->peer()->asUser());
}

std::unique_ptr<PeerListRow> ChannelSearchFromController::createRow(not_null<UserData*> user) const {
	return std::make_unique<PeerListRow>(user);
}

} // namespace Dialogs
