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

#include <rpl/variable.h>
#include "base/weak_unique_ptr.h"
#include "profile/profile_channel_controllers.h"
#include "ui/widgets/popup_menu.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "boxes/confirm_box.h"
#include "window/window_controller.h"

namespace Info {
namespace Profile {
namespace {

constexpr auto kSortByOnlineDelay = TimeMs(1000);

class ChatMembersController
	: public PeerListController
	, private base::Subscriber
	, public base::enable_weak_from_this {
public:
	ChatMembersController(
		not_null<Window::Controller*> window,
		not_null<ChatData*> chat);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Ui::PopupMenu *rowContextMenu(
		not_null<PeerListRow*> row) override;

	rpl::producer<int> onlineCountValue() const override {
		return _onlineCount.value();
	}

private:
	void rebuildRows();
	void refreshOnlineCount();
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user);
	void sortByOnline();
	void sortByOnlineDelayed();
	void removeMember(not_null<UserData*> user);

	not_null<Window::Controller*> _window;
	not_null<ChatData*> _chat;

	base::Timer _sortByOnlineTimer;
	rpl::variable<int> _onlineCount = 0;

};

ChatMembersController::ChatMembersController(
	not_null<Window::Controller*> window,
	not_null<ChatData*> chat)
: PeerListController()
, _window(window)
, _chat(chat) {
	_sortByOnlineTimer.setCallback([this] { sortByOnline(); });
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
				if (auto row = delegate()->peerListFindRow(
					update.peer->id)) {
					row->refreshStatus();
					sortByOnlineDelayed();
				}
			}
		}));
}

void ChatMembersController::sortByOnlineDelayed() {
	if (!_sortByOnlineTimer.isActive()) {
		_sortByOnlineTimer.callOnce(kSortByOnlineDelay);
	}
}

void ChatMembersController::sortByOnline() {
	auto now = unixtime();
	delegate()->peerListSortRows([now](
			const PeerListRow &a,
			const PeerListRow &b) {
		return App::onlineForSort(a.peer()->asUser(), now) >
			App::onlineForSort(b.peer()->asUser(), now);
	});
	refreshOnlineCount();
}

void ChatMembersController::rebuildRows() {
	if (_chat->participants.empty()) {
		while (delegate()->peerListFullRowsCount() > 0) {
			delegate()->peerListRemoveRow(
				delegate()->peerListRowAt(0));
		}
		return;
	}

	auto &participants = _chat->participants;
	for (auto i = 0, count = delegate()->peerListFullRowsCount();
			i != count;) {
		auto row = delegate()->peerListRowAt(i);
		auto user = row->peer()->asUser();
		if (participants.contains(user)) {
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--count;
		}
	}
	for (auto i = participants.cbegin(), e = participants.cend();
		i != e;
		++i) {
		if (auto row = createRow(i.key())) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	sortByOnline();

	delegate()->peerListRefreshRows();
}

void ChatMembersController::refreshOnlineCount() {
	auto now = unixtime();
	auto left = 0, right = delegate()->peerListFullRowsCount();
	while (right > left) {
		auto middle = (left + right) / 2;
		auto row = delegate()->peerListRowAt(middle);
		if (App::onlineColorUse(row->peer()->asUser(), now)) {
			left = middle + 1;
		} else {
			right = middle;
		}
	}
	_onlineCount = left;
}

std::unique_ptr<PeerListRow> ChatMembersController::createRow(not_null<UserData*> user) {
	return std::make_unique<PeerListRow>(user);
}

void ChatMembersController::rowClicked(not_null<PeerListRow*> row) {
	_window->showPeerInfo(row->peer());
}

Ui::PopupMenu *ChatMembersController::rowContextMenu(
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	auto user = row->peer()->asUser();
	auto isCreator = (peerFromUser(_chat->creator) == user->id);
	auto isAdmin = _chat->adminsEnabled() && _chat->admins.contains(user);
	auto canRemoveMember = (user->id == Auth().userPeerId())
		? false
		: _chat->amCreator()
		? true
		: (_chat->amAdmin() && !isCreator && !isAdmin)
		? true
		: (_chat->invitedByMe.contains(user) && !isCreator && !isAdmin)
		? true
		: false;
	
	auto result = new Ui::PopupMenu(nullptr);
	result->addAction(
		lang(lng_context_view_profile),
		[weak = base::make_weak_unique(this), user] {
			if (weak) {
				weak->_window->showPeerInfo(user);
			}
		});
	if (canRemoveMember) {
		result->addAction(
			lang(lng_context_remove_from_group),
			[weak = base::make_weak_unique(this), user] {
				if (weak) {
					weak->removeMember(user);
				}
			});
	}

	return result;
}

void ChatMembersController::removeMember(not_null<UserData*> user) {
	auto text = lng_profile_sure_kick(lt_user, user->firstName);
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), [user, chat = _chat] {
		Ui::hideLayer();
		if (App::main()) App::main()->kickParticipant(chat, user);
	}));
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
