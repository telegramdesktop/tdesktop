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
#include "info/profile/info_profile_members_controllers.h"

#include "profile/profile_channel_controllers.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "observer_peer.h"
#include "window/window_controller.h"

namespace Info {
namespace Profile {
namespace {

class ChatMembersController
	: public PeerListController
	, private base::Subscriber {
public:
	ChatMembersController(
		not_null<Window::Controller*> window,
		not_null<ChatData*> chat);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	void rebuildRows();
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user);

	not_null<Window::Controller*> _window;
	not_null<ChatData*> _chat;

};

ChatMembersController::ChatMembersController(
	not_null<Window::Controller*> window,
	not_null<ChatData*> chat)
: PeerListController()
, _window(window)
, _chat(chat) {
}

void ChatMembersController::prepare() {
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_channel_admins));

	rebuildRows();
	if (!delegate()->peerListFullRowsCount()) {
		Auth().api().requestFullPeer(_chat);
	}
	using UpdateFlag = Notify::PeerUpdate::Flag;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(
		UpdateFlag::MembersChanged | UpdateFlag::UserOnlineChanged,
		[this](const Notify::PeerUpdate &update) {
			if (update.flags & UpdateFlag::MembersChanged) {
				if (update.peer == _chat) {
					rebuildRows();
				}
			} else if (update.flags & UpdateFlag::UserOnlineChanged) {
				auto now = unixtime();
				delegate()->peerListSortRows([now](const PeerListRow &a, const PeerListRow &b) {
					return App::onlineForSort(a.peer()->asUser(), now) >
						App::onlineForSort(b.peer()->asUser(), now);
				});
			}
		}));
}

void ChatMembersController::rebuildRows() {
	if (_chat->participants.empty()) {
		return;
	}

	std::vector<not_null<UserData*>> users;
	auto &participants = _chat->participants;
	for (auto i = participants.cbegin(), e = participants.cend();
			i != e;
			++i) {
		users.push_back(i.key());
	}
	auto now = unixtime();
	base::sort(users, [now](auto a, auto b) {
		return App::onlineForSort(a, now)
			> App::onlineForSort(b, now);
	});
	base::for_each(users, [this](not_null<UserData*> user) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	});

	delegate()->peerListRefreshRows();
}

std::unique_ptr<PeerListRow> ChatMembersController::createRow(not_null<UserData*> user) {
	return std::make_unique<PeerListRow>(user);
}

void ChatMembersController::rowClicked(not_null<PeerListRow*> row) {
	_window->showPeerInfo(row->peer());
}

} // namespace

std::unique_ptr<PeerListController> CreateMembersController(
		not_null<Window::Controller*> window,
		not_null<PeerData*> peer) {
	if (auto chat = peer->asChat()) {
		return std::make_unique<ChatMembersController>(
			window,
			chat);
	} else if (auto channel = peer->asChannel()) {
		using ChannelMembersController
			= ::Profile::ParticipantsBoxController;
		return std::make_unique<ChannelMembersController>(
			window,
			channel,
			ChannelMembersController::Role::Profile);
	}
	Unexpected("Peer type in CreateMembersController()");
}

} // namespace Profile
} // namespace Info
