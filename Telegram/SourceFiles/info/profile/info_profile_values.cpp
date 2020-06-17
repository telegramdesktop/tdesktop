/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_values.h"

#include "core/application.h"
#include "main/main_session.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "data/data_peer_values.h"
#include "data/data_shared_media.h"
#include "data/data_folder.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "app.h"

namespace Info {
namespace Profile {
namespace {

using UpdateFlag = Data::PeerUpdate::Flag;

auto PlainBioValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::About
	) | rpl::map([=] {
		return user->about();
	});
}

auto PlainUsernameValue(not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::Username
	) | rpl::map([=] {
		return peer->userName();
	});
}

} // namespace

rpl::producer<TextWithEntities> NameValue(not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::Name
	) | rpl::map([=] {
		return peer->name;
	}) | Ui::Text::ToWithEntities();;
}

rpl::producer<TextWithEntities> PhoneValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::PhoneNumber
	) | rpl::map([=] {
		return App::formatPhone(user->phone());
	}) | Ui::Text::ToWithEntities();
}

rpl::producer<TextWithEntities> PhoneOrHiddenValue(not_null<UserData*> user) {
	return rpl::combine(
		PhoneValue(user),
		PlainUsernameValue(user),
		PlainBioValue(user),
		tr::lng_info_mobile_hidden()
	) | rpl::map([](
			const TextWithEntities &phone,
			const QString &username,
			const QString &bio,
			const QString &hidden) {
		return (phone.text.isEmpty() && username.isEmpty() && bio.isEmpty())
			? Ui::Text::WithEntities(hidden)
			: phone;
	});
}

rpl::producer<TextWithEntities> BioValue(not_null<UserData*> user) {
	return PlainBioValue(user)
		| ToSingleLine()
		| Ui::Text::ToWithEntities();
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
		if (!user->isBot()) {
			return rpl::single(QString());
		}
	}
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::About
	) | rpl::map([=] {
		return peer->about();
	});
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
	) | rpl::map([=](QString &&username) {
		return username.isEmpty()
			? QString()
			: peer->session().createInternalLinkFull(username);
	});
}

rpl::producer<const ChannelLocation*> LocationValue(
		not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::ChannelLocation
	) | rpl::map([=] {
		return channel->getLocation();
	});
}

rpl::producer<bool> NotificationsEnabledValue(not_null<PeerData*> peer) {
	return rpl::merge(
		peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Notifications
		) | rpl::map([] { return rpl::empty_value(); }),
		peer->owner().defaultNotifyUpdates(peer)
	) | rpl::map([=] {
		return !peer->owner().notifyIsMuted(peer);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> IsContactValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::IsContact
	) | rpl::map([=] {
		return user->isContact();
	});
}

rpl::producer<bool> CanInviteBotToGroupValue(not_null<UserData*> user) {
	if (!user->isBot() || user->isSupport()) {
		return rpl::single(false);
	}
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::BotCanBeInvited
	) | rpl::map([=] {
		return !user->botInfo->cantJoinGroups;
	});
}

rpl::producer<bool> CanShareContactValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::CanShareContact
	) | rpl::map([=] {
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
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::ChannelAmIn
	) | rpl::map([=] {
		return channel->amIn();
	});
}

rpl::producer<int> MembersCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Members
		) | rpl::map([=] {
			return chat->amIn()
				? std::max(chat->count, int(chat->participants.size()))
				: 0;
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Members
		) | rpl::map([=] {
			return channel->membersCount();
		});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> AdminsCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Admins | UpdateFlag::Rights
		) | rpl::map([=] {
			return chat->participants.empty()
				? 0
				: int(chat->admins.size() + 1); // + creator
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Admins | UpdateFlag::Rights
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

	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return countOfRestrictions(chat->defaultRestrictions());
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return countOfRestrictions(channel->defaultRestrictions());
		});
	}
	Unexpected("User in RestrictionsCountValue().");
}

rpl::producer<not_null<PeerData*>> MigratedOrMeValue(
		not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Migration
		) | rpl::map([=] {
			return chat->migrateToOrMe();
		});
	} else {
		return rpl::single(peer);
	}
}

rpl::producer<int> RestrictedCountValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::BannedUsers | UpdateFlag::Rights
	) | rpl::map([=] {
		return channel->canViewBanned()
			? channel->restrictedCount()
			: 0;
	});
}

rpl::producer<int> KickedCountValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::BannedUsers | UpdateFlag::Rights
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
		&peer->session(),
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
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::CommonChats
	) | rpl::map([=] {
		return user->commonChatsCount();
	});
}

rpl::producer<bool> CanAddMemberValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return chat->canAddMembers();
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
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
//		feed->owner().feedUpdated()
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
