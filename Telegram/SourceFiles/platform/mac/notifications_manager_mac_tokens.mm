/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/notifications_manager_mac_tokens.h"

#include "base/random.h"

#include <Foundation/Foundation.h>

namespace Platform::Notifications {
namespace {

using NotificationId = NotificationTokens::NotificationId;

constexpr auto kMinMsgId = MsgId(std::numeric_limits<int64>::min());

NSString *const kManagerKey = @"manager";
NSString *const kTokenKey = @"token";

[[nodiscard]] NotificationId LowestKey(uint64 sessionId, PeerId peerId) {
	return {
		.contextId = {
			.sessionId = sessionId,
			.peerId = peerId,
			.topicRootId = kMinMsgId,
		},
		.msgId = kMinMsgId,
	};
}

[[nodiscard]] uint64 ReadValue(NSDictionary *userInfo, NSString *key) {
	id value = [userInfo objectForKey:key];
	return [value isKindOfClass:[NSNumber class]]
		? [(NSNumber*)value unsignedLongLongValue]
		: 0ULL;
}

} // namespace

uint64 NotificationTokens::add(const NotificationId &notificationId) {
	// An equal id keeps its token, so the system replaces its notification.
	const auto i = _tokensByItem.find(notificationId);
	if (i != end(_tokensByItem)) {
		return i->second;
	}
	auto token = base::RandomValue<uint64>();
	while (!token || _ids.contains(token)) {
		token = base::RandomValue<uint64>();
	}
	_ids.emplace(token, notificationId);
	_tokensByItem.emplace(notificationId, token);
	return token;
}

std::optional<NotificationId> NotificationTokens::lookup(
		uint64 token) const {
	const auto i = _ids.find(token);
	if (i == end(_ids)) {
		return std::nullopt;
	}
	return i->second;
}

base::flat_set<uint64> NotificationTokens::takeFromItem(
		const NotificationId &target) {
	return takeRange(target, [&](const NotificationId &key) {
		return key.msgId && (key == target);
	});
}

base::flat_set<uint64> NotificationTokens::takeFromContext(
		const ContextId &target) {
	const auto from = NotificationId{ target, kMinMsgId };
	return takeRange(from, [&](const NotificationId &key) {
		return (key.contextId == target);
	});
}

base::flat_set<uint64> NotificationTokens::takeFromHistory(
		const ContextId &target) {
	const auto from = LowestKey(target.sessionId, target.peerId);
	return takeRange(from, [&](const NotificationId &key) {
		return ContextId{
			.sessionId = key.contextId.sessionId,
			.peerId = key.contextId.peerId,
		} == target;
	});
}

base::flat_set<uint64> NotificationTokens::takeFromSession(
		uint64 sessionId) {
	const auto from = LowestKey(sessionId, PeerId());
	return takeRange(from, [&](const NotificationId &key) {
		return (key.contextId.sessionId == sessionId);
	});
}

void NotificationTokens::clear() {
	_ids.clear();
	_tokensByItem.clear();
}

base::flat_set<uint64> NotificationTokens::takeRange(
		const NotificationId &from,
		Fn<bool(const NotificationId &)> predicate) {
	auto tokens = std::vector<uint64>();
	auto i = _tokensByItem.lower_bound(from);
	while (i != end(_tokensByItem) && predicate(i->first)) {
		tokens.push_back(i->second);
		_ids.erase(i->second);
		i = _tokensByItem.erase(i);
	}
	return { begin(tokens), end(tokens) };
}

NSDictionary *NotificationUserInfo(uint64 managerId, uint64 token) {
	return @{
		kManagerKey: [NSNumber numberWithUnsignedLongLong:managerId],
		kTokenKey: [NSNumber numberWithUnsignedLongLong:token],
	};
}

uint64 NotificationManagerId(NSDictionary *userInfo) {
	return ReadValue(userInfo, kManagerKey);
}

uint64 NotificationToken(NSDictionary *userInfo) {
	return ReadValue(userInfo, kTokenKey);
}

} // namespace Platform::Notifications
