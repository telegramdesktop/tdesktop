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
#include "base/weak_ptr.h"
#include "profile/profile_channel_controllers.h"
#include "ui/widgets/popup_menu.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "boxes/confirm_box.h"
#include "window/window_controller.h"
#include "styles/style_info.h"
#include "data/data_peer_values.h"

namespace Info {
namespace Profile {
namespace {

constexpr auto kSortByOnlineDelay = TimeMs(1000);

class ChatMembersController
	: public PeerListController
	, private base::Subscriber
	, public base::has_weak_ptr {
public:
	ChatMembersController(
		not_null<Window::Navigation*> navigation,
		not_null<ChatData*> chat);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	Ui::PopupMenu *rowContextMenu(
		not_null<PeerListRow*> row) override;

	rpl::producer<int> onlineCountValue() const override {
		return _onlineCount.value();
	}

	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	using Rights = MemberListRow::Rights;
	using Type = MemberListRow::Type;
	struct SavedState : SavedStateBase {
		rpl::lifetime lifetime;
	};
	void rebuildRows();
	void rebuildRowTypes();
	void refreshOnlineCount();
	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user);
	void sortByOnline();
	void sortByOnlineDelayed();
	void removeMember(not_null<UserData*> user);
	Type computeType(not_null<UserData*> user);

	not_null<Window::Navigation*> _navigation;
	not_null<ChatData*> _chat;

	base::Timer _sortByOnlineTimer;
	rpl::variable<int> _onlineCount = 0;

};

ChatMembersController::ChatMembersController(
	not_null<Window::Navigation*> navigation,
	not_null<ChatData*> chat)
: PeerListController()
, _navigation(navigation)
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
		UpdateFlag::MembersChanged
		| UpdateFlag::UserOnlineChanged
		| UpdateFlag::AdminsChanged,
		[this](const Notify::PeerUpdate &update) {
			if (update.flags & UpdateFlag::MembersChanged) {
				if (update.peer == _chat) {
					rebuildRows();
				}
			} else if (update.flags & UpdateFlag::AdminsChanged) {
				if (update.peer == _chat) {
					rebuildRowTypes();
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
		return Data::SortByOnlineValue(a.peer()->asUser(), now) >
			Data::SortByOnlineValue(b.peer()->asUser(), now);
	});
	refreshOnlineCount();
}

std::unique_ptr<PeerListState> ChatMembersController::saveState() const {
	auto result = PeerListController::saveState();
	auto my = std::make_unique<SavedState>();
	using Flag = Notify::PeerUpdate::Flag;
	Notify::PeerUpdateViewer(_chat, Flag::MembersChanged)
		| rpl::start_with_next([state = result.get()](auto update) {
			state->controllerState = nullptr;
		}, my->lifetime);
	result->controllerState = std::move(my);
	return result;
}

void ChatMembersController::restoreState(
		std::unique_ptr<PeerListState> state) {
	PeerListController::restoreState(std::move(state));
	sortByOnline();
}

void ChatMembersController::rebuildRows() {
	if (_chat->participants.empty()) {
		// We get such updates often
		// (when participants list was invalidated).
		//while (delegate()->peerListFullRowsCount() > 0) {
		//	delegate()->peerListRemoveRow(
		//		delegate()->peerListRowAt(0));
		//}
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
	for (const auto [user, v] : participants) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	sortByOnline();

	delegate()->peerListRefreshRows();
}

void ChatMembersController::rebuildRowTypes() {
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		auto row = static_cast<MemberListRow*>(
			delegate()->peerListRowAt(i).get());
		row->setType(computeType(row->user()));
	}
	delegate()->peerListRefreshRows();
}

void ChatMembersController::refreshOnlineCount() {
	auto now = unixtime();
	auto left = 0, right = delegate()->peerListFullRowsCount();
	while (right > left) {
		auto middle = (left + right) / 2;
		auto row = delegate()->peerListRowAt(middle);
		if (Data::OnlineTextActive(row->peer()->asUser(), now)) {
			left = middle + 1;
		} else {
			right = middle;
		}
	}
	_onlineCount = left;
}

std::unique_ptr<PeerListRow> ChatMembersController::createRestoredRow(
		not_null<PeerData*> peer) {
	if (auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

std::unique_ptr<PeerListRow> ChatMembersController::createRow(
		not_null<UserData*> user) {
	return std::make_unique<MemberListRow>(user, computeType(user));
}

auto ChatMembersController::computeType(
		not_null<UserData*> user) -> Type {
	auto isCreator = (peerFromUser(_chat->creator) == user->id);
	auto isAdmin = _chat->adminsEnabled()
		&& _chat->admins.contains(user);
	auto canRemove = [&] {
		if (user->isSelf()) {
			return false;
		} else if (_chat->amCreator()) {
			return true;
		} else if (isAdmin || isCreator) {
			return false;
		} else if (_chat->amAdmin()) {
			return true;
		} else if (_chat->invitedByMe.contains(user)) {
			return true;
		}
		return false;
	}();

	auto result = Type();
	result.rights = isCreator
		? Rights::Creator
		: isAdmin
		? Rights::Admin
		: Rights::Normal;
	result.canRemove = canRemove;
	return result;
}

void ChatMembersController::rowClicked(not_null<PeerListRow*> row) {
	_navigation->showPeerInfo(row->peer());
}

void ChatMembersController::rowActionClicked(
		not_null<PeerListRow*> row) {
	removeMember(row->peer()->asUser());
}

Ui::PopupMenu *ChatMembersController::rowContextMenu(
		not_null<PeerListRow*> row) {
	auto my = static_cast<MemberListRow*>(row.get());
	auto user = my->user();
	auto canRemoveMember = my->canRemove();

	auto result = new Ui::PopupMenu(nullptr);
	result->addAction(
		lang(lng_context_view_profile),
		[weak = base::make_weak(this), user] {
			if (weak) {
				weak->_navigation->showPeerInfo(user);
			}
		});
	if (canRemoveMember) {
		result->addAction(
			lang(lng_context_remove_from_group),
			[weak = base::make_weak(this), user] {
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

MemberListRow::MemberListRow(
	not_null<UserData*> user,
	Type type)
: PeerListRow(user)
, _type(type) {
}

void MemberListRow::setType(Type type) {
	_type = type;
}

QSize MemberListRow::actionSize() const {
	return canRemove()
		? QRect(
			QPoint(),
			st::infoMembersRemoveIcon.size()).marginsAdded(
				st::infoMembersRemoveIconMargins).size()
		: QSize();
}

void MemberListRow::paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (_type.canRemove && selected) {
		x += st::infoMembersRemoveIconMargins.left();
		y += st::infoMembersRemoveIconMargins.top();
		(actionSelected
			? st::infoMembersRemoveIconOver
			: st::infoMembersRemoveIcon).paint(p, x, y, outerWidth);
	}
}

int MemberListRow::nameIconWidth() const {
	return (_type.rights == Rights::Admin)
		? st::infoMembersAdminIcon.width()
		: (_type.rights == Rights::Creator)
		? st::infoMembersCreatorIcon.width()
		: 0;
}

void MemberListRow::paintNameIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected) {
	auto icon = [&] {
		return (_type.rights == Rights::Admin)
			? (selected
				? &st::infoMembersAdminIconOver
				: &st::infoMembersAdminIcon)
			: (selected
				? &st::infoMembersCreatorIconOver
				: &st::infoMembersCreatorIcon);
	}();
	icon->paint(p, x, y, outerWidth);
}

std::unique_ptr<PeerListController> CreateMembersController(
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer) {
	if (auto chat = peer->asChat()) {
		return std::make_unique<ChatMembersController>(
			navigation,
			chat);
	} else if (auto channel = peer->asChannel()) {
		using ChannelMembersController
			= ::Profile::ParticipantsBoxController;
		return std::make_unique<ChannelMembersController>(
			navigation,
			channel,
			ChannelMembersController::Role::Profile);
	}
	Unexpected("Peer type in CreateMembersController()");
}

} // namespace Profile
} // namespace Info
