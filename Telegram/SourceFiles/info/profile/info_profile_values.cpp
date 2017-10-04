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
#include "info/profile/info_profile_values.h"

#include <rpl/filter.h>
#include <rpl/range.h>
#include <rpl/then.h>
#include <rpl/combine.h>
#include "observer_peer.h"
#include "messenger.h"
#include "ui/wrap/slide_wrap.h"
#include "history/history_shared_media.h"

namespace Info {
namespace Profile {

rpl::producer<Notify::PeerUpdate> PeerUpdateViewer(
		Notify::PeerUpdate::Flags flags) {
	return [=](const auto &consumer) {
		auto lifetime = rpl::lifetime();
		lifetime.make_state<base::Subscription>(
			Notify::PeerUpdated().add_subscription({ flags, [=](
					const Notify::PeerUpdate &update) {
				consumer.put_next_copy(update);
			}}));
		return lifetime;
	};
}

rpl::producer<Notify::PeerUpdate> PeerUpdateViewer(
		not_null<PeerData*> peer,
		Notify::PeerUpdate::Flags flags) {
	return PeerUpdateViewer(flags)
		| rpl::filter([=](const Notify::PeerUpdate &update) {
			return (update.peer == peer);
		});
}

rpl::producer<Notify::PeerUpdate> PeerUpdateValue(
		not_null<PeerData*> peer,
		Notify::PeerUpdate::Flags flags) {
	auto initial = Notify::PeerUpdate(peer);
	initial.flags = flags;
	return rpl::single(initial)
		| then(PeerUpdateViewer(peer, flags));
}

rpl::producer<TextWithEntities> PhoneValue(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserPhoneChanged)
		| rpl::map([user] {
			return App::formatPhone(user->phone());
		})
		| WithEmptyEntities();
}

rpl::producer<TextWithEntities> BioValue(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::AboutChanged)
		| rpl::map([user] { return user->about(); })
		| WithEmptyEntities();
}

rpl::producer<QString> PlainUsernameViewer(
		not_null<PeerData*> peer) {
	return PeerUpdateValue(
			peer,
			Notify::PeerUpdate::Flag::UsernameChanged)
		| rpl::map([peer] {
			return peer->userName();
		});
}

rpl::producer<TextWithEntities> UsernameValue(
		not_null<UserData*> user) {
	return PlainUsernameViewer(user)
		| rpl::map([](QString &&username) {
			return username.isEmpty()
				? QString()
				: ('@' + username);
		})
		| WithEmptyEntities();
}

rpl::producer<TextWithEntities> AboutValue(
		not_null<PeerData*> peer) {
	if (auto channel = peer->asChannel()) {
		return PeerUpdateValue(
				channel,
				Notify::PeerUpdate::Flag::AboutChanged)
			| rpl::map([channel] { return channel->about(); })
			| WithEmptyEntities();
	}
	return rpl::single(TextWithEntities{});
}

rpl::producer<TextWithEntities> LinkValue(
		not_null<PeerData*> peer) {
	return PlainUsernameViewer(peer)
		| rpl::map([](QString &&username) {
			return username.isEmpty()
				? QString()
				: Messenger::Instance().createInternalLink(username);
		})
		| WithEmptyEntities();
}

rpl::producer<bool> NotificationsEnabledValue(
		not_null<PeerData*> peer) {
	return PeerUpdateValue(
			peer,
			Notify::PeerUpdate::Flag::NotificationsEnabled)
		| rpl::map([peer] { return !peer->isMuted(); });
}

rpl::producer<bool> IsContactValue(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserIsContact)
		| rpl::map([user] { return user->isContact(); });
}

rpl::producer<bool> CanShareContactValue(
		not_null<UserData*> user) {
	return PeerUpdateValue(
			user,
			Notify::PeerUpdate::Flag::UserCanShareContact)
		| rpl::map([user] {
			return user->canShareThisContact();
		});
}

rpl::producer<bool> CanAddContactValue(
		not_null<UserData*> user) {
	using namespace rpl::mappers;
	return rpl::combine(
			IsContactValue(user),
			CanShareContactValue(user),
			!$1 && $2);
}

rpl::producer<int> MembersCountValue(
		not_null<PeerData*> peer) {
	if (auto chat = peer->asChat()) {
		return PeerUpdateValue(
				peer,
				Notify::PeerUpdate::Flag::MembersChanged)
			| rpl::map([chat] {
				return chat->amIn()
					? qMax(chat->count, chat->participants.size())
					: 0;
			});
	} else if (auto channel = peer->asChannel()) {
		return PeerUpdateValue(
				peer,
				Notify::PeerUpdate::Flag::MembersChanged)
			| rpl::map([channel] {
				auto canViewCount = channel->canViewMembers()
					|| !channel->isMegagroup();
				return canViewCount
					? qMax(channel->membersCount(), 1)
					: 0;
			});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> SharedMediaCountValue(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type) {
	auto real = peer->migrateTo() ? peer->migrateTo() : peer;
	auto migrated = real->migrateFrom()
		? real->migrateFrom()
		: nullptr;
	auto aroundId = 0;
	auto limit = 0;
	auto updated = SharedMediaMergedViewer(
		SharedMediaMergedSlice::Key(
			real->id,
			migrated ? migrated->id : 0,
			type,
			aroundId),
		limit,
		limit)
		| rpl::map([](const SharedMediaMergedSlice &slice) {
			return slice.fullCount();
		})
		| rpl::filter_optional();
	return rpl::single(0) | rpl::then(std::move(updated));
}

rpl::producer<int> CommonGroupsCountValue(
		not_null<UserData*> user) {
	return PeerUpdateValue(
		user,
		Notify::PeerUpdate::Flag::UserCommonChatsChanged)
		| rpl::map([user] {
			return user->commonChatsCount();
		});
}

rpl::producer<bool> CanAddMemberValue(
		not_null<PeerData*> peer) {
	if (auto chat = peer->asChat()) {
		return PeerUpdateValue(
			chat,
			Notify::PeerUpdate::Flag::ChatCanEdit)
			| rpl::map([chat] {
				return chat->canEdit();
			});
	} else if (auto channel = peer->asChannel()) {
		return PeerUpdateValue(
			channel,
			Notify::PeerUpdate::Flag::ChannelRightsChanged)
			| rpl::map([channel] {
				return channel->canAddMembers();
			});
	}
	return rpl::single(false);
}

} // namespace Profile
} // namespace Info
