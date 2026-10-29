/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_unread_value.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"

namespace Data {
namespace {

rpl::producer<Dialogs::UnreadState> MainListUnreadState(
		not_null<Dialogs::MainList*> list) {
	return rpl::single(rpl::empty) | rpl::then(
		list->unreadStateChanges() | rpl::to_empty
	) | rpl::map([=] {
		return list->unreadState();
	});
}

} // namespace

[[nodiscard]] Dialogs::UnreadState MainListMapUnreadState(
		not_null<Main::Session*> session,
		const Dialogs::UnreadState &state) {
	const auto folderId = Data::Folder::kId;
	if (const auto folder = session->data().folderLoaded(folderId)) {
		return state - folder->chatsList()->unreadState();
	}
	return state;
}

rpl::producer<Dialogs::UnreadState> UnreadStateValue(
		not_null<Main::Session*> session,
		FilterId filterId) {
	if (filterId > 0) {
		const auto filters = &session->data().chatsFilters();
		return MainListUnreadState(filters->chatsList(filterId));
	}
	return MainListUnreadState(
		session->data().chatsList()
	) | rpl::map([=](const Dialogs::UnreadState &state) {
		return MainListMapUnreadState(session, state);
	});
}

rpl::producer<bool> IncludeMutedCounterFoldersValue() {
	using namespace Window::Notifications;
	return rpl::single(rpl::empty_value()) | rpl::then(
		Core::App().notifications().settingsChanged(
		) | rpl::filter(
			rpl::mappers::_1 == ChangeType::IncludeMuted
		) | rpl::to_empty
	) | rpl::map([] {
		return Core::App().settings().includeMutedCounterFolders();
	});
}

std::optional<int> ComputeUnreadCountLocally(
		MsgId afterId,
		MsgId minReadTillId,
		const std::vector<MsgId> &list,
		std::optional<int> skippedBefore,
		std::optional<int> skippedAfter,
		std::optional<int> fullCount,
		std::optional<int> currentUnreadCountAfter,
		MsgId wasReadTillId) {
	Expects(afterId >= wasReadTillId);

	const auto startingMarkingAsRead = (currentUnreadCountAfter == 0)
		&& (wasReadTillId == 1)
		&& (afterId > 1);
	const auto wasUnreadCountAfter = startingMarkingAsRead
		? fullCount.value_or(0)
		: currentUnreadCountAfter;
	const auto readTillId = std::max(afterId, minReadTillId);
	const auto backLoaded = (skippedBefore == 0);
	const auto frontLoaded = (skippedAfter == 0);
	const auto fullLoaded = backLoaded && frontLoaded;
	const auto allUnread = (readTillId == minReadTillId)
		|| (fullLoaded && list.empty());
	if (allUnread && fullLoaded) {
		// Should not happen too often unless the list is empty.
		return int(list.size());
	} else if (frontLoaded && !list.empty() && readTillId >= list.front()) {
		// Always "count by local data" if read till the end.
		return 0;
	} else if (wasReadTillId == readTillId) {
		// Otherwise don't recount the same value over and over.
		return wasUnreadCountAfter;
	} else if (frontLoaded && !list.empty() && readTillId >= list.back()) {
		// And count by local data if it is available and read-till changed.
		return int(ranges::lower_bound(list, readTillId, std::greater<>())
			- begin(list));
	} else if (list.empty()) {
		return std::nullopt;
	} else if (wasUnreadCountAfter.has_value()
		&& (frontLoaded || readTillId <= list.front())
		&& (backLoaded || wasReadTillId >= list.back())) {
		// Count how many were read since previous value.
		const auto from = ranges::lower_bound(
			list,
			readTillId,
			std::greater<>());
		const auto till = ranges::lower_bound(
			from,
			end(list),
			wasReadTillId,
			std::greater<>());
		return std::max(*wasUnreadCountAfter - int(till - from), 0);
	}
	return std::nullopt;
}

int ReconcileSkippedCounts(
		std::optional<int> &skippedBefore,
		std::optional<int> &skippedAfter,
		bool toFront,
		int wasSize,
		int nowSize,
		int fullCount) {
	auto &decrementFrom = toFront ? skippedAfter : skippedBefore;
	if (decrementFrom.has_value()) {
		*decrementFrom = std::max(
			*decrementFrom - (nowSize - wasSize),
			0);
	}

	const auto checkedCount = std::max(fullCount, nowSize);
	if (skippedBefore && skippedAfter) {
		auto &correct = toFront ? skippedBefore : skippedAfter;
		*correct = std::max(
			checkedCount - *decrementFrom - nowSize,
			0);
		*decrementFrom = checkedCount - *correct - nowSize;
		Assert(*decrementFrom >= 0);
	} else if (skippedBefore) {
		*skippedBefore = std::min(*skippedBefore, checkedCount - nowSize);
		skippedAfter = checkedCount - *skippedBefore - nowSize;
	} else if (skippedAfter) {
		*skippedAfter = std::min(*skippedAfter, checkedCount - nowSize);
		skippedBefore = checkedCount - *skippedAfter - nowSize;
	}
	return checkedCount;
}

} // namespace Data
