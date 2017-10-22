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
#pragma once

#include <rpl/filter.h>
#include <rpl/map.h>
#include <rpl/combine.h>
#include "data/data_peer.h"

namespace Data {

template <typename ChangeType, typename Error, typename Generator>
inline auto FlagsValueWithMask(
		rpl::producer<ChangeType, Error, Generator> &&value,
		typename ChangeType::Type mask) {
	return std::move(value)
		| rpl::filter([mask](const ChangeType &change) {
			return change.diff & mask;
		})
		| rpl::map([mask](const ChangeType &change) {
			return change.value & mask;
		});
}

template <typename ChangeType, typename Error, typename Generator>
inline auto SingleFlagValue(
		rpl::producer<ChangeType, Error, Generator> &&value,
		typename ChangeType::Enum flag) {
	return FlagsValueWithMask(std::move(value), flag)
		| rpl::map([flag](typename ChangeType::Type value) {
			return !!value;
		});
}

template <
	typename PeerType,
	typename ChangeType = typename PeerType::Flags::Change>
inline auto PeerFlagsValue(PeerType *peer) {
	Expects(peer != nullptr);
	return peer->flagsValue();
}

template <
	typename PeerType,
	typename ChangeType = typename PeerType::Flags::Change>
inline auto PeerFlagsValue(
		PeerType *peer,
		typename PeerType::Flags::Type mask) {
	return FlagsValueWithMask(PeerFlagsValue(peer), mask);
}

template <
	typename PeerType,
	typename ChangeType = typename PeerType::Flags::Change>
inline auto PeerFlagValue(
		PeerType *peer,
		typename PeerType::Flags::Enum flag) {
	return SingleFlagValue(PeerFlagsValue(peer), flag);
}
//
//inline auto PeerFlagValue(
//		UserData *user,
//		MTPDuser_ClientFlag flag) {
//	return PeerFlagValue(user, static_cast<MTPDuser::Flag>(flag));
//}

inline auto PeerFlagValue(
		ChatData *chat,
		MTPDchat_ClientFlag flag) {
	return PeerFlagValue(chat, static_cast<MTPDchat::Flag>(flag));
}

inline auto PeerFlagValue(
		ChannelData *channel,
		MTPDchannel_ClientFlag flag) {
	return PeerFlagValue(channel, static_cast<MTPDchannel::Flag>(flag));
}

template <
	typename PeerType,
	typename = typename PeerType::FullFlags::Change>
inline auto PeerFullFlagsValue(PeerType *peer) {
	Expects(peer != nullptr);
	return peer->fullFlagsValue();
}

template <
	typename PeerType,
	typename = typename PeerType::FullFlags::Change>
inline auto PeerFullFlagsValue(
		PeerType *peer,
		typename PeerType::FullFlags::Type mask) {
	return FlagsValueWithMask(PeerFullFlagsValue(peer), mask);
}

template <
	typename PeerType,
	typename = typename PeerType::FullFlags::Change>
inline auto PeerFullFlagValue(
		PeerType *peer,
		typename PeerType::FullFlags::Enum flag) {
	return SingleFlagValue(PeerFullFlagsValue(peer), flag);
}

inline auto AdminRightsValue(not_null<ChannelData*> channel) {
	return channel->adminRightsValue();
}

inline auto AdminRightsValue(
		not_null<ChannelData*> channel,
		MTPDchannelAdminRights::Flags mask) {
	return FlagsValueWithMask(AdminRightsValue(channel), mask);
}

inline auto AdminRightValue(
		not_null<ChannelData*> channel,
		MTPDchannelAdminRights::Flag flag) {
	return SingleFlagValue(AdminRightsValue(channel), flag);
}

inline auto RestrictionsValue(not_null<ChannelData*> channel) {
	return channel->restrictionsValue();
}

inline auto RestrictionsValue(
		not_null<ChannelData*> channel,
		MTPDchannelBannedRights::Flags mask) {
	return FlagsValueWithMask(RestrictionsValue(channel), mask);
}

inline auto RestrictionValue(
		not_null<ChannelData*> channel,
		MTPDchannelBannedRights::Flag flag) {
	return SingleFlagValue(RestrictionsValue(channel), flag);
}

inline auto CanWriteValue(UserData *user) {
	using namespace rpl::mappers;
	return PeerFlagValue(user, MTPDuser::Flag::f_deleted)
		| rpl::map(!$1);
}

inline auto CanWriteValue(ChatData *chat) {
	using namespace rpl::mappers;
	auto mask = 0
		| MTPDchat::Flag::f_deactivated
		| MTPDchat_ClientFlag::f_forbidden
		| MTPDchat::Flag::f_left
		| MTPDchat::Flag::f_kicked;
	return PeerFlagsValue(chat, mask)
		| rpl::map(!$1);
}

inline auto CanWriteValue(ChannelData *channel) {
	auto flagsMask = 0
		| MTPDchannel::Flag::f_left
		| MTPDchannel_ClientFlag::f_forbidden
		| MTPDchannel::Flag::f_creator
		| MTPDchannel::Flag::f_broadcast;
	return rpl::combine(
		PeerFlagsValue(channel, flagsMask),
		AdminRightValue(
			channel,
			MTPDchannelAdminRights::Flag::f_post_messages),
		RestrictionValue(
			channel,
			MTPDchannelBannedRights::Flag::f_send_messages),
		[](
				MTPDchannel::Flags flags,
				bool postMessagesRight,
				bool sendMessagesRestriction) {
			auto notAmInFlags = 0
				| MTPDchannel::Flag::f_left
				| MTPDchannel_ClientFlag::f_forbidden;
			return !(flags & notAmInFlags)
				&& (postMessagesRight
					|| (flags & MTPDchannel::Flag::f_creator)
					|| (!(flags & MTPDchannel::Flag::f_broadcast)
						&& !sendMessagesRestriction));
		});
}

inline rpl::producer<bool> CanWriteValue(not_null<PeerData*> peer) {
	if (auto user = peer->asUser()) {
		return CanWriteValue(user);
	} else if (auto chat = peer->asChat()) {
		return CanWriteValue(chat);
	} else if (auto channel = peer->asChannel()) {
		return CanWriteValue(channel);
	}
	Unexpected("Bad peer value in CanWriteValue()");
}

} // namespace Data
