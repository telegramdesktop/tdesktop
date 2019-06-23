/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_values.h"

#include "observer_peer.h"
#include "core/application.h"
#include "auth_session.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_utilities.h"
#include "data/data_peer_values.h"
#include "data/data_shared_media.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"

#include "boxes/peers/edit_peer_permissions_box.h"

namespace Info {
namespace Profile {

rpl::producer<TextWithEntities> NameValue(not_null<PeerData*> peer) {
	return Notify::PeerUpdateValue(
		peer,
		Notify::PeerUpdate::Flag::NameChanged
	) | rpl::map([=] {
		return App::peerName(peer);
	}) | Ui::Text::ToWithEntities();
}

rpl::producer<TextWithEntities> PhoneValue(not_null<UserData*> user) {
	return Notify::PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserPhoneChanged
	) | rpl::map([user] {
		return App::formatPhone(user->phone());
	}) | Ui::Text::ToWithEntities();
}

auto PlainBioValue(not_null<UserData*> user) {
	return Notify::PeerUpdateValue(
		user,
		Notify::PeerUpdate::Flag::AboutChanged
	) | rpl::map([user] { return user->about(); });
}

rpl::producer<TextWithEntities> BioValue(not_null<UserData*> user) {
	return PlainBioValue(user)
		| ToSingleLine()
		| Ui::Text::ToWithEntities();
}

auto PlainUsernameValue(not_null<PeerData*> peer) {
	return Notify::PeerUpdateValue(
		peer,
		Notify::PeerUpdate::Flag::UsernameChanged
	) | rpl::map([peer] {
		return peer->userName();
	});
}

rpl::producer<TextWithEntities> UsernameValue(not_null<UserData*> user) {
	return PlainUsernameValue(
		user
	) | rpl::map([](QString &&username) {
		return username.isEmpty()
			? QString()
			: ('@' + username);
	}) | Ui::Text::ToWithEntities();
}

rpl::producer<QString> PlainAboutValue(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		if (!user->botInfo) {
			return rpl::single(QString());
		}
	}
	return Notify::PeerUpdateValue(
		peer,
		Notify::PeerUpdate::Flag::AboutChanged
	) | rpl::map([=] { return peer->about(); });
}

rpl::producer<TextWithEntities> AboutValue(not_null<PeerData*> peer) {
	auto flags = TextParseLinks
		| TextParseMentions
		| TextParseHashtags;
	if (peer->isUser()) {
		flags |= TextParseBotCommands;
	}
	return PlainAboutValue(
		peer
	) | Ui::Text::ToWithEntities(
	) | rpl::map([=](TextWithEntities &&text) {
		TextUtilities::ParseEntities(text, flags);
		return std::move(text);
	});
}

rpl::producer<QString> LinkValue(not_null<PeerData*> peer) {
	return PlainUsernameValue(
		peer
	) | rpl::map([](QString &&username) {
		return username.isEmpty()
			? QString()
			: Core::App().createInternalLinkFull(username);
	});
}

rpl::producer<const ChannelLocation*> LocationValue(
		not_null<ChannelData*> channel) {
	return Notify::PeerUpdateValue(
		channel,
		Notify::PeerUpdate::Flag::ChannelLocation
	) | rpl::map([=] {
		return channel->getLocation();
	});
}

rpl::producer<bool> NotificationsEnabledValue(not_null<PeerData*> peer) {
	return rpl::merge(
		Notify::PeerUpdateValue(
			peer,
			Notify::PeerUpdate::Flag::NotificationsEnabled
		) | rpl::map([] { return rpl::empty_value(); }),
		Auth().data().defaultNotifyUpdates(peer)
	) | rpl::map([peer] {
		return !Auth().data().notifyIsMuted(peer);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> IsContactValue(not_null<UserData*> user) {
	return Notify::PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserIsContact
	) | rpl::map([user] { return user->isContact(); });
}

rpl::producer<bool> CanInviteBotToGroupValue(not_null<UserData*> user) {
	if (!user->isBot() || user->isSupport()) {
		return rpl::single(false);
	}
	return Notify::PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::BotCanAddToGroups
	) | rpl::map([user] {
		return !user->botInfo->cantJoinGroups;
	});
}

rpl::producer<bool> CanShareContactValue(not_null<UserData*> user) {
	return Notify::PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserCanShareContact
	) | rpl::map([user] {
		return user->canShareThisContact();
	});
}

rpl::producer<bool> CanAddContactValue(not_null<UserData*> user) {
	using namespace rpl::mappers;
	if (user->isBot() || user->isSelf()) {
		return rpl::single(false);
	}
	return IsContactValue(
		user
	) | rpl::map(!_1);
}

rpl::producer<bool> AmInChannelValue(not_null<ChannelData*> channel) {
	return Notify::PeerUpdateValue(
		channel,
		Notify::PeerUpdate::Flag::ChannelAmIn
	) | rpl::map([channel] { return channel->amIn(); });
}

rpl::producer<int> MembersCountValue(not_null<PeerData*> peer) {
	using Flag = Notify::PeerUpdate::Flag;
	if (const auto chat = peer->asChat()) {
		return Notify::PeerUpdateValue(
			peer,
			Flag::MembersChanged
		) | rpl::map([chat] {
			return chat->amIn()
				? std::max(chat->count, int(chat->participants.size()))
				: 0;
		});
	} else if (const auto channel = peer->asChannel()) {
		return Notify::PeerUpdateValue(
			channel,
			Flag::MembersChanged
		) | rpl::map([channel] {
			return channel->membersCount();
		});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> AdminsCountValue(not_null<PeerData*> peer) {
	using Flag = Notify::PeerUpdate::Flag;
	if (const auto chat = peer->asChat()) {
		return Notify::PeerUpdateValue(
			chat,
			Flag::AdminsChanged | Flag::RightsChanged
		) | rpl::map([=] {
			return chat->participants.empty()
				? 0
				: int(chat->admins.size() + 1); // + creator
		});
	} else if (const auto channel = peer->asChannel()) {
		return Notify::PeerUpdateValue(
			channel,
			Flag::AdminsChanged | Flag::RightsChanged
		) | rpl::map([=] {
			return channel->canViewAdmins()
				? channel->adminsCount()
				: 0;
		});
	}
	Unexpected("User in AdminsCountValue().");
}


rpl::producer<int> RestrictionsCountValue(not_null<PeerData*> peer) {
	const auto countOfRestrictions = [](ChatRestrictions restrictions) {
		auto count = 0;
		for (const auto f : Data::ListOfRestrictions()) {
			if (restrictions & f) count++;
		}
		return int(Data::ListOfRestrictions().size()) - count;
	};

	using Flag = Notify::PeerUpdate::Flag;
	if (const auto chat = peer->asChat()) {
		return Notify::PeerUpdateValue(
			chat,
			Flag::RightsChanged
		) | rpl::map([=] {
			return countOfRestrictions(chat->defaultRestrictions());
		});
	} else if (const auto channel = peer->asChannel()) {
		return Notify::PeerUpdateValue(
			channel,
			Flag::RightsChanged
		) | rpl::map([=] {
			return countOfRestrictions(channel->defaultRestrictions());
		});
	}
	Unexpected("User in RestrictionsCountValue().");
}

rpl::producer<int> RestrictedCountValue(not_null<ChannelData*> channel) {
	using Flag = Notify::PeerUpdate::Flag;
	return Notify::PeerUpdateValue(
		channel,
		Flag::BannedUsersChanged | Flag::RightsChanged
	) | rpl::map([=] {
		return channel->canViewBanned()
			? channel->restrictedCount()
			: 0;
	});
}

rpl::producer<int> KickedCountValue(not_null<ChannelData*> channel) {
	using Flag = Notify::PeerUpdate::Flag;
	return Notify::PeerUpdateValue(
		channel,
		Flag::BannedUsersChanged | Flag::RightsChanged
	) | rpl::map([=] {
		return channel->canViewBanned()
			? channel->kickedCount()
			: 0;
	});
}

rpl::producer<int> SharedMediaCountValue(
		not_null<PeerData*> peer,
		PeerData *migrated,
		Storage::SharedMediaType type) {
	auto aroundId = 0;
	auto limit = 0;
	auto updated = SharedMediaMergedViewer(
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				peer->id,
				migrated ? migrated->id : 0,
				aroundId),
			type),
		limit,
		limit
	) | rpl::map([](const SparseIdsMergedSlice &slice) {
		return slice.fullCount();
	}) | rpl::filter_optional();
	return rpl::single(0) | rpl::then(std::move(updated));
}

rpl::producer<int> CommonGroupsCountValue(not_null<UserData*> user) {
	return Notify::PeerUpdateValue(
		user,
		Notify::PeerUpdate::Flag::UserCommonChatsChanged
	) | rpl::map([user] {
		return user->commonChatsCount();
	});
}

rpl::producer<bool> CanAddMemberValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return Notify::PeerUpdateValue(
			chat,
			Notify::PeerUpdate::Flag::RightsChanged
		) | rpl::map([=] {
			return chat->canAddMembers();
		});
	} else if (const auto channel = peer->asChannel()) {
		return Notify::PeerUpdateValue(
			channel,
			Notify::PeerUpdate::Flag::RightsChanged
		) | rpl::map([=] {
			return channel->canAddMembers();
		});
	}
	return rpl::single(false);
}

rpl::producer<bool> VerifiedValue(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return Data::PeerFlagValue(user, MTPDuser::Flag::f_verified);
	} else if (const auto channel = peer->asChannel()) {
		return Data::PeerFlagValue(
			channel,
			MTPDchannel::Flag::f_verified);
	}
	return rpl::single(false);
}

rpl::producer<bool> ScamValue(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return Data::PeerFlagValue(user, MTPDuser::Flag::f_scam);
	} else if (const auto channel = peer->asChannel()) {
		return Data::PeerFlagValue(
			channel,
			MTPDchannel::Flag::f_scam);
	}
	return rpl::single(false);
}
// // #feed
//rpl::producer<int> FeedChannelsCountValue(not_null<Data::Feed*> feed) {
//	using Flag = Data::FeedUpdateFlag;
//	return rpl::single(
//		Data::FeedUpdate{ feed, Flag::Channels }
//	) | rpl::then(
//		Auth().data().feedUpdated()
//	) | rpl::filter([=](const Data::FeedUpdate &update) {
//		return (update.feed == feed) && (update.flag == Flag::Channels);
//	}) | rpl::filter([=] {
//		return feed->channelsLoaded();
//	}) | rpl::map([=] {
//		return int(feed->channels().size());
//	}) | rpl::distinct_until_changed();
//}

} // namespace Profile
} // namespace Info
