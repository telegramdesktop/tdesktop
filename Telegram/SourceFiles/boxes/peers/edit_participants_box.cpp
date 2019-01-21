/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_participants_box.h"

#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "history/history.h"

namespace {

// How many messages from chat history server should forward to user,
// that was added to this chat.
constexpr auto kForwardMessagesOnAdd = 100;

constexpr auto kParticipantsFirstPageCount = 16;
constexpr auto kParticipantsPerPage = 200;
constexpr auto kSortByOnlineDelay = TimeMs(1000);

void RemoveAdmin(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChatAdminRights &oldRights,
		Fn<void()> onDone,
		Fn<void()> onFail) {
	const auto newRights = MTP_chatAdminRights(MTP_flags(0));
	channel->session().api().request(MTPchannels_EditAdmin(
		channel->inputChannel,
		user->inputUser,
		newRights
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		channel->applyEditAdmin(user, oldRights, newRights);
		if (onDone) {
			onDone();
		}
	}).fail([=](const RPCError &error) {
		if (onFail) {
			onFail();
		}
	}).send();
}

void AddChatParticipant(
		not_null<ChatData*> chat,
		not_null<UserData*> user,
		Fn<void()> onDone,
		Fn<void()> onFail) {
	chat->session().api().request(MTPmessages_AddChatUser(
		chat->inputChat,
		user->inputUser,
		MTP_int(kForwardMessagesOnAdd)
	)).done([=](const MTPUpdates &result) {
		chat->session().api().applyUpdates(result);
		if (onDone) {
			onDone();
		}
	}).fail([=](const RPCError &error) {
		ShowAddParticipantsError(error.type(), chat, { 1, user });
		if (onFail) {
			onFail();
		}
	}).send();
}

void SaveChatAdmin(
		not_null<ChatData*> chat,
		not_null<UserData*> user,
		bool isAdmin,
		Fn<void()> onDone,
		Fn<void()> onFail,
		bool retryOnNotParticipant = true) {
	chat->session().api().request(MTPmessages_EditChatAdmin(
		chat->inputChat,
		user->inputUser,
		MTP_bool(isAdmin)
	)).done([=](const MTPBool &result) {
		chat->applyEditAdmin(user, isAdmin);
		if (onDone) {
			onDone();
		}
	}).fail([=](const RPCError &error) {
		const auto &type = error.type();
		if (retryOnNotParticipant
			&& isAdmin
			&& (type == qstr("USER_NOT_PARTICIPANT"))) {
			AddChatParticipant(chat, user, [=] {
				SaveChatAdmin(chat, user, isAdmin, onDone, onFail, false);
			}, onFail);
		} else if (onFail) {
			onFail();
		}
	}).send();
}

void SaveChannelAdmin(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChatAdminRights &oldRights,
		const MTPChatAdminRights &newRights,
		Fn<void()> onDone,
		Fn<void()> onFail) {
	channel->session().api().request(MTPchannels_EditAdmin(
		channel->inputChannel,
		user->inputUser,
		newRights
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		channel->applyEditAdmin(user, oldRights, newRights);
		if (onDone) {
			onDone();
		}
	}).fail([=](const RPCError &error) {
		if (error.type() == qstr("USER_NOT_MUTUAL_CONTACT")) {
			Ui::show(
				Box<InformBox>(PeerFloodErrorText(
					channel->isMegagroup()
					? PeerFloodType::InviteGroup
					: PeerFloodType::InviteChannel)),
				LayerOption::KeepOther);
		} else if (error.type() == qstr("BOT_GROUPS_BLOCKED")) {
			Ui::show(
				Box<InformBox>(lang(lng_error_cant_add_bot)),
				LayerOption::KeepOther);
		} else if (error.type() == qstr("ADMINS_TOO_MUCH")) {
			Ui::show(
				Box<InformBox>(lang(channel->isMegagroup()
					? lng_error_admin_limit
					: lng_error_admin_limit_channel)),
				LayerOption::KeepOther);
		}
		if (onFail) {
			onFail();
		}
	}).send();
}

void SaveChannelRestriction(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChatBannedRights &oldRights,
		const MTPChatBannedRights &newRights,
		Fn<void()> onDone,
		Fn<void()> onFail) {
	channel->session().api().request(MTPchannels_EditBanned(
		channel->inputChannel,
		user->inputUser,
		newRights
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		channel->applyEditBanned(user, oldRights, newRights);
		if (onDone) {
			onDone();
		}
	}).fail([=](const RPCError &error) {
		if (onFail) {
			onFail();
		}
	}).send();
}

} // namespace

Fn<void(
	const MTPChatAdminRights &oldRights,
	const MTPChatAdminRights &newRights)> SaveAdminCallback(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		Fn<void(const MTPChatAdminRights &newRights)> onDone,
		Fn<void()> onFail) {
	return [=](
			const MTPChatAdminRights &oldRights,
			const MTPChatAdminRights &newRights) {
		const auto done = [=] { if (onDone) onDone(newRights); };
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			SaveChannelAdmin(
				channel,
				user,
				oldRights,
				newRights,
				done,
				onFail);
		};
		if (const auto chat = peer->asChatNotMigrated()) {
			const auto saveChatAdmin = [&](bool isAdmin) {
				SaveChatAdmin(chat, user, isAdmin, done, onFail);
			};
			const auto flags = newRights.match([](
					const MTPDchatAdminRights &data) {
				return data.vflags.v;
			});
			if (flags == ChatData::DefaultAdminRights()) {
				saveChatAdmin(true);
			} else if (!flags) {
				saveChatAdmin(false);
			} else {
				peer->session().api().migrateChat(chat, saveForChannel);
			}
		} else if (const auto channel = peer->asChannelOrMigrated()) {
			saveForChannel(channel);
		} else {
			Unexpected("Peer in SaveAdminCallback.");
		}
	};
}

Fn<void(
	const MTPChatBannedRights &oldRights,
	const MTPChatBannedRights &newRights)> SaveRestrictedCallback(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		Fn<void(const MTPChatBannedRights &newRights)> onDone,
		Fn<void()> onFail) {
	return [=](
			const MTPChatBannedRights &oldRights,
			const MTPChatBannedRights &newRights) {
		const auto done = [=] { if (onDone) onDone(newRights); };
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			SaveChannelRestriction(
				channel,
				user,
				oldRights,
				newRights,
				done,
				onFail);
		};
		if (const auto chat = peer->asChatNotMigrated()) {
			const auto flags = newRights.match([](
					const MTPDchatBannedRights &data) {
				return data.vflags.v;
			});
			if (!flags) {
				done();
			} else {
				peer->session().api().migrateChat(chat, saveForChannel);
			}
		} else if (const auto channel = peer->asChannelOrMigrated()) {
			saveForChannel(channel);
		} else {
			Unexpected("Peer in SaveAdminCallback.");
		}
	};
}

void SubscribeToMigration(
		not_null<PeerData*> peer,
		rpl::lifetime &lifetime,
		Fn<void(not_null<ChannelData*>)> migrate) {
	if (const auto chat = peer->asChat()) {
		if (const auto channel = peer->migrateTo()) {
			migrate(channel);
		} else if (!chat->isDeactivated()) {
			const auto alive = lifetime.make_state<base::Subscription>();
			const auto handler = [=](const Notify::PeerUpdate &update) {
				if (update.peer == peer) {
					if (const auto channel = peer->migrateTo()) {
						const auto onstack = base::duplicate(migrate);
						*alive = base::Subscription();
						onstack(channel);
					}
				}
			};
			*alive = Notify::PeerUpdated().add_subscription(
				Notify::PeerUpdatedHandler(
					Notify::PeerUpdate::Flag::MigrationChanged,
					handler));
		}
	}
}

ParticipantsAdditionalData::ParticipantsAdditionalData(
	not_null<PeerData*> peer,
	Role role)
: _peer(peer)
, _role(role) {
	fillFromPeer();
}

bool ParticipantsAdditionalData::infoLoaded(
		not_null<UserData*> user) const {
	return _peer->isChat()
		|| (_infoNotLoaded.find(user) == end(_infoNotLoaded));
}

bool ParticipantsAdditionalData::canEditAdmin(
		not_null<UserData*> user) const {
	if (_creator == user || user->isSelf()) {
		return false;
	} else if (_creator && _creator->isSelf()) {
		return true;
	} else if (adminRights(user).has_value()) {
		return !_peer->isChat() && _adminCanEdit.contains(user);
	}
	return true;
}

bool ParticipantsAdditionalData::canAddOrEditAdmin(
		not_null<UserData*> user) const {
	if (!canEditAdmin(user)) {
		return false;
	} else if (const auto chat = _peer->asChat()) {
		return chat->canAddAdmins();
	} else if (const auto channel = _peer->asChannel()) {
		return channel->canAddAdmins();
	}
	Unexpected("Peer in ParticipantsAdditionalData::canAddOrEditAdmin.");
}

bool ParticipantsAdditionalData::canRestrictUser(
		not_null<UserData*> user) const {
	if (!canEditAdmin(user)) {
		return false;
	} else if (const auto chat = _peer->asChat()) {
		return chat->canBanMembers();
	} else if (const auto channel = _peer->asChannel()) {
		return channel->canBanMembers();
	}
	Unexpected("Peer in ParticipantsAdditionalData::canRestrictUser.");
}

auto ParticipantsAdditionalData::adminRights(
	not_null<UserData*> user) const
-> std::optional<MTPChatAdminRights> {
	if (const auto chat = _peer->asChat()) {
		return _admins.contains(user)
			? std::make_optional(MTPChatAdminRights(MTP_chatAdminRights(
				MTP_flags(ChatData::DefaultAdminRights()))))
			: std::nullopt;
	}
	const auto i = _adminRights.find(user);
	return (i != end(_adminRights))
		? std::make_optional(i->second)
		: std::nullopt;
}

auto ParticipantsAdditionalData::restrictedRights(
	not_null<UserData*> user) const
-> std::optional<MTPChatBannedRights> {
	if (_peer->isChat()) {
		return std::nullopt;
	}
	const auto i = _restrictedRights.find(user);
	return (i != end(_restrictedRights))
		? std::make_optional(i->second)
		: std::nullopt;
}

bool ParticipantsAdditionalData::isCreator(not_null<UserData*> user) const {
	return (_creator == user);
}

bool ParticipantsAdditionalData::isExternal(
		not_null<UserData*> user) const {
	return _peer->isChat()
		? !_members.contains(user)
		: _external.find(user) != end(_external);
}

bool ParticipantsAdditionalData::isKicked(not_null<UserData*> user) const {
	return _peer->isChat()
		? false
		: _kicked.find(user) != end(_kicked);
}

UserData *ParticipantsAdditionalData::adminPromotedBy(
		not_null<UserData*> user) const {
	if (_peer->isChat()) {
		return _admins.contains(user) ? _creator : nullptr;
	}
	const auto i = _adminPromotedBy.find(user);
	return (i != end(_adminPromotedBy)) ? i->second.get() : nullptr;
}

UserData *ParticipantsAdditionalData::restrictedBy(
		not_null<UserData*> user) const {
	if (_peer->isChat()) {
		return nullptr;
	}
	const auto i = _restrictedBy.find(user);
	return (i != end(_restrictedBy)) ? i->second.get() : nullptr;
}

void ParticipantsAdditionalData::setExternal(not_null<UserData*> user) {
	_infoNotLoaded.erase(user);
	_external.emplace(user);
}

void ParticipantsAdditionalData::checkForLoaded(not_null<UserData*> user) {
	const auto contains = [](const auto &map, const auto &value) {
		return map.find(value) != map.end();
	};
	if (_creator != user
		&& !contains(_adminRights, user)
		&& !contains(_restrictedRights, user)
		&& !contains(_external, user)
		&& !contains(_kicked, user)) {
		_infoNotLoaded.emplace(user);
	}
}

void ParticipantsAdditionalData::fillFromPeer() {
	if (const auto chat = _peer->asChat()) {
		fillFromChat(chat);
	} else if (const auto channel = _peer->asChannel()) {
		fillFromChannel(channel);
	} else {
		Unexpected("Peer in ParticipantsAdditionalData::fillFromPeer.");
	}
}

void ParticipantsAdditionalData::fillFromChat(not_null<ChatData*> chat) {
	if (const auto creator = chat->owner().userLoaded(chat->creator)) {
		_creator = creator;
	}
	if (chat->participants.empty()) {
		return;
	}
	_members = chat->participants;
	_admins = chat->admins;
}

void ParticipantsAdditionalData::fillFromChannel(
		not_null<ChannelData*> channel) {
	const auto information = channel->mgInfo.get();
	if (!information) {
		return;
	}
	if (information->creator) {
		_creator = information->creator;
	}
	for (const auto user : information->lastParticipants) {
		const auto admin = information->lastAdmins.find(user);
		const auto restricted = information->lastRestricted.find(user);
		if (admin != information->lastAdmins.cend()) {
			_restrictedRights.erase(user);
			_kicked.erase(user);
			_restrictedBy.erase(user);
			if (admin->second.canEdit) {
				_adminCanEdit.emplace(user);
			} else {
				_adminCanEdit.erase(user);
			}
			_adminRights.emplace(user, admin->second.rights);
		} else if (restricted != information->lastRestricted.cend()) {
			_adminRights.erase(user);
			_adminCanEdit.erase(user);
			_adminPromotedBy.erase(user);
			_restrictedRights.emplace(user, restricted->second.rights);
		}
	}
}

UserData *ParticipantsAdditionalData::applyParticipant(
		const MTPChannelParticipant &data) {
	return applyParticipant(data, _role);
}

UserData *ParticipantsAdditionalData::applyParticipant(
		const MTPChannelParticipant &data,
		Role overrideRole) {
	const auto logBad = [&]() -> UserData* {
		LOG(("API Error: Bad participant type %1 got "
			"while requesting for participants, role: %2"
			).arg(data.type()
			).arg(static_cast<int>(overrideRole)));
		return nullptr;
	};

	return data.match([&](const MTPDchannelParticipantCreator &data) {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members
			&& overrideRole != Role::Admins) {
			return logBad();
		}
		return applyCreator(data);
	}, [&](const MTPDchannelParticipantAdmin &data) {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members
			&& overrideRole != Role::Admins) {
			return logBad();
		}
		return applyAdmin(data);
	}, [&](const MTPDchannelParticipantSelf &data) {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members) {
			return logBad();
		}
		return applyRegular(data.vuser_id);
	}, [&](const MTPDchannelParticipant &data) {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members) {
			return logBad();
		}
		return applyRegular(data.vuser_id);
	}, [&](const MTPDchannelParticipantBanned &data) {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members
			&& overrideRole != Role::Restricted
			&& overrideRole != Role::Kicked) {
			return logBad();
		}
		return applyBanned(data);
	});
}

UserData *ParticipantsAdditionalData::applyCreator(
		const MTPDchannelParticipantCreator &data) {
	if (const auto user = applyRegular(data.vuser_id)) {
		_creator = user;
		return user;
	}
	return nullptr;
}

UserData *ParticipantsAdditionalData::applyAdmin(
		const MTPDchannelParticipantAdmin &data) {
	const auto user = _peer->owner().userLoaded(data.vuser_id.v);
	if (!user) {
		return nullptr;
	} else if (const auto chat = _peer->asChat()) {
		// This can come from saveAdmin callback.
		_admins.emplace(user);
		return user;
	}

	_infoNotLoaded.erase(user);
	_restrictedRights.erase(user);
	_kicked.erase(user);
	_restrictedBy.erase(user);
	_adminRights[user] = data.vadmin_rights;
	if (data.is_can_edit()) {
		_adminCanEdit.emplace(user);
	} else {
		_adminCanEdit.erase(user);
	}
	if (const auto by = _peer->owner().userLoaded(data.vpromoted_by.v)) {
		const auto i = _adminPromotedBy.find(user);
		if (i == _adminPromotedBy.end()) {
			_adminPromotedBy.emplace(user, by);
		} else {
			i->second = by;
		}
	} else {
		LOG(("API Error: No user %1 for admin promoted by."
			).arg(data.vpromoted_by.v));
	}
	return user;
}

UserData *ParticipantsAdditionalData::applyRegular(MTPint userId) {
	const auto user = _peer->owner().userLoaded(userId.v);
	if (!user) {
		return nullptr;
	} else if (const auto chat = _peer->asChat()) {
		// This can come from saveAdmin or saveRestricted callback.
		_admins.erase(user);
		return user;
	}

	_infoNotLoaded.erase(user);
	_adminRights.erase(user);
	_adminCanEdit.erase(user);
	_adminPromotedBy.erase(user);
	_restrictedRights.erase(user);
	_kicked.erase(user);
	_restrictedBy.erase(user);
	return user;
}

UserData *ParticipantsAdditionalData::applyBanned(
		const MTPDchannelParticipantBanned &data) {
	const auto user = _peer->owner().userLoaded(data.vuser_id.v);
	if (!user) {
		return nullptr;
	}

	_infoNotLoaded.erase(user);
	_adminRights.erase(user);
	_adminCanEdit.erase(user);
	_adminPromotedBy.erase(user);
	if (data.is_left()) {
		_kicked.emplace(user);
	} else {
		_kicked.erase(user);
	}
	_restrictedRights[user] = data.vbanned_rights;
	if (const auto by = _peer->owner().userLoaded(data.vkicked_by.v)) {
		const auto i = _restrictedBy.find(user);
		if (i == _restrictedBy.end()) {
			_restrictedBy.emplace(user, by);
		} else {
			i->second = by;
		}
	}
	return user;
}

void ParticipantsAdditionalData::migrate(not_null<ChannelData*> channel) {
	_peer = channel;
	fillFromChannel(channel);

	for (const auto user : _admins) {
		_adminRights.emplace(
			user,
			MTP_chatAdminRights(MTP_flags(ChatData::DefaultAdminRights())));
		if (channel->amCreator()) {
			_adminCanEdit.emplace(user);
		}
		if (_creator) {
			_adminPromotedBy.emplace(user, _creator);
		}
	}
}

ParticipantsOnlineSorter::ParticipantsOnlineSorter(
	not_null<PeerData*> peer,
	not_null<PeerListDelegate*> delegate)
: _peer(peer)
, _delegate(delegate)
, _sortByOnlineTimer([=] { sort(); }) {
	const auto handleUpdate = [=](const Notify::PeerUpdate &update) {
		const auto peerId = update.peer->id;
		if (const auto row = _delegate->peerListFindRow(peerId)) {
			row->refreshStatus();
			sortDelayed();
		}
	};

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(
		Notify::PeerUpdate::Flag::UserOnlineChanged,
		handleUpdate));
	sort();
}

void ParticipantsOnlineSorter::sortDelayed() {
	if (!_sortByOnlineTimer.isActive()) {
		_sortByOnlineTimer.callOnce(kSortByOnlineDelay);
	}
}

void ParticipantsOnlineSorter::sort() {
	const auto channel = _peer->asChannel();
	if (channel
		&& (!channel->isMegagroup()
			|| channel->membersCount() > Global::ChatSizeMax())) {
		_onlineCount = 0;
		return;
	}
	const auto now = unixtime();
	_delegate->peerListSortRows([&](
			const PeerListRow &a,
			const PeerListRow &b) {
		return Data::SortByOnlineValue(a.peer()->asUser(), now) >
			Data::SortByOnlineValue(b.peer()->asUser(), now);
	});
	refreshOnlineCount();
}

rpl::producer<int> ParticipantsOnlineSorter::onlineCountValue() const {
	return _onlineCount.value();
}

void ParticipantsOnlineSorter::refreshOnlineCount() {
	const auto now = unixtime();
	auto left = 0, right = _delegate->peerListFullRowsCount();
	while (right > left) {
		const auto middle = (left + right) / 2;
		const auto row = _delegate->peerListRowAt(middle);
		if (Data::OnlineTextActive(row->peer()->asUser(), now)) {
			left = middle + 1;
		} else {
			right = middle;
		}
	}
	_onlineCount = left;
}

ParticipantsBoxController::SavedState::SavedState(
	const ParticipantsAdditionalData &additional)
: additional(additional) {
}

ParticipantsBoxController::ParticipantsBoxController(
	not_null<Window::Navigation*> navigation,
	not_null<PeerData*> peer,
	Role role)
: PeerListController(CreateSearchController(peer, role, &_additional))
, _navigation(navigation)
, _peer(peer)
, _role(role)
, _additional(peer, _role) {
	subscribeToMigration();
	if (_role == Role::Profile) {
		setupListChangeViewers();
	}
}

void ParticipantsBoxController::setupListChangeViewers() {
	const auto channel = _peer->asChannel();
	if (!channel || !channel->isMegagroup()) {
		return;
	}

	channel->owner().megagroupParticipantAdded(
		channel
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (delegate()->peerListFullRowsCount() > 0) {
			if (delegate()->peerListRowAt(0)->peer() == user) {
				return;
			}
		}
		if (const auto row = delegate()->peerListFindRow(user->id)) {
			delegate()->peerListPartitionRows([&](const PeerListRow &row) {
				return (row.peer() == user);
			});
		} else {
			delegate()->peerListPrependRow(createRow(user));
			delegate()->peerListRefreshRows();
			if (_onlineSorter) {
				_onlineSorter->sort();
			}
		}
	}, lifetime());

	channel->owner().megagroupParticipantRemoved(
		channel
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (const auto row = delegate()->peerListFindRow(user->id)) {
			delegate()->peerListRemoveRow(row);
		}
		delegate()->peerListRefreshRows();
	}, lifetime());
}

auto ParticipantsBoxController::CreateSearchController(
	not_null<PeerData*> peer,
	Role role,
	not_null<ParticipantsAdditionalData*> additional)
-> std::unique_ptr<PeerListSearchController> {
	const auto channel = peer->asChannel();

	// In admins box complex search is used for adding new admins.
	if (channel && (role != Role::Admins || channel->canAddAdmins())) {
		return std::make_unique<ParticipantsBoxSearchController>(
			channel,
			role,
			additional);
	}
	return nullptr;
}

void ParticipantsBoxController::Start(
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer,
		Role role) {
	auto controller = std::make_unique<ParticipantsBoxController>(
		navigation,
		peer,
		role);
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_close), [=] { box->closeBox(); });

		const auto chat = peer->asChat();
		const auto channel = peer->asChannel();
		const auto canAddNewItem = [&] {
			Assert(chat != nullptr || channel != nullptr);

			switch (role) {
			case Role::Members:
				return chat
					? chat->canAddMembers()
					: (channel->canAddMembers()
						&& (channel->membersCount() < Global::ChatSizeMax()
							|| channel->isMegagroup()));
			case Role::Admins:
				return chat
					? chat->canAddAdmins()
					: channel->canAddAdmins();
			case Role::Restricted:
			case Role::Kicked:
				return chat
					? chat->canBanMembers()
					: channel->canBanMembers();
			}

			Unexpected("Role value in ParticipantsBoxController::Start()");
		}();
		const auto addNewItemText = [&] {
			switch (role) {
			case Role::Members:
				return langFactory((chat || channel->isMegagroup())
					? lng_channel_add_members
					: lng_channel_add_users);
			case Role::Admins:
				return langFactory(lng_channel_add_admin);
			case Role::Restricted:
				return langFactory(lng_channel_add_exception);
			case Role::Kicked:
				return langFactory(lng_channel_add_removed);
			}
			Unexpected("Role value in ParticipantsBoxController::Start()");
		}();
		if (canAddNewItem) {
			box->addLeftButton(addNewItemText, [=] {
				controller->addNewItem();
			});
		}
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), initBox),
		LayerOption::KeepOther);
}

void ParticipantsBoxController::addNewItem() {
	Expects(_role != Role::Profile);

	if (_role == Role::Members) {
		addNewParticipants();
		return;
	}
	const auto adminDone = crl::guard(this, [=](
			not_null<UserData*> user,
			const MTPChatAdminRights &rights) {
		editAdminDone(user, rights);
	});
	const auto restrictedDone = crl::guard(this, [=](
			not_null<UserData*> user,
			const MTPChatBannedRights &rights) {
		editRestrictedDone(user, rights);
	});
	const auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_cancel), [=] { box->closeBox(); });
	};

	_addBox = Ui::show(
		Box<PeerListBox>(
			std::make_unique<AddSpecialBoxController>(
				_peer,
				_role,
				adminDone,
				restrictedDone),
			initBox),
		LayerOption::KeepOther);
}

void ParticipantsBoxController::addNewParticipants() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (chat) {
		AddParticipantsBoxController::Start(chat);
	} else if (channel->isMegagroup()
		|| channel->membersCount() < Global::ChatSizeMax()) {
		const auto count = delegate()->peerListFullRowsCount();
		auto already = std::vector<not_null<UserData*>>();
		already.reserve(count);
		for (auto i = 0; i != count; ++i) {
			already.emplace_back(
				delegate()->peerListRowAt(i)->peer()->asUser());
		}
		AddParticipantsBoxController::Start(
			channel,
			{ already.begin(), already.end() });
	} else {
		Ui::show(Box<MaxInviteBox>(channel), LayerOption::KeepOther);
	}
}

void ParticipantsBoxController::peerListSearchAddRow(
		not_null<PeerData*> peer) {
	PeerListController::peerListSearchAddRow(peer);
	if (_role == Role::Restricted
		&& delegate()->peerListFullRowsCount() > 0) {
		setDescriptionText(QString());
	}
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createSearchRow(
		not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createRestoredRow(
		not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

auto ParticipantsBoxController::saveState() const
-> std::unique_ptr<PeerListState> {
	Expects(_role == Role::Profile);

	auto result = PeerListController::saveState();

	auto my = std::make_unique<SavedState>(_additional);
	my->offset = _offset;
	my->allLoaded = _allLoaded;
	my->wasLoading = (_loadRequestId != 0);
	if (const auto search = searchController()) {
		my->searchState = search->saveState();
	}

	const auto weak = result.get();
	if (const auto chat = _peer->asChat()) {
		Notify::PeerUpdateViewer(
			chat,
			Notify::PeerUpdate::Flag::MembersChanged
		) | rpl::start_with_next([=](const Notify::PeerUpdate &) {
			weak->controllerState = nullptr;
		}, my->lifetime);
	} else if (const auto channel = _peer->asMegagroup()) {
		channel->owner().megagroupParticipantAdded(
			channel
		) | rpl::start_with_next([=](not_null<UserData*> user) {
			if (!weak->list.empty()) {
				if (weak->list[0] == user) {
					return;
				}
			}
			auto pos = ranges::find(weak->list, user);
			if (pos == weak->list.cend()) {
				weak->list.emplace_back(user);
			}
			ranges::stable_partition(
				weak->list,
				[user](auto peer) { return (peer == user); });
		}, my->lifetime);

		channel->owner().megagroupParticipantRemoved(
			channel
		) | rpl::start_with_next([=](not_null<UserData*> user) {
			weak->list.erase(std::remove(
				weak->list.begin(),
				weak->list.end(),
				user), weak->list.end());
			weak->filterResults.erase(std::remove(
				weak->filterResults.begin(),
				weak->filterResults.end(),
				user), weak->filterResults.end());
		}, my->lifetime);
	}
	result->controllerState = std::move(my);
	return result;
}

void ParticipantsBoxController::restoreState(
		std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (const auto my = dynamic_cast<SavedState*>(typeErasedState)) {
		if (const auto requestId = base::take(_loadRequestId)) {
			request(requestId).cancel();
		}

		_additional = std::move(my->additional);
		_offset = my->offset;
		_allLoaded = my->allLoaded;
		if (const auto search = searchController()) {
			search->restoreState(std::move(my->searchState));
		}
		if (my->wasLoading) {
			loadMoreRows();
		}
		PeerListController::restoreState(std::move(state));
		if (delegate()->peerListFullRowsCount() > 0 || _allLoaded) {
			refreshDescription();
		}
		if (_onlineSorter) {
			_onlineSorter->sort();
		}
	}
}

rpl::producer<int> ParticipantsBoxController::onlineCountValue() const {
	return _onlineSorter
		? _onlineSorter->onlineCountValue()
		: rpl::single(0);
}

void ParticipantsBoxController::prepare() {
	const auto titleKey = [&] {
		switch (_role) {
		case Role::Admins: return lng_channel_admins;
		case Role::Profile:
		case Role::Members: return lng_profile_participants_section;
		case Role::Restricted: return lng_exceptions_list_title;
		case Role::Kicked: return lng_removed_list_title;
		}
		Unexpected("Role in ParticipantsBoxController::prepare()");
	}();
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(titleKey));
	setDescriptionText(lang(lng_contacts_loading));
	setSearchNoResultsText(lang(lng_blocked_list_not_found));

	if (const auto chat = _peer->asChat()) {
		prepareChatRows(chat);
	} else {
		loadMoreRows();
	}
	if (_role == Role::Profile && !_onlineSorter) {
		_onlineSorter = std::make_unique<ParticipantsOnlineSorter>(
			_peer,
			delegate());
	}
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::prepareChatRows(not_null<ChatData*> chat) {
	if (_role == Role::Profile || _role == Role::Members) {
		_onlineSorter = std::make_unique<ParticipantsOnlineSorter>(
			chat,
			delegate());
	}

	rebuildChatRows(chat);
	if (!delegate()->peerListFullRowsCount()) {
		chat->updateFullForced();
	}

	using UpdateFlag = Notify::PeerUpdate::Flag;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(
		UpdateFlag::MembersChanged
		| UpdateFlag::AdminsChanged,
		[=](const Notify::PeerUpdate &update) {
			if (update.peer != chat) {
				return;
			}
			_additional.fillFromPeer();
			if ((update.flags & UpdateFlag::MembersChanged)
				|| (_role == Role::Admins)) {
				rebuildChatRows(chat);
			}
			if (update.flags & UpdateFlag::AdminsChanged) {
				rebuildRowTypes();
			}
		}));
}

void ParticipantsBoxController::rebuildChatRows(not_null<ChatData*> chat) {
	switch (_role) {
	case Role::Profile:
	case Role::Members: return rebuildChatParticipants(chat);
	case Role::Admins: return rebuildChatAdmins(chat);
	case Role::Restricted:
	case Role::Kicked: return chatListReady();
	}
	Unexpected("Role in ParticipantsBoxController::rebuildChatRows");
}

void ParticipantsBoxController::rebuildChatParticipants(
		not_null<ChatData*> chat) {
	if (chat->noParticipantInfo()) {
		chat->updateFullForced();
		return;
	}

	auto &participants = chat->participants;
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		auto row = delegate()->peerListRowAt(i);
		auto user = row->peer()->asUser();
		if (participants.contains(user)) {
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--count;
		}
	}
	for (const auto user : participants) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	_onlineSorter->sort();

	delegate()->peerListRefreshRows();
	chatListReady();
}

void ParticipantsBoxController::rebuildChatAdmins(
		not_null<ChatData*> chat) {
	if (chat->participants.empty()) {
		// We get such updates often
		// (when participants list was invalidated).
		//while (delegate()->peerListFullRowsCount() > 0) {
		//	delegate()->peerListRemoveRow(
		//		delegate()->peerListRowAt(0));
		//}
		return;
	}

	auto list = ranges::view::all(chat->admins) | ranges::to_vector;
	if (const auto creator = chat->owner().userLoaded(chat->creator)) {
		list.emplace_back(creator);
	}
	ranges::sort(list, [](not_null<UserData*> a, not_null<UserData*> b) {
		return (a->name.compare(b->name, Qt::CaseInsensitive) < 0);
	});

	const auto same = [&] {
		const auto count = delegate()->peerListFullRowsCount();
		if (count != list.size()) {
			return false;
		}
		for (auto i = 0; i != count; ++i) {
			if (list[i] != delegate()->peerListRowAt(i)->peer()) {
				return false;
			}
		}
		return true;
	}();
	if (same) {
		return;
	}

	while (delegate()->peerListFullRowsCount() > 0) {
		delegate()->peerListRemoveRow(
			delegate()->peerListRowAt(0));
	}
	for (const auto user : list) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}

	delegate()->peerListRefreshRows();
	chatListReady();
}

void ParticipantsBoxController::chatListReady() {
	if (_allLoaded) {
		return;
	}
	_allLoaded = true;
	refreshDescription();
}

void ParticipantsBoxController::rebuildRowTypes() {
	if (_role != Role::Profile) {
		return;
	}
	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto row = static_cast<Row*>(
			delegate()->peerListRowAt(i).get());
		row->setType(computeType(row->user()));
	}
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	} else if (!_peer->isChannel() || _loadRequestId || _allLoaded) {
		return;
	}

	const auto channel = _peer->asChannel();
	if (feedMegagroupLastParticipants()) {
		return;
	}

	const auto filter = [&] {
		if (_role == Role::Members || _role == Role::Profile) {
			return MTP_channelParticipantsRecent();
		} else if (_role == Role::Admins) {
			return MTP_channelParticipantsAdmins();
		} else if (_role == Role::Restricted) {
			return MTP_channelParticipantsBanned(MTP_string(QString()));
		}
		return MTP_channelParticipantsKicked(MTP_string(QString()));
	}();

	// First query is small and fast, next loads a lot of rows.
	const auto perPage = (_offset > 0)
		? kParticipantsPerPage
		: kParticipantsFirstPageCount;
	const auto participantsHash = 0;

	_loadRequestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		filter,
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		const auto firstLoad = !_offset;
		_loadRequestId = 0;

		auto wasRecentRequest = firstLoad
			&& (_role == Role::Members || _role == Role::Profile);
		auto parseParticipants = [&](auto &&result, auto &&callback) {
			if (wasRecentRequest) {
				channel->session().api().parseRecentChannelParticipants(
					channel,
					result,
					callback);
			} else {
				channel->session().api().parseChannelParticipants(
					channel,
					result,
					callback);
			}
		};
		parseParticipants(result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			for (const auto &data : list) {
				if (const auto user = _additional.applyParticipant(data)) {
					appendRow(user);
				}
			}
			if (const auto size = list.size()) {
				_offset += size;
			} else {
				// To be sure - wait for a whole empty result list.
				_allLoaded = true;
			}
		});

		if (_allLoaded
			|| (firstLoad && delegate()->peerListFullRowsCount() > 0)) {
			refreshDescription();
		}
		if (_onlineSorter) {
			_onlineSorter->sort();
		}
		delegate()->peerListRefreshRows();
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void ParticipantsBoxController::refreshDescription() {
	setDescriptionText((_role == Role::Kicked)
		? lang((_peer->isChat() || _peer->isMegagroup())
			? lng_group_removed_list_about
			: lng_channel_removed_list_about)
		: (delegate()->peerListFullRowsCount() > 0)
		? QString()
		: lang(lng_blocked_list_not_found));
}

bool ParticipantsBoxController::feedMegagroupLastParticipants() {
	if ((_role != Role::Members && _role != Role::Profile)
		|| delegate()->peerListFullRowsCount() > 0) {
		return false;
	}
	const auto megagroup = _peer->asMegagroup();
	if (!megagroup) {
		return false;
	}
	const auto info = megagroup->mgInfo.get();
	//
	// channelFull and channels_channelParticipants members count desynced
	// so we almost always have LastParticipantsCountOutdated that is set
	// inside setMembersCount() and so we almost never use lastParticipants.
	//
	// => disable this check temporarily.
	//
	//if (info->lastParticipantsStatus
	//	!= MegagroupInfo::LastParticipantsUpToDate) {
	//	_channel->updateFull();
	//	return false;
	//}
	if (info->lastParticipants.empty()) {
		return false;
	}

	_additional.fillFromPeer();
	for (const auto user : info->lastParticipants) {
		appendRow(user);

		//
		// Don't count lastParticipants in _offset, because we don't know
		// their exact information (admin / creator / restricted), they
		// could simply be added from the last messages authors.
		//
		//++_offset;
	}
	if (_onlineSorter) {
		_onlineSorter->sort();
	}
	return true;
}

void ParticipantsBoxController::rowClicked(not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto user = row->peer()->asUser();
	if (_role == Role::Admins) {
		showAdmin(user);
	} else if (_role == Role::Restricted || _role == Role::Kicked) {
		showRestricted(user);
	} else {
		_navigation->showPeerInfo(user);
	}
}

void ParticipantsBoxController::rowActionClicked(
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto user = row->peer()->asUser();
	if (_role == Role::Members || _role == Role::Profile) {
		kickMember(user);
	} else if (_role == Role::Admins) {
		removeAdmin(user);
	} else if (_role == Role::Restricted) {
		showRestricted(user);
	} else {
		removeKicked(row, user);
	}
}

base::unique_qptr<Ui::PopupMenu> ParticipantsBoxController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto user = row->peer()->asUser();
	auto result = base::make_unique_q<Ui::PopupMenu>(parent);
	result->addAction(
		lang(lng_context_view_profile),
		crl::guard(this, [=] { _navigation->showPeerInfo(user); }));
	if (_role == Role::Kicked) {
		if (_peer->isMegagroup()
			&& _additional.canRestrictUser(user)) {
			if (channel->canAddMembers()) {
				result->addAction(
					lang(lng_context_add_to_group),
					crl::guard(this, [=] { unkickMember(user); }));
			}
			result->addAction(
				lang(lng_profile_delete_removed),
				crl::guard(this, [=] { removeKickedWithRow(user); }));
		}
		return result;
	}
	if (_additional.canAddOrEditAdmin(user)) {
		const auto isAdmin = _additional.isCreator(user)
			|| _additional.adminRights(user).has_value();
		const auto labelKey = isAdmin
			? lng_context_edit_permissions
			: lng_context_promote_admin;
		result->addAction(
			lang(labelKey),
			crl::guard(this, [=] { showAdmin(user); }));
	}
	if (_additional.canRestrictUser(user)) {
		const auto isGroup = _peer->isChat() || _peer->isMegagroup();
		if (isGroup) {
			result->addAction(
				lang(lng_context_restrict_user),
				crl::guard(this, [=] { showRestricted(user); }));
		}
		if (!_additional.isKicked(user)) {
			result->addAction(
				lang(isGroup
					? lng_context_remove_from_group
					: lng_profile_kick),
				crl::guard(this, [=] { kickMember(user); }));
		}
	}
	return result;
}

void ParticipantsBoxController::showAdmin(not_null<UserData*> user) {
	const auto adminRights = _additional.adminRights(user);
	const auto currentRights = _additional.isCreator(user)
		? MTPChatAdminRights(MTP_chatAdminRights(
			MTP_flags(~MTPDchatAdminRights::Flag::f_add_admins
				| MTPDchatAdminRights::Flag::f_add_admins)))
		: adminRights
		? *adminRights
		: MTPChatAdminRights(MTP_chatAdminRights(MTP_flags(0)));
	auto box = Box<EditAdminBox>(_peer, user, currentRights);
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (_additional.canAddOrEditAdmin(user)) {
		const auto done = crl::guard(this, [=](
				const MTPChatAdminRights &newRights) {
			editAdminDone(user, newRights);
		});
		const auto fail = crl::guard(this, [=] {
			_editBox = nullptr;
		});
		box->setSaveCallback(SaveAdminCallback(_peer, user, done, fail));
	}
	_editBox = Ui::show(std::move(box), LayerOption::KeepOther);
}

void ParticipantsBoxController::editAdminDone(
		not_null<UserData*> user,
		const MTPChatAdminRights &rights) {
	_addBox = nullptr;
	_editBox = nullptr;

	const auto date = unixtime(); // Incorrect, but ignored.
	if (rights.c_chatAdminRights().vflags.v == 0) {
		_additional.applyParticipant(MTP_channelParticipant(
			MTP_int(user->bareId()),
			MTP_int(date)));
		if (_role == Role::Admins) {
			removeRow(user);
		}
	} else {
		const auto alreadyPromotedBy = _additional.adminPromotedBy(user);
		_additional.applyParticipant(MTP_channelParticipantAdmin(
			MTP_flags(MTPDchannelParticipantAdmin::Flag::f_can_edit),
			MTP_int(user->bareId()),
			MTPint(), // inviter_id
			MTP_int(alreadyPromotedBy
				? alreadyPromotedBy->bareId()
				: user->session().userId()),
			MTP_int(date),
			rights));
		if (_role == Role::Admins) {
			prependRow(user);
		} else if (_role == Role::Kicked || _role == Role::Restricted) {
			removeRow(user);
		}
	}
	recomputeTypeFor(user);
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::showRestricted(not_null<UserData*> user) {
	const auto restrictedRights = _additional.restrictedRights(user);
	const auto currentRights = restrictedRights
		? *restrictedRights
		: MTPChatBannedRights(MTP_chatBannedRights(
			MTP_flags(0),
			MTP_int(0)));
	const auto hasAdminRights = _additional.adminRights(user).has_value();
	auto box = Box<EditRestrictedBox>(
		_peer,
		user,
		hasAdminRights,
		currentRights);
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (_additional.canRestrictUser(user)) {
		const auto done = crl::guard(this, [=](
				const MTPChatBannedRights &newRights) {
			editRestrictedDone(user, newRights);
		});
		const auto fail = crl::guard(this, [=] {
			_editBox = nullptr;
		});
		box->setSaveCallback(
			SaveRestrictedCallback(_peer, user, done, fail));
	}
	_editBox = Ui::show(std::move(box), LayerOption::KeepOther);
}

void ParticipantsBoxController::editRestrictedDone(
		not_null<UserData*> user,
		const MTPChatBannedRights &rights) {
	_addBox = nullptr;
	_editBox = nullptr;

	const auto date = unixtime(); // Incorrect, but ignored.
	if (rights.c_chatBannedRights().vflags.v == 0) {
		_additional.applyParticipant(MTP_channelParticipant(
			MTP_int(user->bareId()),
			MTP_int(date)));
		if (_role == Role::Kicked || _role == Role::Restricted) {
			removeRow(user);
		}
	} else {
		const auto kicked = rights.c_chatBannedRights().is_view_messages();
		const auto alreadyRestrictedBy = _additional.restrictedBy(user);
		_additional.applyParticipant(MTP_channelParticipantBanned(
			MTP_flags(kicked
				? MTPDchannelParticipantBanned::Flag::f_left
				: MTPDchannelParticipantBanned::Flag(0)),
			MTP_int(user->bareId()),
			MTP_int(alreadyRestrictedBy
				? alreadyRestrictedBy->bareId()
				: user->session().userId()),
			MTP_int(date),
			rights));
		if (kicked) {
			if (_role == Role::Kicked) {
				prependRow(user);
			} else if (_role == Role::Admins
				|| _role == Role::Restricted
				|| _role == Role::Members) {
				removeRow(user);
			}
		} else {
			if (_role == Role::Restricted) {
				prependRow(user);
			} else if (_role == Role::Kicked
				|| _role == Role::Admins
				|| _role == Role::Members) {
				removeRow(user);
			}
		}
	}
	recomputeTypeFor(user);
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::kickMember(not_null<UserData*> user) {
	const auto text = ((_peer->isChat() || _peer->isMegagroup())
		? lng_profile_sure_kick
		: lng_profile_sure_kick_channel)(lt_user, user->firstName);
	_editBox = Ui::show(
		Box<ConfirmBox>(
			text,
			lang(lng_box_remove),
			crl::guard(this, [=] { kickMemberSure(user); })),
		LayerOption::KeepOther);
}

void ParticipantsBoxController::unkickMember(not_null<UserData*> user) {
	_editBox = nullptr;
	if (const auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
	_peer->session().api().addChatParticipants(_peer, { 1, user });
}

void ParticipantsBoxController::kickMemberSure(not_null<UserData*> user) {
	_editBox = nullptr;

	const auto restrictedRights = _additional.restrictedRights(user);
	const auto currentRights = restrictedRights
		? *restrictedRights
		: MTPChatBannedRights(MTP_chatBannedRights(
			MTP_flags(0),
			MTP_int(0)));

	if (const auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
	auto &session = _peer->session();
	if (const auto chat = _peer->asChat()) {
		session.api().kickParticipant(chat, user);
	} else if (const auto channel = _peer->asChannel()) {
		session.api().kickParticipant(channel, user, currentRights);
	}
}

void ParticipantsBoxController::removeAdmin(not_null<UserData*> user) {
	_editBox = Ui::show(
		Box<ConfirmBox>(
			lng_profile_sure_remove_admin(
				lt_user,
				user->firstName),
			lang(lng_box_remove),
			crl::guard(this, [=] { removeAdminSure(user); })),
		LayerOption::KeepOther);
}

void ParticipantsBoxController::removeAdminSure(not_null<UserData*> user) {
	_editBox = nullptr;

	if (const auto chat = _peer->asChat()) {
		SaveChatAdmin(chat, user, false, crl::guard(this, [=] {
			editAdminDone(user, MTP_chatAdminRights(MTP_flags(0)));
		}), nullptr);
	} else if (const auto channel = _peer->asChannel()) {
		const auto adminRights = _additional.adminRights(user);
		if (!adminRights) {
			return;
		}
		RemoveAdmin(channel, user, *adminRights, crl::guard(this, [=] {
			editAdminDone(user, MTP_chatAdminRights(MTP_flags(0)));
		}), nullptr);
	}
}

void ParticipantsBoxController::removeKickedWithRow(
		not_null<UserData*> user) {
	if (const auto row = delegate()->peerListFindRow(user->id)) {
		removeKicked(row, user);
	} else {
		removeKicked(user);
	}
}
void ParticipantsBoxController::removeKicked(not_null<UserData*> user) {
	if (const auto channel = _peer->asChannel()) {
		channel->session().api().unblockParticipant(channel, user);
	}
}

void ParticipantsBoxController::removeKicked(
		not_null<PeerListRow*> row,
		not_null<UserData*> user) {
	delegate()->peerListRemoveRow(row);
	delegate()->peerListRefreshRows();
	removeKicked(user);
}

bool ParticipantsBoxController::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		recomputeTypeFor(user);
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	if (_role != Role::Kicked) {
		setDescriptionText(QString());
	}
	return true;
}

bool ParticipantsBoxController::prependRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		recomputeTypeFor(user);
		refreshCustomStatus(row);
		if (_role == Role::Admins) {
			// Perhaps we've added a new admin from search.
			delegate()->peerListPrependRowFromSearchResult(row);
		}
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	if (_role != Role::Kicked) {
		setDescriptionText(QString());
	}
	return true;
}

bool ParticipantsBoxController::removeRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		if (_role == Role::Admins) {
			// Perhaps we are removing an admin from search results.
			row->setCustomStatus(lang(lng_channel_admin_status_not_admin));
			delegate()->peerListConvertRowToSearchResult(row);
		} else {
			delegate()->peerListRemoveRow(row);
		}
		if (_role != Role::Kicked
			&& !delegate()->peerListFullRowsCount()) {
			setDescriptionText(lang(lng_blocked_list_not_found));
		}
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createRow(
		not_null<UserData*> user) const {
	if (_role == Role::Profile) {
		return std::make_unique<Row>(user, computeType(user));
	}
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	auto row = std::make_unique<PeerListRowWithLink>(user);
	refreshCustomStatus(row.get());
	if (_role == Role::Admins
		&& _additional.adminRights(user).has_value()
		&& _additional.canEditAdmin(user)) {
		row->setActionLink(lang(lng_profile_kick));
	} else if (_role == Role::Kicked) {
		row->setActionLink(lang(lng_profile_delete_removed));
	} else if (_role == Role::Members) {
		if ((chat ? chat->canBanMembers() : channel->canBanMembers())
			&& !_additional.isCreator(user)
			&& (!_additional.adminRights(user)
				|| _additional.canEditAdmin(user))) {
			row->setActionLink(lang(lng_profile_kick));
		}
	}
	return std::move(row);
}

auto ParticipantsBoxController::computeType(
		not_null<UserData*> user) const -> Type {
	auto result = Type();
	result.rights = _additional.isCreator(user)
		? Rights::Creator
		: _additional.adminRights(user).has_value()
		? Rights::Admin
		: Rights::Normal;
	result.canRemove = _additional.canRestrictUser(user);
	return result;
}

void ParticipantsBoxController::recomputeTypeFor(
		not_null<UserData*> user) {
	if (_role != Role::Profile) {
		return;
	}
	if (const auto row = delegate()->peerListFindRow(user->id)) {
		static_cast<Row*>(row)->setType(computeType(user));
	}
}

void ParticipantsBoxController::refreshCustomStatus(
		not_null<PeerListRow*> row) const {
	const auto user = row->peer()->asUser();
	if (_role == Role::Admins) {
		if (const auto by = _additional.adminPromotedBy(user)) {
			row->setCustomStatus(lng_channel_admin_status_promoted_by(
				lt_user,
				App::peerName(by)));
		} else {
			if (_additional.isCreator(user)) {
				row->setCustomStatus(
					lang(lng_channel_admin_status_creator));
			} else {
				row->setCustomStatus(
					lang(lng_channel_admin_status_not_admin));
			}
		}
	} else if (_role == Role::Kicked || _role == Role::Restricted) {
		const auto by = _additional.restrictedBy(user);
		row->setCustomStatus((_role == Role::Kicked
			? lng_channel_banned_status_removed_by
			: lng_channel_banned_status_restricted_by)(
			lt_user,
			by ? App::peerName(by) : "Unknown"));
	}
}

void ParticipantsBoxController::subscribeToMigration() {
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(channel); });
}

void ParticipantsBoxController::migrate(not_null<ChannelData*> channel) {
	_peer = channel;
	_additional.migrate(channel);
}

ParticipantsBoxSearchController::ParticipantsBoxSearchController(
	not_null<ChannelData*> channel,
	Role role,
	not_null<ParticipantsAdditionalData*> additional)
: _channel(channel)
, _role(role)
, _additional(additional) {
	_timer.setCallback([=] { searchOnServer(); });
}

void ParticipantsBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offset = 0;
		_requestId = 0;
		_allLoaded = false;
		if (!_query.isEmpty() && !searchInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

auto ParticipantsBoxSearchController::saveState() const
-> std::unique_ptr<SavedStateBase> {
	auto result = std::make_unique<SavedState>();
	result->query = _query;
	result->offset = _offset;
	result->allLoaded = _allLoaded;
	result->wasLoading = (_requestId != 0);
	return std::move(result);
}

void ParticipantsBoxSearchController::restoreState(
		std::unique_ptr<SavedStateBase> state) {
	if (auto my = dynamic_cast<SavedState*>(state.get())) {
		if (auto requestId = base::take(_requestId)) {
			request(requestId).cancel();
		}
		_cache.clear();
		_queries.clear();

		_allLoaded = my->allLoaded;
		_offset = my->offset;
		_query = my->query;
		if (my->wasLoading) {
			searchOnServer();
		}
	}
}

void ParticipantsBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());

	loadMoreRows();
}

bool ParticipantsBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool ParticipantsBoxSearchController::searchInCache() {
	const auto i = _cache.find(_query);
	if (i != _cache.cend()) {
		_requestId = 0;
		searchDone(
			_requestId,
			i->second.result,
			i->second.requestedCount);
		return true;
	}
	return false;
}

bool ParticipantsBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	}
	if (_allLoaded || isLoading()) {
		return true;
	}
	auto filter = [&] {
		switch (_role) {
		case Role::Admins: // Search for members, appoint as admin on found.
		case Role::Profile:
		case Role::Members:
			return MTP_channelParticipantsSearch(MTP_string(_query));
		case Role::Restricted:
			return MTP_channelParticipantsBanned(MTP_string(_query));
		case Role::Kicked:
			return MTP_channelParticipantsKicked(MTP_string(_query));
		}
		Unexpected("Role in ParticipantsBoxSearchController.");
	}();

	// For search we request a lot of rows from the first query.
	// (because we've waited for search request by timer already,
	// so we don't expect it to be fast, but we want to fill cache).
	auto perPage = kParticipantsPerPage;
	auto participantsHash = 0;

	_requestId = request(MTPchannels_GetParticipants(
		_channel->inputChannel,
		filter,
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](
			const MTPchannels_ChannelParticipants &result,
			mtpRequestId requestId) {
		searchDone(requestId, result, perPage);
	}).fail([=](const RPCError &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_allLoaded = true;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();

	auto entry = Query();
	entry.text = _query;
	entry.offset = _offset;
	_queries.emplace(_requestId, entry);
	return true;
}

void ParticipantsBoxSearchController::searchDone(
		mtpRequestId requestId,
		const MTPchannels_ChannelParticipants &result,
		int requestedCount) {
	auto query = _query;
	if (requestId) {
		const auto addToCache = [&](auto&&...) {
			auto it = _queries.find(requestId);
			if (it != _queries.cend()) {
				query = it->second.text;
				if (it->second.offset == 0) {
					auto &entry = _cache[query];
					entry.result = result;
					entry.requestedCount = requestedCount;
				}
				_queries.erase(it);
			}
		};
		_channel->session().api().parseChannelParticipants(
			_channel,
			result,
			addToCache);
	}
	if (_requestId != requestId) {
		return;
	}

	_requestId = 0;
	result.match([&](const MTPDchannels_channelParticipants &data) {
		const auto &list = data.vparticipants.v;
		if (list.size() < requestedCount) {
			// We want cache to have full information about a query with
			// small results count (that we don't need the second request).
			// So we don't wait for empty list unlike the non-search case.
			_allLoaded = true;
		}
		const auto overrideRole = (_role == Role::Admins)
			? Role::Members
			: _role;
		for (const auto &data : list) {
			const auto user = _additional->applyParticipant(
				data,
				overrideRole);
			if (user) {
				delegate()->peerListSearchAddRow(user);
			}
		}
		_offset += list.size();
	}, [&](const MTPDchannels_channelParticipantsNotModified &) {
		_allLoaded = true;
	});

	delegate()->peerListSearchRefreshRows();
}
