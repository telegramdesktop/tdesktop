/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat.h"

#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_message_reactions.h"
#include "history/history.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_invite_links.h"

namespace {

using UpdateFlag = Data::PeerUpdate::Flag;

} // namespace

ChatData::ChatData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id)
, inputChat(MTP_long(peerToChat(id).bare)) {
	_flags.changes(
	) | rpl::start_with_next([=](const Flags::Change &change) {
		if (change.diff & Flag::CallNotEmpty) {
			if (const auto history = this->owner().historyLoaded(this)) {
				history->updateChatListEntry();
			}
		}
	}, _lifetime);
}

void ChatData::setPhoto(const MTPChatPhoto &photo) {
	photo.match([&](const MTPDchatPhoto &data) {
		updateUserpic(
			data.vphoto_id().v,
			data.vdc_id().v,
			data.is_has_video());
	}, [&](const MTPDchatPhotoEmpty &) {
		clearUserpic();
	});
}

ChatAdminRightsInfo ChatData::defaultAdminRights(not_null<UserData*> user) {
	const auto isCreator = (creator == peerToUser(user->id))
		|| (user->isSelf() && amCreator());
	using Flag = ChatAdminRight;
	return ChatAdminRightsInfo(Flag::Other
		| Flag::ChangeInfo
		| Flag::DeleteMessages
		| Flag::BanUsers
		| Flag::InviteUsers
		| Flag::PinMessages
		| Flag::ManageCall
		| (isCreator ? Flag::AddAdmins : Flag(0)));
}

bool ChatData::canWrite() const {
	// Duplicated in Data::CanWriteValue().
	return amIn() && !amRestricted(ChatRestriction::SendMessages);
}

bool ChatData::allowsForwarding() const {
	return !(flags() & Flag::NoForwards);
}

bool ChatData::canEditInformation() const {
	return amIn() && !amRestricted(ChatRestriction::ChangeInfo);
}

bool ChatData::canEditPermissions() const {
	return amIn()
		&& (amCreator() || (adminRights() & ChatAdminRight::BanUsers));
}

bool ChatData::canEditUsername() const {
	return amCreator()
		&& (flags() & Flag::CanSetUsername);
}

bool ChatData::canEditPreHistoryHidden() const {
	return amCreator();
}

bool ChatData::canDeleteMessages() const {
	return amCreator()
		|| (adminRights() & ChatAdminRight::DeleteMessages);
}

bool ChatData::canAddMembers() const {
	return amIn() && !amRestricted(ChatRestriction::InviteUsers);
}

bool ChatData::canSendPolls() const {
	return amIn() && !amRestricted(ChatRestriction::SendPolls);
}

bool ChatData::canAddAdmins() const {
	return amIn() && amCreator();
}

bool ChatData::canBanMembers() const {
	return amCreator()
		|| (adminRights() & ChatAdminRight::BanUsers);
}

bool ChatData::anyoneCanAddMembers() const {
	return !(defaultRestrictions() & ChatRestriction::InviteUsers);
}

void ChatData::setName(const QString &newName) {
	updateNameDelayed(newName.isEmpty() ? name() : newName, {}, {});
}

void ChatData::applyEditAdmin(not_null<UserData*> user, bool isAdmin) {
	if (isAdmin) {
		admins.emplace(user);
	} else {
		admins.remove(user);
	}
	session().changes().peerUpdated(this, UpdateFlag::Admins);
}

void ChatData::invalidateParticipants() {
	participants.clear();
	admins.clear();
	setAdminRights(ChatAdminRights());
	//setDefaultRestrictions(ChatRestrictions());
	invitedByMe.clear();
	botStatus = 0;
	session().changes().peerUpdated(
		this,
		UpdateFlag::Members | UpdateFlag::Admins);
}

void ChatData::setInviteLink(const QString &newInviteLink) {
	_inviteLink = newInviteLink;
}

bool ChatData::canHaveInviteLink() const {
	return amCreator()
		|| (adminRights() & ChatAdminRight::InviteUsers);
}

void ChatData::setAdminRights(ChatAdminRights rights) {
	if (rights == adminRights()) {
		return;
	}
	_adminRights.set(rights);
	if (!canHaveInviteLink()) {
		setPendingRequestsCount(0, std::vector<UserId>{});
	}
	session().changes().peerUpdated(
		this,
		UpdateFlag::Rights | UpdateFlag::Admins | UpdateFlag::BannedUsers);
}

void ChatData::setDefaultRestrictions(ChatRestrictions rights) {
	if (rights == defaultRestrictions()) {
		return;
	}
	_defaultRestrictions.set(rights);
	session().changes().peerUpdated(this, UpdateFlag::Rights);
}

void ChatData::refreshBotStatus() {
	if (participants.empty()) {
		botStatus = 0;
	} else {
		const auto bot = ranges::none_of(participants, &UserData::isBot);
		botStatus = bot ? -1 : 2;
	}
}

auto ChatData::applyUpdateVersion(int version) -> UpdateStatus {
	if (_version > version) {
		return UpdateStatus::TooOld;
	} else if (_version + 1 < version) {
		invalidateParticipants();
		session().api().requestFullPeer(this);
		return UpdateStatus::Skipped;
	}
	setVersion(version);
	return UpdateStatus::Good;
}

ChannelData *ChatData::getMigrateToChannel() const {
	return _migratedTo;
}

void ChatData::setMigrateToChannel(ChannelData *channel) {
	if (_migratedTo != channel) {
		_migratedTo = channel;
		if (channel->amIn()) {
			session().changes().peerUpdated(this, UpdateFlag::Migration);
		}
	}
}

void ChatData::setGroupCall(
		const MTPInputGroupCall &call,
		TimeId scheduleDate,
		bool rtmp) {
	if (migrateTo()) {
		return;
	}
	call.match([&](const MTPDinputGroupCall &data) {
		if (_call && _call->id() == data.vid().v) {
			return;
		} else if (!_call && !data.vid().v) {
			return;
		} else if (!data.vid().v) {
			clearGroupCall();
			return;
		}
		const auto hasCall = (_call != nullptr);
		if (hasCall) {
			owner().unregisterGroupCall(_call.get());
		}
		_call = std::make_unique<Data::GroupCall>(
			this,
			data.vid().v,
			data.vaccess_hash().v,
			scheduleDate,
			rtmp);
		owner().registerGroupCall(_call.get());
		session().changes().peerUpdated(this, UpdateFlag::GroupCall);
		addFlags(Flag::CallActive);
	});
}

void ChatData::clearGroupCall() {
	if (!_call) {
		return;
	} else if (const auto group = migrateTo(); group && !group->groupCall()) {
		group->migrateCall(base::take(_call));
	} else {
		owner().unregisterGroupCall(_call.get());
		_call = nullptr;
	}
	session().changes().peerUpdated(this, UpdateFlag::GroupCall);
	removeFlags(Flag::CallActive | Flag::CallNotEmpty);
}

void ChatData::setGroupCallDefaultJoinAs(PeerId peerId) {
	_callDefaultJoinAs = peerId;
}

PeerId ChatData::groupCallDefaultJoinAs() const {
	return _callDefaultJoinAs;
}

void ChatData::setBotCommands(const std::vector<Data::BotCommands> &list) {
	if (_botCommands.update(list)) {
		owner().botCommandsChanged(this);
	}
}

void ChatData::setPendingRequestsCount(
		int count,
		const QVector<MTPlong> &recentRequesters) {
	setPendingRequestsCount(count, ranges::views::all(
		recentRequesters
	) | ranges::views::transform([&](const MTPlong &value) {
		return UserId(value);
	}) | ranges::to_vector);
}

void ChatData::setPendingRequestsCount(
		int count,
		std::vector<UserId> recentRequesters) {
	if (_pendingRequestsCount != count
		|| _recentRequesters != recentRequesters) {
		_pendingRequestsCount = count;
		_recentRequesters = std::move(recentRequesters);
		session().changes().peerUpdated(this, UpdateFlag::PendingRequests);
	}
}

void ChatData::setAllowedReactions(base::flat_set<QString> list) {
	if (_allowedReactions != list) {
		const auto toggled = (_allowedReactions.empty() != list.empty());
		_allowedReactions = std::move(list);
		if (toggled) {
			owner().reactions().updateAllInHistory(
				this,
				!_allowedReactions.empty());
		}
		session().changes().peerUpdated(this, UpdateFlag::Reactions);
	}
}

const base::flat_set<QString> &ChatData::allowedReactions() const {
	return _allowedReactions;
}

namespace Data {

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipants &update) {
	ApplyChatUpdate(chat, update.vparticipants());
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipantAdd &update) {
	if (chat->applyUpdateVersion(update.vversion().v)
		!= ChatData::UpdateStatus::Good) {
		return;
	} else if (chat->count < 0) {
		return;
	}
	const auto user = chat->owner().userLoaded(update.vuser_id().v);
	const auto session = &chat->session();
	if (!user
		|| (!chat->participants.empty()
			&& chat->participants.contains(user))) {
		chat->invalidateParticipants();
		++chat->count;
		return;
	}
	if (chat->participants.empty()) {
		if (chat->count > 0) { // If the count is known.
			++chat->count;
		}
		chat->botStatus = 0;
	} else {
		chat->participants.emplace(user);
		if (UserId(update.vinviter_id()) == session->userId()) {
			chat->invitedByMe.insert(user);
		} else {
			chat->invitedByMe.remove(user);
		}
		++chat->count;
		if (user->isBot()) {
			chat->botStatus = 2;
			if (!user->botInfo->inited) {
				session->api().requestFullPeer(user);
			}
		}
	}
	session->changes().peerUpdated(chat, UpdateFlag::Members);
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipantDelete &update) {
	if (chat->applyUpdateVersion(update.vversion().v)
		!= ChatData::UpdateStatus::Good) {
		return;
	} else if (chat->count <= 0) {
		return;
	}
	const auto user = chat->owner().userLoaded(update.vuser_id().v);
	if (!user
		|| (!chat->participants.empty()
			&& !chat->participants.contains(user))) {
		chat->invalidateParticipants();
		--chat->count;
		return;
	}
	if (chat->participants.empty()) {
		if (chat->count > 0) {
			chat->count--;
		}
		chat->botStatus = 0;
	} else {
		chat->participants.erase(user);
		chat->count--;
		chat->invitedByMe.remove(user);
		chat->admins.remove(user);
		if (user->isSelf()) {
			chat->setAdminRights(ChatAdminRights());
		}
		if (const auto history = chat->owner().historyLoaded(chat)) {
			if (history->lastKeyboardFrom == user->id) {
				history->clearLastKeyboard();
			}
		}
		if (chat->botStatus > 0 && user->isBot()) {
			chat->refreshBotStatus();
		}
	}
	chat->session().changes().peerUpdated(chat, UpdateFlag::Members);
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipantAdmin &update) {
	if (chat->applyUpdateVersion(update.vversion().v)
		!= ChatData::UpdateStatus::Good) {
		return;
	}
	const auto session = &chat->session();
	const auto user = chat->owner().userLoaded(update.vuser_id().v);
	if (!user) {
		chat->invalidateParticipants();
		return;
	}
	if (user->isSelf()) {
		chat->setAdminRights(mtpIsTrue(update.vis_admin())
			? chat->defaultAdminRights(user).flags
			: ChatAdminRights());
	}
	if (mtpIsTrue(update.vis_admin())) {
		if (chat->noParticipantInfo()) {
			session->api().requestFullPeer(chat);
		} else {
			chat->admins.emplace(user);
		}
	} else {
		chat->admins.erase(user);
	}
	session->changes().peerUpdated(chat, UpdateFlag::Admins);
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatDefaultBannedRights &update) {
	if (chat->applyUpdateVersion(update.vversion().v)
		!= ChatData::UpdateStatus::Good) {
		return;
	}
	chat->setDefaultRestrictions(ChatRestrictionsInfo(
		update.vdefault_banned_rights()).flags);
}

void ApplyChatUpdate(not_null<ChatData*> chat, const MTPDchatFull &update) {
	ApplyChatUpdate(chat, update.vparticipants());

	if (const auto call = update.vcall()) {
		chat->setGroupCall(*call);
	} else {
		chat->clearGroupCall();
	}
	if (const auto as = update.vgroupcall_default_join_as()) {
		chat->setGroupCallDefaultJoinAs(peerFromMTP(*as));
	} else {
		chat->setGroupCallDefaultJoinAs(0);
	}

	chat->setMessagesTTL(update.vttl_period().value_or_empty());
	if (const auto info = update.vbot_info()) {
		auto &&commands = ranges::views::all(
			info->v
		) | ranges::views::transform(Data::BotCommandsFromTL);
		chat->setBotCommands(std::move(commands) | ranges::to_vector);
	} else {
		chat->setBotCommands({});
	}
	using Flag = ChatDataFlag;
	const auto mask = Flag::CanSetUsername;
	chat->setFlags((chat->flags() & ~mask)
		| (update.is_can_set_username() ? Flag::CanSetUsername : Flag()));
	if (const auto photo = update.vchat_photo()) {
		chat->setUserpicPhoto(*photo);
	} else {
		chat->setUserpicPhoto(MTP_photoEmpty(MTP_long(0)));
	}
	if (const auto invite = update.vexported_invite()) {
		chat->session().api().inviteLinks().setMyPermanent(chat, *invite);
	} else {
		chat->session().api().inviteLinks().clearMyPermanent(chat);
	}
	if (const auto pinned = update.vpinned_msg_id()) {
		SetTopPinnedMessageId(chat, pinned->v);
	}
	chat->checkFolder(update.vfolder_id().value_or_empty());
	chat->setThemeEmoji(qs(update.vtheme_emoticon().value_or_empty()));
	chat->setAllowedReactions(
		Data::Reactions::ParseAllowed(update.vavailable_reactions()));
	chat->fullUpdated();
	chat->setAbout(qs(update.vabout()));
	chat->setPendingRequestsCount(
		update.vrequests_pending().value_or_empty(),
		update.vrecent_requesters().value_or_empty());

	chat->session().api().applyNotifySettings(
		MTP_inputNotifyPeer(chat->input),
		update.vnotify_settings());
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPChatParticipants &participants) {
	const auto session = &chat->session();
	participants.match([&](const MTPDchatParticipantsForbidden &data) {
		if (const auto self = data.vself_participant()) {
			// self->
		}
		chat->count = -1;
		chat->invalidateParticipants();
	}, [&](const MTPDchatParticipants &data) {
		const auto status = chat->applyUpdateVersion(data.vversion().v);
		if (status == ChatData::UpdateStatus::TooOld) {
			return;
		}
		// Even if we skipped some updates, we got current participants
		// and we've requested peer from API to have current rights.
		chat->setVersion(data.vversion().v);

		const auto &list = data.vparticipants().v;
		chat->count = list.size();
		chat->participants.clear();
		chat->invitedByMe.clear();
		chat->admins.clear();
		chat->setAdminRights(ChatAdminRights());
		const auto selfUserId = session->userId();
		for (const auto &participant : list) {
			const auto userId = participant.match([&](const auto &data) {
				return data.vuser_id().v;
			});
			const auto user = chat->owner().userLoaded(userId);
			if (!user) {
				chat->invalidateParticipants();
				break;
			}

			chat->participants.emplace(user);

			const auto inviterId = participant.match([&](
					const MTPDchatParticipantCreator &data) {
				return UserId(0);
			}, [&](const auto &data) {
				return UserId(data.vinviter_id());
			});
			if (inviterId == selfUserId) {
				chat->invitedByMe.insert(user);
			}

			participant.match([&](const MTPDchatParticipantCreator &data) {
				chat->creator = userId;
			}, [&](const MTPDchatParticipantAdmin &data) {
				chat->admins.emplace(user);
				if (user->isSelf()) {
					chat->setAdminRights(
						chat->defaultAdminRights(user).flags);
				}
			}, [](const MTPDchatParticipant &) {
			});
		}
		if (chat->participants.empty()) {
			return;
		}
		if (const auto history = chat->owner().historyLoaded(chat)) {
			if (history->lastKeyboardFrom) {
				const auto i = ranges::find(
					chat->participants,
					history->lastKeyboardFrom,
					&UserData::id);
				if (i == end(chat->participants)) {
					history->clearLastKeyboard();
				}
			}
		}
		chat->refreshBotStatus();
		session->changes().peerUpdated(
			chat,
			UpdateFlag::Members | UpdateFlag::Admins);
	});
}

} // namespace Data
