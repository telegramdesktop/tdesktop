/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "profile/profile_block_group_members.h"

#include "styles/style_profile.h"
#include "ui/widgets/labels.h"
#include "boxes/confirm_box.h"
#include "boxes/peers/edit_participant_box.h"
#include "profile/profile_channel_controllers.h"
#include "ui/widgets/popup_menu.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "lang/lang_keys.h"

namespace Profile {
namespace {

using UpdateFlag = Notify::PeerUpdate::Flag;

} // namespace

GroupMembersWidget::Member::Member(UserData *user) : Item(user) {
}

UserData *GroupMembersWidget::Member::user() const {
	return static_cast<UserData*>(peer);
}

GroupMembersWidget::GroupMembersWidget(
	QWidget *parent,
	PeerData *peer,
	const style::PeerListItem &st)
: PeerListWidget(parent, peer, QString(), st, lang(lng_profile_kick)) {
	_updateOnlineTimer.setSingleShot(true);
	connect(&_updateOnlineTimer, SIGNAL(timeout()), this, SLOT(onUpdateOnlineDisplay()));

	auto observeEvents = UpdateFlag::AdminsChanged
		| UpdateFlag::MembersChanged
		| UpdateFlag::UserOnlineChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	setRemovedCallback([=](PeerData *selectedPeer) {
		removePeer(selectedPeer);
	});
	setSelectedCallback([=](PeerData *selectedPeer) {
		Ui::showPeerProfile(selectedPeer);
	});
	setUpdateItemCallback([=](Item *item) {
		updateItemStatusText(item);
	});
	setPreloadMoreCallback([=] {
		preloadMore();
	});

	refreshMembers();
}

void GroupMembersWidget::editAdmin(not_null<UserData*> user) {
	auto megagroup = peer()->asMegagroup();
	if (!megagroup) {
		return; // not supported
	}
	auto currentRightsIt = megagroup->mgInfo->lastAdmins.find(user);
	auto hasAdminRights = (currentRightsIt != megagroup->mgInfo->lastAdmins.cend());
	auto currentRights = hasAdminRights ? currentRightsIt->second.rights : MTP_chatAdminRights(MTP_flags(0));
	auto weak = std::make_shared<QPointer<EditAdminBox>>(nullptr);
	auto box = Box<EditAdminBox>(megagroup, user, currentRights);
	box->setSaveCallback(SaveAdminCallback(megagroup, user, [=](
			const MTPChatAdminRights &newRights) {
		if (*weak) {
			(*weak)->closeBox();
		}
	}, [=] {
		if (*weak) {
			(*weak)->closeBox();
		}
	}));
	*weak = Ui::show(std::move(box));
}

void GroupMembersWidget::restrictUser(not_null<UserData*> user) {
	auto megagroup = peer()->asMegagroup();
	if (!megagroup) {
		return; // not supported
	}
	auto currentRightsIt = megagroup->mgInfo->lastRestricted.find(user);
	auto currentRights = (currentRightsIt != megagroup->mgInfo->lastRestricted.end())
		? currentRightsIt->second.rights
		: MTP_chatBannedRights(MTP_flags(0), MTP_int(0));
	auto hasAdminRights = megagroup->mgInfo->lastAdmins.find(user) != megagroup->mgInfo->lastAdmins.cend();
	auto box = Box<EditRestrictedBox>(megagroup, user, hasAdminRights, currentRights);
	box->setSaveCallback([megagroup, user](const MTPChatBannedRights &oldRights, const MTPChatBannedRights &newRights) {
		Ui::hideLayer();
		// #TODO use Auth().api().
		MTP::send(MTPchannels_EditBanned(megagroup->inputChannel, user->inputUser, newRights), rpcDone([megagroup, user, oldRights, newRights](const MTPUpdates &result) {
			Auth().api().applyUpdates(result);
			megagroup->applyEditBanned(user, oldRights, newRights);
		}));
	});
	Ui::show(std::move(box));
}

void GroupMembersWidget::removePeer(PeerData *selectedPeer) {
	auto user = selectedPeer->asUser();
	Assert(user != nullptr);

	auto text = lng_profile_sure_kick(lt_user, user->firstName);
	auto currentRestrictedRights = [&]() -> MTPChatBannedRights {
		if (auto channel = peer()->asMegagroup()) {
			auto it = channel->mgInfo->lastRestricted.find(user);
			if (it != channel->mgInfo->lastRestricted.cend()) {
				return it->second.rights;
			}
		}
		return MTP_chatBannedRights(MTP_flags(0), MTP_int(0));
	}();
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), [user, currentRestrictedRights, peer = peer()] {
		Ui::hideLayer();
		if (const auto chat = peer->asChat()) {
			Auth().api().kickParticipant(chat, user);
			Ui::showPeerHistory(chat->id, ShowAtTheEndMsgId);
		} else if (const auto channel = peer->asChannel()) {
			Auth().api().kickParticipant(
				channel,
				user,
				currentRestrictedRights);
		}
	}));
}

void GroupMembersWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		if (update.flags & UpdateFlag::UserOnlineChanged) {
			if (auto user = update.peer->asUser()) {
				refreshUserOnline(user);
			}
		}
		return;
	}

	if (update.flags & UpdateFlag::MembersChanged) {
		refreshMembers();
		contentSizeUpdated();
	} else if (update.flags & UpdateFlag::AdminsChanged) {
		if (auto chat = peer()->asChat()) {
			for_const (auto item, items()) {
				setItemFlags(getMember(item), chat);
			}
		} else if (auto megagroup = peer()->asMegagroup()) {
			for_const (auto item, items()) {
				setItemFlags(getMember(item), megagroup);
			}
		}
	}
	this->update();
}

void GroupMembersWidget::refreshUserOnline(UserData *user) {
	auto it = _membersByUser.find(user);
	if (it == _membersByUser.cend()) return;

	_now = unixtime();

	auto member = getMember(it.value());
	member->statusHasOnlineColor = !user->botInfo && Data::OnlineTextActive(user->onlineTill, _now);
	member->onlineTill = user->onlineTill;
	member->onlineForSort = user->isSelf()
		? std::numeric_limits<TimeId>::max()
		: Data::SortByOnlineValue(user, _now);
	member->statusText = QString();

	sortMembers();
	update();
}

void GroupMembersWidget::preloadMore() {
	//
	// This can cause a ddos, because lastParticipants may never reach members count.
	//
	//if (auto megagroup = peer()->asMegagroup()) {
	//	auto &megagroupInfo = megagroup->mgInfo;
	//	if (!megagroupInfo->lastParticipants.isEmpty() && megagroupInfo->lastParticipants.size() < megagroup->membersCount()) {
	//		Auth().api().requestLastParticipants(megagroup, false);
	//	}
	//}
}

void GroupMembersWidget::updateItemStatusText(Item *item) {
	auto member = getMember(item);
	auto user = member->user();
	if (member->statusText.isEmpty() || (member->onlineTextTill <= _now)) {
		if (user->botInfo) {
			auto seesAllMessages = (user->botInfo->readsAllHistory || (member->adminState != Item::AdminState::None));
			member->statusText = lang(seesAllMessages ? lng_status_bot_reads_all : lng_status_bot_not_reads_all);
			member->onlineTextTill = _now + 86400;
		} else {
			member->statusHasOnlineColor = Data::OnlineTextActive(member->onlineTill, _now);
			member->statusText = Data::OnlineText(member->onlineTill, _now);
			const auto changeInMs = Data::OnlineChangeTimeout(
				member->onlineTill,
				_now);
			member->onlineTextTill = _now + TimeId(changeInMs / 1000);
		}
	}
	if (_updateOnlineAt <= _now || _updateOnlineAt > member->onlineTextTill) {
		_updateOnlineAt = member->onlineTextTill;
		_updateOnlineTimer.start((_updateOnlineAt - _now + 1) * 1000);
	}
}

void GroupMembersWidget::refreshMembers() {
	_now = unixtime();
	if (auto chat = peer()->asChat()) {
		checkSelfAdmin(chat);
		if (chat->noParticipantInfo()) {
			Auth().api().requestFullPeer(chat);
		}
		fillChatMembers(chat);
	} else if (auto megagroup = peer()->asMegagroup()) {
		auto &megagroupInfo = megagroup->mgInfo;
		if (megagroupInfo->lastParticipants.empty() || megagroup->lastParticipantsCountOutdated()) {
			Auth().api().requestLastParticipants(megagroup);
		}
		fillMegagroupMembers(megagroup);
	}
	sortMembers();

	refreshVisibility();
}

void GroupMembersWidget::checkSelfAdmin(ChatData *chat) {
	if (chat->participants.empty()) return;

	// #TODO groups
	const auto self = Auth().user();
	//if (chat->amAdmin() && !chat->admins.contains(self)) {
	//	chat->admins.insert(self);
	//} else if (!chat->amAdmin() && chat->admins.contains(self)) {
	//	chat->admins.remove(self);
	//}
}

void GroupMembersWidget::sortMembers() {
	if (!_sortByOnline || !itemsCount()) return;

	sortItems([this](Item *a, Item *b) {
		return getMember(a)->onlineForSort > getMember(b)->onlineForSort;
	});

	updateOnlineCount();
}

void GroupMembersWidget::updateOnlineCount() {
	bool onlyMe = true;
	int newOnlineCount = 0;
	for_const (auto item, items()) {
		auto member = getMember(item);
		auto user = member->user();
		auto isOnline = !user->botInfo && Data::OnlineTextActive(member->onlineTill, _now);
		if (member->statusHasOnlineColor != isOnline) {
			member->statusHasOnlineColor = isOnline;
			member->statusText = QString();
		}
		if (member->statusHasOnlineColor) {
			++newOnlineCount;
			if (!user->isSelf()) {
				onlyMe = false;
			}
		}
	}
	if (newOnlineCount == 1 && onlyMe) {
		newOnlineCount = 0;
	}
	if (_onlineCount != newOnlineCount) {
		_onlineCount = newOnlineCount;
		emit onlineCountUpdated(_onlineCount);
	}
}

GroupMembersWidget::Member *GroupMembersWidget::addUser(ChatData *chat, UserData *user) {
	auto member = computeMember(user);
	setItemFlags(member, chat);
	addItem(member);
	return member;
}

void GroupMembersWidget::fillChatMembers(ChatData *chat) {
	if (chat->participants.empty()) return;

	clearItems();
	if (!chat->amIn()) return;

	_sortByOnline = true;

	reserveItemsForSize(chat->participants.size());
	addUser(chat, Auth().user())->onlineForSort
		= std::numeric_limits<TimeId>::max();
	for (const auto &[user, v] : chat->participants) {
		if (!user->isSelf()) {
			addUser(chat, user);
		}
	}
}

void GroupMembersWidget::setItemFlags(Item *item, ChatData *chat) {
	using AdminState = Item::AdminState;
	auto user = getMember(item)->user();
	auto isCreator = (peerFromUser(chat->creator) == item->peer->id);
	// #TODO groups
	auto isAdmin = false;// chat->adminsEnabled() && chat->admins.contains(user);
	auto adminState = isCreator ? AdminState::Creator : isAdmin ? AdminState::Admin : AdminState::None;
	item->adminState = adminState;
	if (item->peer->id == Auth().userPeerId()) {
		item->hasRemoveLink = false;
	} else if (chat->amCreator() /*|| (chat->amAdmin() && (adminState == AdminState::None))*/) {
		item->hasRemoveLink = true;
	} else if (chat->invitedByMe.contains(user) && (adminState == AdminState::None)) {
		item->hasRemoveLink = true;
	} else {
		item->hasRemoveLink = false;
	}
}

GroupMembersWidget::Member *GroupMembersWidget::addUser(ChannelData *megagroup, UserData *user) {
	auto member = computeMember(user);
	setItemFlags(member, megagroup);
	addItem(member);
	return member;
}

void GroupMembersWidget::fillMegagroupMembers(ChannelData *megagroup) {
	Assert(megagroup->mgInfo != nullptr);
	if (megagroup->mgInfo->lastParticipants.empty()) return;

	if (!megagroup->canViewMembers()) {
		clearItems();
		return;
	}

	_sortByOnline = (megagroup->membersCount() > 0 && megagroup->membersCount() <= Global::ChatSizeMax());

	auto &membersList = megagroup->mgInfo->lastParticipants;
	if (_sortByOnline) {
		clearItems();
		reserveItemsForSize(membersList.size());
		if (megagroup->amIn()) {
			addUser(megagroup, Auth().user())->onlineForSort
				= std::numeric_limits<TimeId>::max();
		}
	} else if (membersList.size() >= itemsCount()) {
		if (addUsersToEnd(megagroup)) {
			return;
		}
	}
	if (!_sortByOnline) {
		clearItems();
		reserveItemsForSize(membersList.size());
	}
	for_const (auto user, membersList) {
		if (!_sortByOnline || !user->isSelf()) {
			addUser(megagroup, user);
		}
	}
}

bool GroupMembersWidget::addUsersToEnd(ChannelData *megagroup) {
	auto &membersList = megagroup->mgInfo->lastParticipants;
	auto &itemsList = items();
	for (int i = 0, count = itemsList.size(); i < count; ++i) {
		if (itemsList[i]->peer != membersList.at(i)) {
			return false;
		}
	}
	reserveItemsForSize(membersList.size());
	for (int i = itemsCount(), count = membersList.size(); i < count; ++i) {
		addUser(megagroup, membersList.at(i));
	}
	return true;
}

void GroupMembersWidget::setItemFlags(Item *item, ChannelData *megagroup) {
	using AdminState = Item::AdminState;
	auto amCreator = item->peer->isSelf() && megagroup->amCreator();
	auto amAdmin = item->peer->isSelf() && megagroup->hasAdminRights();
	auto adminIt = megagroup->mgInfo->lastAdmins.find(getMember(item)->user());
	auto isAdmin = (adminIt != megagroup->mgInfo->lastAdmins.cend());
	auto isCreator = megagroup->mgInfo->creator == item->peer;
	auto adminCanEdit = isAdmin && adminIt->second.canEdit;
	auto adminState = (amCreator || isCreator) ? AdminState::Creator : (amAdmin || isAdmin) ? AdminState::Admin : AdminState::None;
	if (item->adminState != adminState) {
		item->adminState = adminState;
		auto user = item->peer->asUser();
		Assert(user != nullptr);
		if (user->botInfo) {
			// Update "has access to messages" status.
			item->statusText = QString();
			updateItemStatusText(item);
		}
	}
	if (item->peer->isSelf()) {
		item->hasRemoveLink = false;
	} else if (megagroup->amCreator() || (megagroup->canBanMembers() && ((adminState == AdminState::None) || adminCanEdit))) {
		item->hasRemoveLink = true;
	} else {
		item->hasRemoveLink = false;
	}
}

GroupMembersWidget::Member *GroupMembersWidget::computeMember(UserData *user) {
	auto it = _membersByUser.constFind(user);
	if (it == _membersByUser.cend()) {
		auto member = new Member(user);
		it = _membersByUser.insert(user, member);
		member->statusHasOnlineColor = !user->botInfo
			&& Data::OnlineTextActive(user->onlineTill, _now);
		member->onlineTill = user->onlineTill;
		member->onlineForSort = Data::SortByOnlineValue(user, _now);
	}
	return it.value();
}

void GroupMembersWidget::onUpdateOnlineDisplay() {
	if (_sortByOnline) {
		_now = unixtime();

		bool changed = false;
		for_const (auto item, items()) {
			if (!item->statusHasOnlineColor) {
				if (!item->peer->isSelf()) {
					continue;
				} else {
					break;
				}
			}
			auto member = getMember(item);
			bool isOnline = !member->user()->botInfo
				&& Data::OnlineTextActive(member->onlineTill, _now);
			if (!isOnline) {
				changed = true;
			}
		}
		if (changed) {
			updateOnlineCount();
		}
	}
	update();
}

GroupMembersWidget::~GroupMembersWidget() {
	auto members = base::take(_membersByUser);
	for_const (auto member, members) {
		delete member;
	}
}

} // namespace Profile
