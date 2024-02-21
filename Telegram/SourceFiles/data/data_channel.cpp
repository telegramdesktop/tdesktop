/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_channel.h"

#include "data/data_changes.h"
#include "data/data_channel_admins.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_icons.h"
#include "data/data_histories.h"
#include "data/data_group_call.h"
#include "data/data_message_reactions.h"
#include "data/data_wall_paper.h"
#include "data/notify/data_notify_settings.h"
#include "main/main_session.h"
#include "main/session/send_as_peers.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "history/history.h"
#include "api/api_chat_invite.h"
#include "api/api_invite_links.h"
#include "apiwrap.h"
#include "window/notifications_manager.h"

namespace {

using UpdateFlag = Data::PeerUpdate::Flag;

} // namespace

MegagroupInfo::MegagroupInfo() = default;

MegagroupInfo::~MegagroupInfo() = default;

ChatData *MegagroupInfo::getMigrateFromChat() const {
	return _migratedFrom;
}

void MegagroupInfo::setMigrateFromChat(ChatData *chat) {
	_migratedFrom = chat;
}

const ChannelLocation *MegagroupInfo::getLocation() const {
	return _location.address.isEmpty() ? nullptr : &_location;
}

void MegagroupInfo::setLocation(const ChannelLocation &location) {
	_location = location;
}

Data::ChatBotCommands::Changed MegagroupInfo::setBotCommands(
		const std::vector<Data::BotCommands> &list) {
	return _botCommands.update(list);
}

void MegagroupInfo::ensureForum(not_null<ChannelData*> that) {
	if (!_forum) {
		const auto history = that->owner().history(that);
		_forum = std::make_unique<Data::Forum>(history);
		history->forumChanged(nullptr);
	}
}

Data::Forum *MegagroupInfo::forum() const {
	return _forum.get();
}

std::unique_ptr<Data::Forum> MegagroupInfo::takeForumData() {
	if (auto result = base::take(_forum)) {
		result->history()->forumChanged(result.get());
		return result;
	}
	return nullptr;
}

ChannelData::ChannelData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id)
, inputChannel(
	MTP_inputChannel(MTP_long(peerToChannel(id).bare), MTP_long(0)))
, _ptsWaiter(&owner->session().updates()) {
}

void ChannelData::setPhoto(const MTPChatPhoto &photo) {
	photo.match([&](const MTPDchatPhoto & data) {
		updateUserpic(
			data.vphoto_id().v,
			data.vdc_id().v,
			data.is_has_video());
	}, [&](const MTPDchatPhotoEmpty &) {
		clearUserpic();
	});
}

void ChannelData::setName(
		const QString &newName,
		const QString &newUsername) {
	updateNameDelayed(newName.isEmpty() ? name() : newName, {}, newUsername);
}

void ChannelData::setUsername(const QString &username) {
	_username.setUsername(username);
}

void ChannelData::setUsernames(const Data::Usernames &newUsernames) {
	const auto wasUsername = username();
	const auto wasUsernames = usernames();
	_username.setUsernames(newUsernames);
	const auto nowUsername = username();
	const auto nowUsernames = usernames();
	session().changes().peerUpdated(
		this,
		UpdateFlag()
		| ((wasUsername != nowUsername)
			? UpdateFlag::Username
			: UpdateFlag())
		| (!ranges::equal(wasUsernames, nowUsernames)
			? UpdateFlag::Usernames
			: UpdateFlag()));
}

QString ChannelData::username() const {
	return _username.username();
}

QString ChannelData::editableUsername() const {
	return _username.editableUsername();
}

const std::vector<QString> &ChannelData::usernames() const {
	return _username.usernames();
}

void ChannelData::setAccessHash(uint64 accessHash) {
	access = accessHash;
	input = MTP_inputPeerChannel(
		MTP_long(peerToChannel(id).bare),
		MTP_long(accessHash));
	inputChannel = MTP_inputChannel(
		MTP_long(peerToChannel(id).bare),
		MTP_long(accessHash));
}

void ChannelData::setFlags(ChannelDataFlags which) {
	const auto diff = flags() ^ which;
	if ((which & Flag::Megagroup) && !mgInfo) {
		mgInfo = std::make_unique<MegagroupInfo>();
	}

	// Let Data::Forum live till the end of _flags.set.
	// That way the data can be used in changes handler.
	// Example: render frame for forum auto-closing animation.
	const auto taken = ((diff & Flag::Forum) && !(which & Flag::Forum))
		? mgInfo->takeForumData()
		: nullptr;
	if ((diff & Flag::Forum) && (which & Flag::Forum)) {
		mgInfo->ensureForum(this);
	}
	_flags.set(which);
	if (diff & (Flag::Left | Flag::Forbidden)) {
		if (const auto chat = getMigrateFromChat()) {
			session().changes().peerUpdated(chat, UpdateFlag::Migration);
			session().changes().peerUpdated(this, UpdateFlag::Migration);
		}
	}
	if (diff & (Flag::Forum | Flag::CallNotEmpty | Flag::SimilarExpanded)) {
		if (const auto history = this->owner().historyLoaded(this)) {
			if (diff & Flag::CallNotEmpty) {
				history->updateChatListEntry();
			}
			if (diff & Flag::Forum) {
				Core::App().notifications().clearFromHistory(history);
				history->updateChatListEntryHeight();
				if (history->inChatList()) {
					if (const auto forum = this->forum()) {
						forum->preloadTopics();
					}
				}
			}
			if (diff & Flag::SimilarExpanded) {
				if (const auto item = history->joinedMessageInstance()) {
					history->owner().requestItemResize(item);
				}
			}
		}
	}
	if (const auto raw = taken.get()) {
		owner().forumIcons().clearUserpicsReset(raw);
	}
}

void ChannelData::addFlags(ChannelDataFlags which) {
	setFlags(flags() | which);
}

void ChannelData::removeFlags(ChannelDataFlags which) {
	setFlags(flags() & ~which);
}

void ChannelData::setInviteLink(const QString &newInviteLink) {
	_inviteLink = newInviteLink;
}

bool ChannelData::canHaveInviteLink() const {
	return amCreator()
		|| (adminRights() & AdminRight::InviteByLinkOrAdd);
}

void ChannelData::setLocation(const MTPChannelLocation &data) {
	if (!mgInfo) {
		return;
	}
	const auto was = mgInfo->getLocation();
	const auto wasValue = was ? *was : ChannelLocation();
	data.match([&](const MTPDchannelLocation &data) {
		data.vgeo_point().match([&](const MTPDgeoPoint &point) {
			mgInfo->setLocation({
				qs(data.vaddress()),
				Data::LocationPoint(point)
			});
		}, [&](const MTPDgeoPointEmpty &) {
			mgInfo->setLocation(ChannelLocation());
		});
	}, [&](const MTPDchannelLocationEmpty &) {
		mgInfo->setLocation(ChannelLocation());
	});
	const auto now = mgInfo->getLocation();
	const auto nowValue = now ? *now : ChannelLocation();
	if (was != now || (was && wasValue != nowValue)) {
		session().changes().peerUpdated(
			this,
			UpdateFlag::ChannelLocation);
	}
}

const ChannelLocation *ChannelData::getLocation() const {
	return mgInfo ? mgInfo->getLocation() : nullptr;
}

void ChannelData::setLinkedChat(ChannelData *linked) {
	if (_linkedChat != linked) {
		_linkedChat = linked;
		if (const auto history = owner().historyLoaded(this)) {
			history->forceFullResize();
		}
		session().changes().peerUpdated(this, UpdateFlag::ChannelLinkedChat);
	}
}

ChannelData *ChannelData::linkedChat() const {
	return _linkedChat.value_or(nullptr);
}

bool ChannelData::linkedChatKnown() const {
	return _linkedChat.has_value();
}

void ChannelData::setMembersCount(int newMembersCount) {
	if (_membersCount != newMembersCount) {
		if (isMegagroup()
			&& canViewMembers()
			&& !mgInfo->lastParticipants.empty()) {
			mgInfo->lastParticipantsStatus
				|= MegagroupInfo::LastParticipantsCountOutdated;
			mgInfo->lastParticipantsCount = membersCount();
		}
		_membersCount = newMembersCount;
		session().changes().peerUpdated(this, UpdateFlag::Members);
	}
}

void ChannelData::setAdminsCount(int newAdminsCount) {
	if (_adminsCount != newAdminsCount) {
		_adminsCount = newAdminsCount;
		session().changes().peerUpdated(this, UpdateFlag::Admins);
	}
}

void ChannelData::setRestrictedCount(int newRestrictedCount) {
	if (_restrictedCount != newRestrictedCount) {
		_restrictedCount = newRestrictedCount;
		session().changes().peerUpdated(this, UpdateFlag::BannedUsers);
	}
}

void ChannelData::setKickedCount(int newKickedCount) {
	if (_kickedCount != newKickedCount) {
		_kickedCount = newKickedCount;
		session().changes().peerUpdated(this, UpdateFlag::BannedUsers);
	}
}

void ChannelData::setPendingRequestsCount(
		int count,
		const QVector<MTPlong> &recentRequesters) {
	setPendingRequestsCount(count, ranges::views::all(
		recentRequesters
	) | ranges::views::transform([&](const MTPlong &value) {
		return UserId(value);
	}) | ranges::to_vector);
}

void ChannelData::setPendingRequestsCount(
		int count,
		std::vector<UserId> recentRequesters) {
	if (_pendingRequestsCount != count
		|| _recentRequesters != recentRequesters) {
		_pendingRequestsCount = count;
		_recentRequesters = std::move(recentRequesters);
		session().changes().peerUpdated(this, UpdateFlag::PendingRequests);
	}
}

ChatRestrictionsInfo ChannelData::KickedRestrictedRights(
		not_null<PeerData*> participant) {
	using Flag = ChatRestriction;
	const auto flags = Flag::ViewMessages
		| Flag::SendStickers
		| Flag::SendGifs
		| Flag::SendGames
		| Flag::SendInline
		| Flag::SendPhotos
		| Flag::SendVideos
		| Flag::SendVideoMessages
		| Flag::SendMusic
		| Flag::SendVoiceMessages
		| Flag::SendFiles
		| Flag::SendOther
		| Flag::EmbedLinks;
	return ChatRestrictionsInfo(
		(participant->isUser() ? flags : Flag::ViewMessages),
		std::numeric_limits<int32>::max());
}

void ChannelData::applyEditAdmin(
		not_null<UserData*> user,
		ChatAdminRightsInfo oldRights,
		ChatAdminRightsInfo newRights,
		const QString &rank) {
	if (mgInfo) {
		// If rights are empty - still add participant? TODO check
		if (!base::contains(mgInfo->lastParticipants, user)) {
			mgInfo->lastParticipants.push_front(user);
			setMembersCount(membersCount() + 1);
			if (user->isBot() && !mgInfo->bots.contains(user)) {
				mgInfo->bots.insert(user);
				if (mgInfo->botStatus != 0 && mgInfo->botStatus < 2) {
					mgInfo->botStatus = 2;
				}
			}
		}
		// If rights are empty - still remove restrictions? TODO check
		if (mgInfo->lastRestricted.contains(user)) {
			mgInfo->lastRestricted.remove(user);
			if (restrictedCount() > 0) {
				setRestrictedCount(restrictedCount() - 1);
			}
		}

		auto userId = peerToUser(user->id);
		auto it = mgInfo->lastAdmins.find(user);
		if (newRights.flags) {
			auto lastAdmin = MegagroupInfo::Admin { newRights };
			lastAdmin.canEdit = true;
			if (it == mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.emplace(user, lastAdmin);
				setAdminsCount(adminsCount() + 1);
			} else {
				it->second = lastAdmin;
			}
			Data::ChannelAdminChanges(this).add(userId, rank);
		} else {
			if (it != mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.erase(it);
				if (adminsCount() > 0) {
					setAdminsCount(adminsCount() - 1);
				}
			}
			Data::ChannelAdminChanges(this).remove(userId);
		}
	}
	if (oldRights.flags && !newRights.flags) {
		// We removed an admin.
		if (adminsCount() > 1) {
			setAdminsCount(adminsCount() - 1);
		}
		if (!isMegagroup() && user->isBot() && membersCount() > 1) {
			// Removing bot admin removes it from channel.
			setMembersCount(membersCount() - 1);
		}
	} else if (!oldRights.flags && newRights.flags) {
		// We added an admin.
		setAdminsCount(adminsCount() + 1);
		updateFullForced();
	}
	session().changes().peerUpdated(this, UpdateFlag::Admins);
}

void ChannelData::applyEditBanned(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo oldRights,
		ChatRestrictionsInfo newRights) {
	auto flags = UpdateFlag::BannedUsers | UpdateFlag::None;
	auto isKicked = newRights.flags & ChatRestriction::ViewMessages;
	auto isRestricted = !isKicked && newRights.flags;
	const auto user = participant->asUser();
	if (mgInfo && user) {
		// If rights are empty - still remove admin? TODO check
		if (mgInfo->lastAdmins.contains(user)) {
			mgInfo->lastAdmins.remove(user);
			if (adminsCount() > 1) {
				setAdminsCount(adminsCount() - 1);
			} else {
				flags |= UpdateFlag::Admins;
			}
		}
		auto it = mgInfo->lastRestricted.find(user);
		if (isRestricted) {
			if (it == mgInfo->lastRestricted.cend()) {
				mgInfo->lastRestricted.emplace(
					user,
					MegagroupInfo::Restricted { newRights });
				setRestrictedCount(restrictedCount() + 1);
			} else {
				it->second.rights = newRights;
			}
		} else {
			if (it != mgInfo->lastRestricted.cend()) {
				mgInfo->lastRestricted.erase(it);
				if (restrictedCount() > 0) {
					setRestrictedCount(restrictedCount() - 1);
				}
			}
			if (isKicked) {
				auto i = ranges::find(
					mgInfo->lastParticipants,
					not_null{ user });
				if (i != mgInfo->lastParticipants.end()) {
					mgInfo->lastParticipants.erase(i);
				}
				if (membersCount() > 1) {
					setMembersCount(membersCount() - 1);
				} else {
					mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
					mgInfo->lastParticipantsCount = 0;
				}
				setKickedCount(kickedCount() + 1);
				if (mgInfo->bots.contains(user)) {
					mgInfo->bots.remove(user);
					if (mgInfo->bots.empty() && mgInfo->botStatus > 0) {
						mgInfo->botStatus = -1;
					}
				}
				flags |= UpdateFlag::Members;
				owner().removeMegagroupParticipant(this, user);
			}
		}
		Data::ChannelAdminChanges(this).remove(peerToUser(user->id));
	} else if (!mgInfo) {
		if (isKicked) {
			if (user && membersCount() > 1) {
				setMembersCount(membersCount() - 1);
				flags |= UpdateFlag::Members;
			}
			setKickedCount(kickedCount() + 1);
		}
	}
	session().changes().peerUpdated(this, flags);
}

void ChannelData::setViewAsMessagesFlag(bool enabled) {
	if (viewForumAsMessages() == enabled) {
		return;
	}
	setFlags((flags() & ~Flag::ViewAsMessages)
		| (enabled ? Flag::ViewAsMessages : Flag()));
}

void ChannelData::markForbidden() {
	owner().processChat(MTP_channelForbidden(
		MTP_flags(isMegagroup()
			? MTPDchannelForbidden::Flag::f_megagroup
			: MTPDchannelForbidden::Flag::f_broadcast),
		MTP_long(peerToChannel(id).bare),
		MTP_long(access),
		MTP_string(name()),
		MTPint()));
}

bool ChannelData::isGroupAdmin(not_null<UserData*> user) const {
	if (auto info = mgInfo.get()) {
		return info->admins.contains(peerToUser(user->id));
	}
	return false;
}

bool ChannelData::lastParticipantsRequestNeeded() const {
	if (!mgInfo || !canViewMembers()) {
		return false;
	} else if (mgInfo->lastParticipantsCount == membersCount()) {
		mgInfo->lastParticipantsStatus
			&= ~MegagroupInfo::LastParticipantsCountOutdated;
	}
	return mgInfo->lastParticipants.empty()
		|| !(mgInfo->lastParticipantsStatus
			& MegagroupInfo::LastParticipantsOnceReceived)
		|| (mgInfo->lastParticipantsStatus
			& MegagroupInfo::LastParticipantsCountOutdated);
}

auto ChannelData::unavailableReasons() const
-> const std::vector<Data::UnavailableReason> & {
	return _unavailableReasons;
}

void ChannelData::setUnavailableReasons(
		std::vector<Data::UnavailableReason> &&reasons) {
	if (_unavailableReasons != reasons) {
		_unavailableReasons = std::move(reasons);
		session().changes().peerUpdated(this, UpdateFlag::UnavailableReason);
	}
}

void ChannelData::setAvailableMinId(MsgId availableMinId) {
	if (_availableMinId != availableMinId) {
		_availableMinId = availableMinId;
	}
}

bool ChannelData::canBanMembers() const {
	return amCreator()
		|| (adminRights() & AdminRight::BanUsers);
}

bool ChannelData::canPostMessages() const {
	return amCreator()
		|| (adminRights() & AdminRight::PostMessages);
}

bool ChannelData::canEditMessages() const {
	return amCreator()
		|| (adminRights() & AdminRight::EditMessages);
}

bool ChannelData::canDeleteMessages() const {
	return amCreator()
		|| (adminRights() & AdminRight::DeleteMessages);
}

bool ChannelData::canPostStories() const {
	return amCreator()
		|| (adminRights() & AdminRight::PostStories);
}

bool ChannelData::canEditStories() const {
	return amCreator()
		|| (adminRights() & AdminRight::EditStories);
}

bool ChannelData::canDeleteStories() const {
	return amCreator()
		|| (adminRights() & AdminRight::DeleteStories);
}

bool ChannelData::anyoneCanAddMembers() const {
	return !(defaultRestrictions() & Restriction::AddParticipants);
}

bool ChannelData::hiddenPreHistory() const {
	return (flags() & Flag::PreHistoryHidden);
}

bool ChannelData::canAddMembers() const {
	return isMegagroup()
		? !amRestricted(ChatRestriction::AddParticipants)
		: ((adminRights() & AdminRight::InviteByLinkOrAdd) || amCreator());
}

bool ChannelData::canAddAdmins() const {
	return amCreator()
		|| (adminRights() & AdminRight::AddAdmins);
}

bool ChannelData::allowsForwarding() const {
	return !(flags() & Flag::NoForwards);
}

bool ChannelData::canViewMembers() const {
	return (flags() & Flag::CanViewParticipants)
		&& (!(flags() & Flag::ParticipantsHidden)
			|| amCreator()
			|| hasAdminRights());
}

bool ChannelData::canViewAdmins() const {
	return (isMegagroup() || hasAdminRights() || amCreator());
}

bool ChannelData::canViewBanned() const {
	return (hasAdminRights() || amCreator());
}

bool ChannelData::canEditInformation() const {
	return isMegagroup()
		? !amRestricted(Restriction::ChangeInfo)
		: ((adminRights() & AdminRight::ChangeInfo) || amCreator());
}

bool ChannelData::canEditPermissions() const {
	return isMegagroup()
		&& !isGigagroup()
		&& ((adminRights() & AdminRight::BanUsers) || amCreator());
}

bool ChannelData::canEditSignatures() const {
	return isChannel() && canEditInformation();
}

bool ChannelData::canEditPreHistoryHidden() const {
	return isMegagroup()
		&& ((adminRights() & AdminRight::BanUsers) || amCreator())
		&& (!isPublic() || canEditUsername());
}

bool ChannelData::canEditUsername() const {
	return amCreator()
		&& (flags() & Flag::CanSetUsername);
}

bool ChannelData::canEditStickers() const {
	return (flags() & Flag::CanSetStickers);
}

bool ChannelData::canEditEmoji() const {
	return amCreator() || (adminRights() & ChatAdminRight::ChangeInfo);
}

bool ChannelData::canDelete() const {
	constexpr auto kDeleteChannelMembersLimit = 1000;
	return amCreator()
		&& (membersCount() <= kDeleteChannelMembersLimit);
}

bool ChannelData::canEditLastAdmin(not_null<UserData*> user) const {
	// Duplicated in ParticipantsAdditionalData::canEditAdmin :(
	if (mgInfo) {
		auto i = mgInfo->lastAdmins.find(user);
		if (i != mgInfo->lastAdmins.cend()) {
			return i->second.canEdit;
		}
		return (user != mgInfo->creator);
	}
	return false;
}

bool ChannelData::canEditAdmin(not_null<UserData*> user) const {
	// Duplicated in ParticipantsAdditionalData::canEditAdmin :(
	if (user->isSelf()) {
		return false;
	} else if (amCreator()) {
		return true;
	} else if (!canEditLastAdmin(user)) {
		return false;
	}
	return adminRights() & AdminRight::AddAdmins;
}

bool ChannelData::canRestrictParticipant(
		not_null<PeerData*> participant) const {
	// Duplicated in ParticipantsAdditionalData::canRestrictParticipant :(
	if (participant->isSelf()) {
		return false;
	} else if (amCreator()) {
		return true;
	} else if (const auto user = participant->asUser()) {
		if (!canEditLastAdmin(user)) {
			return false;
		}
	}
	return adminRights() & AdminRight::BanUsers;
}

void ChannelData::setAdminRights(ChatAdminRights rights) {
	if (rights == adminRights()) {
		return;
	}
	_adminRights.set(rights);
	if (!canHaveInviteLink()) {
		setPendingRequestsCount(0, std::vector<UserId>{});
	}
	if (isMegagroup()) {
		const auto self = session().user();
		if (hasAdminRights()) {
			if (!amCreator()) {
				auto me = MegagroupInfo::Admin{
					ChatAdminRightsInfo{ rights } };
				me.canEdit = false;
				mgInfo->lastAdmins.emplace(self, me);
			}
			mgInfo->lastRestricted.remove(self);
		} else {
			mgInfo->lastAdmins.remove(self);
		}
	}
	session().changes().peerUpdated(
		this,
		UpdateFlag::Rights | UpdateFlag::Admins | UpdateFlag::BannedUsers);
}

void ChannelData::setRestrictions(ChatRestrictionsInfo rights) {
	if (rights.flags == restrictions() && rights.until == _restrictedUntil) {
		return;
	}
	_restrictedUntil = rights.until;
	_restrictions.set(rights.flags);
	if (isMegagroup()) {
		const auto self = session().user();
		if (hasRestrictions()) {
			if (!amCreator()) {
				auto me = MegagroupInfo::Restricted { rights };
				mgInfo->lastRestricted.emplace(self, me);
			}
			mgInfo->lastAdmins.remove(self);
			Data::ChannelAdminChanges(this).remove(session().userId());
		} else {
			mgInfo->lastRestricted.remove(self);
		}
	}
	session().changes().peerUpdated(
		this,
		UpdateFlag::Rights | UpdateFlag::Admins | UpdateFlag::BannedUsers);
}

void ChannelData::setDefaultRestrictions(ChatRestrictions rights) {
	if (rights == defaultRestrictions()) {
		return;
	}
	_defaultRestrictions.set(rights);
	session().changes().peerUpdated(this, UpdateFlag::Rights);
}

ChatData *ChannelData::getMigrateFromChat() const {
	if (const auto info = mgInfo.get()) {
		return info->getMigrateFromChat();
	}
	return nullptr;
}

void ChannelData::setMigrateFromChat(ChatData *chat) {
	Expects(mgInfo != nullptr);

	const auto info = mgInfo.get();
	if (chat != info->getMigrateFromChat()) {
		info->setMigrateFromChat(chat);
		if (amIn()) {
			session().changes().peerUpdated(this, UpdateFlag::Migration);
		}
	}
}

int ChannelData::slowmodeSeconds() const {
	if (const auto info = mgInfo.get()) {
		return info->slowmodeSeconds;
	}
	return 0;
}

void ChannelData::setSlowmodeSeconds(int seconds) {
	if (!mgInfo || slowmodeSeconds() == seconds) {
		return;
	}
	mgInfo->slowmodeSeconds = seconds;
	session().changes().peerUpdated(this, UpdateFlag::Slowmode);
}

TimeId ChannelData::slowmodeLastMessage() const {
	return (hasAdminRights()
		|| amCreator()
		|| unrestrictedByBoosts()
		|| !mgInfo)
		? 0
		: mgInfo->slowmodeLastMessage;
}

void ChannelData::growSlowmodeLastMessage(TimeId when) {
	const auto info = mgInfo.get();
	const auto now = base::unixtime::now();
	accumulate_min(when, now);
	if (!info) {
		return;
	} else if (info->slowmodeLastMessage > now) {
		info->slowmodeLastMessage = when;
	} else if (info->slowmodeLastMessage >= when) {
		return;
	} else {
		info->slowmodeLastMessage = when;
	}
	session().changes().peerUpdated(this, UpdateFlag::Slowmode);
}

int ChannelData::boostsApplied() const {
	if (const auto info = mgInfo.get()) {
		return info->boostsApplied;
	}
	return 0;
}

int ChannelData::boostsUnrestrict() const {
	if (const auto info = mgInfo.get()) {
		return info->boostsUnrestrict;
	}
	return 0;
}

bool ChannelData::unrestrictedByBoosts() const {
	if (const auto info = mgInfo.get()) {
		return (info->boostsUnrestrict > 0)
			&& (info->boostsApplied >= info->boostsUnrestrict);
	}
	return 0;
}

rpl::producer<bool> ChannelData::unrestrictedByBoostsValue() const {
	return mgInfo
		? mgInfo->unrestrictedByBoostsChanges.events_starting_with(
			unrestrictedByBoosts())
		: (rpl::single(false) | rpl::type_erased());
}

void ChannelData::setBoostsUnrestrict(int applied, int unrestrict) {
	if (const auto info = mgInfo.get()) {
		if (info->boostsApplied == applied
			&& info->boostsUnrestrict == unrestrict) {
			return;
		}
		const auto wasUnrestricted = unrestrictedByBoosts();
		info->boostsApplied = applied;
		info->boostsUnrestrict = unrestrict;
		const auto nowUnrestricted = unrestrictedByBoosts();
		if (wasUnrestricted != nowUnrestricted) {
			info->unrestrictedByBoostsChanges.fire_copy(nowUnrestricted);
			session().changes().peerUpdated(
				this,
				UpdateFlag::Rights | UpdateFlag::Slowmode);
		}
	}
}

void ChannelData::setInvitePeek(const QString &hash, TimeId expires) {
	if (!_invitePeek) {
		_invitePeek = std::make_unique<InvitePeek>();
	}
	_invitePeek->hash = hash;
	_invitePeek->expires = expires;
}

void ChannelData::clearInvitePeek() {
	_invitePeek = nullptr;
}

TimeId ChannelData::invitePeekExpires() const {
	return _invitePeek ? _invitePeek->expires : 0;
}

QString ChannelData::invitePeekHash() const {
	return _invitePeek ? _invitePeek->hash : QString();
}

void ChannelData::privateErrorReceived() {
	if (const auto expires = invitePeekExpires()) {
		const auto hash = invitePeekHash();
		for (const auto &window : session().windows()) {
			clearInvitePeek();
			Api::CheckChatInvite(window, hash, this);
			return;
		}
		_invitePeek->expires = base::unixtime::now();
	} else {
		markForbidden();
	}
}

void ChannelData::migrateCall(std::unique_ptr<Data::GroupCall> call) {
	Expects(_call == nullptr);
	Expects(call != nullptr);

	_call = std::move(call);
	_call->setPeer(this);
	session().changes().peerUpdated(this, UpdateFlag::GroupCall);
	addFlags(Flag::CallActive);
}

void ChannelData::setGroupCall(
		const MTPInputGroupCall &call,
		TimeId scheduleDate,
		bool rtmp) {
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

void ChannelData::clearGroupCall() {
	if (!_call) {
		return;
	}
	owner().unregisterGroupCall(_call.get());
	_call = nullptr;
	session().changes().peerUpdated(this, UpdateFlag::GroupCall);
	removeFlags(Flag::CallActive | Flag::CallNotEmpty);
}

void ChannelData::setGroupCallDefaultJoinAs(PeerId peerId) {
	_callDefaultJoinAs = peerId;
}

PeerId ChannelData::groupCallDefaultJoinAs() const {
	return _callDefaultJoinAs;
}

void ChannelData::setAllowedReactions(Data::AllowedReactions value) {
	if (_allowedReactions != value) {
		const auto enabled = [](const Data::AllowedReactions &allowed) {
			return (allowed.type != Data::AllowedReactionsType::Some)
				|| !allowed.some.empty();
		};
		const auto was = enabled(_allowedReactions);
		_allowedReactions = std::move(value);
		const auto now = enabled(_allowedReactions);
		if (was != now) {
			owner().reactions().updateAllInHistory(this, now);
		}
		session().changes().peerUpdated(this, UpdateFlag::Reactions);
	}
}

const Data::AllowedReactions &ChannelData::allowedReactions() const {
	return _allowedReactions;
}

bool ChannelData::hasActiveStories() const {
	return flags() & Flag::HasActiveStories;
}

bool ChannelData::hasUnreadStories() const {
	return flags() & Flag::HasUnreadStories;
}

void ChannelData::setStoriesState(StoriesState state) {
	Expects(state != StoriesState::Unknown);

	const auto was = flags();
	switch (state) {
	case StoriesState::None:
		_flags.remove(Flag::HasActiveStories | Flag::HasUnreadStories);
		break;
	case StoriesState::HasRead:
		_flags.set(
			(flags() & ~Flag::HasUnreadStories) | Flag::HasActiveStories);
		break;
	case StoriesState::HasUnread:
		_flags.add(Flag::HasActiveStories | Flag::HasUnreadStories);
		break;
	}
	if (flags() != was) {
		if (const auto history = owner().historyLoaded(this)) {
			history->updateChatListEntryPostponed();
		}
		session().changes().peerUpdated(this, UpdateFlag::StoriesState);
	}
}

void ChannelData::processTopics(const MTPVector<MTPForumTopic> &topics) {
	if (const auto forum = this->forum()) {
		forum->applyReceivedTopics(topics);
	}
}

int ChannelData::levelHint() const {
	return _levelHint;
}

void ChannelData::updateLevelHint(int levelHint) {
	_levelHint = levelHint;
}

namespace Data {

void ApplyMigration(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	Expects(channel->isMegagroup());

	chat->setMigrateToChannel(channel);
	channel->setMigrateFromChat(chat);
}

void ApplyChannelUpdate(
		not_null<ChannelData*> channel,
		const MTPDupdateChatDefaultBannedRights &update) {
	channel->setDefaultRestrictions(ChatRestrictionsInfo(
		update.vdefault_banned_rights()).flags);
}

void ApplyChannelUpdate(
		not_null<ChannelData*> channel,
		const MTPDchannelFull &update) {
	const auto session = &channel->session();

	if (channel->isMegagroup()) {
		const auto suggestions = update.vpending_suggestions().value_or_empty();
		channel->owner().setSuggestToGigagroup(
			channel,
			ranges::contains(
				suggestions,
				"convert_to_gigagroup"_q,
				&MTPstring::v));
	}

	channel->setAvailableMinId(update.vavailable_min_id().value_or_empty());
	auto canViewAdmins = channel->canViewAdmins();
	auto canViewMembers = channel->canViewMembers();
	auto canEditStickers = channel->canEditStickers();

	if (const auto call = update.vcall()) {
		channel->setGroupCall(*call);
	} else {
		channel->clearGroupCall();
	}
	if (const auto as = update.vgroupcall_default_join_as()) {
		channel->setGroupCallDefaultJoinAs(peerFromMTP(*as));
	} else {
		channel->setGroupCallDefaultJoinAs(0);
	}

	channel->setMessagesTTL(update.vttl_period().value_or_empty());
	using Flag = ChannelDataFlag;
	const auto mask = Flag::CanSetUsername
		| Flag::CanViewParticipants
		| Flag::CanSetStickers
		| Flag::PreHistoryHidden
		| Flag::AntiSpam
		| Flag::Location
		| Flag::ParticipantsHidden
		| Flag::CanGetStatistics
		| Flag::ViewAsMessages;
	channel->setFlags((channel->flags() & ~mask)
		| (update.is_can_set_username() ? Flag::CanSetUsername : Flag())
		| (update.is_can_view_participants()
			? Flag::CanViewParticipants
			: Flag())
		| (update.is_can_set_stickers() ? Flag::CanSetStickers : Flag())
		| (update.is_hidden_prehistory() ? Flag::PreHistoryHidden : Flag())
		| (update.is_antispam() ? Flag::AntiSpam : Flag())
		| (update.vlocation() ? Flag::Location : Flag())
		| (update.is_participants_hidden()
			? Flag::ParticipantsHidden
			: Flag())
		| (update.is_can_view_stats() ? Flag::CanGetStatistics : Flag())
		| (update.is_view_forum_as_messages()
			? Flag::ViewAsMessages
			: Flag()));
	channel->setUserpicPhoto(update.vchat_photo());
	if (const auto migratedFrom = update.vmigrated_from_chat_id()) {
		channel->addFlags(Flag::Megagroup);
		const auto chat = channel->owner().chat(migratedFrom->v);
		Data::ApplyMigration(chat, channel);
	}
	channel->setAbout(qs(update.vabout()));
	channel->setMembersCount(update.vparticipants_count().value_or_empty());
	channel->setAdminsCount(update.vadmins_count().value_or_empty());
	channel->setRestrictedCount(update.vbanned_count().value_or_empty());
	channel->setKickedCount(update.vkicked_count().value_or_empty());
	channel->setSlowmodeSeconds(update.vslowmode_seconds().value_or_empty());
	if (const auto next = update.vslowmode_next_send_date()) {
		channel->growSlowmodeLastMessage(
			next->v - channel->slowmodeSeconds());
	}
	if (const auto invite = update.vexported_invite()) {
		channel->session().api().inviteLinks().setMyPermanent(
			channel,
			*invite);
	} else {
		channel->session().api().inviteLinks().clearMyPermanent(channel);
	}
	if (const auto location = update.vlocation()) {
		channel->setLocation(*location);
	} else {
		channel->setLocation(MTP_channelLocationEmpty());
	}
	if (const auto chat = update.vlinked_chat_id()) {
		channel->setLinkedChat(channel->owner().channelLoaded(chat->v));
	} else {
		channel->setLinkedChat(nullptr);
	}
	if (const auto history = channel->owner().historyLoaded(channel)) {
		if (const auto available = update.vavailable_min_id()) {
			history->clearUpTill(available->v);
		}
		const auto folderId = update.vfolder_id().value_or_empty();
		const auto folder = folderId
			? channel->owner().folderLoaded(folderId)
			: nullptr;
		auto &histories = channel->owner().histories();
		if (folder && history->folder() != folder) {
			// If history folder is unknown or not synced, request both.
			histories.requestDialogEntry(history);
			histories.requestDialogEntry(folder);
		} else if (!history->folderKnown()
			|| channel->pts() != update.vpts().v) {
			histories.requestDialogEntry(history);
		} else {
			history->applyDialogFields(
				history->folder(),
				update.vunread_count().v,
				update.vread_inbox_max_id().v,
				update.vread_outbox_max_id().v);
		}
	}
	if (const auto pinned = update.vpinned_msg_id()) {
		SetTopPinnedMessageId(channel, pinned->v);
	}
	if (channel->isMegagroup()) {
		auto commands = ranges::views::all(
			update.vbot_info().v
		) | ranges::views::transform(
			Data::BotCommandsFromTL
		) | ranges::to_vector;

		if (channel->mgInfo->setBotCommands(std::move(commands))) {
			channel->owner().botCommandsChanged(channel);
		}
		const auto stickerSet = update.vstickerset();
		const auto sset = stickerSet ? &stickerSet->c_stickerSet() : nullptr;
		const auto newStickerSetId = (sset ? sset->vid().v : 0);
		const auto oldStickerSetId = channel->mgInfo->stickerSet.id;
		const auto stickersChanged = (canEditStickers != channel->canEditStickers())
			|| (oldStickerSetId != newStickerSetId);
		if (oldStickerSetId != newStickerSetId) {
			channel->mgInfo->stickerSet = StickerSetIdentifier{
				.id = sset ? sset->vid().v : 0,
				.accessHash = sset ? sset->vaccess_hash().v : 0,
			};
		}
		if (stickersChanged) {
			session->changes().peerUpdated(channel, UpdateFlag::StickersSet);
		}
		const auto emojiSet = update.vemojiset();
		const auto eset = emojiSet ? &emojiSet->c_stickerSet() : nullptr;
		const auto newEmojiSetId = (eset ? eset->vid().v : 0);
		const auto oldEmojiSetId = channel->mgInfo->emojiSet.id;
		const auto emojiChanged = (oldEmojiSetId != newEmojiSetId);
		if (oldEmojiSetId != newEmojiSetId) {
			channel->mgInfo->emojiSet = StickerSetIdentifier{
				.id = eset ? eset->vid().v : 0,
				.accessHash = eset ? eset->vaccess_hash().v : 0,
			};
		}
		if (emojiChanged) {
			session->changes().peerUpdated(channel, UpdateFlag::EmojiSet);
		}
		channel->setBoostsUnrestrict(
			update.vboosts_applied().value_or_empty(),
			update.vboosts_unrestrict().value_or_empty());
	}
	channel->setThemeEmoji(qs(update.vtheme_emoticon().value_or_empty()));
	channel->setTranslationDisabled(update.is_translations_disabled());
	if (const auto allowed = update.vavailable_reactions()) {
		channel->setAllowedReactions(Data::Parse(*allowed));
	} else {
		channel->setAllowedReactions({});
	}
	channel->owner().stories().apply(channel, update.vstories());
	channel->fullUpdated();
	channel->setPendingRequestsCount(
		update.vrequests_pending().value_or_empty(),
		update.vrecent_requesters().value_or_empty());

	if (canViewAdmins != channel->canViewAdmins()
		|| canViewMembers != channel->canViewMembers()) {
		session->changes().peerUpdated(channel, UpdateFlag::Rights);
	}

	channel->owner().notifySettings().apply(
		channel,
		update.vnotify_settings());

	if (update.vstats_dc()) {
		channel->owner().applyStatsDcId(channel, update.vstats_dc()->v);
	}

	if (const auto sendAs = update.vdefault_send_as()) {
		session->sendAsPeers().setChosen(channel, peerFromMTP(*sendAs));
	} else {
		session->sendAsPeers().setChosen(channel, PeerId());
	}

	if (const auto paper = update.vwallpaper()) {
		channel->setWallPaper(
			Data::WallPaper::Create(&channel->session(), *paper));
	} else {
		channel->setWallPaper({});
	}

	// For clearUpTill() call.
	channel->owner().sendHistoryChangeNotifications();
}

} // namespace Data
