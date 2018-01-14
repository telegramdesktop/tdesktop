/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_groups.h"

#include "history/history_item.h"

namespace Data {

void Groups::registerMessage(not_null<HistoryItem*> item) {
	const auto groupId = item->groupId();
	if (!groupId) {
		return;
	}
	const auto i = _data.emplace(groupId, Group()).first;
	i->second.items.push_back(item);
}

void Groups::unregisterMessage(not_null<HistoryItem*> item) {
	const auto groupId = item->groupId();
	if (!groupId) {
		return;
	}
	const auto i = _data.find(groupId);
	if (i != _data.end()) {
		auto &group = i->second;
		group.items.erase(
			ranges::remove(group.items, item),
			group.items.end());
		if (group.items.empty()) {
			_data.erase(i);
		}
	}
}

const Group *Groups::find(not_null<HistoryItem*> item) const {
	const auto groupId = item->groupId();
	if (!groupId) {
		return nullptr;
	}
	const auto i = _data.find(groupId);
	return (i != _data.end()) ? &i->second : nullptr;
}

} // namespace Data
