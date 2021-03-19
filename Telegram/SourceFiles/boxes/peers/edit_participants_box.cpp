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
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "base/unixtime.h"
#include "ui/widgets/popup_menu.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "history/history.h"

namespace {

// How many messages from chat history server should forward to user,
// that was added to this chat.
constexpr auto kForwardMessagesOnAdd = 100;

constexpr auto kParticipantsFirstPageCount = 16;
constexpr auto kParticipantsPerPage = 200;
constexpr auto kSortByOnlineDelay = crl::time(1000);

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
		newRights,
		MTP_string(QString())
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		channel->applyEditAdmin(user, oldRights, newRights, QString());
		if (onDone) {
			onDone();
		}
	}).fail([=](const MTP::Error &error) {
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
	}).fail([=](const MTP::Error &error) {
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
	}).fail([=](const MTP::Error &error) {
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
		const QString &rank,
		Fn<void()> onDone,
		Fn<void()> onFail) {
	channel->session().api().request(MTPchannels_EditAdmin(
		channel->inputChannel,
		user->inputUser,
		newRights,
		MTP_string(rank)
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		channel->applyEditAdmin(user, oldRights, newRights, rank);
		if (onDone) {
			onDone();
		}
	}).fail([=](const MTP::Error &error) {
		ShowAddParticipantsError(error.type(), channel, { 1, user });
		if (onFail) {
			onFail();
		}
	}).send();
}

void SaveChannelRestriction(
		not_null<ChannelData*> channel,
		not_null<PeerData*> participant,
		const MTPChatBannedRights &oldRights,
		const MTPChatBannedRights &newRights,
		Fn<void()> onDone,
		Fn<void()> onFail) {
	channel->session().api().request(MTPchannels_EditBanned(
		channel->inputChannel,
		participant->input,
		newRights
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		channel->applyEditBanned(participant, oldRights, newRights);
		if (onDone) {
			onDone();
		}
	}).fail([=](const MTP::Error &error) {
		if (onFail) {
			onFail();
		}
	}).send();
}

void SaveChatParticipantKick(
		not_null<ChatData*> chat,
		not_null<UserData*> user,
		Fn<void()> onDone,
		Fn<void()> onFail) {
	chat->session().api().request(MTPmessages_DeleteChatUser(
		MTP_flags(0),
		chat->inputChat,
		user->inputUser
	)).done([=](const MTPUpdates &result) {
		chat->session().api().applyUpdates(result);
		if (onDone) {
			onDone();
		}
	}).fail([=](const MTP::Error &error) {
		if (onFail) {
			onFail();
		}
	}).send();
}

} // namespace

Fn<void(
	const MTPChatAdminRights &oldRights,
	const MTPChatAdminRights &newRights,
	const QString &rank)> SaveAdminCallback(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		Fn<void(
			const MTPChatAdminRights &newRights,
			const QString &rank)> onDone,
		Fn<void()> onFail) {
	return [=](
			const MTPChatAdminRights &oldRights,
			const MTPChatAdminRights &newRights,
			const QString &rank) {
		const auto done = [=] { if (onDone) onDone(newRights, rank); };
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			SaveChannelAdmin(
				channel,
				user,
				oldRights,
				newRights,
				rank,
				done,
				onFail);
		};
		if (const auto chat = peer->asChatNotMigrated()) {
			const auto saveChatAdmin = [&](bool isAdmin) {
				SaveChatAdmin(chat, user, isAdmin, done, onFail);
			};
			const auto flags = newRights.match([](
					const MTPDchatAdminRights &data) {
				return data.vflags().v;
			});
			if (flags == chat->defaultAdminRights(user) && rank.isEmpty()) {
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
		not_null<PeerData*> participant,
		Fn<void(const MTPChatBannedRights &newRights)> onDone,
		Fn<void()> onFail) {
	return [=](
			const MTPChatBannedRights &oldRights,
			const MTPChatBannedRights &newRights) {
		const auto done = [=] { if (onDone) onDone(newRights); };
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			SaveChannelRestriction(
				channel,
				participant,
				oldRights,
				newRights,
				done,
				onFail);
		};
		if (const auto chat = peer->asChatNotMigrated()) {
			const auto flags = Data::ChatBannedRightsFlags(newRights);
			if (participant->isUser()
				&& (flags & MTPDchatBannedRights::Flag::f_view_messages)) {
				SaveChatParticipantKick(
					chat,
					participant->asUser(),
					done,
					onFail);
			} else if (!flags) {
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
			chat->session().changes().peerUpdates(
				peer,
				Data::PeerUpdate::Flag::Migration
			) | rpl::map([](const Data::PeerUpdate &update) {
				return update.peer->migrateTo();
			}) | rpl::filter([](ChannelData *channel) {
				return (channel != nullptr);
			}) | rpl::take(
				1
			) | rpl::start_with_next([=](not_null<ChannelData*> channel) {
				const auto onstack = base::duplicate(migrate);
				onstack(channel);
			}, lifetime);
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
		not_null<PeerData*> participant) const {
	return _peer->isChat()
		|| (_infoNotLoaded.find(participant) == end(_infoNotLoaded));
}

bool ParticipantsAdditionalData::canEditAdmin(
		not_null<UserData*> user) const {
	if (_creator && _creator->isSelf()) {
		return true;
	} else if (_creator == user || user->isSelf()) {
		return false;
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

bool ParticipantsAdditionalData::canRestrictParticipant(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (user && (!canEditAdmin(user) || user->isSelf())) {
		return false;
	} else if (const auto chat = _peer->asChat()) {
		return chat->canBanMembers();
	} else if (const auto channel = _peer->asChannel()) {
		return channel->canBanMembers();
	}
	Unexpected("Peer in ParticipantsAdditionalData::canRestrictParticipant.");
}

bool ParticipantsAdditionalData::canRemoveParticipant(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (canRestrictParticipant(participant)) {
		return true;
	} else if (const auto chat = _peer->asChat()) {
		return user
			&& !user->isSelf()
			&& chat->invitedByMe.contains(user)
			&& (chat->amCreator() || !_admins.contains(user));
	}
	return false;
}

auto ParticipantsAdditionalData::adminRights(
	not_null<UserData*> user) const
-> std::optional<MTPChatAdminRights> {
	if (const auto chat = _peer->asChat()) {
		return _admins.contains(user)
			? std::make_optional(MTPChatAdminRights(MTP_chatAdminRights(
				MTP_flags(chat->defaultAdminRights(user)))))
			: std::nullopt;
	}
	const auto i = _adminRights.find(user);
	return (i != end(_adminRights))
		? std::make_optional(i->second)
		: std::nullopt;
}

QString ParticipantsAdditionalData::adminRank(
		not_null<UserData*> user) const {
	const auto i = _adminRanks.find(user);
	return (i != end(_adminRanks)) ? i->second : QString();
}

auto ParticipantsAdditionalData::restrictedRights(
	not_null<PeerData*> participant) const
-> std::optional<MTPChatBannedRights> {
	if (_peer->isChat()) {
		return std::nullopt;
	}
	const auto i = _restrictedRights.find(participant);
	return (i != end(_restrictedRights))
		? std::make_optional(i->second)
		: std::nullopt;
}

bool ParticipantsAdditionalData::isCreator(not_null<UserData*> user) const {
	return (_creator == user);
}

bool ParticipantsAdditionalData::isExternal(
		not_null<PeerData*> participant) const {
	return _peer->isChat()
		? (participant->isUser()
			&& !_members.contains(participant->asUser()))
		: _external.find(participant) != end(_external);
}

bool ParticipantsAdditionalData::isKicked(
		not_null<PeerData*> participant) const {
	return _peer->isChat()
		? false
		: _kicked.find(participant) != end(_kicked);
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
		not_null<PeerData*> participant) const {
	if (_peer->isChat()) {
		return nullptr;
	}
	const auto i = _restrictedBy.find(participant);
	return (i != end(_restrictedBy)) ? i->second.get() : nullptr;
}

void ParticipantsAdditionalData::setExternal(
		not_null<PeerData*> participant) {
	if (const auto user = participant->asUser()) {
		_adminRights.erase(user);
		_adminCanEdit.erase(user);
		_adminPromotedBy.erase(user);
		_admins.erase(user);
	}
	_restrictedRights.erase(participant);
	_kicked.erase(participant);
	_restrictedBy.erase(participant);
	_infoNotLoaded.erase(participant);
	_external.emplace(participant);
}

void ParticipantsAdditionalData::checkForLoaded(
		not_null<PeerData*> participant) {
	const auto contains = [](const auto &map, const auto &value) {
		return map.find(value) != map.end();
	};
	const auto user = participant->asUser();
	if (!(user && _creator == user)
		&& !(user && contains(_adminRights, user))
		&& !contains(_restrictedRights, participant)
		&& !contains(_external, participant)
		&& !contains(_kicked, participant)) {
		_infoNotLoaded.emplace(participant);
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
		_adminRanks[information->creator] = information->creatorRank;
	}
	for (const auto user : information->lastParticipants) {
		const auto admin = information->lastAdmins.find(user);
		const auto rank = information->admins.find(peerToUser(user->id));
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
			if (rank != end(information->admins)
				&& !rank->second.isEmpty()) {
				_adminRanks[user] = rank->second;
			}
		} else if (restricted != information->lastRestricted.cend()) {
			_adminRights.erase(user);
			_adminCanEdit.erase(user);
			_adminPromotedBy.erase(user);
			_restrictedRights.emplace(user, restricted->second.rights);
		}
	}
}

PeerData *ParticipantsAdditionalData::applyParticipant(
		const MTPChannelParticipant &data) {
	return applyParticipant(data, _role);
}

PeerData *ParticipantsAdditionalData::applyParticipant(
		const MTPChannelParticipant &data,
		Role overrideRole) {
	const auto logBad = [&]() -> PeerData* {
		LOG(("API Error: Bad participant type %1 got "
			"while requesting for participants, role: %2"
			).arg(data.type()
			).arg(static_cast<int>(overrideRole)));
		return nullptr;
	};

	return data.match([&](
			const MTPDchannelParticipantCreator &data) -> PeerData* {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members
			&& overrideRole != Role::Admins) {
			return logBad();
		}
		return applyCreator(data);
	}, [&](const MTPDchannelParticipantAdmin &data) -> PeerData* {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members
			&& overrideRole != Role::Admins) {
			return logBad();
		}
		return applyAdmin(data);
	}, [&](const MTPDchannelParticipantSelf &data) -> PeerData* {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members) {
			return logBad();
		}
		return applyRegular(data.vuser_id());
	}, [&](const MTPDchannelParticipant &data) -> PeerData* {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members) {
			return logBad();
		}
		return applyRegular(data.vuser_id());
	}, [&](const MTPDchannelParticipantBanned &data) {
		if (overrideRole != Role::Profile
			&& overrideRole != Role::Members
			&& overrideRole != Role::Restricted
			&& overrideRole != Role::Kicked) {
			return logBad();
		}
		return applyBanned(data);
	}, [&](const MTPDchannelParticipantLeft &data) {
		return logBad();
	});
}

UserData *ParticipantsAdditionalData::applyCreator(
		const MTPDchannelParticipantCreator &data) {
	if (const auto user = applyRegular(data.vuser_id())) {
		_creator = user;
		_adminRights[user] = data.vadmin_rights();
		if (user->isSelf()) {
			_adminCanEdit.emplace(user);
		} else {
			_adminCanEdit.erase(user);
		}
		if (const auto rank = data.vrank()) {
			_adminRanks[user] = qs(*rank);
		} else {
			_adminRanks.remove(user);
		}
		return user;
	}
	return nullptr;
}

UserData *ParticipantsAdditionalData::applyAdmin(
		const MTPDchannelParticipantAdmin &data) {
	const auto user = _peer->owner().userLoaded(data.vuser_id().v);
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
	_adminRights[user] = data.vadmin_rights();
	if (data.is_can_edit()) {
		_adminCanEdit.emplace(user);
	} else {
		_adminCanEdit.erase(user);
	}
	if (const auto rank = data.vrank()) {
		_adminRanks[user] = qs(*rank);
	} else {
		_adminRanks.remove(user);
	}
	if (const auto by = _peer->owner().userLoaded(data.vpromoted_by().v)) {
		const auto i = _adminPromotedBy.find(user);
		if (i == _adminPromotedBy.end()) {
			_adminPromotedBy.emplace(user, by);
		} else {
			i->second = by;
		}
	} else {
		LOG(("API Error: No user %1 for admin promoted by."
			).arg(data.vpromoted_by().v));
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

PeerData *ParticipantsAdditionalData::applyBanned(
		const MTPDchannelParticipantBanned &data) {
	const auto participant = _peer->owner().peerLoaded(
		peerFromMTP(data.vpeer()));
	if (!participant) {
		return nullptr;
	}

	_infoNotLoaded.erase(participant);
	if (const auto user = participant->asUser()) {
		_adminRights.erase(user);
		_adminCanEdit.erase(user);
		_adminPromotedBy.erase(user);
	}
	if (data.is_left()) {
		_kicked.emplace(participant);
	} else {
		_kicked.erase(participant);
	}
	_restrictedRights[participant] = data.vbanned_rights();
	if (const auto by = _peer->owner().userLoaded(data.vkicked_by().v)) {
		const auto i = _restrictedBy.find(participant);
		if (i == _restrictedBy.end()) {
			_restrictedBy.emplace(participant, by);
		} else {
			i->second = by;
		}
	}
	return participant;
}

void ParticipantsAdditionalData::migrate(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	_peer = channel;
	fillFromChannel(channel);

	for (const auto user : _admins) {
		_adminRights.emplace(
			user,
			MTP_chatAdminRights(MTP_flags(chat->defaultAdminRights(user))));
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
	peer->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::OnlineStatus
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		const auto peerId = update.peer->id;
		if (const auto row = _delegate->peerListFindRow(peerId)) {
			row->refreshStatus();
			sortDelayed();
		}
	}, _lifetime);
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
			|| (channel->membersCount()
				> channel->session().serverConfig().chatSizeMax))) {
		_onlineCount = 0;
		return;
	}
	const auto now = base::unixtime::now();
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
	const auto now = base::unixtime::now();
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
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Role role)
: ParticipantsBoxController(CreateTag(), navigation, peer, role) {
}

ParticipantsBoxController::ParticipantsBoxController(
	CreateTag,
	Window::SessionNavigation *navigation,
	not_null<PeerData*> peer,
	Role role)
: PeerListController(CreateSearchController(peer, role, &_additional))
, _navigation(navigation)
, _peer(peer)
, _api(&_peer->session().mtp())
, _role(role)
, _additional(peer, _role) {
	subscribeToMigration();
	if (_role == Role::Profile) {
		setupListChangeViewers();
	}
	if (const auto channel = _peer->asChannel()) {
		subscribeToCreatorChange(channel);
	}
}

Main::Session &ParticipantsBoxController::session() const {
	return _peer->session();
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
		} else if (auto row = createRow(user)) {
			delegate()->peerListPrependRow(std::move(row));
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
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Role role) {
	auto controller = std::make_unique<ParticipantsBoxController>(
		navigation,
		peer,
		role);
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });

		const auto chat = peer->asChat();
		const auto channel = peer->asChannel();
		const auto canAddNewItem = [&] {
			Assert(chat != nullptr || channel != nullptr);

			switch (role) {
			case Role::Members:
				return chat
					? chat->canAddMembers()
					: (channel->canAddMembers()
						&& (channel->isMegagroup()
							|| (channel->membersCount()
								< channel->session().serverConfig().chatSizeMax)));
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
		auto addNewItemText = [&] {
			switch (role) {
			case Role::Members:
				return (chat || channel->isMegagroup())
					? tr::lng_channel_add_members()
					: tr::lng_channel_add_users();
			case Role::Admins:
				return tr::lng_channel_add_admin();
			case Role::Restricted:
				return tr::lng_channel_add_exception();
			case Role::Kicked:
				return tr::lng_channel_add_removed();
			}
			Unexpected("Role value in ParticipantsBoxController::Start()");
		}();
		if (canAddNewItem) {
			box->addLeftButton(std::move(addNewItemText), [=] {
				controller->addNewItem();
			});
		}
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), initBox),
		Ui::LayerOption::KeepOther);
}

void ParticipantsBoxController::addNewItem() {
	Expects(_role != Role::Profile);

	if (_role == Role::Members) {
		addNewParticipants();
		return;
	}
	const auto adminDone = crl::guard(this, [=](
			not_null<UserData*> user,
			const MTPChatAdminRights &rights,
			const QString &rank) {
		editAdminDone(user, rights, rank);
	});
	const auto restrictedDone = crl::guard(this, [=](
			not_null<PeerData*> participant,
			const MTPChatBannedRights &rights) {
		editRestrictedDone(participant, rights);
	});
	const auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};

	_addBox = Ui::show(
		Box<PeerListBox>(
			std::make_unique<AddSpecialBoxController>(
				_peer,
				_role,
				adminDone,
				restrictedDone),
			initBox),
		Ui::LayerOption::KeepOther);
}

void ParticipantsBoxController::addNewParticipants() {
	Expects(_navigation != nullptr);

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (chat) {
		AddParticipantsBoxController::Start(_navigation, chat);
	} else if (channel->isMegagroup()
		|| (channel->membersCount()
			< channel->session().serverConfig().chatSizeMax)) {
		const auto count = delegate()->peerListFullRowsCount();
		auto already = std::vector<not_null<UserData*>>();
		already.reserve(count);
		for (auto i = 0; i != count; ++i) {
			const auto participant = delegate()->peerListRowAt(i)->peer();
			if (const auto user = participant->asUser()) {
				already.emplace_back(user);
			}
		}
		AddParticipantsBoxController::Start(
			_navigation,
			channel,
			{ already.begin(), already.end() });
	} else {
		Ui::show(Box<MaxInviteBox>(channel), Ui::LayerOption::KeepOther);
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
		chat->session().changes().peerUpdates(
			chat,
			Data::PeerUpdate::Flag::Members
		) | rpl::start_with_next([=] {
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
			_api.request(requestId).cancel();
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
	auto title = [&] {
		switch (_role) {
		case Role::Admins: return tr::lng_channel_admins();
		case Role::Profile:
		case Role::Members: return (_peer->isChannel() && !_peer->isMegagroup()
			? tr::lng_profile_subscribers_section()
			: tr::lng_profile_participants_section());
		case Role::Restricted: return tr::lng_exceptions_list_title();
		case Role::Kicked: return tr::lng_removed_list_title();
		}
		Unexpected("Role in ParticipantsBoxController::prepare()");
	}();
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(std::move(title));
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));

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

	using UpdateFlag = Data::PeerUpdate::Flag;
	chat->session().changes().peerUpdates(
		chat,
		UpdateFlag::Members | UpdateFlag::Admins
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		_additional.fillFromPeer();
		if ((update.flags & UpdateFlag::Members)
			|| (_role == Role::Admins)) {
			rebuildChatRows(chat);
		}
		if (update.flags & UpdateFlag::Admins) {
			rebuildRowTypes();
		}
	}, lifetime());
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
		Assert(row->peer()->isUser());
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

	auto list = ranges::views::all(chat->admins) | ranges::to_vector;
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
		if (!_allLoaded && !delegate()->peerListFullRowsCount()) {
			chatListReady();
		}
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
			return MTP_channelParticipantsBanned(MTP_string());
		}
		return MTP_channelParticipantsKicked(MTP_string());
	}();

	// First query is small and fast, next loads a lot of rows.
	const auto perPage = (_offset > 0)
		? kParticipantsPerPage
		: kParticipantsFirstPageCount;
	const auto participantsHash = 0;

	_loadRequestId = _api.request(MTPchannels_GetParticipants(
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
				if (const auto participant = _additional.applyParticipant(
						data)) {
					appendRow(participant);
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
	}).fail([this](const MTP::Error &error) {
		_loadRequestId = 0;
	}).send();
}

void ParticipantsBoxController::refreshDescription() {
	setDescriptionText((_role == Role::Kicked)
		? ((_peer->isChat() || _peer->isMegagroup())
			? tr::lng_group_removed_list_about
			: tr::lng_channel_removed_list_about)(tr::now)
		: (delegate()->peerListFullRowsCount() > 0)
		? QString()
		: tr::lng_blocked_list_not_found(tr::now));
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

	auto added = false;
	_additional.fillFromPeer();
	for (const auto user : info->lastParticipants) {
		if (appendRow(user)) {
			added = true;
		}

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
	return added;
}

void ParticipantsBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto participant = row->peer();
	const auto user = participant->asUser();
	if (_role == Role::Admins) {
		Assert(user != nullptr);
		showAdmin(user);
	} else if (_role == Role::Restricted
		&& (_peer->isChat() || _peer->isMegagroup())
		&& user) {
		showRestricted(user);
	} else {
		Assert(_navigation != nullptr);
		_navigation->showPeerInfo(participant);
	}
}

void ParticipantsBoxController::rowActionClicked(
		not_null<PeerListRow*> row) {
	const auto participant = row->peer();
	const auto user = participant->asUser();
	if (_role == Role::Members || _role == Role::Profile) {
		kickParticipant(participant);
	} else if (_role == Role::Admins) {
		Assert(user != nullptr);
		removeAdmin(user);
	} else {
		removeKicked(row, participant);
	}
}

base::unique_qptr<Ui::PopupMenu> ParticipantsBoxController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto participant = row->peer();
	const auto user = participant->asUser();
	auto result = base::make_unique_q<Ui::PopupMenu>(parent);
	if (_navigation) {
		result->addAction(
			tr::lng_context_view_profile(tr::now),
			crl::guard(this, [=] {
				_navigation->showPeerInfo(participant); }));
	}
	if (_role == Role::Kicked) {
		if (_peer->isMegagroup()
			&& _additional.canRestrictParticipant(participant)) {
			if (user && channel->canAddMembers()) {
				result->addAction(
					tr::lng_context_add_to_group(tr::now),
					crl::guard(this, [=] { unkickParticipant(user); }));
			}
			result->addAction(
				tr::lng_profile_delete_removed(tr::now),
				crl::guard(this, [=] { removeKickedWithRow(participant); }));
		}
		return result;
	}
	if (user && _additional.canAddOrEditAdmin(user)) {
		const auto isAdmin = _additional.isCreator(user)
			|| _additional.adminRights(user).has_value();
		result->addAction(
			(isAdmin
				? tr::lng_context_edit_permissions
				: tr::lng_context_promote_admin)(tr::now),
			crl::guard(this, [=] { showAdmin(user); }));
	}
	if (_additional.canRestrictParticipant(participant)) {
		const auto canRestrictWithoutKick = [&] {
			if (const auto chat = _peer->asChat()) {
				return chat->amCreator();
			}
			return _peer->isMegagroup();
		}();
		if (canRestrictWithoutKick) {
			result->addAction(
				tr::lng_context_restrict_user(tr::now),
				crl::guard(this, [=] { showRestricted(user); }));
		}
	}
	if (_additional.canRemoveParticipant(participant)) {
		if (!_additional.isKicked(participant)) {
			const auto isGroup = _peer->isChat() || _peer->isMegagroup();
			result->addAction(
				(isGroup
					? tr::lng_context_remove_from_group
					: tr::lng_profile_kick)(tr::now),
				crl::guard(this, [=] { kickParticipant(user); }));
		}
	}
	return result;
}

void ParticipantsBoxController::showAdmin(not_null<UserData*> user) {
	const auto adminRights = _additional.adminRights(user);
	const auto currentRights = adminRights
		? *adminRights
		: MTPChatAdminRights(MTP_chatAdminRights(MTP_flags(0)));
	auto box = Box<EditAdminBox>(
		_peer,
		user,
		currentRights,
		_additional.adminRank(user));
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (_additional.canAddOrEditAdmin(user)) {
		const auto done = crl::guard(this, [=](
				const MTPChatAdminRights &newRights,
				const QString &rank) {
			editAdminDone(user, newRights, rank);
		});
		const auto fail = crl::guard(this, [=] {
			if (_editParticipantBox) {
				_editParticipantBox->closeBox();
			}
		});
		box->setSaveCallback(SaveAdminCallback(_peer, user, done, fail));
	}
	_editParticipantBox = Ui::show(std::move(box), Ui::LayerOption::KeepOther);
}

void ParticipantsBoxController::editAdminDone(
		not_null<UserData*> user,
		const MTPChatAdminRights &rights,
		const QString &rank) {
	_addBox = nullptr;
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto date = base::unixtime::now(); // Incorrect, but ignored.
	if (_additional.isCreator(user) && user->isSelf()) {
		using Flag = MTPDchannelParticipantCreator::Flag;
		_additional.applyParticipant(MTP_channelParticipantCreator(
			MTP_flags(rank.isEmpty() ? Flag(0) : Flag::f_rank),
			MTP_int(user->bareId()),
			rights,
			MTP_string(rank)));
	} else if (rights.c_chatAdminRights().vflags().v == 0) {
		_additional.applyParticipant(MTP_channelParticipant(
			MTP_int(user->bareId()),
			MTP_int(date)));
		if (_role == Role::Admins) {
			removeRow(user);
		}
	} else {
		using Flag = MTPDchannelParticipantAdmin::Flag;
		const auto alreadyPromotedBy = _additional.adminPromotedBy(user);
		_additional.applyParticipant(MTP_channelParticipantAdmin(
			MTP_flags(Flag::f_can_edit
				| (rank.isEmpty() ? Flag(0) : Flag::f_rank)),
			MTP_int(user->bareId()),
			MTPint(), // inviter_id
			MTP_int(alreadyPromotedBy
				? alreadyPromotedBy->bareId()
				: user->session().userId()),
			MTP_int(date),
			rights,
			MTP_string(rank)));
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
		: ChannelData::EmptyRestrictedRights(user);
	const auto hasAdminRights = _additional.adminRights(user).has_value();
	auto box = Box<EditRestrictedBox>(
		_peer,
		user,
		hasAdminRights,
		currentRights);
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (_additional.canRestrictParticipant(user)) {
		const auto done = crl::guard(this, [=](
				const MTPChatBannedRights &newRights) {
			editRestrictedDone(user, newRights);
		});
		const auto fail = crl::guard(this, [=] {
			if (_editParticipantBox) {
				_editParticipantBox->closeBox();
			}
		});
		box->setSaveCallback(
			SaveRestrictedCallback(_peer, user, done, fail));
	}
	_editParticipantBox = Ui::show(std::move(box), Ui::LayerOption::KeepOther);
}

void ParticipantsBoxController::editRestrictedDone(
		not_null<PeerData*> participant,
		const MTPChatBannedRights &rights) {
	_addBox = nullptr;
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto user = participant->asUser();
	const auto date = base::unixtime::now(); // Incorrect, but ignored.
	if (Data::ChatBannedRightsFlags(rights) == 0) {
		if (user) {
			_additional.applyParticipant(MTP_channelParticipant(
				MTP_int(user->bareId()),
				MTP_int(date)));
		} else {
			_additional.setExternal(participant);
		}
		if (_role == Role::Kicked || _role == Role::Restricted) {
			removeRow(participant);
		}
	} else {
		const auto kicked = Data::ChatBannedRightsFlags(rights)
			& ChatRestriction::f_view_messages;
		const auto alreadyRestrictedBy = _additional.restrictedBy(
			participant);
		_additional.applyParticipant(MTP_channelParticipantBanned(
			MTP_flags(kicked
				? MTPDchannelParticipantBanned::Flag::f_left
				: MTPDchannelParticipantBanned::Flag(0)),
			(participant->isUser()
				? MTP_peerUser(MTP_int(participant->bareId()))
				: participant->isChat()
				? MTP_peerChat(MTP_int(participant->bareId()))
				: MTP_peerChannel(MTP_int(participant->bareId()))),
			MTP_int(alreadyRestrictedBy
				? alreadyRestrictedBy->bareId()
				: participant->session().userId()),
			MTP_int(date),
			rights));
		if (kicked) {
			if (_role == Role::Kicked) {
				prependRow(participant);
			} else if (_role == Role::Admins
				|| _role == Role::Restricted
				|| _role == Role::Members) {
				removeRow(participant);
			}
		} else {
			if (_role == Role::Restricted) {
				prependRow(participant);
			} else if (_role == Role::Kicked
				|| _role == Role::Admins
				|| _role == Role::Members) {
				removeRow(participant);
			}
		}
	}
	recomputeTypeFor(participant);
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::kickParticipant(not_null<PeerData*> participant) {
	const auto user = participant->asUser();
	const auto text = ((_peer->isChat() || _peer->isMegagroup())
		? tr::lng_profile_sure_kick
		: tr::lng_profile_sure_kick_channel)(
			tr::now,
			lt_user,
			user ? user->firstName : participant->name);
	_editBox = Ui::show(
		Box<ConfirmBox>(
			text,
			tr::lng_box_remove(tr::now),
			crl::guard(this, [=] { kickParticipantSure(participant); })),
		Ui::LayerOption::KeepOther);
}

void ParticipantsBoxController::unkickParticipant(not_null<UserData*> user) {
	_editBox = nullptr;
	if (const auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
	_peer->session().api().addChatParticipants(_peer, { 1, user });
}

void ParticipantsBoxController::kickParticipantSure(
		not_null<PeerData*> participant) {
	_editBox = nullptr;

	const auto restrictedRights = _additional.restrictedRights(participant);
	const auto currentRights = restrictedRights
		? *restrictedRights
		: ChannelData::EmptyRestrictedRights(participant);

	if (const auto row = delegate()->peerListFindRow(participant->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
	auto &session = _peer->session();
	if (const auto chat = _peer->asChat()) {
		session.api().kickParticipant(chat, participant);
	} else if (const auto channel = _peer->asChannel()) {
		session.api().kickParticipant(channel, participant, currentRights);
	}
}

void ParticipantsBoxController::removeAdmin(not_null<UserData*> user) {
	_editBox = Ui::show(
		Box<ConfirmBox>(
			tr::lng_profile_sure_remove_admin(
				tr::now,
				lt_user,
				user->firstName),
			tr::lng_box_remove(tr::now),
			crl::guard(this, [=] { removeAdminSure(user); })),
		Ui::LayerOption::KeepOther);
}

void ParticipantsBoxController::removeAdminSure(not_null<UserData*> user) {
	_editBox = nullptr;

	if (const auto chat = _peer->asChat()) {
		SaveChatAdmin(chat, user, false, crl::guard(this, [=] {
			editAdminDone(
				user,
				MTP_chatAdminRights(MTP_flags(0)),
				QString());
		}), nullptr);
	} else if (const auto channel = _peer->asChannel()) {
		const auto adminRights = _additional.adminRights(user);
		if (!adminRights) {
			return;
		}
		RemoveAdmin(channel, user, *adminRights, crl::guard(this, [=] {
			editAdminDone(
				user,
				MTP_chatAdminRights(MTP_flags(0)),
				QString());
		}), nullptr);
	}
}

void ParticipantsBoxController::removeKickedWithRow(
		not_null<PeerData*> participant) {
	if (const auto row = delegate()->peerListFindRow(participant->id)) {
		removeKicked(row, participant);
	} else {
		removeKicked(participant);
	}
}
void ParticipantsBoxController::removeKicked(
		not_null<PeerData*> participant) {
	if (const auto channel = _peer->asChannel()) {
		channel->session().api().unblockParticipant(channel, participant);
	}
}

void ParticipantsBoxController::removeKicked(
		not_null<PeerListRow*> row,
		not_null<PeerData*> participant) {
	delegate()->peerListRemoveRow(row);
	if (_role != Role::Kicked
		&& !delegate()->peerListFullRowsCount()) {
		setDescriptionText(tr::lng_blocked_list_not_found(tr::now));
	}
	delegate()->peerListRefreshRows();
	removeKicked(participant);
}

bool ParticipantsBoxController::appendRow(not_null<PeerData*> participant) {
	if (delegate()->peerListFindRow(participant->id)) {
		recomputeTypeFor(participant);
		return false;
	} else if (auto row = createRow(participant)) {
		delegate()->peerListAppendRow(std::move(row));
		if (_role != Role::Kicked) {
			setDescriptionText(QString());
		}
		return true;
	}
	return false;
}

bool ParticipantsBoxController::prependRow(not_null<PeerData*> participant) {
	if (const auto row = delegate()->peerListFindRow(participant->id)) {
		recomputeTypeFor(participant);
		refreshCustomStatus(row);
		if (_role == Role::Admins) {
			// Perhaps we've added a new admin from search.
			delegate()->peerListPrependRowFromSearchResult(row);
		}
		return false;
	} else if (auto row = createRow(participant)) {
		delegate()->peerListPrependRow(std::move(row));
		if (_role != Role::Kicked) {
			setDescriptionText(QString());
		}
		return true;
	}
	return false;
}

bool ParticipantsBoxController::removeRow(not_null<PeerData*> participant) {
	if (auto row = delegate()->peerListFindRow(participant->id)) {
		if (_role == Role::Admins) {
			// Perhaps we are removing an admin from search results.
			row->setCustomStatus(tr::lng_channel_admin_status_not_admin(tr::now));
			delegate()->peerListConvertRowToSearchResult(row);
		} else {
			delegate()->peerListRemoveRow(row);
		}
		if (_role != Role::Kicked
			&& !delegate()->peerListFullRowsCount()) {
			setDescriptionText(tr::lng_blocked_list_not_found(tr::now));
		}
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (_role == Role::Profile) {
		Assert(user != nullptr);
		return std::make_unique<Row>(user, computeType(user));
	}
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	auto row = std::make_unique<PeerListRowWithLink>(participant);
	refreshCustomStatus(row.get());
	if (_role == Role::Admins
		&& user
		&& !_additional.isCreator(user)
		&& _additional.adminRights(user).has_value()
		&& _additional.canEditAdmin(user)) {
		row->setActionLink(tr::lng_profile_kick(tr::now));
	} else if (_role == Role::Kicked || _role == Role::Restricted) {
		if (_additional.canRestrictParticipant(participant)) {
			row->setActionLink(tr::lng_profile_delete_removed(tr::now));
		}
	} else if (_role == Role::Members) {
		Assert(user != nullptr);
		if ((chat ? chat->canBanMembers() : channel->canBanMembers())
			&& !_additional.isCreator(user)
			&& (!_additional.adminRights(user)
				|| _additional.canEditAdmin(user))) {
			row->setActionLink(tr::lng_profile_kick(tr::now));
		}
		if (_role == Role::Members && user->isBot()) {
			auto seesAllMessages = (user->botInfo->readsAllHistory || _additional.adminRights(user).has_value());
			row->setCustomStatus(seesAllMessages
				? tr::lng_status_bot_reads_all(tr::now)
				: tr::lng_status_bot_not_reads_all(tr::now));
		}
	}
	return row;
}

auto ParticipantsBoxController::computeType(
		not_null<PeerData*> participant) const -> Type {
	const auto user = participant->asUser();
	auto result = Type();
	result.rights = (user && _additional.isCreator(user))
		? Rights::Creator
		: (user && _additional.adminRights(user).has_value())
		? Rights::Admin
		: Rights::Normal;
	result.canRemove = _additional.canRemoveParticipant(participant);
	return result;
}

void ParticipantsBoxController::recomputeTypeFor(
		not_null<PeerData*> participant) {
	if (_role != Role::Profile) {
		return;
	}
	if (const auto row = delegate()->peerListFindRow(participant->id)) {
		static_cast<Row*>(row)->setType(computeType(participant));
	}
}

void ParticipantsBoxController::refreshCustomStatus(
		not_null<PeerListRow*> row) const {
	const auto participant = row->peer();
	const auto user = participant->asUser();
	if (_role == Role::Admins) {
		Assert(user != nullptr);
		if (const auto by = _additional.adminPromotedBy(user)) {
			row->setCustomStatus(tr::lng_channel_admin_status_promoted_by(
				tr::now,
				lt_user,
				by->name));
		} else {
			if (_additional.isCreator(user)) {
				row->setCustomStatus(
					tr::lng_channel_admin_status_creator(tr::now));
			} else {
				row->setCustomStatus(
					tr::lng_channel_admin_status_not_admin(tr::now));
			}
		}
	} else if (_role == Role::Kicked || _role == Role::Restricted) {
		const auto by = _additional.restrictedBy(participant);
		row->setCustomStatus((_role == Role::Kicked
			? tr::lng_channel_banned_status_removed_by
			: tr::lng_channel_banned_status_restricted_by)(
				tr::now,
				lt_user,
				by ? by->name : "Unknown"));
	}
}

void ParticipantsBoxController::subscribeToMigration() {
	const auto chat = _peer->asChat();
	if (!chat) {
		return;
	}
	SubscribeToMigration(
		chat,
		lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(chat, channel); });
}

void ParticipantsBoxController::migrate(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	_peer = channel;
	_additional.migrate(chat, channel);
	subscribeToCreatorChange(channel);
}

void ParticipantsBoxController::subscribeToCreatorChange(
		not_null<ChannelData*> channel) {
	const auto isCreator = channel->amCreator();
	channel->flagsValue(
	) | rpl::filter([](const ChannelData::Flags::Change &change) {
		return (change.diff & MTPDchannel::Flag::f_creator);
	}) | rpl::filter([=] {
		return (isCreator != channel->amCreator());
	}) | rpl::start_with_next([=] {
		if (channel->isBroadcast()) {
			fullListRefresh();
			return;
		}
		const auto weak = base::make_weak(this);
		const auto api = &channel->session().api();
		api->request(MTPchannels_GetParticipants(
			channel->inputChannel,
			MTP_channelParticipantsRecent(),
			MTP_int(0),
			MTP_int(channel->session().serverConfig().chatSizeMax),
			MTP_int(0)
		)).done([=](const MTPchannels_ChannelParticipants &result) {
			if (channel->amCreator()) {
				channel->mgInfo->creator = channel->session().user().get();
			}
			channel->mgInfo->lastAdmins.clear();
			channel->mgInfo->lastRestricted.clear();
			channel->mgInfo->lastParticipants.clear();
			api->parseRecentChannelParticipants(channel, result);
			if (weak) {
				fullListRefresh();
			}
		}).send();
	}, lifetime());
}

void ParticipantsBoxController::fullListRefresh() {
	_additional = ParticipantsAdditionalData(_peer, _role);

	while (const auto count = delegate()->peerListFullRowsCount()) {
		delegate()->peerListRemoveRow(
			delegate()->peerListRowAt(count - 1));
	}
	loadMoreRows();
	delegate()->peerListRefreshRows();
}

ParticipantsBoxSearchController::ParticipantsBoxSearchController(
	not_null<ChannelData*> channel,
	Role role,
	not_null<ParticipantsAdditionalData*> additional)
: _channel(channel)
, _role(role)
, _additional(additional)
, _api(&_channel->session().mtp()) {
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
	return result;
}

void ParticipantsBoxSearchController::restoreState(
		std::unique_ptr<SavedStateBase> state) {
	if (auto my = dynamic_cast<SavedState*>(state.get())) {
		if (auto requestId = base::take(_requestId)) {
			_api.request(requestId).cancel();
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

	_requestId = _api.request(MTPchannels_GetParticipants(
		_channel->inputChannel,
		filter,
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](
			const MTPchannels_ChannelParticipants &result,
			mtpRequestId requestId) {
		searchDone(requestId, result, perPage);
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
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
		const auto &list = data.vparticipants().v;
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
