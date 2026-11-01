/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "window/notifications_manager.h"

@class NSDictionary;

namespace Platform::Notifications {

// WHY: the system logs request identifiers and stores userInfo, so only
// random tokens may leave the client; this in-memory map resolves them
// back to account, chat and message ids and is never handed to the OS.
class NotificationTokens final {
public:
	using ContextId = Window::Notifications::Manager::ContextId;
	using NotificationId = Window::Notifications::Manager::NotificationId;

	[[nodiscard]] uint64 add(const NotificationId &notificationId);
	[[nodiscard]] std::optional<NotificationId> lookup(uint64 token) const;

	[[nodiscard]] base::flat_set<uint64> takeFromItem(
		const NotificationId &target);
	[[nodiscard]] base::flat_set<uint64> takeFromContext(
		const ContextId &target);
	[[nodiscard]] base::flat_set<uint64> takeFromHistory(
		const ContextId &target);
	[[nodiscard]] base::flat_set<uint64> takeFromSession(uint64 sessionId);
	void clear();

private:
	[[nodiscard]] base::flat_set<uint64> takeRange(
		const NotificationId &from,
		Fn<bool(const NotificationId &)> predicate);

	std::unordered_map<uint64, NotificationId> _ids;
	std::map<NotificationId, uint64> _tokensByItem;

};

[[nodiscard]] NSDictionary *NotificationUserInfo(
	uint64 managerId,
	uint64 token);
[[nodiscard]] uint64 NotificationManagerId(NSDictionary *userInfo);
[[nodiscard]] uint64 NotificationToken(NSDictionary *userInfo);

} // namespace Platform::Notifications
