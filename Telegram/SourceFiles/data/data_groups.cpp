/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_groups.h"

#include "history/history_item.h"
#include "data/data_media_types.h"
#include "data/data_session.h"

namespace Data {
namespace {

constexpr auto kMaxItemsInGroup = 10;

} // namespace

Groups::Groups(not_null<Session*> data) : _data(data) {
}

bool Groups::isGrouped(not_null<const HistoryItem*> item) const {
	if (!item->groupId()) {
		return false;
	}
	const auto media = item->media();
	return media && media->canBeGrouped();
}

void Groups::registerMessage(not_null<HistoryItem*> item) {
	if (!isGrouped(item)) {
		return;
	}
	const auto i = _groups.emplace(item->groupId(), Group()).first;
	auto &items = i->second.items;
	if (items.size() < kMaxItemsInGroup) {
		items.insert(findPositionForItem(items, item), item);
		if (items.size() > 1) {
			refreshViews(items);
		}
	}
}

void Groups::unregisterMessage(not_null<const HistoryItem*> item) {
	const auto groupId = item->groupId();
	if (!groupId) {
		return;
	}
	const auto i = _groups.find(groupId);
	if (i != end(_groups)) {
		auto &items = i->second.items;
		const auto removed = ranges::remove(items, item);
		const auto last = end(items);
		if (removed != last) {
			items.erase(removed, last);
			if (!items.empty()) {
				refreshViews(items);
			} else {
				_groups.erase(i);
			}
		}
	}
}

void Groups::refreshMessage(
		not_null<HistoryItem*> item,
		bool justRefreshViews) {
	if (!isGrouped(item)) {
		unregisterMessage(item);
		return;
	}
	if (!IsServerMsgId(item->id) && !item->isScheduled()) {
		return;
	}
	const auto groupId = item->groupId();
	const auto i = _groups.find(groupId);
	if (i == end(_groups)) {
		registerMessage(item);
		return;
	}
	auto &items = i->second.items;

	if (justRefreshViews) {
		refreshViews(items);
		return;
	}

	const auto position = findPositionForItem(items, item);
	auto current = ranges::find(items, item);
	if (current == end(items)) {
		items.insert(position, item);
	} else if (position == current + 1) {
		return;
	} else if (position > current + 1) {
		for (++current; current != position; ++current) {
			std::swap(*(current - 1), *current);
		}
	} else if (position < current) {
		for (; current != position; --current) {
			std::swap(*(current - 1), *current);
		}
	} else {
		Unexpected("Position of item in Groups::refreshMessage().");
	}
	refreshViews(items);
}

HistoryItemsList::const_iterator Groups::findPositionForItem(
		const HistoryItemsList &group,
		not_null<HistoryItem*> item) {
	const auto itemId = item->id;
	const auto last = end(group);
	if (!IsServerMsgId(itemId)) {
		return last;
	}
	for (auto result = begin(group); result != last; ++result) {
		const auto alreadyId = (*result)->id;
		if (IsServerMsgId(alreadyId) && alreadyId > itemId) {
			return result;
		}
	}
	return last;
}

const Group *Groups::find(not_null<const HistoryItem*> item) const {
	const auto groupId = item->groupId();
	if (!groupId) {
		return nullptr;
	}
	const auto i = _groups.find(groupId);
	if (i != _groups.end()) {
		const auto &result = i->second;
		if (result.items.size() > 1) {
			return &result;
		}
	}
	return nullptr;
}

void Groups::refreshViews(const HistoryItemsList &items) {
	for (const auto item : items) {
		_data->requestItemViewRefresh(item);
	}
}

not_null<HistoryItem*> Groups::findItemToEdit(
		not_null<HistoryItem*> item) const {
	const auto group = find(item);
	if (!group) {
		return item;
	}
	const auto &list = group->items;
	const auto it = ranges::find_if(
		list,
		ranges::not_fn(&HistoryItem::emptyText));
	if (it == end(list)) {
		return list.front();
	}
	return (*it);
}

} // namespace Data
