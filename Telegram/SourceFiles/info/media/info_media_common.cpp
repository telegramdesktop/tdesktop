/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_common.h"

#include "history/history_item.h"

namespace Info::Media {

UniversalMsgId GetUniversalId(FullMsgId itemId) {
	return peerIsChannel(itemId.peer)
		? UniversalMsgId(itemId.msg)
		: UniversalMsgId(itemId.msg - ServerMaxMsgId);
}

UniversalMsgId GetUniversalId(not_null<const HistoryItem*> item) {
	return GetUniversalId(item->fullId());
}

UniversalMsgId GetUniversalId(not_null<const BaseLayout*> layout) {
	return GetUniversalId(layout->getItem()->fullId());
}

bool ChangeItemSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> item,
		TextSelection selection) {
	const auto changeExisting = [&](auto it) {
		if (it == selected.cend()) {
			return false;
		} else if (it->second.text != selection) {
			it->second.text = selection;
			return true;
		}
		return false;
	};
	if (selected.size() < MaxSelectedItems) {
		const auto [i, ok] = selected.try_emplace(item, selection);
		if (ok) {
			// #TODO downloads
			i->second.canDelete = item->canDelete();
			i->second.canForward = item->allowsForward();
			return true;
		}
		return changeExisting(i);
	}
	return changeExisting(selected.find(item));
}

} // namespace Info::Media
