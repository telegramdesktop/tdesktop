/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	return std::move(
		value
	) | rpl::filter([mask](const ChangeType &change) {
		return change.diff & mask;
	}) | rpl::map([mask](const ChangeType &change) {
		return change.value & mask;
	});
}

template <typename ChangeType, typename Error, typename Generator>
inline auto SingleFlagValue(
		rpl::producer<ChangeType, Error, Generator> &&value,
		typename ChangeType::Enum flag) {
	return FlagsValueWithMask(
		std::move(value),
		flag
	) | rpl::map([](typename ChangeType::Type value) {
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

rpl::producer<bool> PeerFlagValue(
	ChatData *chat,
	MTPDchat_ClientFlag flag);

rpl::producer<bool> PeerFlagValue(
	ChannelData *channel,
	MTPDchannel_ClientFlag flag);

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

rpl::producer<bool> CanWriteValue(UserData *user);
rpl::producer<bool> CanWriteValue(ChatData *chat);
rpl::producer<bool> CanWriteValue(ChannelData *channel);
rpl::producer<bool> CanWriteValue(not_null<PeerData*> peer);

TimeId SortByOnlineValue(not_null<UserData*> user, TimeId now);
TimeMs OnlineChangeTimeout(TimeId online, TimeId now);
TimeMs OnlineChangeTimeout(not_null<UserData*> user, TimeId now);
QString OnlineText(TimeId online, TimeId now);
QString OnlineText(not_null<UserData*> user, TimeId now);
QString OnlineTextFull(not_null<UserData*> user, TimeId now);
bool OnlineTextActive(TimeId online, TimeId now);
bool OnlineTextActive(not_null<UserData*> user, TimeId now);

} // namespace Data
