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
#include "data/data_peer_values.h"

namespace Data {

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

rpl::producer<bool> PeerFlagValue(
		ChatData *chat,
		MTPDchat_ClientFlag flag) {
	return PeerFlagValue(chat, static_cast<MTPDchat::Flag>(flag));
}

rpl::producer<bool> PeerFlagValue(
		ChannelData *channel,
		MTPDchannel_ClientFlag flag) {
	return PeerFlagValue(channel, static_cast<MTPDchannel::Flag>(flag));
}

rpl::producer<bool> CanWriteValue(UserData *user) {
	using namespace rpl::mappers;
	return PeerFlagValue(user, MTPDuser::Flag::f_deleted)
		| rpl::map(!_1);
}

rpl::producer<bool> CanWriteValue(ChatData *chat) {
	using namespace rpl::mappers;
	auto mask = 0
		| MTPDchat::Flag::f_deactivated
		| MTPDchat_ClientFlag::f_forbidden
		| MTPDchat::Flag::f_left
		| MTPDchat::Flag::f_kicked;
	return PeerFlagsValue(chat, mask)
		| rpl::map(!_1);
}

rpl::producer<bool> CanWriteValue(ChannelData *channel) {
	auto mask = 0
		| MTPDchannel::Flag::f_left
		| MTPDchannel_ClientFlag::f_forbidden
		| MTPDchannel::Flag::f_creator
		| MTPDchannel::Flag::f_broadcast;
	return rpl::combine(
		PeerFlagsValue(channel, mask),
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

rpl::producer<bool> CanWriteValue(not_null<PeerData*> peer) {
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
