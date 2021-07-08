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

[[nodiscard]] rpl::producer<bool> CanWriteValue(UserData *user);
[[nodiscard]] rpl::producer<bool> CanWriteValue(ChatData *chat);
[[nodiscard]] rpl::producer<bool> CanWriteValue(ChannelData *channel);
[[nodiscard]] rpl::producer<bool> CanWriteValue(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<bool> CanPinMessagesValue(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<bool> CanManageGroupCallValue(not_null<PeerData*> peer);

[[nodiscard]] TimeId SortByOnlineValue(not_null<UserData*> user, TimeId now);
[[nodiscard]] crl::time OnlineChangeTimeout(TimeId online, TimeId now);
[[nodiscard]] crl::time OnlineChangeTimeout(not_null<UserData*> user, TimeId now);
[[nodiscard]] QString OnlineText(TimeId online, TimeId now);
[[nodiscard]] QString OnlineText(not_null<UserData*> user, TimeId now);
[[nodiscard]] QString OnlineTextFull(not_null<UserData*> user, TimeId now);
[[nodiscard]] bool OnlineTextActive(TimeId online, TimeId now);
[[nodiscard]] bool OnlineTextActive(not_null<UserData*> user, TimeId now);
[[nodiscard]] bool IsUserOnline(not_null<UserData*> user);
[[nodiscard]] bool ChannelHasActiveCall(not_null<ChannelData*> channel);

} // namespace Data
